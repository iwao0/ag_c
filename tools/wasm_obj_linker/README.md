# ag_wasm_link

Experimental linker for the Wasm object files emitted by this repository's
`ag_c_wasm -c` mode.

This directory is intentionally self-contained so it can later move to a
separate repository.

## Build

```sh
make build/ag_wasm_link build/libagc_runtime.o
```

The linker can also be built as a wasm module:

```sh
make wasm-linker-selfhost
make test-wasm-linker-selfhost
```

`build/wasm_linker_selfhost/ag_wasm_link.wasm` exports `memory`, `malloc`,
`free`, `main`, `agc_wasm_link_objects`,
`agc_wasm_link_objects_with_options`, and
`agc_wasm_link_objects_with_export_signatures`. The API takes object bytes from linear
memory rather than filesystem paths:

```c
typedef struct {
  long ptr;
  long len;
} agc_link_slice_t;

long agc_wasm_link_objects(long inputs, int input_count,
                           long exports, int export_count,
                           int use_stdlib, long out_len);

typedef struct {
  long name;
  long signature; /* nullable for a name-only export */
} agc_link_export_t;

long agc_wasm_link_objects_with_export_signatures(
    long inputs, int input_count, long exports, int export_count,
    int use_stdlib, long options, long max_output_bytes, long out_len);
```

`inputs` points to an array of `(ptr,len)` object slices. `exports` points to an
array of C string pointers in the original API, or `agc_link_export_t` entries
in the signature-aware API. The return value is a pointer to the linked wasm
bytes, and `*out_len` receives the byte length.

For JavaScript/TypeScript, use `tools/wasm_obj_linker/ag-wasm-link.js` and
`ag-wasm-link.d.ts`:

```js
import { createLinker } from "./tools/wasm_obj_linker/ag-wasm-link.js";

const linker = await createLinker(wasmBytes);
const linked = linker.link([mainObjectBytes, otherObjectBytes], {
  exports: [{ name: "start", signature: "v()" }, "main"],
  initialMemoryPages: 128,
  maximumMemoryPages: 256,
  stackSize: 2097152,
  maximumTableElements: 4096,
});
```

Link failures that callers are expected to handle use `AgcLinkError` with a
stable `code` and immutable `details`. Current project-facing codes are:

- `AGC_LINK_MISSING_EXPORT`: `exportName` and whether it was `signed`.
- `AGC_LINK_DUPLICATE_CONTINUATION_ENTRY`: `entry` and two `objectIndices`.
- `AGC_LINK_DUPLICATE_SYMBOL`: `symbol` and two `objectIndices`.
- `AGC_LINK_FRAME_CONDITION_OUTSIDE_LOOP`: `frameCondition` and `objectIndex`.

The higher-level `createToolchain()` API maps object indices back to source
names and localizes the message using that compile request's
`diagnosticLocale`. Callers should branch on `code` and `details`, not message
text.

String exports retain the original name-only behavior. An object export checks
the function's canonical C type recorded in the `agc.c_signature` object
section. Common signature atoms are `v`, `b`, `i8`/`u8`, `i16`/`u16`,
`i32`/`u32`, `lN`/`ulN`, `llN`/`ullN`, `f32`, `f64`, and recursive
`p<type>` pointers. Function types use `result(param,...)`, so `void (void)` is
`v()` and `int (int)` is `i32(i32)`. Function pointer types remain structural,
for example `p<i32(i32)>`. A named enumeration records both its tag identity
and compatible integer type, for example `e{6:status:u32}`. Cross-object
signature comparison accepts that enumeration wherever the corresponding
`u32` occurs recursively, while distinct enumeration tags and `i32` remain
different types. Array types use `aN<type>`. An incomplete or variable bound
is `a0<type>` and is compatible with a specified bound when the element types
are compatible; two different non-zero constant bounds remain incompatible.
Named records retain their tag atom and append a structural suffix, for
example `s{6:packet}[1:1|5:value:0u:m1:16:i32]`. The `m` fields retain whether
the member declaration had an alignment specifier and its resolved requested
alignment. Complete records with the same tag must have compatible
corresponding members, including equivalent member alignment specifiers.
Structure members correspond in declaration order, while union members are
matched by name, bit-field attributes, alignment specifier, and compatible
type regardless of declaration order. An incomplete record suffix such as
`s{8:envelope}[0:0]` remains compatible with its completed definition.
Within one object, an earlier function-reference signature containing an
incomplete array bound or record is refined to the more complete signature
when its compatible definition becomes available.

