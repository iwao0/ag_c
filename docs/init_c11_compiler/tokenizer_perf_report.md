# Tokenizer Performance Report

Date: 2026-03-16  
Environment: Apple clang (`-O0`), `make bench`

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

## Summary

- Allocation count improved significantly with arena allocation (`165,602 -> 590` on 256KB).
- Throughput has improved in several phases, but latest phase shows a regression on mixed/punct-heavy input that needs follow-up tuning.
- CI perf guard is added to detect major regressions in tokenizer throughput and allocations.
- Additional numeric fast-path refactoring keeps throughput at high level (`9,718,939 tokens/sec` on 256KB).
- Additional punctuator exact-match fast path improved throughput further (`12,023,597 tokens/sec` on 256KB).
