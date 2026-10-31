#pragma once
#include <stddef.h>
#include <stdint.h>
void improv_serial_init(void);
void improv_serial_feed(uint8_t byte);
size_t improv_serial_read_console(void *out,size_t cap);
unsigned improv_serial_wait_ms(void);
