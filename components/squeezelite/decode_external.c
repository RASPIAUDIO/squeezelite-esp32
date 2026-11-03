/* 
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include <math.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#endif
#include "platform_config.h"
#include "squeezelite.h"
#include "audio_source.h"
#include "audio_controls.h"
#if CONFIG_IDF_TARGET_ESP32S31
#include "airplay2_sink.h"
#include "esp_timer.h"
#endif
#if CONFIG_MNCAST_SENDSPIN
#include "sendspin_sink.h"
#include "sendspin_pcm.h"
#include "display.h"
#include "esp_heap_caps.h"
static bool enable_sendspin;
static unsigned sendspin_input_bytes;
static uint32_t sendspin_probed_epoch;
#endif


#if CONFIG_BT_SINK
#include "bt_app_sink.h"
static bool enable_bt_sink;
#endif

#if CONFIG_CSPOT_SINK
#include "cspot_sink.h"
static bool enable_cspot;
#endif

#if CONFIG_AIRPLAY_SINK
#include "raop_sink.h"
static bool enable_airplay;

#define RAOP_OUTPUT_SIZE (((RAOP_SAMPLE_RATE * BYTES_PER_FRAME * 2 * 120) / 100) & ~BYTES_PER_FRAME)
#define SYNC_WIN_SLOW	32
#define SYNC_WIN_CHECK	8
#define SYNC_WIN_FAST	2

static raop_event_t	raop_state;
#if CONFIG_MNCAST_SENDSPIN
// RTP can outlive output ownership while RTSP teardown completes. It must not
// alias the shared PCM ring that a new source immediately reuses.
static uint8_t *raop_packet_storage;
#define RAOP_PACKET_STORAGE_BYTES (RAOP_SAMPLE_RATE * 4 * 3)
#endif

static EXT_RAM_BSS_ATTR struct {
	bool enabled;
	int sum, count, win, errors[SYNC_WIN_SLOW];
	s32_t len;
	u32_t start_time, playtime;
} raop_sync;
#endif

static enum { SINK_RUNNING, SINK_ABORT, SINK_DISCARD } sink_state;

#define LOCK_O   mutex_lock(outputbuf->mutex)
#define UNLOCK_O mutex_unlock(outputbuf->mutex)
#define LOCK_D   mutex_lock(decode.mutex);
#define UNLOCK_D mutex_unlock(decode.mutex);

enum { DECODE_BT = 1, DECODE_RAOP, DECODE_CSPOT, DECODE_SENDSPIN, DECODE_AP2 };

extern struct outputstate output;
extern struct decodestate decode;
extern struct buffer *outputbuf;
// this is the only system-wide loglevel variable
extern log_level loglevel;

/****************************************************************************************
 * Common sink data handler
 */
static uint32_t sink_data_handler_width(const uint8_t *data, uint32_t len, int retries, int source, unsigned input_frame_bytes)
{
    size_t bytes, space;
    uint32_t written = 0;    
	int wait = retries + 1;

    if ((input_frame_bytes != 4 && input_frame_bytes != 8) || len % input_frame_bytes) {
        return 0;
    }
		
	LOCK_O;
	// A rejected source can still deliver queued PCM. Only the current owner
	// may write, including after the lock is released while waiting for space.
	if (output.external != source) {
		LOG_SDEBUG("Discarding PCM from inactive sink %d (owner %d)", source, output.external);
		UNLOCK_O;
		return 0;
	} 

	if (sink_state == SINK_ABORT) sink_state = SINK_RUNNING;

	// there will always be room at some point
	while (len && wait && sink_state == SINK_RUNNING && output.external == source) {
		bytes = min(_buf_space(outputbuf), _buf_cont_write(outputbuf)) / BYTES_PER_FRAME * input_frame_bytes;
		bytes = min(len, bytes);
        if (input_frame_bytes == BYTES_PER_FRAME) {
            memcpy(outputbuf->writep, data, bytes);
        } else if (input_frame_bytes == 4) {
			s16_t *iptr = (s16_t*) data;
			s32_t *optr = (s32_t *) outputbuf->writep;
			size_t n = bytes / 2;
			while (n--) *optr++ = (s32_t)*iptr++ * 65536;
		} else {
            const s32_t *iptr = (const s32_t *)data;
            s16_t *optr = (s16_t *)outputbuf->writep;
            size_t n = bytes / 4;
            while (n--) *optr++ = *iptr++ >> 16;
		}
		_buf_inc_writep(outputbuf, bytes / input_frame_bytes * BYTES_PER_FRAME);
		space = _buf_space(outputbuf);

		len -= bytes;
		data += bytes;
        written += bytes;
				
		// allow i2s to empty the buffer if needed
		if (len && space < BYTES_PER_FRAME) {
            if (!retries) break;
			wait--;
			UNLOCK_O; usleep(50000); LOCK_O;
		}
	}	

	if (!wait && output.external == source) {
        // Dropping whole input frames must not move the output ring pointer.
		LOG_WARN("Waited too long, dropping %u input bytes", (unsigned)len);
	}
    
    UNLOCK_O;
    
    return written;
}

