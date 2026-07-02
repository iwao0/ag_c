import { createToolchain } from "./agc-toolchain.js";
import { inlineStandardIncludes } from "./agc-include-inline.js";

let toolchainPromise = null;

function getToolchain() {
  if (!toolchainPromise) {
    toolchainPromise = createToolchain({
      compilerWasm: "../../build/wasm_selfhost_api/ag_c_wasm_api.wasm",
      linkerWasm: "../../build/wasm_linker_selfhost/ag_wasm_link.wasm",
    });
  }
  return toolchainPromise;
}

function hexDump(bytes) {
  const lines = [];
  for (let off = 0; off < bytes.length; off += 16) {
    const chunk = bytes.subarray(off, off + 16);
    const hex = Array.from(chunk, (b) => b.toString(16).padStart(2, "0")).join(" ");
    lines.push(`${off.toString(16).padStart(8, "0")}  ${hex}`);
  }
  return lines.join("\n");
}

self.onmessage = async (ev) => {
  const { mode, sources } = ev.data;
  try {
    const toolchain = await getToolchain();
    const expandedSources = await Promise.all(sources.map((source) => inlineStandardIncludes(source)));
    if (mode === "object") {
      let obj;
      try {
        obj = toolchain.compileObject(expandedSources[0]);
      } catch (err) {
        err.message = `source 1: ${err.message}`;
        throw err;
      }
      self.postMessage({
        ok: true,
        status: "OK",
        output: hexDump(obj),
        download: { bytes: obj, name: "out.o", type: "application/wasm" },
      }, [obj.buffer]);
      return;
    }
    if (mode === "linked") {
      const linked = await toolchain.instantiateLinkedWasm(expandedSources, {
        exports: ["main"],
        useStdlib: false,
      });
      let status = "OK";
      if (typeof linked.instance.exports.main === "function") {
        status = `OK main()=${String(linked.instance.exports.main())}`;
      }
      self.postMessage({
        ok: true,
        status,
        output: hexDump(linked.wasm),
        download: { bytes: linked.wasm, name: "out.wasm", type: "application/wasm" },
      }, [linked.wasm.buffer]);
      return;
    }
    let wat;
    try {
      wat = toolchain.compileWat(expandedSources[0]);
    } catch (err) {
      err.message = `source 1: ${err.message}`;
      throw err;
    }
    self.postMessage({
      ok: true,
      status: "OK",
      output: wat,
      download: { text: wat, name: "out.wat", type: "text/plain" },
    });
  } catch (err) {
    self.postMessage({
      ok: false,
      error: err && err.message ? err.message : String(err),
    });
  }
};
