import { readFile } from "node:fs/promises";
import { createToolchain } from "./agc-toolchain.js";

const compilerPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const linkerPath = process.argv[3] || "build/wasm_linker_selfhost/ag_wasm_link.wasm";
const runtimePath = process.argv[4] || "build/libagc_runtime.o";
const callbackRuntimePath = process.argv[5] || "build/libagc_runtime_js.o";
const compilerWasm = await readFile(compilerPath);
const linkerWasm = await readFile(linkerPath);
const runtimeObject = await readFile(runtimePath);
const callbackRuntimeObject = await readFile(callbackRuntimePath);

async function freshToolchain(runtime = runtimeObject) {
  return createToolchain({ compilerWasm, linkerWasm, runtimeObject: runtime });
}

async function expectMain42(label, source, runtime = runtimeObject) {
  const toolchain = await freshToolchain(runtime);
  const linked = await toolchain.instantiateLinkedWasm(source, {
    exports: ["main"],
    useStdlib: true,
  });
  const result = linked.instance.exports.main();
  if (result !== 42) throw new Error(`${label} failed: main returned ${result}`);
}

const unsupportedJumpSource = `
typedef long jmp_buf[48];
int setjmp(jmp_buf env);
int main(void) { jmp_buf env; return setjmp(env); }
`;
try {
  const jumpToolchain = await freshToolchain();
  jumpToolchain.compileLinkedWasm(unsupportedJumpSource, {
    exports: ["main"],
    useStdlib: true,
  });
  throw new Error("setjmp unexpectedly linked");
} catch (error) {
  if (error.message === "setjmp unexpectedly linked" ||
      !error.message.includes("unsupported C control-flow function")) {
    throw error;
  }
}

await expectMain42("sandbox failure contracts", `
typedef long time_t;
typedef long clock_t;
struct timespec { long tv_sec; long tv_nsec; };
int *__error(void);
#define errno (*__error())
#define ENOSYS 78
char *getenv(const char *name);
char *realpath(const char *path, char *resolved);
int getrusage(int who, void *usage);
int system(const char *command);
time_t time(time_t *result);
clock_t clock(void);
int timespec_get(struct timespec *ts, int base);
char *strerror(int error);
int strcmp(const char *left, const char *right);
int main(void) {
  time_t now = 7;
  struct timespec ts = { 7, 9 };
  char resolved[16];
  long usage[8];
  if (time(&now) != -1 || now != -1 || clock() != -1) return 1;
  if (timespec_get(&ts, 1) != 0 || ts.tv_sec != 7 || ts.tv_nsec != 9) return 2;
  if (system((const char *)0) != 0) return 3;
  errno = 0;
  if (system("echo unsafe") != -1 || errno != ENOSYS) return 4;
  if (getenv("PATH") != (char *)0) return 5;
  errno = 0;
  if (realpath("file", resolved) != (char *)0 || errno != ENOSYS) return 6;
  errno = 0;
  if (getrusage(0, usage) != -1 || errno != ENOSYS) return 7;
  if (strcmp(strerror(2), strerror(9)) == 0) return 8;
  if (strcmp(strerror(12), strerror(22)) == 0) return 9;
  if (strcmp(strerror(78), "function not implemented") != 0) return 10;
  if (strcmp(strerror(12345), "unknown error") != 0) return 11;
  return 42;
}
`);

await expectMain42("stream contracts", `
typedef unsigned long size_t;
typedef struct FILE FILE;
int *__error(void);
#define errno (*__error())
#define ENOSYS 78
#define _IOFBF 0
#define _IONBF 2
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fputc(int ch, FILE *stream);
int fwide(FILE *stream, int mode);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
int main(void) {
  FILE *wide = fopen("wide", "w+");
  FILE *bytes = fopen("bytes", "w+");
  if (!wide || !bytes) return 1;
  if (fwide(wide, 0) != 0) return 2;
  if (fwide(wide, 1) <= 0 || fwide(wide, -1) <= 0 || fwide(wide, 0) <= 0) return 3;
  if (setvbuf(wide, (char *)0, _IONBF, 0) != 0) return 4;
  errno = 0;
  if (setvbuf(wide, (char *)0, _IOFBF, 128) == 0 || errno != ENOSYS) return 5;
  if (fputc('x', bytes) != 'x') return 6;
  if (fwide(bytes, 0) >= 0 || fwide(bytes, 1) >= 0) return 7;
  if (fclose(wide) != 0 || fclose(bytes) != 0) return 8;
  return 42;
}
`);

