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

export interface AgcRuntimeImportManifest {
  readonly version: 1;
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
