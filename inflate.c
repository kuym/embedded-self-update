#include "inflate.h"

#define EXECUTE_FROM_RAM

#include <string.h> // memset()
#include <stdlib.h> // abort()

// DEFLATE (IETF RFC 1951) implementation

// LUTs are 24-bit values with the upper 12 bits as length and the bottom 12
//   bits as symbol value.
// Length tables, used to construct LUTs, have a length in the upper 4 bits and
//   value in the lower 12 bits.

EXECUTE_FROM_RAM
static u16 readBits(DeflateState* state, u8 b) {
  if((b > 16) || (state->haveBits < 0)) {
    // TODO: make this behavior configurable.
    abort();
  }

  if(state->haveBits < b) {
    while((state->haveBits < 25) &&
      state->bytesRemain(state->context)) {
      state->bitWindow |= (u32)(state->readByte(state->context)
        << state->haveBits);
      state->haveBits += 8;
    }

    if(state->haveBits < b)
      return 0;
  }

  u16 v = (u16)(state->bitWindow & ((1 << b) - 1));
  state->bitWindow >>= b;
  state->haveBits -= b;
  return v;
}

EXECUTE_FROM_RAM
static u16 readReverseBits(DeflateState* state, u16 b) {
  static const u8 kBitswap[] =
    {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};

  u16 v = 0;
  while(1) {
    switch(b) {
    case 1: return (u16)((v << 1) | readBits(state, 1));
    case 2: return (u16)((v << 2) | kBitswap[readBits(state, 2) << 2]);
    case 3: return (u16)((v << 3) | kBitswap[readBits(state, 3) << 1]);
    case 4: return (u16)((v << 4) | kBitswap[readBits(state, 4)]);
    default:
      v = (u16)((v << 4) | kBitswap[readBits(state, 4)]);
      b -= 4;
      break;
    }
  }
}

EXECUTE_FROM_RAM
static inline void swap(u16* l, u16* r) {
  u16 t = *r;
  *r = *l;
  *l = t;
}

// This is a special implementaion of QuickSort which sorts elements of the
// look-up tables (trees) first by the top four bits of each table element and
// then by the bottom 12 bits, with a special property that zero-valued elements
// are moved to the end of the list so they can be truncated in a post-sort
// operation.
EXECUTE_FROM_RAM
static void deflateQS(u16* array, s16 left, s16 right) {
  if(left < right) {
    swap(&array[(right + left + 1) >> 1], &array[right]);

    s16 storeIndex = left, v;
    for(s16 i = left; i < right; i++) {
      u16 l = array[i], r = array[right];
      if(((l >> 12) == 0))  v = 0;
      else if(((r >> 12) == 0))  v = 1;
      else {
        s16 d = (l >> 12) - (r >> 12);
        v = ((d == 0)? ((l & 0xFFF) - (r & 0xFFF)) : d) < 0;
      }
      if(v)
        swap(&array[i], &array[storeIndex++]);
    }
    swap(&array[storeIndex], &array[right]);

    deflateQS(array, left, storeIndex - 1);
    deflateQS(array, storeIndex + 1, right);
  }
}

EXECUTE_FROM_RAM
static inline void lookupTableWrite(u8* table, u16 index, u32 value) {
  table += (index * 3);
  table[0] = (u8)(value >> 16);
  table[1] = (u8)(value >> 8);
  table[2] = (u8)value;
}

EXECUTE_FROM_RAM
static inline u32 lookupTableRead(u8 const* table, u16 index) {
  table += (index * 3);
  return (((u32)table[0]) << 16) | (((u32)table[1]) << 8) | (u32)table[2];
}

EXECUTE_FROM_RAM
static u16 makeTable(u8* lutOut, u16* lutIn, s16 count) {
  deflateQS(lutIn, 0, count - 1);
  // Trim codes with zero bit-length from the end.
  while((count > 0) && ((lutIn[count - 1] >> 12) == 0))
    count--;

  u16 lastMax = 0, lastBits = (lutIn[0] >> 12), di = 0, sc = 0;
  u32 moreBits = lastBits;

  lookupTableWrite(lutOut, 0, 0 | (moreBits << 16));
  for(u16 i = 1; i < count; i++) {
    u16 bits = (lutIn[i] >> 12);
    if(bits > lastBits) {
      lastMax = sc;
      moreBits = bits - lastBits;
      sc = (u16)((sc + 1) << moreBits);

      lookupTableWrite(lutOut, ++di, (sc - lastMax) | (moreBits << 16));

      lastBits = bits;
    } else {
      sc++;
      lookupTableWrite(lutOut, di, (sc - lastMax) | (moreBits << 16));
    }
  }

  return di + 1;
}

