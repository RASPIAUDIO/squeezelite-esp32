/* 
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include "esp_log.h"
#include "globdefs.h"
#include "platform_config.h"
#include "tools.h"
#include "display.h"
#include "services.h"
#include "gds.h"
#include "gds_private.h"
#include "gds_default_if.h"
#include "gds_draw.h"
#include "gds_text.h"
#include "gds_font.h"
#include "gds_image.h"
#include "wifi_setup.h"
#include "now_playing.h"

static const char *TAG = "display";

// Define global function pointer declared in display.h
bool (*display_bus)(void *from, enum display_bus_cmd_e cmd) = NULL;
bool (*display_spectrum)(uint8_t *levels, unsigned bands) = NULL;
static bool spectrum_enabled = true;
static uint8_t spectrum_levels[NOW_PLAYING_BANDS], spectrum_peaks[NOW_PLAYING_BANDS];
static TickType_t spectrum_tick;

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))

#define DISPLAYER_STACK_SIZE 	(6*1024)
#define SCROLLABLE_SIZE			384
#define HEADER_SIZE				64
#define	DEFAULT_SLEEP			3600
#define ARTWORK_BORDER			1

extern const uint8_t default_artwork[]   asm("_binary_note_jpg_start");

static EXT_RAM_BSS_ATTR struct {
	TaskHandle_t task;
	SemaphoreHandle_t mutex;
	int pause, speed, by;
	enum { DISPLAYER_DOWN, DISPLAYER_IDLE, DISPLAYER_ACTIVE } state;
	char header[HEADER_SIZE + 1];
	char string[SCROLLABLE_SIZE + 1];
	int offset, boundary;
	char *metadata_config;
	bool timer, refresh;
	uint32_t elapsed;
	struct {
		uint32_t value;
		char string[8]; // H:MM:SS
		bool visible;
	} duration;
	struct {
		bool enable, active;
		bool fit;
		bool updated;
		bool pending_flush;
		int tick;
		int offset;
	}  artwork;
	TickType_t tick;
} displayer;

static EXT_RAM_BSS_ATTR struct {
    bool active, dirty, open;
    char ssid[33], password[65], ip[16];
} setup_screen;

void displayer_wifi_setup(const char *ssid, const char *password, const char *ip, bool open) {
    if (!display || !displayer.mutex || display->Width < 320 || display->Height < 240) return;
    xSemaphoreTake(displayer.mutex, portMAX_DELAY);
    if (ssid && password && ip) {
        strlcpy(setup_screen.ssid, ssid, sizeof(setup_screen.ssid));
        strlcpy(setup_screen.password, password, sizeof(setup_screen.password));
        strlcpy(setup_screen.ip, ip, sizeof(setup_screen.ip));
        setup_screen.open = open;
        setup_screen.active = setup_screen.dirty = true;
        displayer.artwork.pending_flush = false;
        GDS_DisplayOn(display);
    } else if (setup_screen.active) {
        memset(&setup_screen, 0, sizeof(setup_screen));
        displayer.state = DISPLAYER_DOWN;
        GDS_Clear(display, GDS_COLOR_BLACK);
        GDS_Update(display);
        if (display_bus) display_bus(&displayer, DISPLAY_BUS_GIVE);
        ESP_LOGI(TAG, "Wi-Fi setup screen hidden");
    }
    xTaskNotifyGive(displayer.task);
    xSemaphoreGive(displayer.mutex);
}

static const char *known_drivers[] = {"SH1106",
        "SH1122",
		"SSD1306",
		"SSD1322",
		"SSD1326",
		"SSD1327",
		"SSD1675",
		"SSD1351",
		"ST7735",
		"ST7789",
		"ILI9341",
		NULL
	};
    
static void displayer_task(void *args);
static void display_sleep(void);
static void displayer_artwork_locked(uint8_t *data);

void displayer_spectrum_enable(bool enabled) {
    if (!displayer.mutex) { spectrum_enabled = enabled; return; }
    xSemaphoreTake(displayer.mutex, portMAX_DELAY);
    spectrum_enabled = enabled;
    memset(spectrum_levels, 0, sizeof(spectrum_levels));
    memset(spectrum_peaks, 0, sizeof(spectrum_peaks));
    displayer.refresh = true;
    xTaskNotifyGive(displayer.task);
    xSemaphoreGive(displayer.mutex);
}

struct GDS_Device *display;   
extern GDS_DetectFunc SSD1306_Detect, SSD132x_Detect, SH1106_Detect, SH1122_Detect, SSD1675_Detect, SSD1322_Detect, SSD1351_Detect, ST77xx_Detect, ILI9341_Detect;
GDS_DetectFunc *drivers[] = { SH1106_Detect, SH1122_Detect, SSD1306_Detect, SSD132x_Detect, SSD1675_Detect, SSD1322_Detect, SSD1351_Detect, ST77xx_Detect, ILI9341_Detect, NULL };

/****************************************************************************************
 * 
 */
