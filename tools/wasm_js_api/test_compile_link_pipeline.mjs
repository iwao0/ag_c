import { execFileSync } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { createToolchain } from "./agc-toolchain.js";
import { inlineStandardIncludes } from "./agc-include-inline.js";

const compilerWasmPath = process.argv[2] || "build/wasm_selfhost_api/ag_c_wasm_api.wasm";
const linkerWasmPath = process.argv[3] || "build/wasm_linker_selfhost/ag_wasm_link.wasm";
const outDir = "build/wasm_js_pipeline_smoke";
await mkdir(outDir, { recursive: true });

const toolchain = await createToolchain({
  compilerWasm: await readFile(compilerWasmPath),
  linkerWasm: await readFile(linkerWasmPath),
  runtimeObject: await readFile("build/libagc_runtime.o"),
});
const loadInclude = async (name) => readFile(new URL(`../../include/${name}`, import.meta.url), "utf8");

const mainSource = "int other(void); int main(void) { return other() + 1; }\n";
const otherSource = "int other(void) { return 41; }\n";
const mainObj = toolchain.compileObject(mainSource);
const otherObj = toolchain.compileObject(otherSource);

const mainObjPath = path.join(outDir, "main_from_compiler_api.o");
const otherObjPath = path.join(outDir, "other_from_compiler_api.o");
await writeFile(mainObjPath, mainObj);
await writeFile(otherObjPath, otherObj);

