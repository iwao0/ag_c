function agcRound(x) {
  x = Number(x);
  return x < 0 ? -Math.floor(-x + 0.5) : Math.floor(x + 0.5);
}

function agcFmin(x, y) {
  x = Number(x);
  y = Number(y);
  if (Number.isNaN(x)) return y;
  if (Number.isNaN(y)) return x;
  return Math.min(x, y);
}

function agcFmax(x, y) {
  x = Number(x);
  y = Number(y);
  if (Number.isNaN(x)) return y;
  if (Number.isNaN(y)) return x;
  return Math.max(x, y);
}

function wrapMath(fn) {
  return (...args) => fn(...args.map(Number));
}

export function createAgcRuntimeMathEnvImports() {
  return {
    acos: wrapMath(Math.acos),
    acosf: wrapMath(Math.acos),
    acosl: wrapMath(Math.acos),
    asin: wrapMath(Math.asin),
    asinf: wrapMath(Math.asin),
    asinl: wrapMath(Math.asin),
    atan: wrapMath(Math.atan),
    atanf: wrapMath(Math.atan),
    atanl: wrapMath(Math.atan),
    atan2: wrapMath(Math.atan2),
    atan2f: wrapMath(Math.atan2),
    atan2l: wrapMath(Math.atan2),
    cbrt: wrapMath(Math.cbrt),
    cbrtf: wrapMath(Math.cbrt),
    cbrtl: wrapMath(Math.cbrt),
    ceil: wrapMath(Math.ceil),
    ceilf: wrapMath(Math.ceil),
    ceill: wrapMath(Math.ceil),
    cos: wrapMath(Math.cos),
    cosf: wrapMath(Math.cos),
    cosl: wrapMath(Math.cos),
    cosh: wrapMath(Math.cosh),
    exp: wrapMath(Math.exp),
    expf: wrapMath(Math.exp),
    expl: wrapMath(Math.exp),
    fabs: wrapMath(Math.abs),
    fabsf: wrapMath(Math.abs),
    fabsl: wrapMath(Math.abs),
    floor: wrapMath(Math.floor),
    floorf: wrapMath(Math.floor),
    floorl: wrapMath(Math.floor),
    fmax: agcFmax,
    fmaxf: agcFmax,
    fmaxl: agcFmax,
    fmin: agcFmin,
    fminf: agcFmin,
    fminl: agcFmin,
    fmod: wrapMath((x, y) => x % y),
    fmodf: wrapMath((x, y) => x % y),
    fmodl: wrapMath((x, y) => x % y),
    hypot: wrapMath(Math.hypot),
    hypotf: wrapMath(Math.hypot),
    hypotl: wrapMath(Math.hypot),
    log: wrapMath(Math.log),
    logf: wrapMath(Math.log),
    logl: wrapMath(Math.log),
    log10: wrapMath(Math.log10),
    log10f: wrapMath(Math.log10),
    log10l: wrapMath(Math.log10),
    log2: wrapMath(Math.log2),
    log2f: wrapMath(Math.log2),
    log2l: wrapMath(Math.log2),
    pow: wrapMath(Math.pow),
    powf: wrapMath(Math.pow),
    powl: wrapMath(Math.pow),
    round: agcRound,
    roundf: agcRound,
    roundl: agcRound,
    sin: wrapMath(Math.sin),
    sinf: wrapMath(Math.sin),
    sinl: wrapMath(Math.sin),
    sinh: wrapMath(Math.sinh),
    sqrt: wrapMath(Math.sqrt),
    sqrtf: wrapMath(Math.sqrt),
    sqrtl: wrapMath(Math.sqrt),
    tan: wrapMath(Math.tan),
    tanf: wrapMath(Math.tan),
    tanl: wrapMath(Math.tan),
    tanh: wrapMath(Math.tanh),
    trunc: wrapMath(Math.trunc),
    truncf: wrapMath(Math.trunc),
    truncl: wrapMath(Math.trunc),
    __agc_runtime_math_acos: wrapMath(Math.acos),
    __agc_runtime_math_asin: wrapMath(Math.asin),
    __agc_runtime_math_atan: wrapMath(Math.atan),
    __agc_runtime_math_atan2: wrapMath(Math.atan2),
    __agc_runtime_math_cbrt: wrapMath(Math.cbrt),
    __agc_runtime_math_ceil: wrapMath(Math.ceil),
    __agc_runtime_math_cos: wrapMath(Math.cos),
    __agc_runtime_math_cosh: wrapMath(Math.cosh),
    __agc_runtime_math_exp: wrapMath(Math.exp),
    __agc_runtime_math_fabs: wrapMath(Math.abs),
    __agc_runtime_math_floor: wrapMath(Math.floor),
    __agc_runtime_math_fmax: agcFmax,
    __agc_runtime_math_fmin: agcFmin,
    __agc_runtime_math_fmod: wrapMath((x, y) => x % y),
    __agc_runtime_math_hypot: wrapMath(Math.hypot),
    __agc_runtime_math_log: wrapMath(Math.log),
    __agc_runtime_math_log10: wrapMath(Math.log10),
    __agc_runtime_math_log2: wrapMath(Math.log2),
    __agc_runtime_math_pow: wrapMath(Math.pow),
    __agc_runtime_math_round: agcRound,
    __agc_runtime_math_sin: wrapMath(Math.sin),
    __agc_runtime_math_sinh: wrapMath(Math.sinh),
    __agc_runtime_math_sqrt: wrapMath(Math.sqrt),
    __agc_runtime_math_tan: wrapMath(Math.tan),
    __agc_runtime_math_tanh: wrapMath(Math.tanh),
    __agc_runtime_math_trunc: wrapMath(Math.trunc),
  };
}

export function createAgcRuntimeImports(imports = {}) {
  return {
    ...imports,
    env: {
      ...createAgcRuntimeMathEnvImports(),
      ...(imports.env || {}),
    },
  };
}
