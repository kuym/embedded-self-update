// Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

// DEFLATE (IETF RFC 1951) compressor implementation

#include "deflate.h"

#define EXECUTE_FROM_RAM

#include <string.h> // memset()


//  Bitstream output

// Appends the low `b` bits of `value`, least-significant bit first, which is
// the order RFC 1951 uses for everything except Huffman codes.  `b` may be at
// most 25 so that the accumulator cannot overflow.
EXECUTE_FROM_RAM
static void writeBits(DeflateState* state, u32 value, u8 b) {
  state->bitWindow |= (value & ((((u32)1) << b) - 1)) << state->haveBits;
  state->haveBits += b;

  while(state->haveBits >= 8) {
    state->writeByte(state->context, (u8)state->bitWindow);
    state->bitWindow >>= 8;
    state->haveBits -= 8;
  }
}

// Pads the stream with zero bits up to the next byte boundary.
EXECUTE_FROM_RAM
static void alignToByte(DeflateState* state) {
  u8 partial = (u8)(state->haveBits & 7);
  if(partial != 0)
    writeBits(state, 0, (u8)(8 - partial));
}

// Emits any bits still held in the accumulator, zero-padding the final byte.
EXECUTE_FROM_RAM
static void flushBits(DeflateState* state) {
  if(state->haveBits > 0) {
    state->writeByte(state->context, (u8)state->bitWindow);
    state->bitWindow = 0;
    state->haveBits = 0;
  }
}

// Huffman codes travel most-significant bit first, so they are reversed once at
// codebook-construction time and then written like any other field.  This is
// the inverse of `inflate.c`'s `readReverseBits()` and shares its nibble table.
EXECUTE_FROM_RAM
static u16 reverseBits(u16 v, u8 b) {
  static const u8 kBitswap[] =
    {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};

  u16 r = 0;
  while(b >= 4) {
    r = (u16)((r << 4) | kBitswap[v & 0xF]);
    v >>= 4;
    b -= 4;
  }
  while(b-- > 0) {
    r = (u16)((r << 1) | (v & 1));
    v >>= 1;
  }
  return r;
}


//  Length and distance codes

EXECUTE_FROM_RAM
static u8 log2Floor(u32 v) {
  u8 n = 0;
  while(v > 1) {
    v >>= 1;
    n++;
  }
  return n;
}

// Splits a match length (3..258) into its RFC 1951 section 3.2.5 length code
// and the extra bits that follow it.  Codes 265..284 come in runs of four that
// share an extra-bit count, which is what lets this be computed rather than
// tabulated: the same trick ``inflate.c`` uses in reverse.
EXECUTE_FROM_RAM
static void encodeLength(u16 length, u16* outSymbol, u8* outBits,
  u16* outExtra) {
  *outBits = 0;
  *outExtra = 0;

  if(length >= 258)
    *outSymbol = 285;
  else if(length < 11)
    *outSymbol = (u16)(254 + length);
  else {
    u16 v = (u16)(length - 3);
    u8 b = (u8)(log2Floor(v) - 2);
    *outSymbol = (u16)(265 + ((b - 1) << 2) + ((v >> b) & 3));
    *outBits = b;
    *outExtra = (u16)(v & ((1 << b) - 1));
  }
}

// The same for a match distance (1..32768), giving a distance code 0..29.
EXECUTE_FROM_RAM
static void encodeDistance(u16 distance, u16* outSymbol, u8* outBits,
  u16* outExtra) {
  *outBits = 0;
  *outExtra = 0;

  if(distance < 5)
    *outSymbol = (u16)(distance - 1);
  else {
    u16 v = (u16)(distance - 1);
    u8 b = (u8)(log2Floor(v) - 1);
    *outSymbol = (u16)(2 + (b << 1) + ((v >> b) & 1));
    *outBits = b;
    *outExtra = (u16)(v & ((1 << b) - 1));
  }
}


//  Huffman codebook construction

