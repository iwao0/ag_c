export interface AgcWasmLinkOptions {
  exports?: string[];
  useStdlib?: boolean;
}

export interface AgcWasmLinkerOptions {
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

export interface AgcWasmLinker {
  instance: WebAssembly.Instance;
  memory: WebAssembly.Memory;
  link(objects: Array<ArrayBuffer | Uint8Array>, options?: AgcWasmLinkOptions): Uint8Array;
  readStdout(): string;
  readStderr(): string;
  readTermination(): AgcWasmTerminationEvent | null;
}

export type AgcWasmLinkerSource = string | URL | WebAssembly.Module | ArrayBuffer | Uint8Array;

export function createLinker(
  wasmSource: AgcWasmLinkerSource,
  options?: AgcWasmLinkerOptions,
): Promise<AgcWasmLinker>;

export default createLinker;
