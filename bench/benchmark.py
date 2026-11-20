#!/usr/bin/env python3
"""Data and tables for the pattern-coder benchmark on a synthetic grid.

Nine coders, all run by bin/main (src/main.c over src/encoders/), each with a
decoder:

  * MCD                                 -- src/encoders/mcd.c
  * AMC-T[inc], [exc], [inc]+F, [exc]+F -- src/encoders/amc_t.c
  * AMC-G[inc], [exc], [inc]+F, [exc]+F -- src/encoders/amc_g.c

The grid: alphabet size A in {10000, 2500, 625, 156, 39, 10}, B = 20,000
blocks, source symbol length L = 512 bytes, seeds 1 and 2, chunked by this
repo's `dedup --dump-pattern` with chunker mask bits M = 6 and M = 8 -- 24
patterns. The source model and MCD coder follow [1].
See src/datagen/dedup_datagen.py and src/encoders/mcd.h for implementation details.

Every timed measurement is the median of RUNS separate processes, and every
timed decode is checked against the input. Writes grid.csv next to this script
and tables/table{1..3}.md (table3.md holds one timing table per M). Generated
sources and patterns go in work/ (git-ignored) and are reused if already
present.

    make                                     # from the repository root
    python3 bench/benchmark.py
    python3 bench/benchmark.py --tables-only # rebuild tables from grid.csv

References:
  [1] U. Niesen, "An information-theoretic analysis of deduplication,"
      IEEE Transactions on Information Theory, 2019,
      doi: 10.1109/TIT.2019.2916037.
"""
import csv
import re
import statistics
import struct
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------- setup

# Paths: tools are the built binaries; work/ caches sources and patterns.
HERE = Path(__file__).resolve().parent
REPO = HERE.parent
DATAGEN = REPO / "src" / "datagen" / "dedup_datagen.py"
DEDUP = REPO / "bin" / "dedup"
MAIN = REPO / "bin" / "main"

WORK = HERE / "work"
SRC_DIR = WORK / "src"
PAT_DIR = WORK / "pat"
TABLES = HERE / "tables"

# The grid: every A x seed source, each chunked at every M, and RUNS timed
# processes per measurement.
B_BLOCKS = 20000
L_BYTES = 512
A_VALUES = [10000, 2500, 625, 156, 39, 10]
M_VALUES = [6, 8]
SEEDS = [1, 2]
RUNS = 5

T_CODERS = ["AMC-T[inc]", "AMC-T[exc]", "AMC-T[inc]+F", "AMC-T[exc]+F"]
G_CODERS = ["AMC-G[inc]", "AMC-G[exc]", "AMC-G[inc]+F", "AMC-G[exc]+F"]
CODERS = ["MCD"] + T_CODERS + G_CODERS   # every one has a decoder

# Lines parsed from bin/main's stdout: the -H entropy line, a size row
# ("METHOD  C_seq(B)  bits/chk  time_ms") and a -dt decode line.
ENT_RE = re.compile(r"H0=([\d.]+) bits/chunk")
ROW_RE = re.compile(r"^(\S+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s*$")
DEC_RE = re.compile(r"^DEC (\S+) ([\d.]+) (OK|FAIL)")


# ---------------------------------------------------------------- running

def run(cmd):
    """Run cmd and return its stdout; exit with its output if it fails."""
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"command failed ({r.returncode}): {' '.join(map(str, cmd))}\n"
                 f"{r.stdout}{r.stderr}")
    return r.stdout


def gen_source(A, seed):
    """The source for (A, seed), generated once and cached in work/src/."""
    p = SRC_DIR / f"A{A}_s{seed}.bin"
    if not p.exists():
        run([sys.executable, DATAGEN, "-A", A, "-B", B_BLOCKS, "-L", L_BYTES,
             "-s", seed, "-o", p])
    return p


def dump_pattern(src, A, seed, M):
    """The pattern of src chunked at M, generated once and cached in work/pat/."""
    p = PAT_DIR / f"A{A}_s{seed}_M{M}.bin"
    if not p.exists():
        run([DEDUP, "--dump-pattern", p, "-M", M, src])
    return p


def read_DN(pat):
    """(D, N) from a pattern file's 8-byte header."""
    with open(pat, "rb") as f:
        return struct.unpack("<II", f.read(8))


