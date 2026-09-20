// Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

// Compresses a file into a gzip container, the complement of inflate-test.c.
// The DEFLATE payload is produced by deflate.c and the trailer checksum by
// crc32.c, so the resulting file can be checked either with this toolkit's own
// inflate-test or with any stock gzip.

#include "deflate.h"
#include "crc32.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct State {
  unsigned char* in;
  size_t inLength;
  size_t outLength;
  FILE* outFile;

} State;

unsigned char readByte(void* context, unsigned int offset) {
  State* s = (State*)context;

  if(offset >= s->inLength) {
    // This shouldn't happen!
    printf("Read at offset %u which is beyond end (%zu) \n", offset,
      s->inLength);
    abort();
    return 0;
  }

  return s->in[offset];
}

unsigned char writeByte(void* context, unsigned char value) {
  State* s = (State*)context;

  if(fputc(value, s->outFile) == EOF)
    return 0;

  s->outLength++;
  return 1;
}

static void writeLE32(FILE* f, unsigned int value) {
  fputc((int)(value & 0xFF), f);
  fputc((int)((value >> 8) & 0xFF), f);
  fputc((int)((value >> 16) & 0xFF), f);
  fputc((int)((value >> 24) & 0xFF), f);
}

int main(int argc, char const* const* argv) {
  if(argc < 3) {
    printf("Usage: <tool> input output.gz\n");
    return -1;
  }

  FILE* input = fopen(argv[1], "rb");
  if(input == 0) {
    printf("Error: Unable to open input file \"%s\"\n", argv[1]);
    return -1;
  }

  FILE* output = fopen(argv[2], "wb");
  if(output == 0) {
    fclose(input);
    printf("Error: Unable to open output file \"%s\"\n", argv[2]);
    return -1;
  }

  State ioState;

  fseek(input, 0, SEEK_END);

  ioState.inLength = ftell(input);
  ioState.in = (unsigned char*)malloc(ioState.inLength + 1);

  fseek(input, 0, SEEK_SET);
  if(fread(ioState.in, 1, ioState.inLength, input) != ioState.inLength) {
    fclose(input);
    fclose(output);
    printf("Error: Short read on input file \"%s\"\n", argv[1]);
    return -1;
  }
  fclose(input);

  // Emit a minimal gzip header (RFC 1952): magic, DEFLATE compression, no
  // flags, no timestamp, no extra flags, unknown platform.
  fputc(0x1F, output);
  fputc(0x8B, output);
  fputc(0x08, output);
  fputc(0x00, output);
  writeLE32(output, 0);
  fputc(0x00, output);
  fputc(0xFF, output);

  ioState.outLength = 0;
  ioState.outFile = output;

  DeflateState* state = malloc(sizeof(DeflateState));
  DeflateEncodeInit(state, (u32)ioState.inLength);
  state->readByte = &readByte;
  state->writeByte = &writeByte;
  state->context = &ioState;

  int result = 0, blocks = 0;
  while((result = DeflateEncodeNextBlock(state)) == 0)
    blocks++;
  blocks++;

  // The gzip trailer is the CRC-32 of the uncompressed data followed by its
  // length, both little-endian.
  writeLE32(output, CRC32(0, ioState.in, (u32)ioState.inLength));
  writeLE32(output, (unsigned int)ioState.inLength);

  fclose(output);

  // If showing debug info:
  if(1) {
    printf("Attributes:\n  uncompressed = %zu, deflated = %zu, blocks = %i, "
      "ratio = %.1f%%\n",
      ioState.inLength, ioState.outLength, blocks,
      ioState.inLength? (100.0 * ioState.outLength / ioState.inLength) : 0.0);
  }

  free(ioState.in);
  free(state);

  return result != 1;
}
