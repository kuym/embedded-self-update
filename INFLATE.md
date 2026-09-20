A DEFLATE (RFC 1951) decompressor: a small, embedded-oriented inflate
implementation (~390 lines). Its distinguishing design choice: instead of a
classic Huffman decode table, it builds a compact hierarchical LUT("tree") that
is small enough to fit easily (12 + 16 three-byte entries) and avoids recursion
into tree structures.

Here's how it works, layer by layer:

1. The state struct (inflate.h)

Everything the decoders need lives in InflateState. I/O is abstracted behind 4
callbacks (bytesRemain, readByte, writeByte, readBack) with an opaque context
pointer — so it works on files, memory buffers, Flash streams, etc. readBack
exists specifically for LZ77 back-references: "give me the byte offset bytes
before the current output position." The state also holds:

- bitWindow / haveBits — the bit-buffer
- litTree[16*3], distTree[12*3] — the two Huffman trees (literal/length and
  distance), stored as 24-bit entries
- valueTable[337] — 288 literal/length codes + up to 32 distance codes (max 320
  in dynamic blocks, plus headroom; 337 = 288 + 32 + 17 code-length slots)

2. Bit reading

readBits(state, b) — top-level API. Maintains bitWindow (up to 24 bits,
MSB-prioritized as it's left-shifted in). When not enough bits are buffered, it
pulls in more bytes via bytesRemain/readByte, always topping up to ≥25 bits so
subsequent 16-bit reads (uncompressed blocks) don't stall. Returns b
least-significant bits and shifts them out.

readReverseBits(state, b) — Huffman codes in DEFLATE are transmitted LSB-first
(bit-reversed from their canonical bit-pattern meaning). This function reads b
LSB-first bits and uses the kBitswap table (a classic 4-bit bit-reversal LUT:
kBitswap[n<<k] reverses the top k bits) in chunks of 4, accumulating the
correctly-oriented value. E.g. for 4 bits it returns the fully reversed 4 bits;
for 7 bits it reverses a 4-bit chunk first, then a 3-bit chunk.

3. Tree construction

This is the clever part. A Huffman code is represented as a pair
(bit-length, symbol) packed into a 16-bit "length table" entry: upper 4 bits =
code length, lower 12 bits = the symbol value. Zero-length entries mean "no
code assigned."

deflateQS — a specialized in-place quicksort that orders these entries by
(length, symbol), with the twist that zero-length entries always sort to the
end (see the v=0/v=1 cases so zero-length entries sink past everything). This
lets the next step just trim the tail.

makeTable(lutOut, lutIn, count) — converts the sorted (length, symbol) list into
the compact hierarchical LUT. DEFLATE requires canonical Huffman codes
(symbols ordered by length, then value, assigned the next available code in
each length group), so once the list is sorted, code assignment is trivial
arithmetic. The output LUT is a run-length table of 24-bit entries, each with:
- top 8 bits — moreBits, the number of bits to read at this step
- low 16 bits — max, the cumulative maximum code value seen so far at this
  level

So an LUT row means: "read moreBits reversed bits; if the cumulative code ≤ max,
we're at a leaf — the symbol is in the value table at base + (code - sc)
(offset computed during decoding); otherwise advance to the next row." This is
a compact way to encode the code-length histogram boundaries without a full 2^N
lookup table — which keeps memory tiny (the point for an embedded target).

The valueTable array holds the mapping from "cumulative code index" → actual
symbol (literal byte 0–255, length code, or distance code).

4. Decoding symbols

decodeSymbol(state, lut, valueTable) — walks the LUT: reads moreBits bits per
row (with readReverseBits), accumulates code and max (cumulative max), and once
code <= max for a row, computes the flat index base + (code - sc) into the
value table and returns the 12-bit symbol. It's a linear probe through at most
a few rows — no bit-at-a-time tree walk.

5. The three block types

DeflateDecodeNextBlock(state) — entry point. Reads the 1-bit BFINAL flag and
2-bit BTYPE, dispatches:
- 0 → stored (uncompressed)
- 1 → fixed Huffman
- 2 → dynamic Huffman

It returns non-zero when the final block has been fully decoded, otherwise 0 —
so the caller loops: while(!DeflateDecodeNextBlock(&state)) ....

decodeUncompressedBlock (case 0) — discards any partial bits to align to a byte
boundary, reads the 16-bit length and its one's-complement (NLEN check —
returns error 2 on mismatch), then copies length raw bytes.