EXECUTE_FROM_RAM
static void treeSwap(u16* weight, u16* order, u16 l, u16 r) {
  u16 t = weight[l];
  weight[l] = weight[r];
  weight[r] = t;
  t = order[l];
  order[l] = order[r];
  order[r] = t;
}

EXECUTE_FROM_RAM
static u8 treeLess(u16 const* weight, u16 const* order, u16 l, u16 r) {
  if(weight[l] != weight[r])
    return (weight[l] < weight[r]);
  return (order[l] < order[r]);
}

EXECUTE_FROM_RAM
static void treeSiftDown(u16* weight, u16* order, u16 root, u16 count) {
  while(1) {
    u16 child = (u16)((root << 1) + 1);
    if(child >= count)
      break;
    if((((u16)(child + 1)) < count) && treeLess(weight, order, child,
      (u16)(child + 1)))
      child++;
    if(!treeLess(weight, order, root, child))
      break;
    treeSwap(weight, order, root, child);
    root = child;
  }
}

// Sorts symbols into ascending frequency order, ties broken by symbol value so
// that the codebook is reproducible.  Heapsort is used rather than the
// quicksort `inflate.c` employs because it needs no stack of its own, which
// matters in a bootloader running from RAM.
EXECUTE_FROM_RAM
static void treeSort(u16* weight, u16* order, u16 count) {
  for(u16 i = (u16)(count >> 1); i-- > 0; )
    treeSiftDown(weight, order, i, count);
  for(u16 i = count; i-- > 1; ) {
    treeSwap(weight, order, 0, i);
    treeSiftDown(weight, order, 0, i);
  }
}

// Computes minimum-redundancy (Huffman) code lengths for `count` symbols whose
// frequencies are `freq`, writing a bit length per symbol into `lengthsOut` and
// zero for any symbol that does not occur.  Lengths are capped at `maxBits`.
//
// The tree is built with the Moffat/Katajainen in-place algorithm, which turns
// a frequency array sorted in ascending order into an array of node depths
// using no memory beyond the array itself: the reason the sorted frequencies
// and their symbol numbers are the only scratch this needs.
EXECUTE_FROM_RAM
static void buildCodeLengths(DeflateState* state, u16 const* freq,
  u8* lengthsOut, u16 count, u8 maxBits) {
  u16* weight = state->work.tree.weight;
  u16* order = state->work.tree.order;

  u16 used = 0;
  for(u16 i = 0; i < count; i++) {
    lengthsOut[i] = 0;
    if(freq[i] != 0) {
      weight[used] = freq[i];
      order[used] = i;
      used++;
    }
  }

  // A DEFLATE tree with fewer than two codes cannot be described, so invent a
  // second (never-used) code rather than emit a degenerate one.
  if(used < 2) {
    u16 a = (used == 1)? order[0] : 0;
    lengthsOut[a] = 1;
    lengthsOut[(a == 0)? 1 : 0] = 1;
    return;
  }

  treeSort(weight, order, used);

  {
    u16 root = 0, leaf = 2, next;

    weight[0] = (u16)(weight[0] + weight[1]);

    // First pass: build the tree, leaving each internal node's parent index
    // behind in the slot the node's weight vacated.
    for(next = 1; next < (u16)(used - 1); next++) {
      if((leaf >= used) || (weight[root] < weight[leaf])) {
        weight[next] = weight[root];
        weight[root++] = next;
      } else
        weight[next] = weight[leaf++];

      if((leaf >= used) || ((root < next) && (weight[root] < weight[leaf]))) {
        weight[next] = (u16)(weight[next] + weight[root]);
        weight[root++] = next;
      } else
        weight[next] = (u16)(weight[next] + weight[leaf++]);
    }

    // Second pass: turn the parent indices into internal node depths.
    weight[used - 2] = 0;
    for(u16 i = (u16)(used - 2); i-- > 0; )
      weight[i] = (u16)(weight[weight[i]] + 1);

    // Third pass: hand those depths out to the leaves.
    {
      s16 available = 1, consumed = 0, depth = 0;
      s16 node = (s16)(used - 2), leafIndex = (s16)(used - 1);
      while((available > 0) && (depth < 32)) {
        while((node >= 0) && (weight[node] == (u16)depth)) {
          consumed++;
          node--;
        }
        while(available > consumed) {
          weight[leafIndex--] = (u16)depth;
          available--;
        }
        available = (s16)(2 * consumed);
        depth++;
        consumed = 0;
      }
    }
  }

  // Tally the depths, folding anything longer than `maxBits` back to the limit,
  // then restore the Kraft equality the clamping broke by pushing shorter codes
  // down one level at a time.  Each iteration moves one leaf from level `b` to
  // level `b + 1` (which is free) and retires one over-long code, so the total
  // falls by exactly one and the number of codes is preserved.
  u16 blCount[16] = {0};
  for(u16 i = 0; i < used; i++) {
    u16 depth = weight[i];
    blCount[(depth > maxBits)? maxBits : depth]++;
  }

  u32 total = 0;
  for(u8 b = 1; b <= maxBits; b++)
    total += ((u32)blCount[b]) << (maxBits - b);

  while(total > (((u32)1) << maxBits)) {
    if(blCount[maxBits] == 0)
      break;
    blCount[maxBits]--;
    for(u8 b = (u8)(maxBits - 1); b > 0; b--) {
      if(blCount[b] != 0) {
        blCount[b]--;
        blCount[b + 1] += 2;
        break;
      }
    }
    total--;
  }

  // Give the shortest codes to the most frequent symbols, which sit at the end
  // of the ascending sort.
  u16 i = used;
  for(u8 b = 1; b <= maxBits; b++) {
    for(u16 c = blCount[b]; c > 0; c--)
      lengthsOut[order[--i]] = b;
  }
}

