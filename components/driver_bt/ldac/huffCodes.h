#pragma once

#include "bit_reader.h"

typedef struct
{
	const unsigned char* Bits;
	const unsigned short* Codes;
	unsigned char* Lookup;
	const int Length;
	const int ValueCount;
	const int ValueCountPower;
	const int ValueBits;
	const int ValueMax;
	const int MaxBitSize;
} HuffmanCodebook;

int ldac_ReadHuffmanValue(const HuffmanCodebook* huff, BitReaderCxt* br, int isSigned);
void ldac_DecodeHuffmanValues(int* spectrum, int index, int bandCount, const HuffmanCodebook* huff, const int* values);
void ldac_InitHuffmanCodebook(const HuffmanCodebook* codebook);

extern HuffmanCodebook ldac_HuffmanScaleFactorsUnsigned[7];
extern HuffmanCodebook ldac_HuffmanScaleFactorsSigned[6];
extern HuffmanCodebook ldac_HuffmanSpectrum[2][8][4];

void ldac_InitHuffmanCodebooks();
