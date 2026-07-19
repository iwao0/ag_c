#include "wasm32_ir.h"
#include "wasm32_machine_abi.h"
#include "wasm32_machine_function.h"
#include "wasm32_machine_ir.h"
#include "wasm32_wat_runtime.h"
#include "../../codegen_emit.h"
#include "../../diag/diag.h"
#include "../../lowering/abi_lowering.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WASM_PAGE_SIZE 65536
#define WASM_STATIC_BASE 1024
#define WASM_STACK_BASE WASM_PAGE_SIZE
#define WASM_HEAP_BASE 32768

#define WASM_FE_TONEAREST 0x00000000
#define WASM_FE_UPWARD 0x00400000
#define WASM_FE_DOWNWARD 0x00800000
#define WASM_FE_TOWARDZERO 0x00C00000

typedef struct {
  const wasm32_machine_alloca_t *layout;
  char *func_ref_name;
  int func_ref_name_len;
} wasm_alloca_slot_t;

typedef struct {
  const ir_symbol_t *symbol;
  int offset;
  char *func_ref_name;
  int func_ref_name_len;
  int is_set;
} wasm_global_func_state_t;

typedef struct {
  wasm32_ir_context_t *context;
  const ir_module_t *module;
  ir_func_t *f;
  wasm32_machine_function_t machine;
  wasm_alloca_slot_t *allocas;
  wasm_global_func_state_t *global_func_states;
  int global_func_state_count;
  int global_func_state_cap;
  char **vreg_func_ref_names;
  int *vreg_func_ref_name_lens;
  const ir_symbol_t **vreg_global_refs;
  int *vreg_global_ref_offsets;
  unsigned char *vreg_const_known;
  long long *vreg_const_values;
} wasm_func_ctx_t;

typedef struct {
  int next_data_off;
  wasm_data_symbol_t *symbols;
  int symbol_count;
  int symbol_cap;
} wasm_data_ctx_t;

typedef struct {
  char *name;
  int name_len;
} wasm_func_ref_t;

typedef struct {
  wasm_func_ref_t *refs;
  int ref_count;
  int ref_cap;
  int needs_table;
} wasm_func_table_ctx_t;

typedef struct {
  wasm_function_symbol_t *symbols;
  int count;
  int cap;
} wasm_function_symbol_ctx_t;

struct wasm32_ir_context_t {
  ag_codegen_emit_context_t *emit_context;
  wasm_data_ctx_t data;
  const ir_data_module_t *data_module;
  wasm_func_table_ctx_t func_table;
  wasm_function_symbol_ctx_t function_symbols;
  const ir_abi_data_module_t *data_abi;
  wasm32_machine_primitive_plan_t primitives;
};

wasm32_ir_context_t *wasm32_ir_context_create(
    ag_codegen_emit_context_t *emit_context) {
  if (!emit_context) return NULL;
  wasm32_ir_context_t *ctx = calloc(1, sizeof(*ctx));
  if (ctx) {
    ctx->emit_context = emit_context;
    ctx->data.next_data_off = WASM_STATIC_BASE;
  }
  return ctx;
}

void wasm32_ir_context_destroy(wasm32_ir_context_t *ctx) {
  if (!ctx) return;
  for (int i = 0; i < ctx->data.symbol_count; i++)
    free(ctx->data.symbols[i].name);
  for (int i = 0; i < ctx->func_table.ref_count; i++)
    free(ctx->func_table.refs[i].name);
  for (int i = 0; i < ctx->function_symbols.count; i++)
    free(ctx->function_symbols.symbols[i].name);
  free(ctx->data.symbols);
  free(ctx->func_table.refs);
  free(ctx->function_symbols.symbols);
  free(ctx);
}

#define g_data (context->data)
#define g_data_module (context->data_module)
#define g_func_table (context->func_table)
#define g_function_symbols (context->function_symbols)
#define g_data_abi (context->data_abi)
#define g_machine_primitives (context->primitives)

ag_codegen_emit_context_t *wasm32_ir_emit_context(
    wasm32_ir_context_t *context) {
  if (!context || !context->emit_context) abort();
  return context->emit_context;
}

static ag_diagnostic_context_t *wasm32_ir_diagnostics(
    wasm32_ir_context_t *context) {
  return cg_context_diagnostics(wasm32_ir_emit_context(context));
}

#define wasm_cg_emitf(...) \
  cg_emitf_in(wasm32_ir_emit_context(context), __VA_ARGS__)
static const char k_wasm_indent_spaces[] = "                                ";

static void wasm_emit_indent(
    wasm32_ir_context_t *context, int spaces) {
  int chunk = (int)sizeof(k_wasm_indent_spaces) - 1;
  while (spaces > chunk) {
    wasm_cg_emitf("%s", k_wasm_indent_spaces);
    spaces -= chunk;
  }
  if (spaces > 0) wasm_cg_emitf("%.*s", spaces, k_wasm_indent_spaces);
}

#define wasm_emitf(spaces, ...)       \
  do {                                \
    wasm_emit_indent(context, (spaces)); \
    wasm_cg_emitf(__VA_ARGS__);            \
  } while (0)

const char *wasm_type(ir_type_t t) {
  switch (t) {
    case IR_TY_I8:
    case IR_TY_I16:
    case IR_TY_I32:
    case IR_TY_PTR:
      return "i32";
    case IR_TY_I64:
      return "i64";
    case IR_TY_F32:
      return "f32";
    case IR_TY_F64:
      return "f64";
    default:
      return NULL;
  }
}

static int is_fp_type(ir_type_t t) {
  return t == IR_TY_F32 || t == IR_TY_F64;
}

const char *wasm_any_type_or_unsupported(
    wasm32_ir_context_t *context, ir_type_t t) {
  const char *ty = wasm_type(t);
  if (!ty)
    wasm_unsupported_msg(context, "unsupported Wasm value type");
  return ty;
}

static int align_to(int value, int align) {
  if (align <= 1) return value;
  int mask = align - 1;
  return (value + mask) & ~mask;
}

static void wasm_unsupported_op(
    wasm32_ir_context_t *context, ir_op_t op) {
  diag_emit_internalf_in(wasm32_ir_diagnostics(context), DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
                      diag_message_for_in(wasm32_ir_diagnostics(context), DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
                      ir_op_name(op));
}

void wasm_unsupported_msg(
    wasm32_ir_context_t *context, const char *msg) {
  diag_emit_internalf_in(wasm32_ir_diagnostics(context), DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
                      diag_message_for_in(wasm32_ir_diagnostics(context), DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
                      msg);
}

static int name_eq(const char *a, int alen, const char *b, int blen) {
  return alen == blen && a && b && memcmp(a, b, (size_t)alen) == 0;
}

wasm_function_symbol_t *function_symbol_state(
    wasm32_ir_context_t *context,
    const char *name, int name_len, int create) {
  if (!name || name_len <= 0) return NULL;
  for (int i = 0; i < g_function_symbols.count; i++) {
    wasm_function_symbol_t *symbol = &g_function_symbols.symbols[i];
    if (name_eq(symbol->name, symbol->name_len, name, name_len)) return symbol;
  }
  if (!create) return NULL;
  if (g_function_symbols.count == g_function_symbols.cap) {
    int next_cap = g_function_symbols.cap ? g_function_symbols.cap * 2 : 32;
    wasm_function_symbol_t *next = realloc(
        g_function_symbols.symbols, (size_t)next_cap * sizeof(*next));
    if (!next)
      diag_emit_internalf_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM, "%s",
                          diag_message_for_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM));
    g_function_symbols.symbols = next;
    g_function_symbols.cap = next_cap;
  }
  wasm_function_symbol_t *symbol =
      &g_function_symbols.symbols[g_function_symbols.count++];
  memset(symbol, 0, sizeof(*symbol));
  symbol->name = malloc((size_t)name_len + 1);
  if (!symbol->name)
    diag_emit_internalf_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM, "%s",
                        diag_message_for_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM));
  memcpy(symbol->name, name, (size_t)name_len);
  symbol->name[name_len] = '\0';
  symbol->name_len = name_len;
  return symbol;
}

static void register_function_reference(
    wasm32_ir_context_t *context, const char *name, int name_len) {
  wasm_function_symbol_t *symbol = function_symbol_state(
      context, name, name_len, 1);
  if (symbol) symbol->referenced = 1;
}

static void record_function_signature(
    wasm32_ir_context_t *context,
    const char *name, int name_len,
    const wasm32_machine_signature_t *signature) {
  wasm_function_symbol_t *symbol = function_symbol_state(
      context, name, name_len, 1);
  if (!symbol || !signature) return;
  int param_count = signature->nparams;
  ir_type_t *params = NULL;
  if (param_count > 0) {
    params = malloc((size_t)param_count * sizeof(*params));
    if (!params)
      diag_emit_internalf_in(
          wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM, "%s",
          diag_message_for_in(
              wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM));
    for (int i = 0; i < param_count; i++)
      params[i] = signature->params[i];
  }
  free(symbol->param_types);
  symbol->param_types = params;
  symbol->result_type = signature->result;
  symbol->param_count = param_count;
  symbol->has_signature = 1;
}

static void register_function_definition(
    wasm32_ir_context_t *context, const ir_func_t *function,
    const wasm32_machine_function_t *machine) {
  if (!function || !machine) return;
  wasm_function_symbol_t *symbol = function_symbol_state(
      context, function->name, function->name_len, 1);
  if (!symbol) return;
  symbol->defined = 1;
  record_function_signature(
      context, function->name, function->name_len,
      &machine->signature);
}

static void reset_function_symbols(wasm32_ir_context_t *context) {
  for (int i = 0; i < g_function_symbols.count; i++) {
    free(g_function_symbols.symbols[i].param_types);
    g_function_symbols.symbols[i].param_types = NULL;
    free(g_function_symbols.symbols[i].name);
  }
  g_function_symbols.count = 0;
}

static wasm_data_symbol_t *find_data_symbol(
    wasm32_ir_context_t *context, const char *name, int name_len) {
  for (int i = 0; i < g_data.symbol_count; i++) {
    if (name_eq(g_data.symbols[i].name, g_data.symbols[i].name_len, name, name_len)) {
      return &g_data.symbols[i];
    }
  }
  return NULL;
}

wasm_data_symbol_t *intern_data_symbol(
    wasm32_ir_context_t *context,
    const char *name, int name_len, int size, int align) {
  wasm_data_symbol_t *existing = find_data_symbol(context, name, name_len);
  if (existing) return existing;
  if (g_data.symbol_count == g_data.symbol_cap) {
    int ncap = g_data.symbol_cap ? g_data.symbol_cap * 2 : 32;
    wasm_data_symbol_t *n = realloc(g_data.symbols, (size_t)ncap * sizeof(*n));
    if (!n) diag_emit_internalf_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM));
    g_data.symbols = n;
    g_data.symbol_cap = ncap;
  }
  g_data.next_data_off = align_to(g_data.next_data_off, align > 0 ? align : 1);
  wasm_data_symbol_t *s = &g_data.symbols[g_data.symbol_count++];
  s->name = malloc((size_t)name_len + 1);
  if (!s->name)
    diag_emit_internalf_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM, "%s",
                        diag_message_for_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM));
  memcpy(s->name, name, (size_t)name_len);
  s->name[name_len] = '\0';
  s->name_len = name_len;
  s->addr = g_data.next_data_off;
  s->size = size > 0 ? size : 1;
  g_data.next_data_off += s->size;
  return s;
}

