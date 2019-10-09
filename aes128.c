// Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

#include "aes128.h"

#define EXECUTE_FROM_RAM

#include <string.h> // memcpy()
#include <stdlib.h>  // abort()

// AES-128 (NIST FIPS 197) Implementation

static const u8 kAESStateBoxInverse[256] = {
  0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38, 0xBF, 0x40, 0xA3, 0x9E, 0x81,
  0xF3, 0xD7, 0xFB, 0x7C, 0xE3, 0x39, 0x82, 0x9B, 0x2F, 0xFF, 0x87, 0x34, 0x8E,
  0x43, 0x44, 0xC4, 0xDE, 0xE9, 0xCB, 0x54, 0x7B, 0x94, 0x32, 0xA6, 0xC2, 0x23,
  0x3D, 0xEE, 0x4C, 0x95, 0x0B, 0x42, 0xFA, 0xC3, 0x4E, 0x08, 0x2E, 0xA1, 0x66,
  0x28, 0xD9, 0x24, 0xB2, 0x76, 0x5B, 0xA2, 0x49, 0x6D, 0x8B, 0xD1, 0x25, 0x72,
  0xF8, 0xF6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xD4, 0xA4, 0x5C, 0xCC, 0x5D, 0x65,
  0xB6, 0x92, 0x6C, 0x70, 0x48, 0x50, 0xFD, 0xED, 0xB9, 0xDA, 0x5E, 0x15, 0x46,
  0x57, 0xA7, 0x8D, 0x9D, 0x84, 0x90, 0xD8, 0xAB, 0x00, 0x8C, 0xBC, 0xD3, 0x0A,
  0xF7, 0xE4, 0x58, 0x05, 0xB8, 0xB3, 0x45, 0x06, 0xD0, 0x2C, 0x1E, 0x8F, 0xCA,
  0x3F, 0x0F, 0x02, 0xC1, 0xAF, 0xBD, 0x03, 0x01, 0x13, 0x8A, 0x6B, 0x3A, 0x91,
  0x11, 0x41, 0x4F, 0x67, 0xDC, 0xEA, 0x97, 0xF2, 0xCF, 0xCE, 0xF0, 0xB4, 0xE6,
  0x73, 0x96, 0xAC, 0x74, 0x22, 0xE7, 0xAD, 0x35, 0x85, 0xE2, 0xF9, 0x37, 0xE8,
  0x1C, 0x75, 0xDF, 0x6E, 0x47, 0xF1, 0x1A, 0x71, 0x1D, 0x29, 0xC5, 0x89, 0x6F,
  0xB7, 0x62, 0x0E, 0xAA, 0x18, 0xBE, 0x1B, 0xFC, 0x56, 0x3E, 0x4B, 0xC6, 0xD2,
  0x79, 0x20, 0x9A, 0xDB, 0xC0, 0xFE, 0x78, 0xCD, 0x5A, 0xF4, 0x1F, 0xDD, 0xA8,
  0x33, 0x88, 0x07, 0xC7, 0x31, 0xB1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xEC, 0x5F,
  0x60, 0x51, 0x7F, 0xA9, 0x19, 0xB5, 0x4A, 0x0D, 0x2D, 0xE5, 0x7A, 0x9F, 0x93,
  0xC9, 0x9C, 0xEF, 0xA0, 0xE0, 0x3B, 0x4D, 0xAE, 0x2A, 0xF5, 0xB0, 0xC8, 0xEB,
  0xBB, 0x3C, 0x83, 0x53, 0x99, 0x61, 0x17, 0x2B, 0x04, 0x7E, 0xBA, 0x77, 0xD6,
  0x26, 0xE1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0C, 0x7D
};

// Slow (O(n)) reverse lookup saves 220 bytes of flash, but is only used four
//   times per key-expansion step.
EXECUTE_FROM_RAM
static u8 aesStateBox(u8 i) {
  for(int j = 0; j < 256; j++) {
    if(kAESStateBoxInverse[j] == i) {
      return (u8)j;
    }
  }
  abort();
}

// Like above but faster because it substitutes all four values in one scan.
EXECUTE_FROM_RAM
static u32 aesStateBox4x(u32 i) {
  u8 bits = 0xF;
  u32 out = 0;
  for(int j = 0; j < 256; j++) {
    for(int b = 0; b < 4; b++) {
      if((bits & (1 << b)) && (kAESStateBoxInverse[j] == (u8)(i >> (b << 3)))) {
        out |= (j << (b << 3));
        bits &= ~(1 << b);
      }
    }
  }
  return out;
}