try {
  const dump = execFileSync("wasm-objdump", ["-x", mainObjPath], { encoding: "utf8" });
  if (!dump.includes("linking") || !dump.includes("reloc.CODE")) {
    throw new Error("compiler API object is missing linking metadata or call relocation");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

const linked = toolchain.compileLinkedWasm([mainSource, otherSource], {
  exports: ["main"],
  useStdlib: false,
});
if (linked[0] !== 0x00 || linked[1] !== 0x61 || linked[2] !== 0x73 || linked[3] !== 0x6d) {
  throw new Error("pipeline output is not a wasm module");
}

const linkedPath = path.join(outDir, "linked_from_wasm_compiler_and_linker.wasm");
await writeFile(linkedPath, linked);

try {
  execFileSync("wasm-validate", [linkedPath], { stdio: "inherit" });
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

try {
  const out = execFileSync("wasm-interp", [linkedPath, "--run-all-exports"], { encoding: "utf8" });
  if (!out.includes("main() => i32:42")) {
    throw new Error(out.trim() || "wasm-interp produced no main result");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

const instantiated = await toolchain.instantiateLinkedWasm([mainSource, otherSource], {
  exports: ["main"],
  useStdlib: false,
});
if (instantiated.instance.exports.main() !== 42) {
  throw new Error("instantiated pipeline main() did not return 42");
}

const mathSource = `
double sin(double);
double sqrt(double);
double pow(double, double);
int main(void) {
  return (int)(sin(1.5707963267948966) * 1000.0) + (int)sqrt(4.0) + (int)pow(2.0, 3.0);
}
`;
const mathLinked = toolchain.compileLinkedWasm(mathSource, { exports: ["main"], useStdlib: false });
const mathLinkedPath = path.join(outDir, "linked_math_imports_from_api.wasm");
await writeFile(mathLinkedPath, mathLinked);
try {
  const dump = execFileSync("wasm-objdump", ["-x", mathLinkedPath], { encoding: "utf8" });
  if (!dump.includes("env.sin") ||
      !dump.includes("env.sqrt") ||
      !dump.includes("env.pow")) {
    throw new Error("linked math wasm did not import JS math helpers");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}
const mathInstantiated = await toolchain.instantiateLinkedWasm(mathSource, {
  exports: ["main"],
  useStdlib: false,
});
if (mathInstantiated.instance.exports.main() !== 1010) {
  throw new Error("instantiated math pipeline did not use JS math imports");
}

const linkedStdioSource = await inlineStandardIncludes(`#include <stdio.h>
int main(void) {
  printf("%*s:%03d:aa", 4, "x", 7);
  return 7;
}
`, { loadInclude });
let linkedStdioStdout = "";
const linkedStdio = await toolchain.instantiateLinkedWasm(linkedStdioSource, {
  exports: [
    "main",
    "__agc_runtime_stdout_ptr",
    "__agc_runtime_stdout_len",
    "__agc_runtime_stderr_ptr",
    "__agc_runtime_stderr_len",
  ],
  useStdlib: true,
}, {
  onStdout: (chunk) => { linkedStdioStdout += chunk; },
});
if (linkedStdio.instance.exports.main() !== 7) {
  throw new Error("instantiated stdio import pipeline did not use JS stdio imports");
}
if (linkedStdioStdout === "") linkedStdioStdout = linkedStdio.readStdout();
if (linkedStdioStdout !== "   x:007:aa") {
  throw new Error(`instantiated stdio import pipeline stdout mismatch: ${JSON.stringify(linkedStdioStdout)}`);
}

const linkedStdinSource = await inlineStandardIncludes(`#include <stdio.h>
int main(void) {
  char buf[4];
  int a = getchar();
  char *p = fgets(buf, sizeof(buf), stdin);
  int z = fgetc(stdin);
  int eof = fgetc(stdin);
  if (a != 'A') return 1;
  if (!p || buf[0] != 'B' || buf[1] != 'C' || buf[2] != '\\n' || buf[3] != 0) return 2;
  if (z != 'Z') return 3;
  if (eof != EOF || !feof(stdin)) return 4;
  return 42;
}
`, { loadInclude });
const linkedStdin = await toolchain.instantiateLinkedWasm(linkedStdinSource, {
  exports: ["main"],
  useStdlib: true,
}, {
  stdio: { stdin: "ABC\nZ" },
});
if (linkedStdin.instance.exports.main() !== 42) {
  throw new Error("linked runtime stdin injection failed");
}

const linkedLargeStdinSource = await inlineStandardIncludes(`#include <stdio.h>
int main(void) {
  char buf[600];
  unsigned long n = fread(buf, 1, sizeof(buf), stdin);
  unsigned long i = 0;
  int sum = 0;
  if (n != sizeof(buf)) return 1;
  while (i < sizeof(buf)) {
    sum += buf[i];
    i++;
  }
  return sum == 600 * 'x' ? 42 : 2;
}
`, { loadInclude });
const linkedLargeStdin = await toolchain.instantiateLinkedWasm(linkedLargeStdinSource, {
  exports: ["main"],
  useStdlib: true,
}, {
  stdio: { stdin: "x".repeat(600) },
});
if (linkedLargeStdin.instance.exports.main() !== 42) {
  throw new Error("linked runtime large stdin injection failed");
}

const linkedGetlineSource = await inlineStandardIncludes(`#include <stdio.h>
int main(void) {
  char *line = 0;
  unsigned long cap = 0;
  long n1 = getline(&line, &cap, stdin);
  if (n1 != 6 || line[0] != 'f' || line[4] != 't' || line[5] != '\\n' || line[6] != 0) return 1;
  long n2 = getline(&line, &cap, stdin);
  if (n2 != 7 || line[0] != 's' || line[6] != '\\n' || line[7] != 0) return 2;
  long n3 = getline(&line, &cap, stdin);
  if (n3 != -1 || !feof(stdin)) return 3;
  if (cap < 8) return 4;
  return 42;
}
`, { loadInclude });
const linkedGetline = await toolchain.instantiateLinkedWasm(linkedGetlineSource, {
  exports: ["main"],
  useStdlib: true,
}, {
  stdio: { stdin: "first\nsecond\n" },
});
const linkedGetlineResult = linkedGetline.instance.exports.main();
if (linkedGetlineResult !== 42) {
  throw new Error(`linked runtime getline stdin failed: ${linkedGetlineResult}`);
}

const jsSnprintfSource = `
int sprintf(char *buf, const char *fmt, int n, const char *s);
int snprintf(char *buf, unsigned long size, const char *fmt, int n);
int main(void) {
  char a[16];
  char b[4];
  char c[1];
  int n = sprintf(a, "%d-%s", 12, "ok");
  int m = snprintf(b, sizeof(b), "%05d", 42);
  int z = snprintf(c, 0, "%s", "longer");
  if (n != 5) return 1;
  if (a[0] != '1' || a[1] != '2' || a[2] != '-' || a[3] != 'o' || a[4] != 'k' || a[5] != 0) return 2;
  if (m != 5) return 3;
  if (b[0] != '0' || b[1] != '0' || b[2] != '0' || b[3] != 0) return 4;
  if (z != 6) return 5;
  return 42;
}
`;
const jsSnprintf = await toolchain.instantiateLinkedWasm(jsSnprintfSource, {
  exports: ["main"],
  useStdlib: false,
});
const jsSnprintfResult = jsSnprintf.instance.exports.main();
if (jsSnprintfResult !== 42) {
  throw new Error(`JS stdio snprintf/sprintf imports did not format into wasm memory: ${jsSnprintfResult}`);
}

const jsBasicStdioSource = `
int fputs(const char *s, void *stream);
int fputc(int c, void *stream);
int fflush(void *stream);
unsigned long fwrite(const void *ptr, unsigned long size, unsigned long nmemb, void *stream);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, void *stream);
int main(void) {
  char buf[4];
  if (fputs("A", (void *)1) != 1) return 1;
  if (fputc('B', (void *)1) != 'B') return 2;
  if (fputs("E", (void *)2) != 1) return 3;
  if (fputc('R', (void *)2) != 'R') return 4;
  if (fflush((void *)1) != 0) return 5;
  if (fwrite("CD", 1, 2, (void *)1) != 2) return 6;
  if (fwrite("!", 1, 1, (void *)2) != 1) return 7;
  if (fread(buf, 1, sizeof(buf), (void *)0) != 0) return 8;
  return 42;
}
`;
let jsBasicStdout = "";
let jsBasicStderr = "";
const jsBasicStdio = await toolchain.instantiateLinkedWasm(jsBasicStdioSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  onStdout: (chunk) => { jsBasicStdout += chunk; },
  onStderr: (chunk) => { jsBasicStderr += chunk; },
});
const jsBasicStdioResult = jsBasicStdio.instance.exports.main();
if (jsBasicStdioResult !== 42 || jsBasicStdout !== "ABCD" || jsBasicStderr !== "ER!") {
  throw new Error(
    `JS basic stdio imports failed: result=${jsBasicStdioResult}, stdout=${JSON.stringify(jsBasicStdout)}, stderr=${JSON.stringify(jsBasicStderr)}`,
  );
}

const jsStdinSource = `
int fgetc(void *stream);
int getchar(void);
char *fgets(char *s, int size, void *stream);
unsigned long fread(void *ptr, unsigned long size, unsigned long nmemb, void *stream);
int feof(void *stream);
int ferror(void *stream);
void clearerr(void *stream);
void perror(const char *s);
int main(void) {
  char line[4];
  char rest[4];
  if (fgetc((void *)0) != 'a') return 1;
  if (getchar() != '\\n') return 2;
  if (fgets(line, sizeof(line), (void *)0) != line) return 3;
  if (line[0] != 'b' || line[1] != 'c' || line[2] != '\\n' || line[3] != 0) return 4;
  if (fread(rest, 2, 2, (void *)0) != 2) return 5;
  if (rest[0] != 'd' || rest[1] != 'e' || rest[2] != 'f' || rest[3] != 'g') return 6;
  if (feof((void *)0) != 0) return 7;
  if (fgetc((void *)0) != -1) return 7;
  if (feof((void *)0) != 1) return 8;
  if (ferror((void *)0) != 0) return 9;
  clearerr((void *)0);
  if (feof((void *)0) != 0) return 10;
  perror("stdin");
  return 42;
}
`;
let jsStdinStderr = "";
const jsStdin = await toolchain.instantiateLinkedWasm(jsStdinSource, {
  exports: ["main"],
  useStdlib: false,
}, {
  stdio: { stdin: "a\nbc\ndefg" },
  onStderr: (chunk) => { jsStdinStderr += chunk; },
});
const jsStdinResult = jsStdin.instance.exports.main();
if (jsStdinResult !== 42 || jsStdinStderr !== "stdin: error\n") {
  throw new Error(`JS stdin imports failed: result=${jsStdinResult}, stderr=${JSON.stringify(jsStdinStderr)}`);
}

try {
  toolchain.compileLinkedWasm([
    "int main(void) { return other(); }\n",
    "int other(void){return 42２2;}\n",
  ], {
    exports: ["main"],
    useStdlib: false,
  });
  throw new Error("invalid UTF-8 token source unexpectedly compiled");
} catch (err) {
  const message = err && err.message ? err.message : String(err);
  if (!message.includes("source 2:") || !message.includes("E2028")) {
    throw new Error(`invalid token diagnostic was not surfaced: ${JSON.stringify(message)}`);
  }
  if (message.includes("%*s")) {
    throw new Error(`invalid token diagnostic leaked printf format text: ${JSON.stringify(message)}`);
  }
}

const stdioSource = await inlineStandardIncludes(`#include <stdio.h>
int main(void) { return 42; }
`, { loadInclude });
const stdioWat = toolchain.compileWat(stdioSource);
if (!stdioWat.includes("(return (i32.const 42))")) {
  throw new Error("standard include expansion did not compile stdio.h in the JS pipeline");
}

const complexSource = await inlineStandardIncludes(
  await readFile("test/fixtures/stdheader/complex_ops.c", "utf8"),
  { loadInclude },
);
const complexObj = toolchain.compileObject(complexSource);
const complexObjPath = path.join(outDir, "complex_ops_from_compiler_api.o");
await writeFile(complexObjPath, complexObj);
try {
  const dump = execFileSync("wasm-objdump", ["-x", complexObjPath], { encoding: "utf8" });
  if (!dump.includes("F <__ag_complex_sqrt>")) {
    throw new Error("compiler API object is missing __ag_complex_sqrt definition");
  }
  if (dump.includes("@Q0") || dump.includes("env.__ag_complex_sqrt")) {
    throw new Error("compiler API object has a corrupted or unresolved __ag_complex_sqrt symbol");
  }
} catch (err) {
  if (err.code !== "ENOENT") throw err;
}

console.log(`ag_c wasm JS compile+link pipeline smoke: ok (${linkedPath})`);