void display_init(char *welcome) {
	bool init = false;
	char *config = config_alloc_get_str("display_config", CONFIG_DISPLAY_CONFIG, "N/A");
	
	int width = -1, height = -1, backlight_pin = -1;
	char *drivername = strstr(config, "driver");

	PARSE_PARAM(config, "width", '=', width);
	PARSE_PARAM(config, "height", '=', height);
	PARSE_PARAM(config, "back", '=', backlight_pin);
		
	// query drivers to see if we have a match
	ESP_LOGI(TAG, "Trying to configure display with %s", config);
	if (backlight_pin >= 0) {
		struct GDS_BacklightPWM PWMConfig = { .Channel = pwm_system.base_channel++, .Timer = pwm_system.timer, .Max = pwm_system.max, .Init = false	};
		display = GDS_AutoDetect(drivername, drivers, &PWMConfig);
	} else {
		display = GDS_AutoDetect(drivername, drivers, NULL);
	}

	// so far so good
	if (display && width > 0 && height > 0) {
		int RST_pin = -1;
		PARSE_PARAM(config, "reset", '=', RST_pin);
		
		// Detect driver interface
		if (strcasestr(config, "I2C") && i2c_system_port != -1) {
			int address = 0x3C;
				
			PARSE_PARAM(config, "address", '=', address);
				
			init = true;
			GDS_I2CInit( i2c_system_port, -1, -1, i2c_system_speed ) ;
			GDS_I2CAttachDevice( display, width, height, address, RST_pin, backlight_pin );
		
			ESP_LOGI(TAG, "Display is I2C on port %u", address);
		} else if (strcasestr(config, "SPI") && spi_system_host != -1) {
			int CS_pin = -1, speed = 0, mode = 0;
		
			PARSE_PARAM(config, "cs", '=', CS_pin);
			PARSE_PARAM(config, "speed", '=', speed);
			PARSE_PARAM(config, "mode", '=', mode);
		
			init = true;
			GDS_SPIInit( spi_system_host, spi_system_dc_gpio );
			GDS_SPIAttachDevice( display, width, height, CS_pin, RST_pin, backlight_pin, speed, mode );
				
			ESP_LOGI(TAG, "Display is SPI host %u with cs:%d", spi_system_host, CS_pin);
		} else {
			display = NULL;
			ESP_LOGI(TAG, "Unsupported display interface or serial link not configured");
		}
	} else {
		display = NULL;
		ESP_LOGW(TAG, "No display driver");
	}	
	
	if (init) {
		static DRAM_ATTR StaticTask_t xTaskBuffer __attribute__ ((aligned (4)));
		static EXT_RAM_BSS_ATTR StackType_t xStack[DISPLAYER_STACK_SIZE] __attribute__ ((aligned (4)));
		struct GDS_Layout Layout = {
			.HFlip = strcasestr(config, "HFlip"), 
			.VFlip = strcasestr(config, "VFlip"), 
			.Rotate = strcasestr(config, "rotate"), 
			.Invert = strcasestr(config, "invert"),
			.ColorSwap = strcasestr(config, "cswap"),
		};	

		GDS_SetLayout(display, &Layout);
		GDS_SetFont(display, &Font_line_2);
		GDS_TextPos(display, GDS_FONT_DEFAULT, GDS_TEXT_CENTERED, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, welcome);

		// start the task that will handle scrolling & counting
		displayer.mutex = xSemaphoreCreateMutex();
		displayer.by = 2;
		displayer.pause = 3600;
		displayer.speed = 33;
		displayer.task = xTaskCreateStatic( (TaskFunction_t) displayer_task, "common_displayer", DISPLAYER_STACK_SIZE, NULL, ESP_TASK_PRIO_MIN + 1, xStack, &xTaskBuffer);
		
		// set lines for "fixed" text mode
		GDS_TextSetFontAuto(display, 1, GDS_FONT_LINE_1, -3);
		GDS_TextSetFontAuto(display, 2, GDS_FONT_LINE_2, -3);
		
		displayer.metadata_config = config_alloc_get(NVS_TYPE_STR, "metadata_config");

        char *spectrum_config = config_alloc_get_str("spectrum", "Y", "Y");
        spectrum_enabled = spectrum_config && !strcasecmp(spectrum_config, "Y");
        free(spectrum_config);
		
		// leave room for artwork is display is horizontal-style
		if (displayer.metadata_config && strcasestr(displayer.metadata_config, "artwork")) {
			displayer.artwork.enable = true;
			displayer.artwork.fit = true;
			if (height <= 64 && width > height * 2) displayer.artwork.offset = width - height - ARTWORK_BORDER;
			PARSE_PARAM(displayer.metadata_config, "artwork", ':', displayer.artwork.fit);
		}	
        
        // and finally register ourselves to power off upon deep sleep
        services_sleep_setsuspend(display_sleep);
	}
	
	free(config);
}