static int data_addr_for_string_label(
    wasm32_ir_context_t *context, const char *sym) {
  if (!sym) return -1;
  int sym_len = (int)strlen(sym);
  ir_data_object_t *object =
      ir_data_module_find_object(g_data_module, sym, sym_len);
  if (!object || object->kind != IR_DATA_STRING) return -1;
  return intern_data_symbol(context, sym, sym_len, object->byte_size,
                            object->alignment)->addr;
}

static int data_addr_for_ir_string(
    wasm32_ir_context_t *context,
    const char *sym, int sym_len, int size) {
  if (!sym || sym_len <= 0 || size <= 0) return -1;
  return intern_data_symbol(context, sym, sym_len, size, 1)->addr;
}

static int data_addr_for_data_object(
    wasm32_ir_context_t *context, const ir_data_object_t *object) {
  if (!object) return -1;
  return intern_data_symbol(context, object->name, object->name_len,
                            object->byte_size, object->alignment)->addr;
}

static int data_addr_for_ir_symbol(
    wasm32_ir_context_t *context, const ir_symbol_t *symbol) {
  if (!symbol) return -1;
  return intern_data_symbol(context, symbol->name, symbol->name_len,
                            symbol->byte_size, symbol->alignment)->addr;
}

static int intern_function_table_ref(
    wasm32_ir_context_t *context, char *name, int name_len) {
  if (!name || name_len <= 0) return -1;
  register_function_reference(context, name, name_len);
  for (int i = 0; i < g_func_table.ref_count; i++) {
    if (name_eq(g_func_table.refs[i].name, g_func_table.refs[i].name_len, name, name_len)) return i + 2;
  }
  if (g_func_table.ref_count == g_func_table.ref_cap) {
    int ncap = g_func_table.ref_cap ? g_func_table.ref_cap * 2 : 16;
    wasm_func_ref_t *n = realloc(g_func_table.refs, (size_t)ncap * sizeof(*n));
    if (!n) diag_emit_internalf_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM));
    g_func_table.refs = n;
    g_func_table.ref_cap = ncap;
  }
  int idx = g_func_table.ref_count++;
  g_func_table.refs[idx].name = malloc((size_t)name_len + 1);
  if (!g_func_table.refs[idx].name)
    diag_emit_internalf_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM, "%s",
                        diag_message_for_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM));
  memcpy(g_func_table.refs[idx].name, name, (size_t)name_len);
  g_func_table.refs[idx].name[name_len] = '\0';
  g_func_table.refs[idx].name_len = name_len;
  return idx + 2;
}

int function_table_has_ref(
    wasm32_ir_context_t *context, const char *name, int name_len) {
  for (int i = 0; i < g_func_table.ref_count; i++) {
    if (name_eq(g_func_table.refs[i].name, g_func_table.refs[i].name_len,
                name, name_len))
      return 1;
  }
  return 0;
}

void wasm32_wat_require_function_table(wasm32_ir_context_t *context) {
  g_func_table.needs_table = 1;
}

static int function_table_index_or_unsupported(
    wasm32_ir_context_t *context, char *name, int name_len) {
  return intern_function_table_ref(context, name, name_len);
}

static void record_function_reference_signature(
    wasm32_ir_context_t *context, const wasm32_machine_inst_t *inst) {
  if (!inst || !inst->sym || !inst->has_reference_signature) return;
  record_function_signature(
      context, inst->sym, inst->sym_len, &inst->reference_signature);
}

static void record_function_call_signature(
    wasm32_ir_context_t *context,
    const wasm32_machine_inst_t *call,
    const wasm32_machine_call_t *machine_call) {
  if (!call || !call->sym) return;
  register_function_reference(context, call->sym, call->sym_len);
  record_function_signature(
      context, call->sym, call->sym_len, &machine_call->signature);
}

static int has_minimal_libc_stub_function(char *name, int name_len) {
  static const char *stub_names[] = {
      "__assert_rtn", "__error",
      "_Exit", "abs", "acos", "acosf", "acosh", "acoshf", "acoshl", "acosl",
      "aligned_alloc", "asctime", "asin", "asinf", "asinh", "asinhf", "asinhl",
      "asinl", "abort", "at_quick_exit", "atan", "atan2", "atan2f", "atan2l", "atanf",
      "atanh", "atanhf", "atanhl", "atanl", "atexit", "atof", "atoi", "atol",
      "atoll", "bsearch", "btowc", "c16rtomb", "c32rtomb", "calloc", "cbrt",
      "cbrtf", "cbrtl", "ceil", "ceilf", "ceill", "clearerr", "clock",
      "copysign", "copysignf", "copysignl", "cos", "cosf", "cosh", "coshf",
      "coshl", "cosl", "ctime", "difftime", "div", "erf", "erfc", "erfcf",
      "erfcl", "erff", "erfl", "exp", "exp2", "exp2f", "exp2l", "expf",
      "expl", "expm1", "expm1f", "expm1l", "exit", "fabs", "fabsf", "fabsl", "fclose",
      "fdim", "fdimf", "fdiml", "fdopen", "feclearexcept", "fegetenv",
      "fegetexceptflag", "fegetround", "feholdexcept", "feof", "feraiseexcept",
      "ferror", "fesetenv", "fesetexceptflag", "fesetround", "fetestexcept",
      "feupdateenv", "fflush", "fgetc", "fgetpos", "fgets", "fgetwc", "fgetws",
      "floor", "floorf", "floorl", "fma", "fmaf", "fmal", "fmax", "fmaxf",
      "fmaxl", "fmin", "fminf", "fminl", "fmod", "fmodf", "fmodl", "fopen",
      "fpclassify", "fprintf", "fputc", "fputs", "fputwc", "fputws", "fread",
      "free", "freopen", "frexp", "frexpf", "frexpl", "fscanf", "fseek",
      "fsetpos", "ftell", "fwide", "fwrite", "getc", "getchar", "getenv",
      "getline", "getwc", "getwchar", "gmtime", "hypot", "hypotf", "hypotl",
      "ilogb", "ilogbf", "ilogbl", "imaxabs", "imaxdiv", "isalnum", "isalpha",
      "isblank", "iscntrl", "isdigit", "isfinite", "isgraph", "isgreater",
      "isgreaterequal", "isinf", "isless", "islessequal", "islessgreater",
      "islower", "isnan", "isnormal", "isprint", "ispunct", "isspace",
      "isunordered", "isupper", "iswalnum", "iswalpha", "iswblank", "iswcntrl",
      "iswctype", "iswdigit", "iswgraph", "iswlower", "iswprint", "iswpunct",
      "iswspace", "iswupper", "iswxdigit", "isxdigit", "labs", "ldexp",
      "ldexpf", "ldexpl", "ldiv", "llabs", "lldiv", "llrint", "llrintf",
      "llrintl", "llround", "llroundf", "llroundl", "localeconv", "localtime",
      "longjmp", "log", "log10", "log10f", "log10l", "log1p", "log1pf", "log1pl", "log2",
      "log2f", "log2l", "logb", "logbf", "logbl", "logf", "logl", "lrint",
      "lrintf", "lrintl", "lround", "lroundf", "lroundl", "malloc", "mblen",
      "mbrlen", "mbrtoc16", "mbrtoc32", "mbrtowc", "mbsinit", "mbsrtowcs",
      "mbstowcs", "mbtowc", "memchr", "memcmp", "memcpy", "memmove", "memset",
      "mktime", "modf", "modff", "modfl", "nan", "nanf", "nanl",
      "nearbyint", "nearbyintf", "nearbyintl", "perror", "pow", "powf", "powl", "printf", "putc",
      "putchar", "puts", "putwc", "putwchar", "qsort", "raise", "rand",
      "quick_exit", "realloc", "realpath", "remainder", "remainderf", "remainderl", "remove",
      "remquo", "remquof", "remquol", "rename", "rewind", "rint", "rintf",
      "rintl", "round", "roundf", "roundl", "scalbln", "scalblnf", "scalblnl",
      "scalbn", "scalbnf", "scalbnl", "scanf", "setbuf", "setjmp", "setlocale",
      "setvbuf", "signal", "signbit", "sin", "sinf", "sinh", "sinhf", "sinhl",
      "sinl", "snprintf", "sprintf", "sqrt", "sqrtf", "sqrtl", "srand",
      "sscanf", "strcat", "strchr", "strcmp", "strcoll", "strcpy", "strcspn",
      "strerror", "strftime", "strlen", "strncat", "strncmp", "strncpy",
      "strpbrk", "strrchr", "strspn", "strstr", "strtod", "strtof",
      "strtoimax", "strtok", "strtol", "strtold", "strtoll", "strtoul",
      "strtoull", "strtoumax", "strxfrm", "swprintf", "swscanf", "system",
      "tan", "tanf", "tanh", "tanhf", "tanhl", "tanl", "time", "timespec_get",
      "tmpfile", "tmpnam", "tolower", "toupper", "towctrans", "towlower",
      "towupper", "trunc", "truncf", "truncl", "ungetc", "ungetwc", "vfprintf",
      "vfscanf", "vprintf", "vscanf", "vsnprintf", "vsprintf", "vsscanf",
      "wcrtomb", "wcscat", "wcschr", "wcscmp", "wcscoll", "wcscpy", "wcscspn",
      "wcsftime", "wcslen", "wcsncat", "wcsncmp", "wcsncpy", "wcspbrk",
      "wcsrchr", "wcsrtombs", "wcsspn", "wcsstr", "wcstod", "wcstof", "wcstok",
      "wcstol", "wcstold", "wcstoll", "wcstombs", "wcstoul", "wcstoull",
      "wcsxfrm", "wctob", "wctomb", "wctrans", "wctype", "wmemchr", "wmemcmp",
      "wmemcpy", "wmemmove", "wmemset",
  };
  int n = (int)(sizeof(stub_names) / sizeof(stub_names[0]));
  for (int i = 0; i < n; i++) {
    int len = (int)strlen(stub_names[i]);
    if (name_len == len && memcmp(name, stub_names[i], (size_t)len) == 0) return 1;
  }
  return 0;
}

static void emit_function_table(wasm32_ir_context_t *context) {
  if (g_func_table.ref_count <= 0) {
    if (g_func_table.needs_table) wasm_emitf(2, "(table 2 funcref)\n");
    return;
  }
  for (int i = 0; i < g_func_table.ref_count; i++) {
    char *name = g_func_table.refs[i].name;
    int name_len = g_func_table.refs[i].name_len;
    if (!has_defined_function(context, name, name_len) &&
        !(has_undefined_function(context, name, name_len) &&
          has_minimal_libc_stub_function(name, name_len))) {
      wasm_unsupported_msg(
          context, "external function pointer in Wasm backend");
    }
  }
  wasm_emitf(2, "(table %d funcref)\n", g_func_table.ref_count + 2);
  wasm_emitf(2, "(elem (i32.const 2)");
  for (int i = 0; i < g_func_table.ref_count; i++) {
    char *name = g_func_table.refs[i].name;
    int name_len = g_func_table.refs[i].name_len;
    if (name_len == 7 && memcmp(name, "fprintf", 7) == 0 &&
        !has_defined_function(context, name, name_len)) {
      wasm_cg_emitf(" $__ag_funcptr_fprintf");
    } else {
      wasm_cg_emitf(" $%.*s", name_len, name);
    }
  }
  wasm_cg_emitf(")\n");
}

