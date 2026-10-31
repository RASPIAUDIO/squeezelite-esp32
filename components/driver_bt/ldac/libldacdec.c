#include <assert.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "ldacdec.h"
#include "log.h"
#include "utility.h"
#include "bit_reader.h"
#include "imdct.h"
#include "huffCodes.h"
#include "spectrum.h"
#include "bit_allocation.h"

#define LDAC_SYNCWORDBITS   (8)
#define LDAC_SYNCWORD       (0xAA)
/** Sampling Rate **/
#define LDAC_SMPLRATEBITS   (3)
/** Channel **/
#define LDAC_CHCONFIG2BITS  (2)
enum CHANNEL {
    MONO   = 0,
    STEREO = 1
};
/** Frame Length **/
#define LDAC_FRAMELEN2BITS  (9)
/** Frame Status **/
#define LDAC_FRAMESTATBITS  (2)

/** Band Info **/
#define LDAC_NBANDBITS      (4)
#define LDAC_BAND_OFFSET    (2)

/** Band **/
#define LDAC_MAXNBANDS        16

/* Flag */
#define LDAC_FLAGBITS       (1)
#define LDAC_TRUE           (1)
#define LDAC_FALSE          (0)

 /* Mode */
#define LDAC_MODE_0         (0)
#define LDAC_MODE_1         (1)
#define LDAC_MODE_2         (2)
#define LDAC_MODE_3         (3)

/** Gradient Data **/
#define LDAC_GRADMODEBITS      (2)
#define LDAC_GRADOSBITS        (5)
#define LDAC_GRADQU0BITS       (6)
#define LDAC_GRADQU1BITS       (5)
#define LDAC_NADJQUBITS        (5)

/** Scale Factor Data **/
#define LDAC_SFCMODEBITS       1
#define LDAC_IDSFBITS          5
#define LDAC_NSFCWTBL          8
#define LDAC_SFCBLENBITS       2
#define LDAC_MINSFCBLEN_0      3
#define LDAC_MINSFCBLEN_1      2
#define LDAC_MINSFCBLEN_2      2
#define LDAC_SFCWTBLBITS       3

#define LDAC_MINIDWL1          1
#define LDAC_MAXIDWL1         15
#define LDAC_MAXIDWL2         15

// Quantization Units
#define LDAC_MAXNQUS          34
#define LDAC_MAXGRADQU        50

/***************************************************************************************************
    Weighting Tables for Scale Factor Data
***************************************************************************************************/
static const uint8_t gaa_sfcwgt_ldac[LDAC_NSFCWTBL][LDAC_MAXNQUS] = {
{
     1,  0,  0,  1,  1,  1,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  8,
},
{
     0,  1,  1,  2,  3,  4,  4,  4,  4,  5,  6,  6,  6,  6,  6,  7,
     7,  7,  7,  7,  7,  7,  8,  8,  8,  9, 10, 10, 11, 11, 12, 12, 12, 12,
},
{
     0,  1,  1,  2,  3,  3,  3,  3,  3,  4,  4,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  6,  6,  6,  7,  8,  9,  9, 10, 10, 11, 11, 11,
},
{
     0,  1,  3,  4,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  9,  9,  9, 10, 10, 10, 10,
},
{
     0,  1,  3,  4,  5,  5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 10,
    10, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13,
},
{
     1,  0,  1,  2,  2,  3,  3,  4,  4,  5,  6,  7,  7,  8,  8,  8,
     9,  9,  9,  9,  9, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11,
},
{
     0,  0,  1,  1,  2,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  5,  5,  6,  7,  7,  7,  8,  9,  9,  9,  9,
},
{
     0,  0,  1,  2,  3,  4,  4,  5,  5,  6,  7,  7,  8,  8,  8,  8,
     9,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 12,
},
};

static const uint8_t ga_nqus_ldac[LDAC_MAXNBANDS+1] = {
     0,  4,  8, 10, 12, 14, 16, 18, 20, 22, 24, 25, 26, 28, 30, 32, 34,
};


void ldacdecReset(ldacdec_t *this)
{
    if (!this) return;
    memset(this, 0, sizeof(*this));
    this->frame.channels[0].frame = &this->frame;
    this->frame.channels[1].frame = &this->frame;
}

int ldacdecInit(ldacdec_t *this)
{
    if (!this) return -1;
    ldac_InitHuffmanCodebooks();
    ldac_InitMdct();
    ldacdecReset(this);
    return 0;
}