// Turns a table of bit lengths into canonical RFC 1951 codes: ordered by
// length, then by symbol, which is exactly the order `inflate.c`'s
// `makeTable()` recovers by sorting.  The codes are stored bit-reversed, ready
// to be handed straight to writeBits().
EXECUTE_FROM_RAM
static void makeCodes(u8 const* lengths, u16* codesOut, u16 count) {
  u16 blCount[16] = {0};
  for(u16 i = 0; i < count; i++)
    blCount[lengths[i]]++;
  blCount[0] = 0;

  u16 nextCode[16];
  u16 code = 0;
  for(u8 b = 1; b < 16; b++) {
    code = (u16)((code + blCount[b - 1]) << 1);
    nextCode[b] = code;
  }

  for(u16 i = 0; i < count; i++) {
    u8 bits = lengths[i];
    codesOut[i] = bits? reverseBits(nextCode[bits]++, bits) : 0;
  }
}


//  Dynamic block tree description

// The two trees are described to the decoder as one run of
// `usedLCodes + usedDCodes` bit lengths; this reads that logical sequence out
// of the two physically separate halves of `lengthTable`.
EXECUTE_FROM_RAM
static u8 codeLengthAt(DeflateState const* state, u16 i) {
  return (i < state->usedLCodes)? state->lengthTable[i]
    : state->lengthTable[kDeflateLiteralCodes + (i - state->usedLCodes)];
}

// mode 0 counts code-length symbol frequencies, mode 1 measures the encoded
// size in bits, and mode 2 writes it.  Modes 1 and 2 both return the size.
EXECUTE_FROM_RAM
static u32 emitCodeLengthSymbol(DeflateState* state, u8 mode, u16 symbol,
  u16 extra, u8 extraBits) {
  if(mode == 0) {
    state->clValueTable[symbol]++;
    return 0;
  }

  if(mode == 2) {
    writeBits(state, state->clValueTable[symbol],
      state->clLengthTable[symbol]);
    writeBits(state, extra, extraBits);
  }

  return (u32)state->clLengthTable[symbol] + extraBits;
}

