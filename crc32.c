// Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

#include "crc32.h"

u32 CRC32(u32 lastCRC, u8 const* message, u32 length) {
  lastCRC = ~lastCRC;
  while(length-- > 0) {
    lastCRC = lastCRC ^ *message++;
    for(int j = 7; j >= 0; j--) {
      lastCRC = (lastCRC >> 1) ^ (0xEDB88320 & (-(lastCRC & 1)));
    }
  }
  return ~lastCRC;
}
