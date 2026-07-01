# ag_wasm_link

Experimental linker for the Wasm object files emitted by this repository's
`ag_c_wasm -c` mode.

This directory is intentionally self-contained so it can later move to a
separate repository.

## Build

```sh
make build/ag_wasm_link build/libagc_runtime.o
```

## Usage

```sh
./build/ag_c_wasm -c -o main.o main.c
./build/ag_c_wasm -c -o other.o other.c
./build/ag_wasm_link --no-entry --export=main -o linked.wasm main.o other.o
```

`ag_wasm_link` appends `build/libagc_runtime.o` by default as the current
standard runtime object. Build it first with `make build/libagc_runtime.o`, or
use `--nostdlib` to leave those symbols as ordinary unresolved imports.
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
- Data symbols with non-zero offsets within a data segment.
- Duplicate non-local function/data definitions are rejected.
- Cross-object function and host import signature mismatches are rejected before
  producing an invalid final wasm.
- Relocation custom sections must target the matching Code/Data section.
- Imported object globals used by the current backend, such as `__stack_pointer`.
- A defined linear memory exported as `memory`; memory pages are sized from the
  linked data layout and `__stack_pointer` is placed at the top of that memory.
- BSS-like data symbols whose symbol size is larger than their data payload;
  zero-initialized memory covers the omitted tail.
- A defined function table with element segments for address-taken functions;
  table index 0 is reserved for null function pointers.
- Final active data segment offsets and global initializer `i32.const`
  immediates are emitted as signed LEB128.
- Default runtime-object linking through `build/libagc_runtime.o`; currently it
  carries the small C runtime used by the fixture suite: formatter helpers
  (`printf`, `fprintf`, `snprintf`, `sprintf`), string/memory/ctype helpers,
  `puts`/`fputs`/`fputc`/`putchar`/`fflush`/`perror`/`getchar`,
  minimal file I/O stubs including seek/tell/error helpers,
  a tiny bump allocator plus small stdlib helpers
  (`realloc`, `atol`, `strtol`, `rand`, `srand`, `labs`, `atexit`,
  `exit`, `abort`, `qsort`, `bsearch`, `getenv`, `system`, `imaxabs`),
  `time`/`clock`/`difftime`, `errno` storage, wide-char string/memory/conversion helpers,
  fenv/locale/signal/wctype helpers, selected math helpers including
  trigonometric, inverse-trigonometric, and hyperbolic helpers, exp/log/log2/log10,
  `pow`/`powf`/`powl`, remainder, cube-root, selected long-double wrappers,
  and rounding helpers, stdio globals, and `__assert_rtn`.
  The linker emits only small ABI bridges for those public symbols.

## Smoke Test

```sh
make test-wasm-obj-linker
```

The smoke test covers cross-object direct calls, extern global read/write,
data-address relocations in both code and data, static symbol collisions,
unresolved host function imports, function pointer relocation through both code
and data, imported host function table entries through both code and data
relocations, cross-object function pointer variables, a large BSS-like global
with an omitted zero payload, a patched object with a non-zero data symbol
offset, duplicate external function/data definition errors,
cross-object function/import signature mismatch errors, malformed relocation
target errors, and a many-data-segment case that requires more than one Wasm
memory page. It also checks that default runtime-object linking resolves the
runtime helpers above, while `--nostdlib` leaves those symbols as imports
instead.

Not yet supported:

- TLS/runtime/startup integration beyond the current minimal globals.
- General-purpose LLVM/Clang Wasm object compatibility.
