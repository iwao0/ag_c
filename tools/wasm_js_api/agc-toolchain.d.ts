import type {
  AgcWasmCompiler,
  AgcWasmCompilerOptions,
  AgcWasmSource,
} from "./agc-wasm.js";
import type {
  AgcWasmLinker,
  AgcWasmLinkOptions,
  AgcWasmLinkerSource,
} from "../wasm_obj_linker/ag-wasm-link.js";

export interface AgcWasmToolchainOptions {
  compilerWasm: AgcWasmSource;
  linkerWasm: AgcWasmLinkerSource;
  compilerOptions?: AgcWasmCompilerOptions;
}

export interface AgcWasmToolchain {
  compiler: AgcWasmCompiler;
  linker: AgcWasmLinker;
  compileWat(source: string): string;
  compileObject(source: string): Uint8Array;
  compileLinkedWasm(sources: string | string[], options?: AgcWasmLinkOptions): Uint8Array;
  instantiateLinkedWasm(
    sources: string | string[],
    options?: AgcWasmLinkOptions,
    imports?: WebAssembly.Imports,
  ): Promise<{
    wasm: Uint8Array;
    module: WebAssembly.Module;
    instance: WebAssembly.Instance;
  }>;
}

export function createToolchain(options: AgcWasmToolchainOptions): Promise<AgcWasmToolchain>;

export default createToolchain;
