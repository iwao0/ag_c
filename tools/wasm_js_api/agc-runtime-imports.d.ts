export interface AgcRuntimeStdioOptions {
  getMemory?: () => WebAssembly.Memory | undefined | null;
  onStdout?: (chunk: string) => void;
  onStderr?: (chunk: string) => void;
  stdin?: string | ArrayBuffer | Uint8Array;
  maxWriteBytes?: number;
}

export type AgcRuntimeImports = WebAssembly.Imports & {
  stdio?: AgcRuntimeStdioOptions;
  onStdout?: (chunk: string) => void;
  onStderr?: (chunk: string) => void;
};

export type AgcRuntimeWasmValueType = "i32" | "i64" | "f32" | "f64";

export interface AgcRuntimeFunctionManifestEntry {
  readonly cSymbol: string;
  readonly runtimeSymbol: string | null;
  readonly importNamespace: string;
  readonly importGroup: "math" | "stdio";
  readonly implementation: string;
  readonly signature: {
    readonly kind: "exact";
    readonly params: readonly AgcRuntimeWasmValueType[];
    readonly result: "void" | AgcRuntimeWasmValueType;
  };
  readonly memory: {
    readonly read: boolean;
    readonly write: boolean;
  };
  readonly availability: readonly (
    | "wasm32-js"
    | "wasm32-object-linker"
    | "wasm32-object-runtime"
  )[];
  readonly bridge: "runtime" | "host";
}

export interface AgcRuntimeImportManifest {
  readonly version: 2;
  readonly functions: readonly AgcRuntimeFunctionManifestEntry[];
  readonly namespaces: {
    readonly env: {
      readonly math: readonly string[];
      readonly stdio: readonly string[];
    };
  };
}

export const AGC_RUNTIME_IMPORT_MANIFEST: AgcRuntimeImportManifest;

export function createAgcRuntimeMathEnvImports(): WebAssembly.ModuleImports;
export function createAgcRuntimeStdioEnvImports(options?: AgcRuntimeStdioOptions): WebAssembly.ModuleImports;
export function createAgcRuntimeImports(imports?: AgcRuntimeImports): WebAssembly.Imports;