void aesExpandKey8(u8 const* key, u8 round, u8* outKeyState) {
  u8 state[16];
  memcpy(state, key, 16);

  u8 a = 1;
  for(u8 i = 0; i < round; i++) {
    state[0] ^= (aesStateBox(state[13]) ^ a);
    state[1] ^= aesStateBox(state[14]);
    state[2] ^= aesStateBox(state[15]);
    state[3] ^= aesStateBox(state[12]);
    state[4] ^= state[0];
    state[5] ^= state[1];
    state[6] ^= state[2];
    state[7] ^= state[3];
    state[8] ^= state[4];
    state[9] ^= state[5];
    state[10] ^= state[6];
    state[11] ^= state[7];
    state[12] ^= state[8];
    state[13] ^= state[9];
    state[14] ^= state[10];
    state[15] ^= state[11];

    if(a & 0x80) {
      a = (u8)((a << 1) ^ 0x1B);
    } else {
      a <<= 1;
    }
  }
  memcpy(outKeyState, state, 16);
}

// Vectorized aesExpandKey for 32-bit CPUs.
void aesExpandKey(u8 const* key, u8 round, u8* outKeyState) {
  u32 state[4];
  memcpy((void*)&state[0], (void*)key, 16);

  u8 a = 1;
  for(u8 i = 0; i < round; i++) {
    state[0] ^= (aesStateBox4x((state[3] >> 8) | (state[3] << 24)) ^ a);
    state[1] ^= state[0];
    state[2] ^= state[1];
    state[3] ^= state[2];

    if(a & 0x80) {
      a = (u8)((a << 1) ^ 0x1B);
    } else {
      a <<= 1;
    }
  }
  memcpy((void*)outKeyState, (void*)&state[0], 16);
}


EXECUTE_FROM_RAM
static void aesAddRoundKey(u8* state, u8 const* key) {
  // A good compiler should vectorize this.
  for(int i = 0; i < 16; i++) {
    state[i] ^= key[i];
  }
}

EXECUTE_FROM_RAM
static void aesSubBytesShiftRows(u8* state, u8 const* sbox) {
  state[0] = sbox[state[0]];
  state[4] = sbox[state[4]];
  state[8] = sbox[state[8]];
  state[12] = sbox[state[12]];
  
  u8 temp = state[1];
  state[1] = sbox[state[13]];
  state[13] = sbox[state[9]];
  state[9] = sbox[state[5]];
  state[5] = sbox[temp];
  
  temp = state[10];
  state[10] = sbox[state[2]];
  state[2] = sbox[temp];
  temp = state[14];
  state[14] = sbox[state[6]];
  state[6] = sbox[temp];
  
  temp = state[3];
  state[3] = sbox[state[7]];
  state[7] = sbox[state[11]];
  state[11] = sbox[state[15]];
  state[15] = sbox[temp];
}

#define gf8(x) (((x)<<1) ^ ((((x)>>7) & 1) * 0x1b))
#define mul_gf8(x, y) ( \
  ((y & 1) * x) ^ \
  ((y >> 1 & 1) * gf8(x)) ^ \
  ((y >> 2 & 1) * gf8(gf8(x))) ^ \
  ((y >> 3 & 1) * gf8(gf8(gf8(x)))) \
)

EXECUTE_FROM_RAM
static void aesMixColumns(u8* state) {
  for (int i = 0; i < 4; i++) { 
    u8 a = state[4 * i + 0], b = state[4 * i + 1],
      c = state[4 * i + 2], d = state[4 * i + 3];

    state[4 * i + 0] = (u8)(mul_gf8(a, 0xe) ^ mul_gf8(b, 0xb) ^
      mul_gf8(c, 0xd) ^ mul_gf8(d, 0x9));
    state[4 * i + 1] = (u8)(mul_gf8(a, 0x9) ^ mul_gf8(b, 0xe) ^
      mul_gf8(c, 0xb) ^ mul_gf8(d, 0xd));
    state[4 * i + 2] = (u8)(mul_gf8(a, 0xd) ^ mul_gf8(b, 0x9) ^
      mul_gf8(c, 0xe) ^ mul_gf8(d, 0xb));
    state[4 * i + 3] = (u8)(mul_gf8(a, 0xb) ^ mul_gf8(b, 0xd) ^
      mul_gf8(c, 0x9) ^ mul_gf8(d, 0xe));
  }
}

EXECUTE_FROM_RAM
void AESDecipherBlock(u8 const* block, u8 const* key, u8* out) {
  
  u8 keySchedule[16];
  u8 state[16];
  memcpy(state, block, 16);

  aesExpandKey(key, 10, keySchedule);
  aesAddRoundKey(state, keySchedule);

  for(int i = 0; i < 9; i++) {
    aesSubBytesShiftRows(state, kAESStateBoxInverse);
    aesExpandKey(key, 9 - i, keySchedule);
    aesAddRoundKey(state, keySchedule);
    aesMixColumns(state);
  }
  aesSubBytesShiftRows(state, kAESStateBoxInverse);
  aesAddRoundKey(state, key);
  memcpy(out, state, 16);
}
