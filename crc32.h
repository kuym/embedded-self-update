// Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

#ifndef EMBEDDED_CRC32_H_
#define EMBEDDED_CRC32_H_

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;

// CRC-32
u32 CRC32(u32 lastCRC, u8 const* message, u32 length);

#endif // #ifndef EMBEDDED_CRC32_H_