static wasm_alloca_slot_t *find_alloca_slot(wasm_func_ctx_t *ctx, int vreg) {
  for (int i = 0; i < ctx->machine.alloca_count; i++) {
    if (ctx->allocas[i].layout &&
        ctx->allocas[i].layout->vreg == vreg)
      return &ctx->allocas[i];
  }
  return NULL;
}

static int find_alloca_offset(wasm_func_ctx_t *ctx, int vreg) {
  wasm_alloca_slot_t *slot = find_alloca_slot(ctx, vreg);
  if (slot && slot->layout) return slot->layout->offset;
  return -1;
}

static ir_type_t effective_val_type(wasm_func_ctx_t *ctx, ir_val_t v) {
  return wasm32_machine_function_vreg_type(&ctx->machine, v);
}

static int val_is_unsigned(wasm_func_ctx_t *ctx, ir_val_t v) {
  return wasm32_machine_function_vreg_is_unsigned(&ctx->machine, v);
}

static void set_vreg_func_ref(wasm_func_ctx_t *ctx, int vreg, char *name, int name_len) {
  if (vreg < 0 || vreg >= ctx->f->next_vreg_id) return;
  ctx->vreg_func_ref_names[vreg] = name;
  ctx->vreg_func_ref_name_lens[vreg] = name_len;
}

static char *get_vreg_func_ref(wasm_func_ctx_t *ctx, int vreg, int *out_len) {
  if (out_len) *out_len = 0;
  if (vreg < 0 || vreg >= ctx->f->next_vreg_id) return NULL;
  if (out_len) *out_len = ctx->vreg_func_ref_name_lens[vreg];
  return ctx->vreg_func_ref_names[vreg];
}

static void set_vreg_global_ref(wasm_func_ctx_t *ctx, int vreg,
                                const ir_symbol_t *symbol, int offset) {
  if (vreg < 0 || vreg >= ctx->f->next_vreg_id) return;
  ctx->vreg_global_refs[vreg] = symbol;
  ctx->vreg_global_ref_offsets[vreg] = offset;
}

static const ir_symbol_t *get_vreg_global_ref(wasm_func_ctx_t *ctx, int vreg,
                                               int *out_offset) {
  if (out_offset) *out_offset = 0;
  if (vreg < 0 || vreg >= ctx->f->next_vreg_id) return NULL;
  if (out_offset) *out_offset = ctx->vreg_global_ref_offsets[vreg];
  return ctx->vreg_global_refs[vreg];
}

static void set_vreg_const(wasm_func_ctx_t *ctx, int vreg, long long value) {
  if (vreg < 0 || vreg >= ctx->f->next_vreg_id) return;
  ctx->vreg_const_known[vreg] = 1;
  ctx->vreg_const_values[vreg] = value;
}

static int get_vreg_const(wasm_func_ctx_t *ctx, ir_val_t v, long long *out_value) {
  if (v.id == IR_VAL_IMM) {
    if (out_value) *out_value = v.imm;
    return 1;
  }
  if (v.id < 0 || v.id >= ctx->f->next_vreg_id || !ctx->vreg_const_known[v.id]) return 0;
  if (out_value) *out_value = ctx->vreg_const_values[v.id];
  return 1;
}

static wasm_global_func_state_t *find_global_func_state(
    wasm_func_ctx_t *ctx, const ir_symbol_t *symbol, int offset) {
  if (!symbol) return NULL;
  for (int i = 0; i < ctx->global_func_state_count; i++) {
    if (ctx->global_func_states[i].symbol == symbol &&
        ctx->global_func_states[i].offset == offset) {
      return &ctx->global_func_states[i];
    }
  }
  return NULL;
}

static void set_global_func_ref(wasm_func_ctx_t *ctx,
                                const ir_symbol_t *symbol, int offset,
                                char *name, int name_len) {
  wasm32_ir_context_t *context = ctx->context;
  if (!symbol) return;
  wasm_global_func_state_t *s = find_global_func_state(ctx, symbol, offset);
  if (!s) {
    if (ctx->global_func_state_count == ctx->global_func_state_cap) {
      int ncap = ctx->global_func_state_cap ? ctx->global_func_state_cap * 2 : 8;
      wasm_global_func_state_t *n =
          realloc(ctx->global_func_states, (size_t)ncap * sizeof(*n));
      if (!n) diag_emit_internalf_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM));
      ctx->global_func_states = n;
      ctx->global_func_state_cap = ncap;
    }
    s = &ctx->global_func_states[ctx->global_func_state_count++];
    s->symbol = symbol;
    s->offset = offset;
  }
  s->func_ref_name = name;
  s->func_ref_name_len = name_len;
  s->is_set = 1;
}

static char *current_global_func_ref(wasm_func_ctx_t *ctx,
                                     const ir_symbol_t *symbol, int offset,
                                     int *out_len) {
  if (out_len) *out_len = 0;
  wasm_global_func_state_t *s = find_global_func_state(ctx, symbol, offset);
  if (s && s->is_set) {
    if (out_len) *out_len = s->func_ref_name_len;
    return s->func_ref_name;
  }
  const ir_symbol_func_ref_t *ref = ir_symbol_find_func_ref(symbol, offset);
  if (!ref) return NULL;
  if (out_len) *out_len = ref->name_len;
  return ref->name;
}

static void analyze_func(
    wasm_func_ctx_t *ctx,
    const wasm32_machine_function_t *machine) {
  wasm32_ir_context_t *context = ctx->context;
  if (!machine)
    wasm_unsupported_msg(context, "failed to build Wasm machine function");
  ctx->machine = *machine;
  if (ctx->machine.alloca_count > 0) {
    ctx->allocas = calloc(
        (size_t)ctx->machine.alloca_count, sizeof(*ctx->allocas));
    if (!ctx->allocas)
      diag_emit_internalf_in(
          wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM, "%s",
          diag_message_for_in(
              wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM));
    for (int i = 0; i < ctx->machine.alloca_count; i++)
      ctx->allocas[i].layout = &ctx->machine.allocas[i];
  }
  int vreg_count = ctx->machine.vreg_count;
  if (vreg_count > 0) {
    ctx->vreg_func_ref_names =
        calloc((size_t)vreg_count, sizeof(char *));
    ctx->vreg_func_ref_name_lens =
        calloc((size_t)vreg_count, sizeof(int));
    ctx->vreg_global_refs =
        calloc((size_t)vreg_count, sizeof(ir_symbol_t *));
    ctx->vreg_global_ref_offsets =
        calloc((size_t)vreg_count, sizeof(int));
    ctx->vreg_const_known =
        calloc((size_t)vreg_count, sizeof(unsigned char));
    ctx->vreg_const_values =
        calloc((size_t)vreg_count, sizeof(long long));
  }
  if (vreg_count > 0 &&
      (!ctx->vreg_func_ref_names || !ctx->vreg_func_ref_name_lens ||
       !ctx->vreg_global_refs || !ctx->vreg_global_ref_offsets ||
       !ctx->vreg_const_known || !ctx->vreg_const_values)) {
    diag_emit_internalf_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for_in(wasm32_ir_diagnostics(context), DIAG_ERR_INTERNAL_OOM));
  }
}

static void emit_val_expr(wasm_func_ctx_t *ctx, ir_val_t v) {
  wasm32_ir_context_t *context = ctx->context;
  ir_type_t type = effective_val_type(ctx, v);
  const char *ty = wasm_type(type);
  if (!ty) wasm_unsupported_msg(context, "unsupported Wasm value type");
  if (v.id == IR_VAL_IMM) {
    if (is_fp_type(type)) {
      wasm_cg_emitf("(%s.const %.17g)", ty, v.fp_imm);
    } else if (type == IR_TY_I64) {
      wasm_cg_emitf("(%s.const %lld)", ty, v.imm);
    } else {
      wasm_cg_emitf("(%s.const %d)", ty, (int32_t)v.imm);
    }
  } else if (v.id >= 0) {
    wasm_cg_emitf("(local.get $v%d)", v.id);
  } else {
    wasm_unsupported_msg(context, "missing Wasm value");
  }
}

static wasm32_machine_conversion_t planned_conversion_or_unsupported(
    wasm32_ir_context_t *context,
    ir_type_t source_type, ir_type_t result_type, int is_unsigned) {
  const wasm32_machine_conversion_t *selected =
      wasm32_machine_planned_conversion(
          &g_machine_primitives, source_type, result_type, is_unsigned);
  if (!selected)
    wasm_unsupported_msg(context, "unsupported Wasm value conversion");
  return *selected;
}

static const char *memory_wat_or_unsupported(
    wasm32_ir_context_t *context,
    wasm32_machine_memory_t selected) {
  const char *opcode = wasm32_machine_opcode_wat(selected.opcode);
  if (!opcode) wasm_unsupported_msg(context, "missing Wasm memory opcode");
  return opcode;
}

static void emit_val_expr_as(wasm_func_ctx_t *ctx, ir_val_t v, ir_type_t target) {
  wasm32_ir_context_t *context = ctx->context;
  if (target == IR_TY_PTR) target = IR_TY_I32;
  ir_type_t src = effective_val_type(ctx, v);
  if (src == IR_TY_PTR) src = IR_TY_I32;
  wasm32_machine_conversion_t selected =
      planned_conversion_or_unsupported(
          context, src, target, val_is_unsigned(ctx, v));
  if (selected.opcode == WASM32_MI_COPY) {
    emit_val_expr(ctx, v);
    return;
  }
  if (target == IR_TY_I64 && src == IR_TY_I32 &&
      v.id == IR_VAL_IMM && !is_fp_type(src)) {
    wasm_cg_emitf("(i64.const %lld)", v.imm);
    return;
  }
  const char *opcode = wasm32_machine_opcode_wat(selected.opcode);
  if (!opcode) wasm_unsupported_msg(context, "missing Wasm conversion opcode");
  wasm_cg_emitf("(%s ", opcode);
  emit_val_expr(ctx, v);
  wasm_cg_emitf(")");
}

