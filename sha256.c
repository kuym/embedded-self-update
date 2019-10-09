// Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

#include "sha256.h"

#include <string.h> // memset(), memcpy()

// SHA-256 (NIST FIPS 180-4) implementation

static u32 readU32BE(u8 const* p) {
  return (((u32)p[0]) << 24) | (((u32)p[1]) << 16) |
    (((u32)p[2]) << 8) | ((u32)p[3]);
}

static void writeU32BE(u8* p, u32 v) {
  p[0] = (u8)(v >> 24);
  p[1] = (u8)(v >> 16);
  p[2] = (u8)(v >> 8);
  p[3] = (u8)v;
}

#define rotr32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define big_sigma0(x) (rotr32(x, 2) ^ rotr32(x,13) ^ rotr32(x, 22))
#define big_sigma1(x) (rotr32(x, 6) ^ rotr32(x,11) ^ rotr32(x, 25))
#define little_sigma0(x) (rotr32(x, 7) ^ rotr32(x,18) ^ (x >> 3))
#define little_sigma1(x) (rotr32(x,17) ^ rotr32(x,19) ^ (x >> 10))

static void sha256Transform(u32 h[8], u8 const* buffer) {
  static const u32 K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
  };

  u32 words[64];
  u32 v[8];

  for (u8 i = 0; i < 16; i++) {
    words[i] = readU32BE(&buffer[i * 4]);
  }
  for (u8 i = 16; i < 64; i++) {
    words[i] = little_sigma1(words[i - 2]) + words[i - 7] +
    little_sigma0(words[i - 15]) + words[i - 16];
  }

  for(u8 i = 0; i < 8; i++) {
    v[i] = h[i];
  }
  
  for(u8 i = 0; i < 64; i++) {
    u32 t1 = v[7] + big_sigma1(v[4]) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K[i] +
      words[i];
    u32 t2 = big_sigma0(v[0]) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));

    v[7] = v[6];
    v[6] = v[5];
    v[5] = v[4];
    v[4] = v[3] + t1;
    v[3] = v[2];
    v[2] = v[1];
    v[1] = v[0];
    v[0] = t1 + t2;
  }

  for(u8 i = 0; i < 8; i++) {
    h[i] += v[i];
  }
}

void SHA256(u8 const* d, u32 length, u8* outDigest) {
  // Initialize the hash state.
  u32 h[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
  };

  u32 originalLength = length;
  while(length > 63) {
    sha256Transform(h, d);
    length -= 64;
    d += 64;
  }

  u8 lastBlock[64];

  // If there's a remainder, copy it.
  memcpy(lastBlock, d, length);

  // Add an 0x80 and pad with zeroes.
  lastBlock[length] = 0x80;
  length++;
  memset(&lastBlock[length], 0, 64 - length);

  // Process an extra block if needed.
  if(length > 56) {
    sha256Transform(h, lastBlock);
    memset(lastBlock, 0, 64);
    length = 0;
  }

  // Append the data length in bits.
  writeU32BE(&lastBlock[56], originalLength >> (32 - 3));
  writeU32BE(&lastBlock[60], originalLength << 3);
  sha256Transform(h, lastBlock);

  for(u8 i = 0; i < 8; i++) {
    writeU32BE(&outDigest[i * sizeof(u32)], h[i]);
  }
}