The separate version 3 `agc.abi_layout` section records the target ABI layout
used for each function result and parameter. Its recursive fingerprint
includes aggregate size, alignment, member offsets, bit offsets, nested member
layouts, complete aggregate layouts reachable through pointers, and the
result/parameter layouts of pointed-to callback types. Recursive record
pointers use a cycle marker, while an incomplete pointee uses a type-local
wildcard so known result and parameter layouts remain checked.
This catches cross-object `#pragma pack` differences both for large aggregates
passed indirectly by the ABI and for aggregates dereferenced through
pointers or passed through callbacks. Union member fingerprints are compared
independently of declaration order. Versions 1 and 2 and objects without this
section remain linkable for backward compatibility.

The version 2 `agc.data_signature` section records the canonical C type,
version 3 target layout fingerprint, thread-local storage flag, and explicit
object alignment requirement of each externally visible data symbol. When
both the reference and definition provide this metadata, the linker rejects
incompatible scalar types, aggregate layout differences,
`_Thread_local`/ordinary-storage mismatches, and a reference whose `_Alignas`
requirement is absent or different on the definition before applying memory
relocations. A reference without `_Alignas` remains compatible with a more
strictly aligned definition. Version 1 entries remain readable; their absent
object properties are treated as unknown so old objects stay linkable.
Subobject symbols with a non-zero segment offset and manifest-declared runtime
data bridges are excluded because their symbol-level type intentionally
differs from the owning storage object. Objects without this section remain
linkable for backward compatibility.
Function-pointer data objects also preserve C11 compatibility between an
unprototyped callback and a prototype whose parameters are unchanged by the
default argument promotions. This comparison follows pointer and array
wrappers while still rejecting narrow, `_Bool`, `float`, and variadic
parameter mismatches.

## Usage

```sh
./build/ag_c_wasm -c -o main.o main.c
./build/ag_c_wasm -c -o other.o other.c
./build/ag_wasm_link --no-entry --export=main -o linked.wasm main.o other.o
```

Memory, stack reservation, and function-table limits can be configured from
both the CLI and JavaScript API:

```sh
./build/ag_wasm_link --no-entry --export=main \
  --initial-memory-pages=128 \
  --maximum-memory-pages=256 \
  --stack-size=2097152 \
  --maximum-table-elements=4096 \
  -o linked.wasm main.o other.o
```

The initial memory value is a lower bound. The linker raises it when linked
data plus the requested stack reservation needs more pages, and rejects the
module if that exceeds the configured maximum. Memory defaults to 1024 initial
pages with no maximum for compatibility. Table maximums are emitted only when
a table is needed and the option is specified. The linker does not emit a Wasm
Start section; a C `start` function must be exported and invoked by the host.

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
- Canonical C function signatures are retained in `agc.c_signature` object
  metadata for optional export-contract validation.
- Relocation custom sections must target the matching Code/Data section.
- Imported object globals used by the current backend, such as `__stack_pointer`.
- A defined linear memory exported as `memory`; memory pages are sized from the
  linked data layout and `__stack_pointer` is placed at the top of that memory.
- BSS-like data symbols whose symbol size is larger than their data payload;
  zero-initialized memory covers the omitted tail.
- A defined function table with element segments for address-taken functions;
  table index 0 is reserved for null function pointers and table index 1 is
  reserved for `SIG_IGN`.
