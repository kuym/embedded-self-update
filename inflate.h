#ifndef EMBEDDED_DELATE_H_
#define EMBEDDED_DELATE_H_

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;

// DEFLATE (e.g. gzip) decompressor
typedef struct DeflateState {
  // Client function pointers:

  // This is the context passed opaquely to the following four callbacks.
  void* context;

  // This returns 1 if there are more bytes in the input stream, else 0.
  u8 (*bytesRemain)(void* context);

  // This returns the next byte from the input stream.
  u8 (*readByte)(void* context);

  // This writes a byte to the output stream, returning 1 if ok, else 0.
  u8 (*writeByte)(void* context, u8 value);

  // Function pointer to read a byte from the end of the output stream minus
  // `offset`.  `offset` will never be zero but the callee should check that it
  // does not refer to a byte before the start of the output stream.
  u8 (*readBack)(void* context, u32 offset);

  u32 bitWindow;
  s16 haveBits;
  u16 usedLCodes, usedDCodes;
  u8 distTree[12 * 3], litTree[16 * 3];
  u16 valueTable[337];

} DeflateState;

u8 DeflateInit(DeflateState* context);
u8 DeflateDecodeNextBlock(DeflateState* context);

#endif // #ifndef EMBEDDED_DELATE_H_
