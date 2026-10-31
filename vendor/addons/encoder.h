/*
 *  encoder: main server interface
 *
 *  (c) Philippe, philippe_44@outlook.com
 *
 * See LICENSE
 * 
 */

#pragma once

#include <stdint.h>

#define ENCODER_MAX_FRAMES	8192

struct encoder_s;

struct encoder_s* encoder_create(char* codec, uint32_t sample_rate, uint8_t channels, 
							     uint8_t sample_size, size_t max_frames, size_t *icy_interval);
char*    encoder_mimetype(struct encoder_s* encoder);
void     encoder_close(struct encoder_s* encoder);
bool     encoder_open(struct encoder_s* encoder);
void     encoder_delete(struct encoder_s* encoder);
uint8_t* encoder_encode(struct encoder_s* encoder, int16_t* pcm, size_t frames, size_t* bytes);
size_t	 encoder_space(struct encoder_s* encoder);