/****************************************************************************************
 * 
 */
static void display_sleep(void) {
    GDS_DisplayOff(display);
}

/****************************************************************************************
 * Serialize complete draws with activation/shutdown and artwork updates.
 * The LMS display lock is transferred only after this task has finished drawing.
 */
static void displayer_task(void *args) {
	int scroll_sleep = 0, timer_sleep;
		
	while (1) {
		xSemaphoreTake(displayer.mutex, portMAX_DELAY);
        if (setup_screen.active) {
            if (display_bus) display_bus(&displayer, DISPLAY_BUS_TAKE);
            if (setup_screen.dirty) {
                bool ok = wifi_setup_render(display, setup_screen.ssid, setup_screen.password, setup_screen.ip, setup_screen.open);
                setup_screen.dirty = false;
                ESP_LOGI(TAG, "Wi-Fi setup screen rendered; QR codes %s", ok ? "ready" : "unavailable");
            }
            xSemaphoreGive(displayer.mutex);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }
		// Idle/down no longer owns the display. Never draw after giving it to LMS.
		if (displayer.state < DISPLAYER_ACTIVE) {
			xSemaphoreGive(displayer.mutex);
			// A notification also survives activation between unlock and wait.
			ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
			scroll_sleep = 0;
			continue;
		}
        /* Independent of LMS: the common displayer owns this framebuffer.
         * Small/monochrome displays keep their original two-line layout. */
        if (now_playing_supported(display)) {
            TickType_t tick = xTaskGetTickCount();
            if (displayer.timer) {
                uint32_t seconds = (tick - displayer.tick) * portTICK_PERIOD_MS / 1000;
                displayer.elapsed += seconds;
                displayer.tick += pdMS_TO_TICKS(seconds * 1000);
            }
            if (displayer.refresh) scroll_sleep = 0;
            if (scroll_sleep <= 10) {
                now_playing_title(display, displayer.string, displayer.offset);
                scroll_sleep = displayer.offset ? displayer.speed : displayer.pause;
                displayer.offset = displayer.offset >= displayer.boundary ? 0 :
                    displayer.offset + min(displayer.by, displayer.boundary - displayer.offset);
            }
            now_playing_info(display, displayer.header, displayer.elapsed);
            if (spectrum_enabled && (displayer.refresh || tick - spectrum_tick >= pdMS_TO_TICKS(100))) {
                if (display_spectrum) display_spectrum(spectrum_levels, NOW_PLAYING_BANDS);
                now_playing_spectrum(display, spectrum_levels, spectrum_peaks);
                spectrum_tick = tick;
            } else if (displayer.refresh) {
                now_playing_spectrum(display, NULL, spectrum_peaks);
            }
            if (!displayer.artwork.updated && tick - displayer.artwork.tick > pdMS_TO_TICKS(5000))
                displayer_artwork_locked((uint8_t *)default_artwork);
            displayer.refresh = false;
            displayer.artwork.pending_flush = false;
            GDS_Update(display);
            xSemaphoreGive(displayer.mutex);
            TickType_t start = xTaskGetTickCount();
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(min(100, max(10, scroll_sleep))));
            scroll_sleep -= (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
            continue;
        }
		if (displayer.refresh) {
			// little trick when switching master while in IDLE and missing it
			GDS_TextLine(display, 1, GDS_TEXT_LEFT, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, displayer.header);	
			displayer.refresh = false;			
		}
		
		// we have been waken up before our requested time
		if (scroll_sleep <= 10) {
			// something to scroll (or we'll wake-up every pause ms ... no big deal)
			if (*displayer.string && displayer.state == DISPLAYER_ACTIVE) {

				// Keep state and rendering under the ownership-transfer mutex.
				int offset = -displayer.offset;
				char *string = displayer.string;
				scroll_sleep = displayer.offset ? displayer.speed : displayer.pause;
				displayer.offset = displayer.offset >= displayer.boundary ? 0 : (displayer.offset + min(displayer.by, displayer.boundary - displayer.offset));			


				// A shutdown waits until this complete draw has finished.
				GDS_TextLine(display, 2, offset, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, string);
			} else {
				scroll_sleep = DEFAULT_SLEEP;
			}	
		}	
		
		// handler elapsed track time
		if (displayer.timer && displayer.state == DISPLAYER_ACTIVE) {
			char line[19] = "-", *_line = line + 1; // [-]H:MM:SS / H:MM:SS
			TickType_t tick = xTaskGetTickCount();
			uint32_t elapsed = (tick - displayer.tick) * portTICK_PERIOD_MS;

			if (elapsed >= 1000) {

				displayer.tick = tick;
				elapsed = displayer.elapsed += elapsed / 1000;

				// when we have duration but no space, display remaining time
				if (displayer.duration.value && !displayer.duration.visible) elapsed = displayer.duration.value - elapsed;

				if (elapsed < 3600) sprintf(_line, "%" PRIu32 ":%02" PRIu32, elapsed / 60, elapsed % 60);
				else sprintf(_line, "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32, (elapsed / 3600) % 100, (elapsed % 3600) / 60, elapsed % 60);

				// concatenate if we have room for elapsed / duration
				if (displayer.duration.visible) {
					strcat(_line, "/");
					strcat(_line, displayer.duration.string);
				} else if (displayer.duration.value) {
					_line--;
				}

				// just re-write the whole line it's easier
				GDS_TextLine(display, 1, GDS_TEXT_LEFT, GDS_TEXT_CLEAR, displayer.header);	
				GDS_TextLine(display, 1, GDS_TEXT_RIGHT, GDS_TEXT_UPDATE, _line);
				
				// if we have not received artwork after 5s, display a default icon
				if (displayer.artwork.active && !displayer.artwork.updated && tick - displayer.artwork.tick > pdMS_TO_TICKS(5000)) {
					ESP_LOGI(TAG, "no artwork received, setting default");
					displayer_artwork_locked((uint8_t*) default_artwork);
				}	
				timer_sleep = 1000;
			} else timer_sleep = max(1000 - elapsed, 0);	
		} else timer_sleep = DEFAULT_SLEEP;
		
		// A Sendspin image can arrive without metadata or a running timer.
		// Flush it from this task, never while holding the audio ownership lock.

		if (displayer.artwork.pending_flush) {
			if (displayer.state == DISPLAYER_ACTIVE) GDS_Update(display);
			displayer.artwork.pending_flush = false;
		}

		// Image delivery wakes this wait; preserve the remaining scroll delay.
		int sleep = min(scroll_sleep, timer_sleep);
		ESP_LOGD(TAG, "timers s:%d t:%d", scroll_sleep, timer_sleep);
		xSemaphoreGive(displayer.mutex);
		TickType_t wait_start = xTaskGetTickCount();
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleep));
		scroll_sleep -= (xTaskGetTickCount() - wait_start) * portTICK_PERIOD_MS;
	}
}	