await expectMain42("floating-point contracts", `
#define FE_TONEAREST 0x00000000
#define FE_DOWNWARD 0x00800000
#define FE_DIVBYZERO 0x04
int fegetround(void);
int fesetround(int mode);
int feclearexcept(int flags);
int feraiseexcept(int flags);
int fetestexcept(int flags);
double fma(double x, double y, double z);
int main(void) {
  double delta = 1.0 / 134217728.0;
  double x = 1.0 + delta;
  double y = 1.0 - delta;
  double expected = -1.0 / 18014398509481984.0;
  if (fesetround(FE_TONEAREST) != 0 || fegetround() != FE_TONEAREST) return 1;
  if (fesetround(FE_DOWNWARD) == 0 || fegetround() != FE_TONEAREST) return 2;
  if (feclearexcept(FE_DIVBYZERO) != 0 || feraiseexcept(FE_DIVBYZERO) != 0) return 3;
  if (fetestexcept(FE_DIVBYZERO) != FE_DIVBYZERO) return 4;
  if (x * y - 1.0 != 0.0) return 5;
  if (fma(x, y, -1.0) != expected) return 6;
  return 42;
}
`);

await expectMain42("stateful UTF conversion", `
typedef unsigned long size_t;
typedef unsigned short char16_t;
typedef struct { unsigned int words[8]; } mbstate_t;
int *__error(void);
#define errno (*__error())
#define EILSEQ 92
size_t mbrtowc(int *wc, const char *s, size_t n, mbstate_t *state);
size_t mbrtoc16(char16_t *out, const char *s, size_t n, mbstate_t *state);
size_t c16rtomb(char *out, char16_t value, mbstate_t *state);
int mbsinit(const mbstate_t *state);
int main(void) {
  mbstate_t split = {{0}};
  mbstate_t utf16 = {{0}};
  mbstate_t encode = {{0}};
  int wc = 0;
  char16_t c16 = 0;
  char encoded[4];
  const char first[] = { (char)0xf0, 0 };
  const char rest[] = { (char)0x9f, (char)0x98, (char)0x80, 0 };
  const char emoji[] = { (char)0xf0, (char)0x9f, (char)0x98, (char)0x80, 0 };
  const char overlong[] = { (char)0xc0, (char)0xaf, 0 };
  const char surrogate[] = { (char)0xed, (char)0xa0, (char)0x80, 0 };
  if (mbrtowc(&wc, first, 1, &split) != (size_t)-2 || mbsinit(&split)) return 1;
  if (mbrtowc(&wc, rest, 3, &split) != 3 || wc != 0x1f600 || !mbsinit(&split)) return 2;
  if (mbrtoc16(&c16, emoji, 4, &utf16) != 4 || c16 != 0xd83d || mbsinit(&utf16)) return 3;
  if (mbrtoc16(&c16, "", 0, &utf16) != (size_t)-3 || c16 != 0xde00 || !mbsinit(&utf16)) return 4;
  if (c16rtomb(encoded, 0xd83d, &encode) != 0 || mbsinit(&encode)) return 5;
  if (c16rtomb(encoded, 0xde00, &encode) != 4 || !mbsinit(&encode)) return 6;
  if ((unsigned char)encoded[0] != 0xf0 || (unsigned char)encoded[3] != 0x80) return 7;
  errno = 0;
  if (mbrtowc(&wc, overlong, 2, &split) != (size_t)-1 || errno != EILSEQ) return 8;
  errno = 0;
  if (mbrtowc(&wc, surrogate, 3, &split) != (size_t)-1 || errno != EILSEQ) return 9;
  return 42;
}
`);

await expectMain42("qsort and bsearch complexity", `
typedef unsigned long size_t;
static int comparisons;
static int compare(const void *left, const void *right) {
  comparisons++;
  return *(const int *)left - *(const int *)right;
}
void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));
int main(void) {
  int values[256];
  int i;
  int key = 173;
  for (i = 0; i < 256; i++) values[i] = 255 - i;
  comparisons = 0;
  qsort(values, 256, sizeof(int), compare);
  if (comparisons > 10000) return 1;
  for (i = 0; i < 256; i++) if (values[i] != i) return 2;
  comparisons = 0;
  if (*(int *)bsearch(&key, values, 256, sizeof(int), compare) != key) return 3;
  if (comparisons > 10) return 4;
  return 42;
}
`);

await expectMain42("JS callback rename failure", `
int *__error(void);
#define errno (*__error())
#define ENOSYS 78
int rename(const char *old_name, const char *new_name);
int main(void) {
  errno = 0;
  if (rename("old", "new") != -1 || errno != ENOSYS) return 1;
  return 42;
}
`, callbackRuntimeObject);

console.log("ag_c wasm runtime contracts: ok");