static int decodeBand( frame_t *this, BitReaderCxt *br )
{
    this->nbrBands = ldac_ReadInt( br, LDAC_NBANDBITS ) + LDAC_BAND_OFFSET;
    LOG("nbrBands:        %d\n", this->nbrBands );
    ldac_ReadInt( br, LDAC_FLAGBITS ); // unused

    if (br->Failed || this->nbrBands > (this->frameSamples == 128 ? 12 : 16))
        return -1;

    this->quantizationUnitCount = ga_nqus_ldac[this->nbrBands];
    return 0;
}

static int decodeGradient( frame_t *this, BitReaderCxt *br )
{
    this->gradientMode = ldac_ReadInt( br, LDAC_GRADMODEBITS );
    if( this->gradientMode == LDAC_MODE_0 )
    {
        this->gradientStartUnit  = ldac_ReadInt( br, LDAC_GRADQU0BITS );
        this->gradientEndUnit    = ldac_ReadInt( br, LDAC_GRADQU0BITS ) + 1;
        this->gradientStartValue = ldac_ReadInt( br, LDAC_GRADOSBITS );
        this->gradientEndValue   = ldac_ReadInt( br, LDAC_GRADOSBITS );
        LOG("gradient\n\tqu [%3d,%3d]\n\tvalue [%3d,%3d]\n",
                this->gradientStartUnit, this->gradientEndUnit,
                this->gradientStartValue, this->gradientEndValue );
    } else
    {
        this->gradientStartUnit  = ldac_ReadInt( br, LDAC_GRADQU1BITS );
        this->gradientEndUnit    = 26;
        this->gradientStartValue = ldac_ReadInt( br, LDAC_GRADOSBITS );
        this->gradientEndValue   = 31;
        LOG("gradient\n\tqu [%3d,%3d\n\tvalue [%3d,%3d]\n",
                this->gradientStartUnit, this->gradientEndUnit,
                this->gradientStartValue, this->gradientEndValue );
    }

    this->gradientBoundary = ldac_ReadInt( br, LDAC_NADJQUBITS );
    if (br->Failed || this->gradientStartUnit > this->gradientEndUnit ||
        this->gradientEndUnit > LDAC_MAXGRADQU ||
        this->gradientBoundary > this->quantizationUnitCount) return -1;
    return 0;
}

static void calculateGradient( frame_t *this )
{
    int valueCount = this->gradientEndValue - this->gradientStartValue;
    int unitCount = this->gradientEndUnit - this->gradientStartUnit;

    memset( this->gradient, 0, sizeof( this->gradient ) );

    for( int i=0; i<this->gradientEndUnit; ++i )
        this->gradient[i] = -this->gradientStartValue;
    for( int i=this->gradientEndUnit; i<this->quantizationUnitCount; ++i )
        this->gradient[i] = -this->gradientEndValue;

    if( unitCount > 0 )
    {
        const uint8_t *curve = ldac_gradientCurves[unitCount-1];
        if(  valueCount > 0 )
        {
            for( int i=this->gradientStartUnit; i<this->gradientEndUnit; ++i )
            {
                this->gradient[i] -= ((curve[i-this->gradientStartUnit] * (valueCount-1)) >> 8) + 1;
            }
        } else if( valueCount < 0 )
        {
            for( int i=this->gradientStartUnit; i<this->gradientEndUnit; ++i )
            {
                this->gradient[i] += ((curve[i-this->gradientStartUnit] * (-valueCount-1)) >> 8) + 1;
            }
        }
    }

    LOG_ARRAY_LEN( this->gradient, "%3d, ", this->quantizationUnitCount );
}

static void calculatePrecisionMask(channel_t* this)
{
	memset(this->precisionMask, 0, sizeof(this->precisionMask));
	for (int i = 1; i < this->frame->quantizationUnitCount; i++)
	{
		const int delta = this->scaleFactors[i] - this->scaleFactors[i - 1];
		if (delta > 1)
		{
			this->precisionMask[i] += min(delta - 1, 5);
		}
		else if (delta < -1)
		{
			this->precisionMask[i - 1] += min(delta * -1 - 1, 5);
		}
	}
}