/****************************************************************************************
 * 
 */
static void displayer_artwork_locked(uint8_t *data) {
	if (!displayer.artwork.active) return;

    if (now_playing_supported(display)) {
        int w = 0, h = 0, side = now_playing_cover_size(display);
        uint16_t *pixels = NULL;
        if (data) {
            GDS_GetJPEGSize(data, &w, &h);
            if (w > 0 && h > 0) {
                float scale = w > h ? (float)side / w : (float)side / h;
                pixels = GDS_DecodeJPEG(data, &w, &h, scale, display->Mode);
            }
        }
        now_playing_cover(display, pixels, w, h);
        displayer.artwork.updated = pixels != NULL;
        displayer.artwork.tick = xTaskGetTickCount();
        displayer.artwork.pending_flush = true;
        free(pixels);
        return;
    }
	
	int x = displayer.artwork.offset ? displayer.artwork.offset + ARTWORK_BORDER : 0;
	int y = x ? 0 : 32;
	GDS_ClearWindow(display, x, y, -1, -1, GDS_COLOR_BLACK);
	if (data) {
		displayer.artwork.updated = true;
		GDS_DrawJPEG(display, data, x, y, GDS_IMAGE_CENTER | (displayer.artwork.fit ? GDS_IMAGE_FIT : 0));
	} else {
		displayer.artwork.updated = false;
		displayer.artwork.tick = xTaskGetTickCount();
	}	
	
}

