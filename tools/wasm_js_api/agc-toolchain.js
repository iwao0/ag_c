import { createCompiler } from "./agc-wasm.js";
import { createLinker } from "../wasm_obj_linker/ag-wasm-link.js";

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
  const linker = await createLinker(options.linkerWasm);

  function compileLinkedWasm(sources, linkOptions = {}) {
    const objects = normalizeSources(sources).map((source) => compiler.compileObject(source));
    return linker.link(objects, linkOptions);
  }

  return {
    compiler,
    linker,
    compileWat: (source) => compiler.compileWat(source),
    compileObject: (source) => compiler.compileObject(source),
    compileLinkedWasm,
  };
}

export default createToolchain;