// Run-length encodes the bit lengths of both trees with the code-length
// alphabet of RFC 1951 section 3.2.7.
//
// Runs are never allowed to straddle the literal/length-to-distance boundary:
// `inflate.c` resolves a repeat's symbol numbers only at the top of its outer
// loop, so a run that crosses the boundary would leave it decoding distance
// codes offset by `usedLCodes`.  Splitting at the boundary costs at most a few
// bits and keeps every stream this encoder produces safe for that decoder.
EXECUTE_FROM_RAM
static u32 walkCodeLengths(DeflateState* state, u8 mode) {
  u16 count = (u16)(state->usedLCodes + state->usedDCodes), i = 0;
  u32 bits = 0;

  while(i < count) {
    u16 limit = (i < state->usedLCodes)? state->usedLCodes : count;
    u8 value = codeLengthAt(state, i);
    u16 run = 1;

    while((((u16)(i + run)) < limit) &&
      (codeLengthAt(state, (u16)(i + run)) == value))
      run++;

    if(value == 0) {
      // 17 repeats a zero length 3-10 times, 18 repeats it 11-138 times.
      while(run >= 3) {
        u16 r = (run > 138)? 138 : run;
        if(r <= 10)
          bits += emitCodeLengthSymbol(state, mode, 17, (u16)(r - 3), 3);
        else
          bits += emitCodeLengthSymbol(state, mode, 18, (u16)(r - 11), 7);
        run = (u16)(run - r);
        i = (u16)(i + r);
      }
    } else {
      bits += emitCodeLengthSymbol(state, mode, value, 0, 0);
      run--;
      i++;

      // 16 repeats the preceding non-zero length 3-6 times.
      while(run >= 3) {
        u16 r = (run > 6)? 6 : run;
        bits += emitCodeLengthSymbol(state, mode, 16, (u16)(r - 3), 2);
        run = (u16)(run - r);
        i = (u16)(i + r);
      }
    }

    while(run > 0) {
      bits += emitCodeLengthSymbol(state, mode, value, 0, 0);
      run--;
      i++;
    }
  }

  return bits;
}

