export interface AgcWasmSignedExport {
  name: string;
  signature: string;
}

export type AgcWasmExport = string | AgcWasmSignedExport;

export type AgcLinkErrorCode =
  | "AGC_LINK_DUPLICATE_CONTINUATION_ENTRY"
  | "AGC_LINK_DUPLICATE_SYMBOL"
  | "AGC_LINK_FRAME_CONDITION_OUTSIDE_LOOP";

export interface AgcLinkSourceReference {
  readonly sourceIndex: number;
  readonly sourceName: string;
}

export type AgcLinkErrorDetails =
  | {
      readonly entry: string;
      readonly objectIndices: readonly [number, number];
      readonly sources?: readonly AgcLinkSourceReference[];
    }
  | {
      readonly symbol: string;
      readonly objectIndices: readonly [number, number];
      readonly sources?: readonly AgcLinkSourceReference[];
    }
  | {
      readonly frameCondition: string;
      readonly objectIndex: number;
      readonly source?: AgcLinkSourceReference | null;
    };

export class AgcLinkError extends Error {
  readonly name: "AgcLinkError";
  readonly code: AgcLinkErrorCode;
  readonly details: Readonly<AgcLinkErrorDetails>;
}

export interface AgcWasmLinkOptions {
  exports?: AgcWasmExport[];
  useStdlib?: boolean;
  initialMemoryPages?: number;
  maximumMemoryPages?: number;
  stackSize?: number;
  /** Maximum linked Wasm output bytes. Used internally by the toolchain resource policy. */
  maxOutputBytes?: number;
  maximumTableElements?: number;
  stdio?: {
    writeImportModule?: string;
    writeImportName?: string;
  };
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
