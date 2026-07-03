import { createCompiler } from "./agc-wasm.js?v=runtime-object";
import { createAgcRuntimeImports } from "./agc-runtime-imports.js?v=runtime-object";
import { createLinker } from "../wasm_obj_linker/ag-wasm-link.js?v=runtime-object";

const utf8Decoder = new TextDecoder();
const utf8Encoder = new TextEncoder();

async function loadBytes(source, label) {
  if (source === undefined || source === null) return null;
  if (source instanceof Uint8Array) return source;
  if (source instanceof ArrayBuffer) return new Uint8Array(source);
  if (typeof source === "string" || source instanceof URL) {
    if (typeof fetch !== "function") {
      throw new TypeError(`${label} URL loading requires fetch`);
    }
    const response = await fetch(source);
    if (!response.ok) throw new Error(`failed to fetch ${label}: ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
  }
  throw new TypeError(`${label} must be a URL, ArrayBuffer, or Uint8Array`);
}

function normalizeSources(sources) {
  if (typeof sources === "string") return [sources];
  if (!Array.isArray(sources) || sources.length === 0) {
    throw new TypeError("sources must be a string or a non-empty string array");
  }
  for (const [i, source] of sources.entries()) {
    if (typeof source !== "string") {
      throw new TypeError(`sources[${i}] must be a string`);
    }
  }
  return sources;
}

function hasOwn(obj, name) {
  return Object.prototype.hasOwnProperty.call(obj, name);
}

function normalizeStdinBytes(input) {
  if (typeof input === "string") return utf8Encoder.encode(input);
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  throw new TypeError("stdio.stdin must be a string, ArrayBuffer, or Uint8Array");
}

function mergeExports(options, names) {
  const exportNames = options.exports ?? ["main"];
  if (!Array.isArray(exportNames)) throw new TypeError("exports must be an array");
  const merged = exportNames.slice();
  for (const name of names) {
    if (!merged.includes(name)) merged.push(name);
  }
  return { ...options, exports: merged };
}

function callRuntimeNumber(fn, ...args) {
  try {
    return Number(fn(...args.map((arg) => BigInt(arg))));
  } catch (err) {
    if (err instanceof TypeError) {
      return Number(fn(...args.map(Number)));
    }
    throw err;
  }
}

function callRuntimeVoid(fn, ...args) {
  try {
    fn(...args.map((arg) => BigInt(arg)));
  } catch (err) {
    if (err instanceof TypeError) {
      fn(...args.map(Number));
      return;
    }
    throw err;
  }
}

export async function createToolchain(options) {
  if (!options || options.compilerWasm === undefined || options.linkerWasm === undefined) {
    throw new TypeError("createToolchain requires compilerWasm and linkerWasm");
  }
  const compiler = await createCompiler(options.compilerWasm, options.compilerOptions);
  const linker = await createLinker(options.linkerWasm, options.linkerOptions);
  const runtimeObject = await loadBytes(options.runtimeObject, "runtimeObject");

  function compileLinkedWasm(sources, linkOptions = {}) {
    const objects = normalizeSources(sources).map((source, i) => {
      try {
        return compiler.compileObject(source);
      } catch (err) {
        err.sourceIndex = i;
        err.message = `source ${i + 1}: ${err.message}`;
        throw err;
      }
    });
    if ((linkOptions.useStdlib ?? true) && runtimeObject) {
      objects.push(runtimeObject);
    }
    return linker.link(objects, linkOptions);
  }

  async function instantiateLinkedWasm(sources, linkOptions = {}, imports = {}) {
    const stdioOptions = imports.stdio || {};
    const shouldInjectRuntimeStdin =
      hasOwn(stdioOptions, "stdin") && (linkOptions.useStdlib ?? true) && runtimeObject;
    const stdinBytes = shouldInjectRuntimeStdin ? normalizeStdinBytes(stdioOptions.stdin) : null;
    const effectiveLinkOptions = shouldInjectRuntimeStdin ? mergeExports(linkOptions, [
      "__agc_runtime_malloc",
      "__agc_runtime_free",
      "__agc_runtime_stdin_capacity",
      "__agc_runtime_stdin_write",
    ]) : linkOptions;
    const wasm = compileLinkedWasm(sources, effectiveLinkOptions);
    let memory = null;
    const runtimeImports = createAgcRuntimeImports({
      ...imports,
      stdio: {
        ...(imports.stdio || {}),
        getMemory: () => memory,
      },
    });
    const result = await WebAssembly.instantiate(wasm, runtimeImports);
    memory = result.instance.exports.memory;
    if (stdinBytes) {
      const capacityFn = result.instance.exports.__agc_runtime_stdin_capacity;
      const writeFn = result.instance.exports.__agc_runtime_stdin_write;
      const malloc = result.instance.exports.__agc_runtime_malloc;
      const free = result.instance.exports.__agc_runtime_free;
      if (!(memory instanceof WebAssembly.Memory) ||
          typeof capacityFn !== "function" ||
          typeof writeFn !== "function" ||
          typeof malloc !== "function" ||
          typeof free !== "function") {
        throw new Error("linked runtime does not export stdin injection helpers");
      }
      const capacity = callRuntimeNumber(capacityFn);
      if (stdinBytes.length > capacity) {
        throw new RangeError(`stdio.stdin is ${stdinBytes.length} bytes; runtime capacity is ${capacity} bytes`);
      }
      const ptr = callRuntimeNumber(malloc, Math.max(1, stdinBytes.length));
      if (!ptr || ptr + stdinBytes.length > memory.buffer.byteLength) {
        throw new Error("linked runtime malloc failed for stdin");
      }
      try {
        new Uint8Array(memory.buffer, ptr, stdinBytes.length).set(stdinBytes);
        const written = callRuntimeNumber(writeFn, ptr, stdinBytes.length);
        if (written !== stdinBytes.length) {
          throw new Error(`linked runtime accepted ${written} of ${stdinBytes.length} stdin bytes`);
        }
      } finally {
        callRuntimeVoid(free, ptr);
      }
    }
    function readExportBuffer(ptrName, lenName) {
      const ptrFn = result.instance.exports[ptrName];
      const lenFn = result.instance.exports[lenName];
      if (!(memory instanceof WebAssembly.Memory) ||
          typeof ptrFn !== "function" ||
          typeof lenFn !== "function") {
        return "";
      }
      const ptr = Number(ptrFn());
      const len = Number(lenFn());
      if (ptr <= 0 || len <= 0 || ptr + len > memory.buffer.byteLength) return "";
      return utf8Decoder.decode(new Uint8Array(memory.buffer, ptr, len));
    }
    return {
      wasm,
      module: result.module,
      instance: result.instance,
      readStdout: () => readExportBuffer("__agc_runtime_stdout_ptr", "__agc_runtime_stdout_len"),
      readStderr: () => readExportBuffer("__agc_runtime_stderr_ptr", "__agc_runtime_stderr_len"),
    };
  }

  return {
    compiler,
    linker,
    compileWat: (source) => compiler.compileWat(source),
    compileObject: (source) => compiler.compileObject(source),
    compileLinkedWasm,
    instantiateLinkedWasm,
  };
}

export default createToolchain;
