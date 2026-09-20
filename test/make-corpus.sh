#!/bin/sh
# Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)
#
# Builds the sample set that `make deflate-tests` compresses and round-trips.
# Samples are chosen to exercise each of the three DEFLATE block types and the
# edges of deflate.c's fixed-size tables: incompressible data (stored blocks),
# highly repetitive data (long matches), skewed alphabets (long Huffman codes)
# and lengths that straddle a kDeflateBlockLength boundary.
#
# Usage: make-corpus.sh <output-directory>

set -e

dir="$1"
if [ -z "$dir" ]; then
  echo "Usage: $0 <output-directory>" >&2
  exit 1
fi
mkdir -p "$dir"

src=$(dirname "$0")/..

# Incompressible: forces stored blocks, and checks that they never grow the
# payload by more than the block headers.
for size in 1 2 3 1024 65536 1048576; do
  dd if=/dev/urandom of="$dir/urandom-$size.bin" bs=1 count=0 seek=0 2>/dev/null
  head -c "$size" /dev/urandom > "$dir/urandom-$size.bin"
done

# Degenerate and boundary lengths.  32768 is exactly one block.
: > "$dir/empty.bin"
head -c 32767 /dev/zero > "$dir/zeros-32767.bin"
head -c 32768 /dev/zero > "$dir/zeros-32768.bin"
head -c 32769 /dev/zero > "$dir/zeros-32769.bin"
head -c 102400 /dev/zero > "$dir/zeros-100k.bin"

# Synthetic patterns.  awk keeps these byte-exact and reproducible.
awk 'BEGIN {
  for(i = 0; i < 1024; i++)
    for(j = 0; j < 256; j++)
      printf "%c", j
}' > "$dir/cycle-256k.bin"

awk 'BEGIN {
  for(i = 0; i < 5000; i++)
    printf "The quick brown fox jumps over the lazy dog. "
}' > "$dir/text-repeat.bin"

# Two symbols only: the smallest non-degenerate Huffman tree.
awk 'BEGIN {
  srand(1);
  for(i = 0; i < 200000; i++)
    printf "%c", (rand() < 0.5)? 97 : 98
}' > "$dir/two-symbol.bin"

# A heavily skewed alphabet, which drives Huffman code lengths to the limits
# deflate.c clamps at.
awk 'BEGIN {
  srand(7);
  for(i = 0; i < 200000; i++) {
    r = rand();
    v = int(-log(r + 0.000001) * 24);
    if(v > 255) v = 255;
    printf "%c", v
  }
}' > "$dir/skewed.bin"

# Real-world text: this toolkit'"'"'s own sources, and the existing sample.
cat "$src"/*.c "$src"/*.h "$src"/Makefile "$src"/README.md > "$dir/sources.txt"
cat "$dir/sources.txt" "$dir/sources.txt" "$dir/sources.txt" > "$dir/sources-x3.txt"
if [ -f "$src/test/inflate1.out" ]; then
  cp "$src/test/inflate1.out" "$dir/inflate1.out"
fi

count=$(ls -1 "$dir" | wc -l | tr -d ' ')
echo "Corpus ready: $count samples in $dir"
