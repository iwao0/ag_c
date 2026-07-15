import { createAgcRuntimeImports } from "../wasm_js_api/agc-runtime-imports.js?v=runtime-object";

function asBytes(input, label) {
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  throw new TypeError(`${label} must be an ArrayBuffer or Uint8Array`);
}

async function instantiateFromSource(wasmSource, imports = {}) {
  if (wasmSource instanceof WebAssembly.Module) {
    return await WebAssembly.instantiate(wasmSource, imports);
  }
  if (typeof wasmSource === "string" || wasmSource instanceof URL) {
    if (typeof WebAssembly.instantiateStreaming === "function" && typeof fetch === "function") {
      try {
        const result = await WebAssembly.instantiateStreaming(fetch(wasmSource), imports);
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
    return (await WebAssembly.instantiate(bytes, imports)).instance;
  }
  return (await WebAssembly.instantiate(asBytes(wasmSource, "wasm"), imports)).instance;
}

function callMalloc(malloc, size) {
  try {
    return Number(malloc(BigInt(size || 1)));
  } catch (err) {
    if (err instanceof TypeError) {
      return Number(malloc(size || 1));
    }
    throw err;
  }
}

function callFree(free, ptr) {
  try {
    free(BigInt(ptr));
  } catch (err) {
    if (err instanceof TypeError) {
      free(ptr);
      return;
    }
    throw err;
  }
}

function callLinkObjects(fn, descPtr, objectCount, exportsPtr, exportCount, useStdlib, outLenPtr) {
  try {
    return Number(fn(
      BigInt(descPtr),
      BigInt(objectCount),
      BigInt(exportsPtr),
      BigInt(exportCount),
      BigInt(useStdlib ? 1 : 0),
      BigInt(outLenPtr),
    ));
  } catch (err) {
    if (err instanceof TypeError) {
      return Number(fn(descPtr, objectCount, exportsPtr, exportCount, useStdlib ? 1 : 0, outLenPtr));
    }
    throw err;
  }
}

function callLinkObjectsWithOptions(fn, descPtr, objectCount, exportsPtr, exportCount,
                                    useStdlib, optionsPtr, outLenPtr) {
  return Number(fn(
    BigInt(descPtr),
    objectCount,
    BigInt(exportsPtr),
    exportCount,
    useStdlib ? 1 : 0,
    BigInt(optionsPtr),
    BigInt(outLenPtr),
  ));
}

function callLinkObjectsWithResourceLimits(fn, descPtr, objectCount, exportsPtr, exportCount,
                                           useStdlib, optionsPtr, maxOutputBytes, outLenPtr) {
  return Number(fn(
    BigInt(descPtr),
    objectCount,
    BigInt(exportsPtr),
    exportCount,
    useStdlib ? 1 : 0,
    BigInt(optionsPtr),
    BigInt(maxOutputBytes),
    BigInt(outLenPtr),
  ));
}

function callLinkObjectsWithExportSignatures(fn, descPtr, objectCount, exportsPtr, exportCount,
                                             useStdlib, optionsPtr, maxOutputBytes, outLenPtr) {
  return Number(fn(
    BigInt(descPtr),
    objectCount,
    BigInt(exportsPtr),
    exportCount,
    useStdlib ? 1 : 0,
    BigInt(optionsPtr),
    BigInt(maxOutputBytes),
    BigInt(outLenPtr),
  ));
}

function asU32Option(value, fallback, name) {
  const resolved = value ?? fallback;
  if (!Number.isInteger(resolved) || resolved < 0 || resolved > 0xffffffff) {
    throw new RangeError(`${name} must be an unsigned 32-bit integer`);
  }
  return resolved;
}

function ensureMemoryRange(memory, ptr, len, label) {
  if (ptr < 0 || len < 0 || ptr + len > memory.buffer.byteLength) {
    throw new RangeError(`${label} buffer is outside wasm memory`);
  }
}

export async function createLinker(wasmSource, options = {}) {
  const decoder = new TextDecoder();
  let callbackMemory = null;
  const stdoutChunks = [];
  const stderrChunks = [];
  let terminationNotified = false;
  function readCallbackBytes(ptr, len) {
    const n = Number(len);
    const p = Number(ptr);
    if (!callbackMemory || p <= 0 || n <= 0 || p + n > callbackMemory.buffer.byteLength) return "";
    return decoder.decode(new Uint8Array(callbackMemory.buffer, p, n));
  }
  const envImports = {
    __agc_host_write(stream, ptr, len) {
      const n = Number(len);
      if (n === 0) return 0;
      const text = readCallbackBytes(ptr, len);
      if (!text || (Number(stream) !== 1 && Number(stream) !== 2)) return -1;
      if (Number(stream) === 2) {
        if (typeof options.onStderr === "function") options.onStderr(text);
        else stderrChunks.push(text);
      } else if (typeof options.onStdout === "function") {
        options.onStdout(text);
      } else {
        stdoutChunks.push(text);
      }
      return n;
    },
    __agc_runtime_stdout_write(ptr, len) {
      const text = readCallbackBytes(ptr, len);
      if (!text) return;
      if (typeof options.onStdout === "function") {
        options.onStdout(text);
      } else {
        stdoutChunks.push(text);
      }
    },
    __agc_runtime_stderr_write(ptr, len) {
      const text = readCallbackBytes(ptr, len);
      if (!text) return;
      if (typeof options.onStderr === "function") {
        options.onStderr(text);
      } else {
        stderrChunks.push(text);
      }
    },
  };
  const imports = createAgcRuntimeImports({ env: envImports });
  const instance = await instantiateFromSource(wasmSource, imports);
  const memory = instance.exports.memory;
  callbackMemory = memory;
  const malloc = instance.exports.malloc;
  const free = instance.exports.free;
  const linkObjects = instance.exports.agc_wasm_link_objects;
  const linkObjectsWithOptions = instance.exports.agc_wasm_link_objects_with_options;
  const linkObjectsWithResourceLimits = instance.exports.agc_wasm_link_objects_with_resource_limits;
  const linkObjectsWithExportSignatures =
    instance.exports.agc_wasm_link_objects_with_export_signatures;
  const stdoutPtrExport = instance.exports.__agc_runtime_stdout_ptr;
  const stdoutLenExport = instance.exports.__agc_runtime_stdout_len;
  const stderrPtrExport = instance.exports.__agc_runtime_stderr_ptr;
  const stderrLenExport = instance.exports.__agc_runtime_stderr_len;
  const stderrResetExport = instance.exports.__agc_runtime_stderr_reset;
  const terminationKindExport = instance.exports.__agc_runtime_termination_kind;
  const terminationStatusExport = instance.exports.__agc_runtime_termination_status;
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
  function setU32(addr, value) {
    view.setUint32(addr, value, true);
  }
  function getU64(addr) {
    return Number(view.getBigUint64(addr, true));
  }
  function allocBytes(bytes) {
    const ptr = callMalloc(malloc, bytes.length || 1);
    if (!ptr) throw new Error("ag_wasm_link malloc failed");
    refresh();
    ensureMemoryRange(memory, ptr, bytes.length, "allocated");
    u8.set(bytes, ptr);
    return ptr;
  }
  function allocCString(s) {
    return allocBytes(new TextEncoder().encode(`${s}\0`));
  }

  function callNoArgNumberFunc(fn) {
    return Number(fn());
  }

  function callNoArgVoidFunc(fn) {
    fn();
  }

  function resetDiagnostics() {
    stdoutChunks.length = 0;
    stderrChunks.length = 0;
    terminationNotified = false;
    if (typeof stderrResetExport === "function") callNoArgVoidFunc(stderrResetExport);
  }

  function readFallbackBuffer(ptrExport, lenExport) {
    if (typeof ptrExport !== "function" || typeof lenExport !== "function") return "";
    const ptr = callNoArgNumberFunc(ptrExport);
    const len = callNoArgNumberFunc(lenExport);
    if (ptr <= 0 || len <= 0 || ptr + len > memory.buffer.byteLength) return "";
    return decoder.decode(new Uint8Array(memory.buffer, ptr, len));
  }

  function readStdout() {
    return stdoutChunks.join("") || readFallbackBuffer(stdoutPtrExport, stdoutLenExport);
  }

  function readStderr() {
    return (stderrChunks.join("") || readFallbackBuffer(stderrPtrExport, stderrLenExport)).trim();
  }

  function readTermination() {
    if (typeof terminationKindExport !== "function" || typeof terminationStatusExport !== "function") {
      return null;
    }
    const kindNo = callNoArgNumberFunc(terminationKindExport);
    if (kindNo === 0) return null;
    return {
      kind: kindNo === 1 ? "exit" : kindNo === 2 ? "abort" : "unknown",
      status: callNoArgNumberFunc(terminationStatusExport),
    };
  }

  function notifyTermination() {
    if (terminationNotified) return;
    const event = readTermination();
    if (!event) return;
    terminationNotified = true;
    if (typeof options.onTerminate === "function") options.onTerminate(event);
    if (event.kind === "exit" && typeof options.onExit === "function") options.onExit(event.status);
    if (event.kind === "abort" && typeof options.onAbort === "function") options.onAbort(event.status);
  }

  function throwLinkFailure(err, maxOutputBytes) {
    notifyTermination();
    const diag = readStderr();
    const termination = readTermination();
    if (diag.includes("AGC_LIMIT_MAX_LINKED_WASM_BYTES")) {
      const limitError = new RangeError(diag);
      limitError.name = "AgcResourceLimitError";
      limitError.code = "AGC_LIMIT_MAX_LINKED_WASM_BYTES";
      limitError.limit = "maxLinkedWasmBytes";
      limitError.max = maxOutputBytes;
      limitError.actual = maxOutputBytes + 1;
      throw limitError;
    }
    if (diag) throw new Error(diag);
    if (termination && termination.kind === "exit") {
      throw new Error(`ag_wasm_link exited with status ${termination.status}`);
    }
    if (termination && termination.kind === "abort") throw new Error("ag_wasm_link aborted");
    if (err) throw err;
    throw new Error("ag_wasm_link failed");
  }

  function link(objects, options = {}) {
    resetDiagnostics();
    if (!Array.isArray(objects) || objects.length === 0) {
      throw new TypeError("objects must be a non-empty array");
    }
    const exportEntries = options.exports ?? ["main"];
    if (!Array.isArray(exportEntries)) throw new TypeError("exports must be an array");
    const exportSpecs = exportEntries.map((entry, index) => {
      if (typeof entry === "string") {
        if (entry.length === 0 || entry.includes("\0")) {
          throw new TypeError(`exports[${index}] must be a non-empty string without NUL`);
        }
        return { name: entry, signature: null };
      }
      if (!entry || typeof entry !== "object" || Array.isArray(entry) ||
          typeof entry.name !== "string" || entry.name.length === 0 || entry.name.includes("\0") ||
          typeof entry.signature !== "string" || entry.signature.length === 0 ||
          entry.signature.includes("\0")) {
        throw new TypeError(
          `exports[${index}] must be a name string or { name, signature } with non-empty strings without NUL`,
        );
      }
      return { name: entry.name, signature: entry.signature };
    });
    const hasSignedExports = exportSpecs.some((entry) => entry.signature !== null);
    const useStdlib = options.useStdlib ?? true;
    const maxOutputBytes = options.maxOutputBytes;
    if (maxOutputBytes !== undefined &&
        (!Number.isSafeInteger(maxOutputBytes) || maxOutputBytes <= 0 || maxOutputBytes > 0x7fffffff)) {
      throw new RangeError("maxOutputBytes must be an integer from 1 to 2147483647");
    }
    if (maxOutputBytes !== undefined && typeof linkObjectsWithResourceLimits !== "function" &&
        typeof linkObjectsWithExportSignatures !== "function") {
      throw new Error("this ag_wasm_link module does not support linked Wasm byte limits");
    }
    const hasLayoutOptions = options.initialMemoryPages !== undefined ||
      options.maximumMemoryPages !== undefined || options.stackSize !== undefined ||
      options.maximumTableElements !== undefined || options.stdio !== undefined;
    if (hasLayoutOptions && typeof linkObjectsWithOptions !== "function" &&
        typeof linkObjectsWithExportSignatures !== "function") {
      throw new Error("this ag_wasm_link module does not support memory/table layout options");
    }
    if (hasSignedExports && typeof linkObjectsWithExportSignatures !== "function") {
      throw new Error("this ag_wasm_link module does not support C export signatures");
    }
    const allocations = [];
    let linkedPtr = 0;
    try {
      const objectBytes = objects.map((obj, i) => asBytes(obj, `objects[${i}]`));
      const descPtr = callMalloc(malloc, objectBytes.length * 16);
      if (!descPtr) throw new Error("ag_wasm_link malloc failed for object descriptors");
      allocations.push(descPtr);
      refresh();
      ensureMemoryRange(memory, descPtr, objectBytes.length * 16, "object descriptor");
      for (let i = 0; i < objectBytes.length; i++) {
        const ptr = allocBytes(objectBytes[i]);
        allocations.push(ptr);
        setU64(descPtr + i * 16, ptr);
        setU64(descPtr + i * 16 + 8, objectBytes[i].length);
      }

      const allocatedExports = exportSpecs.map(({ name, signature }) => {
        const namePtr = allocCString(name);
        allocations.push(namePtr);
        let signaturePtr = 0;
        if (signature !== null) {
          signaturePtr = allocCString(signature);
          allocations.push(signaturePtr);
        }
        return { namePtr, signaturePtr };
      });
      const exportStride = hasSignedExports ? 16 : 8;
      const exportsPtr = callMalloc(malloc, Math.max(1, allocatedExports.length) * exportStride);
      if (!exportsPtr) throw new Error("ag_wasm_link malloc failed for export names");
      allocations.push(exportsPtr);
      refresh();
      ensureMemoryRange(
        memory, exportsPtr, Math.max(1, allocatedExports.length) * exportStride, "exports",
      );
      for (let i = 0; i < allocatedExports.length; i++) {
        setU64(exportsPtr + i * exportStride, allocatedExports[i].namePtr);
        if (hasSignedExports) {
          setU64(exportsPtr + i * exportStride + 8, allocatedExports[i].signaturePtr);
        }
      }

      const outLenPtr = callMalloc(malloc, 8);
      if (!outLenPtr) throw new Error("ag_wasm_link malloc failed for output length");
      allocations.push(outLenPtr);
      refresh();
      ensureMemoryRange(memory, outLenPtr, 8, "output length");
      setU64(outLenPtr, 0);

      let linkerOptionsPtr = 0;
      if (typeof linkObjectsWithOptions === "function" ||
          typeof linkObjectsWithExportSignatures === "function") {
        const initialMemoryPages = asU32Option(options.initialMemoryPages, 1024,
                                               "initialMemoryPages");
        const maximumMemoryPages = options.maximumMemoryPages === undefined
          ? 0 : asU32Option(options.maximumMemoryPages, 0, "maximumMemoryPages");
        const stackSize = asU32Option(options.stackSize, 0, "stackSize");
        const maximumTableElements = options.maximumTableElements === undefined
          ? 0 : asU32Option(options.maximumTableElements, 0, "maximumTableElements");
        const stdio = options.stdio;
        if (stdio !== undefined &&
            (!stdio || typeof stdio !== "object" || Array.isArray(stdio))) {
          throw new TypeError("stdio must be an object");
        }
        const writeImportModule = stdio?.writeImportModule ?? "env";
        const writeImportName = stdio?.writeImportName ?? "__agc_host_write";
        if (stdio !== undefined &&
            (typeof writeImportModule !== "string" || writeImportModule.length === 0 ||
             writeImportModule.includes("\0") || typeof writeImportName !== "string" ||
             writeImportName.length === 0 || writeImportName.includes("\0"))) {
          throw new TypeError("stdio write import module/name must be non-empty strings without NUL");
        }
        let flags = 0;
        if (options.maximumMemoryPages !== undefined) flags |= 1;
        if (options.maximumTableElements !== undefined) flags |= 2;
        if (stdio !== undefined) flags |= 4;
        let writeImportModulePtr = 0;
        let writeImportNamePtr = 0;
        if (stdio !== undefined) {
          writeImportModulePtr = allocCString(writeImportModule);
          writeImportNamePtr = allocCString(writeImportName);
          allocations.push(writeImportModulePtr, writeImportNamePtr);
        }
        linkerOptionsPtr = callMalloc(malloc, 40);
        if (!linkerOptionsPtr) throw new Error("ag_wasm_link malloc failed for linker options");
        allocations.push(linkerOptionsPtr);
        refresh();
        ensureMemoryRange(memory, linkerOptionsPtr, 40, "linker options");
        setU32(linkerOptionsPtr, flags);
        setU32(linkerOptionsPtr + 4, initialMemoryPages);
        setU32(linkerOptionsPtr + 8, maximumMemoryPages);
        setU32(linkerOptionsPtr + 12, stackSize);
        setU32(linkerOptionsPtr + 16, maximumTableElements);
        setU32(linkerOptionsPtr + 20, 0);
        setU64(linkerOptionsPtr + 24, writeImportModulePtr);
        setU64(linkerOptionsPtr + 32, writeImportNamePtr);
      }

      try {
        if (hasSignedExports) {
          linkedPtr = callLinkObjectsWithExportSignatures(
            linkObjectsWithExportSignatures, descPtr, objectBytes.length,
            exportsPtr, allocatedExports.length, useStdlib, linkerOptionsPtr,
            maxOutputBytes ?? 0, outLenPtr,
          );
        } else if (maxOutputBytes !== undefined) {
          linkedPtr = callLinkObjectsWithResourceLimits(
            linkObjectsWithResourceLimits, descPtr, objectBytes.length,
            exportsPtr, allocatedExports.length, useStdlib, linkerOptionsPtr,
            maxOutputBytes, outLenPtr,
          );
        } else if (typeof linkObjectsWithOptions === "function") {
          linkedPtr = callLinkObjectsWithOptions(
            linkObjectsWithOptions, descPtr, objectBytes.length,
            exportsPtr, allocatedExports.length, useStdlib, linkerOptionsPtr, outLenPtr,
          );
        } else {
          linkedPtr = callLinkObjects(
            linkObjects, descPtr, objectBytes.length, exportsPtr,
            allocatedExports.length, useStdlib, outLenPtr,
          );
        }
      } catch (err) {
        throwLinkFailure(err, maxOutputBytes);
      }
      refresh();
      const linkedLen = getU64(outLenPtr);
      if (!linkedPtr || linkedLen < 8) {
        throwLinkFailure(undefined, maxOutputBytes);
      }
      ensureMemoryRange(memory, linkedPtr, linkedLen, "linked wasm");
      return new Uint8Array(u8.slice(linkedPtr, linkedPtr + linkedLen));
    } finally {
      if (linkedPtr) callFree(free, linkedPtr);
      for (let i = allocations.length - 1; i >= 0; i--) callFree(free, allocations[i]);
    }
  }

  return {
    instance,
    memory,
    link,
    readStdout,
    readStderr,
    readTermination,
  };
}

export default createLinker;