def measure(pat):
    """{coder: dict(bytes, enc_ms, dec_ms, rt)} plus H0 for one pattern."""
    # Entropy, from one untimed -H run.
    H0 = float(ENT_RE.search(run([MAIN, pat, "-H"])).group(1))
    out = {}
    for coder in CODERS:
        # Encode RUNS times: the size must not vary, the time is the median.
        sizes, enc = set(), []
        for _ in range(RUNS):
            for line in run([MAIN, pat, "-m", coder]).splitlines():
                r = ROW_RE.match(line)
                if r and r.group(1) == coder:
                    sizes.add(int(r.group(2)))
                    enc.append(float(r.group(4)))
        assert len(sizes) == 1 and len(enc) == RUNS, (pat, coder, sizes, enc)
        # Decode RUNS times: median time, and OK only if every decode matched.
        dec, oks = [], set()
        for _ in range(RUNS):
            r = DEC_RE.match(run([MAIN, pat, "-m", coder, "-dt"]))
            dec.append(float(r.group(2)))
            oks.add(r.group(3))
        out[coder] = dict(bytes=sizes.pop(), enc_ms=statistics.median(enc),
                          dec_ms=statistics.median(dec),
                          rt="OK" if oks == {"OK"} else "FAIL")
    return out, H0


# grid.csv columns: the pattern's parameters and statistics, then one coder's
# size (bytes, bits/chunk), median times and round-trip status.
FIELDS = ["cell", "A", "seed", "M", "D", "N", "H0", "coder",
          "bytes", "bpc", "enc_ms", "dec_ms", "rt"]


def rows_for(label, A, seed, M, pat):
    """One grid.csv row per coder for one pattern."""
    D, N = read_DN(pat)
    res, H0 = measure(pat)
    for c in CODERS:
        r = res[c]
        yield dict(cell=label, A=A, seed=seed, M=M, D=D, N=N,
                   H0=H0, coder=c,
                   bytes=r["bytes"], bpc=round(8 * r["bytes"] / N, 4),
                   enc_ms=round(r["enc_ms"], 3),
                   dec_ms=round(r["dec_ms"], 3),
                   rt=r["rt"])


def write_csv(path, rows):
    """Write the rows to path as CSV with the FIELDS header."""
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)


# ---------------------------------------------------------------- tables

def fmt_ms(ms):
    """A time in ms with about three significant figures."""
    ms = float(ms)
    if ms >= 1000:
        return f"{ms / 1000:.2f} s"
    if ms >= 100:
        return f"{ms:,.0f} ms"
    if ms >= 10:
        return f"{ms:.1f} ms"
    return f"{ms:.2f} ms"


def cells(grid):
    """[(label, A, M, rows, one row per seed)] by decreasing A, M8 before M6."""
    # Group grid rows by (A, M); within a cell, keep the first row of each
    # seed for the per-pattern columns (D, N, H0), which every coder's row
    # repeats.
    by = {}
    for r in grid:
        by.setdefault((int(r["A"]), int(r["M"])), []).append(r)
    out = []
    for (A, M), rs in by.items():
        seeds = {}
        for r in rs:
            seeds.setdefault(r["seed"], r)
        out.append(((-A, -M), f"A{A}/M{M}", A, M, rs, list(seeds.values())))
    out.sort()
    return [(lbl, A, M, rs, seeds) for _, lbl, A, M, rs, seeds in out]


def table1(grid):
    """Per cell: mean N, distinct chunks, N/D and the order-0 entropy."""
    L = ["| cell | source A | M | mean N | mean D | N/D | H0 |",
         "|---|---|---|---|---|---|---|"]
    for lbl, A, M, _, seeds in cells(grid):   # means over the seeds
        N = statistics.mean(int(s["N"]) for s in seeds)
        D = statistics.mean(int(s["D"]) for s in seeds)
        nd = statistics.mean(int(s["N"]) / int(s["D"]) for s in seeds)
        H0 = statistics.mean(float(s["H0"]) for s in seeds)
        L.append(f"| {lbl} | {A} | {M} | {N:,.0f} | {D:,.0f} | {nd:.2f} "
                 f"| {H0:.2f} |")
    return "\n".join(L) + "\n"


