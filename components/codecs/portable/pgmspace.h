#pragma once
#include <stdint.h>
#include <string.h>
/* ESP-IDF maps const data into addressable flash; no Arduino dependency needed. */
#define PROGMEM
#define PSTR(s) (s)
#define PGM_P const char *
#define memcpy_P memcpy
#define memcmp_P memcmp
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define pgm_read_dword(p) (*(const uint32_t *)(p))
#define pgm_read_ptr(p) (*(const void * const *)(p))