decodeFixedTableBlock (case 1) — hardcodes the RFC 1951 §3.2.6 fixed code
lengths: literals 0–143 → 8 bits, 144–255 → 9 bits, length symbols 256–279 → 7
bits, 280–287 → 8 bits; distance codes 0–31 → 5 bits. Populates valueTable
with (sym | (len << 12)) entries, calls makeTable for both trees, then decodes
data.

decodeDynamicTableBlock (case 2, RFC §3.2.7) — the tricky one:

1. Reads counts: hNumLiterals = 257+5 bits, hNumDistances = 1+5 bits,
hCodeLengthCodes = 4+4 bits (4–19).

2. Builds a temporary 19-symbol code-length code tree: the RFC-specified order
permutation kOrderLUT (16,17,18,0,8,7,...), reading 3-bit lengths for the first
N codes (zeros for the rest), then makeTable into codeLengthDecoder.

3. decodeCompressedTables uses that temp tree to read hNumLiterals +
hNumDistances code-length entries, expanding the repeat opcodes:
 - 16 — repeat previous length 3–6× (2 extra bits)
 - 17 — repeat zero 3–10× (3 extra bits)
 - 18 — repeat zero 11–138× (7 extra bits)

Fills state->valueTable (lengths first, then distances) as (sym | (v << 12)),
zero-fills the rest.

4. Runs makeTable on the first usedLCodes and next usedDCodes entries to produce
litTree and distTree, then decodes data.

6. Actual decompression: decodeDEFLATEData

The main loop until the end-of-block symbol (256):

- Literal (symbol < 256): emit the byte directly.
- Length symbol (≥ 257): decode the back-reference length. This implements the
  RFC 1951 §3.2.5 length table (257–285), but note symbol 285 is the special
  case for length 258 (it would need 5 extra bits; RFC says it never occurs so
  it's given a fixed value). The table is split cleverly: symbols 257–259 map
  to lengths 3–5 directly; for 261+ the kLengthLUTMinus10 array holds (base −
  10) values (since all lengths ≥ 261 base are 11 or more, and base 10 + LUT
  value covers it; note the table actually covers entries for 265+ where
  lengths are 11, 13, 15, 17, 19, 23, ...). The number of extra bits is 1 +
  (code >> 2) per the RFC table.
- Distance symbol: decoded with distTree; small codes (<4) are distances 1–4
  directly; others use b = s >> 1 extra bits with dist = (1 + (2<<b) + (
  (s&1)<<b)) + extra — the RFC §3.2.5 distance formula (offset 2, min distance
  1 for that branch, min 1).
- Then copies: len bytes are emitted by reading them back from dist bytes before
  the write position via readBack (LZ77 copy — note it does not guard against
  overlapping copies where dist < len; the RFC permits such references and
  byte-at-a-time copy handles them correctly).

7. Entry points & error model

- DeflateInit — just zeroes bitWindow/haveBits (the rest is assumed stack-zeroed
  or unused until first block).
- DeflateDecodeNextBlock — returns 0 = more blocks follow, 1 = final block
  complete (BFINAL was set), or an error code (2 = NLEN mismatch, 3 = bad
  BTYPE). On malformed bitstream data it simply abort().

Design trade-offs worth noting:

- Memory is minimal: ~300 bytes of tables total — this is why it exists (it's in
  an embedded self-update toolchain; the EXECUTE_FROM_RAM macro is presumably
  defined elsewhere to mark functions that run from RAM on MCU targets).
- Speed cost: decodeSymbol is a loop over LUT rows + a linear value-table
  lookup; classic decoders use a flat 2^maxBits table. For a self-update
  loader, this is fine.
- Simplifications: no zlib/wrapper header handling (raw DEFLATE only), abort
  () on error rather than recoverable codes, no support for dist > output so
  far validation beyond the callback's responsibility.
