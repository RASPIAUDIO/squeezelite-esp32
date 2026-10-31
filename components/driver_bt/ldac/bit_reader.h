#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
	const uint8_t * Buffer;
	int Position;
	int Limit;
	int Failed;
} BitReaderCxt;

// Make MSVC compiler happy. Leave const in for value parameters

void ldac_InitBitReaderCxt(BitReaderCxt* br, const void * buffer, size_t length);
uint32_t ldac_PeekInt(BitReaderCxt* br, const int bits);
uint32_t ldac_ReadInt(BitReaderCxt* br, const int bits);
int32_t ldac_ReadSignedInt(BitReaderCxt* br, const int bits);
int32_t ldac_ReadOffsetBinary(BitReaderCxt* br, const int bits);
void ldac_AlignPosition(BitReaderCxt* br, const unsigned int multiple);
