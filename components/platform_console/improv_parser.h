#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* UART-only demultiplexer. Never pass a recognised binary frame to linenoise. */
typedef void (*improv_frame_cb)(const uint8_t *rpc, size_t len, bool valid);
typedef struct {
    uint8_t frame[265], text[8];
    size_t used, text_len, text_pos;
    uint32_t last_ms;
    bool quarantine, trailing_newline;
    unsigned resync;
} improv_parser_t;
void improv_parser_feed(improv_parser_t *p, uint8_t b, uint32_t now, improv_frame_cb cb);
void improv_parser_expire(improv_parser_t *p, uint32_t now);
size_t improv_parser_read(improv_parser_t *p, void *out, size_t cap);
