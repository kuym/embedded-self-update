// Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

#ifndef EMBEDDED_SHA256_H_
#define EMBEDDED_SHA256_H_

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;

// SHA-256
void SHA256(u8 const* d, u32 length, u8* outDigest);

#endif // #ifndef EMBEDDED_SHA256_H_