// Decodes a Huffman-encoded symbol from the bitstream using the provided
// codebook look-up table and value table.  The encoding of the look-up table
// places the number of bits, `b`, that should be read in the uppermost 8 bits
// of each 24-bit LUT entry, and the maximum value expressed with a `b`-bit code
// in the bottom 16 bits of the LUT entry.  If the value, `v`, read from the
// bitstream is greater than the maximum expected, the function repeats at the
// next row of the table, reading another `b` bits and comparing with the next
// maximum, and so on.  If any value `v` is less than the maximum for the
// current row, its value is looked up from the value table, where the lowest 12
// bits (of each 16-bit entry) contains the logical symbol, which may be a
// literal or length symbol or a distance depending on the context.
EXECUTE_FROM_RAM
static u16 decodeSymbol(
  DeflateState* state,
  u8 const* lut,
  u16 const* valueTable
) {
  u16 i = 0, code = 0, sc = 0, base = 0, max = 0;
  while(1) {
    u32 maxStep = lookupTableRead(lut, i++);
    u16 moreBits = (maxStep >> 16);
    max += (maxStep & 0xFFFF);
    sc = (u16)(sc << moreBits);
    code = (u16)((code << moreBits) | readReverseBits(state, moreBits));

    if(code <= max)
      return valueTable[base + (code - sc)] & 0xFFF;

    base += (max - sc + 1);
    sc = max + 1;
  }
}

EXECUTE_FROM_RAM
static void decodeCompressedTables(
  DeflateState* state,
  u8 const* lut,
  u16 const* valueTable
) {
  u16 lastLength = 0;
  u16 i = 0, sym = 0;
  for(; i < (state->usedLCodes + state->usedDCodes); i++, sym++) {
    if(sym >= state->usedLCodes) {
      sym -= state->usedLCodes;
    }

    u16 v = decodeSymbol(state, lut, valueTable);

    if(v >= 16) {
      u16 rep = 0, value = 0;
      if(v == 17)
        // Repeat code-length '0' 3-10 times (as defined by the next three bits)
        rep = 3 + readBits(state, 3);
      else if(v == 18)
        // Repeat code-length '0' 3-10 times (as defined by the next seven bits)
        rep = 11 + readBits(state, 7);
      else if(v == 16) {
        // Repeat the previous code-length 3-6 times (as defined by the next
        // two bits)
        rep = 3 + readBits(state, 2);
        value = lastLength;
      } else {
        abort();
      }

      while((rep > 0) && (i < 337)) {
        state->valueTable[i] = value? ((u16)(sym | (value << 12))) : 0;
        rep--;
        i++;
        sym++;
      }
      i--;
      sym--;
    }
    else {
      state->valueTable[i] = v? ((u16)(sym | (v << 12))) : 0;
      lastLength = v;
    }
  }

  // Fill the remainder of the value table with zeroes.
  for(; i < 337; i++) {
    state->valueTable[i] = 0;
  }
}

EXECUTE_FROM_RAM
static u8 decodeDEFLATEData(DeflateState* state) {
  static u8 const kLengthLUTMinus10[] = {1, 3, 5, 7, 9, 13, 17, 21, 25, 33, 41,
    49, 57, 73, 89, 105, 121, 153, 185, 217, 248};

  while(1) {
    if((state->haveBits == 0) && !state->bytesRemain(state->context)) {
      return 0;
    }
    u16 symbol = decodeSymbol(state, state->litTree, state->valueTable);

    if(symbol >= 256) {
      u16 len;

      if(symbol >= 265) {
        if(symbol == 285)
          len = 258;
        else {
          u16 s = (symbol - 265);
          len = 10 + kLengthLUTMinus10[s] + readBits(state, (u8)(1 + (s >> 2)));
        }
      }
      else if(symbol == 256) {
        break;
      }
      else
        len = (symbol - 254);

      u16 dist, distCode = decodeSymbol(state, state->distTree,
        &state->valueTable[state->usedLCodes]);
      if(distCode < 4)
        dist = (1 + distCode);
      else {
        u16 s = (distCode - 2), b = (s >> 1);
        s = (u16)(1 + ((2 << b) + ((s & 1) << b)));
        dist = (s + readBits(state, (u8)b));
      }

      if(len > 258) {
        // Error: length is over 258.
        abort();
      }

      while(len-- > 0)
        state->writeByte(
          state->context,
          state->readBack(state->context, dist)
        );
    }
    else
      state->writeByte(state->context, (u8)symbol);
  }

  return 0;
}

