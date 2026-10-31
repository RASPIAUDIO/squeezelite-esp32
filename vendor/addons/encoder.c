/*
 * encoder : simple 2 channels 16 bits encoder serie
 * 
  * (c) Philippe, philippe_44@outlook.com
 *
 * See LICENSE
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "FLAC/stream_encoder.h"
#include "layer3.h"
#include "faac.h"

#include "encoder.h"

#ifdef _WIN32
#define strncasecmp strnicmp
#else
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

// this can be made an opaque structure
struct encoder_s {
	enum { CODEC_MP3 = 0, CODEC_AAC, CODEC_FLAC, CODEC_PCM, CODEC_WAV } format;
	uint32_t sample_rate;
	char* mimetype;
	void* codec;
	int16_t* buffer;
	size_t max_frames;
	uint8_t* data;
	size_t count, bytes;
	uint8_t* (*encode)(struct encoder_s* encoder, int16_t* pcm, size_t frames, size_t* bytes);
	bool (*open)(struct encoder_s* encoder);
	void (*close)(struct encoder_s* encoder);
	union {
		struct {
			bool header;
		} wav;
		struct {
			int bitrate;
		} mp3;
		struct {
			int level;
			size_t size;
			int blocksize;
		} flac;
		struct {
			unsigned long in_samples, out_max_bytes;
			int bitrate;
		} aac;
	};
};

static struct wave_header_s {
	uint8_t	chunk_id[4];
	uint8_t	chunk_size[4];
	uint8_t	format[4];
	uint8_t	subchunk1_id[4];
	uint8_t	subchunk1_size[4];
	uint8_t	audio_format[2];
	uint8_t	channels[2];
	uint8_t	sample_rate[4];
	uint8_t byte_rate[4];
	uint8_t	block_align[2];
	uint8_t	bits_per_sample[2];
	uint8_t	subchunk2_id[4];
	uint8_t	subchunk2_size[4];
} wave_header = {
		{ 'R', 'I', 'F', 'F' },
		{ 0x24, 0xff, 0xff, 0xff },
		{ 'W', 'A', 'V', 'E' },
		{ 'f','m','t',' ' },
		{ 16, 0, 0, 0 },
		{ 1, 0 },
		{ 2, 0 },
		{ 0x44, 0xac, 0x00, 0x00  },
		{ 0x10, 0xb1, 0x02, 0x00 },
		{ 4, 0 },
		{ 16, 0 },
		{ 'd', 'a', 't', 'a' },
		{ 0x00, 0xff, 0xff, 0xff },
};

/*---------------------------------------------------------------------------*/
static FLAC__StreamEncoderWriteStatus flac_write_callback(const FLAC__StreamEncoder* codec, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void* client_data) {
	struct encoder_s * encoder = (struct encoder_s*)client_data;

	if (encoder->bytes + bytes > encoder->flac.size) {
		encoder->flac.size = encoder->bytes + bytes + 1024;
		encoder->data = realloc(encoder->data, encoder->flac.size);
	}

	memcpy(encoder->data + encoder->bytes, buffer, bytes);
	encoder->bytes += bytes;

	return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

/*---------------------------------------------------------------------------*/
static bool flac_open(struct encoder_s * encoder) {
	bool ok = true;

	FLAC__StreamEncoder* codec = FLAC__stream_encoder_new();
	if (!codec) return false;

	encoder->codec = codec;
	encoder->flac.size = encoder->flac.blocksize * 4 + 1024;
	encoder->data = malloc(encoder->flac.size);
	encoder->max_frames = max(encoder->max_frames, 2 * ENCODER_MAX_FRAMES * sizeof(FLAC__int32) * 2);
	encoder->buffer = malloc(encoder->max_frames * 4);

	ok &= FLAC__stream_encoder_set_verify(codec, false);
	ok &= FLAC__stream_encoder_set_compression_level(codec, encoder->flac.level);
	ok &= FLAC__stream_encoder_set_channels(codec, 2);
	ok &= FLAC__stream_encoder_set_bits_per_sample(codec, 16);
	ok &= FLAC__stream_encoder_set_sample_rate(codec, encoder->sample_rate);
	ok &= FLAC__stream_encoder_set_blocksize(codec, encoder->flac.blocksize);
	ok &= FLAC__stream_encoder_set_streamable_subset(codec, true);
	ok &= !FLAC__stream_encoder_init_stream(codec, flac_write_callback, NULL, NULL, NULL, encoder);

	if (!ok) FLAC__stream_encoder_delete(codec);

	return ok;
}

/*---------------------------------------------------------------------------*/
static bool mp3_open(struct encoder_s* encoder) {
	shine_config_t config;

	shine_set_config_mpeg_defaults(&config.mpeg);
	config.wave.samplerate = encoder->sample_rate;
	config.wave.channels = 2;
	config.mpeg.bitr = encoder->mp3.bitrate;
	config.mpeg.mode = STEREO;

	encoder->codec = shine_initialise(&config);
	if (!encoder->codec) return false;

	// we should not have more than 2 blocks to buffer and the result is much less than 
	encoder->max_frames = max(encoder->max_frames, SHINE_MAX_SAMPLES * 2);
	encoder->buffer = malloc(encoder->max_frames * 4);
	encoder->data = malloc(encoder->max_frames * 4);

	return true;
}

/*---------------------------------------------------------------------------*/
static bool aac_open(struct encoder_s* encoder) {
	encoder->codec = (void*)faacEncOpen(encoder->sample_rate, 2, &encoder->aac.in_samples, &encoder->aac.out_max_bytes);
	if (!encoder->codec) return false;

	encoder->max_frames = max(encoder->max_frames, encoder->aac.in_samples * 2);
	encoder->buffer = malloc(encoder->max_frames * 4);
	encoder->data = malloc(encoder->aac.out_max_bytes);

	faacEncConfigurationPtr format = faacEncGetCurrentConfiguration(encoder->codec);
	format->bitRate = encoder->aac.bitrate * 1000 / 2;
	format->mpegVersion = MPEG4;
	format->bandWidth = 0;
	format->outputFormat = ADTS_STREAM;
	format->inputFormat = FAAC_INPUT_16BIT;
	faacEncSetConfiguration(encoder->codec, format);

	return true;
}

/*---------------------------------------------------------------------------*/
static bool wav_open(struct encoder_s* encoder) {
	encoder->max_frames = max(encoder->max_frames, 2 * ENCODER_MAX_FRAMES + sizeof(wave_header));
	encoder->wav.header = true;
	encoder->data = malloc(encoder->max_frames);

	return true;
}

/*---------------------------------------------------------------------------*/
static void mp3_close(struct encoder_s* encoder) {
	int len;
	shine_flush(encoder->codec, &len);
	shine_close(encoder->codec);
}

/*---------------------------------------------------------------------------*/
static void aac_close(struct encoder_s* encoder) {
	faacEncEncode(encoder->codec, NULL, 0, encoder->data, encoder->aac.out_max_bytes);
	faacEncClose(encoder->codec);
}

/*---------------------------------------------------------------------------*/
static void flac_close(struct encoder_s* encoder) {
	FLAC__stream_encoder_finish(encoder->codec);
	FLAC__stream_encoder_delete(encoder->codec);
}

/*---------------------------------------------------------------------------*/
static uint8_t *flac_encode(struct encoder_s *encoder, int16_t *pcm, size_t frames, size_t *bytes) {
	for (size_t i = 0; i < frames * 2; i++) ((FLAC__int32*)encoder->buffer)[i] = pcm[i];
	FLAC__stream_encoder_process_interleaved(encoder->codec, (FLAC__int32*)encoder->buffer, frames);

	// callback has filled encoded data but might have reallocated it
	*bytes = encoder->bytes;
	encoder->bytes = 0;
	return encoder->data;
}

/*---------------------------------------------------------------------------*/
static uint8_t* mp3_encode(struct encoder_s* encoder, int16_t* pcm, size_t frames, size_t* bytes) {
	size_t block_size = shine_samples_per_pass(encoder->codec);
	memcpy(encoder->buffer + encoder->count * 2, pcm, frames * 4);
	encoder->count += frames;
	*bytes = 0;

	// encode all full block to not accumulate pcm
	while (encoder->count >= block_size) {
		int written;
		uint8_t* encoded = shine_encode_buffer_interleaved(encoder->codec, encoder->buffer, &written);
		memcpy(encoder->data + *bytes, encoded, written);

		*bytes += written;
		encoder->count -= block_size;
		memmove(encoder->buffer, encoder->buffer + block_size * 2, encoder->count * 4);
	}

	return encoder->data;
}

/*---------------------------------------------------------------------------*/
static uint8_t* aac_encode(struct encoder_s* encoder, int16_t* pcm, size_t frames, size_t* bytes) {
	memcpy(encoder->buffer + encoder->count * 2, pcm, frames * 4);
	encoder->count += frames;
	*bytes = 0;

	// encode all full block to not accumulate pcm
	while (encoder->count >= encoder->aac.in_samples / 2) {
		*bytes += faacEncEncode(encoder->codec, (int32_t*)encoder->buffer, encoder->aac.in_samples,
								encoder->data + *bytes, encoder->aac.out_max_bytes);
		encoder->count -= encoder->aac.in_samples / 2;
		// we could just update the encode.buffer starting point but at least one last memcpy will be needed
		memmove(encoder->buffer, encoder->buffer + encoder->aac.in_samples, encoder->count * 4);
	}

	return encoder->data;
}

/*---------------------------------------------------------------------------*/
static uint8_t* pcm_encode(struct encoder_s* encoder, int16_t* pcm, size_t frames, size_t* bytes) {
#ifdef _WIN32
	for (size_t i = 0; i < frames * 2; i++) pcm[i] = _byteswap_ushort(pcm[i]);
#else
	for (size_t i = 0; i < frames * 2; i++) pcm[i] = __builtin_bswap16(pcm[i]);
#endif
	*bytes = frames * 4;
	return (uint8_t*) pcm;
}

/*---------------------------------------------------------------------------*/
static uint8_t* wav_encode(struct encoder_s* encoder, int16_t* pcm, size_t frames, size_t* bytes) {
	*bytes = frames * 4;

	if (encoder->wav.header) {
		memcpy(encoder->data, &wave_header, sizeof(wave_header));
		memcpy(encoder->data + sizeof(wave_header), pcm, frames * 4);

		*bytes += sizeof(wave_header);
		encoder->wav.header = false;
		return encoder->data;
	}
	
	return (uint8_t*)pcm;
}

/*---------------------------------------------------------------------------*/
struct encoder_s* encoder_create(char* codec, uint32_t sample_rate, uint8_t channels, uint8_t sample_size, size_t max_frames, size_t *icy_interval) {
	struct encoder_s* encoder = calloc(1, sizeof(struct encoder_s));
	encoder->open = NULL;
	encoder->close = NULL;
	encoder->count = 0;
	encoder->max_frames = max(ENCODER_MAX_FRAMES, 2 * max_frames);
	encoder->sample_rate = sample_rate;
	
	if (!strncasecmp(codec, "pcm", 3)) {
		encoder->format = CODEC_PCM;
		encoder->mimetype = malloc(128);
		sprintf(encoder->mimetype, "audio/L16;rate=%d;channels=2", encoder->sample_rate);
		encoder->encode = pcm_encode;
	} else if (!strncasecmp(codec, "wav",3)) {
		encoder->format = CODEC_WAV;
		encoder->mimetype = strdup("audio/wav");
		encoder->open = wav_open;
		encoder->encode = wav_encode;
	} else if (!strncasecmp(codec, "mp3",3)) {
		encoder->format = CODEC_MP3;
		encoder->mimetype = strdup("audio/mpeg");
		encoder->open = mp3_open;
		encoder->close = mp3_close;
		encoder->encode = mp3_encode;
		encoder->mp3.bitrate = 192;
		if (sscanf(codec, "%*[^:]:%d", &encoder->mp3.bitrate) && encoder->mp3.bitrate > 320) encoder->mp3.bitrate = 320;
	} else if (!strncasecmp(codec, "aac",3)) {
		encoder->format = CODEC_AAC;
		encoder->mimetype = strdup("audio/aac");
		encoder->open = aac_open;
		encoder->close = aac_close;
		encoder->encode = aac_encode;
		encoder->aac.bitrate = 128;
		if (sscanf(codec, "%*[^:]:%d", &encoder->aac.bitrate) && encoder->aac.bitrate > 320) encoder->aac.bitrate = 320;
	} else {
		encoder->format = CODEC_FLAC;
		encoder->mimetype = strdup("audio/flac");
		encoder->open = flac_open;
		encoder->close = flac_close;
		encoder->encode = flac_encode;
		encoder->flac.level = 5;
		encoder->flac.blocksize = 4096;
		if (sscanf(codec, "%*[^:]:%d", &encoder->flac.level) && encoder->flac.level > 9) encoder->flac.level = 9;
		if (sscanf(codec, "%*[^/]/%d", &encoder->flac.blocksize) && (encoder->flac.blocksize < 1152 || encoder->flac.blocksize > 16384)) encoder->flac.blocksize = 4096;
	}

	*icy_interval = (encoder->format == CODEC_MP3 || encoder->format == CODEC_AAC) ? 16 * 1024 : 128 * 1024;

	return encoder;
}

/*---------------------------------------------------------------------------*/
void encoder_delete(struct encoder_s* encoder) {
	if (!encoder) return;

	encoder_close(encoder);
	free(encoder->mimetype);
	free(encoder);
}

/*---------------------------------------------------------------------------*/
bool encoder_open(struct encoder_s* encoder) {
	if (!encoder) return false;

	encoder->buffer = NULL;
	encoder->data = NULL;
	encoder->codec = NULL;
	encoder->bytes = encoder->count = 0;
	if (encoder->open) return encoder->open(encoder);

	return true;
}

/*---------------------------------------------------------------------------*/
void encoder_close(struct encoder_s* encoder) {
	if (!encoder) return;

	if (encoder->codec && encoder->close) encoder->close(encoder);
	if (encoder->buffer) free(encoder->buffer);
	if (encoder->data) free(encoder->data);

	encoder->buffer = NULL;
	encoder->data = NULL;
	encoder->codec = NULL;
}

/*---------------------------------------------------------------------------*/
uint8_t* encoder_encode(struct encoder_s* encoder, int16_t* pcm, size_t frames, size_t* bytes) {
	return encoder ? encoder->encode(encoder, pcm, frames, bytes) : NULL;
}

/*---------------------------------------------------------------------------*/
char* encoder_mimetype(struct encoder_s* encoder) {
	return encoder ? encoder->mimetype : NULL;
}

size_t encoder_space(struct encoder_s* encoder) {
	return encoder ? encoder->max_frames / 2 - encoder->count : 0;
}