static void calculatePrecisions( channel_t *this )
{
    frame_t *frame = this->frame;

    for( int i=0; i<frame->quantizationUnitCount; ++i )
    {
        switch( frame->gradientMode )
        {
            case LDAC_MODE_0:
            {
                int precision = this->scaleFactors[i] + frame->gradient[i];
                precision = max( precision, LDAC_MINIDWL1 );
                this->precisions[i] = precision;
                break;
            }
            case LDAC_MODE_1:
            {
                int precision = this->scaleFactors[i] + frame->gradient[i] + this->precisionMask[i];
                if( precision > 0 )
                    precision /= 2;
                precision = max( precision, LDAC_MINIDWL1 );
                this->precisions[i] = precision;
                break;
            }
            case LDAC_MODE_2:
            {
                int precision = this->scaleFactors[i] + frame->gradient[i] + this->precisionMask[i];
                if( precision > 0 )
                    precision = ( precision * 3 ) / 8;
                precision = max( precision, LDAC_MINIDWL1 );
                this->precisions[i] = precision;
                break;
            }
            case LDAC_MODE_3:
            {
                int precision = this->scaleFactors[i] + frame->gradient[i] + this->precisionMask[i];
                if( precision > 0 )
                    precision /= 4;
                precision = max( precision, LDAC_MINIDWL1 );
                this->precisions[i] = precision;
                break;
            }
            default:
                assert(0);
                break;
        }
    }

    for( int i=0; i<frame->gradientBoundary; ++i )
    {
        this->precisions[i]++;
    }

    for( int i=0; i<frame->quantizationUnitCount; ++i )
    {
        this->precisionsFine[i] = 0;
        if( this->precisions[i] > LDAC_MAXIDWL1 )
        {
            /* The reference encoder saturates the fine word-length index at
             * 15, including after the low-band adjustment. Large scale factors
             * can otherwise produce 16 or 17 on valid transient frames. */
            this->precisionsFine[i] = min(this->precisions[i] - LDAC_MAXIDWL1, LDAC_MAXIDWL2);
            this->precisions[i] = LDAC_MAXIDWL1;
        }
    }

    LOG_ARRAY_LEN( this->precisions, "%3d, ", frame->quantizationUnitCount );
    LOG_ARRAY_LEN( this->precisionsFine, "%3d, ", frame->quantizationUnitCount );
}

static int decodeScaleFactor0( channel_t *this, BitReaderCxt *br )
{
    LOG_FUNCTION();
    frame_t *frame = this->frame;
    this->scaleFactorBitlen = ldac_ReadInt( br, LDAC_SFCBLENBITS ) + LDAC_MINSFCBLEN_0;
    this->scaleFactorOffset = ldac_ReadInt( br, LDAC_IDSFBITS );
    this->scaleFactorWeight = ldac_ReadInt( br, LDAC_SFCWTBLBITS );

    LOG("scale factor bitlen = %d\n", this->scaleFactorBitlen );
    LOG("scale factor offset = %d\n", this->scaleFactorOffset );
    LOG("scale factor weight = %d\n", this->scaleFactorWeight );

    const int mask = (1<<this->scaleFactorBitlen)-1;
    const uint8_t *weightTable = gaa_sfcwgt_ldac[this->scaleFactorWeight];
    const HuffmanCodebook* codebook = &ldac_HuffmanScaleFactorsUnsigned[this->scaleFactorBitlen];
    this->scaleFactors[0] = ldac_ReadInt( br, this->scaleFactorBitlen );
//    printf("diff =\n    ");
    for( int i=1; i<frame->quantizationUnitCount; ++i )
    {
        int diff = ldac_ReadHuffmanValue( codebook, br, 1 );

//        printf("%2d, ", diff );
        this->scaleFactors[i] = (this->scaleFactors[i-1] + diff) & mask;
        this->scaleFactors[i-1] += this->scaleFactorOffset - weightTable[i-1]; // cancel weights
    }
    this->scaleFactors[frame->quantizationUnitCount-1] += this->scaleFactorOffset - weightTable[frame->quantizationUnitCount-1];

    LOG_ARRAY_LEN( this->scaleFactors, "%2d, ", frame->quantizationUnitCount );
    return 0;
}

