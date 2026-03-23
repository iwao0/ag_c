# Tokenizer Performance Report

Date: 2026-03-16  
Environment: Apple clang (`-O0`), `make bench`

## How To Run

```bash
# 1) 通常テスト + ベンチ
make clean
make -j4 test bench

# 2) 関数サイズ回帰チェック
bash scripts/check_function_size.sh

# 3) Tokenizer性能回帰チェック（case別しきい値）
bash scripts/check_tokenizer_perf.sh /tmp/agc_tb.out

# 4) -O0 / -O2 比較ベンチ
scripts/bench_tokenizer_opt_levels.sh /tmp/agc_tokenizer_bench

# 5) Tokenizer軽量perfゲート（ローカル向け）
make check-tokenizer-perf-light

# 6) 日次ホットパス記録（CSV追記）
make log-tokenizer-hotpath-daily
```

`check_tokenizer_perf.sh` は `case=mixed/ident/numeric/punct` の `tokens/sec` と `alloc_count` を検査します。  
しきい値は環境変数（例: `MIXED_MIN_TPS`）で上書きできます。

`build/bench_tokenizer` は継続計測用に次のホットパス指標も出力します。

- `hotpath=scanner`: `tk_skip_ignored` + `tk_scan_ident_*` の `ops/sec`
- `hotpath=literals`: `tk_skip_escape_in_literal` の `ops/sec`
- `hotpath=punctuator`: `match_punctuator` / `punctuator_kind_for_str` の `ops/sec`
- 軽量ゲートでは `check_tokenizer_perf.sh` に加え、上記 hotpath の最小 `ops/sec` も検査する
- 日次ログは `docs/init_c11_compiler/tokenizer_hotpath_daily.csv` に追記する

## Ongoing Comparison Template

| Date | Build | mixed 256KB (tokens/sec) | ident 256KB | numeric 256KB | punct 256KB | scanner hotpath (ops/sec) | literals hotpath (ops/sec) | punctuator hotpath (ops/sec) | alloc_count mixed | Notes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| YYYY-MM-DD | -O0 / -O2 | - | - | - | - | - | - | - | - | change summary |

### Dispatch Order Change Rule (`tokenize_one`)

- 対象: `tokenize_one` の分岐順（punct/string/char/ident/number）を変更したコミット
- 必須記録:
  - `make log-tokenizer-hotpath-daily` を実行して `tokenizer_hotpath_daily.csv` を更新する
  - 変更前直近行と変更後行の差分を `Notes` に要約する（`mixed_tps`, `scanner_ops`, `punctuator_ops` を最低限含める）
- 判定目安（ローカル運用）:
  - `mixed_tps` が -5% 以下の低下なら順序変更は原則ロールバック候補
  - `scanner_ops` / `punctuator_ops` のいずれかが +5% 以上改善し、`mixed_tps` が維持できる場合は採用候補

## Baseline (Before Optimization)

- 1KB: `1,399,168 tokens/sec`, `alloc_count=674`, `peak_alloc_bytes=40,313`
- 16KB: `2,177,904 tokens/sec`, `alloc_count=10,370`, `peak_alloc_bytes=621,265`
- 256KB: `3,744,003 tokens/sec`, `alloc_count=165,602`, `peak_alloc_bytes=9,922,249`

## After Optimization

- 1KB: `2,549,242 tokens/sec`, `alloc_count=3`, `peak_alloc_bytes=49,224`
- 16KB: `2,368,974 tokens/sec`, `alloc_count=37`, `peak_alloc_bytes=607,096`
- 256KB: `3,721,371 tokens/sec`, `alloc_count=590`, `peak_alloc_bytes=9,680,720`

## After Additional Fast-Path Optimization

- 1KB: `6,289,720 tokens/sec`, `alloc_count=3`, `peak_alloc_bytes=49,224`
- 16KB: `6,621,328 tokens/sec`, `alloc_count=37`, `peak_alloc_bytes=607,096`
- 256KB: `9,728,074 tokens/sec`, `alloc_count=590`, `peak_alloc_bytes=9,680,720`

## Pattern-Specific Benchmark (Mixed + 3 Cases)

- mixed 1KB: `6,063,063 tokens/sec`, `alloc_count=3`
- mixed 16KB: `6,903,462 tokens/sec`, `alloc_count=37`
- mixed 256KB: `10,909,157 tokens/sec`, `alloc_count=590`
- ident-heavy 256KB: `8,680,014 tokens/sec`, `alloc_count=189`
- numeric-heavy 256KB: `8,872,873 tokens/sec`, `alloc_count=472`
- punct-heavy 256KB: `24,901,290 tokens/sec`, `alloc_count=294`

## After Locality Optimization (Adaptive Arena + Token Layout)

