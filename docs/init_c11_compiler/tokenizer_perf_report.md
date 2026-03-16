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

## Summary

- Allocation count improved significantly with arena allocation (`165,602 -> 590` on 256KB).
- Throughput is maintained or improved depending on input size.
- CI perf guard is added to detect major regressions in tokenizer throughput and allocations.
- Additional numeric fast-path refactoring keeps throughput at high level (`9,718,939 tokens/sec` on 256KB).