static void emit_abi_argument_expr_as(
    wasm_func_ctx_t *ctx, const wasm32_machine_argument_t *argument,
    ir_type_t target) {
  wasm32_ir_context_t *context = ctx->context;
  if (!argument)
    wasm_unsupported_msg(context, "missing lowered call argument");
  if (argument->access == WASM32_MACHINE_ARGUMENT_DIRECT) {
    emit_val_expr_as(ctx, argument->source, target);
    return;
  }
  if (argument->access != WASM32_MACHINE_ARGUMENT_LOAD ||
      argument->source.type != IR_TY_PTR) {
    wasm_unsupported_msg(
        context, "unsupported lowered call argument access");
  }
  wasm32_machine_memory_t load = argument->load;
  wasm32_machine_conversion_t conversion =
      planned_conversion_or_unsupported(
          context, load.value_type, target, 1);
  const char *op = memory_wat_or_unsupported(context, load);
  if (conversion.opcode != WASM32_MI_COPY) {
    const char *conversion_op =
        wasm32_machine_opcode_wat(conversion.opcode);
    if (!conversion_op)
      wasm_unsupported_msg(
          context, "missing Wasm ABI argument conversion opcode");
    wasm_cg_emitf("(%s ", conversion_op);
  }
  wasm_cg_emitf("(%s ", op);
  if (argument->byte_offset == 0) {
    emit_val_expr_as(ctx, argument->source, IR_TY_PTR);
  } else {
    wasm_cg_emitf("(i32.add ");
    emit_val_expr_as(ctx, argument->source, IR_TY_PTR);
    wasm_cg_emitf(" (i32.const %d))", argument->byte_offset);
  }
  wasm_cg_emitf(")");
  if (conversion.opcode != WASM32_MI_COPY) wasm_cg_emitf(")");
}

static void emit_wasm_type_cast_prefix(
    wasm_func_ctx_t *ctx,
    ir_type_t from, ir_type_t to, int is_unsigned) {
  wasm32_ir_context_t *context = ctx->context;
  wasm32_machine_conversion_t selected =
      planned_conversion_or_unsupported(context, from, to, is_unsigned);
  if (selected.opcode != WASM32_MI_COPY) {
    const char *opcode = wasm32_machine_opcode_wat(selected.opcode);
    if (!opcode) wasm_unsupported_msg(context, "missing Wasm cast opcode");
    wasm_cg_emitf("(%s ", opcode);
  }
}

static void emit_wasm_type_cast_suffix(
    wasm_func_ctx_t *ctx, ir_type_t from, ir_type_t to) {
  wasm32_ir_context_t *context = ctx->context;
  wasm32_machine_conversion_t selected =
      planned_conversion_or_unsupported(context, from, to, 0);
  if (selected.opcode != WASM32_MI_COPY) wasm_cg_emitf(")");
}

static void emit_addr_expr(wasm_func_ctx_t *ctx, ir_val_t v) {
  emit_val_expr_as(ctx, v, IR_TY_I32);
}

static void emit_addr_plus_const(wasm_func_ctx_t *ctx, ir_val_t base, int off) {
  wasm32_ir_context_t *context = ctx->context;
  if (off == 0) {
    emit_addr_expr(ctx, base);
    return;
  }
  wasm_cg_emitf("(i32.add ");
  emit_addr_expr(ctx, base);
  wasm_cg_emitf(" (i32.const %d))", off);
}

static void emit_load(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *i,
    const wasm32_machine_inst_t *selected, int indent) {
  wasm32_ir_context_t *context = ctx->context;
  wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
  if (selected->kind != WASM32_MACHINE_INST_LOAD)
    wasm_unsupported_op(context, i->op);
  const char *op = memory_wat_or_unsupported(context, selected->load);
  wasm_emitf(indent, "(local.set $v%d (%s ", i->dst.id, op);
  emit_addr_expr(ctx, i->src1);
  wasm_cg_emitf("))\n");
  if (slot && slot->func_ref_name) {
    set_vreg_func_ref(ctx, i->dst.id, slot->func_ref_name, slot->func_ref_name_len);
  } else {
    int global_off = 0;
    const ir_symbol_t *symbol =
        get_vreg_global_ref(ctx, i->src1.id, &global_off);
    int name_len = 0;
    char *name = current_global_func_ref(ctx, symbol, global_off, &name_len);
    if (name) set_vreg_func_ref(ctx, i->dst.id, name, name_len);
  }
}

static void emit_store(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *i,
    const wasm32_machine_inst_t *selected, int indent) {
  wasm32_ir_context_t *context = ctx->context;
  wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
  int global_off = 0;
  const ir_symbol_t *symbol =
      get_vreg_global_ref(ctx, i->src1.id, &global_off);
  if (selected->kind != WASM32_MACHINE_INST_STORE)
    wasm_unsupported_op(context, i->op);
  ir_type_t store_ty = selected->store.value_type;
  const char *op = memory_wat_or_unsupported(context, selected->store);
  wasm_emitf(indent, "(%s ", op);
  emit_addr_expr(ctx, i->src1);
  wasm_cg_emitf(" ");
  emit_val_expr_as(ctx, i->src2, store_ty);
  wasm_cg_emitf(")\n");
  if (slot) {
    int name_len = 0;
    char *name = get_vreg_func_ref(ctx, i->src2.id, &name_len);
    slot->func_ref_name = name;
    slot->func_ref_name_len = name_len;
  }
  if (symbol) {
    int name_len = 0;
    char *name = get_vreg_func_ref(ctx, i->src2.id, &name_len);
    if (ctx->machine.has_control_flow) {
      set_global_func_ref(ctx, symbol, global_off, NULL, 0);
    } else {
      set_global_func_ref(ctx, symbol, global_off, name, name_len);
    }
  }
}

static ir_type_t atomic_value_type(
    const wasm32_machine_inst_t *selected) {
  if (selected->atomic.load.opcode != WASM32_MI_INVALID)
    return selected->atomic.load.value_type;
  return selected->atomic.store.value_type;
}

static const char *atomic_load_op(
    wasm32_ir_context_t *context,
    const wasm32_machine_inst_t *selected) {
  return memory_wat_or_unsupported(context, selected->atomic.load);
}

static const char *atomic_store_op(
    wasm32_ir_context_t *context,
    const wasm32_machine_inst_t *selected) {
  return memory_wat_or_unsupported(context, selected->atomic.store);
}

static void emit_atomic_load_expr(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *i,
    const wasm32_machine_inst_t *selected) {
  wasm32_ir_context_t *context = ctx->context;
  const char *op = atomic_load_op(context, selected);
  if (!op) wasm_unsupported_op(context, i->op);
  wasm_cg_emitf("(%s ", op);
  emit_addr_expr(ctx, i->src1);
  wasm_cg_emitf(")");
}

static void emit_atomic_store(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *i,
    const wasm32_machine_inst_t *selected,
    ir_val_t ptr, ir_val_t val, int indent) {
  wasm32_ir_context_t *context = ctx->context;
  const char *op = atomic_store_op(context, selected);
  if (!op) wasm_unsupported_op(context, i->op);
  wasm_emitf(indent, "(%s ", op);
  emit_addr_expr(ctx, ptr);
  wasm_cg_emitf(" ");
  emit_val_expr_as(ctx, val, atomic_value_type(selected));
  wasm_cg_emitf(")\n");
}

static void emit_atomic_inst(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *i,
    const wasm32_machine_inst_t *selected, int indent) {
  wasm32_ir_context_t *context = ctx->context;
  if (selected->kind != WASM32_MACHINE_INST_ATOMIC)
    wasm_unsupported_op(context, i->op);
  if (selected->atomic.kind == WASM32_MACHINE_ATOMIC_FENCE) {
    wasm_emitf(indent, "(nop)\n");
    return;
  }

  ir_type_t mem_ty = atomic_value_type(selected);
  if (selected->atomic.kind == WASM32_MACHINE_ATOMIC_LOAD) {
    ir_type_t dst_ty = effective_val_type(ctx, i->dst);
    wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
    emit_wasm_type_cast_prefix(ctx, mem_ty, dst_ty, i->is_unsigned);
    emit_atomic_load_expr(ctx, i, selected);
    emit_wasm_type_cast_suffix(ctx, mem_ty, dst_ty);
    wasm_cg_emitf(")\n");
    return;
  }

  if (selected->atomic.kind == WASM32_MACHINE_ATOMIC_STORE) {
    emit_atomic_store(ctx, i, selected, i->src1, i->src2, indent);
    return;
  }

  if (selected->atomic.kind == WASM32_MACHINE_ATOMIC_EXCHANGE ||
      selected->atomic.kind == WASM32_MACHINE_ATOMIC_RMW) {
    ir_type_t dst_ty = effective_val_type(ctx, i->dst);
    wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
    emit_wasm_type_cast_prefix(ctx, mem_ty, dst_ty, i->is_unsigned);
    emit_atomic_load_expr(ctx, i, selected);
    emit_wasm_type_cast_suffix(ctx, mem_ty, dst_ty);
    wasm_cg_emitf(")\n");

    const char *store_op = atomic_store_op(context, selected);
    if (!store_op) wasm_unsupported_op(context, i->op);
    wasm_emitf(indent, "(%s ", store_op);
    emit_addr_expr(ctx, i->src1);
    wasm_cg_emitf(" ");
    if (selected->atomic.kind == WASM32_MACHINE_ATOMIC_EXCHANGE) {
      emit_val_expr_as(ctx, i->src2, mem_ty);
    } else {
      const char *op = wasm32_machine_opcode_wat(
          selected->atomic.binary.opcode);
      if (!op) wasm_unsupported_op(context, i->op);
      wasm_cg_emitf("(%s ", op);
      emit_val_expr_as(ctx, i->dst, mem_ty);
      wasm_cg_emitf(" ");
      emit_val_expr_as(ctx, i->src2, mem_ty);
      wasm_cg_emitf(")");
    }
    wasm_cg_emitf(")\n");
    return;
  }

  if (selected->atomic.kind ==
      WASM32_MACHINE_ATOMIC_COMPARE_EXCHANGE) {
    const char *load_op = atomic_load_op(context, selected);
    const char *store_op = atomic_store_op(context, selected);
    const char *eq_op = wasm32_machine_opcode_wat(
        selected->atomic.comparison.opcode);
    const char *tmp = mem_ty == IR_TY_I64 ? "$atomic_tmp_i64" : "$atomic_tmp_i32";
    const char *exp = mem_ty == IR_TY_I64 ? "$atomic_exp_i64" : "$atomic_exp_i32";
    if (!load_op || !store_op || !eq_op)
      wasm_unsupported_op(context, i->op);

    wasm_emitf(indent, "(local.set %s (%s ", tmp, load_op);
    emit_addr_expr(ctx, i->src1);
    wasm_cg_emitf("))\n");
    wasm_emitf(indent, "(local.set %s (%s ", exp, load_op);
    emit_addr_expr(ctx, i->src2);
    wasm_cg_emitf("))\n");
    wasm_emitf(indent, "(local.set $v%d (%s (local.get %s) (local.get %s)))\n",
               i->dst.id, eq_op, tmp, exp);
    wasm_emitf(indent, "(if (local.get $v%d)\n", i->dst.id);
    wasm_emitf(indent + 2, "(then\n");
    wasm_emitf(indent + 4, "(%s ", store_op);
    emit_addr_expr(ctx, i->src1);
    wasm_cg_emitf(" ");
    emit_val_expr_as(ctx, i->src3, mem_ty);
    wasm_cg_emitf(")\n");
    wasm_emitf(indent + 2, ")\n");
    wasm_emitf(indent, ")\n");
    wasm_emitf(indent, "(%s ", store_op);
    emit_addr_expr(ctx, i->src2);
    wasm_cg_emitf(" (local.get %s))\n", tmp);
    return;
  }

  wasm_unsupported_op(context, i->op);
}