- Final active data segment offsets and global initializer `i32.const`
  immediates are emitted as signed LEB128.
- Default runtime-object linking through `build/libagc_runtime.o`; currently it
  carries the small C runtime used by the fixture suite: formatter helpers
  (`printf`, `fprintf`, `snprintf`, `sprintf`, `scanf`, `fscanf`, `sscanf`,
  `swprintf`, `swscanf`),
  string/memory/ctype helpers including span/search helpers
  (`strspn`, `strcspn`, `strpbrk`),
  `puts`/`fputs`/`fputc`/`putchar`/`fflush`/`perror`/`getchar`,
  minimal file I/O stubs including seek/tell/error helpers,
  POSIX-style `open`/`read`/`close`/`fstat` plus `fdopen`,
  a reusable free-list allocator plus small stdlib helpers
  (`realloc`, `aligned_alloc`, `atol`, `atoll`, `strtol`, `strtoll`, `strtoull`,
  `rand`, `srand`, `labs`, `llabs`, `div`, `ldiv`, `lldiv`, `atexit`, `at_quick_exit`,
  `exit`, `quick_exit`, `_Exit`, `abort`, `qsort`, `bsearch`, `getenv`, `system`, `imaxabs`,
  `realpath`, `strtoimax`, `strtoumax`, `imaxdiv`),
  `time`/`clock`/`difftime`/`timespec_get`/`gmtime`/`localtime`/`mktime`/`asctime`/`ctime`/
  `strftime`/`wcsftime`, `getrusage`, `getline`,
  `errno` storage, wide-char string and conversion helpers
  including `wcsspn`/`wcscspn`/`wcspbrk`/`wcstok` and
  `wcstoll`/`wcstoull`/`wcstof`/`wcstold`, restartable multibyte helpers
  (`mbrlen`, `mbsinit`), wide character I/O helpers
  (`fgetwc`, `fputwc`, `fgetws`, `fputws`, `fwide`, and get/put aliases),
  uchar conversion helpers,
  fenv/locale/signal/wctype helpers, selected math helpers including
  trigonometric, inverse-trigonometric, hyperbolic, and inverse-hyperbolic helpers with f/l wrappers,
  exp/exp2/expm1/erf/erfc/log/log1p/log2/log10,
  `pow`/`powf`/`powl`, remainder/remquo, positive-difference/fused-multiply-add entry points,
  decomposition/sign helpers (`frexp`, `ldexp`, `scalbn`, `scalbln`, `ilogb`,
  `logb`, `modf`, `copysign`, `nan`, and f/l wrappers),
  cube-root, selected long-double wrappers,
  rounding helpers including fenv-aware `rint`/`nearbyint` and integer
  rounding wrappers, and math classification/comparison helpers (`fpclassify`,
  `isfinite`, `isinf`, `isnan`, `isnormal`, `signbit`, `isgreater`,
  `isgreaterequal`, `isless`, `islessequal`, `islessgreater`, `isunordered`),
  stdio globals, and `__assert_rtn`.
  The linker emits only small ABI bridges for those public symbols.

### Sandbox runtime policy

The default runtime does not expose the host filesystem, process environment,
process launcher, wall clock, CPU clock, or timezone database. `getenv` therefore
models an empty environment. Non-null `system`, `realpath`, `getrusage`, and the
JS-callback runtime's `rename` fail with `ENOSYS`; `time` and `clock` return `-1`,
and `timespec_get` returns `0`. `localtime` is deliberately the same UTC-only
conversion as `gmtime` and does not inspect host timezone state.

Streams are immediate, unbuffered in-memory streams. `_IONBF` is accepted by
`setvbuf`; full and line buffering fail with `ENOSYS`. Stream orientation is
sticky once selected. Wasm floating-point instructions always use their
specified IEEE-754 rounding behavior and cannot be reconfigured by
`fesetround`. The runtime still tracks all four C rounding modes for
mode-sensitive software helpers such as `rint` and `nearbyint`; unsupported
mode values fail without changing the current mode. Software exception flags
remain available through the fenv flag APIs.

