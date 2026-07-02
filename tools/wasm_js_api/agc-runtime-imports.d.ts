export interface AgcRuntimeStdioOptions {
  getMemory?: () => WebAssembly.Memory | undefined | null;
  onStdout?: (chunk: string) => void;
  onStderr?: (chunk: string) => void;
}

export type AgcRuntimeImports = WebAssembly.Imports & {
  stdio?: AgcRuntimeStdioOptions;
  onStdout?: (chunk: string) => void;
  onStderr?: (chunk: string) => void;
};

export function createAgcRuntimeMathEnvImports(): WebAssembly.ModuleImports;
export function createAgcRuntimeStdioEnvImports(options?: AgcRuntimeStdioOptions): WebAssembly.ModuleImports;
export function createAgcRuntimeImports(imports?: AgcRuntimeImports): WebAssembly.Imports;