- mixed 256KB: `8,314,973 tokens/sec`, `alloc_count=21`
- ident-heavy 256KB: `8,160,672 tokens/sec`, `alloc_count=7`
- numeric-heavy 256KB: `8,226,898 tokens/sec`, `alloc_count=18`
- punct-heavy 256KB: `24,632,483 tokens/sec`, `alloc_count=10`

## After Lazy Decode + Decimal Int Fast Path

- mixed 1KB: `4,948,529 tokens/sec`, `alloc_count=3`
- mixed 16KB: `5,687,877 tokens/sec`, `alloc_count=16`
- mixed 256KB: `8,045,132 tokens/sec`, `alloc_count=21`
- ident-heavy 256KB: `8,177,164 tokens/sec`, `alloc_count=7`
- numeric-heavy 256KB: `8,802,507 tokens/sec`, `alloc_count=18`
- punct-heavy 256KB: `22,046,992 tokens/sec`, `alloc_count=10`

## Goal Check (2026-03-16)

- Fixed baseline target was `12,023,597 tokens/sec` (mixed 256KB).
- This phase target (`+10〜20%`) would be `13.2M〜14.4M tokens/sec`.
- Latest mixed 256KB is `8,045,132 tokens/sec`, so target is **not reached** in this measurement.
- Allocation target is maintained (`alloc_count=21`, previous low-alloc path level).
- Remaining focus: keep the low-allocation design while recovering mixed/punctuator throughput.

## Optimization Follow-Up (Branch Table + Two-Layer Scanner)

- mixed 256KB: `7,159,886 tokens/sec`, `alloc_count=21`
- ident-heavy 256KB: `7,487,433 tokens/sec`, `alloc_count=7`
- numeric-heavy 256KB: `8,641,135 tokens/sec`, `alloc_count=18`
- punct-heavy 256KB: `21,226,638 tokens/sec`, `alloc_count=10`

## Opt-Level Benchmark Matrix (same code, 2026-03-16)

- `-O0` mixed 256KB: `9,101,957 tokens/sec`, ident: `8,173,035`, numeric: `8,606,553`, punct: `20,709,983`
- `-O2` mixed 256KB: `31,198,380 tokens/sec`, ident: `21,503,986`, numeric: `23,359,810`, punct: `43,997,371`

## Round-2 Progress (Token Layout Tuning)

- mixed 256KB: `17,107,541 tokens/sec`, `alloc_count=21`
- ident-heavy 256KB: `10,263,369 tokens/sec`, `alloc_count=6`
- numeric-heavy 256KB: `11,283,489 tokens/sec`, `alloc_count=15`
- punct-heavy 256KB: `31,582,888 tokens/sec`, `alloc_count=10`

## Round-2 Progress (Decimal Prescan Strengthening)

- mixed 256KB: `15,266,986 tokens/sec`, `alloc_count=21`
- ident-heavy 256KB: `10,019,810 tokens/sec`, `alloc_count=6`
- numeric-heavy 256KB: `11,219,107 tokens/sec`, `alloc_count=15`
- punct-heavy 256KB: `29,669,622 tokens/sec`, `alloc_count=10`

## Round-2 Progress (Branch Hint + Inline Cleanup)

- mixed 256KB: `17,516,501 tokens/sec`, `alloc_count=21`
- ident-heavy 256KB: `11,032,947 tokens/sec`, `alloc_count=6`
- numeric-heavy 256KB: `12,223,446 tokens/sec`, `alloc_count=15`
- punct-heavy 256KB: `28,155,356 tokens/sec`, `alloc_count=10`

## Keyword Lookup Comparison (manual vs gperf)

- Added comparison tools:
  - `test/bench_keywords.c`
  - `scripts/compare_keyword_lookup.sh`
  - experimental implementation `src/tokenizer/keywords_gperf.c`
- Result example (`-O2`, mixed keyword/non-keyword workload):
  - manual: `269,128,696 qps`
  - gperf: `194,242,858 qps`
- Decision:
  - **Do not adopt gperf version in production path for now**.
  - Current hand-tuned length/character branch implementation remains faster and easier to debug in this codebase.

## Real-Source Corpus Benchmark

- Added corpus benchmark script:
  - `scripts/bench_tokenizer_real_corpus.sh`
- Corpus source:
  - repository `src/**/*.c`, `src/**/*.h`, `test/**/*.c`, `test/**/*.h` concatenated
- Example result:
  - corpus size: `211,541 bytes`
  - case=corpus: `18,350,728 tokens/sec`, `alloc_count=7`, `peak_alloc_bytes=2,265,256`
- Note:
  - corpus case sits between synthetic `ident`/`numeric`/`mixed` patterns and is useful for practical regression checks.

## PGO Trial (`-fprofile-generate/-fprofile-use`)

- Added PGO benchmark script:
  - `scripts/bench_tokenizer_pgo.sh`
- 256KB result snapshot (`-O2` baseline vs PGO):
  - mixed: `44,939,213` -> `46,347,887`
  - ident-heavy: `28,018,465` -> `35,083,815`
  - numeric-heavy: `32,400,188` -> `36,296,414`
  - punct-heavy: `59,550,415` -> `52,484,056`
