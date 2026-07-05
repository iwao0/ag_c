function agcRound(x) {
  x = Number(x);
  return x < 0 ? -Math.floor(-x + 0.5) : Math.floor(x + 0.5);
}

function agcRoundEven(x) {
  x = Number(x);
  if (!Number.isFinite(x) || x === 0) return x;
  const t = Math.trunc(x);
  const frac = Math.abs(x - t);
  if (frac > 0.5 || (frac === 0.5 && (Math.abs(t) & 1))) {
    return t + (x < 0 ? -1 : 1);
  }
  return t;
}

function agcI64FromNumber(x) {
  x = Number(x);
  if (!Number.isFinite(x)) return 0n;
  return BigInt(Math.trunc(x));
}

function agcLrint(x) {
  return agcI64FromNumber(agcRoundEven(x));
}

function agcLround(x) {
  return agcI64FromNumber(agcRound(x));
}

function agcErfcTail(ax) {
  const t = 1 / (1 + 0.3275911 * ax);
  const poly = (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t -
    0.284496736) * t + 0.254829592) * t;
  return poly * Math.exp(-ax * ax);
}

function agcErf(x) {
  x = Number(x);
  if (Number.isNaN(x)) return x;
  if (!Number.isFinite(x)) return x < 0 ? -1 : 1;
  if (x === 0) return x;
  const sign = x < 0 ? -1 : 1;
  return sign * (1 - agcErfcTail(Math.abs(x)));
}