static int uses_ptr_value(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *i) {
  ir_type_t src1 = effective_val_type(ctx, i->src1);
  ir_type_t src2 = effective_val_type(ctx, i->src2);
  return src1 == IR_TY_PTR || src2 == IR_TY_PTR;
}

static int i64_runtime_extension_unsupported(wasm_func_ctx_t *ctx, ir_val_t v) {
  ir_type_t src = effective_val_type(ctx, v);
  if (src == IR_TY_I64) return 0;
  if (v.id >= 0 && (src == IR_TY_I8 || src == IR_TY_I16 || src == IR_TY_I32)) return 0;
  if (v.id == IR_VAL_IMM) return 0;
  return 1;
}

static void emit_memory_copy_chunk(
    wasm_func_ctx_t *ctx, ir_val_t destination, ir_val_t source,
    const wasm32_machine_copy_chunk_t *chunk, int indent) {
  wasm32_ir_context_t *context = ctx->context;
  const char *store = memory_wat_or_unsupported(context, chunk->store);
  const char *load = memory_wat_or_unsupported(context, chunk->load);
  wasm_emitf(indent, "(%s ", store);
  emit_addr_plus_const(ctx, destination, chunk->offset);
  wasm_cg_emitf(" (%s ", load);
  emit_addr_plus_const(ctx, source, chunk->offset);
  wasm_cg_emitf("))\n");
}

static void emit_memcpy(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *i, int indent) {
  for (int chunk = 0; chunk < i->copy.chunk_count; chunk++)
    emit_memory_copy_chunk(
        ctx, i->src1, i->src2, &i->copy.chunks[chunk], indent);
}

static void emit_parameter_copy(
    wasm_func_ctx_t *ctx, ir_val_t destination,
    int parameter_slot, const wasm32_machine_copy_plan_t *plan,
    int indent) {
  wasm32_ir_context_t *context = ctx->context;
  for (int index = 0; index < plan->chunk_count; index++) {
    const wasm32_machine_copy_chunk_t *chunk = &plan->chunks[index];
    const char *store = memory_wat_or_unsupported(context, chunk->store);
    const char *load = memory_wat_or_unsupported(context, chunk->load);
    wasm_emitf(indent, "(%s ", store);
    emit_addr_plus_const(ctx, destination, chunk->offset);
    wasm_cg_emitf(" (%s ", load);
    if (chunk->offset == 0) {
      wasm_cg_emitf("(local.get $p%d)", parameter_slot);
    } else {
      wasm_cg_emitf(
          "(i32.add (local.get $p%d) (i32.const %d))",
          parameter_slot, chunk->offset);
    }
    wasm_cg_emitf("))\n");
  }
}

static void emit_parameter_bind(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *instruction,
    const wasm32_machine_inst_t *selected, int indent) {
  wasm32_ir_context_t *context = ctx->context;
  if (selected->kind != WASM32_MACHINE_INST_PARAMETER_BIND)
    wasm_unsupported_op(context, instruction->op);
  const wasm32_machine_parameter_bind_t *binding =
      &selected->parameter_bind;
  if (!binding->pieces || binding->piece_count == 0 ||
      instruction->src1.type != IR_TY_PTR)
    wasm_unsupported_op(context, instruction->op);
  for (int i = 0; i < binding->piece_count; i++) {
    int parameter_slot = binding->physical_index + i;
    if (binding->pieces[i].kind ==
        WASM32_MACHINE_PARAMETER_INDIRECT) {
      emit_parameter_copy(
          ctx, instruction->src1, parameter_slot,
          &binding->copy_plans[i], indent);
      continue;
    }
    const char *store = memory_wat_or_unsupported(
        context, binding->stores[i]);
    if (!store) wasm_unsupported_op(context, instruction->op);
    wasm_emitf(indent, "(%s ", store);
    emit_addr_plus_const(
        ctx, instruction->src1, binding->pieces[i].byte_offset);
    wasm_cg_emitf(" (local.get $p%d))\n", parameter_slot);
  }
}

static int val_uses_vreg(ir_val_t v, int id) {
  return v.id == id;
}

static int inst_uses_vreg(
    const wasm32_machine_inst_t *planned, int id) {
  const wasm32_machine_inst_t *i = planned;
  if (!i) return 0;
  if (val_uses_vreg(i->src1, id) || val_uses_vreg(i->src2, id) ||
      val_uses_vreg(i->src3, id) || val_uses_vreg(i->callee, id) ||
      val_uses_vreg(i->result_storage, id)) {
    return 1;
  }
  if (planned->kind == WASM32_MACHINE_INST_CALL) {
    for (int a = 0; a < planned->call.argument_count; a++) {
      if (val_uses_vreg(planned->call.arguments[a].source, id)) return 1;
    }
  }
  return 0;
}

static int vreg_used_after(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *from, int id) {
  if (!from || id < 0 || !ctx->machine.instructions) return 0;
  ptrdiff_t index = from - ctx->machine.instructions;
  if (index < 0 || index >= ctx->machine.instruction_count) return 0;
  for (int i = (int)index + 1; i < ctx->machine.instruction_count; i++) {
    if (inst_uses_vreg(&ctx->machine.instructions[i], id)) return 1;
  }
  return 0;
}

static int emit_variadic_arg_area_prepare(
    wasm_func_ctx_t *ctx, const wasm32_machine_call_t *call,
    int indent) {
  wasm32_ir_context_t *context = ctx->context;
  if (!call->is_variadic) return 0;
  const wasm32_machine_argument_t *arguments = call->arguments;
  int bytes = call->variadic_area_size;
  if (bytes <= 0) return 0;
  wasm_emitf(indent, "(local.set $old_va_arg_area (global.get $__ag_va_arg_area))\n");
  wasm_emitf(indent, "(global.set $__stack_pointer (i32.sub (global.get $__stack_pointer) (i32.const %d)))\n",
             bytes);
  wasm_emitf(indent, "(global.set $__ag_va_arg_area (global.get $__stack_pointer))\n");
  for (int i = 0; i < call->variadic_argument_count; i++) {
    const wasm32_machine_variadic_argument_t *variadic =
        &call->variadic_arguments[i];
    const char *store = memory_wat_or_unsupported(
        context, variadic->store);
    const char *conversion =
        wasm32_machine_opcode_wat(variadic->conversion.opcode);
    wasm_emitf(
        indent,
        "(%s (i32.add (global.get $__ag_va_arg_area) (i32.const %d)) ",
        store, variadic->byte_offset);
    if (variadic->conversion.opcode != WASM32_MI_COPY) {
      if (!conversion)
        wasm_unsupported_msg(
            context,
            "missing Wasm variadic argument conversion opcode");
      wasm_cg_emitf("(%s ", conversion);
    }
    emit_abi_argument_expr_as(
        ctx, &arguments[variadic->argument_index],
        variadic->argument_type);
    if (variadic->conversion.opcode != WASM32_MI_COPY)
      wasm_cg_emitf(")");
    wasm_cg_emitf(")\n");
  }
  return bytes;
}

static void emit_variadic_arg_area_restore(
    wasm_func_ctx_t *ctx, int bytes, int indent) {
  wasm32_ir_context_t *context = ctx->context;
  if (bytes <= 0) return;
  wasm_emitf(indent, "(global.set $__stack_pointer (i32.add (global.get $__stack_pointer) (i32.const %d)))\n",
             bytes);
  wasm_emitf(indent, "(global.set $__ag_va_arg_area (local.get $old_va_arg_area))\n");
}

