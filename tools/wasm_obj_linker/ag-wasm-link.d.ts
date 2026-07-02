export interface AgcWasmLinkOptions {
  exports?: string[];
  useStdlib?: boolean;
}

export interface AgcWasmLinker {
  instance: WebAssembly.Instance;
  memory: WebAssembly.Memory;
  link(objects: Array<ArrayBuffer | Uint8Array>, options?: AgcWasmLinkOptions): Uint8Array;
}

export type AgcWasmLinkerSource = string | URL | ArrayBuffer | Uint8Array;

export function createLinker(wasmSource: AgcWasmLinkerSource): Promise<AgcWasmLinker>;

export default createLinker;
