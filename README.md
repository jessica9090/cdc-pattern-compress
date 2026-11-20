# cdc-pattern-compress

Lossless coders for the **chunk pattern** of a deduplicated stream, and a
benchmark that compares them.

Content-defined chunking cuts a byte stream into chunks, and exact
deduplication stores each distinct chunk once. What remains is the chunk
*pattern* [1]: the dictionary index of every chunk, in stream order, numbered in
first-appearance order. In this repo, the coders compress that pattern.

## Coders

- **MCD**: baseline following Niesen's multi-chunk deduplication [2]. Each
  group starts with a flag bit. A group of new chunks is written as a count.
  (In [2], it also carries the chunks' contents; here only the pattern is coded.)
  A repeat group matches the history that followed the repeated chunk's first
  occurrence and is written as a length and pointer.
- **AMC-T**: like an MCD repeat group, a match is written as only a length
  and a pointer. The pointer is coded with adaptive tiers: the first tier
  holds the targets seen after the current chunk, and a target missing from
  a tier escapes to the next. The second tier ranks chunks by how often they
  were landed on.
- **AMC-G**: the same coder, with the second tier ranked by how often
  each chunk has occurred.

AMC stands for *adaptive match coder*; the suffix names the second tier,
ranked by jump **t**argets (T) or by **g**lobal occurrences (G). Each comes in
four variants: `[inc]` or `[exc]` (whether the second tier leaves out the
targets already in the first), each with or without `+F` (frontier mode: a
run of new chunks is coded as one length, like an MCD new group). That makes
nine coders with MCD, and each has a decoder.

The AMC coders code every decision with an arithmetic coder [3]. Their tiers
are PPM-style escape models [4], [5]. With frontier mode on, AMC-T adds a
third tier: the chunks that have never been a target, ranked by how often
each has occurred.

## Running the benchmark

Requires a C99 compiler on a POSIX system (the timer uses `clock_gettime`),
`make`, and Python 3 with NumPy.

```bash
make
```

```bash
python3 bench/benchmark.py
```

The benchmark generates synthetic sources from Niesen's source model [2],
chunks them with `bin/dedup`, and runs all nine coders on each pattern with
`bin/main`. It covers 24 patterns: alphabet size A ∈ {10000, 2500, 625, 156,
39, 10}, source symbol length L = 512 bytes, seeds 1 and 2, and chunker mask
bits M ∈ {6, 8}. Each time is the median of 5 runs, and every timed decode is
checked against the input.

Results go to `bench/grid.csv` and `bench/tables/table{1,2,3}.md`. Sources and
patterns are cached in `bench/work/`. To rebuild only the tables from an
existing `grid.csv`:

```bash
python3 bench/benchmark.py --tables-only
```

To round-trip every coder on one pattern (encode, decode, compare):

```bash
bin/main bench/work/pat/A625_s1_M8.bin -rt
```

## Layout

```
bench/benchmark.py                benchmark driver -> grid.csv, tables/
bench/tables.qmd                  Quarto page: tables/ with a key of terms -> tables.html
src/datagen/dedup_datagen.py      Niesen-model source generator
src/chunker/dedup.c               bin/dedup: content-defined chunking + exact dedup
src/main.c                        bin/main: runs the coders
src/encoders/                     MCD, AMC-T, AMC-G (amc_common.h: shared by the AMC coders)
src/common/                       bit I/O, arithmetic coder, adaptive tables, pattern reader
```

Each source file documents its own usage, file formats and references.

## References

[1] A. Orlitsky, N. P. Santhanam, and J. Zhang, "Universal compression of
memoryless sources over unknown alphabets," *IEEE Transactions on Information
Theory*, 2004, doi: 10.1109/TIT.2004.830761.

[2] U. Niesen, "An information-theoretic analysis of deduplication," *IEEE
Transactions on Information Theory*, 2019, doi: 10.1109/TIT.2019.2916037.

[3] I. H. Witten, R. M. Neal, and J. G. Cleary, "Arithmetic coding for data
compression," *Communications of the ACM*, 1987, doi: 10.1145/214762.214771.

[4] J. G. Cleary and I. H. Witten, "Data compression using adaptive coding and
partial string matching," *IEEE Transactions on Communications*, 1984, doi:
10.1109/TCOM.1984.1096090.

[5] A. Moffat, "Implementing the PPM data compression scheme," *IEEE
Transactions on Communications*, 1990, doi: 10.1109/26.61469.