static void emit_call(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *i,
    const wasm32_machine_inst_t *selected, int indent) {
  wasm32_ir_context_t *context = ctx->context;
  if (selected->kind != WASM32_MACHINE_INST_CALL)
    wasm_unsupported_op(context, i->op);
  const wasm32_machine_call_t *call = &selected->call;
  const wasm32_machine_signature_t *call_signature = &call->signature;
  int argument_count = call->argument_count;
  const wasm32_machine_argument_t *arguments = call->arguments;
  int vararg_area_bytes =
      emit_variadic_arg_area_prepare(ctx, call, indent);
  if (i->callee.id != IR_VAL_NONE) {
    g_func_table.needs_table = 1;
    int returns_hidden = call_signature->has_hidden_result;
    int returns_direct_aggregate =
        call_signature->has_direct_aggregate_result;
    if ((returns_hidden || returns_direct_aggregate) &&
        call->result_area.id == IR_VAL_NONE) {
      wasm_unsupported_msg(
          context,
          "indirect aggregate function call without return area in Wasm backend");
    }
    int returns_void = call_signature->result == IR_TY_VOID;
    int result_unused =
        !returns_void && !vreg_used_after(ctx, selected, i->dst.id);
    const char *ret_ty = returns_void ? NULL :
        wasm_any_type_or_unsupported(context, call_signature->result);
    if (returns_direct_aggregate) {
      const char *store =
          memory_wat_or_unsupported(context, call->direct_result_store);
      if (!store) wasm_unsupported_op(context, i->op);
      wasm_emitf(indent, "(%s ", store);
      emit_addr_expr(ctx, call->result_area);
      wasm_cg_emitf(" ");
    } else if (result_unused) wasm_emitf(indent, "(drop ");
    else if (!returns_void) wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
    else wasm_emitf(indent, "");
    wasm_cg_emitf("(call_indirect");
    int call_nargs = call_signature->nparams - (returns_hidden ? 1 : 0);
    if (call_nargs > argument_count)
      wasm_unsupported_msg(
          context,
          "call has fewer lowered arguments than its function type");
    if (returns_hidden) wasm_cg_emitf(" (param i32)");
    for (int a = 0; a < call_nargs; a++) {
      ir_type_t arg_ty =
          call_signature->params[a + (returns_hidden ? 1 : 0)];
      wasm_cg_emitf(
          " (param %s)", wasm_any_type_or_unsupported(context, arg_ty));
    }
    if (!returns_void) wasm_cg_emitf(" (result %s)", ret_ty);
    if (returns_hidden) {
      wasm_cg_emitf(" ");
      emit_val_expr_as(ctx, call->result_area, IR_TY_PTR);
    }
    for (int a = 0; a < call_nargs; a++) {
      ir_type_t arg_ty =
          call_signature->params[a + (returns_hidden ? 1 : 0)];
      wasm_cg_emitf(" ");
      emit_abi_argument_expr_as(ctx, &arguments[a], arg_ty);
    }
    wasm_cg_emitf(" ");
    emit_val_expr_as(ctx, i->callee, IR_TY_I32);
    wasm_cg_emitf(")");
    if (result_unused || !returns_void || returns_direct_aggregate)
      wasm_cg_emitf(")");
    wasm_cg_emitf("\n");
    emit_variadic_arg_area_restore(ctx, vararg_area_bytes, indent);
    return;
  }
  if (!i->sym) wasm_unsupported_op(context, i->op);
  if (i->is_implicit_call) {
    wasm_unsupported_msg(
        context,
        "external or implicitly declared function call in Wasm backend");
  }
  record_function_call_signature(context, i, call);
  int returns_direct_aggregate =
      call_signature->has_direct_aggregate_result;
  int returns_hidden = call_signature->has_hidden_result;
  int returns_void = call_signature->result == IR_TY_VOID;
  if (returns_hidden) {
    wasm_emitf(indent, "(call $%.*s ", i->sym_len, i->sym);
    emit_val_expr_as(ctx, call->result_area, IR_TY_PTR);
  } else if (returns_direct_aggregate) {
    const char *store =
        memory_wat_or_unsupported(context, call->direct_result_store);
    if (!store || call->result_area.id == IR_VAL_NONE)
      wasm_unsupported_op(context, i->op);
    wasm_emitf(indent, "(%s ", store);
    emit_addr_expr(ctx, call->result_area);
    wasm_cg_emitf(" (call $%.*s", i->sym_len, i->sym);
  } else if (!returns_void && i->dst.id >= 0 && i->dst.type != IR_TY_VOID) {
    wasm_emitf(indent, "(local.set $v%d (call $%.*s", i->dst.id, i->sym_len, i->sym);
  } else {
    wasm_emitf(indent, "(call $%.*s", i->sym_len, i->sym);
  }
  int is_minimal_snprintf =
      i->sym_len == 8 && memcmp(i->sym, "snprintf", 8) == 0 &&
      !has_defined_function(context, i->sym, i->sym_len);
  int is_minimal_swprintf =
      i->sym_len == 8 && memcmp(i->sym, "swprintf", 8) == 0 &&
      !has_defined_function(context, i->sym, i->sym_len);
  int is_minimal_printf =
      i->sym_len == 6 && memcmp(i->sym, "printf", 6) == 0 &&
      !has_defined_function(context, i->sym, i->sym_len);
  int is_minimal_fprintf =
      i->sym_len == 7 && memcmp(i->sym, "fprintf", 7) == 0 &&
      !has_defined_function(context, i->sym, i->sym_len);
  int is_minimal_sscanf =
      i->sym_len == 6 && memcmp(i->sym, "sscanf", 6) == 0 &&
      !has_defined_function(context, i->sym, i->sym_len);
  int is_minimal_swscanf =
      i->sym_len == 7 && memcmp(i->sym, "swscanf", 7) == 0 &&
      !has_defined_function(context, i->sym, i->sym_len);
  int is_minimal_fixed2_format = is_minimal_snprintf || is_minimal_swprintf;
  int is_minimal_output_count = is_minimal_printf || is_minimal_fprintf;
  int planned_argument_count =
      call_signature->nparams - (returns_hidden ? 1 : 0);
  int call_nargs = is_minimal_fixed2_format ? 5 :
                   (is_minimal_printf ? 3 :
                    (is_minimal_fprintf ? 4 :
                     ((is_minimal_sscanf || is_minimal_swscanf) ? 4 :
                      planned_argument_count)));
  for (int a = 0; a < call_nargs; a++) {
    if ((is_minimal_fixed2_format || is_minimal_output_count || is_minimal_sscanf || is_minimal_swscanf) &&
        a >= argument_count) {
      wasm_cg_emitf(" (i64.const 0)");
      continue;
    }
    ir_type_t arg_ty = a < planned_argument_count
                           ? call_signature->params[
                                 a + (returns_hidden ? 1 : 0)]
                           : arguments[a].value_type;
    ir_type_t abi_arg_ty = arg_ty;
    if ((is_minimal_fixed2_format || is_minimal_output_count || is_minimal_sscanf || is_minimal_swscanf) &&
        a >= call->fixed_argument_count && is_fp_type(arg_ty)) {
      wasm_cg_emitf(" (i64.const 0)");
      continue;
    }
    int minimal_stub_ptr_arg =
        (i->sym_len == 6 && memcmp(i->sym, "printf", 6) == 0 && a == 0) ||
        (i->sym_len == 7 && memcmp(i->sym, "fprintf", 7) == 0 && (a == 0 || a == 1)) ||
        (i->sym_len == 4 && memcmp(i->sym, "puts", 4) == 0 && a == 0) ||
        (i->sym_len == 6 && memcmp(i->sym, "strlen", 6) == 0 && a == 0) ||
        (is_minimal_fixed2_format && (a == 0 || a == 2)) ||
        (is_minimal_sscanf && (a == 0 || a == 1 || a >= 2)) ||
        (is_minimal_swscanf && (a == 0 || a == 1 || a >= 2));
    int pointer_arg = abi_arg_ty == IR_TY_PTR || arg_ty == IR_TY_PTR;
    if (minimal_stub_ptr_arg ||
        ((is_minimal_sscanf || is_minimal_swscanf) && pointer_arg)) {
      arg_ty = IR_TY_PTR;
    } else if ((is_minimal_fixed2_format || is_minimal_output_count) &&
               a >= call->fixed_argument_count) {
      arg_ty = IR_TY_I64;
    } else if (pointer_arg) {
      arg_ty = IR_TY_PTR;
    } else if (is_minimal_fixed2_format || is_minimal_output_count || is_minimal_sscanf || is_minimal_swscanf) {
      arg_ty = IR_TY_I64;
    } else if (a == 1 &&
               ((i->sym_len == 7 && memcmp(i->sym, "scalbln", 7) == 0) ||
                (i->sym_len == 8 && (memcmp(i->sym, "scalblnf", 8) == 0 ||
                                      memcmp(i->sym, "scalblnl", 8) == 0)))) {
      arg_ty = IR_TY_I64;
    } else if (abi_arg_ty == IR_TY_I64 || arg_ty == IR_TY_I64) {
      arg_ty = IR_TY_I64;
    }
    wasm_cg_emitf(" ");
    emit_abi_argument_expr_as(ctx, &arguments[a], arg_ty);
  }
  wasm_cg_emitf(")");
  if ((!returns_hidden &&
       !returns_void && i->dst.id >= 0 && i->dst.type != IR_TY_VOID) ||
      returns_direct_aggregate) {
    wasm_cg_emitf(")");
  }
  wasm_cg_emitf("\n");
  emit_variadic_arg_area_restore(ctx, vararg_area_bytes, indent);
}

static void emit_indirect_ret_copy(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *instruction,
    ir_val_t source, int indent) {
  wasm32_ir_context_t *context = ctx->context;
  if (!ctx->machine.signature.has_hidden_result ||
      source.type != IR_TY_PTR)
    wasm_unsupported_op(context, instruction->op);
  for (int index = 0;
       index < ctx->machine.result_copy.chunk_count; index++) {
    const wasm32_machine_copy_chunk_t *chunk =
        &ctx->machine.result_copy.chunks[index];
    const char *store = memory_wat_or_unsupported(context, chunk->store);
    const char *load = memory_wat_or_unsupported(context, chunk->load);
    wasm_emitf(indent, "(%s ", store);
    if (chunk->offset == 0)
      wasm_cg_emitf("(local.get $p0)");
    else
      wasm_cg_emitf(
          "(i32.add (local.get $p0) (i32.const %d))",
          chunk->offset);
    wasm_cg_emitf(" (%s ", load);
    emit_addr_plus_const(ctx, source, chunk->offset);
    wasm_cg_emitf("))\n");
  }
}

static void emit_unary(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *instruction,
    const wasm32_machine_inst_t *machine, int indent) {
  wasm32_ir_context_t *context = ctx->context;
  if (machine->kind != WASM32_MACHINE_INST_UNARY)
    wasm_unsupported_op(context, instruction->op);
  const wasm32_machine_unary_t *unary = &machine->unary;
  const char *opcode = wasm32_machine_opcode_wat(unary->opcode);
  const char *type = wasm_any_type_or_unsupported(
      context, unary->operand_type);
  if (!opcode) wasm_unsupported_op(context, instruction->op);
  wasm_emitf(
      indent, "(local.set $v%d (%s ", instruction->dst.id, opcode);
  if (unary->form == WASM32_MI_UNARY_ZERO_THEN_OPERAND) {
    wasm_cg_emitf("(%s.const 0) ", type);
    emit_val_expr_as(ctx, instruction->src1, unary->operand_type);
  } else if (unary->form == WASM32_MI_UNARY_OPERAND_THEN_NEG_ONE) {
    emit_val_expr_as(ctx, instruction->src1, unary->operand_type);
    wasm_cg_emitf(" (%s.const -1)", type);
  } else {
    emit_val_expr_as(ctx, instruction->src1, unary->operand_type);
  }
  wasm_cg_emitf("))\n");
}

