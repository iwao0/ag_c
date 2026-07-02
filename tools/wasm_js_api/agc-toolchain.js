import { createCompiler } from "./agc-wasm.js?v=runtime-object";
import { createAgcRuntimeImports } from "./agc-runtime-imports.js?v=runtime-object";
import { createLinker } from "../wasm_obj_linker/ag-wasm-link.js?v=runtime-object";

const utf8Decoder = new TextDecoder();

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
    const wasm = compileLinkedWasm(sources, linkOptions);
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
