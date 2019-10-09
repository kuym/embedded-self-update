#include "inflate.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct State {
  unsigned char* in;
  size_t inOffset;
  size_t inLength;
  unsigned char* out;
  size_t outOffset;
  size_t outLength;
  size_t decompressedLength;
  FILE* outFile;

} State;

unsigned char bytesRemain(void* context) {
  State* s = (State*)context;
  return s->inOffset < s->inLength;
}

unsigned char readByte(void* context) {
  State* s = (State*)context;
  return s->in[s->inOffset++];
}

unsigned char writeByte(void* context, unsigned char value) {
  State* s = (State*)context;

  if(s->outLength < s->decompressedLength)
    s->outLength++;
  else
    return 0;

  if(s->outOffset >= s->outLength)
    s->outOffset -= s->outLength;

  s->out[s->outOffset++] = value;
  fputc(value, s->outFile);
  return 0;
}

unsigned char readBack(void* context, unsigned int offset) {
  State* s = (State*)context;

  if(offset > s->outLength) {
    // This shouldn't happen!
    printf("Read at offset %u which is beyond end (%zu) \n", offset,
      s->outLength);
    abort();
    return 0;
  }

  size_t p;
  if(offset > s->outOffset)
    p = (s->outLength - (offset - s->outOffset));
  else
    p = (s->outOffset - offset);

  return s->out[p];
}

int main(int argc, char const* const* argv) {
  if(argc < 3) {
    printf("Usage: <tool> input.gz output\n");
    return -1;
  }

  FILE* input = fopen(argv[1], "r");
  if(input == 0) {
    printf("Error: Unable to open input file \"%s\"\n", argv[1]);
    return -1;
  }

  FILE* output = fopen(argv[2], "w");
  if(output == 0) {
    fclose(input);
    printf("Error: Unable to open output file \"%s\"\n", argv[2]);
    return -1;
  }

  State ioState;

  fseek(input, 0, SEEK_END);

  ioState.inLength = ftell(input);
  ioState.inOffset = 0;
  ioState.in = (unsigned char*)malloc(ioState.inLength);

  fseek(input, 0, SEEK_SET);
  fread(ioState.in, 1, ioState.inLength, input);
  fclose(input);

  int magic = ioState.in[0] | (ioState.in[1] << 8);
  int compression = ioState.in[2];
  int flags = ioState.in[3];
  int timestamp = ioState.in[4] | (ioState.in[5] << 8) | (ioState.in[6] << 16) |
    (ioState.in[7] << 24);
  int extraFlags = ioState.in[8];
  int platform = ioState.in[9];
  ioState.decompressedLength = ioState.in[ioState.inLength - 4] |
    (ioState.in[ioState.inLength - 3] << 8) |
    (ioState.in[ioState.inLength - 2] << 16) |
    (ioState.in[ioState.inLength - 1] << 24);

  // If showing debug info:
  if(1) {
    printf("Attributes:\n  magic = 0x%04x, compression = 0x%02x, flags = "
      "0x%02x, timestamp = %i, extraFlags = 0x%02x, platform = 0x%02x, "
      "decompressed = %zu\n",
      magic, compression, flags, timestamp, extraFlags, platform,
      ioState.decompressedLength);
  }

  // Skip over the gzip header.
  ioState.inOffset = 10;

  // Display the decompressed filename, if stored in the header.
  if(flags & 8) {
    while(ioState.in[ioState.inOffset++] != 0);
    printf("filename = \"");
    fwrite(ioState.in + 10, 1, ioState.inOffset - 10 - 1, stdout);
    printf("\"\n");
  }

  ioState.outOffset = 0;
  ioState.outLength = 0;
  ioState.out = (unsigned char*)malloc(ioState.decompressedLength);
  ioState.outFile = output;

  DeflateState* state = malloc(sizeof(DeflateState));
  DeflateInit(state);
  state->bytesRemain = &bytesRemain;
  state->readByte = &readByte;
  state->writeByte = &writeByte;
  state->readBack = &readBack;
  state->context = &ioState;

  int result = 0;
  while((result = DeflateDecodeNextBlock(state)) == 0);

  fclose(ioState.outFile);

  return result != 1;
}