function agcErfc(x) {
  x = Number(x);
  if (Number.isNaN(x)) return x;
  if (!Number.isFinite(x)) return x < 0 ? 2 : 0;
  if (x === 0) return 1;
  const tail = agcErfcTail(Math.abs(x));
  return x < 0 ? 2 - tail : tail;
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

function agcFdim(x, y) {
  x = Number(x);
  y = Number(y);
  if (Number.isNaN(x)) return x;
  if (Number.isNaN(y)) return y;
  return x > y ? x - y : 0;
}

function agcFma(x, y, z) {
  return Number(x) * Number(y) + Number(z);
}

function agcNearestEvenQuot(q) {
  q = Number(q);
  const sign = q < 0 ? -1 : 1;
  const aq = Math.abs(q);
  let n = Math.trunc(aq);
  const frac = aq - n;
  if (frac > 0.5 || (frac === 0.5 && (n & 1))) n++;
  return sign * n;
}

function agcRemainder(x, y) {
  x = Number(x);
  y = Number(y);
  if (Number.isNaN(x)) return x;
  if (Number.isNaN(y)) return y;
  if (y === 0 || !Number.isFinite(x)) return Number.NaN;
  if (!Number.isFinite(y)) return x;
  return x - agcNearestEvenQuot(x / y) * y;
}

function agcRemquo(memory, x, y, quoPtr) {
  const result = agcRemainder(x, y);
  if (quoPtr) {
    const q = Number.isFinite(Number(x)) && Number.isFinite(Number(y)) && Number(y) !== 0
      ? agcNearestEvenQuot(Number(x) / Number(y))
      : 0;
    const bits = Math.sign(q || 1) * (Math.abs(q) & 7);
    writeMemoryI32(memory, quoPtr, bits);
  }
  return result;
}

function agcFpclassify(x) {
  x = Number(x);
  if (Number.isNaN(x)) return 0;
  if (!Number.isFinite(x)) return 1;
  if (x === 0) return 2;
  return Math.abs(x) < 2.2250738585072014e-308 ? 3 : 4;
}

function agcSignbit(x) {
  x = Number(x);
  return x < 0 || Object.is(x, -0) ? 1 : 0;
}

function agcIsunordered(x, y) {
  return Number.isNaN(Number(x)) || Number.isNaN(Number(y)) ? 1 : 0;
}

function agcLdexp(x, exp) {
  return Number(x) * Math.pow(2, Number(exp) | 0);
}

function agcScalbln(x, exp) {
  return Number(x) * Math.pow(2, Number(exp));
}

function agcIlogb(x) {
  x = Number(x);
  if (Number.isNaN(x) || x === 0) return -2147483648;
  if (!Number.isFinite(x)) return 2147483647;
  return Math.floor(Math.log2(Math.abs(x)));
}

function agcLogb(x) {
  x = Number(x);
  if (Number.isNaN(x)) return x;
  if (!Number.isFinite(x)) return Number.POSITIVE_INFINITY;
  if (x === 0) return Number.NEGATIVE_INFINITY;
  return agcIlogb(x);
}

function agcFrexp(memory, x, expPtr) {
  x = Number(x);
  if (!Number.isFinite(x) || x === 0) {
    writeMemoryI32(memory, expPtr, 0);
    return x;
  }
  const sign = x < 0 || Object.is(x, -0) ? -1 : 1;
  let ax = Math.abs(x);
  let exp = 0;
  while (ax >= 1) {
    ax /= 2;
    exp++;
  }
  while (ax < 0.5) {
    ax *= 2;
    exp--;
  }
  writeMemoryI32(memory, expPtr, exp);
  return sign * ax;
}

function agcModf(memory, x, intPtr, writeFloat) {
  x = Number(x);
  let whole;
  let frac;
  if (Number.isNaN(x)) {
    whole = x;
    frac = x;
  } else if (!Number.isFinite(x)) {
    whole = x;
    frac = x < 0 ? -0 : 0;
  } else {
    whole = Math.trunc(x);
    frac = x === whole ? (agcSignbit(x) ? -0 : 0) : x - whole;
  }
  if (writeFloat) writeMemoryF32(memory, intPtr, whole);
  else writeMemoryF64(memory, intPtr, whole);
  return frac;
}

function wrapMath(fn) {
  return (...args) => fn(...args.map(Number));
}

const AGC_MATH_IMPORTS = [
  ["acos", wrapMath(Math.acos), ["f", "l"], true],
  ["acosh", wrapMath(Math.acosh), ["f", "l"], true],
  ["asin", wrapMath(Math.asin), ["f", "l"], true],
  ["asinh", wrapMath(Math.asinh), ["f", "l"], true],
  ["atan", wrapMath(Math.atan), ["f", "l"], true],
  ["atan2", wrapMath(Math.atan2), ["f", "l"], true],
  ["atanh", wrapMath(Math.atanh), ["f", "l"], true],
  ["cbrt", wrapMath(Math.cbrt), ["f", "l"], true],
  ["ceil", wrapMath(Math.ceil), ["f", "l"], true],
  ["cos", wrapMath(Math.cos), ["f", "l"], true],
  ["cosh", wrapMath(Math.cosh), ["f", "l"], true],
  ["exp", wrapMath(Math.exp), ["f", "l"], true],
  ["exp2", wrapMath((x) => Math.pow(2, x)), ["f", "l"], true],
  ["expm1", wrapMath(Math.expm1), ["f", "l"], true],
  ["erf", agcErf, ["f", "l"], true],
  ["erfc", agcErfc, ["f", "l"], true],
  ["fabs", wrapMath(Math.abs), ["f", "l"], true],
  ["floor", wrapMath(Math.floor), ["f", "l"], true],
  ["fdim", agcFdim, ["f", "l"], true],
  ["fma", agcFma, ["f", "l"], true],
  ["fmax", agcFmax, ["f", "l"], true],
  ["fmin", agcFmin, ["f", "l"], true],
  ["fmod", wrapMath((x, y) => x % y), ["f", "l"], true],
  ["hypot", wrapMath(Math.hypot), ["f", "l"], true],
  ["ilogb", agcIlogb, ["f", "l"], true],
  ["log", wrapMath(Math.log), ["f", "l"], true],
  ["log1p", wrapMath(Math.log1p), ["f", "l"], true],
  ["log10", wrapMath(Math.log10), ["f", "l"], true],
  ["log2", wrapMath(Math.log2), ["f", "l"], true],
  ["logb", agcLogb, ["f", "l"], true],
  ["nearbyint", agcRoundEven, ["f", "l"], true],
  ["pow", wrapMath(Math.pow), ["f", "l"], true],
  ["remainder", agcRemainder, ["f", "l"], true],
  ["rint", agcRoundEven, ["f", "l"], true],
  ["lrint", agcLrint, ["f", "l"], true],
  ["llrint", agcLrint, ["f", "l"], true],
  ["round", agcRound, ["f", "l"], true],
  ["lround", agcLround, ["f", "l"], true],
  ["llround", agcLround, ["f", "l"], true],
  ["scalbn", agcLdexp, ["f", "l"], true],
  ["scalbln", agcScalbln, ["f", "l"], true],
  ["sin", wrapMath(Math.sin), ["f", "l"], true],
  ["sinh", wrapMath(Math.sinh), ["f", "l"], true],
  ["sqrt", wrapMath(Math.sqrt), ["f", "l"], true],
  ["tan", wrapMath(Math.tan), ["f", "l"], true],
  ["tanh", wrapMath(Math.tanh), ["f", "l"], true],
  ["trunc", wrapMath(Math.trunc), ["f", "l"], true],
];

function addMathImportFamily(env, baseName, fn, suffixes, runtimeAlias) {
  env[baseName] = fn;
  for (const suffix of suffixes) env[`${baseName}${suffix}`] = fn;
  if (runtimeAlias) env[`__agc_runtime_math_${baseName}`] = fn;
}

const utf8Decoder = new TextDecoder();
const utf8Encoder = new TextEncoder();
const AGC_JS_ERRNO_ADDR = 16;
const AGC_ENOENT = 2;
const AGC_EBADF = 9;
const AGC_ENAMETOOLONG = 36;
const AGC_EINVAL = 22;
const AGC_FILE_NAME_CAP = 64;

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

function cStringFileNameErrno(memory, ptr) {
  ptr = Number(ptr) >>> 0;
  if (!memory || !memory.buffer || ptr >= memory.buffer.byteLength) return AGC_EINVAL;
  const bytes = new Uint8Array(memory.buffer);
  for (let i = 0; ptr + i < bytes.length; i++) {
    if (bytes[ptr + i] === 0) return 0;
    if (i + 1 >= AGC_FILE_NAME_CAP) return AGC_ENAMETOOLONG;
  }
  return AGC_EINVAL;
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

function writeWChar(memory, ptr, value) {
  ptr = Number(ptr) >>> 0;
  if (!memory || !memory.buffer || ptr + 3 >= memory.buffer.byteLength) return false;
  new DataView(memory.buffer).setInt32(ptr, Number(value) | 0, true);
  return true;
}

function writeMemoryI32(memory, ptr, value) {
  ptr = Number(ptr) >>> 0;
  if (!memory || !memory.buffer || ptr + 3 >= memory.buffer.byteLength) return false;
  new DataView(memory.buffer).setInt32(ptr, Number(value) | 0, true);
  return true;
}

function writeMemoryI16(memory, ptr, value) {
  ptr = Number(ptr) >>> 0;
  if (!memory || !memory.buffer || ptr + 1 >= memory.buffer.byteLength) return false;
  new DataView(memory.buffer).setInt16(ptr, Number(value) | 0, true);
  return true;
}

function writeMemoryI8(memory, ptr, value) {
  ptr = Number(ptr) >>> 0;
  if (!memory || !memory.buffer || ptr >= memory.buffer.byteLength) return false;
  new DataView(memory.buffer).setInt8(ptr, Number(value) | 0);
  return true;
}

function writeMemoryI64(memory, ptr, value) {
  ptr = Number(ptr) >>> 0;
  if (!memory || !memory.buffer || ptr + 7 >= memory.buffer.byteLength) return false;
  new DataView(memory.buffer).setBigInt64(ptr, BigInt(value), true);
  return true;
}

function readMemoryI32(memory, ptr) {
  ptr = Number(ptr) >>> 0;
  if (!memory || !memory.buffer || ptr + 3 >= memory.buffer.byteLength) return 0;
  return new DataView(memory.buffer).getInt32(ptr, true);
}

function writeMemoryF32(memory, ptr, value) {
  ptr = Number(ptr) >>> 0;
  if (!memory || !memory.buffer || ptr + 3 >= memory.buffer.byteLength) return false;
  new DataView(memory.buffer).setFloat32(ptr, Number(value), true);
  return true;
}

function writeMemoryF64(memory, ptr, value) {
  ptr = Number(ptr) >>> 0;
  if (!memory || !memory.buffer || ptr + 7 >= memory.buffer.byteLength) return false;
  new DataView(memory.buffer).setFloat64(ptr, Number(value), true);
  return true;
}

function utf8ByteLength(text) {
  return utf8Encoder.encode(String(text)).length;
}

function padFormatted(text, flags, width, numeric = false) {
  const len = utf8ByteLength(text);
  if (width <= len) return text;
  const leftAlign = flags.includes("-");
  const padChar = numeric && flags.includes("0") && !leftAlign ? "0" : " ";
  const padding = padChar.repeat(width - len);
  if (leftAlign) return text + padding;
  if (padChar === "0" && (text.startsWith("-") || text.startsWith("+"))) {
    return text[0] + padding + text.slice(1);
  }
  if (padChar === "0" && text.startsWith(" ")) {
    return text[0] + padding + text.slice(1);
  }
  if (padChar === "0" && (text.startsWith("0x") || text.startsWith("0X"))) {
    return text.slice(0, 2) + padding + text.slice(2);
  }
  return padding + text;
}

function formatStringArg(memory, ptr, precision) {
  if (!Number(ptr)) {
    const text = "(null)";
    return precision >= 0 ? text.slice(0, precision) : text;
  }
  return readCString(memory, ptr, precision >= 0 ? precision : 1024 * 1024);
}

function integerBitsForLength(length) {
  if (length === "hh") return 8n;
  if (length === "h") return 16n;
  if (length === "l" || length === "ll" || length === "z" || length === "t" || length === "j") return 64n;
  return 32n;
}

function valueToBigInt(value) {
  if (typeof value === "bigint") return value;
  return BigInt(Number(value) | 0);
}

function precisionFlags(flags, precision) {
  return precision >= 0 ? flags.replace(/0/g, "") : flags;
}

function zeroPadIntegerDigits(digits, precision) {
  if (precision <= digits.length) return digits;
  return "0".repeat(precision - digits.length) + digits;
}

function signedIntegerValue(value, length) {
  const bits = integerBitsForLength(length);
  const mod = 1n << bits;
  const sign = 1n << (bits - 1n);
  let v = valueToBigInt(value) & (mod - 1n);
  if ((v & sign) !== 0n) v -= mod;
  return v;
}

function unsignedIntegerValue(value, length) {
  const bits = integerBitsForLength(length);
  const mod = 1n << bits;
  return valueToBigInt(value) & (mod - 1n);
}

function formatSignedInteger(value, length, precision, flags) {
  const v = signedIntegerValue(value, length);
  const negative = v < 0n;
  const magnitude = negative ? -v : v;
  let digits = precision === 0 && magnitude === 0n ? "" : magnitude.toString(10);
  digits = zeroPadIntegerDigits(digits, precision);
  if (negative) return `-${digits}`;
  if (flags.includes("+")) return `+${digits}`;
  if (flags.includes(" ")) return ` ${digits}`;
  return digits;
}

function formatUnsignedInteger(value, length, base, upper, precision, flags) {
  const v = unsignedIntegerValue(value, length);
  let digits = precision === 0 && v === 0n ? "" : v.toString(base);
  if (upper) digits = digits.toUpperCase();
  const rawDigitLength = digits.length;
  digits = zeroPadIntegerDigits(digits, precision);
  if (flags.includes("#") && base === 16 && v !== 0n) return `${upper ? "0X" : "0x"}${digits}`;
  if (flags.includes("#") && base === 8 &&
      ((v === 0n && precision === 0) || (v !== 0n && (precision < 0 || precision <= rawDigitLength)))) {
    return `0${digits}`;
  }
  return digits;
}

function formatPointer(value) {
  return `0x${(valueToBigInt(value) & ((1n << 64n) - 1n)).toString(16)}`;
}

function storeFormatCount(memory, ptr, length, count) {
  if (length === "hh") return writeMemoryI8(memory, ptr, count);
  if (length === "h") return writeMemoryI16(memory, ptr, count);
  if (length === "l" || length === "ll" || length === "z" || length === "t" || length === "j") {
    return writeMemoryI64(memory, ptr, BigInt(count));
  }
  return writeMemoryI32(memory, ptr, count);
}

function formatPrintf(memory, fmtPtr, args) {
  const fmt = readCString(memory, fmtPtr);
  let argIndex = 0;
  let lastEnd = 0;
  let outputByteCount = 0;
  return fmt.replace(/%([-+ #0]*)(\d+|\*)?(\.(\d+|\*))?(hh|h|ll|l|L|z|t|j)?([%csdiuoxXfFeEgGaApn])/g,
    (match, flags, widthToken, precisionMatch, precisionValue, length, spec, offset) => {
      outputByteCount += utf8ByteLength(fmt.slice(lastEnd, offset));
      lastEnd = offset + match.length;
      if (spec === "%") {
        outputByteCount += 1;
        return "%";
      }
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
          text = formatStringArg(memory, value, precision);
          break;
        case "c":
          text = String.fromCodePoint(Number(value) & 0xff);
          break;
        case "d":
        case "i":
          text = formatSignedInteger(value, length, precision, flags);
          flags = precisionFlags(flags, precision);
          numeric = true;
          break;
        case "u":
          text = formatUnsignedInteger(value, length, 10, false, precision, flags);
          flags = precisionFlags(flags, precision);
          numeric = true;
          break;
        case "o":
          text = formatUnsignedInteger(value, length, 8, false, precision, flags);
          flags = precisionFlags(flags, precision);
          numeric = true;
          break;
        case "x":
          text = formatUnsignedInteger(value, length, 16, false, precision, flags);
          flags = precisionFlags(flags, precision);
          numeric = true;
          break;
        case "X":
          text = formatUnsignedInteger(value, length, 16, true, precision, flags);
          flags = precisionFlags(flags, precision);
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
          text = formatPointer(value);
          break;
        case "n":
          storeFormatCount(memory, value, length, outputByteCount);
          return "";
        default:
          return match;
      }
      text = padFormatted(text, flags, width, numeric);
      outputByteCount += utf8ByteLength(text);
      return text;
    });
}

function makeStdio(options = {}) {
  const getMemory = options.getMemory || defaultGetMemory;
  const writeStdout = options.onStdout || defaultWrite;
  const writeStderr = options.onStderr || writeStdout;
  const stdinBytes = normalizeBytes(options.stdin);
  let stdinOffset = 0;
  const stdinPushback = [];
  let stdinEof = false;
  let stdinError = 0;
  const setErrno = (value) => writeMemoryI32(getMemory(), AGC_JS_ERRNO_ADDR, value);
  const getErrno = () => readMemoryI32(getMemory(), AGC_JS_ERRNO_ADDR);
  const strerrorMessage = (value) => Number(value) === 0 ? "no error" : "error";

  function emitStdout(text) {
    text = String(text);
    writeStdout(text);
    return utf8Encoder.encode(text).length;
  }

  function emitStderr(text) {
    text = String(text);
    writeStderr(text);
    return utf8Encoder.encode(text).length;
  }

  function outputStreamKind(stream) {
    const s = Number(stream);
    if (stream === undefined || s === 1) return "stdout";
    if (s === 2) return "stderr";
    setErrno(AGC_EBADF);
    if (s === 0) stdinError = 1;
    return null;
  }

  function isKnownStream(stream) {
    const s = Number(stream);
    return stream === undefined || s === 0 || s === 1 || s === 2;
  }

  function rejectKnownStream(stream) {
    if (isKnownStream(stream)) return false;
    setErrno(AGC_EBADF);
    return true;
  }

  function isInputStream(stream) {
    return stream === undefined || Number(stream) === 0;
  }

  function rejectInputStream(stream) {
    if (isInputStream(stream)) return false;
    setErrno(AGC_EBADF);
    return true;
  }

  function ioTotalSize(size, nmemb) {
    size = Number(size);
    nmemb = Number(nmemb);
    if (size < 0 || nmemb < 0 || !Number.isFinite(size) || !Number.isFinite(nmemb)) {
      setErrno(AGC_EINVAL);
      return null;
    }
    if (size === 0 || nmemb === 0) return 0;
    if (size > Number.MAX_SAFE_INTEGER / nmemb) {
      setErrno(AGC_EINVAL);
      return null;
    }
    return size * nmemb;
  }

  function ioByteCount(count) {
    count = Number(count);
    if (count < 0 || !Number.isFinite(count) || count > Number.MAX_SAFE_INTEGER) {
      setErrno(AGC_EINVAL);
      return null;
    }
    return count;
  }

  function __error() {
    return AGC_JS_ERRNO_ADDR;
  }

  function printf(fmt, ...args) {
    return emitStdout(formatPrintf(getMemory(), fmt, args));
  }

  function fprintf(stream, fmt, ...args) {
    const kind = outputStreamKind(stream);
    if (!kind) return -1;
    const text = formatPrintf(getMemory(), fmt, args);
    return kind === "stderr" ? emitStderr(text) : emitStdout(text);
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
    const kind = outputStreamKind(stream);
    if (!kind) return -1;
    const text = readCString(getMemory(), s);
    return kind === "stderr" ? emitStderr(text) : emitStdout(text);
  }

  function putchar(c) {
    const text = String.fromCodePoint(Number(c) & 0xff);
    emitStdout(text);
    return Number(c) | 0;
  }

  function fputc(c, stream) {
    const kind = outputStreamKind(stream);
    if (!kind) return -1;
    const text = String.fromCodePoint(Number(c) & 0xff);
    if (kind === "stderr") {
      emitStderr(text);
    } else {
      emitStdout(text);
    }
    return Number(c) | 0;
  }

  function fflush(stream) {
    if (rejectKnownStream(stream)) return -1;
    return 0;
  }

  function isValidFileMode(modePtr) {
    const mode = readCString(getMemory(), modePtr);
    return mode === "r" || mode === "w" || mode === "a" ||
      mode === "r+" || mode === "w+" || mode === "a+" ||
      mode === "rb" || mode === "wb" || mode === "ab" ||
      mode === "rb+" || mode === "wb+" || mode === "ab+" ||
      mode === "r+b" || mode === "w+b" || mode === "a+b";
  }

  function fopen(path, mode) {
    if (!Number(path) || !isValidFileMode(mode)) {
      setErrno(AGC_EINVAL);
      return 0;
    }
    const nameErrno = cStringFileNameErrno(getMemory(), path);
    if (nameErrno) {
      setErrno(nameErrno);
      return 0;
    }
    setErrno(AGC_ENOENT);
    return 0;
  }

  function fclose(stream) {
    if (rejectKnownStream(stream)) return -1;
    return 0;
  }

  function readStdinByte() {
    if (stdinPushback.length > 0) return stdinPushback.shift();
    if (stdinOffset >= stdinBytes.length) {
      stdinEof = true;
      return -1;
    }
    return stdinBytes[stdinOffset++];
  }

  function ungetc(c, stream) {
    if (rejectInputStream(stream)) return -1;
    c = Number(c) | 0;
    if (c === -1) {
      setErrno(AGC_EINVAL);
      return -1;
    }
    const ch = c & 0xff;
    stdinPushback.unshift(ch);
    stdinEof = false;
    return ch;
  }

  function fwrite(ptr, size, nmemb, stream) {
    const total = ioTotalSize(size, nmemb);
    if (total === null) return 0n;
    if (total === 0) return 0n;
    size = Number(size);
    nmemb = Number(nmemb);
    const kind = outputStreamKind(stream);
    if (!kind) return 0n;
    const text = readMemoryUtf8(getMemory(), ptr, total);
    if (kind === "stderr") {
      emitStderr(text);
    } else {
      emitStdout(text);
    }
    return BigInt(nmemb);
  }

  function write(fd, ptr, count) {
    fd = Number(fd);
    const isStderr = fd === 2;
    if (fd !== 1 && !isStderr) {
      setErrno(AGC_EBADF);
      return -1n;
    }
    count = ioByteCount(count);
    if (count === null) return -1n;
    if (count === 0) return 0n;
    const text = readMemoryUtf8(getMemory(), ptr, count);
    if (isStderr) {
      emitStderr(text);
    } else {
      emitStdout(text);
    }
    return BigInt(count);
  }

  function lseek(_fd, _offset, _whence) {
    setErrno(AGC_EBADF);
    return -1n;
  }

  function fread(ptr, size, nmemb, stream) {
    if (rejectInputStream(stream)) return 0n;
    const requested = ioTotalSize(size, nmemb);
    if (requested === null) return 0n;
    if (requested === 0) return 0n;
    size = Number(size);
    const out = new Uint8Array(requested);
    let copied = 0;
    while (copied < requested) {
      const ch = readStdinByte();
      if (ch < 0) break;
      out[copied++] = ch;
    }
    if (copied === 0) return 0n;
    writeMemoryBytes(getMemory(), ptr, out.subarray(0, copied));
    return BigInt(Math.floor(copied / size));
  }

  function fgetc(stream) {
    if (rejectInputStream(stream)) return -1;
    return readStdinByte();
  }

  function fgets(s, size, stream) {
    size = Number(size);
    if (rejectInputStream(stream)) return 0;
    if (size <= 0) {
      setErrno(AGC_EINVAL);
      return 0;
    }
    const out = [];
    while (out.length + 1 < size) {
      const ch = readStdinByte();
      if (ch < 0) break;
      out.push(ch);
      if (ch === 10) break;
    }
    if (out.length === 0) return 0;
    const bytes = new Uint8Array(out.length + 1);
    bytes.set(out, 0);
    bytes[out.length] = 0;
    writeMemoryBytes(getMemory(), s, bytes);
    return Number(s);
  }

  function readUtf8CodePoint() {
    const first = readStdinByte();
    let need = 0;
    let cp = 0;
    if (first < 0) return -1;
    if ((first & 0x80) === 0) return first;
    if ((first & 0xe0) === 0xc0) {
      need = 1;
      cp = first & 0x1f;
    } else if ((first & 0xf0) === 0xe0) {
      need = 2;
      cp = first & 0x0f;
    } else if ((first & 0xf8) === 0xf0) {
      need = 3;
      cp = first & 0x07;
    } else {
      stdinError = 1;
      return -1;
    }
    for (let i = 0; i < need; i++) {
      const ch = readStdinByte();
      if (ch < 0 || (ch & 0xc0) !== 0x80) {
        stdinError = 1;
        return -1;
      }
      cp = (cp << 6) | (ch & 0x3f);
    }
    return cp;
  }

  function fgetwc(stream) {
    if (rejectInputStream(stream)) return -1;
    return readUtf8CodePoint(stream);
  }

  function fgetws(s, n, stream) {
    n = Number(n) | 0;
    if (!Number(s) || n <= 0) return 0;
    if (rejectInputStream(stream)) return 0;
    let count = 0;
    while (count + 1 < n) {
      const wc = fgetwc(stream);
      if (wc < 0) break;
      if (!writeWChar(getMemory(), Number(s) + count * 4, wc)) return 0;
      count++;
      if (wc === 10) break;
    }
    if (count === 0) return 0;
    writeWChar(getMemory(), Number(s) + count * 4, 0);
    return Number(s);
  }

  function fputwc(wc, stream) {
    wc = Number(wc) | 0;
    let text;
    try {
      text = String.fromCodePoint(wc);
    } catch {
      return -1;
    }
    const kind = outputStreamKind(stream);
    if (!kind) return -1;
    if (kind === "stderr") {
      emitStderr(text);
    } else {
      emitStdout(text);
    }
    return wc;
  }

  function fputws(s, stream) {
    const memory = getMemory();
    const ptr = Number(s) >>> 0;
    if (!Number(s)) return -1;
    if (!memory || !memory.buffer || ptr >= memory.buffer.byteLength) return -1;
    const view = new DataView(memory.buffer);
    const end = memory.buffer.byteLength;
    let count = 0;
    for (let p = ptr; p + 3 < end; p += 4) {
      const wc = view.getInt32(p, true);
      if (wc === 0) return count;
      if (fputwc(wc, stream) < 0) return -1;
      count++;
    }
    return -1;
  }

  function ungetwc(wc, stream) {
    wc = Number(wc) | 0;
    if (wc < 0 || wc > 0x7f) {
      return -1;
    }
    if (rejectInputStream(stream)) return -1;
    return ungetc(wc, stream);
  }

  function fwide(stream, mode) {
    if (rejectKnownStream(stream)) return 0;
    mode = Number(mode) | 0;
    if (mode > 0) return 1;
    if (mode < 0) return -1;
    return 0;
  }

  function feof(stream) {
    if (rejectKnownStream(stream)) return 0;
    if (!isInputStream(stream)) return 0;
    return stdinEof ? 1 : 0;
  }

  function ferror(stream) {
    if (rejectKnownStream(stream)) return 1;
    const s = Number(stream);
    if (stream === undefined || s === 0) return stdinError;
    return 0;
  }

  function clearerr(stream) {
    if (rejectKnownStream(stream)) return;
    const s = Number(stream);
    if (stream === undefined || s === 0) {
      stdinEof = false;
      stdinError = 0;
    }
  }

  function perror(s) {
    const prefix = readCString(getMemory(), s);
    const message = strerrorMessage(getErrno());
    const text = prefix ? `${prefix}: ${message}\n` : `${message}\n`;
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
    fopen,
    fclose,
    putchar,
    fputc,
    fflush,
    ungetc,
    fread,
    fwrite,
    write,
    lseek,
    fgetc,
    fgets,
    fgetwc,
    fgetws,
    fputwc,
    fputws,
    ungetwc,
    fwide,
    feof,
    ferror,
    clearerr,
    perror,
    __error,
    runtimeStdoutWrite,
    runtimeStderrWrite,
  };
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
    ungetc: stdio.ungetc,
    __agc_runtime_stdout_write: stdio.runtimeStdoutWrite,
    __agc_runtime_stderr_write: stdio.runtimeStderrWrite,
    fopen: stdio.fopen,
    fclose: stdio.fclose,
    fread: stdio.fread,
    fwrite: stdio.fwrite,
    write: stdio.write,
    lseek: stdio.lseek,
    fgetc: stdio.fgetc,
    getc: stdio.fgetc,
    getchar: stdio.fgetc,
    fgets: stdio.fgets,
    fgetwc: stdio.fgetwc,
    getwc: stdio.fgetwc,
    getwchar: stdio.fgetwc,
    fputwc: stdio.fputwc,
    putwc: stdio.fputwc,
    putwchar: (wc) => stdio.fputwc(wc, 1),
    ungetwc: stdio.ungetwc,
    fgetws: stdio.fgetws,
    fputws: stdio.fputws,
    fwide: stdio.fwide,
    feof: stdio.feof,
    ferror: stdio.ferror,
    clearerr: stdio.clearerr,
    perror: stdio.perror,
    __error: stdio.__error,
  };
}

export function createAgcRuntimeMathEnvImports(options = {}) {
  const getMemory = options.getMemory || defaultGetMemory;
  const env = {};
  for (const def of AGC_MATH_IMPORTS) addMathImportFamily(env, ...def);
  env.frexp = (x, expPtr) => agcFrexp(getMemory(), x, expPtr);
  env.frexpf = (x, expPtr) => agcFrexp(getMemory(), x, expPtr);
  env.frexpl = (x, expPtr) => agcFrexp(getMemory(), x, expPtr);
  env.ldexp = agcLdexp;
  env.ldexpf = agcLdexp;
  env.ldexpl = agcLdexp;
  env.modf = (x, intPtr) => agcModf(getMemory(), x, intPtr, false);
  env.modff = (x, intPtr) => agcModf(getMemory(), x, intPtr, true);
  env.modfl = (x, intPtr) => agcModf(getMemory(), x, intPtr, false);
  env.remquo = (x, y, quoPtr) => agcRemquo(getMemory(), x, y, quoPtr);
  env.remquof = env.remquo;
  env.remquol = env.remquo;
  env.__agc_runtime_math_remquo = env.remquo;
  env.copysign = (x, y) => (agcSignbit(y) ? -Math.abs(Number(x)) : Math.abs(Number(x)));
  env.copysignf = env.copysign;
  env.copysignl = env.copysign;
  env.nan = () => Number.NaN;
  env.nanf = env.nan;
  env.nanl = env.nan;
  env.fpclassify = agcFpclassify;
  env.isfinite = (x) => Number.isFinite(Number(x)) ? 1 : 0;
  env.isinf = (x) => !Number.isNaN(Number(x)) && !Number.isFinite(Number(x)) ? 1 : 0;
  env.isnan = (x) => Number.isNaN(Number(x)) ? 1 : 0;
  env.isnormal = (x) => agcFpclassify(x) === 4 ? 1 : 0;
  env.signbit = agcSignbit;
  env.isgreater = (x, y) => !agcIsunordered(x, y) && Number(x) > Number(y) ? 1 : 0;
  env.isgreaterequal = (x, y) => !agcIsunordered(x, y) && Number(x) >= Number(y) ? 1 : 0;
  env.isless = (x, y) => !agcIsunordered(x, y) && Number(x) < Number(y) ? 1 : 0;
  env.islessequal = (x, y) => !agcIsunordered(x, y) && Number(x) <= Number(y) ? 1 : 0;
  env.islessgreater = (x, y) => !agcIsunordered(x, y) && Number(x) !== Number(y) ? 1 : 0;
  env.isunordered = agcIsunordered;
  return env;
}

export function createAgcRuntimeImports(imports = {}) {
  const { stdio, onStdout, onStderr, ...wasmImports } = imports;
  const stdioOptions = stdio || {};
  return {
    ...wasmImports,
    env: {
      ...createAgcRuntimeMathEnvImports({ getMemory: stdioOptions.getMemory }),
      ...createAgcRuntimeStdioEnvImports({ ...stdioOptions, onStdout, onStderr }),
      ...(imports.env || {}),
    },
  };
}