static uint32_t sink_data_handler(const uint8_t *data, uint32_t len, int retries, int source) {
    return sink_data_handler_width(data, len, retries, source, 4);
}

/****************************************************************************************
 * BT sink data handler
 */
#if CONFIG_BT_SINK
#if CONFIG_MNCAST_SENDSPIN
static unsigned bt_gain = FIXED_ONE;
#endif
static void bt_sink_data_handler(const uint8_t *data, uint32_t len) {
    sink_data_handler(data, len, 10, DECODE_BT);
}    

/****************************************************************************************
 * BT sink command handler
 */
static bool bt_sink_cmd_handler(bt_sink_cmd_t cmd, va_list args) 
{
#if !CONFIG_MNCAST_SENDSPIN
	// don't LOCK_O as there is always a chance that LMS takes control later anyway
	if (output.external != DECODE_BT && output.state > OUTPUT_STOPPED) {
		LOG_WARN("Cannot use BT sink while LMS/AirPlay/CSpot are controlling player %d", output.external);
		return false;
	} 	
#endif

	LOCK_D;

	if (cmd != BT_SINK_VOLUME) LOCK_O;

#if CONFIG_MNCAST_SENDSPIN
    // Only a playback START may acquire output. A connected/paused phone's
    // queued STOP, RATE, metadata, etc. cannot affect a newer source.
    if (cmd != BT_SINK_AUDIO_STARTED && cmd != BT_SINK_VOLUME && output.external != DECODE_BT) {
        UNLOCK_O; UNLOCK_D; return false;
    }
#endif
		
	switch(cmd) {
	case BT_SINK_AUDIO_STARTED:
        audio_source_changed();
		_buf_flush(outputbuf);
		_buf_limit(outputbuf, 0);
		output.next_sample_rate = output.current_sample_rate = va_arg(args, u32_t);
		output.external = DECODE_BT;
		output.state = OUTPUT_STOPPED;
		output.frames_played = 0;
#if CONFIG_MNCAST_SENDSPIN
        output.track_start = NULL;
        output.fade = FADE_INACTIVE;
#endif
		if (decode.state != DECODE_STOPPED) decode.state = DECODE_ERROR;
		LOG_INFO("BT sink started");
		break;
	case BT_SINK_AUDIO_STOPPED:	
		if (output.external == DECODE_BT) {
            audio_source_changed();
			if (output.state > OUTPUT_STOPPED) output.state = OUTPUT_STOPPED;
			output.external = 0;
			output.stop_time = gettime_ms();
			LOG_INFO("BT sink stopped");
		}	
		break;
	case BT_SINK_PLAY:
		output.state = OUTPUT_RUNNING;
		LOG_INFO("BT play");
		break;
	case BT_SINK_STOP:		
		_buf_flush(outputbuf);
		output.state = OUTPUT_STOPPED;
		output.stop_time = gettime_ms();
		sink_state = SINK_ABORT;
		LOG_INFO("BT stop");
		break;
	case BT_SINK_PAUSE:		
		output.stop_time = gettime_ms();
		LOG_INFO("BT pause, just silence");
		break;
	case BT_SINK_RATE:
		output.next_sample_rate = output.current_sample_rate = va_arg(args, u32_t);
		LOG_INFO("Setting BT sample rate %u", output.next_sample_rate);
		break;
	case BT_SINK_VOLUME: {
		u32_t volume = va_arg(args, u32_t);
		volume = 65536 * powf(volume / 128.0f, 3);
#if CONFIG_MNCAST_SENDSPIN
        bt_gain = volume;
        set_volume_owned(DECODE_BT, bt_gain, bt_gain);
#else
		set_volume(volume, volume);
#endif
		break;
	default:
		break;
	}
	}
	
	if (cmd != BT_SINK_VOLUME) UNLOCK_O;
#if CONFIG_MNCAST_SENDSPIN
    if (cmd == BT_SINK_AUDIO_STARTED) set_volume_owned(DECODE_BT, bt_gain, bt_gain);
#endif
	UNLOCK_D;

	return true;
}