void displayer_artwork(uint8_t *data) {
    if (!display || !displayer.mutex) return;
    xSemaphoreTake(displayer.mutex, portMAX_DELAY);
    if (!setup_screen.active && displayer.state == DISPLAYER_ACTIVE) displayer_artwork_locked(data);
    xSemaphoreGive(displayer.mutex);
}

/****************************************************************************************
 * 
 */
typedef struct {
    uint8_t *pixels;
    int width, height, mode;
} prepared_image_t;

void *displayer_image_decode(const uint8_t *data, size_t length) {
    // Network artwork currently targets framebuffer colour displays only.
    if (!display || !display->Framebuffer || display->Mode != GDS_RGB565 ||
        display->DrawRGB || display->ClearWindow || !displayer.mutex) return NULL;
    xSemaphoreTake(displayer.mutex, portMAX_DELAY);
    int x = displayer.artwork.offset ? displayer.artwork.offset + ARTWORK_BORDER : 0;
    int y = x ? 0 : 32;
    int width = min(208, display->Width - x), height = min(208, display->Height - y);
    if (now_playing_supported(display)) width = height = now_playing_cover_size(display);
    int mode = display->Mode;
    xSemaphoreGive(displayer.mutex);
    prepared_image_t *image = calloc(1, sizeof(*image));
    if (!image) return NULL;
    image->mode = mode;
    image->pixels = GDS_DecodeJPEGBounded(data, length, &image->width, &image->height, width, height, mode);
    if (!image->pixels) { free(image); return NULL; }
    return image;
}

