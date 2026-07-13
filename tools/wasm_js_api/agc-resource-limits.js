export const DEFAULT_AGC_RESOURCE_LIMITS = Object.freeze({
  maxSources: 4096,
  maxSourceBytes: 0x7ffffffe,
  maxTotalSourceBytes: 0x7fffffff,
  maxHeaders: 128,
  maxHeaderBytes: 1024 * 1024,
  maxTotalHeaderBytes: 4 * 1024 * 1024,
  maxIncludeDepth: 32,
  maxObjectBytes: 16 * 1024 * 1024,
  maxLinkedWasmBytes: 0x7fffffff,
  maxDiagnostics: 128,
  maxDiagnosticBytes: 1024 * 1024,
});

const LIMIT_CODES = Object.freeze({
  maxSources: "AGC_LIMIT_MAX_SOURCES",
  maxSourceBytes: "AGC_LIMIT_MAX_SOURCE_BYTES",
  maxTotalSourceBytes: "AGC_LIMIT_MAX_TOTAL_SOURCE_BYTES",
  maxHeaders: "AGC_LIMIT_MAX_HEADERS",
  maxHeaderBytes: "AGC_LIMIT_MAX_HEADER_BYTES",
  maxTotalHeaderBytes: "AGC_LIMIT_MAX_TOTAL_HEADER_BYTES",
  maxIncludeDepth: "AGC_LIMIT_MAX_INCLUDE_DEPTH",
  maxObjectBytes: "AGC_LIMIT_MAX_OBJECT_BYTES",
  maxLinkedWasmBytes: "AGC_LIMIT_MAX_LINKED_WASM_BYTES",
  maxDiagnostics: "AGC_LIMIT_MAX_DIAGNOSTICS",
  maxDiagnosticBytes: "AGC_LIMIT_MAX_DIAGNOSTIC_BYTES",
});

function validateLimitObject(input, label) {
  if (!input || typeof input !== "object" || Array.isArray(input)) {
    throw new TypeError(`${label} must be an object`);
  }
  for (const name of Object.keys(input)) {
    if (!Object.prototype.hasOwnProperty.call(LIMIT_CODES, name)) {
      throw new TypeError(`${label} contains unknown limit ${JSON.stringify(name)}`);
    }
  }
}

export function resolveResourceLimits(base = DEFAULT_AGC_RESOURCE_LIMITS, overrides = {}) {
  validateLimitObject(base, "base limits");
  validateLimitObject(overrides, "limits");
  const resolved = {};
  for (const name of Object.keys(LIMIT_CODES)) {
    const value = overrides[name] ?? base[name] ?? DEFAULT_AGC_RESOURCE_LIMITS[name];
    if (!Number.isSafeInteger(value) || value <= 0 || value > 0x7fffffff) {
      throw new RangeError(`limits.${name} must be an integer from 1 to 2147483647`);
    }
    resolved[name] = value;
  }
  return Object.freeze(resolved);
}

export function utf8ByteLength(text) {
  let bytes = 0;
  for (let i = 0; i < text.length; i++) {
    const code = text.charCodeAt(i);
    if (code <= 0x7f) {
      bytes++;
    } else if (code <= 0x7ff) {
      bytes += 2;
    } else if (code >= 0xd800 && code <= 0xdbff &&
               i + 1 < text.length &&
               text.charCodeAt(i + 1) >= 0xdc00 && text.charCodeAt(i + 1) <= 0xdfff) {
      bytes += 4;
      i++;
    } else {
      bytes += 3;
    }
  }
  return bytes;
}

export class AgcResourceLimitError extends RangeError {
  constructor(limit, max, actual, diagnostics = Object.freeze([])) {
    const code = LIMIT_CODES[limit];
    if (!code) throw new TypeError(`unknown resource limit ${JSON.stringify(limit)}`);
    super(`${code}: ${limit} exceeded (${actual} > ${max})`);
    this.name = "AgcResourceLimitError";
    this.code = code;
    this.limit = limit;
    this.max = max;
    this.actual = actual;
    this.diagnostics = diagnostics;
  }
}

export function assertResourceLimit(limit, actual, max, diagnostics) {
  if (actual > max) throw new AgcResourceLimitError(limit, max, actual, diagnostics);
}
