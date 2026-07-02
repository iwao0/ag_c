const DEFAULT_SOURCE_PTR = 393216;
const DEFAULT_SOURCE_CAP = 32768;
const DEFAULT_OUTPUT_PTR = DEFAULT_SOURCE_PTR + DEFAULT_SOURCE_CAP;
const DEFAULT_OUTPUT_CAP = 98304;

function asBytes(input) {
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  throw new TypeError("wasm must be an ArrayBuffer or Uint8Array");
}

async function instantiateFromSource(wasmSource) {
  if (typeof wasmSource === "string" || wasmSource instanceof URL) {
    if (typeof WebAssembly.instantiateStreaming === "function" && typeof fetch === "function") {
      try {
        const result = await WebAssembly.instantiateStreaming(fetch(wasmSource), {});
        return result.instance;
      } catch (_) {
        // Fall back for file servers that do not send application/wasm.
      }
    }
    if (typeof fetch !== "function") {
      throw new TypeError("URL wasm loading requires fetch; pass an ArrayBuffer in this environment");
    }
    const response = await fetch(wasmSource);
    if (!response.ok) throw new Error(`failed to fetch wasm: ${response.status}`);
    const bytes = await response.arrayBuffer();
    return (await WebAssembly.instantiate(bytes, {})).instance;
  }
  return (await WebAssembly.instantiate(asBytes(wasmSource), {})).instance;
}

function callCompile(instance, sourcePtr, outputPtr, outputCap) {
  const fn = instance.exports.agc_wasm_compile_wat;
  try {
    return Number(fn(BigInt(sourcePtr), BigInt(outputPtr), BigInt(outputCap)));
  } catch (err) {
    if (err instanceof TypeError) {
      return Number(fn(sourcePtr, outputPtr, outputCap));
    }
    throw err;
  }
}

export async function createCompiler(wasmSource, options = {}) {
  const instance = await instantiateFromSource(wasmSource);
  const memory = instance.exports.memory;
  const compile = instance.exports.agc_wasm_compile_wat;
  if (!(memory instanceof WebAssembly.Memory)) {
    throw new Error("ag_c wasm module does not export memory");
  }
  if (typeof compile !== "function") {
    throw new Error("ag_c wasm module does not export agc_wasm_compile_wat");
  }

  const sourcePtr = options.sourcePtr ?? DEFAULT_SOURCE_PTR;
  const sourceCap = options.sourceCap ?? DEFAULT_SOURCE_CAP;
  const outputPtr = options.outputPtr ?? DEFAULT_OUTPUT_PTR;
  const outputCap = options.outputCap ?? DEFAULT_OUTPUT_CAP;
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();

  function compileWat(source) {
    const sourceBytes = encoder.encode(`${source}\0`);
    if (sourceBytes.length > sourceCap) {
      throw new RangeError(`source is ${sourceBytes.length} bytes; max is ${sourceCap}`);
    }
    if (sourcePtr + sourceCap > memory.buffer.byteLength) {
      throw new RangeError("configured source buffer is outside wasm memory");
    }
    if (outputPtr + outputCap > memory.buffer.byteLength) {
      throw new RangeError("configured output buffer is outside wasm memory");
    }

    let mem = new Uint8Array(memory.buffer);
    mem.fill(0, sourcePtr, sourcePtr + sourceCap);
    mem.fill(0, outputPtr, outputPtr + outputCap);
    mem.set(sourceBytes, sourcePtr);

    const n = callCompile(instance, sourcePtr, outputPtr, outputCap);
    if (n === -1) throw new Error("ag_c wasm compile failed: invalid buffer arguments");
    if (n === -2) throw new RangeError(`ag_c wasm output exceeded ${outputCap} bytes`);
    if (n === -3) throw new Error("ag_c wasm compile failed while building IR");
    if (n < 0) throw new Error(`ag_c wasm compile failed with code ${n}`);

    mem = new Uint8Array(memory.buffer);
    return decoder.decode(mem.subarray(outputPtr, outputPtr + n));
  }

  return {
    instance,
    memory,
    compileWat,
    limits: { sourcePtr, sourceCap, outputPtr, outputCap },
  };
}

export default createCompiler;
