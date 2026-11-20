**M = 8**

| coder | time | A=10000 | A=2500 | A=625 | A=156 | A=39 | A=10 |
|---|---|---|---|---|---|---|---|
| MCD | encode | 0.22 ms | 0.33 ms | 0.38 ms | 0.45 ms | 0.51 ms | 0.42 ms |
| MCD | decode | 0.20 ms | 0.29 ms | 0.35 ms | 0.39 ms | 0.43 ms | 0.34 ms |
| AMC-T[inc] | encode | 6.49 ms | 7.06 ms | 6.92 ms | 6.97 ms | 5.27 ms | 2.76 ms |
| AMC-T[inc] | decode | 6.69 ms | 7.31 ms | 7.17 ms | 7.32 ms | 5.31 ms | 2.59 ms |
| AMC-T[exc] | encode | 8.66 ms | 8.94 ms | 8.48 ms | 8.43 ms | 6.02 ms | 2.89 ms |
| AMC-T[exc] | decode | 9.18 ms | 9.16 ms | 8.74 ms | 8.54 ms | 6.16 ms | 2.74 ms |
| AMC-T[inc]+F | encode | 4.25 ms | 5.58 ms | 5.91 ms | 6.84 ms | 5.53 ms | 2.87 ms |
| AMC-T[inc]+F | decode | 4.43 ms | 5.82 ms | 5.79 ms | 6.99 ms | 5.63 ms | 2.68 ms |
| AMC-T[exc]+F | encode | 4.73 ms | 6.30 ms | 6.73 ms | 8.87 ms | 6.14 ms | 2.99 ms |
| AMC-T[exc]+F | decode | 4.92 ms | 7.02 ms | 7.35 ms | 8.48 ms | 6.21 ms | 2.81 ms |
| AMC-G[inc] | encode | 6.72 ms | 7.72 ms | 7.12 ms | 7.20 ms | 5.47 ms | 3.12 ms |
| AMC-G[inc] | decode | 6.98 ms | 7.78 ms | 7.54 ms | 7.80 ms | 5.72 ms | 2.79 ms |
| AMC-G[exc] | encode | 8.71 ms | 9.33 ms | 8.66 ms | 8.59 ms | 6.20 ms | 3.09 ms |
| AMC-G[exc] | decode | 9.27 ms | 9.53 ms | 9.35 ms | 8.87 ms | 6.74 ms | 2.95 ms |
| AMC-G[inc]+F | encode | 4.17 ms | 5.52 ms | 5.84 ms | 6.78 ms | 5.63 ms | 2.99 ms |
| AMC-G[inc]+F | decode | 4.06 ms | 5.71 ms | 6.26 ms | 7.14 ms | 5.79 ms | 2.84 ms |
| AMC-G[exc]+F | encode | 4.61 ms | 6.25 ms | 6.60 ms | 7.73 ms | 6.32 ms | 3.09 ms |
| AMC-G[exc]+F | decode | 4.67 ms | 6.50 ms | 7.25 ms | 8.46 ms | 6.60 ms | 2.98 ms |

**M = 6**

| coder | time | A=10000 | A=2500 | A=625 | A=156 | A=39 | A=10 |
|---|---|---|---|---|---|---|---|
| MCD | encode | 0.70 ms | 1.06 ms | 1.10 ms | 1.07 ms | 0.86 ms | 0.64 ms |
| MCD | decode | 0.56 ms | 0.82 ms | 0.90 ms | 0.87 ms | 0.66 ms | 0.51 ms |
| AMC-T[inc] | encode | 24.5 ms | 22.3 ms | 18.3 ms | 14.3 ms | 6.36 ms | 2.94 ms |
| AMC-T[inc] | decode | 24.6 ms | 23.7 ms | 18.8 ms | 14.6 ms | 6.20 ms | 2.82 ms |
| AMC-T[exc] | encode | 31.4 ms | 29.3 ms | 23.8 ms | 17.4 ms | 6.82 ms | 3.03 ms |
| AMC-T[exc] | decode | 32.2 ms | 30.9 ms | 24.7 ms | 17.8 ms | 6.68 ms | 2.84 ms |
| AMC-T[inc]+F | encode | 12.4 ms | 17.6 ms | 17.3 ms | 15.8 ms | 7.58 ms | 3.44 ms |
| AMC-T[inc]+F | decode | 12.5 ms | 18.4 ms | 17.8 ms | 16.0 ms | 7.66 ms | 3.23 ms |
| AMC-T[exc]+F | encode | 13.8 ms | 19.2 ms | 20.1 ms | 17.6 ms | 7.93 ms | 3.49 ms |
| AMC-T[exc]+F | decode | 14.6 ms | 20.9 ms | 18.6 ms | 18.1 ms | 8.08 ms | 3.26 ms |
| AMC-G[inc] | encode | 24.8 ms | 24.0 ms | 19.2 ms | 16.5 ms | 8.03 ms | 3.77 ms |
| AMC-G[inc] | decode | 25.6 ms | 24.1 ms | 20.4 ms | 17.3 ms | 8.23 ms | 3.51 ms |
| AMC-G[exc] | encode | 31.9 ms | 28.4 ms | 23.8 ms | 19.8 ms | 8.57 ms | 3.82 ms |
| AMC-G[exc] | decode | 33.2 ms | 29.4 ms | 24.7 ms | 20.0 ms | 8.71 ms | 3.63 ms |
| AMC-G[inc]+F | encode | 11.7 ms | 15.9 ms | 15.3 ms | 15.7 ms | 7.82 ms | 3.77 ms |
| AMC-G[inc]+F | decode | 11.9 ms | 16.3 ms | 15.6 ms | 15.3 ms | 8.07 ms | 3.51 ms |
| AMC-G[exc]+F | encode | 12.9 ms | 18.0 ms | 17.5 ms | 16.9 ms | 8.18 ms | 3.77 ms |
| AMC-G[exc]+F | decode | 13.0 ms | 18.4 ms | 17.8 ms | 16.5 ms | 8.44 ms | 3.54 ms |
