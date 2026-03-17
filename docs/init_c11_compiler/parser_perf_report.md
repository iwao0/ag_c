# Parser Performance Report

## 2026-03-17 Baseline (`-O0`, Apple clang)

- Command: `build/bench_parser`
- Note: `tokenize` と `parser` を分離計測。スループット指標は `parser` 時間のみで算出。

| Case | Input Bytes | Funcs | Tokenize (s) | Parser (s) | Parser MB/s | Funcs/sec |
|---|---:|---:|---:|---:|---:|---:|
| mixed (16KB) | 16,416 | 288 | 0.001625 | 0.001193 | 13.12 | 241,408 |
| mixed (256KB) | 262,200 | 4,600 | 0.013408 | 0.012059 | 20.74 | 381,458 |
| expr-heavy (256KB) | 262,176 | 5,462 | 0.008209 | 0.011295 | 22.14 | 483,577 |
| control-heavy (256KB) | 262,185 | 3,405 | 0.007588 | 0.008913 | 28.05 | 382,026 |

## Interpretation

- 現時点では `parser` 単体で 13.12〜28.05 MB/s の範囲。
- `expr-heavy` / `control-heavy` で `funcs/sec` は 38万〜48万程度。
- 今後の最適化評価は、本レポートの `parser` 指標を基準値として比較する。