static void emit_inst(
    wasm_func_ctx_t *ctx, const wasm32_machine_inst_t *planned,
    int dispatch_mode, int indent) {
  wasm32_ir_context_t *context = ctx->context;
  const wasm32_machine_inst_t *i = planned;
  switch (planned->kind) {
    case WASM32_MACHINE_INST_NOP:
      return;
    case WASM32_MACHINE_INST_PARAMETER_BIND:
      emit_parameter_bind(ctx, i, planned, indent);
      return;
    case WASM32_MACHINE_INST_ALLOCA: {
      int off = find_alloca_offset(ctx, i->dst.id);
      if (off < 0) wasm_unsupported_op(context, i->op);
      wasm_emitf(indent, "(local.set $v%d (i32.add (local.get $fp) (i32.const %d)))\n", i->dst.id, off);
      return;
    }
    case WASM32_MACHINE_INST_INTEGER_CONSTANT:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      emit_val_expr(ctx, i->src1);
      wasm_cg_emitf(")\n");
      if (!is_fp_type(i->dst.type)) set_vreg_const(ctx, i->dst.id, i->src1.imm);
      return;
    case WASM32_MACHINE_INST_FLOAT_CONSTANT:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      emit_val_expr(ctx, i->src1);
      wasm_cg_emitf(")\n");
      return;
    case WASM32_MACHINE_INST_STRING_ADDRESS: {
      int addr = data_addr_for_ir_string(
          context, i->sym, i->sym_len, i->object_size);
      if (addr < 0) wasm_unsupported_op(context, i->op);
      wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, addr);
      return;
    }
    case WASM32_MACHINE_INST_SYMBOL_ADDRESS: {
      if (i->is_function_symbol) {
        record_function_reference_signature(context, i);
        int func_idx = function_table_index_or_unsupported(
            context, i->sym, i->sym_len);
        wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, func_idx);
        set_vreg_func_ref(ctx, i->dst.id, i->sym, i->sym_len);
        return;
      }
      const ir_symbol_t *symbol =
          ir_module_find_symbol(ctx->module, i->sym, i->sym_len);
      int addr = data_addr_for_ir_symbol(context, symbol);
      if (addr < 0)
        wasm_unsupported_msg(
            context, "missing resolved IR global symbol");
      wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, addr);
      set_vreg_global_ref(ctx, i->dst.id, symbol, 0);
      return;
    }
    case WASM32_MACHINE_INST_TLS_ADDRESS: {
      const ir_symbol_t *symbol =
          ir_module_find_symbol(ctx->module, i->sym, i->sym_len);
      int addr = data_addr_for_ir_symbol(context, symbol);
      if (addr < 0)
        wasm_unsupported_msg(
            context, "missing resolved IR TLS symbol");
      wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, addr);
      set_vreg_global_ref(ctx, i->dst.id, symbol, 0);
      return;
    }
    case WASM32_MACHINE_INST_CONVERSION: {
      const wasm32_machine_conversion_t *selected = &planned->conversion;
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      if (selected->opcode != WASM32_MI_COPY) {
        const char *opcode = wasm32_machine_opcode_wat(selected->opcode);
        if (!opcode) wasm_unsupported_op(context, i->op);
        wasm_cg_emitf("(%s ", opcode);
      }
      emit_val_expr(ctx, i->src1);
      if (selected->opcode != WASM32_MI_COPY) wasm_cg_emitf(")");
      wasm_cg_emitf(")\n");
      return;
    }
    case WASM32_MACHINE_INST_LOAD:
      emit_load(ctx, i, planned, indent);
      return;
    case WASM32_MACHINE_INST_STORE:
      emit_store(ctx, i, planned, indent);
      return;
    case WASM32_MACHINE_INST_ATOMIC:
      emit_atomic_inst(ctx, i, planned, indent);
      return;
    case WASM32_MACHINE_INST_MEMORY_COPY:
      emit_memcpy(ctx, i, indent);
      return;
    case WASM32_MACHINE_INST_ALIGN_POINTER: {
      int align = i->alloca_align > 0 ? i->alloca_align : 16;
      wasm_emitf(indent, "(local.set $v%d (i32.and (i32.add ", i->dst.id);
      emit_addr_expr(ctx, i->src1);
      wasm_cg_emitf(" (i32.const %d)) (i32.const %d)))\n", align - 1, -align);
      return;
    }
    case WASM32_MACHINE_INST_DYNAMIC_ALLOCA:
      wasm_emitf(indent, "(local.set $v%d (i32.sub (global.get $__stack_pointer) ", i->dst.id);
      wasm_cg_emitf("(i32.and (i32.add ");
      emit_val_expr_as(ctx, i->src1, IR_TY_I32);
      wasm_cg_emitf(" (i32.const 15)) (i32.const -16))))\n");
      wasm_emitf(indent, "(global.set $__stack_pointer (local.get $v%d))\n", i->dst.id);
      return;
    case WASM32_MACHINE_INST_VARARG_AREA:
      wasm_emitf(indent, "(local.set $v%d (global.get $__ag_va_arg_area))\n", i->dst.id);
      return;
    case WASM32_MACHINE_INST_ADDRESS_ADD:
      wasm_emitf(indent, "(local.set $v%d (i32.add ", i->dst.id);
      emit_addr_expr(ctx, i->src1);
      wasm_cg_emitf(" ");
      emit_addr_expr(ctx, i->src2);
      wasm_cg_emitf("))\n");
      return;
    case WASM32_MACHINE_INST_UNARY:
      emit_unary(ctx, i, planned, indent);
      return;
    case WASM32_MACHINE_INST_BINARY: {
      const wasm32_machine_binary_t *selected = &planned->binary;
      ir_type_t op_ty = selected->operand_type;
      const char *op = wasm32_machine_opcode_wat(selected->opcode);
      if (!op) wasm_unsupported_op(context, i->op);
      if (is_fp_type(op_ty)) {
        wasm_emitf(indent, "(local.set $v%d (%s ", i->dst.id, op);
        emit_val_expr_as(ctx, i->src1, op_ty);
        wasm_cg_emitf(" ");
        emit_val_expr_as(ctx, i->src2, op_ty);
        wasm_cg_emitf("))\n");
        return;
      }
      ir_type_t src1_ty = effective_val_type(ctx, i->src1);
      ir_type_t src2_ty = effective_val_type(ctx, i->src2);
      if (op_ty == IR_TY_I64 &&
          src1_ty != IR_TY_PTR && src2_ty != IR_TY_PTR &&
          (i64_runtime_extension_unsupported(ctx, i->src1) ||
           (!selected->is_shift &&
            i64_runtime_extension_unsupported(ctx, i->src2)))) {
        wasm_unsupported_msg(
            context, "runtime i32 to i64 extension in Wasm backend");
      }
      ir_type_t dst_ty = effective_val_type(ctx, i->dst);
      ir_type_t result_ty = selected->result_type;
      int result_unsigned = uses_ptr_value(ctx, i) ||
                            selected->is_unsigned;
      if (selected->guard_zero_divisor) {
        const char *type = wasm_any_type_or_unsupported(context, op_ty);
        wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
        emit_wasm_type_cast_prefix(
            ctx, result_ty, dst_ty, result_unsigned);
        wasm_cg_emitf("(if (result %s) (%s.eqz ", type, type);
        emit_val_expr_as(ctx, i->src2, op_ty);
        wasm_cg_emitf(") (then ");
        emit_val_expr_as(ctx, i->src1, op_ty);
        wasm_cg_emitf(") (else (%s ", op);
        emit_val_expr_as(ctx, i->src1, op_ty);
        wasm_cg_emitf(" ");
        emit_val_expr_as(ctx, i->src2, op_ty);
        wasm_cg_emitf(")))");
        emit_wasm_type_cast_suffix(ctx, result_ty, dst_ty);
        wasm_cg_emitf(")\n");
        return;
      }
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      emit_wasm_type_cast_prefix(
          ctx, result_ty, dst_ty, result_unsigned);
      wasm_cg_emitf("(%s ", op);
      emit_val_expr_as(ctx, i->src1, op_ty);
      wasm_cg_emitf(" ");
      emit_val_expr_as(ctx, i->src2, op_ty);
      wasm_cg_emitf(")");
      emit_wasm_type_cast_suffix(ctx, result_ty, dst_ty);
      wasm_cg_emitf(")\n");
      if (selected->tracks_address) {
        int base_off = 0;
        const ir_symbol_t *symbol =
            get_vreg_global_ref(ctx, i->src1.id, &base_off);
        long long delta = 0;
        if (symbol && get_vreg_const(ctx, i->src2, &delta)) {
          if (selected->subtracts_address)
            delta = -delta;
          set_vreg_global_ref(ctx, i->dst.id, symbol, base_off + (int)delta);
        } else if (!selected->subtracts_address &&
                   get_vreg_const(ctx, i->src1, &delta)) {
          symbol = get_vreg_global_ref(ctx, i->src2.id, &base_off);
          if (symbol)
            set_vreg_global_ref(ctx, i->dst.id, symbol,
                                base_off + (int)delta);
        }
      }
      return;
    }
    case WASM32_MACHINE_INST_CALL:
      emit_call(ctx, i, planned, indent);
      return;
    case WASM32_MACHINE_INST_CONTROL:
      switch (planned->control.kind) {
        case WASM32_MACHINE_CONTROL_LABEL:
          if (!dispatch_mode) wasm_unsupported_op(context, i->op);
          return;
        case WASM32_MACHINE_CONTROL_BRANCH:
          if (!dispatch_mode) wasm_unsupported_op(context, i->op);
          wasm_emitf(
              indent, "(local.set $pc (i32.const %d))\n",
              planned->control.target_block_id);
          wasm_emitf(indent, "(br $dispatch)\n");
          return;
        case WASM32_MACHINE_CONTROL_BRANCH_CONDITIONAL:
          if (!dispatch_mode) wasm_unsupported_op(context, i->op);
          wasm_emitf(indent, "(if ");
          emit_val_expr_as(ctx, planned->control.value, IR_TY_I32);
          wasm_cg_emitf("\n");
          wasm_emitf(
              indent + 2, "(then (local.set $pc (i32.const %d)))\n",
              planned->control.target_block_id);
          wasm_emitf(
              indent + 2, "(else (local.set $pc (i32.const %d)))\n",
              planned->control.else_block_id);
          wasm_emitf(indent, ")\n");
          wasm_emitf(indent, "(br $dispatch)\n");
          return;
        case WASM32_MACHINE_CONTROL_RETURN: {
          ir_val_t result = planned->control.value;
          if (ctx->machine.signature.has_hidden_result) {
            emit_indirect_ret_copy(ctx, i, result, indent);
            if (ctx->machine.frame_size > 0)
              wasm_emitf(indent, "(global.set $__stack_pointer (local.get $old_sp))\n");
            wasm_emitf(indent, "return\n");
            return;
          }
          if (ctx->machine.frame_size > 0)
            wasm_emitf(indent, "(global.set $__stack_pointer (local.get $old_sp))\n");
          if (ctx->machine.signature.has_direct_aggregate_result) {
            const char *load = memory_wat_or_unsupported(
                context, ctx->machine.direct_result_load);
            if (!load || result.type != IR_TY_PTR)
              wasm_unsupported_op(context, i->op);
            wasm_emitf(indent, "(return (%s ", load);
            emit_addr_expr(ctx, result);
            wasm_cg_emitf("))\n");
          } else if (result.id != IR_VAL_NONE) {
            wasm_emitf(indent, "(return ");
            emit_val_expr_as(
                ctx, result, ctx->machine.direct_result_type);
            wasm_cg_emitf(")\n");
          } else {
            wasm_emitf(indent, "return\n");
          }
          return;
        }
        case WASM32_MACHINE_CONTROL_SUSPEND:
          wasm_unsupported_op(context, i->op);
          return;
        default:
          wasm_unsupported_op(context, i->op);
          return;
      }
    default:
      wasm_unsupported_op(context, i->op);
  }
}

