// Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

#ifndef EMBEDDED_DEFLATE_H_
#define EMBEDDED_DEFLATE_H_

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;

enum {
  // Number of bits of the 3-byte rolling hash used to find matches.  Controls
  // the memory-use to compression-efficiency tradeoff: the match-finder table
  // is `2 << kDeflateHashBits` bytes.  Useful values are 8 (512 bytes, weak
  // matching and less efficient compression) through 15 (64KB, near-optimal for
  // a single-candidate finder).
  kDeflateHashBits = 8,

  // Number of input bytes covered by one DEFLATE block.  Must not exceed 32768:
  // symbol frequencies are counted in u16s, and hash slots hold a position
  // relative to the start of the window as a u16.
  kDeflateBlockLength = 32768,

  // Maximum match distance.  RFC 1951 caps this at 32768, but you can shorten
  // it if necessary.
  kDeflateWindowLength =  32768,

  // Sizes of the two DEFLATE alphabets.  The literal/length alphabet only uses
  // symbols 0..285, but codes 286 and 287 must participate in the canonical
  // code assignment for the fixed (RFC 1951 section 3.2.6) codebook to come out
  // right.
  kDeflateLiteralCodes = 288,
  kDeflateDistanceCodes = 32,
  kDeflateLengthCodes = 19,

  // Maximum code lengths.  RFC 1951 permits 15 bits for the literal/length and
  // distance alphabets and 7 bits for the code-length alphabet, but inflate.c
  // sizes its decode tables by the number of *distinct* code lengths present
  // (`litTree` holds 16 rows, `distTree` 12, and its code-length decoder 6).
  // Holding the encoder to these limits keeps every stream it produces
  // decodable by inflate.c; the cost is a fraction of a percent of ratio.
  kDeflateMaxLiteralBits = 15,
  kDeflateMaxDistanceBits = 12,
  kDeflateMaxLengthBits = 6,
};


// DEFLATE (e.g. gzip) compressor
typedef struct DeflateState {
  // Client function pointers:

  // This is the context passed opaquely to the following two callbacks.
  void* context;

  // This returns the byte at `offset` bytes from the start of the buffer being
  // compressed.  `offset` is always less than the `length` handed to
  // DeflateEncodeInit().  Unlike inflate.c's sequential reader this is a random
  // access, because the compressor rewinds over the input rather than keeping
  // a window of it in RAM.
  u8 (*readByte)(void* context, u32 offset);

  // This writes a byte to the output stream, returning 1 if ok, else 0.
  u8 (*writeByte)(void* context, u8 value);

  // Position of the next input byte to be compressed, and the total length of
  // the input.  Both are maintained by the compressor; `position` is useful to
  // the client as a progress indicator.
  u32 length, position;

  u32 bitWindow;
  s16 haveBits;

  u16 usedLCodes, usedDCodes;

  // Extent of the block currently being compressed, and the oldest input byte
  // a match in it may refer to.
  u32 blockStart, blockEnd, windowStart;

  // Count of the "extra" (non-Huffman) bits the current block will emit.
  u32 extraBits;

  // Holds a symbol frequency per code while the block is being measured, then
  // that code's bit-reversed canonical Huffman code while it is being emitted.
  // Literal/length codes occupy the first kDeflateLiteralCodes entries and
  // distance codes the following kDeflateDistanceCodes entries, following the
  // convention of InflateState's `valueTable`.
  u16 valueTable[kDeflateLiteralCodes + kDeflateDistanceCodes];
  u8 lengthTable[kDeflateLiteralCodes + kDeflateDistanceCodes];

  // The same pair for the code-length alphabet that describes a dynamic block's
  // two trees.
  u16 clValueTable[kDeflateLengthCodes];
  u8 clLengthTable[kDeflateLengthCodes];

  // The match finder's hash table and the Huffman tree builder's scratch space
  // are never live at the same instant: the trees are built after a block has
  // been surveyed and before it is re-scanned for emission, and the table is
  // cleared at the top of each scan.  This makes a union possible here.
  union {
    u16 hashHead[1 << kDeflateHashBits];
    struct {
      u16 weight[kDeflateLiteralCodes];
      u16 order[kDeflateLiteralCodes];
    } tree;
  } work;

} DeflateState;

u8 DeflateEncodeInit(DeflateState* state, u32 length);
u8 DeflateEncodeNextBlock(DeflateState* state);

#endif // #ifndef EMBEDDED_DEFLATE_H_
