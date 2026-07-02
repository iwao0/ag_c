function asBytes(input, label) {
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  throw new TypeError(`${label} must be an ArrayBuffer or Uint8Array`);
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
  return (await WebAssembly.instantiate(asBytes(wasmSource, "wasm"), {})).instance;
}

function callMalloc(malloc, size) {
  return Number(malloc(BigInt(size || 1)));
}

function callFree(free, ptr) {
  free(ptr);
}

export async function createLinker(wasmSource) {
  const instance = await instantiateFromSource(wasmSource);
  const memory = instance.exports.memory;
  const malloc = instance.exports.malloc;
  const free = instance.exports.free;
  const linkObjects = instance.exports.agc_wasm_link_objects;
  if (!(memory instanceof WebAssembly.Memory)) {
    throw new Error("ag_wasm_link wasm module does not export memory");
  }
  if (typeof malloc !== "function" || typeof free !== "function") {
    throw new Error("ag_wasm_link wasm module must export malloc/free");
  }
  if (typeof linkObjects !== "function") {
    throw new Error("ag_wasm_link wasm module does not export agc_wasm_link_objects");
  }

  let u8 = new Uint8Array(memory.buffer);
  let view = new DataView(memory.buffer);
  function refresh() {
    u8 = new Uint8Array(memory.buffer);
    view = new DataView(memory.buffer);
  }
  function setU64(addr, value) {
    view.setBigUint64(addr, BigInt(value), true);
  }
  function getU64(addr) {
    return Number(view.getBigUint64(addr, true));
  }
  function allocBytes(bytes) {
    const ptr = callMalloc(malloc, bytes.length || 1);
    if (!ptr) throw new Error("ag_wasm_link malloc failed");
    refresh();
    u8.set(bytes, ptr);
    return ptr;
  }
  function allocCString(s) {
    return allocBytes(new TextEncoder().encode(`${s}\0`));
  }

  function link(objects, options = {}) {
    if (!Array.isArray(objects) || objects.length === 0) {
      throw new TypeError("objects must be a non-empty array");
    }
    const exportNames = options.exports ?? ["main"];
    if (!Array.isArray(exportNames)) throw new TypeError("exports must be an array");
    const useStdlib = options.useStdlib ?? true;
    const allocations = [];
    let linkedPtr = 0;
    try {
      const objectBytes = objects.map((obj, i) => asBytes(obj, `objects[${i}]`));
      const descPtr = callMalloc(malloc, objectBytes.length * 16);
      allocations.push(descPtr);
      refresh();
      for (let i = 0; i < objectBytes.length; i++) {
        const ptr = allocBytes(objectBytes[i]);
        allocations.push(ptr);
        setU64(descPtr + i * 16, ptr);
        setU64(descPtr + i * 16 + 8, objectBytes[i].length);
      }

      const exportPtrs = exportNames.map((name) => {
        if (typeof name !== "string" || name.length === 0) {
          throw new TypeError("export names must be non-empty strings");
        }
        const ptr = allocCString(name);
        allocations.push(ptr);
        return ptr;
      });
      const exportsPtr = callMalloc(malloc, Math.max(1, exportPtrs.length) * 8);
      allocations.push(exportsPtr);
      refresh();
      for (let i = 0; i < exportPtrs.length; i++) setU64(exportsPtr + i * 8, exportPtrs[i]);

      const outLenPtr = callMalloc(malloc, 8);
      allocations.push(outLenPtr);
      refresh();
      setU64(outLenPtr, 0);

      linkedPtr = Number(linkObjects(
        BigInt(descPtr),
        BigInt(objectBytes.length),
        BigInt(exportsPtr),
        BigInt(exportPtrs.length),
        BigInt(useStdlib ? 1 : 0),
        BigInt(outLenPtr),
      ));
      refresh();
      const linkedLen = getU64(outLenPtr);
      if (!linkedPtr || linkedLen < 8) throw new Error("ag_wasm_link failed");
      return new Uint8Array(u8.slice(linkedPtr, linkedPtr + linkedLen));
    } finally {
      if (linkedPtr) callFree(free, linkedPtr);
      for (let i = allocations.length - 1; i >= 0; i--) callFree(free, allocations[i]);
    }
  }

  return { instance, memory, link };
}

export default createLinker;
