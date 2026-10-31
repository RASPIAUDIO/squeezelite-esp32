/* Bounded reader for the MIT libldacdec port. Huffman lookahead may pad
 * with zero bits, but consuming beyond the actual frame always fails. */
#include "bit_reader.h"
#include "utility.h"
#include <limits.h>

void ldac_InitBitReaderCxt(BitReaderCxt *br, const void *buffer, size_t length) {
    br->Buffer = buffer;
    br->Position = 0;
    br->Failed = !buffer || length > INT_MAX / 8;
    br->Limit = br->Failed ? 0 : (int)length * 8;
}

uint32_t ldac_PeekInt(BitReaderCxt *br, int bits) {
    if (br->Failed || bits < 0 || bits > 32 || br->Position < 0 || br->Position > br->Limit) {
        br->Failed = 1;
        return 0;
    }
    uint32_t value = 0;
    int pos = br->Position;
    while (bits > 0) {
        int take = 8 - pos % 8;
        if (take > bits) take = bits;
        unsigned byte = pos < br->Limit ? br->Buffer[pos / 8] : 0;
        value = (value << take) | ((byte >> (8 - pos % 8 - take)) & ((1U << take) - 1));
        bits -= take;
        pos += take;
    }
    return value;
}

uint32_t ldac_ReadInt(BitReaderCxt *br, int bits) {
    if (bits < 0 || bits > 32 || br->Position > br->Limit - bits) {
        br->Failed = 1;
        return 0;
    }
    uint32_t value = ldac_PeekInt(br, bits);
    if (!br->Failed) br->Position += bits;
    return value;
}

int32_t ldac_ReadSignedInt(BitReaderCxt *br, int bits) {
    return ldac_SignExtend32(ldac_ReadInt(br, bits), bits);
}

int32_t ldac_ReadOffsetBinary(BitReaderCxt *br, int bits) {
    if (bits < 1 || bits > 31) { br->Failed = 1; return 0; }
    return (int)ldac_ReadInt(br, bits) - (1 << (bits - 1));
}

void ldac_AlignPosition(BitReaderCxt *br, unsigned multiple) {
    if (!multiple || multiple > INT_MAX || br->Position < 0 || br->Position > br->Limit) {
        br->Failed = 1; return;
    }
    unsigned add = (multiple - (unsigned)br->Position % multiple) % multiple;
    if (add > (unsigned)(br->Limit - br->Position)) { br->Failed = 1; return; }
    br->Position += add;
}