- Decision for CI:
  - **Do not enable PGO as default CI build path for now**.
  - Rationale: workload-dependent tradeoff (punct-heavy regression), additional tool/runtime complexity (`llvm-profdata`), and longer pipeline steps.
  - Keep PGO as an optional local/periodic experiment via `scripts/bench_tokenizer_pgo.sh`.

## Continue/Stop Gate (Phase0)

- Added gate script:
  - `scripts/check_tokenizer_continue_gate.sh`
- Baseline (2026-03-16):
  - mixed 256KB: `19,705,021`, alloc `21`
  - ident 256KB: `12,289,547`, alloc `6`
  - numeric 256KB: `13,075,812`, alloc `15`
  - punct 256KB: `33,759,919`, alloc `10`
  - corpus: `16,561,884`, alloc `7`
- Pass condition:
  - all cases `tokens/sec` >= baseline * `0.95`
  - all cases `alloc_count` <= baseline
- Stop heuristic:
  - if two consecutive optimization steps stay within ±2% improvement, stop tokenizer micro-optimization and prioritize feature work.
- Operational note:
  - run gate checks from dedicated bench outputs (not from mixed `make test bench` timings) to reduce variance impact.

## Phase1 Trial Note

- Trialed integer-suffix table micro-optimization in `parse_int_suffix`.
- Gate result:
  - ident case regressed beyond -5% threshold in repeated checks.
- Action:
  - rolled back the trial and kept current implementation.

## Phase1 Step1 (`tk_scan_ident_*` branch tuning)

- Change:
  - call UCN parser only when first byte is `'\\'` in `tk_scan_ident_start/continue`.
- Gate check (dedicated bench run):
  - mixed: `20,635,639` (baseline `19,705,021`)
  - ident: `12,114,770` (baseline `12,289,547`)
  - numeric: `13,874,597` (baseline `13,075,812`)
  - punct: `34,173,587` (baseline `33,759,919`)
  - corpus: `17,370,980` (baseline `16,561,884`)
- Decision:
  - keep this change (passes continue gate).

## Phase1 Step2 (`match_punctuator` 3-char order tuning)

- Change:
  - switch-based 3-char check order in `match_punctuator` (`...`, `<<=`, `>>=`) while keeping longest-match behavior.
- Gate check (dedicated bench run):
  - mixed: `25,232,516` (baseline `19,705,021`)
  - ident: `14,515,994` (baseline `12,289,547`)
  - numeric: `14,915,041` (baseline `13,075,812`)
  - punct: `38,855,263` (baseline `33,759,919`)
  - corpus: `18,423,061` (baseline `16,561,884`)
- Decision:
  - keep this change (passes continue gate).

## Phase2 Decision Check

- Current gate run:
  - mixed: `21,749,540`
  - ident: `13,241,342`
  - numeric: `14,316,206`
  - punct: `37,199,704`
  - corpus: `19,643,290` (current corpus size `211,833 bytes`)
- Continue/stop heuristic result:
  - Recent two accepted steps were not within ±2% improvement band, so "stop now" condition was **not** triggered.
  - Continue with low-risk-only optimization policy.

## Phase3 Decision (Chosen)

- Decision:
  - stop tokenizer micro-optimization on mainline for now.
- Mainline policy:
  - avoid large structural changes (DFA/generator migration) in this branch.
  - allow high-cost ideas only as separate PoC branches.
- Next priority:
  - shift effort to feature work (Parser/Preprocessor enhancements).

## C11 Gap Follow-up (Tokenizer)

- strict mode policy:
  - keep strict C11 **disabled by default** (binary literals remain extension by default).
- implemented:
  - tokenizer now preserves floating suffix kind in `token_num_t.float_suffix_kind` (`0=none, 1=f/F, 2=l/L`).
- parser/codegen policy:
  - float suffix metadata is propagated to AST/float literal table.
  - `long double` suffix (`l/L`) is currently lowered to double codegen path by policy.
- prefixed character constants:
  - `L/u/U` prefixed multi-character constants are now accepted as implementation-defined values.
  - current implementation packs units in 8-bit chunks (same style as ordinary multi-character constants) and preserves prefix/width metadata.

## Summary

- Allocation count improved significantly with arena allocation (`165,602 -> 590` on 256KB).
- Throughput has improved in several phases, but latest phase shows a regression on mixed/punct-heavy input that needs follow-up tuning.
- CI perf guard is added to detect major regressions in tokenizer throughput and allocations.
- Additional numeric fast-path refactoring keeps throughput at high level (`9,718,939 tokens/sec` on 256KB).
- Additional punctuator exact-match fast path improved throughput further (`12,023,597 tokens/sec` on 256KB).
- Added `scripts/bench_tokenizer_opt_levels.sh` to run the same benchmark suite on `-O0` and `-O2`.
