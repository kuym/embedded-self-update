// Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

#ifndef EMBEDDED_DELATE_H_
#define EMBEDDED_DELATE_H_

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;

// Values returned by DeflateDecodeNextBlock().  Everything from
// kInflateBadStoredLength up is a reason the decoder stopped early; it is also
// held in `InflateState.status`, and once set it stays set, so every later call
// returns it again without touching the callbacks.
enum {
  // The block was decoded and more follow.
  kInflateMore = 0,
  // The final block was decoded.
  kInflateDone = 1,
  // A stored block's length did not match its one's complement.
  kInflateBadStoredLength = 2,
  // A block had the reserved type 3.
  kInflateBadBlockType = 3,
  // The input ran out (`bytesRemain` returned 0) before the end of the stream.
  kInflateTruncated = 4,
  // The stream is malformed, or uses a Huffman code with more distinct code
  // lengths than the decode tables below hold.
  kInflateCorrupt = 5,
  // `writeByte` returned 0.
  kInflateStopped = 6,
};

// DEFLATE (e.g. gzip) decompressor
typedef struct InflateState {
  // Client function pointers:

  // This is the context passed opaquely to the following four callbacks.
  void* context;

  // This returns 1 if there are more bytes in the input stream, else 0.
  u8 (*bytesRemain)(void* context);

  // This returns the next byte from the input stream.
  u8 (*readByte)(void* context);

  // This writes a byte to the output stream, returning 1 if ok, else 0.
  // Returning 0 stops the decoder: no callback is called again and
  // DeflateDecodeNextBlock() returns kInflateStopped.
  u8 (*writeByte)(void* context, u8 value);

  // Function pointer to read a byte from the end of the output stream minus
  // `offset`.  `offset` will never be zero but the callee should check that it
  // does not refer to a byte before the start of the output stream.  The byte
  // returned is passed straight to `writeByte`, so a callee that cannot supply
  // it can return anything and have `writeByte` return 0.
  u8 (*readBack)(void* context, u32 offset);

  u32 bitWindow;
  s16 haveBits;
  u16 usedLCodes, usedDCodes;
  u8 status, litRows, distRows;
  u8 distTree[12 * 3], litTree[16 * 3];
  u16 valueTable[337];

} InflateState;

u8 DeflateInit(InflateState* context);

// Decodes one block, returning one of the kInflate* values above.  Call this
// function repeatedly until it returns something other than kInflateMore.
u8 DeflateDecodeNextBlock(InflateState* context);

#endif // #ifndef EMBEDDED_DELATE_H_
