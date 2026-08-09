# Compiler limit fixtures

These inputs are valid C programs that intentionally exceed an ag_c resource
guard. They must be rejected with the registered diagnostic and source
location, but they do not belong in `should_reject` because a host compiler may
accept them with higher implementation limits.

Positive WAT and Wasm object fixture scans exclude this directory. Native and
Wasm object compile-fail coverage is registered in `test/test_e2e.c`, and the
design-invariant test requires every fixture here to have exactly one registry
entry.
