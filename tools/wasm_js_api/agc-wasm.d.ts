export interface AgcWasmCompilerOptions {
  sourcePtr?: number;
  sourceCap?: number;
  outputPtr?: number;
  outputCap?: number;
  initialOutputCap?: number;
  useHeapBuffers?: boolean;
  onStdout?: (chunk: string) => void;
  onStderr?: (chunk: string) => void;
  onExit?: (status: number) => void;
  onAbort?: (status: number) => void;
  onTerminate?: (event: AgcWasmTerminationEvent) => void;
}

export interface AgcWasmTerminationEvent {
  kind: "exit" | "abort" | "unknown";
  status: number;
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
  readStdout(): string;
  readStderr(): string;
  readTermination(): AgcWasmTerminationEvent | null;
}

export type AgcWasmSource = string | URL | ArrayBuffer | Uint8Array;

export function createCompiler(
  wasmSource: AgcWasmSource,
  options?: AgcWasmCompilerOptions,
): Promise<AgcWasmCompiler>;

export default createCompiler;
