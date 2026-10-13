#include "aes128.h"

#include <stdio.h>
#include <string.h>

// Known-answer vectors from NIST FIPS 197, Appendix C.
static const u8 kPlaintext[16] = {
  0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC,
  0xDD, 0xEE, 0xFF
};

// The 128-bit key is the first 16 bytes of the 256-bit key.
static const u8 kKey[32] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
  0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

// Appendix C.1
static const u8 kCiphertext128[16] = {
  0x69, 0xC4, 0xE0, 0xD8, 0x6A, 0x7B, 0x04, 0x30, 0xD8, 0xCD, 0xB7, 0x80, 0x70,
  0xB4, 0xC5, 0x5A
};

// Appendix C.3
static const u8 kCiphertext256[16] = {
  0x8E, 0xA2, 0xB7, 0xCA, 0x51, 0x67, 0x45, 0xBF, 0xEA, 0xFC, 0x49, 0x90, 0x4B,
  0x49, 0x60, 0x89
};

static int check(char const* name, u8 const* actual) {
  int ok = (memcmp(actual, kPlaintext, 16) == 0);
  printf("  %s  %s: ", ok ? "ok  " : "FAIL", name);
  for(int i = 0; i < 16; i++) {
    printf("%02x", actual[i]);
  }
  printf("\n");
  return !ok;
}

int main(void) {
  u8 out[16];
  int fail = 0;

  AES128DecipherBlock(kCiphertext128, kKey, out);
  fail += check("AES-128 decipher (FIPS 197 C.1)", out);

  AES256DecipherBlock(kCiphertext256, kKey, out);
  fail += check("AES-256 decipher (FIPS 197 C.3)", out);

  // Deciphering in place must also work, since callers stream through a buffer.
  memcpy(out, kCiphertext256, 16);
  AES256DecipherBlock(out, kKey, out);
  fail += check("AES-256 decipher in place", out);

  printf("aes: %i failed\n", fail);
  return fail != 0;
}
