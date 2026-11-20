#!/usr/bin/env python3
"""
Generate a synthetic deduplication source: a byte stream of B blocks, each
a uniformly random choice from A distinct random symbols.

Reference: the source model comes from U. Niesen, "An information-theoretic
analysis of deduplication," IEEE Transactions on Information Theory,
2019, doi: 10.1109/TIT.2019.2916037.

This script makes two choices of its own:
  * Whole bytes. Every symbol length is a whole number of bytes, so the
    source is a byte stream, and a symbol uniform over the strings of l
    bytes is l uniformly random bytes.
  * Uniform lengths. Each symbol's length is uniform over L_min..L_max
    bytes, which the command line gives. L_min = L_max gives a constant
    length.

The source model constraints can be written in terms of L_min and L_max:
  * E(L)/2 <= L <= 2 E(L) for every length  iff  L_max <= 3 L_min;
  * A <= 2^(E(L)/2 - 1)                    iff  A <= 2^(2 (L_min + L_max) - 1).

Usage (bench/benchmark.py runs the first form):
    # 10.24 MB source: 20,000 blocks of 512 bytes from 625 symbols
    python3 dedup_datagen.py -A 625 -B 20000 -L 512 -s 1 -o out.bin

    # Symbol lengths uniform over 256..768 bytes (mean 512)
    python3 dedup_datagen.py -A 625 -B 20000 -L 256 768 -s 1 -o out.bin
"""

import argparse
import sys
from typing import Optional

import numpy as np


def check_params(A: int, L_min: int, L_max: int) -> None:
    """Raise ValueError unless (A, L_min, L_max) satisfy the bounds in the
    module docstring. Lengths are in bytes.
    """
    if L_min < 1:
        raise ValueError(f"the minimum length must be at least 1 byte, got {L_min}.")
    if L_max < L_min:
        raise ValueError(f"the maximum length {L_max} is below the minimum {L_min}.")
    if L_max > 3 * L_min:
        raise ValueError(
            f"lengths {L_min}..{L_max} bytes (mean {(L_min + L_max) / 2:g}) "
            f"leave [E(L)/2, 2 E(L)]; this needs L_max <= 3 * L_min."
        )
    # The bound A <= 2^k, with k = E(L)/2 - 1 = 2 (L_min + L_max) - 1.
    # A <= 2^k iff A - 1 < 2^k, so compare using the bit length of A - 1
    # instead of 2^k.
    k = 2 * (L_min + L_max) - 1
    if A < 2 or (A - 1).bit_length() > k:
        raise ValueError(
            f"A = {A} is outside 2 <= A <= 2^(E(L)/2 - 1) = 2^{k} "
            f"for mean length {(L_min + L_max) / 2:g} bytes."
        )


def generate_dedup_source(
    A: int,
    B: int,
    L_min: int,
    L_max: Optional[int] = None,
    seed: Optional[int] = None,
    out=None,
) -> int:
    """Writes a source to `out` (default: stdout). The same arguments and seed
    always give the same bytes. Returns the number of bytes written.
    """
    if L_max is None:
        L_max = L_min
    check_params(A, L_min, L_max)
    if out is None:
        out = sys.stdout.buffer
    # One generator drives every random step, in a fixed order, so the bytes
    # depend only on the arguments and the seed.
    rng = np.random.default_rng(seed)

    # The A symbol lengths.
    if L_min == L_max:
        lengths = [L_min] * A
    else:
        lengths = rng.integers(L_min, L_max + 1, size=A).tolist()

    # Building the alphabet.
    alphabet = []
    chosen = set()
    for l in lengths:
        while True:
            x = rng.bytes(l)
            if x not in chosen:
                break
        chosen.add(x)
        alphabet.append(x)

    # The B blocks randomly selected and written.
    indices = rng.integers(0, A, size=B)
    total = 0
    for i in indices:
        block = alphabet[int(i)]
        out.write(block)
        total += len(block)
    return total


def main():
    # Command line: A, B, the length range, seed, and output.
    parser = argparse.ArgumentParser(
        description="Generate a synthetic deduplication source."
    )
    parser.add_argument(
        "-A", "--alphabet-size", type=int, default=1000,
        help="Number of source symbols in alphabet (default: 1000)."
    )
    parser.add_argument(
        "-B", "--num-blocks", type=int, default=100000,
        help="Number of source blocks to concatenate (default: 100000)."
    )
    parser.add_argument(
        "-L", "--length", type=int, nargs="+", default=[512],
        metavar=("L_MIN", "L_MAX"),
        help="Symbol length in bytes: one value for a constant length, or "
             "two for lengths uniform over L_MIN..L_MAX."
    )
    parser.add_argument(
        "-s", "--seed", type=int, default=None,
        help="Random seed for reproducibility."
    )
    parser.add_argument(
        "-o", "--output", type=str, default=None,
        help="Output file path (default: stdout)."
    )
    args = parser.parse_args()
    if len(args.length) > 2:
        parser.error("-L takes one or two lengths.")
    L_min, L_max = args.length[0], args.length[-1]
    try:
        check_params(args.alphabet_size, L_min, L_max)
    except ValueError as e:
        parser.error(str(e))

    # Generate to the output file, or to stdout without -o.
    gen = (args.alphabet_size, args.num_blocks, L_min, L_max, args.seed)
    if args.output:
        with open(args.output, "wb") as f:
            n = generate_dedup_source(*gen, out=f)
    else:
        n = generate_dedup_source(*gen)
    sys.stderr.write(f"Wrote {n} bytes ({n / (1 << 20):.1f} MiB)\n")


if __name__ == "__main__":
    main()