EXECUTE_FROM_RAM
static u8 decodeUncompressedBlock(DeflateState* state) {
  u8 skip = (state->haveBits & 7);
  state->bitWindow >>= skip;
  state->haveBits -= skip;

  u16 length = readBits(state, 16), invLength = readBits(state, 16);

  if((((~length) ^ invLength) & 0xFFFF) != 0)
    return 2;

  while(length-- > 0)
    state->writeByte(state->context, (u8)readBits(state, 8));

  return 0;
}

EXECUTE_FROM_RAM
static u8 decodeFixedTableBlock(DeflateState* state) {
  {
    u16 i = 0;
    for(; i < 144; i++)
      state->valueTable[i] = i | (8 << 12);
    for(; i < 256; i++)
      state->valueTable[i] = i | (9 << 12);
    for(; i < 280; i++)
      state->valueTable[i] = i | (7 << 12);
    for(; i < 288; i++)
      state->valueTable[i] = i | (8 << 12);
  }
  makeTable(state->litTree, state->valueTable, 288);
  // 3.2.6. "Distance codes 0-31 are represented by (fixed-length) 5-bit codes,
  //   with possible additional bits as shown in the table shown in Paragraph
  //   3.2.5, above.  Note that distance codes 30-31 will never actually occur
  //   in the compressed data.
  for(u8 i = 0; i < 32; i++)
    state->valueTable[288 + i] = i | (5 << 12);
  makeTable(state->distTree, &state->valueTable[288], 32);
  
  state->usedLCodes = 288;
  state->usedDCodes = 32;

  return decodeDEFLATEData(state);
}

EXECUTE_FROM_RAM
static u8 decodeDynamicTableBlock(DeflateState* state) {
  // RFC1951 section 3.2.7
  u16 hNumLiterals = 257 + readBits(state, 5),
    hNumDistances = 1 + readBits(state, 5),
    hCodeLengthCodes = 4 + readBits(state, 4);

  {
    u8 codeLengthDecoder[6 * 3] = {0};
    u16 codeLengthTable[19] = {0};
    static const u8 kOrderLUT[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12,
      3, 13, 2, 14, 1, 15};
    for(u8 i = 0; i < 19; i++)
      codeLengthTable[kOrderLUT[i]] = (u16)(kOrderLUT[i] |
        (((i < hCodeLengthCodes)? readBits(state, 3) : 0) << 12));

    makeTable(codeLengthDecoder, codeLengthTable, 19);

    // Decode the next `(hNumLiterals + hNumDistances)` symbols using the
    // temporary `codeLengthTable` and populate state->valueTable accordingly.
    // The literal and length values occupy the first `hNumLiterals` indices and
    // the distance codes occupy the following `hNumDistances` indices.
    state->usedLCodes = hNumLiterals;
    state->usedDCodes = hNumDistances;
    decodeCompressedTables(state, codeLengthDecoder, codeLengthTable);
  }

  // Create the tree for literal and length codes.
  memset(state->litTree, 0, sizeof(state->litTree));
  makeTable(state->litTree, state->valueTable, state->usedLCodes);

  // Create the tree for distance codes.
  memset(state->distTree, 0, sizeof(state->distTree));
  makeTable(state->distTree, &state->valueTable[state->usedLCodes],
    state->usedDCodes);

  // Finally, now that we have the custom value table, literal and length tree
  // and the distance tree, decode the block itself.
  return decodeDEFLATEData(state);
}

EXECUTE_FROM_RAM
u8 DeflateInit(DeflateState* state) {
  state->bitWindow = 0;
  state->haveBits = 0;
  return 0;
}

EXECUTE_FROM_RAM
u8 DeflateDecodeNextBlock(DeflateState* state) {
  // Decode the block header.
  u8 isLastBlock = (u8)readBits(state, 1), blockType = (u8)readBits(state, 2),
    r = 0;

  switch(blockType) {
  case 0:   r = decodeUncompressedBlock(state); break;
  case 1:   r = decodeFixedTableBlock(state); break;
  case 2:   r = decodeDynamicTableBlock(state); break;
  default:  r = 3;  // invalid!
  }

  return (r != 0)? r : isLastBlock;
}