void displayer_image_free(void *ptr) {
    prepared_image_t *image = ptr;
    if (image) { free(image->pixels); free(image); }
}

bool displayer_image_show(void *ptr) {
    prepared_image_t *image = ptr;
    if (!display || !display->Framebuffer || display->Mode != GDS_RGB565 ||
        display->DrawRGB || display->ClearWindow || !displayer.mutex ||
        xSemaphoreTake(displayer.mutex, 0) != pdTRUE) return false;
    bool ok = !setup_screen.active && displayer.artwork.active && displayer.state == DISPLAYER_ACTIVE;
    int x = displayer.artwork.offset ? displayer.artwork.offset + ARTWORK_BORDER : 0;
    int y = x ? 0 : 32;
    if (image && (image->mode != display->Mode || image->width > display->Width-x ||
                  image->height > display->Height-y)) ok = false;
    if (ok) {
        // Framebuffer-only: safe inside audio ownership critical section.
        // The normal display task performs the subsequent SPI update.
        if (now_playing_supported(display)) {
            now_playing_cover(display, image ? (uint16_t *)image->pixels : NULL,
                              image ? image->width : 0, image ? image->height : 0);
        } else {
        GDS_ClearWindow(display, x, y, -1, -1, GDS_COLOR_BLACK);
        if (image) GDS_DrawRGB(display, image->pixels,
            (display->Width+x-image->width)/2, (display->Height+y-image->height)/2,
            image->width, image->height, image->mode);
        }
        displayer.artwork.updated = image != NULL;
        displayer.artwork.tick = xTaskGetTickCount();
        displayer.artwork.pending_flush = true;
        xTaskNotifyGive(displayer.task);
    }
    xSemaphoreGive(displayer.mutex);
    return ok;
}

bool displayer_can_artwork(void) {
	return displayer.artwork.active;
}

/****************************************************************************************
 * 
 */
void displayer_metadata(char *artist, char *album, char *title) {
	char *string = displayer.string, *p;
	int len = SCROLLABLE_SIZE;
	
	// need a display!
	if (!display) return;
	
	// just do title if there is no config set
	if (!displayer.metadata_config && !now_playing_supported(display)) {
		strncpy(displayer.string, title ? title : "", SCROLLABLE_SIZE);
		return;
	}
	
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);
	
	if (now_playing_supported(display)) {
        snprintf(string, SCROLLABLE_SIZE + 1, "%s%s%s", title ? title : "",
                 title && *title && artist && *artist ? " - " : "", artist ? artist : "");
        utf8_decode(string);
        displayer.offset = 0;
        displayer.boundary = max(0, now_playing_title_width(string) - (display->Width - 16));
        displayer.refresh = true;
        xTaskNotifyGive(displayer.task);
        xSemaphoreGive(displayer.mutex);
        return;
    }
	// format metadata parameters and write them directly
	if ((p = strcasestr(displayer.metadata_config, "format")) != NULL) {
		char token[16], *q;
		int space = len;
		bool skip = false;
			
		displayer.string[0] = '\0';	
		p = strchr(displayer.metadata_config, '=');
			
		while (p++) {
			// find token and copy what's after when reaching last one
			if (sscanf(p, "%*[^%%]%%%[^%]%%", token) < 0) {
				q = strchr(p, ',');
				strncat(string, p, q ? min(q - p, space) : space);
				break;
			}

			// copy what's before token (be safe)
			if ((q = strchr(p, '%')) == NULL) break;
			
			// skip whatever is after a token if this token is empty
			if (!skip) {
				strncat(string, p, min(q - p, space));
				space = len - strlen(string);
			}	

			// then copy token's content
			if (!strncasecmp(q + 1, "artist", 6) && artist) strncat(string, p = artist, space);
			else if (!strncasecmp(q + 1, "album", 5) && album) strncat(string, p = album, space);
			else if (!strncasecmp(q + 1, "title", 5) && title) strncat(string, p = title, space);
			space = len - strlen(string);
				
			// flag to skip the data following an empty field
			if (*p) skip = false;
			else skip = true;

			// advance to next separator
			p = strchr(q + 1, '%');
		}
	} else {
		strncpy(string, title ? title : "", SCROLLABLE_SIZE);
	}
	
	// get optional scroll speed & pause
	PARSE_PARAM(displayer.metadata_config, "speed", '=', displayer.speed);
	PARSE_PARAM(displayer.metadata_config, "pause", '=', displayer.pause);
	
	displayer.offset = 0;	
	utf8_decode(displayer.string);
	ESP_LOGI(TAG, "playing %s", displayer.string);
	displayer.boundary = GDS_TextStretch(display, 2, displayer.string, SCROLLABLE_SIZE);
		
	xSemaphoreGive(displayer.mutex);
}	