static void emit_func(
    wasm32_ir_context_t *context,
    const ir_module_t *module, ir_func_t *f,
    const wasm32_machine_function_t *machine) {
  wasm_func_ctx_t ctx = {0};
  ctx.context = context;
  ctx.module = module;
  ctx.f = f;
  analyze_func(&ctx, machine);

  const wasm32_machine_signature_t *function_signature =
      &ctx.machine.signature;
  int nparams = function_signature->nparams;
  wasm_emitf(2, "(func $%.*s", f->name_len, f->name);
  for (int p = 0; p < nparams; p++) {
    const char *pt = wasm_type(function_signature->params[p]);
    if (!pt)
      wasm_unsupported_msg(
          context, "non-integer Wasm function parameter");
    wasm_cg_emitf(" (param $p%d %s)", p, pt);
  }
  if (function_signature->result != IR_TY_VOID) {
    const char *rt = wasm_type(function_signature->result);
    if (!rt)
      wasm_unsupported_msg(context, "non-integer Wasm function return");
    wasm_cg_emitf(" (result %s)", rt);
  }
  wasm_cg_emitf("\n");
  for (int v = 0; v < ctx.machine.vreg_count; v++) {
    if (!ctx.machine.vreg_used[v]) continue;
    const char *vt = wasm_type(ctx.machine.vreg_types[v]);
    if (vt) wasm_emitf(4, "(local $v%d %s)\n", v, vt);
  }
  wasm_emitf(4, "(local $fp i32)\n");
  wasm_emitf(4, "(local $old_sp i32)\n");
  wasm_emitf(4, "(local $old_va_arg_area i32)\n");
  wasm_emitf(4, "(local $atomic_tmp_i32 i32)\n");
  wasm_emitf(4, "(local $atomic_exp_i32 i32)\n");
  wasm_emitf(4, "(local $atomic_tmp_i64 i64)\n");
  wasm_emitf(4, "(local $atomic_exp_i64 i64)\n");
  if (ctx.machine.has_control_flow) wasm_emitf(4, "(local $pc i32)\n");
  if (ctx.machine.frame_size > 0) {
    wasm_emitf(4, "(local.set $old_sp (global.get $__stack_pointer))\n");
    wasm_emitf(4, "(local.set $fp (i32.sub (global.get $__stack_pointer) (i32.const %d)))\n",
               ctx.machine.frame_size);
    wasm_emitf(4, "(global.set $__stack_pointer (local.get $fp))\n");
  }
  if (ctx.machine.has_control_flow) {
    int entry_id = ctx.machine.block_count > 0
                       ? ctx.machine.blocks[0].id
                       : 0;
    wasm_emitf(
        4, "(local.set $pc (i32.const %d))\n", entry_id);
    wasm_emitf(4, "(block $exit\n");
    wasm_emitf(6, "(loop $dispatch\n");
    for (int block_index = 0;
         block_index < ctx.machine.block_count; block_index++) {
      const wasm32_machine_block_t *block =
          &ctx.machine.blocks[block_index];
      wasm_emitf(
          8, "(if (i32.eq (local.get $pc) (i32.const %d))\n",
          block->id);
      wasm_emitf(10, "(then\n");
      for (int i = 0; i < block->instruction_count; i++) {
        const wasm32_machine_inst_t *instruction =
            &ctx.machine.instructions[block->first_instruction + i];
        emit_inst(&ctx, instruction, 1, 12);
      }
      if (!block->has_terminator) {
        if (block->next_block_id >= 0) {
          wasm_emitf(
              12, "(local.set $pc (i32.const %d))\n",
              block->next_block_id);
          wasm_emitf(12, "(br $dispatch)\n");
        } else {
          wasm_emitf(12, "(br $exit)\n");
        }
      }
      wasm_emitf(10, ")\n");
      wasm_emitf(8, ")\n");
    }
    wasm_emitf(8, "(br $exit)\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "unreachable\n");
  } else {
    for (int block_index = 0;
         block_index < ctx.machine.block_count; block_index++) {
      const wasm32_machine_block_t *block =
          &ctx.machine.blocks[block_index];
      for (int i = 0; i < block->instruction_count; i++) {
        const wasm32_machine_inst_t *instruction =
            &ctx.machine.instructions[block->first_instruction + i];
        emit_inst(&ctx, instruction, 0, 4);
      }
    }
  }
  wasm_emitf(2, ")\n");
  if (f->name_len == 4 && memcmp(f->name, "main", 4) == 0) {
    wasm_emitf(2, "(export \"main\" (func $main))\n");
  }

  free(ctx.allocas);
  free(ctx.global_func_states);
  free(ctx.vreg_func_ref_names);
  free(ctx.vreg_func_ref_name_lens);
  free(ctx.vreg_global_refs);
  free(ctx.vreg_global_ref_offsets);
  free(ctx.vreg_const_known);
  free(ctx.vreg_const_values);
}

void wasm32_module_begin_in(wasm32_ir_context_t *ctx) {
  if (!ctx) abort();
  wasm32_ir_context_t *context = ctx;
  if (!wasm32_machine_primitive_plan_build(&g_machine_primitives))
    wasm_unsupported_msg(
        context, "failed to build Wasm Machine primitive plan");
  for (int i = 0; i < g_data.symbol_count; i++)
    free(g_data.symbols[i].name);
  for (int i = 0; i < g_func_table.ref_count; i++)
    free(g_func_table.refs[i].name);
  g_data.next_data_off = WASM_STATIC_BASE;
  g_data.symbol_count = 0;
  g_func_table.ref_count = 0;
  g_func_table.needs_table = 0;
  reset_function_symbols(context);
  wasm_cg_emitf("(module\n");
}

void wasm32_gen_machine_module_in(
    wasm32_ir_context_t *ctx,
    const wasm32_machine_module_t *machine_module) {
  if (!ctx) abort();
  wasm32_ir_context_t *context = ctx;
  if (!machine_module || !machine_module->source)
    wasm_unsupported_msg(context, "failed to build Wasm machine module");
  const ir_module_t *module = machine_module->source;
  size_t index = 0;
  for (ir_func_t *function = module->funcs; function;
       function = function->next, index++) {
    const wasm32_machine_function_t *machine =
        wasm32_machine_module_function(machine_module, index);
    if (!machine)
      wasm_unsupported_msg(context, "incomplete Wasm machine module");
    register_function_definition(context, function, machine);
  }
  index = 0;
  for (ir_func_t *function = module->funcs; function;
       function = function->next, index++)
    emit_func(
        context, module, function,
        wasm32_machine_module_function(machine_module, index));
}

static void emit_wat_escaped_byte(
    wasm32_ir_context_t *context, unsigned char c) {
  if (c == '"' || c == '\\') {
    wasm_cg_emitf("\\%02x", (unsigned)c);
  } else if (c >= 0x20 && c <= 0x7e) {
    wasm_cg_emitf("%c", c);
  } else {
    wasm_cg_emitf("\\%02x", (unsigned)c);
  }
}

static void emit_string_literal_data(
    wasm32_ir_context_t *context, const ir_data_object_t *object) {
  int addr = data_addr_for_string_label(context, object->name);
  if (addr < 0)
    wasm_unsupported_msg(
        context, "string literal label in Wasm backend");
  wasm_emitf(2, "(data (i32.const %d) \"", addr);
  for (int i = 0; i < object->byte_size; i++)
    emit_wat_escaped_byte(context, object->bytes[i]);
  wasm_cg_emitf("\")\n");
}

void emit_i32_data_bytes(
    wasm32_ir_context_t *context,
    int addr, long long value, int size) {
  wasm_emitf(2, "(data (i32.const %d) \"", addr);
  for (int i = 0; i < size; i++)
    emit_wat_escaped_byte(
        context, (unsigned char)((uint64_t)value >> (8 * i)));
  wasm_cg_emitf("\")\n");
}

static long long data_relocation_value(
    wasm32_ir_context_t *context, const ir_data_reloc_t *reloc) {
  if (!reloc) return 0;
  if (reloc->kind == IR_DATA_RELOC_FUNCTION) {
    const ir_abi_signature_t *abi =
        ir_abi_data_relocation_signature(g_data_abi, reloc);
    wasm32_machine_signature_t signature;
    if (!wasm32_machine_signature_from_abi(abi, 1, &signature))
      wasm_unsupported_msg(
          context,
          "function data relocation without ABI lowering result");
    record_function_signature(
        context, reloc->target, reloc->target_len, &signature);
    wasm32_machine_signature_dispose(&signature);
    int index = function_table_index_or_unsupported(
        context, reloc->target, reloc->target_len);
    return (long long)index + reloc->addend;
  }
  ir_data_object_t *target = ir_data_module_find_object(
      g_data_module, reloc->target, reloc->target_len);
  int addr = data_addr_for_data_object(context, target);
  if (addr < 0)
    wasm_unsupported_msg(context, "missing data relocation target");
  return (long long)addr + reloc->addend;
}

static void emit_global_data_object(
    wasm32_ir_context_t *context, const ir_data_object_t *object) {
  if (!object || object->is_extern) return;
  if (!object->has_explicit_initializer && object->byte_size != 1 &&
      object->byte_size != 2 && object->byte_size != 4 &&
      object->byte_size != 8)
    return;
  int addr = data_addr_for_data_object(context, object);
  if (addr < 0 || (object->has_explicit_initializer && !object->bytes))
    wasm_unsupported_msg(
        context, "missing lowered global data in Wasm backend");
  wasm_emitf(2, "(data (i32.const %d) \"", addr);
  const ir_data_reloc_t *reloc = object->relocs;
  for (int offset = 0; offset < object->byte_size; ) {
    if (reloc && reloc->offset == offset) {
      if (reloc->width <= 0 || offset > object->byte_size - reloc->width)
        wasm_unsupported_msg(context, "global data relocation range");
      uint64_t value = (uint64_t)data_relocation_value(context, reloc);
      for (int i = 0; i < reloc->width; i++)
        emit_wat_escaped_byte(
            context, (unsigned char)(value >> (8 * i)));
      offset += reloc->width;
      reloc = reloc->next;
    } else {
      if (reloc && reloc->offset < offset)
        wasm_unsupported_msg(
            context, "overlapping global data relocations");
      emit_wat_escaped_byte(
          context, object->bytes ? object->bytes[offset] : 0);
      offset++;
    }
  }
  if (reloc)
    wasm_unsupported_msg(
        context, "global data relocation outside object");
  wasm_cg_emitf("\")\n");
}

void wasm32_emit_data_segments_in(
    wasm32_ir_context_t *ctx,
    const ir_data_module_t *data_module,
    const ir_abi_data_module_t *data_abi) {
  if (!ctx) abort();
  wasm32_ir_context_t *context = ctx;
  g_data_module = data_module;
  g_data_abi = data_abi;
  for (const ir_data_object_t *object = data_module ? data_module->objects : NULL;
       object; object = object->next) {
    if (object->kind == IR_DATA_STRING)
      emit_string_literal_data(context, object);
    else if (object->kind == IR_DATA_OBJECT)
      emit_global_data_object(context, object);
  }
}

int has_undefined_function(
    wasm32_ir_context_t *context, const char *name, int len) {
  wasm_function_symbol_t *symbol = function_symbol_state(
      context, name, len, 0);
  return symbol && symbol->referenced && !symbol->defined;
}

int has_defined_function(
    wasm32_ir_context_t *context, const char *name, int len) {
  wasm_function_symbol_t *symbol = function_symbol_state(
      context, name, len, 0);
  return symbol && symbol->defined;
}


static int wasm_align_up_int(int value, int align) {
  if (align <= 0) return value;
  int rem = value % align;
  if (rem == 0) return value;
  return value + (align - rem);
}

static void emit_memory_layout_decls(wasm32_ir_context_t *context) {
  int data_end = g_data.next_data_off;
  int heap_base = data_end > WASM_HEAP_BASE ? wasm_align_up_int(data_end, 8) : WASM_HEAP_BASE;
  int pages = 1;
  int stack_base = WASM_STACK_BASE;
  if (heap_base != WASM_HEAP_BASE) {
    int required_bytes = heap_base + WASM_PAGE_SIZE;
    pages = (required_bytes + WASM_PAGE_SIZE - 1) / WASM_PAGE_SIZE;
    if (pages < 1) pages = 1;
    stack_base = pages * WASM_PAGE_SIZE;
  }

  wasm_emitf(2, "(memory (export \"memory\") %d)\n", pages);
  wasm_emitf(2, "(global $__stack_pointer (mut i32) (i32.const %d))\n", stack_base);
  wasm_emitf(2, "(global $__ag_va_arg_area (mut i32) (i32.const 0))\n");
  wasm_emitf(2, "(global $__ag_heap_pointer (mut i32) (i32.const %d))\n", heap_base);
}

void wasm32_module_end_in(wasm32_ir_context_t *ctx) {
  if (!ctx) abort();
  wasm32_ir_context_t *context = ctx;
  wasm32_wat_emit_minimal_libc_stubs(context);
  emit_function_table(context);
  emit_memory_layout_decls(context);
  wasm_cg_emitf(")\n");
}
