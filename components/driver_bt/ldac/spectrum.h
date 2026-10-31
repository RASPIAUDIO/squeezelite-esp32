#ifndef _SPECTRUM_H_
#define _SPECTRUM_H_

#include "ldacdec.h"
#include "bit_reader.h"

int ldac_decodeSpectrum( channel_t *this, BitReaderCxt *br );
int ldac_decodeSpectrumFine( channel_t *this, BitReaderCxt *br );

void ldac_dequantizeSpectra( channel_t *this );
void ldac_scaleSpectrum(channel_t* this);

#endif // _SPECTRUM_H_
