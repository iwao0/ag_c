import { createCompiler } from "./agc-wasm.js?v=stdio-imports";
import { createAgcRuntimeImports } from "./agc-runtime-imports.js?v=stdio-imports";
import { createLinker } from "../wasm_obj_linker/ag-wasm-link.js?v=stdio-imports";

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
    return { wasm, module: result.module, instance: result.instance };
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