static int decodeScaleFactor1( channel_t *this, BitReaderCxt *br )
{
    LOG_FUNCTION();
    frame_t *frame = this->frame;
    this->scaleFactorBitlen = ldac_ReadInt( br, LDAC_SFCBLENBITS ) + LDAC_MINSFCBLEN_1;

    if( this->scaleFactorBitlen > 4 )
    {
        for( int i=0; i<frame->quantizationUnitCount; ++i )
        {
            this->scaleFactors[i] = ldac_ReadInt( br, LDAC_IDSFBITS );
        }
    } else
    {
        this->scaleFactorOffset = ldac_ReadInt( br, LDAC_IDSFBITS );
        this->scaleFactorWeight = ldac_ReadInt( br, LDAC_SFCWTBLBITS );
        const uint8_t *weightTable = gaa_sfcwgt_ldac[this->scaleFactorWeight];
        for( int i=0; i<frame->quantizationUnitCount; ++i )
        {
            this->scaleFactors[i] = ldac_ReadInt( br, this->scaleFactorBitlen ) - weightTable[i] + this->scaleFactorOffset;
        }
    }
    return 0;
}

static int decodeScaleFactor2( channel_t *this, BitReaderCxt *br )
{
    LOG_FUNCTION();
    frame_t *frame = this->frame;
    channel_t *other = &frame->channels[0];

    this->scaleFactorBitlen = ldac_ReadInt( br, LDAC_SFCBLENBITS ) + LDAC_MINSFCBLEN_2;
    LOG("scale factor bitlen: %d\n", this->scaleFactorBitlen );
    const HuffmanCodebook* codebook = &ldac_HuffmanScaleFactorsSigned[this->scaleFactorBitlen];
//    printf("diff =\n");
    for( int i=0; i<frame->quantizationUnitCount; ++i )
    {
       int diff = ldac_ReadHuffmanValue( codebook, br, 1 );
//       printf("%2d, ", diff );
       this->scaleFactors[i] = other->scaleFactors[i] + diff;
    }

    LOG_ARRAY_LEN( this->scaleFactors, "%2d, ", frame->quantizationUnitCount );
    return 0;
}

static int decodeScaleFactors( frame_t *this, BitReaderCxt *br, int channelNbr, int firstInBlock )
{
    LOG_FUNCTION();
    channel_t *channel = &this->channels[channelNbr];
    channel->scaleFactorMode = ldac_ReadInt( br, LDAC_SFCMODEBITS );
    LOG("scale factor mode = %d\n", channel->scaleFactorMode );
    if( firstInBlock )
    {
        if( channel->scaleFactorMode == LDAC_MODE_0 )
            decodeScaleFactor0( channel, br );
        else
            decodeScaleFactor1( channel, br );
    } else
    {
        if( channel->scaleFactorMode == LDAC_MODE_0 )
            decodeScaleFactor0( channel, br );
        else
            decodeScaleFactor2( channel, br );
    }
    return 0;
}

enum {
    CHANNEL_1CH = 1,
    CHANNEL_2CH = 2,
};

static const char gaa_block_setting_ldac[4][4]=
{
    {CHANNEL_1CH, 1, MONO},
    {CHANNEL_2CH, 2, MONO, MONO},
    {CHANNEL_2CH, 1, STEREO},
    {0, 0, 0},
};

static void pcmFloatTo32( frame_t *this, int32_t *pcmOut )
{
    int i=0;
    for(int smpl=0; smpl<this->frameSamples; ++smpl )
    {
        for( int ch=0; ch<this->channelCount; ++ch, ++i )
        {
            double value = (double)this->channels[ch].pcm[smpl] * 65536.0;
            pcmOut[i] = value >= INT32_MAX ? INT32_MAX :
                        value <= INT32_MIN ? INT32_MIN : (int32_t)llrint(value);
        }
    }
}

static const int channelConfigIdToChannelCount[] = { 1, 2, 2 };

int ldacdecGetChannelCount( ldacdec_t *this )
{
    return this && this->frame.channelConfigId >= 0 && this->frame.channelConfigId < 3
        ? channelConfigIdToChannelCount[this->frame.channelConfigId] : 0;
}

static const unsigned short sampleRateIdToSamplesPower[] = {
    7, 7, 8, 8
};

static const int sampleRateIdToFrequency[] = { 44100, 48000, 88200, 96000 };

int ldacdecGetSampleRate( ldacdec_t *this )
{
    return this && this->frame.sampleRateId >= 0 && this->frame.sampleRateId < 4
        ? sampleRateIdToFrequency[this->frame.sampleRateId] : 0;
}