/****************************************************************************************
 *
 */
void displayer_scroll(char *string, int speed, int pause) {
	// need a display!
	if (!display) return;
	
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);

	if (speed) displayer.speed = speed;
	if (pause) displayer.pause = pause;
	displayer.offset = 0;	
	strncpy(displayer.string, string, SCROLLABLE_SIZE);
	displayer.string[SCROLLABLE_SIZE] = '\0';
	displayer.boundary = GDS_TextStretch(display, 2, displayer.string, SCROLLABLE_SIZE);
		
	xSemaphoreGive(displayer.mutex);
}

/****************************************************************************************
 * 
 */
void displayer_timer(enum displayer_time_e mode, int elapsed, int duration) {
	// need a display!
	if (!display) return;
	
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);

	if (displayer.timer) displayer.tick = xTaskGetTickCount();
	if (elapsed >= 0) displayer.elapsed = elapsed / 1000;	
	if (duration > 0) {
		displayer.duration.visible = true;
		displayer.duration.value = duration / 1000;

		if (displayer.duration.value > 3600) sprintf(displayer.duration.string, "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32, (displayer.duration.value / 3600) % 10,
													(displayer.duration.value % 3600) / 60, displayer.duration.value % 60);
		else sprintf(displayer.duration.string, "%" PRIu32 ":%02" PRIu32, displayer.duration.value / 60, displayer.duration.value % 60);

		char *buf;
		asprintf(&buf, "%s %s/%s", displayer.header, displayer.duration.string, displayer.duration.string);
		if (GDS_GetTextWidth(display, 1, 0, buf) > GDS_GetWidth(display)) {
			ESP_LOGW(TAG, "Can't fit duration %s (%d) on screen using elapsed only", buf, GDS_GetTextWidth(display, 1, 0, buf));
			displayer.duration.visible = false;
		}
		free(buf);
	} else if (!duration) {
		displayer.duration.visible = false;
		displayer.duration.value = 0;
	}
		
	xSemaphoreGive(displayer.mutex);
}	

/****************************************************************************************
 * See above comment
 */
