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

const utf8Decoder = new TextDecoder();
const utf8Encoder = new TextEncoder();

function defaultGetMemory() {
  return undefined;
}

function defaultWrite(_text) {}

function normalizeBytes(input) {
  if (input === undefined || input === null) return new Uint8Array();
  if (typeof input === "string") return utf8Encoder.encode(input);
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  throw new TypeError("stdio.stdin must be a string, ArrayBuffer, or Uint8Array");
}

function readCString(memory, ptr, maxLen = 1024 * 1024) {
  ptr = Number(ptr) >>> 0;
  if (!memory || !memory.buffer || ptr >= memory.buffer.byteLength) return "";
  const bytes = new Uint8Array(memory.buffer);
  const endLimit = Math.min(bytes.length, ptr + maxLen);
  let end = ptr;
  while (end < endLimit && bytes[end] !== 0) end++;
  return utf8Decoder.decode(bytes.subarray(ptr, end));
}

function readMemoryUtf8(memory, ptr, len) {
  ptr = Number(ptr) >>> 0;
  len = Number(len) >>> 0;
  if (!memory || !memory.buffer || len === 0 || ptr >= memory.buffer.byteLength) return "";
  const bytes = new Uint8Array(memory.buffer);
  const end = Math.min(bytes.length, ptr + len);
  return utf8Decoder.decode(bytes.subarray(ptr, end));
}

function writeMemoryBytes(memory, ptr, src) {
  ptr = Number(ptr) >>> 0;
  if (!memory || !memory.buffer || ptr >= memory.buffer.byteLength) return 0;
  const bytes = new Uint8Array(memory.buffer);
  const writeLen = Math.min(src.length, bytes.length - ptr);
  if (writeLen > 0) bytes.set(src.subarray(0, writeLen), ptr);
  return writeLen;
}

function writeCString(memory, ptr, text, maxBytes = -1) {
  ptr = Number(ptr) >>> 0;
  maxBytes = Number(maxBytes);
  const encoded = utf8Encoder.encode(String(text));
  if (!memory || !memory.buffer || ptr >= memory.buffer.byteLength) return -1;
  const bytes = new Uint8Array(memory.buffer);
  if (maxBytes === 0) return encoded.length;
  const limit = maxBytes < 0 ? bytes.length - ptr : Math.min(maxBytes, bytes.length - ptr);
  if (limit <= 0) return encoded.length;
  const writeLen = Math.min(encoded.length, limit - 1);
  if (writeLen > 0) bytes.set(encoded.subarray(0, writeLen), ptr);
  bytes[ptr + writeLen] = 0;
  return encoded.length;
}

function padFormatted(text, flags, width, numeric = false) {
  if (width <= text.length) return text;
  const leftAlign = flags.includes("-");
  const padChar = numeric && flags.includes("0") && !leftAlign ? "0" : " ";
  const padding = padChar.repeat(width - text.length);
  if (leftAlign) return text + padding;
  if (padChar === "0" && (text.startsWith("-") || text.startsWith("+"))) {
    return text[0] + padding + text.slice(1);
  }
  return padding + text;
}

