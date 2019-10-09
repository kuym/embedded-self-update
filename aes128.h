// Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

#ifndef EMBEDDED_AES128_H_
#define EMBEDDED_AES128_H_

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;

void KeyExpansion(u8 const* key, u8* outKS);
void AESDecipherBlock(u8 const* block, u8 const* ks, u8* out);

#endif // #ifndef EMBEDDED_AES128_H_
