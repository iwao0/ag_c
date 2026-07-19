# Generated Runtime Symbol Catalog

This file is generated from `tools/wasm_obj_linker/runtime/symbol-manifest.json`.
Do not edit it directly.

| C symbol | Runtime implementation | Import | Signature | Memory | Availability | Bridge |
|---|---|---|---|---|---|---|
| `_Exit` | `__agc_runtime__Exit` | `env._Exit` | `i32->void` | read/write | wasm32-object-runtime | runtime |
| `__ag_complex_sqrt` | - | `env.__ag_complex_sqrt` | `caller` | read/write | wasm32-object-linker | synthetic |
| `__agc_host_write` | `__agc_host_write` | `env.__agc_host_write (stdio)` | `i32,i32,i32->i32` | read/write | wasm32-js | host |
| `__agc_runtime_math_acos` | `__agc_runtime_math_acos` | `env.__agc_runtime_math_acos (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_acosh` | `__agc_runtime_math_acosh` | `env.__agc_runtime_math_acosh (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_asin` | `__agc_runtime_math_asin` | `env.__agc_runtime_math_asin (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_asinh` | `__agc_runtime_math_asinh` | `env.__agc_runtime_math_asinh (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_atan` | `__agc_runtime_math_atan` | `env.__agc_runtime_math_atan (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_atan2` | `__agc_runtime_math_atan2` | `env.__agc_runtime_math_atan2 (math)` | `f64,f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_atanh` | `__agc_runtime_math_atanh` | `env.__agc_runtime_math_atanh (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_cbrt` | `__agc_runtime_math_cbrt` | `env.__agc_runtime_math_cbrt (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_ceil` | `__agc_runtime_math_ceil` | `env.__agc_runtime_math_ceil (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_cos` | `__agc_runtime_math_cos` | `env.__agc_runtime_math_cos (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_cosh` | `__agc_runtime_math_cosh` | `env.__agc_runtime_math_cosh (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_erf` | `__agc_runtime_math_erf` | `env.__agc_runtime_math_erf (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_erfc` | `__agc_runtime_math_erfc` | `env.__agc_runtime_math_erfc (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_exp` | `__agc_runtime_math_exp` | `env.__agc_runtime_math_exp (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_exp2` | `__agc_runtime_math_exp2` | `env.__agc_runtime_math_exp2 (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_expm1` | `__agc_runtime_math_expm1` | `env.__agc_runtime_math_expm1 (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_fabs` | `__agc_runtime_math_fabs` | `env.__agc_runtime_math_fabs (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_fdim` | `__agc_runtime_math_fdim` | `env.__agc_runtime_math_fdim (math)` | `f64,f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_floor` | `__agc_runtime_math_floor` | `env.__agc_runtime_math_floor (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_fma` | `__agc_runtime_math_fma` | `env.__agc_runtime_math_fma (math)` | `f64,f64,f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_fmax` | `__agc_runtime_math_fmax` | `env.__agc_runtime_math_fmax (math)` | `f64,f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_fmin` | `__agc_runtime_math_fmin` | `env.__agc_runtime_math_fmin (math)` | `f64,f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_fmod` | `__agc_runtime_math_fmod` | `env.__agc_runtime_math_fmod (math)` | `f64,f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_hypot` | `__agc_runtime_math_hypot` | `env.__agc_runtime_math_hypot (math)` | `f64,f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_ilogb` | `__agc_runtime_math_ilogb` | `env.__agc_runtime_math_ilogb (math)` | `f64->i32` | none | wasm32-js | host |
| `__agc_runtime_math_llrint` | `__agc_runtime_math_llrint` | `env.__agc_runtime_math_llrint (math)` | `f64->i64` | none | wasm32-js | host |
| `__agc_runtime_math_llround` | `__agc_runtime_math_llround` | `env.__agc_runtime_math_llround (math)` | `f64->i64` | none | wasm32-js | host |
| `__agc_runtime_math_log` | `__agc_runtime_math_log` | `env.__agc_runtime_math_log (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_log10` | `__agc_runtime_math_log10` | `env.__agc_runtime_math_log10 (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_log1p` | `__agc_runtime_math_log1p` | `env.__agc_runtime_math_log1p (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_log2` | `__agc_runtime_math_log2` | `env.__agc_runtime_math_log2 (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_logb` | `__agc_runtime_math_logb` | `env.__agc_runtime_math_logb (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_lrint` | `__agc_runtime_math_lrint` | `env.__agc_runtime_math_lrint (math)` | `f64->i64` | none | wasm32-js | host |
| `__agc_runtime_math_lround` | `__agc_runtime_math_lround` | `env.__agc_runtime_math_lround (math)` | `f64->i64` | none | wasm32-js | host |
| `__agc_runtime_math_nearbyint` | `__agc_runtime_math_nearbyint` | `env.__agc_runtime_math_nearbyint (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_pow` | `__agc_runtime_math_pow` | `env.__agc_runtime_math_pow (math)` | `f64,f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_remainder` | `__agc_runtime_math_remainder` | `env.__agc_runtime_math_remainder (math)` | `f64,f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_remquo` | `__agc_runtime_math_remquo` | `env.__agc_runtime_math_remquo (math)` | `f64,f64,i64->f64` | write | wasm32-js | host |
| `__agc_runtime_math_rint` | `__agc_runtime_math_rint` | `env.__agc_runtime_math_rint (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_round` | `__agc_runtime_math_round` | `env.__agc_runtime_math_round (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_scalbln` | `__agc_runtime_math_scalbln` | `env.__agc_runtime_math_scalbln (math)` | `f64,i64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_scalbn` | `__agc_runtime_math_scalbn` | `env.__agc_runtime_math_scalbn (math)` | `f64,i32->f64` | none | wasm32-js | host |
| `__agc_runtime_math_sin` | `__agc_runtime_math_sin` | `env.__agc_runtime_math_sin (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_sinh` | `__agc_runtime_math_sinh` | `env.__agc_runtime_math_sinh (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_sqrt` | `__agc_runtime_math_sqrt` | `env.__agc_runtime_math_sqrt (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_tan` | `__agc_runtime_math_tan` | `env.__agc_runtime_math_tan (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_tanh` | `__agc_runtime_math_tanh` | `env.__agc_runtime_math_tanh (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_math_trunc` | `__agc_runtime_math_trunc` | `env.__agc_runtime_math_trunc (math)` | `f64->f64` | none | wasm32-js | host |
| `__agc_runtime_stderr_write` | `__agc_runtime_stderr_write` | `env.__agc_runtime_stderr_write (stdio)` | `i32,i32->i32` | read/write | wasm32-js | host |
| `__agc_runtime_stdout_write` | `__agc_runtime_stdout_write` | `env.__agc_runtime_stdout_write (stdio)` | `i32,i32->i32` | read/write | wasm32-js | host |
| `__agc_runtime_trap` | - | `env.__agc_runtime_trap` | `caller` | read/write | wasm32-object-linker | synthetic |
| `__assert_rtn` | `__agc_runtime___assert_rtn` | `env.__assert_rtn` | `i64,i64,i32,i64->void` | read/write | wasm32-object-runtime | runtime |
| `__error` | `__agc_runtime___error` | `env.__error (stdio)` | `->i64` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `abort` | `__agc_runtime_abort` | `env.abort` | `->void` | read/write | wasm32-object-runtime | runtime |
| `abs` | `__agc_runtime_abs` | `env.abs` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `acos` | `__agc_runtime_acos` | `env.acos (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `acosf` | `__agc_runtime_acosf` | `env.acosf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `acosh` | `__agc_runtime_acosh` | `env.acosh (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `acoshf` | `__agc_runtime_acoshf` | `env.acoshf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `acoshl` | `__agc_runtime_acoshl` | `env.acoshl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `acosl` | `__agc_runtime_acosl` | `env.acosl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `aligned_alloc` | `__agc_runtime_aligned_alloc` | `env.aligned_alloc` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `asctime` | `__agc_runtime_asctime` | `env.asctime` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `asin` | `__agc_runtime_asin` | `env.asin (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `asinf` | `__agc_runtime_asinf` | `env.asinf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `asinh` | `__agc_runtime_asinh` | `env.asinh (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `asinhf` | `__agc_runtime_asinhf` | `env.asinhf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `asinhl` | `__agc_runtime_asinhl` | `env.asinhl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `asinl` | `__agc_runtime_asinl` | `env.asinl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `at_quick_exit` | `__agc_runtime_at_quick_exit` | `env.at_quick_exit` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `atan` | `__agc_runtime_atan` | `env.atan (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `atan2` | `__agc_runtime_atan2` | `env.atan2 (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `atan2f` | `__agc_runtime_atan2f` | `env.atan2f (math)` | `f32,f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `atan2l` | `__agc_runtime_atan2l` | `env.atan2l (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `atanf` | `__agc_runtime_atanf` | `env.atanf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `atanh` | `__agc_runtime_atanh` | `env.atanh (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `atanhf` | `__agc_runtime_atanhf` | `env.atanhf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `atanhl` | `__agc_runtime_atanhl` | `env.atanhl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `atanl` | `__agc_runtime_atanl` | `env.atanl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `atexit` | `__agc_runtime_atexit` | `env.atexit` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `atof` | `__agc_runtime_atof` | `env.atof` | `i64->f64` | read/write | wasm32-object-runtime | runtime |
| `atoi` | `__agc_runtime_atoi` | `env.atoi` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `atol` | `__agc_runtime_atol` | `env.atol` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `atoll` | `__agc_runtime_atoll` | `env.atoll` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `bsearch` | `__agc_runtime_bsearch` | `env.bsearch` | `i32,i32,i64,i64,i32->i32` | read/write | wasm32-object-runtime | runtime |
| `btowc` | `__agc_runtime_btowc` | `env.btowc` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `c16rtomb` | `__agc_runtime_c16rtomb` | `env.c16rtomb` | `i64,i32,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `c32rtomb` | `__agc_runtime_c32rtomb` | `env.c32rtomb` | `i64,i32,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `calloc` | `__agc_runtime_calloc` | `env.calloc` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `cbrt` | `__agc_runtime_cbrt` | `env.cbrt (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `cbrtf` | `__agc_runtime_cbrtf` | `env.cbrtf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `cbrtl` | `__agc_runtime_cbrtl` | `env.cbrtl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `ceil` | `__agc_runtime_ceil` | `env.ceil (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `ceilf` | `__agc_runtime_ceilf` | `env.ceilf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `ceill` | `__agc_runtime_ceill` | `env.ceill (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `clearerr` | `__agc_runtime_clearerr` | `env.clearerr (stdio)` | `i64->void` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `clock` | `__agc_runtime_clock` | `env.clock` | `->i64` | read/write | wasm32-object-runtime | runtime |
| `close` | `__agc_runtime_close` | `env.close` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `copysign` | `__agc_runtime_copysign` | `env.copysign (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `copysignf` | `__agc_runtime_copysignf` | `env.copysignf (math)` | `f32,f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `copysignl` | `__agc_runtime_copysignl` | `env.copysignl (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `cos` | `__agc_runtime_cos` | `env.cos (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `cosf` | `__agc_runtime_cosf` | `env.cosf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `cosh` | `__agc_runtime_cosh` | `env.cosh (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `coshf` | `__agc_runtime_coshf` | `env.coshf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `coshl` | `__agc_runtime_coshl` | `env.coshl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `cosl` | `__agc_runtime_cosl` | `env.cosl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `ctime` | `__agc_runtime_ctime` | `env.ctime` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `difftime` | `__agc_runtime_difftime` | `env.difftime` | `i64,i64->f64` | read/write | wasm32-object-runtime | runtime |
| `div` | `__agc_runtime_div` | `env.div` | `i32,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `erf` | `__agc_runtime_erf` | `env.erf (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `erfc` | `__agc_runtime_erfc` | `env.erfc (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `erfcf` | `__agc_runtime_erfcf` | `env.erfcf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `erfcl` | `__agc_runtime_erfcl` | `env.erfcl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `erff` | `__agc_runtime_erff` | `env.erff (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `erfl` | `__agc_runtime_erfl` | `env.erfl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `exit` | `__agc_runtime_exit` | `env.exit` | `i32->void` | read/write | wasm32-object-runtime | runtime |
| `exp` | `__agc_runtime_exp` | `env.exp (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `exp2` | `__agc_runtime_exp2` | `env.exp2 (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `exp2f` | `__agc_runtime_exp2f` | `env.exp2f (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `exp2l` | `__agc_runtime_exp2l` | `env.exp2l (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `expf` | `__agc_runtime_expf` | `env.expf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `expl` | `__agc_runtime_expl` | `env.expl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `expm1` | `__agc_runtime_expm1` | `env.expm1 (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `expm1f` | `__agc_runtime_expm1f` | `env.expm1f (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `expm1l` | `__agc_runtime_expm1l` | `env.expm1l (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fabs` | `__agc_runtime_fabs` | `env.fabs (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fabsf` | `__agc_runtime_fabsf` | `env.fabsf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fabsl` | `__agc_runtime_fabsl` | `env.fabsl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fclose` | `__agc_runtime_fclose` | `env.fclose (stdio)` | `i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fdim` | `__agc_runtime_fdim` | `env.fdim (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fdimf` | `__agc_runtime_fdimf` | `env.fdimf (math)` | `f32,f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fdiml` | `__agc_runtime_fdiml` | `env.fdiml (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fdopen` | `__agc_runtime_fdopen` | `env.fdopen` | `i32,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `feclearexcept` | `__agc_runtime_feclearexcept` | `env.feclearexcept` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `fegetenv` | `__agc_runtime_fegetenv` | `env.fegetenv` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `fegetexceptflag` | `__agc_runtime_fegetexceptflag` | `env.fegetexceptflag` | `i64,i32->i32` | read/write | wasm32-object-runtime | runtime |
| `fegetround` | `__agc_runtime_fegetround` | `env.fegetround` | `->i32` | read/write | wasm32-object-runtime | runtime |
| `feholdexcept` | `__agc_runtime_feholdexcept` | `env.feholdexcept` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `feof` | `__agc_runtime_feof` | `env.feof (stdio)` | `i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `feraiseexcept` | `__agc_runtime_feraiseexcept` | `env.feraiseexcept` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `ferror` | `__agc_runtime_ferror` | `env.ferror (stdio)` | `i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fesetenv` | `__agc_runtime_fesetenv` | `env.fesetenv` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `fesetexceptflag` | `__agc_runtime_fesetexceptflag` | `env.fesetexceptflag` | `i64,i32->i32` | read/write | wasm32-object-runtime | runtime |
| `fesetround` | `__agc_runtime_fesetround` | `env.fesetround` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `fetestexcept` | `__agc_runtime_fetestexcept` | `env.fetestexcept` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `feupdateenv` | `__agc_runtime_feupdateenv` | `env.feupdateenv` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `fflush` | `__agc_runtime_fflush` | `env.fflush (stdio)` | `i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fgetc` | `__agc_runtime_fgetc` | `env.fgetc (stdio)` | `i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fgetpos` | `__agc_runtime_fgetpos` | `env.fgetpos` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `fgets` | `__agc_runtime_fgets` | `env.fgets (stdio)` | `i64,i32,i64->i64` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fgetwc` | `__agc_runtime_fgetwc` | `env.fgetwc (stdio)` | `i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fgetws` | `__agc_runtime_fgetws` | `env.fgetws (stdio)` | `i64,i32,i64->i64` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `floor` | `__agc_runtime_floor` | `env.floor (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `floorf` | `__agc_runtime_floorf` | `env.floorf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `floorl` | `__agc_runtime_floorl` | `env.floorl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fma` | `__agc_runtime_fma` | `env.fma (math)` | `f64,f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fmaf` | `__agc_runtime_fmaf` | `env.fmaf (math)` | `f32,f32,f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fmal` | `__agc_runtime_fmal` | `env.fmal (math)` | `f64,f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fmax` | `__agc_runtime_fmax` | `env.fmax (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fmaxf` | `__agc_runtime_fmaxf` | `env.fmaxf (math)` | `f32,f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fmaxl` | `__agc_runtime_fmaxl` | `env.fmaxl (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fmin` | `__agc_runtime_fmin` | `env.fmin (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fminf` | `__agc_runtime_fminf` | `env.fminf (math)` | `f32,f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fminl` | `__agc_runtime_fminl` | `env.fminl (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fmod` | `__agc_runtime_fmod` | `env.fmod (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fmodf` | `__agc_runtime_fmodf` | `env.fmodf (math)` | `f32,f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fmodl` | `__agc_runtime_fmodl` | `env.fmodl (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fopen` | `__agc_runtime_fopen` | `env.fopen (stdio)` | `i64,i64->i64` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fpclassify` | `__agc_runtime_fpclassify` | `env.fpclassify (math)` | `f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `fprintf` | `__agc_runtime_fprintf` | `env.fprintf (stdio)` | `i64,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fputc` | `__agc_runtime_fputc` | `env.fputc (stdio)` | `i32,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fputs` | `__agc_runtime_fputs` | `env.fputs (stdio)` | `i64,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fputwc` | `__agc_runtime_fputwc` | `env.fputwc (stdio)` | `i32,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fputws` | `__agc_runtime_fputws` | `env.fputws (stdio)` | `i64,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fread` | `__agc_runtime_fread` | `env.fread (stdio)` | `i64,i64,i64,i64->i64` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `free` | `__agc_runtime_free` | `env.free` | `i32->void` | read/write | wasm32-object-runtime | runtime |
| `freopen` | `__agc_runtime_freopen` | `env.freopen` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `frexp` | `__agc_runtime_frexp` | `env.frexp (math)` | `f64,i64->f64` | write | wasm32-js, wasm32-object-runtime | runtime |
| `frexpf` | `__agc_runtime_frexpf` | `env.frexpf (math)` | `f32,i64->f32` | write | wasm32-js, wasm32-object-runtime | runtime |
| `frexpl` | `__agc_runtime_frexpl` | `env.frexpl (math)` | `f64,i64->f64` | write | wasm32-js, wasm32-object-runtime | runtime |
| `fscanf` | `__agc_runtime_fscanf` | `env.fscanf` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `fseek` | `__agc_runtime_fseek` | `env.fseek` | `i64,i64,i32->i32` | read/write | wasm32-object-runtime | runtime |
| `fsetpos` | `__agc_runtime_fsetpos` | `env.fsetpos` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `fstat` | `__agc_runtime_fstat` | `env.fstat` | `i32,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `ftell` | `__agc_runtime_ftell` | `env.ftell` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `fwide` | `__agc_runtime_fwide` | `env.fwide (stdio)` | `i64,i32->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `fwrite` | `__agc_runtime_fwrite` | `env.fwrite (stdio)` | `i64,i64,i64,i64->i64` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `getc` | `__agc_runtime_getc` | `env.getc (stdio)` | `i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `getchar` | `__agc_runtime_getchar` | `env.getchar (stdio)` | `->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `getenv` | `__agc_runtime_getenv` | `env.getenv` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `getline` | `__agc_runtime_getline` | `env.getline` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `getrusage` | `__agc_runtime_getrusage` | `env.getrusage` | `i32,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `getwc` | `__agc_runtime_getwc` | `env.getwc (stdio)` | `i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `getwchar` | `__agc_runtime_getwchar` | `env.getwchar (stdio)` | `->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `gmtime` | `__agc_runtime_gmtime` | `env.gmtime` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `hypot` | `__agc_runtime_hypot` | `env.hypot (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `hypotf` | `__agc_runtime_hypotf` | `env.hypotf (math)` | `f32,f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `hypotl` | `__agc_runtime_hypotl` | `env.hypotl (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `ilogb` | `__agc_runtime_ilogb` | `env.ilogb (math)` | `f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `ilogbf` | `__agc_runtime_ilogbf` | `env.ilogbf (math)` | `f32->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `ilogbl` | `__agc_runtime_ilogbl` | `env.ilogbl (math)` | `f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `imaxabs` | `__agc_runtime_imaxabs` | `env.imaxabs` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `imaxdiv` | `__agc_runtime_imaxdiv` | `env.imaxdiv` | `i32,i64,i64->void` | read/write | wasm32-object-runtime | runtime |
| `isalnum` | `__agc_runtime_isalnum` | `env.isalnum` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `isalpha` | `__agc_runtime_isalpha` | `env.isalpha` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `isblank` | `__agc_runtime_isblank` | `env.isblank` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iscntrl` | `__agc_runtime_iscntrl` | `env.iscntrl` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `isdigit` | `__agc_runtime_isdigit` | `env.isdigit` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `isfinite` | `__agc_runtime_isfinite` | `env.isfinite (math)` | `f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `isgraph` | `__agc_runtime_isgraph` | `env.isgraph` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `isgreater` | `__agc_runtime_isgreater` | `env.isgreater (math)` | `f64,f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `isgreaterequal` | `__agc_runtime_isgreaterequal` | `env.isgreaterequal (math)` | `f64,f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `isinf` | `__agc_runtime_isinf` | `env.isinf (math)` | `f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `isless` | `__agc_runtime_isless` | `env.isless (math)` | `f64,f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `islessequal` | `__agc_runtime_islessequal` | `env.islessequal (math)` | `f64,f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `islessgreater` | `__agc_runtime_islessgreater` | `env.islessgreater (math)` | `f64,f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `islower` | `__agc_runtime_islower` | `env.islower` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `isnan` | `__agc_runtime_isnan` | `env.isnan (math)` | `f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `isnormal` | `__agc_runtime_isnormal` | `env.isnormal (math)` | `f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `isprint` | `__agc_runtime_isprint` | `env.isprint` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `ispunct` | `__agc_runtime_ispunct` | `env.ispunct` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `isspace` | `__agc_runtime_isspace` | `env.isspace` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `isunordered` | `__agc_runtime_isunordered` | `env.isunordered (math)` | `f64,f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `isupper` | `__agc_runtime_isupper` | `env.isupper` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswalnum` | `__agc_runtime_isalnum` | `env.iswalnum` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswalpha` | `__agc_runtime_isalpha` | `env.iswalpha` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswblank` | `__agc_runtime_isblank` | `env.iswblank` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswcntrl` | `__agc_runtime_iscntrl` | `env.iswcntrl` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswctype` | `__agc_runtime_iswctype` | `env.iswctype` | `i32,i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswdigit` | `__agc_runtime_isdigit` | `env.iswdigit` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswgraph` | `__agc_runtime_isgraph` | `env.iswgraph` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswlower` | `__agc_runtime_islower` | `env.iswlower` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswprint` | `__agc_runtime_isprint` | `env.iswprint` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswpunct` | `__agc_runtime_ispunct` | `env.iswpunct` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswspace` | `__agc_runtime_isspace` | `env.iswspace` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswupper` | `__agc_runtime_isupper` | `env.iswupper` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `iswxdigit` | `__agc_runtime_isxdigit` | `env.iswxdigit` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `isxdigit` | `__agc_runtime_isxdigit` | `env.isxdigit` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `labs` | `__agc_runtime_labs` | `env.labs` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `ldexp` | `__agc_runtime_ldexp` | `env.ldexp (math)` | `f64,i32->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `ldexpf` | `__agc_runtime_ldexpf` | `env.ldexpf (math)` | `f32,i32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `ldexpl` | `__agc_runtime_ldexpl` | `env.ldexpl (math)` | `f64,i32->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `ldiv` | `__agc_runtime_ldiv` | `env.ldiv` | `i32,i64,i64->void` | read/write | wasm32-object-runtime | runtime |
| `llabs` | `__agc_runtime_llabs` | `env.llabs` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `lldiv` | `__agc_runtime_lldiv` | `env.lldiv` | `i32,i64,i64->void` | read/write | wasm32-object-runtime | runtime |
| `llrint` | `__agc_runtime_llrint` | `env.llrint (math)` | `f64->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `llrintf` | `__agc_runtime_llrintf` | `env.llrintf (math)` | `f32->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `llrintl` | `__agc_runtime_llrintl` | `env.llrintl (math)` | `f64->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `llround` | `__agc_runtime_llround` | `env.llround (math)` | `f64->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `llroundf` | `__agc_runtime_llroundf` | `env.llroundf (math)` | `f32->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `llroundl` | `__agc_runtime_llroundl` | `env.llroundl (math)` | `f64->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `localeconv` | `__agc_runtime_localeconv` | `env.localeconv` | `->i64` | read/write | wasm32-object-runtime | runtime |
| `localtime` | `__agc_runtime_localtime` | `env.localtime` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `log` | `__agc_runtime_log` | `env.log (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `log10` | `__agc_runtime_log10` | `env.log10 (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `log10f` | `__agc_runtime_log10f` | `env.log10f (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `log10l` | `__agc_runtime_log10l` | `env.log10l (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `log1p` | `__agc_runtime_log1p` | `env.log1p (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `log1pf` | `__agc_runtime_log1pf` | `env.log1pf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `log1pl` | `__agc_runtime_log1pl` | `env.log1pl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `log2` | `__agc_runtime_log2` | `env.log2 (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `log2f` | `__agc_runtime_log2f` | `env.log2f (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `log2l` | `__agc_runtime_log2l` | `env.log2l (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `logb` | `__agc_runtime_logb` | `env.logb (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `logbf` | `__agc_runtime_logbf` | `env.logbf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `logbl` | `__agc_runtime_logbl` | `env.logbl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `logf` | `__agc_runtime_logf` | `env.logf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `logl` | `__agc_runtime_logl` | `env.logl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `lrint` | `__agc_runtime_lrint` | `env.lrint (math)` | `f64->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `lrintf` | `__agc_runtime_lrintf` | `env.lrintf (math)` | `f32->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `lrintl` | `__agc_runtime_lrintl` | `env.lrintl (math)` | `f64->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `lround` | `__agc_runtime_lround` | `env.lround (math)` | `f64->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `lroundf` | `__agc_runtime_lroundf` | `env.lroundf (math)` | `f32->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `lroundl` | `__agc_runtime_lroundl` | `env.lroundl (math)` | `f64->i64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `lseek` | `__agc_runtime_lseek` | `env.lseek (stdio)` | `i32,i64,i32->i64` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `malloc` | `__agc_runtime_malloc` | `env.malloc` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `mblen` | `__agc_runtime_mblen` | `env.mblen` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `mbrlen` | `__agc_runtime_mbrlen` | `env.mbrlen` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `mbrtoc16` | `__agc_runtime_mbrtoc16` | `env.mbrtoc16` | `i64,i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `mbrtoc32` | `__agc_runtime_mbrtoc32` | `env.mbrtoc32` | `i64,i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `mbrtowc` | `__agc_runtime_mbrtowc` | `env.mbrtowc` | `i64,i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `mbsinit` | `__agc_runtime_mbsinit` | `env.mbsinit` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `mbsrtowcs` | `__agc_runtime_mbsrtowcs` | `env.mbsrtowcs` | `i64,i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `mbstowcs` | `__agc_runtime_mbstowcs` | `env.mbstowcs` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `mbtowc` | `__agc_runtime_mbtowc` | `env.mbtowc` | `i64,i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `memchr` | `__agc_runtime_memchr` | `env.memchr` | `i64,i32,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `memcmp` | `__agc_runtime_memcmp` | `env.memcmp` | `i64,i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `memcpy` | `__agc_runtime_memcpy` | `env.memcpy` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `memmove` | `__agc_runtime_memmove` | `env.memmove` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `memset` | `__agc_runtime_memset` | `env.memset` | `i64,i32,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `mktime` | `__agc_runtime_mktime` | `env.mktime` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `modf` | `__agc_runtime_modf` | `env.modf (math)` | `f64,i64->f64` | write | wasm32-js, wasm32-object-runtime | runtime |
| `modff` | `__agc_runtime_modff` | `env.modff (math)` | `f32,i64->f32` | write | wasm32-js, wasm32-object-runtime | runtime |
| `modfl` | `__agc_runtime_modfl` | `env.modfl (math)` | `f64,i64->f64` | write | wasm32-js, wasm32-object-runtime | runtime |
| `nan` | `__agc_runtime_nan` | `env.nan (math)` | `i64->f64` | read | wasm32-js, wasm32-object-runtime | runtime |
| `nanf` | `__agc_runtime_nanf` | `env.nanf (math)` | `i64->f32` | read | wasm32-js, wasm32-object-runtime | runtime |
| `nanl` | `__agc_runtime_nanl` | `env.nanl (math)` | `i64->f64` | read | wasm32-js, wasm32-object-runtime | runtime |
| `nearbyint` | `__agc_runtime_nearbyint` | `env.nearbyint (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `nearbyintf` | `__agc_runtime_nearbyintf` | `env.nearbyintf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `nearbyintl` | `__agc_runtime_nearbyintl` | `env.nearbyintl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `open` | `__agc_runtime_open` | `env.open` | `i64,i32->i32` | read/write | wasm32-object-runtime | runtime |
| `perror` | `__agc_runtime_perror` | `env.perror (stdio)` | `i64->void` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `pow` | `__agc_runtime_pow` | `env.pow (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `powf` | `__agc_runtime_powf` | `env.powf (math)` | `f32,f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `powl` | `__agc_runtime_powl` | `env.powl (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `printf` | `__agc_runtime_printf` | `env.printf (stdio)` | `i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `putc` | `__agc_runtime_putc` | `env.putc` | `i32,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `putchar` | `__agc_runtime_putchar` | `env.putchar (stdio)` | `i32->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `puts` | `__agc_runtime_puts` | `env.puts (stdio)` | `i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `putwc` | `__agc_runtime_putwc` | `env.putwc (stdio)` | `i32,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `putwchar` | `__agc_runtime_putwchar` | `env.putwchar (stdio)` | `i32->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `qsort` | `__agc_runtime_qsort` | `env.qsort` | `i32,i64,i64,i32->void` | read/write | wasm32-object-runtime | runtime |
| `quick_exit` | `__agc_runtime_quick_exit` | `env.quick_exit` | `i32->void` | read/write | wasm32-object-runtime | runtime |
| `raise` | `__agc_runtime_raise` | `env.raise` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `rand` | `__agc_runtime_rand` | `env.rand` | `->i32` | read/write | wasm32-object-runtime | runtime |
| `read` | `__agc_runtime_read` | `env.read` | `i32,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `realloc` | `__agc_runtime_realloc` | `env.realloc` | `i32,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `realpath` | `__agc_runtime_realpath` | `env.realpath` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `remainder` | `__agc_runtime_remainder` | `env.remainder (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `remainderf` | `__agc_runtime_remainderf` | `env.remainderf (math)` | `f32,f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `remainderl` | `__agc_runtime_remainderl` | `env.remainderl (math)` | `f64,f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `remove` | `__agc_runtime_remove` | `env.remove` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `remquo` | `__agc_runtime_remquo` | `env.remquo (math)` | `f64,f64,i64->f64` | write | wasm32-js, wasm32-object-runtime | runtime |
| `remquof` | `__agc_runtime_remquof` | `env.remquof (math)` | `f32,f32,i64->f32` | write | wasm32-js, wasm32-object-runtime | runtime |
| `remquol` | `__agc_runtime_remquol` | `env.remquol (math)` | `f64,f64,i64->f64` | write | wasm32-js, wasm32-object-runtime | runtime |
| `rename` | `__agc_runtime_rename` | `env.rename` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `rewind` | `__agc_runtime_rewind` | `env.rewind` | `i64->void` | read/write | wasm32-object-runtime | runtime |
| `rint` | `__agc_runtime_rint` | `env.rint (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `rintf` | `__agc_runtime_rintf` | `env.rintf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `rintl` | `__agc_runtime_rintl` | `env.rintl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `round` | `__agc_runtime_round` | `env.round (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `roundf` | `__agc_runtime_roundf` | `env.roundf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `roundl` | `__agc_runtime_roundl` | `env.roundl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `scalbln` | `__agc_runtime_scalbln` | `env.scalbln (math)` | `f64,i64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `scalblnf` | `__agc_runtime_scalblnf` | `env.scalblnf (math)` | `f32,i64->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `scalblnl` | `__agc_runtime_scalblnl` | `env.scalblnl (math)` | `f64,i64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `scalbn` | `__agc_runtime_scalbn` | `env.scalbn (math)` | `f64,i32->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `scalbnf` | `__agc_runtime_scalbnf` | `env.scalbnf (math)` | `f32,i32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `scalbnl` | `__agc_runtime_scalbnl` | `env.scalbnl (math)` | `f64,i32->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `scanf` | `__agc_runtime_scanf` | `env.scanf` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `setbuf` | `__agc_runtime_setbuf` | `env.setbuf` | `i64,i64->void` | read/write | wasm32-object-runtime | runtime |
| `setlocale` | `__agc_runtime_setlocale` | `env.setlocale` | `i32,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `setvbuf` | `__agc_runtime_setvbuf` | `env.setvbuf` | `i64,i64,i32,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `signal` | `__agc_runtime_signal` | `env.signal` | `i32,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `signbit` | `__agc_runtime_signbit` | `env.signbit (math)` | `f64->i32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `sin` | `__agc_runtime_sin` | `env.sin (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `sinf` | `__agc_runtime_sinf` | `env.sinf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `sinh` | `__agc_runtime_sinh` | `env.sinh (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `sinhf` | `__agc_runtime_sinhf` | `env.sinhf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `sinhl` | `__agc_runtime_sinhl` | `env.sinhl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `sinl` | `__agc_runtime_sinl` | `env.sinl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `snprintf` | `__agc_runtime_snprintf` | `env.snprintf (stdio)` | `i64,i64,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `sprintf` | `__agc_runtime_sprintf` | `env.sprintf (stdio)` | `i64,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `sqrt` | `__agc_runtime_sqrt` | `env.sqrt (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `sqrtf` | `__agc_runtime_sqrtf` | `env.sqrtf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `sqrtl` | `__agc_runtime_sqrtl` | `env.sqrtl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `srand` | `__agc_runtime_srand` | `env.srand` | `i32->void` | read/write | wasm32-object-runtime | runtime |
| `sscanf` | `__agc_runtime_sscanf` | `env.sscanf` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `strcat` | `__agc_runtime_strcat` | `env.strcat` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `strchr` | `__agc_runtime_strchr` | `env.strchr` | `i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `strcmp` | `__agc_runtime_strcmp` | `env.strcmp` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `strcoll` | `__agc_runtime_strcoll` | `env.strcoll` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `strcpy` | `__agc_runtime_strcpy` | `env.strcpy` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `strcspn` | `__agc_runtime_strcspn` | `env.strcspn` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `strerror` | `__agc_runtime_strerror` | `env.strerror` | `i32->i64` | read/write | wasm32-object-runtime | runtime |
| `strftime` | `__agc_runtime_strftime` | `env.strftime` | `i64,i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `strlen` | `__agc_runtime_strlen` | `env.strlen` | `i64->i64` | read | wasm32-object-runtime | runtime |
| `strncat` | `__agc_runtime_strncat` | `env.strncat` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `strncmp` | `__agc_runtime_strncmp` | `env.strncmp` | `i64,i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `strncpy` | `__agc_runtime_strncpy` | `env.strncpy` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `strpbrk` | `__agc_runtime_strpbrk` | `env.strpbrk` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `strrchr` | `__agc_runtime_strrchr` | `env.strrchr` | `i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `strspn` | `__agc_runtime_strspn` | `env.strspn` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `strstr` | `__agc_runtime_strstr` | `env.strstr` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `strtod` | `__agc_runtime_strtod` | `env.strtod` | `i64,i64->f64` | read/write | wasm32-object-runtime | runtime |
| `strtof` | `__agc_runtime_strtof` | `env.strtof` | `i64,i64->f32` | read/write | wasm32-object-runtime | runtime |
| `strtoimax` | `__agc_runtime_strtoimax` | `env.strtoimax` | `i64,i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `strtok` | `__agc_runtime_strtok` | `env.strtok` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `strtol` | `__agc_runtime_strtol` | `env.strtol` | `i64,i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `strtold` | `__agc_runtime_strtold` | `env.strtold` | `i64,i64->f64` | read/write | wasm32-object-runtime | runtime |
| `strtoll` | `__agc_runtime_strtoll` | `env.strtoll` | `i64,i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `strtoul` | `__agc_runtime_strtoul` | `env.strtoul` | `i64,i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `strtoull` | `__agc_runtime_strtoull` | `env.strtoull` | `i64,i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `strtoumax` | `__agc_runtime_strtoumax` | `env.strtoumax` | `i64,i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `strxfrm` | `__agc_runtime_strxfrm` | `env.strxfrm` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `swprintf` | `__agc_runtime_swprintf` | `env.swprintf` | `i64,i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `swscanf` | `__agc_runtime_swscanf` | `env.swscanf` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `system` | `__agc_runtime_system` | `env.system` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `tan` | `__agc_runtime_tan` | `env.tan (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `tanf` | `__agc_runtime_tanf` | `env.tanf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `tanh` | `__agc_runtime_tanh` | `env.tanh (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `tanhf` | `__agc_runtime_tanhf` | `env.tanhf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `tanhl` | `__agc_runtime_tanhl` | `env.tanhl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `tanl` | `__agc_runtime_tanl` | `env.tanl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `time` | `__agc_runtime_time` | `env.time` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `timespec_get` | `__agc_runtime_timespec_get` | `env.timespec_get` | `i64,i32->i32` | read/write | wasm32-object-runtime | runtime |
| `tmpfile` | `__agc_runtime_tmpfile` | `env.tmpfile` | `->i64` | read/write | wasm32-object-runtime | runtime |
| `tmpnam` | `__agc_runtime_tmpnam` | `env.tmpnam` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `tolower` | `__agc_runtime_tolower` | `env.tolower` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `toupper` | `__agc_runtime_toupper` | `env.toupper` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `towctrans` | `__agc_runtime_towctrans` | `env.towctrans` | `i32,i32->i32` | read/write | wasm32-object-runtime | runtime |
| `towlower` | `__agc_runtime_tolower` | `env.towlower` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `towupper` | `__agc_runtime_toupper` | `env.towupper` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `trunc` | `__agc_runtime_trunc` | `env.trunc (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `truncf` | `__agc_runtime_truncf` | `env.truncf (math)` | `f32->f32` | none | wasm32-js, wasm32-object-runtime | runtime |
| `truncl` | `__agc_runtime_truncl` | `env.truncl (math)` | `f64->f64` | none | wasm32-js, wasm32-object-runtime | runtime |
| `ungetc` | `__agc_runtime_ungetc` | `env.ungetc (stdio)` | `i32,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `ungetwc` | `__agc_runtime_ungetwc` | `env.ungetwc (stdio)` | `i32,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `vfprintf` | `__agc_runtime_vfprintf` | `env.vfprintf (stdio)` | `i64,i64,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `vfscanf` | `__agc_runtime_vfscanf` | `env.vfscanf` | `i64,i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `vprintf` | `__agc_runtime_vprintf` | `env.vprintf` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `vscanf` | `__agc_runtime_vscanf` | `env.vscanf` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `vsnprintf` | `__agc_runtime_vsnprintf` | `env.vsnprintf (stdio)` | `i64,i64,i64,i64->i32` | read/write | wasm32-js, wasm32-object-runtime | runtime |
| `vsprintf` | `__agc_runtime_vsprintf` | `env.vsprintf` | `i64,i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `vsscanf` | `__agc_runtime_vsscanf` | `env.vsscanf` | `i64,i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `wcrtomb` | `__agc_runtime_wcrtomb` | `env.wcrtomb` | `i64,i32,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcscat` | `__agc_runtime_wcscat` | `env.wcscat` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcschr` | `__agc_runtime_wcschr` | `env.wcschr` | `i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `wcscmp` | `__agc_runtime_wcscmp` | `env.wcscmp` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `wcscoll` | `__agc_runtime_wcscoll` | `env.wcscoll` | `i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `wcscpy` | `__agc_runtime_wcscpy` | `env.wcscpy` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcscspn` | `__agc_runtime_wcscspn` | `env.wcscspn` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcsftime` | `__agc_runtime_wcsftime` | `env.wcsftime` | `i64,i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcslen` | `__agc_runtime_wcslen` | `env.wcslen` | `i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcsncat` | `__agc_runtime_wcsncat` | `env.wcsncat` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcsncmp` | `__agc_runtime_wcsncmp` | `env.wcsncmp` | `i64,i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `wcsncpy` | `__agc_runtime_wcsncpy` | `env.wcsncpy` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcspbrk` | `__agc_runtime_wcspbrk` | `env.wcspbrk` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcsrchr` | `__agc_runtime_wcsrchr` | `env.wcsrchr` | `i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `wcsrtombs` | `__agc_runtime_wcsrtombs` | `env.wcsrtombs` | `i64,i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcsspn` | `__agc_runtime_wcsspn` | `env.wcsspn` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcsstr` | `__agc_runtime_wcsstr` | `env.wcsstr` | `i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcstod` | `__agc_runtime_wcstod` | `env.wcstod` | `i64,i64->f64` | read/write | wasm32-object-runtime | runtime |
| `wcstof` | `__agc_runtime_wcstof` | `env.wcstof` | `i64,i64->f32` | read/write | wasm32-object-runtime | runtime |
| `wcstok` | `__agc_runtime_wcstok` | `env.wcstok` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcstol` | `__agc_runtime_wcstol` | `env.wcstol` | `i64,i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `wcstold` | `__agc_runtime_wcstold` | `env.wcstold` | `i64,i64->f64` | read/write | wasm32-object-runtime | runtime |
| `wcstoll` | `__agc_runtime_wcstoll` | `env.wcstoll` | `i64,i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `wcstombs` | `__agc_runtime_wcstombs` | `env.wcstombs` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wcstoul` | `__agc_runtime_wcstoul` | `env.wcstoul` | `i64,i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `wcstoull` | `__agc_runtime_wcstoull` | `env.wcstoull` | `i64,i64,i32->i64` | read/write | wasm32-object-runtime | runtime |
| `wcsxfrm` | `__agc_runtime_wcsxfrm` | `env.wcsxfrm` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wctob` | `__agc_runtime_wctob` | `env.wctob` | `i32->i32` | read/write | wasm32-object-runtime | runtime |
| `wctomb` | `__agc_runtime_wctomb` | `env.wctomb` | `i64,i32->i32` | read/write | wasm32-object-runtime | runtime |
| `wctrans` | `__agc_runtime_wctrans` | `env.wctrans` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `wctype` | `__agc_runtime_wctype` | `env.wctype` | `i64->i32` | read/write | wasm32-object-runtime | runtime |
| `wmemchr` | `__agc_runtime_wmemchr` | `env.wmemchr` | `i64,i32,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wmemcmp` | `__agc_runtime_wmemcmp` | `env.wmemcmp` | `i64,i64,i64->i32` | read/write | wasm32-object-runtime | runtime |
| `wmemcpy` | `__agc_runtime_wmemcpy` | `env.wmemcpy` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wmemmove` | `__agc_runtime_wmemmove` | `env.wmemmove` | `i64,i64,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `wmemset` | `__agc_runtime_wmemset` | `env.wmemset` | `i64,i32,i64->i64` | read/write | wasm32-object-runtime | runtime |
| `write` | `__agc_runtime_write` | `env.write (stdio)` | `i32,i64,i64->i64` | read/write | wasm32-js, wasm32-object-runtime | runtime |
