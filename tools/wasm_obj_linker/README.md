# ag_wasm_link

Experimental linker for the Wasm object files emitted by this repository's
`ag_c_wasm -c` mode.

This directory is intentionally self-contained so it can later move to a
separate repository.

## Build

```sh
make build/ag_wasm_link
```

## Usage

```sh
./build/ag_c_wasm -c -o main.o main.c
./build/ag_c_wasm -c -o other.o other.c
./build/ag_wasm_link --no-entry --export=main -o linked.wasm main.o other.o
```

`--no-entry` is accepted for `wasm-ld`-shaped command lines.

## v1 Scope

Supported:

- Multiple `ag_c_wasm -c` object inputs.
- Defined and undefined functions.
- Direct call relocation: `R_WASM_FUNCTION_INDEX_LEB`.
- Function pointer/table relocations: `R_WASM_TABLE_INDEX_SLEB` and
  `R_WASM_TABLE_INDEX_I32`.
- Indirect-call type index relocation: `R_WASM_TYPE_INDEX_LEB`.
- Data address relocations: `R_WASM_MEMORY_ADDR_LEB` and `R_WASM_MEMORY_ADDR_I32`.
- Imported object globals used by the current backend, such as `__stack_pointer`.
- A defined linear memory exported as `memory`; memory pages are sized from the
  linked data layout and `__stack_pointer` is placed at the top of that memory.
- A defined function table with element segments for address-taken functions;
  table index 0 is reserved for null function pointers.
- Final active data segment offsets and global initializer `i32.const`
  immediates are emitted as signed LEB128.

## Smoke Test

```sh
make test-wasm-obj-linker
```

The smoke test covers cross-object direct calls, extern global read/write,
data-address relocations in both code and data, static symbol collisions,
unresolved host function imports, function pointer relocation through both code
and data, imported host function table entries through both code and data
relocations, cross-object function pointer variables, and a many-data-segment
case that requires more than one Wasm memory page.

Not yet supported:

- TLS/runtime/startup integration beyond the current minimal globals.
- General-purpose LLVM/Clang Wasm object compatibility.