function formatPrintf(memory, fmtPtr, args) {
  const fmt = readCString(memory, fmtPtr);
  let argIndex = 0;
  return fmt.replace(/%([-+ #0]*)(\d+|\*)?(\.(\d+|\*))?(hh|h|ll|l|L|z|t|j)?([%csdiuoxXfFeEgGaAp])/g,
    (match, flags, widthToken, precisionMatch, precisionValue, _length, spec) => {
      if (spec === "%") return "%";
      let width = 0;
      if (widthToken === "*") {
        width = Number(args[argIndex++]) | 0;
      } else if (widthToken) {
        width = Number(widthToken) | 0;
      }
      if (width < 0) {
        flags += "-";
        width = -width;
      }
      let precision = -1;
      if (precisionMatch) {
        if (precisionValue === "*") {
          precision = Number(args[argIndex++]) | 0;
        } else if (precisionValue !== undefined) {
          precision = Number(precisionValue) | 0;
        } else {
          precision = 0;
        }
      }
      const value = args[argIndex++];
      let text;
      let numeric = false;
      switch (spec) {
        case "s":
          text = readCString(memory, value);
          if (precision >= 0) text = text.slice(0, precision);
          return padFormatted(text, flags, width);
        case "c":
          return padFormatted(String.fromCodePoint(Number(value) & 0xff), flags, width);
        case "d":
        case "i":
          text = String(Number(value) | 0);
          numeric = true;
          break;
        case "u":
          text = String(Number(value) >>> 0);
          numeric = true;
          break;
        case "o":
          text = (Number(value) >>> 0).toString(8);
          numeric = true;
          break;
        case "x":
          text = (Number(value) >>> 0).toString(16);
          numeric = true;
          break;
        case "X":
          text = (Number(value) >>> 0).toString(16).toUpperCase();
          numeric = true;
          break;
        case "f":
        case "F":
          text = precision >= 0 ? Number(value).toFixed(precision) : String(Number(value));
          numeric = true;
          break;
        case "e":
        case "E":
          text = precision >= 0 ? Number(value).toExponential(precision) : Number(value).toExponential();
          if (spec === "E") text = text.toUpperCase();
          numeric = true;
          break;
        case "g":
        case "G":
          text = precision >= 0 ? Number(value).toPrecision(precision || 1) : String(Number(value));
          if (spec === "G") text = text.toUpperCase();
          numeric = true;
          break;
        case "a":
        case "A":
          text = String(Number(value));
          numeric = true;
          break;
        case "p":
          text = `0x${(Number(value) >>> 0).toString(16)}`;
          break;
        default:
          return match;
      }
      return padFormatted(text, flags, width, numeric);
    });
}

function makeStdio(options = {}) {
  const getMemory = options.getMemory || defaultGetMemory;
  const writeStdout = options.onStdout || defaultWrite;
  const writeStderr = options.onStderr || writeStdout;
  const stdinBytes = normalizeBytes(options.stdin);
  let stdinOffset = 0;
  let stdinEof = false;
  let fileError = 0;

  function emitStdout(text) {
    writeStdout(String(text));
    return String(text).length;
  }

  function emitStderr(text) {
    writeStderr(String(text));
    return String(text).length;
  }

  function isStderrStream(stream) {
    const s = Number(stream);
    return s === 0 || s === 2;
  }

  function printf(fmt, ...args) {
    return emitStdout(formatPrintf(getMemory(), fmt, args));
  }

  function fprintf(stream, fmt, ...args) {
    const text = formatPrintf(getMemory(), fmt, args);
    return isStderrStream(stream) ? emitStderr(text) : emitStdout(text);
  }

  function sprintf(buf, fmt, ...args) {
    return writeCString(getMemory(), buf, formatPrintf(getMemory(), fmt, args));
  }

  function snprintf(buf, size, fmt, ...args) {
    return writeCString(getMemory(), buf, formatPrintf(getMemory(), fmt, args), size);
  }

  function puts(s) {
    return emitStdout(`${readCString(getMemory(), s)}\n`);
  }

  function fputs(s, stream) {
    const text = readCString(getMemory(), s);
    return isStderrStream(stream) ? emitStderr(text) : emitStdout(text);
  }

  function putchar(c) {
    const text = String.fromCodePoint(Number(c) & 0xff);
    emitStdout(text);
    return Number(c) | 0;
  }

  function fputc(c, stream) {
    const text = String.fromCodePoint(Number(c) & 0xff);
    if (isStderrStream(stream)) {
      emitStderr(text);
    } else {
      emitStdout(text);
    }
    return Number(c) | 0;
  }

  function fflush(_stream) {
    return 0;
  }

  function fwrite(ptr, size, nmemb, stream) {
    size = Number(size);
    nmemb = Number(nmemb);
    if (size <= 0 || nmemb <= 0) return 0n;
    const total = size * nmemb;
    const text = readMemoryUtf8(getMemory(), ptr, total);
    if (isStderrStream(stream)) {
      emitStderr(text);
    } else {
      emitStdout(text);
    }
    return BigInt(nmemb);
  }

  function write(fd, ptr, count) {
    fd = Number(fd);
    count = Number(count);
    if (count <= 0) return 0n;
    const text = readMemoryUtf8(getMemory(), ptr, count);
    if (fd === 2) {
      emitStderr(text);
    } else if (fd === 1) {
      emitStdout(text);
    } else {
      return -1n;
    }
    return BigInt(count);
  }

  function lseek(_fd, _offset, _whence) {
    return -1n;
  }

  function fread(ptr, size, nmemb, _stream) {
    size = Number(size);
    nmemb = Number(nmemb);
    if (size <= 0 || nmemb <= 0) return 0n;
    if (stdinOffset >= stdinBytes.length) {
      stdinEof = true;
      return 0n;
    }
    const requested = size * nmemb;
    const src = stdinBytes.subarray(stdinOffset, Math.min(stdinBytes.length, stdinOffset + requested));
    const copied = writeMemoryBytes(getMemory(), ptr, src);
    stdinOffset += copied;
    if (copied < requested) stdinEof = true;
    return BigInt(Math.floor(copied / size));
  }

  function fgetc(_stream) {
    if (stdinOffset >= stdinBytes.length) {
      stdinEof = true;
      return -1;
    }
    return stdinBytes[stdinOffset++];
  }

  function fgets(s, size, _stream) {
    size = Number(size);
    if (size <= 0) return 0;
    if (stdinOffset >= stdinBytes.length) {
      stdinEof = true;
      return 0;
    }
    const out = [];
    while (out.length + 1 < size && stdinOffset < stdinBytes.length) {
      const ch = stdinBytes[stdinOffset++];
      out.push(ch);
      if (ch === 10) break;
    }
    const bytes = new Uint8Array(out.length + 1);
    bytes.set(out, 0);
    bytes[out.length] = 0;
    writeMemoryBytes(getMemory(), s, bytes);
    return Number(s);
  }

  function feof(_stream) {
    return stdinEof ? 1 : 0;
  }

  function ferror(_stream) {
    return fileError;
  }

  function clearerr(_stream) {
    stdinEof = false;
    fileError = 0;
  }

  function perror(s) {
    const prefix = readCString(getMemory(), s);
    const text = prefix ? `${prefix}: error\n` : "error\n";
    emitStderr(text);
  }

  function runtimeStdoutWrite(ptr, len) {
    emitStdout(readMemoryUtf8(getMemory(), ptr, len));
  }

  function runtimeStderrWrite(ptr, len) {
    emitStderr(readMemoryUtf8(getMemory(), ptr, len));
  }

  return {
    printf,
    fprintf,
    sprintf,
    snprintf,
    puts,
    fputs,
    putchar,
    fputc,
    fflush,
    fread,
    fwrite,
    write,
    lseek,
    fgetc,
    fgets,
    feof,
    ferror,
    clearerr,
    perror,
    runtimeStdoutWrite,
    runtimeStderrWrite,
  };
}

function agcFopen(_path, _mode) {
  return 0;
}

function agcFclose(_stream) {
  return 0;
}

export function createAgcRuntimeStdioEnvImports(options = {}) {
  const stdio = makeStdio(options);
  return {
    printf: stdio.printf,
    fprintf: stdio.fprintf,
    sprintf: stdio.sprintf,
    snprintf: stdio.snprintf,
    vfprintf: stdio.fprintf,
    vsnprintf: () => 0,
    puts: stdio.puts,
    fputs: stdio.fputs,
    putchar: stdio.putchar,
    fputc: stdio.fputc,
    fflush: stdio.fflush,
    __agc_runtime_stdout_write: stdio.runtimeStdoutWrite,
    __agc_runtime_stderr_write: stdio.runtimeStderrWrite,
    fopen: agcFopen,
    fclose: agcFclose,
    fread: stdio.fread,
    fwrite: stdio.fwrite,
    write: stdio.write,
    lseek: stdio.lseek,
    fgetc: stdio.fgetc,
    getc: stdio.fgetc,
    getchar: stdio.fgetc,
    fgets: stdio.fgets,
    feof: stdio.feof,
    ferror: stdio.ferror,
    clearerr: stdio.clearerr,
    perror: stdio.perror,
  };
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
  const { stdio, onStdout, onStderr, ...wasmImports } = imports;
  return {
    ...wasmImports,
    env: {
      ...createAgcRuntimeMathEnvImports(),
      ...createAgcRuntimeStdioEnvImports({ ...(stdio || {}), onStdout, onStderr }),
      ...(imports.env || {}),
    },
  };
}