static const u8 kCodeLengthOrder[kDeflateLengthCodes] =
 {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

EXECUTE_FROM_RAM
static u8 usedCodeLengthCodes(DeflateState const* state) {
  u8 n = kDeflateLengthCodes;
  while((n > 4) && (state->clLengthTable[kCodeLengthOrder[n - 1]] == 0))
    n--;
  return n;
}


//  Match-finding

// Hashes the three bytes at `offset`.  A multiplicative hash is used so that
// all three bytes reach the retained bits whatever kDeflateHashBits is set to.
//
// Note the `u`-suffix on the below constant, which is necessary to compute this
// hash correctly.
EXECUTE_FROM_RAM
static u16 hashAt(DeflateState* state, u32 offset) {
  u32 v = (((u32)state->readByte(state->context, offset)) << 16)
    | (((u32)state->readByte(state->context, offset + 1)) << 8)
    | ((u32)state->readByte(state->context, offset + 2));
  return (u16)((v * 2654435761u) >> (32 - kDeflateHashBits));
}

EXECUTE_FROM_RAM
static void insertHash(DeflateState* state, u32 offset) {
  if((offset + 2) < state->length)
    state->work.hashHead[hashAt(state, offset)] =
      (u16)(offset - state->windowStart);
}

// Records `offset` in the hash table and returns the length of the longest
// match found for it, or zero.  Only one candidate is kept per hash slot: chain
// tables are what make a conventional encoder expensive in RAM, and a single
// head per slot recovers most of the ratio for none of it.
//
// A slot that has never been written reads back as position `windowStart`,
// which is a real position rather than a sentinel.  That needs no special case
//: the candidate is verified byte by byte regardless, and on the rare
// occasion a stale slot happens to point at matching bytes the match is a
// perfectly legal one.
EXECUTE_FROM_RAM
static u16 findMatch(DeflateState* state, u32 offset, u16* outDistance) {
  if((offset + 2) >= state->length)
    return 0;

  u16 h = hashAt(state, offset);
  u32 candidate = state->windowStart + state->work.hashHead[h];
  state->work.hashHead[h] = (u16)(offset - state->windowStart);

  // Matches are not allowed to reach past the end of the block, which keeps the
  // block's extent identical on the measuring and the emitting pass.
  if((offset + 2) >= state->blockEnd)
    return 0;
  if((candidate >= offset) || ((offset - candidate) > kDeflateWindowLength))
    return 0;

  u32 limit = state->blockEnd - offset;
  if(limit > 258)
    limit = 258;

  u16 length = 0;
  while((length < limit) &&
    (state->readByte(state->context, candidate + length) ==
     state->readByte(state->context, offset + length)))
    length++;

  if(length < 3)
    return 0;

  *outDistance = (u16)(offset - candidate);
  return length;
}


//  Block survey and emission

EXECUTE_FROM_RAM
static void outputLiteral(DeflateState* state, u8 emit, u32 offset) {
  u8 value = state->readByte(state->context, offset);
  if(emit)
    writeBits(state, state->valueTable[value], state->lengthTable[value]);
  else
    state->valueTable[value]++;
}

EXECUTE_FROM_RAM
static void outputMatch(DeflateState* state, u8 emit, u16 length,
  u16 distance) {
  u16 lengthSymbol, lengthExtra;
  u8 lengthBits;
  encodeLength(length, &lengthSymbol, &lengthBits, &lengthExtra);

  u16 distanceSymbol, distanceExtra;
  u8 distanceBits;
  encodeDistance(distance, &distanceSymbol, &distanceBits, &distanceExtra);

  if(emit) {
    writeBits(state, state->valueTable[lengthSymbol],
      state->lengthTable[lengthSymbol]);
    writeBits(state, lengthExtra, lengthBits);
    writeBits(state, state->valueTable[kDeflateLiteralCodes + distanceSymbol],
      state->lengthTable[kDeflateLiteralCodes + distanceSymbol]);
    writeBits(state, distanceExtra, distanceBits);
  } else {
    state->valueTable[lengthSymbol]++;
    state->valueTable[kDeflateLiteralCodes + distanceSymbol]++;
    state->extraBits += (u32)lengthBits + distanceBits;
  }
}

// Walks the current block as LZ77 literals and matches, either counting the
// symbols it would produce (`emit` == 0) or writing them (`emit` == 1).
//
// Both walks start from a cleared hash table and re-insert the window that
// precedes the block, so the two produce byte-identical decisions: which is
// what makes the two-pass, zero-token-buffer structure work at all.  It also
// means matches may reach back into earlier blocks without the hash table ever
// being carried across a pass.
EXECUTE_FROM_RAM
static void compressBlockData(DeflateState* state, u8 emit) {
  memset(state->work.hashHead, 0, sizeof(state->work.hashHead));
  for(u32 offset = state->windowStart; offset < state->blockStart; offset++)
    insertHash(state, offset);

  u16 pendingLength = 0, pendingDistance = 0;
  u32 offset = state->blockStart;
  while(offset < state->blockEnd) {
    u16 distance = 0, length = findMatch(state, offset, &distance);

    if(pendingLength >= 3) {
      // Lazy matching: a match was found one byte back, but if this position
      // starts a longer one the earlier byte is better spent as a literal.
      if(length > pendingLength) {
        outputLiteral(state, emit, offset - 1);
        pendingLength = length;
        pendingDistance = distance;
        offset++;
      } else {
        u32 end = (offset - 1) + pendingLength;
        outputMatch(state, emit, pendingLength, pendingDistance);
        // findMatch() already hashed `offset`; the rest of the match still
        // needs to be, or later positions would not see it as a candidate.
        for(offset++; offset < end; offset++)
          insertHash(state, offset);
        pendingLength = 0;
      }
    } else if(length >= 3) {
      pendingLength = length;
      pendingDistance = distance;
      offset++;
    } else {
      outputLiteral(state, emit, offset);
      offset++;
    }
  }

  // A deferred match cannot actually survive the loop: a match of three or
  // more bytes is only ever deferred from a position with at least three bytes
  // of block left, so the loop always gets another turn: but flushing it here
  // keeps the loop correct without relying on that argument.
  if(pendingLength >= 3)
    outputMatch(state, emit, pendingLength, pendingDistance);
}

EXECUTE_FROM_RAM
static void setFixedCodeLengths(DeflateState* state) {
  // RFC 1951 section 3.2.6.
  u16 i = 0;
  for(; i < 144; i++)
    state->lengthTable[i] = 8;
  for(; i < 256; i++)
    state->lengthTable[i] = 9;
  for(; i < 280; i++)
    state->lengthTable[i] = 7;
  for(; i < kDeflateLiteralCodes; i++)
    state->lengthTable[i] = 8;
  for(i = 0; i < kDeflateDistanceCodes; i++)
    state->lengthTable[kDeflateLiteralCodes + i] = 5;
}

// Size in bits of this block's symbols under the fixed codebook.
EXECUTE_FROM_RAM
static u32 fixedBlockCost(DeflateState const* state) {
  u32 cost = 0;
  u16 i = 0;

  for(; i < 144; i++)
    cost += 8 * state->valueTable[i];
  for(; i < 256; i++)
    cost += 9 * state->valueTable[i];
  for(; i < 280; i++)
    cost += 7 * state->valueTable[i];
  for(; i < kDeflateLiteralCodes; i++)
    cost += 8 * state->valueTable[i];
  for(i = 0; i < kDeflateDistanceCodes; i++)
    cost += 5 * state->valueTable[kDeflateLiteralCodes + i];

  return cost;
}

// The same under the codebook just built in `lengthTable`.
EXECUTE_FROM_RAM
static u32 dynamicBlockCost(DeflateState const* state) {
  u32 cost = 0;
  for(u16 i = 0; i < (kDeflateLiteralCodes + kDeflateDistanceCodes); i++)
    cost += ((u32)state->valueTable[i]) * state->lengthTable[i];

  return cost;
}

EXECUTE_FROM_RAM
static void writeStoredBlock(DeflateState* state) {
  u16 length = (u16)(state->blockEnd - state->blockStart);

  writeBits(state, 0, 2);
  alignToByte(state);
  writeBits(state, length, 16);
  writeBits(state, (u16)~length, 16);

  // The stream is byte-aligned here, so the payload can bypass the bit packer.
  for(u32 offset = state->blockStart; offset < state->blockEnd; offset++)
    state->writeByte(state->context,
      state->readByte(state->context, offset));
}


//  Public interface

EXECUTE_FROM_RAM
u8 DeflateEncodeInit(DeflateState* state, u32 length) {
  state->length = length;
  state->position = 0;
  state->bitWindow = 0;
  state->haveBits = 0;
  state->blockStart = 0;
  state->blockEnd = 0;
  state->windowStart = 0;
  state->extraBits = 0;
  state->usedLCodes = 0;
  state->usedDCodes = 0;
  return 0;
}

// Compresses the next block of the input, returning 0 if more input remains,
// 1 once the final block has been written and the stream flushed, and any
// other value on error: the same protocol as DeflateDecodeNextBlock(), so a
// client loop looks the same in both directions:
//
//   while((result = DeflateEncodeNextBlock(state)) == 0);
//
EXECUTE_FROM_RAM
u8 DeflateEncodeNextBlock(DeflateState* state) {
  state->blockStart = state->position;
  state->blockEnd = state->blockStart + kDeflateBlockLength;
  if(state->blockEnd > state->length)
    state->blockEnd = state->length;
  state->windowStart = (state->blockStart > kDeflateWindowLength)?
    (state->blockStart - kDeflateWindowLength) : 0;

  u8 isLastBlock = (state->blockEnd >= state->length);

  // Pass one: survey the block, gathering the symbol frequencies that the
  // codebooks and the block-type decision are derived from.
  memset(state->valueTable, 0, sizeof(state->valueTable));
  state->extraBits = 0;
  compressBlockData(state, 0);
  state->valueTable[256]++; // end-of-block

  // Build the dynamic codebooks.  This uses the scratch space the match
  // finder's hash table shares, which is why it happens between the passes.
  buildCodeLengths(state, state->valueTable, state->lengthTable,
    kDeflateLiteralCodes, kDeflateMaxLiteralBits);
  buildCodeLengths(state, &state->valueTable[kDeflateLiteralCodes],
    &state->lengthTable[kDeflateLiteralCodes], kDeflateDistanceCodes,
    kDeflateMaxDistanceBits);

  // Trim the trailing unused codes off each alphabet; these become HLIT/HDIST.
  state->usedLCodes = 257;
  for(u16 i = kDeflateLiteralCodes; i-- > 257; ) {
    if(state->lengthTable[i] != 0) {
      state->usedLCodes = (u16)(i + 1);
      break;
    }
  }
  state->usedDCodes = 1;
  for(u16 i = kDeflateDistanceCodes; i-- > 1; ) {
    if(state->lengthTable[kDeflateLiteralCodes + i] != 0) {
      state->usedDCodes = (u16)(i + 1);
      break;
    }
  }

  // Describe those codebooks with the code-length alphabet, and price it.
  memset(state->clValueTable, 0, sizeof(state->clValueTable));
  walkCodeLengths(state, 0);
  buildCodeLengths(state, state->clValueTable, state->clLengthTable,
    kDeflateLengthCodes, kDeflateMaxLengthBits);
  u8 hclen = usedCodeLengthCodes(state);
  u32 headerCost = 5 + 5 + 4 + (3 * hclen) + walkCodeLengths(state, 1);

  // Pick whichever of the three block types is smallest.  Stored is what keeps
  // already-compressed payloads from growing by more than a few bytes.
  u32 dynamicCost = 3 + headerCost + dynamicBlockCost(state) + state->extraBits;
  u32 fixedCost = 3 + fixedBlockCost(state) + state->extraBits;
  u32 storedCost = 3 + ((8 - ((((u32)state->haveBits) + 3) & 7)) & 7) + 32
    + (8 * (state->blockEnd - state->blockStart));

  writeBits(state, isLastBlock, 1);

  if((storedCost <= fixedCost) && (storedCost <= dynamicCost))
    writeStoredBlock(state);
  else {
    if(fixedCost <= dynamicCost) {
      writeBits(state, 1, 2);
      setFixedCodeLengths(state);
    } else {
      writeBits(state, 2, 2);

      writeBits(state, (u16)(state->usedLCodes - 257), 5);
      writeBits(state, (u16)(state->usedDCodes - 1), 5);
      writeBits(state, (u8)(hclen - 4), 4);
      for(u16 i = 0; i < hclen; i++)
        writeBits(state, state->clLengthTable[kCodeLengthOrder[i]], 3);

      makeCodes(state->clLengthTable, state->clValueTable,
        kDeflateLengthCodes);
      walkCodeLengths(state, 2);
    }

    // From here the frequencies in `valueTable` have served their purpose and
    // the table is reused to hold the codes themselves.
    makeCodes(state->lengthTable, state->valueTable, kDeflateLiteralCodes);
    makeCodes(&state->lengthTable[kDeflateLiteralCodes],
      &state->valueTable[kDeflateLiteralCodes], kDeflateDistanceCodes);

    // Pass two: replay the block, this time writing it out.
    compressBlockData(state, 1);
    writeBits(state, state->valueTable[256], state->lengthTable[256]);
  }

  state->position = state->blockEnd;

  if(!isLastBlock)
    return 0;

  flushBits(state);
  return 1;
}
