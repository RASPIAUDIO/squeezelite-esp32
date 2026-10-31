#pragma once

/* The S31 FPU executes float natively. Keep double as an explicit host/test
 * reference; decoded spectra and PCM were already float in the decoder. */
#ifdef LDAC_MDCT_REFERENCE_DOUBLE
typedef double ldac_mdct_real_t;
#else
typedef float ldac_mdct_real_t;
#endif

typedef struct {
	int Bits;
	int Size;
	ldac_mdct_real_t Scale;
	ldac_mdct_real_t ImdctPrevious[MAX_FRAME_SAMPLES];
	ldac_mdct_real_t* Window;
	ldac_mdct_real_t* SinTable;
	ldac_mdct_real_t* CosTable;
} Mdct;

void ldac_InitMdct();
void ldac_RunImdct(Mdct* mdct, float* input, float* output);
