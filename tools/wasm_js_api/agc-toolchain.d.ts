import type {
  AgcCompileInput,
  AgcCompileOptions,
  AgcDiagnostic,
  AgcResourceLimits,
  AgcWasmCompiler,
  AgcWasmCompilerOptions,
  AgcWasmObjectResult,
  AgcWasmSource,
  AgcWasmWatResult,
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
  limits?: Partial<AgcResourceLimits>;
}

export { AgcResourceLimitError } from "./agc-wasm.js";
export type { AgcResourceLimitCode, AgcResourceLimits } from "./agc-wasm.js";
export type {
  AgcWasmExport,
  AgcWasmSignedExport,
} from "../wasm_obj_linker/ag-wasm-link.js";

export interface AgcToolchainLinkOptions extends AgcWasmLinkOptions, AgcCompileOptions {}

export interface AgcSourceDiagnosticSnapshot {
  readonly sourceId: number;
  readonly sourceName: string;
  readonly diagnostics: readonly AgcDiagnostic[];
}

export interface AgcLinkedWasmResult {
  readonly wasm: Uint8Array;
  /** Flattened in source compile order, then diagnostic emission order. */
  readonly diagnostics: readonly AgcDiagnostic[];
  readonly sourceDiagnostics: readonly AgcSourceDiagnosticSnapshot[];
}

export interface AgcWasmToolchain {
  compiler: AgcWasmCompiler;
  linker: AgcWasmLinker;
  resourceLimits: Readonly<AgcResourceLimits>;
  compileWat(source: AgcCompileInput, options?: AgcCompileOptions): string;
  compileWatWithDiagnostics(source: AgcCompileInput, options?: AgcCompileOptions): AgcWasmWatResult;
  compileObject(source: AgcCompileInput, options?: AgcCompileOptions): Uint8Array;
  compileObjectWithDiagnostics(
    source: AgcCompileInput,
    options?: AgcCompileOptions,
  ): AgcWasmObjectResult;
  dispose(): void;
  /** Explicit source names must be unique using case-sensitive comparison. */
  compileLinkedWasm(
    sources: AgcCompileInput | AgcCompileInput[],
    options?: AgcToolchainLinkOptions,
  ): Uint8Array;
  /** Explicit source names must be unique using case-sensitive comparison. */
  compileLinkedWasmWithDiagnostics(
    sources: AgcCompileInput | AgcCompileInput[],
    options?: AgcToolchainLinkOptions,
  ): AgcLinkedWasmResult;
  instantiateLinkedWasm(
    sources: AgcCompileInput | AgcCompileInput[],
    options?: AgcToolchainLinkOptions,
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
