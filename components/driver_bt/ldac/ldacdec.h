#ifndef __LDACDEC_H_
#define __LDACDEC_H_

#define MAX_QUANT_UNITS     (34)
#define MAX_FRAME_SAMPLES   (256)

#include "log.h"
#include "imdct.h"
#include <stddef.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#define container_of( ptr, type, member ) ({                \
        const typeof( ((type*)0)->member ) *__mptr = (ptr); \
        (type *)((char*)__ptr - offsetof(type, member));   \
        })

#define min( a, b )             \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
       _a < _b ? _a : _b; });

#define max( a, b )             \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
       _a > _b ? _a : _b; });

typedef struct Frame frame_t;
typedef struct Channel channel_t;

struct Channel {
    frame_t *frame;
    int scaleFactorMode;
    int scaleFactorBitlen;
    int scaleFactorOffset;
    int scaleFactorWeight;

    int scaleFactors[MAX_QUANT_UNITS];

    int precisions[MAX_QUANT_UNITS];
    int precisionsFine[MAX_QUANT_UNITS];
    int precisionMask[MAX_QUANT_UNITS];

    int quantizedSpectra[MAX_FRAME_SAMPLES];
    int quantizedSpectraFine[MAX_FRAME_SAMPLES];

    float spectra[MAX_FRAME_SAMPLES];
    float pcm[MAX_FRAME_SAMPLES];
    Mdct mdct;
};

struct Frame {
    int sampleRateId;
    int channelConfigId;
    int frameLength;
    int frameStatus;
    int frameSamplesPower;
    int frameSamples;

    int nbrBands;

    // gradient data
    int gradient[50];
    int gradientMode;
    int gradientStartUnit;
    int gradientEndUnit;
    int gradientStartValue;
    int gradientEndValue;
    int gradientBoundary;

    int quantizationUnitCount;

    int channelCount;
    channel_t channels[2];
};

typedef struct {
    frame_t frame;

} ldacdec_t;

int ldacdecInit( ldacdec_t *this );
/* Reset only stream state; Init must have initialized the shared tables first. */
void ldacdecReset(ldacdec_t *this);
/* One complete transport frame -> interleaved PCM32. Returns samples/channel
 * or -1, bytesUsed=0 on failure. pcm_capacity counts int32_t elements.
 * Init before use; reset after corrupt/lost input. No output on failed parse. */
int ldacDecode32(ldacdec_t *this, const uint8_t *stream, size_t length,
                int32_t *pcm, size_t pcm_capacity, int *bytesUsed);
int ldacdecGetSampleRate( ldacdec_t *this );
int ldacdecGetChannelCount( ldacdec_t *this );

#endif // __LDACDEC_H_