static void bt_sink_data32_handler(const uint8_t *data, uint32_t len) {
    sink_data_handler_width(data, len, 10, DECODE_BT, 8);
}
#endif

/****************************************************************************************
 * raop sink data handler
 */
#if CONFIG_AIRPLAY_SINK
static void raop_sink_data_handler(const uint8_t *data, size_t len, u32_t playtime) {
	
	raop_sync.playtime = playtime;
	raop_sync.len = len;

	sink_data_handler(data, len, 10, DECODE_RAOP);
}	

/****************************************************************************************
 * AirPlay sink command handler
 */
#if CONFIG_MNCAST_SENDSPIN
static unsigned raop_gain = FIXED_ONE;
#endif

static bool raop_sink_cmd_handler(raop_event_t event, va_list args)
{
#if !CONFIG_MNCAST_SENDSPIN
	// don't LOCK_O as there is always a chance that LMS takes control later anyway
	if (output.external != DECODE_RAOP && output.state > OUTPUT_STOPPED) {
		LOG_WARN("Cannot use Airplay sink while LMS/BT/CSpot are controlling player %d", output.external);
		return false;
	} 	
#endif

	LOCK_D;
	
	if (event != RAOP_VOLUME) LOCK_O;

#if CONFIG_MNCAST_SENDSPIN
    /* SETUP only reserves independent RTP storage. RECORD is the playback
     * request; all subsequent commands must still own the output, even when
     * the new owner is paused. Volume is cached and applied atomically below. */
    if (event != RAOP_SETUP && event != RAOP_VOLUME) {
        bool allowed = event == RAOP_STREAM || output.external == DECODE_RAOP;
        if (!allowed) {
            UNLOCK_O; UNLOCK_D;
            return false;
        }
    }
#endif
	
	// this is async, so player might have been deleted
	switch (event) {
		case RAOP_TIMING: {
			if (!raop_sync.enabled || output.state != OUTPUT_RUNNING || output.frames_played_dmp < output.device_frames) break;

			u32_t ms, now = gettime_ms();
			u32_t level = _buf_used(outputbuf);
			int error;
				
			// in how many ms will the most recent block play 
			ms = (((s32_t)(level - raop_sync.len) / BYTES_PER_FRAME + output.device_frames + output.frames_in_process) * 10) / (RAOP_SAMPLE_RATE / 100) - (s32_t) (now - output.updated);
				
			// when outputbuf is empty, it means we have a network black-out or something
			error = level ? (raop_sync.playtime - now) - ms : 0;
				
			if (loglevel == lDEBUG || !level) {
				LOG_INFO("head local:%d, remote:%d (delta:%d)", ms, raop_sync.playtime - now, error);
				LOG_INFO("obuf:%u, sync_len:%u, devframes:%u, inproc:%u", _buf_used(outputbuf), raop_sync.len, output.device_frames, output.frames_in_process);
			}	
			
			// calculate sum, error and update sliding window
			raop_sync.errors[raop_sync.count++ % raop_sync.win] = error;
			raop_sync.sum += error;
			error = raop_sync.sum / min(raop_sync.count, raop_sync.win);

			// wait till we have enough data or there is a strong deviation
			if ((raop_sync.count >= raop_sync.win && abs(error) > 10) || (raop_sync.count >= SYNC_WIN_CHECK && abs(error) > 100)) {
				if (error < 0) {
					output.skip_frames = -(error * RAOP_SAMPLE_RATE) / 1000;
					output.state = OUTPUT_SKIP_FRAMES;					
					LOG_INFO("skipping %u frames (count:%d)", output.skip_frames, raop_sync.count);
				} else {
					output.pause_frames = (error * RAOP_SAMPLE_RATE) / 1000;
					output.state = OUTPUT_PAUSE_FRAMES;
					LOG_INFO("pausing for %u frames (count: %d)", output.pause_frames, raop_sync.count);
				}
				
				raop_sync.sum = raop_sync.count = 0;
				memset(raop_sync.errors, 0, sizeof(raop_sync.errors));
			}	
			
			// move to normal mode if possible			
			if (raop_sync.win == 1) {
				raop_sync.win = SYNC_WIN_FAST;
				LOG_INFO("backend played %u, desired %u, (delta:%d)", ms, raop_sync.playtime - now, error);
			} else if (raop_sync.win == SYNC_WIN_FAST && raop_sync.count >= SYNC_WIN_FAST && abs(error) < 10) {
				raop_sync.win = SYNC_WIN_SLOW;
				LOG_INFO("switching to slow sync mode %u", raop_sync.win);
			}	

			break;
		}
		case RAOP_SETUP: {
			uint8_t **buffer = va_arg(args, uint8_t**);
			size_t *size = va_arg(args, size_t*);


#if CONFIG_MNCAST_SENDSPIN
            if (!raop_packet_storage) {
                raop_packet_storage = heap_caps_malloc(RAOP_PACKET_STORAGE_BYTES,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
            if (!raop_packet_storage) {
                LOG_WARN("Cannot allocate independent AirPlay RTP storage");
                UNLOCK_O; UNLOCK_D; return false;
            }
            *buffer = raop_packet_storage;
            *size = RAOP_PACKET_STORAGE_BYTES;
            raop_gain = FIXED_ONE;
            LOG_INFO("AirPlay independent RTP storage: %u bytes", (unsigned)*size);
#else
			// steal buffer tail from outputbuf but do not reallocate
			*size = _buf_limit(outputbuf, RAOP_OUTPUT_SIZE);
			*buffer = outputbuf->writep + RAOP_OUTPUT_SIZE;

			output.frames_played = 0;
			output.external = DECODE_RAOP;
			output.state = OUTPUT_STOPPED;

			if (decode.state != DECODE_STOPPED) decode.state = DECODE_ERROR;
			LOG_INFO("resizing buffer %u", outputbuf->size);
#endif
			break;
		}
		case RAOP_STREAM:
#if CONFIG_MNCAST_SENDSPIN
            audio_source_changed();
            _buf_flush(outputbuf);
            _buf_limit(outputbuf, 0);
            output.track_start = NULL;
            output.fade = FADE_INACTIVE;
            output.frames_played = 0;
            output.external = DECODE_RAOP;
            output.state = OUTPUT_STOPPED;
            if (decode.state != DECODE_STOPPED) decode.state = DECODE_ERROR;
#endif
			LOG_INFO("Stream", NULL);
			raop_state = event;
			raop_sync.win = 1;
			raop_sync.sum = raop_sync.count = 0;
			memset(raop_sync.errors, 0, sizeof(raop_sync.errors));
			raop_sync.enabled = !strcasestr(output.device, "BT");
			output.next_sample_rate = output.current_sample_rate = RAOP_SAMPLE_RATE;
			break;
        case RAOP_STALLED:
		case RAOP_STOP:
            audio_source_changed();
			output.external = 0;
			__attribute__ ((fallthrough));
		case RAOP_FLUSH:
			LOG_INFO("%s", event == RAOP_FLUSH ? "Flush" : "Stop");
			_buf_flush(outputbuf);
			raop_state = event;
			if (output.state > OUTPUT_STOPPED) output.state = OUTPUT_STOPPED;
			sink_state = SINK_ABORT;
			output.frames_played = 0;
			output.stop_time = gettime_ms();
			break;
		case RAOP_PLAY: {
			LOG_INFO("Play", NULL);
			if (raop_state != RAOP_PLAY) {
				output.state = OUTPUT_START_AT;
				output.start_at = va_arg(args, u32_t);
				raop_sync.start_time = output.start_at;
				LOG_INFO("Starting at %u (in %d ms)", output.start_at, output.start_at - gettime_ms());
			}
			raop_state = event;
			break;
		}
		case RAOP_VOLUME: {
			float volume = va_arg(args, double);
			LOG_INFO("Volume[0..1] %0.4f", volume);
			volume = 65536 * powf(volume, 3);
#if CONFIG_MNCAST_SENDSPIN
            raop_gain = volume;
            set_volume_owned(DECODE_RAOP, raop_gain, raop_gain);
#else
			set_volume(volume, volume);
#endif
			break;
		}
		default:
			break;
	}
	
	if (event != RAOP_VOLUME) UNLOCK_O;

#if CONFIG_MNCAST_SENDSPIN
    if (event == RAOP_STREAM) set_volume_owned(DECODE_RAOP, raop_gain, raop_gain);
#endif
	
	UNLOCK_D;
	return true;
}
#endif

/****************************************************************************************
 * cspot sink data handler
 */
#if CONFIG_CSPOT_SINK
static uint32_t cspot_sink_data_handler(const uint8_t *data, size_t len) {
    return sink_data_handler(data, len, 0, DECODE_CSPOT);
}    

/****************************************************************************************
 * cspot sink command handler
 */

static bool cspot_cmd_handler(cspot_event_t cmd, va_list args) 
{
#if !CONFIG_MNCAST_SENDSPIN
	// don't LOCK_O as there is always a chance that LMS takes control later anyway
	if (output.external != DECODE_CSPOT && output.state > OUTPUT_STOPPED) {
		LOG_WARN("Cannot use CSpot sink while LMS/BT/Airplay are controlling player %d", output.external);
		return false;
	} 	
#endif

	LOCK_D;
	
	if (cmd != CSPOT_VOLUME) LOCK_O;
#if CONFIG_MNCAST_SENDSPIN
    if (cmd != CSPOT_START && cmd != CSPOT_VOLUME && output.external != DECODE_CSPOT) {
        UNLOCK_O; UNLOCK_D; return false;
    }
#endif

	switch(cmd) {
	case CSPOT_START:
        audio_source_changed();
		output.current_sample_rate = output.next_sample_rate = va_arg(args, u32_t);
		output.external = DECODE_CSPOT;
		output.frames_played = 0;
#if CONFIG_MNCAST_SENDSPIN
        output.track_start = NULL;
        output.fade = FADE_INACTIVE;
#endif
        // in 1/10 of seconds
        output.threshold = 25;
		output.state = OUTPUT_STOPPED;
        sink_state = SINK_ABORT;
		_buf_flush(outputbuf);
        _buf_limit(outputbuf, 0);
		if (decode.state != DECODE_STOPPED) decode.state = DECODE_ERROR;
		LOG_INFO("CSpot start track");
		break;
	case CSPOT_DISC:
        audio_source_changed();
		_buf_flush(outputbuf);
		sink_state = SINK_ABORT;
		output.external = 0;
		output.state = OUTPUT_STOPPED;
		output.stop_time = gettime_ms();
		LOG_INFO("CSpot disconnected");
		break;
	case CSPOT_PLAY:
		sink_state = SINK_RUNNING;			
		output.state = OUTPUT_RUNNING;
		LOG_INFO("CSpot play");
		break;
	case CSPOT_SEEK:
		_buf_flush(outputbuf);		
		sink_state = SINK_ABORT;
		LOG_INFO("CSpot seek by %d", va_arg(args, uint32_t));
		break;
	case CSPOT_FLUSH:
		_buf_flush(outputbuf);
		sink_state = SINK_DISCARD;
		output.state = OUTPUT_STOPPED;
		LOG_INFO("CSpot flush");	
		break;		
	case CSPOT_PAUSE:		
		output.state = OUTPUT_STOPPED;
		output.stop_time = gettime_ms();
		LOG_INFO("CSpot pause");
		break;
    case CSPOT_TRACK_MARK:
        output.track_start = outputbuf->writep;
        break;
    case CSPOT_QUERY_REMAINING: {
        uint32_t *remaining = va_arg(args, uint32_t*);
        *remaining = (_buf_used(outputbuf) * 1000) / (output.current_sample_rate * BYTES_PER_FRAME);
        break;      
    }
    case CSPOT_QUERY_STARTED: {
        uint32_t *started = va_arg(args, uint32_t*);
        *started = output.track_started;
        // this is a read_and_clear event
        output.track_started = false;
        break;      
    }
	case CSPOT_VOLUME: {
		u32_t volume = va_arg(args, u32_t);
		LOG_INFO("CSpot volume %u", volume);
		volume = 65536 * powf(volume / 65536.0f, 4);
#if CONFIG_MNCAST_SENDSPIN
        set_volume_owned(DECODE_CSPOT, volume, volume);
#else
		set_volume(volume, volume);
#endif
		break;
	default:
		break;
	}
	}
	
	if (cmd != CSPOT_VOLUME) UNLOCK_O;
	UNLOCK_D;
	
	return true;
}
#endif

/****************************************************************************************
 * We provide the generic codec register option
 */
#if CONFIG_MNCAST_SENDSPIN
static bool sendspin_available(void) {
    // A new stream/start is allowed to take priority, just like LMS/RECORD.
    return true;
}

static bool sendspin_owns(uint32_t epoch) {
    LOCK_O;
    bool owns = epoch && epoch == mn_sendspin_output_epoch && output.external == DECODE_SENDSPIN;
    UNLOCK_O;
    return owns;
}

static uint32_t sendspin_begin(uint32_t rate, uint8_t depth, uint8_t channels) {
    if (channels != 2 || (depth != 16 && depth != 24 && depth != 32) ||
        (rate != 44100 && rate != 48000 && rate != 88200 && rate != 96000 &&
         rate != 176400 && rate != 192000)) return 0;
#if BYTES_PER_FRAME == 4
    if (depth != 16) return 0;
#endif
    audio_source_lock();
    LOCK_D; LOCK_O;
    audio_source_changed();
    _buf_flush(outputbuf);
    _buf_limit(outputbuf, 0);
    output.track_start = NULL;
    output.fade = FADE_INACTIVE;
    output.current_sample_rate = output.next_sample_rate = rate;
    output.external = DECODE_SENDSPIN;
    output.frames_played = output.frames_played_dmp = 0;
    output.state = OUTPUT_RUNNING;
    sink_state = SINK_RUNNING;
    sendspin_input_bytes = channels * (depth / 8);
    if (++mn_sendspin_output_epoch == 0) ++mn_sendspin_output_epoch;
    uint32_t epoch = mn_sendspin_output_epoch;
    if (decode.state != DECODE_STOPPED) decode.state = DECODE_ERROR;
    UNLOCK_O; UNLOCK_D;
    displayer_control(DISPLAYER_ACTIVATE, "SENDSPIN", true);
    displayer_progress(false, 0);
    actrls_unset();
    audio_source_unlock();
    return epoch;
}

static size_t sendspin_write(uint32_t epoch, const uint8_t *data, size_t len, uint32_t timeout_ms) {
    uint32_t start = gettime_ms();
    size_t done = 0;
    do {
        LOCK_O;
        bool owner = epoch && epoch == mn_sendspin_output_epoch && output.external == DECODE_SENDSPIN && output.state == OUTPUT_RUNNING;
        if (!owner || !sendspin_input_bytes || len % sendspin_input_bytes) {
            UNLOCK_O; break;
        }
        // Bound downstream latency to 100 ms, rather than the multi-second LMS ring.
        size_t cap = output.current_sample_rate / 10 * BYTES_PER_FRAME;
        size_t used = _buf_used(outputbuf);
        size_t room = owner && used < cap ? (cap-used)/BYTES_PER_FRAME*sendspin_input_bytes : 0;
        // Keep ownership and writing in the same critical section. A queued
        // write from an earlier stream must never reach a newly opened stream.
        size_t bytes = min(len-done,room);
        bytes = min(bytes, _buf_cont_write(outputbuf)/BYTES_PER_FRAME*sendspin_input_bytes);
        bytes = min(bytes, _buf_space(outputbuf)/BYTES_PER_FRAME*sendspin_input_bytes);
        if (owner && bytes) {
            unsigned width = sendspin_input_bytes / 2;
            unsigned nonzero=0, lowbyte=0;
            bool probe=sendspin_probed_epoch!=epoch;
            for (size_t i=0; i<bytes/width; ++i) {
                const uint8_t *sample=data+done+width*i;
                if (probe) {
                    uint32_t value=0;
                    for(unsigned b=0;b<width;++b) value|=sample[b];
                    nonzero+=value!=0;
                    lowbyte+=sample[0]!=0;
                }
#if BYTES_PER_FRAME == 8
                ((int32_t *)outputbuf->writep)[i]=mn_sendspin_pcm32(sample,width);
#else
                ((int16_t *)outputbuf->writep)[i]=(int16_t)((uint16_t)sample[0] | (uint16_t)sample[1]<<8);
#endif
            }
            if(probe && nonzero) {
                sendspin_probed_epoch=epoch;
                LOG_INFO("Sendspin input PCM bits:%u samples:%u nonzero:%u lowbyte_nonzero:%u",
                         width*8,(unsigned)(bytes/width),nonzero,lowbyte);
            }
            _buf_inc_writep(outputbuf,bytes/sendspin_input_bytes*BYTES_PER_FRAME);
            done+=bytes;
        }
        UNLOCK_O;
        if (!owner) break;
        if (done == len || gettime_ms()-start >= timeout_ms) break;
        vTaskDelay(1);
    } while (true);
    return done;
}

static void sendspin_end(uint32_t epoch) {
    audio_source_lock();
    LOCK_D; LOCK_O;
    bool owner = epoch && epoch == mn_sendspin_output_epoch && output.external == DECODE_SENDSPIN;
    if (owner) {
        audio_source_changed();
        _buf_flush(outputbuf); sink_state = SINK_ABORT;
        output.external = 0; output.state = OUTPUT_STOPPED;
        output.stop_time = gettime_ms();
    }
    UNLOCK_O; UNLOCK_D;
    if (owner) displayer_control(DISPLAYER_SHUTDOWN);
    audio_source_unlock();
}

static void sendspin_volume(uint32_t epoch, uint8_t percent, bool muted) {
    mn_sendspin_volume_epoch(epoch, percent, muted);
}
static void sendspin_metadata(uint32_t epoch, const char *artist,const char *album,const char *title) {
    // Metadata only formats the display string; unlike display control/artwork
    // it does not call the display bus. Keep ownership stable through the update.
    LOCK_O;
    if (epoch && epoch == mn_sendspin_output_epoch && output.external == DECODE_SENDSPIN)
        displayer_metadata((char *)artist,(char *)album,(char *)title);
    UNLOCK_O;
}
static bool sendspin_image_show(uint32_t epoch, void *image) {
    LOCK_O;
    bool shown = epoch && epoch == mn_sendspin_output_epoch && output.external == DECODE_SENDSPIN &&
        displayer_image_show(image);
    UNLOCK_O;
    return shown;
}
static void sendspin_progress(uint32_t epoch, bool known, uint32_t position_ms) {
    LOCK_O;
    if (epoch && epoch == mn_sendspin_output_epoch && output.external == DECODE_SENDSPIN)
        displayer_progress(known, position_ms);
    UNLOCK_O;
}
#endif

#if CONFIG_IDF_TARGET_ESP32S31
#include "external/airplay2_output.inc"
#endif

void register_external(void) {
	char *p;
#if CONFIG_IDF_TARGET_ESP32S31
    mn_airplay2_configure(&ap2_output_ops);
#endif
#if CONFIG_MNCAST_SENDSPIN
    p = config_alloc_get(NVS_TYPE_STR, "enable_sendspin");
    enable_sendspin = p && (!strcmp(p,"1") || !strcasecmp(p,"y"));
    free(p);
    enable_sendspin = mn_sendspin_enabled(enable_sendspin);
    if (enable_sendspin) {
        const mn_sendspin_output_t ops = {sendspin_begin,sendspin_write,sendspin_end,
            sendspin_available,sendspin_volume,sendspin_metadata,
            displayer_image_decode,sendspin_image_show,displayer_image_free,sendspin_owns,sendspin_progress};
        if (!mn_sendspin_init(&ops)) LOG_WARN("Sendspin task creation failed");
    }
#endif

#if CONFIG_BT_SINK
	if ((p = config_alloc_get(NVS_TYPE_STR, "enable_bt_sink")) != NULL) {
		enable_bt_sink = !strcmp(p,"1") || !strcasecmp(p,"y");
		free(p);
		if (!strcasestr(output.device, "BT") && enable_bt_sink) {
			bt_sink_init_with_pcm32(bt_sink_cmd_handler, bt_sink_data_handler, bt_sink_data32_handler);
		}	
	}
#endif	

#if CONFIG_AIRPLAY_SINK
	if ((p = config_alloc_get(NVS_TYPE_STR, "enable_airplay")) != NULL) {
		enable_airplay = !strcmp(p,"1") || !strcasecmp(p,"y");
		free(p);
		if (enable_airplay){
			raop_sink_init(raop_sink_cmd_handler, raop_sink_data_handler);
			LOG_INFO("Initializing AirPlay sink");
		}
	}
#endif	
	
#if CONFIG_CSPOT_SINK	
	if ((p = config_alloc_get(NVS_TYPE_STR, "enable_cspot")) != NULL) {
		enable_cspot = strcmp(p,"1") == 0 || strcasecmp(p,"y") == 0;
		free(p);
		if (enable_cspot){
			cspot_sink_init(cspot_cmd_handler, cspot_sink_data_handler);
			LOG_INFO("Initializing CSpot sink");
		}	
	}	
#endif	
}

void deregister_external(void) {
#if CONFIG_MNCAST_SENDSPIN
    if (enable_sendspin) mn_sendspin_deinit();
#endif
#if CONFIG_BT_SINK
	if (!strcasestr(output.device, "BT") && enable_bt_sink) {
		bt_sink_deinit();
	}
#endif

#if CONFIG_AIRPLAY_SINK
	if (enable_airplay){
		LOG_INFO("Stopping AirPlay sink");		
		raop_sink_deinit();
#if CONFIG_MNCAST_SENDSPIN
        // raop_sink_deinit joins RTSP/RTP before storage can be released.
        heap_caps_free(raop_packet_storage);
        raop_packet_storage = NULL;
#endif
	}
#endif

#if CONFIG_CSPOT_SINK
	if (enable_cspot){
		LOG_INFO("Stopping CSpot sink");		
		cspot_sink_deinit();
	}
#endif
}

void decode_restore(int external) {
	switch (external) {
#if CONFIG_IDF_TARGET_ESP32S31
    case DECODE_AP2:
        mn_airplay2_disconnect();
        break;
#endif
#if CONFIG_BT_SINK		
	case DECODE_BT:
		bt_disconnect();
		break;
#endif
#if CONFIG_AIRPLAY_SINK
	case DECODE_RAOP:
		raop_disconnect();
		break;
#endif
#if CONFIG_CSPOT_SINK
	case DECODE_CSPOT:
		cspot_disconnect();
		break;
#endif			
	}
}
