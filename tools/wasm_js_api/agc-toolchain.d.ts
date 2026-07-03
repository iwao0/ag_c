import type {
  AgcWasmCompiler,
  AgcWasmCompilerOptions,
  AgcWasmSource,
} from "./agc-wasm.js";
import type {
  AgcWasmLinker,
  AgcWasmLinkOptions,
  AgcWasmLinkerOptions,
  AgcWasmLinkerSource,
} from "../wasm_obj_linker/ag-wasm-link.js";
import type { AgcRuntimeImports } from "./agc-runtime-imports.js";

export type AgcWasmObjectSource = string | URL | ArrayBuffer | Uint8Array;

export interface AgcWasmToolchainOptions {
  compilerWasm: AgcWasmSource;
  linkerWasm: AgcWasmLinkerSource;
  runtimeObject?: AgcWasmObjectSource;
  compilerOptions?: AgcWasmCompilerOptions;
  linkerOptions?: AgcWasmLinkerOptions;
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
    imports?: AgcRuntimeImports,
  ): Promise<{
    wasm: Uint8Array;
    module: WebAssembly.Module;
    instance: WebAssembly.Instance;
    readStdout(): string;
    readStderr(): string;
  }>;
}

export function createToolchain(options: AgcWasmToolchainOptions): Promise<AgcWasmToolchain>;

export default createToolchain;
