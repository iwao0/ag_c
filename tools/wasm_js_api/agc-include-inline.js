const DEFAULT_INCLUDE_BASE_URL = new URL("../../include/", import.meta.url);

const BROWSER_INCLUDE_SHIMS = {
  "stddef.h": `#ifndef _STDDEF_H
#define _STDDEF_H
#define size_t unsigned long
#define ptrdiff_t long
#define wchar_t int
#define max_align_t long double
#define NULL ((void *)0)
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif
`,
  "stdarg.h": `#ifndef _STDARG_H
#define _STDARG_H
#define va_list char *
#define va_start(ap, last) ((void)(last), (ap) = (va_list)__va_arg_area)
#define va_arg(ap, type) (*(type *)((long)(ap += ((sizeof(type) + 7) & -8)) - ((sizeof(type) + 7) & -8)))
#define va_end(ap) ((void)(ap))
#define va_copy(dest, src) ((dest) = (src))
#endif
`,
  "stdio.h": `#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
#include <stdarg.h>
#define FILE void
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int puts(const char *s);
int putchar(int c);
int fputs(const char *s, FILE *stream);
int fputc(int c, FILE *stream);
int getchar(void);
int fgetc(FILE *stream);
int getc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
void perror(const char *s);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
#define stdin ((FILE *)0)
#define stdout ((FILE *)1)
#define stderr ((FILE *)2)
#endif
`,
};

function defaultAllowedInclude(name) {
  return /^[A-Za-z0-9_]+\.h$/.test(name);
}

async function defaultLoadInclude(name, baseUrl) {
  const url = new URL(name, baseUrl);
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`failed to fetch include <${name}>: ${response.status}`);
  }
  return response.text();
}

export async function inlineStandardIncludes(source, options = {}) {
  const baseUrl = options.baseUrl ?? DEFAULT_INCLUDE_BASE_URL;
  const useBrowserShims = options.useBrowserShims ?? true;
  const loadInclude = options.loadInclude ?? ((name) => defaultLoadInclude(name, baseUrl));
  const allowedInclude = options.allowedInclude ?? defaultAllowedInclude;
  const cache = new Map();
  const stack = [];

  async function expand(text, fromName) {
    const lines = text.split(/(\n)/);
    let out = "";
    for (let i = 0; i < lines.length; i += 2) {
      const line = lines[i];
      const newline = lines[i + 1] ?? "";
      const m = line.match(/^(\s*)#\s*include\s*<([^>]+)>\s*(?:\/\/.*|\/\*.*\*\/\s*)?$/);
      if (!m) {
        out += line + newline;
        continue;
      }
      const name = m[2];
      if (!allowedInclude(name)) {
        out += line + newline;
        continue;
      }
      out += await expandInclude(name, fromName);
      if (newline && !out.endsWith("\n")) out += newline;
    }
    return out;
  }

  async function expandInclude(name, fromName) {
    if (stack.includes(name)) {
      throw new Error(`include cycle while expanding <${name}> from ${fromName || "source"}`);
    }
    if (!cache.has(name)) {
      stack.push(name);
      try {
        const text = useBrowserShims && BROWSER_INCLUDE_SHIMS[name]
          ? BROWSER_INCLUDE_SHIMS[name]
          : await loadInclude(name);
        cache.set(name, await expand(text, name));
      } finally {
        stack.pop();
      }
    }
    return `\n/* begin <${name}> */\n${cache.get(name)}\n/* end <${name}> */\n`;
  }

  return expand(source, "source");
}

export default inlineStandardIncludes;