static int decodeFrame( frame_t *this, BitReaderCxt *br )
{
    int syncWord = ldac_ReadInt( br, LDAC_SYNCWORDBITS );
    if( syncWord != LDAC_SYNCWORD )
        return -1;

    this->sampleRateId = ldac_ReadInt( br, LDAC_SMPLRATEBITS );
    this->channelConfigId = ldac_ReadInt( br, LDAC_CHCONFIG2BITS );
    this->frameLength = ldac_ReadInt( br, LDAC_FRAMELEN2BITS ) + 1;
    this->frameStatus = ldac_ReadInt( br, LDAC_FRAMESTATBITS );
    if (br->Failed || this->sampleRateId > 3 || this->channelConfigId > 2 ||
        (this->frameLength + 3) * 8 > br->Limit) return -1;
    br->Limit = (this->frameLength + 3) * 8;

    this->channelCount = channelConfigIdToChannelCount[this->channelConfigId];
    this->frameSamplesPower = sampleRateIdToSamplesPower[this->sampleRateId];
    this->frameSamples = 1<<this->frameSamplesPower;

    this->channels[0].mdct.Bits = this->frameSamplesPower;
    this->channels[1].mdct.Bits = this->frameSamplesPower;

    LOG("sampleRateId:    %d\n", this->sampleRateId );
    LOG("   sample rate:  %d\n", sampleRateIdToFrequency[this->sampleRateId] );
    LOG("   samplePower:  %d\n", this->frameSamplesPower );
    LOG("channelConfigId: %d\n", this->channelConfigId );
    LOG("frameLength:     %d\n", this->frameLength );
    LOG("frameStatus:     %d\n", this->frameStatus );

    return 0;
}

int ldacDecode32(ldacdec_t *this, const uint8_t *stream, size_t length,
                 int32_t *pcm, size_t pcm_capacity, int *bytesUsed)
{
    if (bytesUsed) *bytesUsed = 0;
    if (!this || !stream || !pcm || length < 3) return -1;
    BitReaderCxt brObject;
    BitReaderCxt *br = &brObject;
    ldac_InitBitReaderCxt(br, stream, length);
    frame_t *frame = &this->frame;
    if (decodeFrame(frame, br) < 0 ||
        pcm_capacity < (size_t)frame->frameSamples * frame->channelCount) return -1;

    int first = 0;
    for (int block = 0; block < gaa_block_setting_ldac[frame->channelConfigId][1]; ++block) {
        const int count = gaa_block_setting_ldac[frame->channelConfigId][block + 2] == STEREO ? 2 : 1;
        if (decodeBand(frame, br) < 0 || decodeGradient(frame, br) < 0) return -1;
        calculateGradient(frame);
        for (int i = first; i < first + count; ++i) {
            channel_t *channel = &frame->channels[i];
            decodeScaleFactors(frame, br, i, i == first);
            if (br->Failed) return -1;
            for (int q = 0; q < frame->quantizationUnitCount; ++q)
                if (channel->scaleFactors[q] < 0 || channel->scaleFactors[q] > 31) return -1;
            calculatePrecisionMask(channel);
            calculatePrecisions(channel);
            for (int q = 0; q < frame->quantizationUnitCount; ++q)
                if (channel->precisions[q] < 1 || channel->precisions[q] > 15 ||
                    channel->precisionsFine[q] < 0 || channel->precisionsFine[q] > 15) return -1;
            if (ldac_decodeSpectrum(channel, br) < 0 || br->Failed) return -1;
            if (ldac_decodeSpectrumFine(channel, br) < 0 || br->Failed) return -1;
            ldac_dequantizeSpectra(channel);
            ldac_scaleSpectrum(channel);
        }
        first += count;
        ldac_AlignPosition(br, 8);
        if (br->Failed) return -1;
    }
    /* Only commit the overlap state after every block has parsed successfully. */
    for (int i = 0; i < frame->channelCount; ++i) {
        channel_t *channel = &frame->channels[i];
        ldac_RunImdct(&channel->mdct, channel->spectra, channel->pcm);
        for (int j = 0; j < frame->frameSamples; ++j)
            if (!isfinite(channel->pcm[j])) return -1;
    }
    pcmFloatTo32(frame, pcm);
    if (bytesUsed) *bytesUsed = frame->frameLength + 3;
    return frame->frameSamples;
}