`setjmp` and `longjmp` require compiler-assisted non-local control flow and are
rejected by the linker with an explicit unsupported-control-flow error. They are
not mapped to ordinary runtime functions.

## Host Stdio

Linked stdio uses one Wasm callback ABI for stdout and stderr:

```c
int __agc_host_write(int stream, const void *bytes, unsigned int length);
```

`stream` is `1` for stdout and `2` for stderr. The callback returns the accepted
byte count or a negative value on failure. A short write is reported to C as an
I/O failure. `printf`, `vprintf`, `fprintf`, `vfprintf`, `puts`, `fputs`,
`putchar`, stdout/stderr `fwrite`, and `write(1/2, ...)` share this path.
Memory-only formatting such as `snprintf` does not call the host.

The JavaScript link API can select the final import module and name:

```js
const wasm = toolchain.compileLinkedWasm(source, {
  stdio: {
    writeImportModule: "host_io",
    writeImportName: "write_bytes",
  },
});
```

The CLI equivalents are `--stdio-write-import-module=NAME` and
`--stdio-write-import-name=NAME`. If no stdio import option is present, the
linker selects the runtime's bounded in-memory stdout/stderr sink and emits no
host import. `createAgcRuntimeImports` validates stream numbers, memory ranges,
and the optional `stdio.maxWriteBytes` limit before accepting a write.

## Resumable Entry

The JavaScript toolchain can turn one direct frame loop in an entry function
into a resumable Wasm state machine:

```js
const wasm = toolchain.compileLinkedWasm(source, {
  exports: ["main"],
  continuation: { entry: "main", frameCondition: "game_running" },
});
```

`main()` starts the entry and returns `2` when suspended. The generated
`__agc_continuation_resume(i32)`, `__agc_continuation_status()`, and
`__agc_continuation_result()` exports resume the pending condition, read the
state, and read the completed C result. Status `0` is not started, `2` is
suspended, `3` is completed, and `-1` rejects an invalid start or resume.
The `start`, `resume`, `status`, and `result` names can be overridden in the
`continuation` option. Object files retain this contract in the
`agc.continuation` custom section for link-time validation.

An entry with no frame-condition call is also accepted. It executes once,
returns status `3`, stores the C result, and rejects every later resume. Because
there is no suspension point, ordinary automatic storage, VLA, `alloca`, and
goto/label control flow keep their synchronous function semantics.

## Smoke Test

```sh
make test-wasm-obj-linker
make test-wasm-linker-selfhost
make test-wasm-runtime-contracts
```

The native linker smoke test covers cross-object direct calls, extern global read/write,
data-address relocations in both code and data, static symbol collisions,
unresolved host function imports, function pointer relocation through both code
and data, imported host function table entries through both code and data
relocations, cross-object function pointer variables, indirect-call signatures
with floating-point parameters/results, indirect small-aggregate returns,
indirect hidden-ret-area aggregate returns, a large BSS-like global with an
omitted zero payload, a patched object with a non-zero data symbol offset,
duplicate external function/data definition errors, cross-object function/import
signature mismatch errors, malformed relocation target errors, and a
many-data-segment case that requires more than one Wasm memory page. It also
checks that default runtime-object linking resolves the runtime helpers above,
while `--nostdlib` leaves those symbols as imports instead.

The wasm self-host smoke test runs the JavaScript wrapper against
`build/wasm_linker_selfhost/ag_wasm_link.wasm`. It checks both a single-object
link and a two-object cross-TU link through `createLinker(...).link(...)`, then
validates and executes the produced wasm.

Not yet supported:

- TLS/runtime/startup integration beyond the current minimal globals.
- General-purpose LLVM/Clang Wasm object compatibility.
