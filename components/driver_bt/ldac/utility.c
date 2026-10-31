#include "utility.h"
#include <limits.h>

int ldac_Max(int a, int b) { return a > b ? a : b; }
int ldac_Min(int a, int b) { return a > b ? b : a; }

uint32_t ldac_BitReverse32(uint32_t value, int bitCount)
{
	value = ((value & 0xaaaaaaaa) >> 1) | ((value & 0x55555555) << 1);
	value = ((value & 0xcccccccc) >> 2) | ((value & 0x33333333) << 2);
	value = ((value & 0xf0f0f0f0) >> 4) | ((value & 0x0f0f0f0f) << 4);
	value = ((value & 0xff00ff00) >> 8) | ((value & 0x00ff00ff) << 8);
	value = (value >> 16) | (value << 16);
	return bitCount > 0 && bitCount <= 32 ? value >> (32 - bitCount) : 0;
}

int32_t ldac_SignExtend32(int32_t value, int bits)
{
	if (bits < 1 || bits > 32) return 0;
	if (bits == 32) return value;
	uint32_t mask = (1U << bits) - 1;
	uint32_t sign = 1U << (bits - 1);
	uint32_t v = (uint32_t)value & mask;
	return (v & sign) ? (int32_t)(v - (1U << bits)) : (int32_t)v;
}

int16_t ldac_Clamp16(int value)
{
	if (value > SHRT_MAX)
		return SHRT_MAX;
	if (value < SHRT_MIN)
		return SHRT_MIN;
	return (int16_t)value;
}

int ldac_Round(double x)
{
	x += 0.5;
	return (int)x - (x < (int)x);
}
