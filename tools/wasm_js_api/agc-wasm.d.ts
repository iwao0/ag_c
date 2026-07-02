export interface AgcWasmCompilerOptions {
  sourcePtr?: number;
  sourceCap?: number;
  outputPtr?: number;
  outputCap?: number;
  initialOutputCap?: number;
  useHeapBuffers?: boolean;
}

export interface AgcWasmCompiler {
  instance: WebAssembly.Instance;
  memory: WebAssembly.Memory;
  limits: {
    sourcePtr: number;
    sourceCap: number;
    outputPtr: number;
    outputCap: number;
    initialOutputCap: number;
    useHeapBuffers: boolean;
  };
  compileWat(source: string): string;
  compileObject(source: string): Uint8Array;
}

export type AgcWasmSource = string | URL | ArrayBuffer | Uint8Array;

export function createCompiler(
  wasmSource: AgcWasmSource,
  options?: AgcWasmCompilerOptions,
): Promise<AgcWasmCompiler>;

export default createCompiler;
