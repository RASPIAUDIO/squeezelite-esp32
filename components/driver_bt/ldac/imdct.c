#include <math.h>
#include <stdint.h>

#include "ldacdec.h"
#include "utility.h"

/* Sizes 1,2,...,256 need 511 entries, not nine full 256-entry rows.
 * Use the native S31 float FPU for the transform; the reference build
 * can retain double precision with LDAC_MDCT_REFERENCE_DOUBLE. */
static ldac_mdct_real_t ldac_MdctWindow[2][256];
static ldac_mdct_real_t ldac_ImdctWindow[2][256];
static ldac_mdct_real_t ldac_SinTables[511];
static ldac_mdct_real_t ldac_CosTables[511];
static int ldac_ShuffleTables[511];
#define TABLE_OFFSET(bits) ((1U << (bits)) - 1U)

static void GenerateTrigTables(int sizeBits)
{
	const int size = 1 << sizeBits;
	ldac_mdct_real_t* sinTab = ldac_SinTables + TABLE_OFFSET(sizeBits);
	ldac_mdct_real_t* cosTab = ldac_CosTables + TABLE_OFFSET(sizeBits);

	for (int i = 0; i < size; i++)
	{
		const double value = M_PI * (4 * i + 1) / (4 * size);
		sinTab[i] = sin(value);
		cosTab[i] = cos(value);
	}
}

static void GenerateShuffleTable(int sizeBits)
{
	const int size = 1 << sizeBits;
	int* table = ldac_ShuffleTables + TABLE_OFFSET(sizeBits);

	for (int i = 0; i < size; i++)
	{
		table[i] = ldac_BitReverse32(i ^ (i / 2), sizeBits);
	}
}

static void GenerateMdctWindow(int frameSizePower)
{
	const int frameSize = 1 << frameSizePower;
	ldac_mdct_real_t* mdct = ldac_MdctWindow[frameSizePower - 7];

	for (int i = 0; i < frameSize; i++)
	{
		mdct[i] = (sin(((i + 0.5) / frameSize - 0.5) * M_PI) + 1.0) * 0.5;
	}
}

static void GenerateImdctWindow(int frameSizePower)
{
	const int frameSize = 1 << frameSizePower;
	ldac_mdct_real_t* imdct = ldac_ImdctWindow[frameSizePower - 7];
	ldac_mdct_real_t* mdct = ldac_MdctWindow[frameSizePower - 7];

	for (int i = 0; i < frameSize; i++)
	{
		imdct[i] = mdct[i] / (mdct[frameSize - 1 - i] * mdct[frameSize - 1 - i] + mdct[i] * mdct[i]);
	}
}

void ldac_InitMdct()
{
	for (int i = 0; i < 9; i++)
	{
		GenerateTrigTables(i);
		GenerateShuffleTable(i);
	}
    GenerateMdctWindow(7);
    GenerateImdctWindow(7);

    GenerateMdctWindow(8);
	GenerateImdctWindow(8);
}


static void Dct4(Mdct* mdct, float* input, float* output);

void ldac_RunImdct(Mdct* mdct, float* input, float* output)
{
	const int size = 1 << mdct->Bits;
	const int half = size / 2;
	float dctOut[MAX_FRAME_SAMPLES] = { 0.f };
	const ldac_mdct_real_t* window = ldac_ImdctWindow[mdct->Bits - 7];
	ldac_mdct_real_t* previous = mdct->ImdctPrevious;

    Dct4(mdct, input, dctOut);

    for (int i = 0; i < half; i++)
	{
		output[i] = window[i] * dctOut[i + half] + previous[i];
		output[i + half] = window[i + half] * -dctOut[size - 1 - i] - previous[i + half];
		previous[i] = window[size - 1 - i] * -dctOut[half - i - 1];
		previous[i + half] = window[half - i - 1] * dctOut[i];
    }
}

static void Dct4(Mdct* mdct, float* input, float* output)
{
	int MdctBits = mdct->Bits;
	int MdctSize = 1 << MdctBits;
	const int* shuffleTable = ldac_ShuffleTables + TABLE_OFFSET(MdctBits);
	const ldac_mdct_real_t* sinTable = ldac_SinTables + TABLE_OFFSET(MdctBits);
	const ldac_mdct_real_t* cosTable = ldac_CosTables + TABLE_OFFSET(MdctBits);
	ldac_mdct_real_t dctTemp[MAX_FRAME_SAMPLES];

	int size = MdctSize;
	int lastIndex = size - 1;
	int halfSize = size / 2;

	for (int i = 0; i < halfSize; i++)
	{
		int i2 = i * 2;
		ldac_mdct_real_t a = input[i2];
		ldac_mdct_real_t b = input[lastIndex - i2];
		ldac_mdct_real_t sin = sinTable[i];
		ldac_mdct_real_t cos = cosTable[i];
		dctTemp[i2] = a * cos + b * sin;
		dctTemp[i2 + 1] = a * sin - b * cos;
	}
	int stageCount = MdctBits - 1;

	for (int stage = 0; stage < stageCount; stage++)
	{
		int blockCount = 1 << stage;
		int blockSizeBits = stageCount - stage;
		int blockHalfSizeBits = blockSizeBits - 1;
		int blockSize = 1 << blockSizeBits;
		int blockHalfSize = 1 << blockHalfSizeBits;
		sinTable = ldac_SinTables + TABLE_OFFSET(blockHalfSizeBits);
		cosTable = ldac_CosTables + TABLE_OFFSET(blockHalfSizeBits);

		for (int block = 0; block < blockCount; block++)
		{
			for (int i = 0; i < blockHalfSize; i++)
			{
				int frontPos = (block * blockSize + i) * 2;
				int backPos = frontPos + blockSize;
				ldac_mdct_real_t a = dctTemp[frontPos] - dctTemp[backPos];
				ldac_mdct_real_t b = dctTemp[frontPos + 1] - dctTemp[backPos + 1];
				ldac_mdct_real_t sin = sinTable[i];
				ldac_mdct_real_t cos = cosTable[i];
				dctTemp[frontPos] += dctTemp[backPos];
				dctTemp[frontPos + 1] += dctTemp[backPos + 1];
				dctTemp[backPos] = a * cos + b * sin;
				dctTemp[backPos + 1] = a * sin - b * cos;
			}
		}
	}

	for (int i = 0; i < MdctSize; i++)
	{
		output[i] = dctTemp[shuffleTable[i]];
	}
}