def table2(grid):
    """Mean bits/chunk per cell; the best coder per cell in bold."""
    # One column per cell, led by an italic N/D row for context.
    cs = cells(grid)
    L = ["| coder | " + " | ".join(c[0] for c in cs) + " |",
         "|---" * (len(cs) + 1) + "|"]
    nd = [statistics.mean(int(s["N"]) / int(s["D"]) for s in c[4]) for c in cs]
    L.append("| *$N/D$* | " + " | ".join(f"*{x:.1f}*" for x in nd) + " |")
    # bpc[(coder, cell)]: mean over the seeds.
    bpc = {}
    for i, c in enumerate(cs):
        for coder in CODERS:
            bpc[(coder, i)] = statistics.mean(
                float(r["bpc"]) for r in c[3] if r["coder"] == coder)
    # One row per coder; bold = equal to the best at two decimals.
    for coder in CODERS:
        vals = []
        for i in range(len(cs)):
            v = bpc[(coder, i)]
            best = min(bpc[(k, i)] for k in CODERS)
            vals.append(f"**{v:.2f}**" if round(v, 2) == round(best, 2) else f"{v:.2f}")
        L.append(f"| {coder} | " + " | ".join(vals) + " |")
    # Per family: its best variant in each cell, and that variant vs MCD.
    for fam, group in (("AMC-T", T_CODERS), ("AMC-G", G_CODERS)):
        best = [min(group, key=lambda k: bpc[(k, i)]) for i in range(len(cs))]
        L.append(f"| *best {fam}* | " + " | ".join(f"*{b}*" for b in best) + " |")
        rel = [bpc[(b, i)] / bpc[("MCD", i)] - 1 for i, b in enumerate(best)]
        L.append(f"| *best {fam} vs MCD* | "
                 + " | ".join(f"*{d * 100:+.0f}%*" for d in rel) + " |")
    return "\n".join(L) + "\n"


def table3(grid):
    """Encode/decode time, one table per M: coders by row, source A by column."""
    # Each value is the median of RUNS separate processes (one pattern's time
    # in grid.csv), averaged over the seeds. Each coder has an encode row
    # followed by a decode row.
    t = {}   # t[(M, A, coder, col)]: that time for each seed
    for r in grid:
        for col in ("enc_ms", "dec_ms"):
            t.setdefault((int(r["M"]), int(r["A"]), r["coder"], col),
                         []).append(float(r[col]))
    out = []
    for M in sorted(M_VALUES, reverse=True):
        L = [f"**M = {M}**", "",
             "| coder | time | " + " | ".join(f"A={A}" for A in A_VALUES) + " |",
             "|---" * (len(A_VALUES) + 2) + "|"]
        for coder in CODERS:
            for col, name in (("enc_ms", "encode"), ("dec_ms", "decode")):
                vals = [fmt_ms(statistics.mean(t[(M, A, coder, col)]))
                        for A in A_VALUES]
                L.append(f"| {coder} | {name} | " + " | ".join(vals) + " |")
        out.append("\n".join(L) + "\n")
    return "\n".join(out)


def read_csv(path):
    """grid.csv as a list of dicts (all values are strings)."""
    with open(path) as f:
        return list(csv.DictReader(f))


def main():
    # Full run: generate, chunk and measure every pattern, then write grid.csv.
    if "--tables-only" not in sys.argv:
        for tool in (DEDUP, MAIN):
            if not tool.exists():
                sys.exit(f"{tool} not found: run `make` in {REPO} first")
        for d in (SRC_DIR, PAT_DIR):
            d.mkdir(parents=True, exist_ok=True)
        grid = []
        for A in A_VALUES:
            for seed in SEEDS:
                src = gen_source(A, seed)
                for M in M_VALUES:
                    pat = dump_pattern(src, A, seed, M)
                    rows = list(rows_for(f"A{A}_s{seed}_M{M}", A, seed, M, pat))
                    grid += rows
                    print(f"A={A} seed={seed} M={M}: D={rows[0]['D']} "
                          f"N={rows[0]['N']}", flush=True)
        write_csv(HERE / "grid.csv", grid)

    # Tables are always rebuilt from grid.csv as written to disk.
    grid = read_csv(HERE / "grid.csv")
    TABLES.mkdir(exist_ok=True)
    for i, t in enumerate([table1(grid), table2(grid), table3(grid)], 1):
        (TABLES / f"table{i}.md").write_text(t)
    print(f"wrote {TABLES}/table1..3.md")


if __name__ == "__main__":
    main()
