export interface AgcWasmCompilerOptions {
  sourcePtr?: number;
  sourceCap?: number;
  outputPtr?: number;
  outputCap?: number;
}

export interface AgcWasmCompiler {
  instance: WebAssembly.Instance;
  memory: WebAssembly.Memory;
  limits: {
    sourcePtr: number;
    sourceCap: number;
    outputPtr: number;
    outputCap: number;
  };
  compileWat(source: string): string;
}

export type AgcWasmSource = string | URL | ArrayBuffer | Uint8Array;

export function createCompiler(
  wasmSource: AgcWasmSource,
  options?: AgcWasmCompilerOptions,
): Promise<AgcWasmCompiler>;

export default createCompiler;