void displayer_control(enum displayer_cmd_e cmd, ...) {
	va_list args;
	
	if (!display) return;
	
	va_start(args, cmd);
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);
    if (setup_screen.active) { xSemaphoreGive(displayer.mutex); va_end(args); return; }
		
	switch(cmd) {
	case DISPLAYER_ACTIVATE: {	
		char *header = va_arg(args, char*);
		displayer.artwork.active = displayer.artwork.enable && va_arg(args, int);
        if (now_playing_supported(display)) displayer.artwork.active = true;
        displayer.artwork.updated = displayer.artwork.pending_flush = false;
        displayer.artwork.tick = xTaskGetTickCount();
        memset(spectrum_levels, 0, sizeof(spectrum_levels));
        memset(spectrum_peaks, 0, sizeof(spectrum_peaks));
		strncpy(displayer.header, header, HEADER_SIZE);
		displayer.header[HEADER_SIZE] = '\0';
        if (now_playing_supported(display)) ESP_LOGI(TAG,
            "Standalone now-playing: %s, cover %dx%d, spectrum %s", header,
            now_playing_cover_size(display), now_playing_cover_size(display), spectrum_enabled ? "enabled" : "disabled");
		displayer.state = DISPLAYER_ACTIVE;
		displayer.timer = false;
		displayer.refresh = true;
		displayer.string[0] = '\0';
		displayer.elapsed = displayer.duration.value = 0;
		displayer.duration.visible = false;
		displayer.offset = displayer.boundary = 0;
		display_bus(&displayer, DISPLAY_BUS_TAKE);
		if (displayer.artwork.active) GDS_SetTextWidth(display, now_playing_supported(display) ? 0 : displayer.artwork.offset);
		GDS_Clear(display, GDS_COLOR_BLACK);
		xTaskNotifyGive(displayer.task);
		break;
	}	
	case DISPLAYER_SUSPEND:		
		// Finish the final line before transferring display ownership to LMS.
		if (displayer.state == DISPLAYER_ACTIVE) {
			if (!now_playing_supported(display)) GDS_TextLine(display, 2, 0, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, displayer.string);
			displayer_artwork_locked(NULL);
		}
		displayer.state = DISPLAYER_IDLE;
		display_bus(&displayer, DISPLAY_BUS_GIVE);
		break;		
	case DISPLAYER_SHUTDOWN:
		displayer.artwork.pending_flush = false;
		// The renderer holds this mutex throughout each draw; it is now quiescent.
		if (displayer.state == DISPLAYER_ACTIVE) {
			GDS_SetTextWidth(display, 0);
			displayer_artwork_locked(NULL);
		}
		displayer.state = DISPLAYER_DOWN;
		display_bus(&displayer, DISPLAY_BUS_GIVE);
		break;
	case DISPLAYER_TIMER_RUN:
		if (!displayer.timer) {
			display_bus(&displayer, DISPLAY_BUS_TAKE);
			displayer.timer = true;		
			displayer.tick = xTaskGetTickCount();		
		}	
		break;
	case DISPLAYER_TIMER_PAUSE:
		displayer.timer = false;
		break;
	default:
		break;
	}	
	
	xSemaphoreGive(displayer.mutex);
	va_end(args);
}

/****************************************************************************************
 *
 */
bool display_is_valid_driver(const char * driver){
	return display_conf_get_driver_name(driver)!=NULL;
}

/****************************************************************************************
 *
 */
const char *display_conf_get_driver_name(const char * driver){
	for(uint8_t i=0;known_drivers[i]!=NULL && strlen(known_drivers[i])>0;i++ ){
		if(strcasestr(driver,known_drivers[i])){
			return known_drivers[i];
		}
	}
	return NULL;
}

/****************************************************************************************
 *
 */
char * display_get_supported_drivers(void){
	int total_size = 1;
	char * supported_drivers=NULL;
	const char * separator = "|";
	int separator_len = strlen(separator);

	for(uint8_t i=0;known_drivers[i]!=NULL && strlen(known_drivers[i])>0;i++ ){
		total_size += strlen(known_drivers[i])+separator_len;
	}
	total_size+=2;
	supported_drivers = malloc(total_size);
	memset(supported_drivers,0x00,total_size);
	strcat(supported_drivers,"<");
	for(uint8_t i=0;known_drivers[i]!=NULL && strlen(known_drivers[i])>0;i++ ){
		supported_drivers = strcat(supported_drivers,known_drivers[i]);
		supported_drivers = strcat(supported_drivers,separator);
	}
	strcat(supported_drivers,">");
	return supported_drivers;
}
