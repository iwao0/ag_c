#include "wasm32_ir.h"
#include "../codegen_emit.h"
#include "../diag/diag.h"
#include "../parser/parser_public.h"
#include "../tokenizer/literals.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WASM_PAGE_SIZE 65536
#define WASM_STATIC_BASE 1024
#define WASM_STACK_BASE WASM_PAGE_SIZE
#define WASM_HEAP_BASE 32768

typedef struct {
  int vreg;
  int offset;
  int size;
  ir_type_t value_type;
  char *func_ref_name;
  int func_ref_name_len;
} wasm_alloca_slot_t;

typedef struct {
  global_var_t *gv;
  int offset;
  char *func_ref_name;
  int func_ref_name_len;
  int is_set;
} wasm_global_func_state_t;

typedef struct {
  ir_func_t *f;
  wasm_alloca_slot_t *allocas;
  int alloca_count;
  int alloca_cap;
  wasm_global_func_state_t *global_func_states;
  int global_func_state_count;
  int global_func_state_cap;
  int frame_size;
  int *vreg_type_seen;
  ir_type_t *vreg_types;
  unsigned char *vreg_unsigned;
  char **vreg_func_ref_names;
  int *vreg_func_ref_name_lens;
  global_var_t **vreg_global_refs;
  int *vreg_global_ref_offsets;
  unsigned char *vreg_const_known;
  long long *vreg_const_values;
  int has_control_flow;
} wasm_func_ctx_t;

typedef struct {
  char *name;
  int name_len;
  int addr;
  int size;
} wasm_data_symbol_t;

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

static wasm_data_ctx_t g_data = {WASM_STATIC_BASE, NULL, 0, 0};
static wasm_func_table_ctx_t g_func_table = {NULL, 0, 0, 0};
static const char k_wasm_indent_spaces[] = "                                ";

static void wasm_emit_indent(int spaces) {
  int chunk = (int)sizeof(k_wasm_indent_spaces) - 1;
  while (spaces > chunk) {
    cg_emitf("%s", k_wasm_indent_spaces);
    spaces -= chunk;
  }
  if (spaces > 0) cg_emitf("%.*s", spaces, k_wasm_indent_spaces);
}

#define wasm_emitf(spaces, ...)       \
  do {                                \
    wasm_emit_indent((spaces));       \
    cg_emitf(__VA_ARGS__);            \
  } while (0)

static const char *wasm_type(ir_type_t t) {
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

static void wasm_unsupported_msg(const char *msg);
static int has_undefined_function(const char *name, int len);

static int is_fp_type(ir_type_t t) {
  return t == IR_TY_F32 || t == IR_TY_F64;
}

static const char *wasm_fp_type_or_unsupported(ir_type_t t) {
  const char *ty = wasm_type(t);
  if (!ty || !is_fp_type(t)) wasm_unsupported_msg("unsupported Wasm floating-point type");
  return ty;
}

static const char *wasm_int_type_or_unsupported(ir_type_t t) {
  const char *ty = wasm_type(t);
  if (!ty || is_fp_type(t)) wasm_unsupported_msg("unsupported Wasm integer type");
  return ty;
}

static const char *wasm_any_type_or_unsupported(ir_type_t t) {
  const char *ty = wasm_type(t);
  if (!ty) wasm_unsupported_msg("unsupported Wasm value type");
  return ty;
}

static int align_to(int value, int align) {
  if (align <= 1) return value;
  int mask = align - 1;
  return (value + mask) & ~mask;
}

static void wasm_unsupported_op(ir_op_t op) {
  diag_emit_internalf(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
                      diag_message_for(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
                      ir_op_name(op));
}

static void wasm_unsupported_msg(const char *msg) {
  diag_emit_internalf(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
                      diag_message_for(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
                      msg);
}

static int name_eq(const char *a, int alen, const char *b, int blen) {
  return alen == blen && a && b && memcmp(a, b, (size_t)alen) == 0;
}

static wasm_data_symbol_t *find_data_symbol(const char *name, int name_len) {
  for (int i = 0; i < g_data.symbol_count; i++) {
    if (name_eq(g_data.symbols[i].name, g_data.symbols[i].name_len, name, name_len)) {
      return &g_data.symbols[i];
    }
  }
  return NULL;
}

static wasm_data_symbol_t *intern_data_symbol(const char *name, int name_len, int size, int align) {
  wasm_data_symbol_t *existing = find_data_symbol(name, name_len);
  if (existing) return existing;
  if (g_data.symbol_count == g_data.symbol_cap) {
    int ncap = g_data.symbol_cap ? g_data.symbol_cap * 2 : 32;
    wasm_data_symbol_t *n = realloc(g_data.symbols, (size_t)ncap * sizeof(*n));
    if (!n) diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    g_data.symbols = n;
    g_data.symbol_cap = ncap;
  }
  g_data.next_data_off = align_to(g_data.next_data_off, align > 0 ? align : 1);
  wasm_data_symbol_t *s = &g_data.symbols[g_data.symbol_count++];
  s->name = (char *)name;
  s->name_len = name_len;
  s->addr = g_data.next_data_off;
  s->size = size > 0 ? size : 1;
  g_data.next_data_off += s->size;
  return s;
}

typedef struct {
  const char *label;
  int label_len;
  string_lit_t *found;
} string_find_ctx_t;

static void find_string_lit_cb(string_lit_t *lit, void *user) {
  string_find_ctx_t *ctx = user;
  psx_string_lit_view_t view = psx_string_lit_view(lit);
  if (!ctx->found && view.label &&
      name_eq(view.label, (int)strlen(view.label), ctx->label, ctx->label_len)) {
    ctx->found = lit;
  }
}

static int narrow_string_encoded_size(string_lit_t *lit) {
  if (!lit) return 1;
  psx_string_lit_view_t view = psx_string_lit_view(lit);
  return tk_emit_string_literal_bytes(view.str, view.len, (int)view.char_width,
                                      true, NULL, NULL);
}

static int data_addr_for_string_label(const char *sym) {
  if (!sym) return -1;
  string_find_ctx_t ctx = {sym, (int)strlen(sym), NULL};
  ps_iter_string_literals(find_string_lit_cb, &ctx);
  int size = narrow_string_encoded_size(ctx.found);
  return intern_data_symbol(sym, ctx.label_len, size, 1)->addr;
}

typedef struct {
  const char *name;
  int name_len;
  global_var_t *found;
} global_find_ctx_t;

static void find_global_cb(global_var_t *gv, void *user) {
  global_find_ctx_t *ctx = user;
  if (!ctx->found &&
      name_eq(psx_gvar_name(gv), psx_gvar_name_len(gv), ctx->name, ctx->name_len)) {
    ctx->found = gv;
  }
}

static int data_addr_for_global(const char *sym, int sym_len) {
  global_find_ctx_t ctx = {sym, sym_len, NULL};
  ps_iter_globals(find_global_cb, &ctx);
  int size = psx_gvar_storage_size(ctx.found, 8);
  int align = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
  return intern_data_symbol(sym, sym_len, size, align)->addr;
}

static int intern_function_table_ref(char *name, int name_len) {
  if (!name || name_len <= 0) return -1;
  for (int i = 0; i < g_func_table.ref_count; i++) {
    if (name_eq(g_func_table.refs[i].name, g_func_table.refs[i].name_len, name, name_len)) return i + 2;
  }
  if (g_func_table.ref_count == g_func_table.ref_cap) {
    int ncap = g_func_table.ref_cap ? g_func_table.ref_cap * 2 : 16;
    wasm_func_ref_t *n = realloc(g_func_table.refs, (size_t)ncap * sizeof(*n));
    if (!n) diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    g_func_table.refs = n;
    g_func_table.ref_cap = ncap;
  }
  int idx = g_func_table.ref_count++;
  g_func_table.refs[idx].name = name;
  g_func_table.refs[idx].name_len = name_len;
  return idx + 2;
}

static int function_table_index_or_unsupported(char *name, int name_len) {
  if (!psx_ctx_has_function_name(name, name_len)) return -1;
  return intern_function_table_ref(name, name_len);
}

static ir_type_t funcptr_int_mask_type(unsigned iw) {
  return iw == 2 ? IR_TY_I64 : IR_TY_I32;
}

static ir_type_t funcptr_param_type_from_inst(const ir_inst_t *i, int idx, ir_type_t fallback) {
  if (!i || !i->has_funcptr_sig || idx < 0 || idx >= 8) return fallback;
  psx_decl_funcptr_sig_t fs = i->funcptr_sig;
  unsigned fp = (fs.param_fp_mask >> (2 * idx)) & 3u;
  unsigned iw = (fs.param_int_mask >> (2 * idx)) & 3u;
  if (fp == TK_FLOAT_KIND_FLOAT) return IR_TY_F32;
  if (fp >= TK_FLOAT_KIND_DOUBLE) return IR_TY_F64;
  if (iw != 0) {
    ir_type_t ty = funcptr_int_mask_type(iw);
    if (ty == IR_TY_I32 && iw != 3 && fallback != IR_TY_PTR && !is_fp_type(fallback)) return IR_TY_I64;
    return ty;
  }
  return fallback;
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

static void emit_function_table(void) {
  if (g_func_table.ref_count <= 0) {
    if (g_func_table.needs_table) wasm_emitf(2, "(table 2 funcref)\n");
    return;
  }
  for (int i = 0; i < g_func_table.ref_count; i++) {
    char *name = g_func_table.refs[i].name;
    int name_len = g_func_table.refs[i].name_len;
    if (!psx_ctx_is_function_defined(name, name_len) &&
        !(has_undefined_function(name, name_len) && has_minimal_libc_stub_function(name, name_len))) {
      wasm_unsupported_msg("external function pointer in Wasm backend");
    }
  }
  wasm_emitf(2, "(table %d funcref)\n", g_func_table.ref_count + 2);
  wasm_emitf(2, "(elem (i32.const 2)");
  for (int i = 0; i < g_func_table.ref_count; i++) {
    cg_emitf(" $%.*s", g_func_table.refs[i].name_len, g_func_table.refs[i].name);
  }
  cg_emitf(")\n");
}

static ir_type_t func_param_type_from_decl(ir_func_t *f, int idx, ir_type_t raw) {
  if (psx_ctx_get_function_param_category(f->name, f->name_len, idx) == PSX_PCAT_STRUCT) {
    return IR_TY_PTR;
  }
  if (raw != IR_TY_PTR && psx_ctx_get_function_param_int_size(f->name, f->name_len, idx) == 8) {
    return IR_TY_I64;
  }
  return raw;
}

static int func_has_hidden_ret_area(ir_func_t *f) {
  return f && (f->ret_struct_size > 0 || f->ret_complex_half > 0);
}

static int func_param_ordinal_for_inst(ir_func_t *f, ir_inst_t *target) {
  int ordinal = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op != IR_PARAM || i->src1.id != IR_VAL_IMM || i->src1.imm < 0) continue;
      if (i == target) return ordinal;
      ordinal++;
    }
  }
  return -1;
}

static ir_inst_t *func_param_inst_at_ordinal(ir_func_t *f, int ordinal) {
  if (ordinal < 0) return NULL;
  int cur = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op != IR_PARAM || i->src1.id != IR_VAL_IMM || i->src1.imm < 0) continue;
      if (cur == ordinal) return i;
      cur++;
    }
  }
  return NULL;
}

static void collect_vreg_type_as(wasm_func_ctx_t *ctx, ir_val_t v, ir_type_t type) {
  if (v.id < 0 || v.id >= ctx->f->next_vreg_id) return;
  if (!wasm_type(type)) return;
  if (ctx->vreg_type_seen[v.id]) return;
  ctx->vreg_type_seen[v.id] = 1;
  ctx->vreg_types[v.id] = type;
}

static void collect_vreg_type(wasm_func_ctx_t *ctx, ir_val_t v) {
  collect_vreg_type_as(ctx, v, v.type);
}

static void collect_inst_vregs(wasm_func_ctx_t *ctx, ir_inst_t *i) {
  if (i->op == IR_PARAM && i->src1.id == IR_VAL_IMM) {
    int ordinal = func_param_ordinal_for_inst(ctx->f, i);
    ir_type_t ty = (i->src1.imm < 0) ? IR_TY_PTR
                                     : func_param_type_from_decl(ctx->f, ordinal, i->dst.type);
    collect_vreg_type_as(ctx, i->dst, ty);
    return;
  }
  collect_vreg_type(ctx, i->dst);
  collect_vreg_type(ctx, i->src1);
  collect_vreg_type(ctx, i->src2);
  collect_vreg_type(ctx, i->src3);
  collect_vreg_type(ctx, i->ret_struct_area);
  collect_vreg_type(ctx, i->callee);
  for (int a = 0; a < i->nargs; a++) collect_vreg_type(ctx, i->args[a]);
}

static void add_alloca_slot(wasm_func_ctx_t *ctx, ir_inst_t *i) {
  if (ctx->alloca_count == ctx->alloca_cap) {
    int ncap = ctx->alloca_cap ? ctx->alloca_cap * 2 : 16;
    wasm_alloca_slot_t *n = realloc(ctx->allocas, (size_t)ncap * sizeof(*n));
    if (!n) diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    ctx->allocas = n;
    ctx->alloca_cap = ncap;
  }
  int align = i->alloca_align > 0 ? i->alloca_align : 4;
  ctx->frame_size = align_to(ctx->frame_size, align);
  ctx->allocas[ctx->alloca_count].vreg = i->dst.id;
  ctx->allocas[ctx->alloca_count].offset = ctx->frame_size;
  ctx->allocas[ctx->alloca_count].size = i->alloca_size;
  ctx->allocas[ctx->alloca_count].value_type = IR_TY_VOID;
  ctx->alloca_count++;
  ctx->frame_size += i->alloca_size;
}

static wasm_alloca_slot_t *find_alloca_slot(wasm_func_ctx_t *ctx, int vreg) {
  for (int i = 0; i < ctx->alloca_count; i++) {
    if (ctx->allocas[i].vreg == vreg) return &ctx->allocas[i];
  }
  return NULL;
}

static int find_alloca_offset(wasm_func_ctx_t *ctx, int vreg) {
  wasm_alloca_slot_t *slot = find_alloca_slot(ctx, vreg);
  if (slot) return slot->offset;
  return -1;
}

static ir_type_t effective_val_type(wasm_func_ctx_t *ctx, ir_val_t v) {
  if (v.id >= 0 && v.id < ctx->f->next_vreg_id && ctx->vreg_type_seen[v.id]) {
    return ctx->vreg_types[v.id];
  }
  return v.type;
}

static void set_vreg_type(wasm_func_ctx_t *ctx, ir_val_t v, ir_type_t type) {
  if (v.id < 0 || v.id >= ctx->f->next_vreg_id) return;
  if (!wasm_type(type)) return;
  ctx->vreg_type_seen[v.id] = 1;
  ctx->vreg_types[v.id] = type;
}

static int val_is_unsigned(wasm_func_ctx_t *ctx, ir_val_t v) {
  return v.id >= 0 && v.id < ctx->f->next_vreg_id && ctx->vreg_unsigned[v.id];
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

static void set_vreg_global_ref(wasm_func_ctx_t *ctx, int vreg, global_var_t *gv, int offset) {
  if (vreg < 0 || vreg >= ctx->f->next_vreg_id) return;
  ctx->vreg_global_refs[vreg] = gv;
  ctx->vreg_global_ref_offsets[vreg] = offset;
}

static global_var_t *get_vreg_global_ref(wasm_func_ctx_t *ctx, int vreg, int *out_offset) {
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

static char *global_scalar_func_ref(global_var_t *gv, int *out_len) {
  if (out_len) *out_len = 0;
  psx_gvar_init_scalar_value_t value = psx_gvar_init_scalar_value(gv, 4);
  psx_gvar_symbol_ref_t ref = value.symbol_ref;
  if (ref.kind != PSX_GVAR_SYMBOL_REF_NAMED) return NULL;
  if (!psx_ctx_has_function_name(ref.symbol, ref.symbol_len)) return NULL;
  if (out_len) *out_len = ref.symbol_len;
  return ref.symbol;
}

static wasm_global_func_state_t *find_global_func_state(wasm_func_ctx_t *ctx, global_var_t *gv,
                                                        int offset) {
  if (!gv) return NULL;
  for (int i = 0; i < ctx->global_func_state_count; i++) {
    if (ctx->global_func_states[i].gv == gv && ctx->global_func_states[i].offset == offset) {
      return &ctx->global_func_states[i];
    }
  }
  return NULL;
}

static void set_global_func_ref(wasm_func_ctx_t *ctx, global_var_t *gv, int offset, char *name,
                                int name_len) {
  if (!gv) return;
  wasm_global_func_state_t *s = find_global_func_state(ctx, gv, offset);
  if (!s) {
    if (ctx->global_func_state_count == ctx->global_func_state_cap) {
      int ncap = ctx->global_func_state_cap ? ctx->global_func_state_cap * 2 : 8;
      wasm_global_func_state_t *n =
          realloc(ctx->global_func_states, (size_t)ncap * sizeof(*n));
      if (!n) diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
      ctx->global_func_states = n;
      ctx->global_func_state_cap = ncap;
    }
    s = &ctx->global_func_states[ctx->global_func_state_count++];
    s->gv = gv;
    s->offset = offset;
  }
  s->func_ref_name = name;
  s->func_ref_name_len = name_len;
  s->is_set = 1;
}

static char *global_member_func_ref(global_var_t *gv, int offset, int *out_len) {
  if (out_len) *out_len = 0;
  psx_gvar_view_t view = psx_gvar_view(gv);
  if (!gv || view.tag_kind == TK_EOF || view.is_tag_pointer || view.is_array ||
      view.init_count <= 0) {
    return NULL;
  }
  int n = psx_ctx_get_tag_member_count(view.tag_kind, view.tag_name, view.tag_len);
  int init_idx = 0;
  for (int m = 0; m < n; m++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(view.tag_kind, view.tag_name, view.tag_len, m, &mi)) break;
    if (mi.bit_width > 0) {
      init_idx++;
      continue;
    }
    if (psx_tag_member_is_tag_aggregate(&mi)) return NULL;
    if (mi.array_len > 0) {
      for (int k = 0; k < mi.array_len && init_idx < view.init_count; k++, init_idx++) {
        if (mi.offset + k * mi.type_size != offset) continue;
        psx_gvar_init_member_value_t value =
            psx_gvar_init_member_value(gv, init_idx, &mi);
        psx_gvar_symbol_ref_t ref = value.symbol_ref;
        if (ref.kind == PSX_GVAR_SYMBOL_REF_NAMED &&
            psx_ctx_has_function_name(ref.symbol, ref.symbol_len)) {
          if (out_len) *out_len = ref.symbol_len;
          return ref.symbol;
        }
        return NULL;
      }
      continue;
    }
    if (mi.offset == offset && init_idx < view.init_count) {
      psx_gvar_init_member_value_t value =
          psx_gvar_init_member_value(gv, init_idx, &mi);
      psx_gvar_symbol_ref_t ref = value.symbol_ref;
      if (ref.kind == PSX_GVAR_SYMBOL_REF_NAMED &&
          psx_ctx_has_function_name(ref.symbol, ref.symbol_len)) {
        if (out_len) *out_len = ref.symbol_len;
        return ref.symbol;
      }
      return NULL;
    }
    init_idx++;
  }
  return NULL;
}

static char *current_global_func_ref(wasm_func_ctx_t *ctx, global_var_t *gv, int offset,
                                     int *out_len) {
  if (out_len) *out_len = 0;
  wasm_global_func_state_t *s = find_global_func_state(ctx, gv, offset);
  if (s && s->is_set) {
    if (out_len) *out_len = s->func_ref_name_len;
    return s->func_ref_name;
  }
  if (offset == 0) {
    char *name = global_scalar_func_ref(gv, out_len);
    if (name) return name;
  }
  return global_member_func_ref(gv, offset, out_len);
}

static void infer_alloca_value_types(wasm_func_ctx_t *ctx) {
  for (ir_block_t *b = ctx->f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_LOAD && i->dst.id >= 0 && i->dst.id < ctx->f->next_vreg_id &&
          i->is_unsigned) {
        ctx->vreg_unsigned[i->dst.id] = 1;
      }
      if (i->op == IR_ZEXT && i->dst.id >= 0 && i->dst.id < ctx->f->next_vreg_id) {
        ctx->vreg_unsigned[i->dst.id] = 1;
      }
      if (i->op == IR_F2I && i->is_unsigned &&
          i->dst.id >= 0 && i->dst.id < ctx->f->next_vreg_id) {
        ctx->vreg_unsigned[i->dst.id] = 1;
      }
      if (i->op == IR_STORE) {
        wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
        ir_type_t val_ty = i->src2.type;
        if (slot && slot->value_type == IR_TY_VOID && wasm_type(val_ty)) {
          slot->value_type = val_ty;
        }
      }
      if (i->op == IR_LOAD && i->dst.type == IR_TY_PTR) {
        wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
        if (slot && slot->value_type == IR_TY_I64) set_vreg_type(ctx, i->dst, IR_TY_I64);
      }
    }
  }
}

static void analyze_func(wasm_func_ctx_t *ctx) {
  ctx->vreg_type_seen = calloc((size_t)ctx->f->next_vreg_id, sizeof(int));
  ctx->vreg_types = calloc((size_t)ctx->f->next_vreg_id, sizeof(ir_type_t));
  ctx->vreg_unsigned = calloc((size_t)ctx->f->next_vreg_id, sizeof(unsigned char));
  ctx->vreg_func_ref_names = calloc((size_t)ctx->f->next_vreg_id, sizeof(char *));
  ctx->vreg_func_ref_name_lens = calloc((size_t)ctx->f->next_vreg_id, sizeof(int));
  ctx->vreg_global_refs = calloc((size_t)ctx->f->next_vreg_id, sizeof(global_var_t *));
  ctx->vreg_global_ref_offsets = calloc((size_t)ctx->f->next_vreg_id, sizeof(int));
  ctx->vreg_const_known = calloc((size_t)ctx->f->next_vreg_id, sizeof(unsigned char));
  ctx->vreg_const_values = calloc((size_t)ctx->f->next_vreg_id, sizeof(long long));
  if (!ctx->vreg_type_seen || !ctx->vreg_types || !ctx->vreg_unsigned ||
      !ctx->vreg_func_ref_names || !ctx->vreg_func_ref_name_lens || !ctx->vreg_global_refs ||
      !ctx->vreg_global_ref_offsets || !ctx->vreg_const_known || !ctx->vreg_const_values) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  for (ir_block_t *b = ctx->f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      collect_inst_vregs(ctx, i);
      if (i->op == IR_ALLOCA) add_alloca_slot(ctx, i);
      if (i->op == IR_LABEL || i->op == IR_BR || i->op == IR_BR_COND) ctx->has_control_flow = 1;
    }
  }
  for (ir_block_t *b = ctx->f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_CALL && i->callee.id != IR_VAL_NONE) {
        set_vreg_type(ctx, i->callee, IR_TY_I32);
      }
    }
  }
  infer_alloca_value_types(ctx);
  ctx->frame_size = align_to(ctx->frame_size, 16);
}

static void emit_val_expr(wasm_func_ctx_t *ctx, ir_val_t v) {
  ir_type_t type = effective_val_type(ctx, v);
  const char *ty = wasm_type(type);
  if (!ty) wasm_unsupported_msg("unsupported Wasm value type");
  if (v.id == IR_VAL_IMM) {
    if (is_fp_type(type)) {
      cg_emitf("(%s.const %.17g)", ty, v.fp_imm);
    } else if (type == IR_TY_I64) {
      cg_emitf("(%s.const %lld)", ty, v.imm);
    } else {
      cg_emitf("(%s.const %d)", ty, (int32_t)v.imm);
    }
  } else if (v.id >= 0) {
    cg_emitf("(local.get $v%d)", v.id);
  } else {
    wasm_unsupported_msg("missing Wasm value");
  }
}

static void emit_val_expr_as(wasm_func_ctx_t *ctx, ir_val_t v, ir_type_t target) {
  if (target == IR_TY_PTR) target = IR_TY_I32;
  ir_type_t src = effective_val_type(ctx, v);
  if (src == IR_TY_PTR) src = IR_TY_I32;
  if (src == target) {
    emit_val_expr(ctx, v);
    return;
  }
  if (target == IR_TY_I64 && src != IR_TY_I64) {
    if (v.id == IR_VAL_IMM && !is_fp_type(src)) {
      cg_emitf("(i64.const %lld)", v.imm);
      return;
    }
    cg_emitf("(%s ", val_is_unsigned(ctx, v) ? "i64.extend_i32_u" : "i64.extend_i32_s");
    emit_val_expr(ctx, v);
    cg_emitf(")");
    return;
  }
  if (target != IR_TY_I64 && src == IR_TY_I64) {
    cg_emitf("(i32.wrap_i64 ");
    emit_val_expr(ctx, v);
    cg_emitf(")");
    return;
  }
  emit_val_expr(ctx, v);
}

static void emit_wasm_type_cast_prefix(ir_type_t from, ir_type_t to, int is_unsigned) {
  if (from == IR_TY_PTR) from = IR_TY_I32;
  if (to == IR_TY_PTR) to = IR_TY_I32;
  if (from == to) return;
  if (to == IR_TY_I64 && from != IR_TY_I64) {
    cg_emitf("(%s ", is_unsigned ? "i64.extend_i32_u" : "i64.extend_i32_s");
  } else if (to != IR_TY_I64 && from == IR_TY_I64) {
    cg_emitf("(i32.wrap_i64 ");
  }
}

static void emit_wasm_type_cast_suffix(ir_type_t from, ir_type_t to) {
  if (from == IR_TY_PTR) from = IR_TY_I32;
  if (to == IR_TY_PTR) to = IR_TY_I32;
  if (from != to && (from == IR_TY_I64 || to == IR_TY_I64)) cg_emitf(")");
}

static void emit_addr_expr(wasm_func_ctx_t *ctx, ir_val_t v) {
  emit_val_expr_as(ctx, v, IR_TY_I32);
}

static void emit_addr_plus_const(wasm_func_ctx_t *ctx, ir_val_t base, int off) {
  if (off == 0) {
    emit_addr_expr(ctx, base);
    return;
  }
  cg_emitf("(i32.add ");
  emit_addr_expr(ctx, base);
  cg_emitf(" (i32.const %d))", off);
}

static const char *wasm_binop(ir_op_t op, ir_type_t t) {
  const char *prefix = wasm_type(t);
  if (!prefix) return NULL;
  switch (op) {
    case IR_ADD: return t == IR_TY_I64 ? "i64.add" : "i32.add";
    case IR_SUB: return t == IR_TY_I64 ? "i64.sub" : "i32.sub";
    case IR_MUL: return t == IR_TY_I64 ? "i64.mul" : "i32.mul";
    case IR_DIV: return t == IR_TY_I64 ? "i64.div_s" : "i32.div_s";
    case IR_UDIV: return t == IR_TY_I64 ? "i64.div_u" : "i32.div_u";
    case IR_MOD: return t == IR_TY_I64 ? "i64.rem_s" : "i32.rem_s";
    case IR_UMOD: return t == IR_TY_I64 ? "i64.rem_u" : "i32.rem_u";
    case IR_AND: return t == IR_TY_I64 ? "i64.and" : "i32.and";
    case IR_OR:  return t == IR_TY_I64 ? "i64.or" : "i32.or";
    case IR_XOR: return t == IR_TY_I64 ? "i64.xor" : "i32.xor";
    case IR_SHL: return t == IR_TY_I64 ? "i64.shl" : "i32.shl";
    case IR_SHR: return t == IR_TY_I64 ? "i64.shr_s" : "i32.shr_s";
    case IR_LSR: return t == IR_TY_I64 ? "i64.shr_u" : "i32.shr_u";
    case IR_EQ:  return t == IR_TY_I64 ? "i64.eq" : "i32.eq";
    case IR_NE:  return t == IR_TY_I64 ? "i64.ne" : "i32.ne";
    case IR_LT:  return t == IR_TY_I64 ? "i64.lt_s" : "i32.lt_s";
    case IR_LE:  return t == IR_TY_I64 ? "i64.le_s" : "i32.le_s";
    case IR_ULT: return t == IR_TY_I64 ? "i64.lt_u" : "i32.lt_u";
    case IR_ULE: return t == IR_TY_I64 ? "i64.le_u" : "i32.le_u";
    default: return NULL;
  }
}

static const char *wasm_fp_binop(ir_op_t op, ir_type_t t) {
  if (!is_fp_type(t)) return NULL;
  switch (op) {
    case IR_FADD: return t == IR_TY_F64 ? "f64.add" : "f32.add";
    case IR_FSUB: return t == IR_TY_F64 ? "f64.sub" : "f32.sub";
    case IR_FMUL: return t == IR_TY_F64 ? "f64.mul" : "f32.mul";
    case IR_FDIV: return t == IR_TY_F64 ? "f64.div" : "f32.div";
    case IR_FEQ:  return t == IR_TY_F64 ? "f64.eq" : "f32.eq";
    case IR_FNE:  return t == IR_TY_F64 ? "f64.ne" : "f32.ne";
    case IR_FLT:  return t == IR_TY_F64 ? "f64.lt" : "f32.lt";
    case IR_FLE:  return t == IR_TY_F64 ? "f64.le" : "f32.le";
    default: return NULL;
  }
}

static ir_type_t effective_load_type(wasm_func_ctx_t *ctx, ir_inst_t *i) {
  ir_type_t dst_ty = effective_val_type(ctx, i->dst);
  if (dst_ty != i->dst.type) return dst_ty;
  if (i->dst.type == IR_TY_PTR) {
    wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
    if (slot && slot->value_type == IR_TY_I64) return IR_TY_I64;
  }
  return i->dst.type;
}

static void emit_load(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
  const char *op = NULL;
  switch (effective_load_type(ctx, i)) {
    case IR_TY_I8:  op = i->is_unsigned ? "i32.load8_u" : "i32.load8_s"; break;
    case IR_TY_I16: op = i->is_unsigned ? "i32.load16_u" : "i32.load16_s"; break;
    case IR_TY_I32:
    case IR_TY_PTR: op = "i32.load"; break;
    case IR_TY_I64: op = "i64.load"; break;
    case IR_TY_F32: op = "f32.load"; break;
    case IR_TY_F64: op = "f64.load"; break;
    default: wasm_unsupported_op(i->op);
  }
  wasm_emitf(indent, "(local.set $v%d (%s ", i->dst.id, op);
  emit_addr_expr(ctx, i->src1);
  cg_emitf("))\n");
  if (slot && slot->func_ref_name) {
    set_vreg_func_ref(ctx, i->dst.id, slot->func_ref_name, slot->func_ref_name_len);
  } else {
    int global_off = 0;
    global_var_t *gv = get_vreg_global_ref(ctx, i->src1.id, &global_off);
    int name_len = 0;
    char *name = current_global_func_ref(ctx, gv, global_off, &name_len);
    if (name) set_vreg_func_ref(ctx, i->dst.id, name, name_len);
  }
}

static void emit_store(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
  int global_off = 0;
  global_var_t *gv = get_vreg_global_ref(ctx, i->src1.id, &global_off);
  const char *op = NULL;
  ir_type_t store_ty = i->src2.type;
  switch (store_ty) {
    case IR_TY_I8:  op = "i32.store8"; break;
    case IR_TY_I16: op = "i32.store16"; break;
    case IR_TY_I32:
    case IR_TY_PTR: op = "i32.store"; break;
    case IR_TY_I64: op = "i64.store"; break;
    case IR_TY_F32: op = "f32.store"; break;
    case IR_TY_F64: op = "f64.store"; break;
    default: wasm_unsupported_op(i->op);
  }
  wasm_emitf(indent, "(%s ", op);
  emit_addr_expr(ctx, i->src1);
  cg_emitf(" ");
  emit_val_expr_as(ctx, i->src2, store_ty);
  cg_emitf(")\n");
  if (slot) {
    int name_len = 0;
    char *name = get_vreg_func_ref(ctx, i->src2.id, &name_len);
    slot->func_ref_name = name;
    slot->func_ref_name_len = name_len;
  }
  if (gv) {
    int name_len = 0;
    char *name = get_vreg_func_ref(ctx, i->src2.id, &name_len);
    if (ctx->has_control_flow) {
      set_global_func_ref(ctx, gv, global_off, NULL, 0);
    } else {
      set_global_func_ref(ctx, gv, global_off, name, name_len);
    }
  }
}

static ir_type_t atomic_mem_type(ir_inst_t *i) {
  return i->atomic_width == 8 ? IR_TY_I64 : IR_TY_I32;
}

static const char *atomic_load_op(ir_inst_t *i) {
  switch (i->atomic_width ? i->atomic_width : 4) {
    case 1: return i->is_unsigned ? "i32.load8_u" : "i32.load8_s";
    case 2: return i->is_unsigned ? "i32.load16_u" : "i32.load16_s";
    case 4: return "i32.load";
    case 8: return "i64.load";
    default: return NULL;
  }
}

static const char *atomic_store_op(ir_inst_t *i) {
  switch (i->atomic_width ? i->atomic_width : 4) {
    case 1: return "i32.store8";
    case 2: return "i32.store16";
    case 4: return "i32.store";
    case 8: return "i64.store";
    default: return NULL;
  }
}

static void emit_atomic_load_expr(wasm_func_ctx_t *ctx, ir_inst_t *i) {
  const char *op = atomic_load_op(i);
  if (!op) wasm_unsupported_op(i->op);
  cg_emitf("(%s ", op);
  emit_addr_expr(ctx, i->src1);
  cg_emitf(")");
}

static void emit_atomic_store(wasm_func_ctx_t *ctx, ir_inst_t *i, ir_val_t ptr, ir_val_t val,
                              int indent) {
  const char *op = atomic_store_op(i);
  if (!op) wasm_unsupported_op(i->op);
  wasm_emitf(indent, "(%s ", op);
  emit_addr_expr(ctx, ptr);
  cg_emitf(" ");
  emit_val_expr_as(ctx, val, atomic_mem_type(i));
  cg_emitf(")\n");
}

static const char *atomic_rmw_wasm_op(ir_inst_t *i) {
  int is64 = atomic_mem_type(i) == IR_TY_I64;
  switch (i->atomic_rmw_op) {
    case IR_ARMW_ADD: return is64 ? "i64.add" : "i32.add";
    case IR_ARMW_SUB: return is64 ? "i64.sub" : "i32.sub";
    case IR_ARMW_OR:  return is64 ? "i64.or"  : "i32.or";
    case IR_ARMW_AND: return is64 ? "i64.and" : "i32.and";
    case IR_ARMW_XOR: return is64 ? "i64.xor" : "i32.xor";
    default: return NULL;
  }
}

static void emit_atomic_inst(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  if (i->atomic_kind == IR_ATOMIC_FENCE) {
    wasm_emitf(indent, "(nop)\n");
    return;
  }

  ir_type_t mem_ty = atomic_mem_type(i);
  if (i->atomic_kind == IR_ATOMIC_LOAD) {
    ir_type_t dst_ty = effective_val_type(ctx, i->dst);
    wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
    emit_wasm_type_cast_prefix(mem_ty, dst_ty, i->is_unsigned);
    emit_atomic_load_expr(ctx, i);
    emit_wasm_type_cast_suffix(mem_ty, dst_ty);
    cg_emitf(")\n");
    return;
  }

  if (i->atomic_kind == IR_ATOMIC_STORE) {
    emit_atomic_store(ctx, i, i->src1, i->src2, indent);
    return;
  }

  if (i->atomic_kind == IR_ATOMIC_RMW) {
    ir_type_t dst_ty = effective_val_type(ctx, i->dst);
    wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
    emit_wasm_type_cast_prefix(mem_ty, dst_ty, i->is_unsigned);
    emit_atomic_load_expr(ctx, i);
    emit_wasm_type_cast_suffix(mem_ty, dst_ty);
    cg_emitf(")\n");

    const char *store_op = atomic_store_op(i);
    if (!store_op) wasm_unsupported_op(i->op);
    wasm_emitf(indent, "(%s ", store_op);
    emit_addr_expr(ctx, i->src1);
    cg_emitf(" ");
    if (i->atomic_rmw_op == IR_ARMW_XCHG) {
      emit_val_expr_as(ctx, i->src2, mem_ty);
    } else {
      const char *op = atomic_rmw_wasm_op(i);
      if (!op) wasm_unsupported_op(i->op);
      cg_emitf("(%s ", op);
      emit_val_expr_as(ctx, i->dst, mem_ty);
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->src2, mem_ty);
      cg_emitf(")");
    }
    cg_emitf(")\n");
    return;
  }

  if (i->atomic_kind == IR_ATOMIC_CAS) {
    const char *load_op = atomic_load_op(i);
    const char *store_op = atomic_store_op(i);
    const char *eq_op = mem_ty == IR_TY_I64 ? "i64.eq" : "i32.eq";
    const char *tmp = mem_ty == IR_TY_I64 ? "$atomic_tmp_i64" : "$atomic_tmp_i32";
    const char *exp = mem_ty == IR_TY_I64 ? "$atomic_exp_i64" : "$atomic_exp_i32";
    if (!load_op || !store_op) wasm_unsupported_op(i->op);

    wasm_emitf(indent, "(local.set %s (%s ", tmp, load_op);
    emit_addr_expr(ctx, i->src1);
    cg_emitf("))\n");
    wasm_emitf(indent, "(local.set %s (%s ", exp, load_op);
    emit_addr_expr(ctx, i->src2);
    cg_emitf("))\n");
    wasm_emitf(indent, "(local.set $v%d (%s (local.get %s) (local.get %s)))\n",
               i->dst.id, eq_op, tmp, exp);
    wasm_emitf(indent, "(if (local.get $v%d)\n", i->dst.id);
    wasm_emitf(indent + 2, "(then\n");
    wasm_emitf(indent + 4, "(%s ", store_op);
    emit_addr_expr(ctx, i->src1);
    cg_emitf(" ");
    emit_val_expr_as(ctx, i->src3, mem_ty);
    cg_emitf(")\n");
    wasm_emitf(indent + 2, ")\n");
    wasm_emitf(indent, ")\n");
    wasm_emitf(indent, "(%s ", store_op);
    emit_addr_expr(ctx, i->src2);
    cg_emitf(" (local.get %s))\n", tmp);
    return;
  }

  wasm_unsupported_op(i->op);
}

static int uses_ptr_value(wasm_func_ctx_t *ctx, ir_inst_t *i) {
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

static void emit_memcpy(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  int n = i->alloca_size;
  if (n < 0) wasm_unsupported_op(i->op);
  int off = 0;
  for (; off + 8 <= n; off += 8) {
    wasm_emitf(indent, "(i64.store ");
    emit_addr_plus_const(ctx, i->src1, off);
    cg_emitf(" (i64.load ");
    emit_addr_plus_const(ctx, i->src2, off);
    cg_emitf("))\n");
  }
  for (; off + 4 <= n; off += 4) {
    wasm_emitf(indent, "(i32.store ");
    emit_addr_plus_const(ctx, i->src1, off);
    cg_emitf(" (i32.load ");
    emit_addr_plus_const(ctx, i->src2, off);
    cg_emitf("))\n");
  }
  for (; off + 2 <= n; off += 2) {
    wasm_emitf(indent, "(i32.store16 ");
    emit_addr_plus_const(ctx, i->src1, off);
    cg_emitf(" (i32.load16_u ");
    emit_addr_plus_const(ctx, i->src2, off);
    cg_emitf("))\n");
  }
  for (; off < n; off++) {
    wasm_emitf(indent, "(i32.store8 ");
    emit_addr_plus_const(ctx, i->src1, off);
    cg_emitf(" (i32.load8_u ");
    emit_addr_plus_const(ctx, i->src2, off);
    cg_emitf("))\n");
  }
}

static int val_uses_vreg(ir_val_t v, int id) {
  return v.id == id;
}

static int inst_uses_vreg(ir_inst_t *i, int id) {
  if (!i) return 0;
  if (val_uses_vreg(i->src1, id) || val_uses_vreg(i->src2, id) ||
      val_uses_vreg(i->src3, id) || val_uses_vreg(i->callee, id) ||
      val_uses_vreg(i->ret_struct_area, id)) {
    return 1;
  }
  for (int a = 0; a < i->nargs; a++) {
    if (val_uses_vreg(i->args[a], id)) return 1;
  }
  return 0;
}

static int vreg_used_after(ir_inst_t *from, int id) {
  if (!from || id < 0) return 0;
  for (ir_inst_t *i = from->next; i; i = i->next) {
    if (inst_uses_vreg(i, id)) return 1;
  }
  return 0;
}

static int emit_variadic_arg_area_prepare(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  if (!i->is_variadic_call) return 0;
  int nargs_var = i->nargs - i->nargs_fixed;
  if (nargs_var <= 0) return 0;
  int bytes = align_to(nargs_var * 8, 16);
  wasm_emitf(indent, "(local.set $old_va_arg_area (global.get $__ag_va_arg_area))\n");
  wasm_emitf(indent, "(global.set $__stack_pointer (i32.sub (global.get $__stack_pointer) (i32.const %d)))\n",
             bytes);
  wasm_emitf(indent, "(global.set $__ag_va_arg_area (global.get $__stack_pointer))\n");
  for (int a = i->nargs_fixed; a < i->nargs; a++) {
    int off = (a - i->nargs_fixed) * 8;
    ir_type_t arg_ty = i->args[a].type == IR_TY_PTR ? IR_TY_PTR : effective_val_type(ctx, i->args[a]);
    if (arg_ty == IR_TY_F64) {
      wasm_emitf(indent, "(f64.store (i32.add (global.get $__ag_va_arg_area) (i32.const %d)) ", off);
      emit_val_expr_as(ctx, i->args[a], IR_TY_F64);
      cg_emitf(")\n");
    } else if (arg_ty == IR_TY_F32) {
      wasm_emitf(indent, "(f64.store (i32.add (global.get $__ag_va_arg_area) (i32.const %d)) (f64.promote_f32 ", off);
      emit_val_expr_as(ctx, i->args[a], IR_TY_F32);
      cg_emitf("))\n");
    } else {
      wasm_emitf(indent, "(i64.store (i32.add (global.get $__ag_va_arg_area) (i32.const %d)) ", off);
      emit_val_expr_as(ctx, i->args[a], IR_TY_I64);
      cg_emitf(")\n");
    }
  }
  return bytes;
}

static void emit_variadic_arg_area_restore(int bytes, int indent) {
  if (bytes <= 0) return;
  wasm_emitf(indent, "(global.set $__stack_pointer (i32.add (global.get $__stack_pointer) (i32.const %d)))\n",
             bytes);
  wasm_emitf(indent, "(global.set $__ag_va_arg_area (local.get $old_va_arg_area))\n");
}

static void emit_call(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  int vararg_area_bytes = emit_variadic_arg_area_prepare(ctx, i, indent);
  if (i->callee.id != IR_VAL_NONE) {
    g_func_table.needs_table = 1;
    if (i->ret_complex_half != 0) {
      wasm_unsupported_op(i->op);
    }
    int callee_name_len = 0;
    char *callee_name = get_vreg_func_ref(ctx, i->callee.id, &callee_name_len);
    psx_function_ret_info_t callee_ret =
        callee_name ? psx_ctx_get_function_ret_info(callee_name, callee_name_len)
                    : (psx_function_ret_info_t){0};
    int returns_aggregate = i->ret_struct_size > 0 || i->ret_struct_area.id != IR_VAL_NONE;
    if (returns_aggregate && i->ret_struct_area.id == IR_VAL_NONE) {
      wasm_unsupported_msg("indirect aggregate function call without return area in Wasm backend");
    }
    psx_decl_funcptr_sig_t fs = i->funcptr_sig;
    int returns_void = returns_aggregate || i->is_void_call || fs.ret_is_void ||
                       callee_ret.is_void ||
                       i->dst.id == IR_VAL_NONE || i->dst.type == IR_TY_VOID;
    int result_unused = !returns_void && !vreg_used_after(i, i->dst.id);
    const char *ret_ty = returns_void ? NULL : wasm_any_type_or_unsupported(i->dst.type);
    if (result_unused) wasm_emitf(indent, "(drop ");
    else if (!returns_void) wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
    else wasm_emitf(indent, "");
    cg_emitf("(call_indirect");
    int call_nargs = i->is_variadic_call ? i->nargs_fixed : i->nargs;
    if (returns_aggregate) cg_emitf(" (param i32)");
    for (int a = 0; a < call_nargs; a++) {
      ir_type_t raw_arg_ty = effective_val_type(ctx, i->args[a]);
      ir_type_t arg_ty = raw_arg_ty;
      int from_funcptr_sig = i->has_funcptr_sig ? 1 : 0;
      if (from_funcptr_sig) arg_ty = funcptr_param_type_from_inst(i, a, arg_ty);
      int null_ptr_pair_arg =
          !from_funcptr_sig && a == 0 && call_nargs >= 2 && i->args[1].type == IR_TY_PTR;
      if (null_ptr_pair_arg) arg_ty = IR_TY_I32;
      unsigned iw = a < 8 ? ((fs.param_int_mask >> (2 * a)) & 3u) : 0;
      if (from_funcptr_sig && !fs.is_variadic && !i->is_variadic_call &&
          arg_ty == IR_TY_I32 &&
          raw_arg_ty != IR_TY_PTR && !is_fp_type(raw_arg_ty) && !null_ptr_pair_arg &&
          iw != 3) {
        arg_ty = IR_TY_I64;
      }
      if (arg_ty == IR_TY_PTR) arg_ty = IR_TY_I32;
      else if (!from_funcptr_sig && !is_fp_type(arg_ty) && !null_ptr_pair_arg) arg_ty = IR_TY_I64;
      cg_emitf(" (param %s)", wasm_any_type_or_unsupported(arg_ty));
    }
    if (!returns_void) cg_emitf(" (result %s)", ret_ty);
    if (returns_aggregate) {
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->ret_struct_area, IR_TY_PTR);
    }
    for (int a = 0; a < call_nargs; a++) {
      ir_type_t raw_arg_ty = effective_val_type(ctx, i->args[a]);
      ir_type_t arg_ty = raw_arg_ty;
      int from_funcptr_sig = i->has_funcptr_sig ? 1 : 0;
      if (from_funcptr_sig) arg_ty = funcptr_param_type_from_inst(i, a, arg_ty);
      int null_ptr_pair_arg =
          !from_funcptr_sig && a == 0 && call_nargs >= 2 && i->args[1].type == IR_TY_PTR;
      if (null_ptr_pair_arg) arg_ty = IR_TY_I32;
      unsigned iw = a < 8 ? ((fs.param_int_mask >> (2 * a)) & 3u) : 0;
      if (from_funcptr_sig && !fs.is_variadic && !i->is_variadic_call &&
          arg_ty == IR_TY_I32 &&
          raw_arg_ty != IR_TY_PTR && !is_fp_type(raw_arg_ty) && !null_ptr_pair_arg &&
          iw != 3) {
        arg_ty = IR_TY_I64;
      }
      if (arg_ty == IR_TY_PTR) arg_ty = IR_TY_I32;
      else if (!from_funcptr_sig && !is_fp_type(arg_ty) && !null_ptr_pair_arg) arg_ty = IR_TY_I64;
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->args[a], arg_ty);
    }
    cg_emitf(" ");
    emit_val_expr_as(ctx, i->callee, IR_TY_I32);
    cg_emitf(")");
    if (result_unused || !returns_void) cg_emitf(")");
    cg_emitf("\n");
    emit_variadic_arg_area_restore(vararg_area_bytes, indent);
    return;
  }
  if (!i->sym) wasm_unsupported_op(i->op);
  if (!psx_ctx_has_function_name(i->sym, i->sym_len)) {
    wasm_unsupported_msg("external or implicitly declared function call in Wasm backend");
  }
  psx_function_ret_info_t ret = psx_ctx_get_function_ret_info(i->sym, i->sym_len);
  int returns_void = ret.is_void;
  if (i->ret_complex_half > 0) {
    wasm_emitf(indent, "(call $%.*s ", i->sym_len, i->sym);
    emit_val_expr_as(ctx, i->dst, IR_TY_PTR);
  } else if (i->ret_struct_size > 0) {
    wasm_emitf(indent, "(call $%.*s ", i->sym_len, i->sym);
    emit_val_expr_as(ctx, i->ret_struct_area, IR_TY_PTR);
  } else if (!returns_void && i->dst.id >= 0 && i->dst.type != IR_TY_VOID) {
    wasm_emitf(indent, "(local.set $v%d (call $%.*s", i->dst.id, i->sym_len, i->sym);
  } else {
    wasm_emitf(indent, "(call $%.*s", i->sym_len, i->sym);
  }
  int is_minimal_snprintf =
      i->sym_len == 8 && memcmp(i->sym, "snprintf", 8) == 0 &&
      !psx_ctx_is_function_defined(i->sym, i->sym_len);
  int is_minimal_swprintf =
      i->sym_len == 8 && memcmp(i->sym, "swprintf", 8) == 0 &&
      !psx_ctx_is_function_defined(i->sym, i->sym_len);
  int is_minimal_printf =
      i->sym_len == 6 && memcmp(i->sym, "printf", 6) == 0 &&
      !psx_ctx_is_function_defined(i->sym, i->sym_len);
  int is_minimal_fprintf =
      i->sym_len == 7 && memcmp(i->sym, "fprintf", 7) == 0 &&
      !psx_ctx_is_function_defined(i->sym, i->sym_len);
  int is_minimal_sscanf =
      i->sym_len == 6 && memcmp(i->sym, "sscanf", 6) == 0 &&
      !psx_ctx_is_function_defined(i->sym, i->sym_len);
  int is_minimal_swscanf =
      i->sym_len == 7 && memcmp(i->sym, "swscanf", 7) == 0 &&
      !psx_ctx_is_function_defined(i->sym, i->sym_len);
  int is_minimal_fixed2_format = is_minimal_snprintf || is_minimal_swprintf;
  int is_minimal_output_count = is_minimal_printf || is_minimal_fprintf;
  int call_nargs = is_minimal_fixed2_format ? 5 :
                   (is_minimal_printf ? 3 :
                    (is_minimal_fprintf ? 4 :
                     ((is_minimal_sscanf || is_minimal_swscanf) ? 4 :
                      (i->is_variadic_call ? i->nargs_fixed : i->nargs))));
  for (int a = 0; a < call_nargs; a++) {
    if ((is_minimal_fixed2_format || is_minimal_output_count || is_minimal_sscanf || is_minimal_swscanf) &&
        a >= i->nargs) {
      cg_emitf(" (i64.const 0)");
      continue;
    }
    ir_type_t arg_ty = effective_val_type(ctx, i->args[a]);
    if ((is_minimal_fixed2_format || is_minimal_output_count || is_minimal_sscanf || is_minimal_swscanf) &&
        a >= i->nargs_fixed && is_fp_type(arg_ty)) {
      cg_emitf(" (i64.const 0)");
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
    if (minimal_stub_ptr_arg ||
        psx_ctx_get_function_param_category(i->sym, i->sym_len, a) == PSX_PCAT_PTR) {
      arg_ty = IR_TY_PTR;
    } else if (is_minimal_fixed2_format || is_minimal_output_count || is_minimal_sscanf || is_minimal_swscanf) {
      arg_ty = IR_TY_I64;
    } else if (a == 1 &&
               ((i->sym_len == 5 && memcmp(i->sym, "ldexp", 5) == 0) ||
                (i->sym_len == 6 && (memcmp(i->sym, "scalbn", 6) == 0 ||
                                      memcmp(i->sym, "ldexpf", 6) == 0 ||
                                      memcmp(i->sym, "ldexpl", 6) == 0)) ||
                (i->sym_len == 7 && (memcmp(i->sym, "scalbnf", 7) == 0 ||
                                      memcmp(i->sym, "scalbnl", 7) == 0 ||
                                      memcmp(i->sym, "scalbln", 7) == 0)) ||
                (i->sym_len == 8 && (memcmp(i->sym, "scalblnf", 8) == 0 ||
                                      memcmp(i->sym, "scalblnl", 8) == 0)))) {
      arg_ty = IR_TY_I64;
    } else if (psx_ctx_get_function_param_int_size(i->sym, i->sym_len, a) == 8) {
      arg_ty = IR_TY_I64;
    }
    cg_emitf(" ");
    emit_val_expr_as(ctx, i->args[a], arg_ty);
  }
  cg_emitf(")");
  if (i->ret_struct_size == 0 && i->ret_complex_half == 0 &&
      !returns_void && i->dst.id >= 0 && i->dst.type != IR_TY_VOID) {
    cg_emitf(")");
  }
  cg_emitf("\n");
  emit_variadic_arg_area_restore(vararg_area_bytes, indent);
}

static void emit_complex_ret_copy(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  int half = i->ret_complex_half;
  const char *ty = half == 4 ? "f32" : "f64";
  wasm_emitf(indent, "(%s.store (local.get $p0) (%s.load ", ty, ty);
  emit_addr_expr(ctx, i->src1);
  cg_emitf("))\n");
  wasm_emitf(indent, "(%s.store (i32.add (local.get $p0) (i32.const %d)) (%s.load ", ty, half, ty);
  emit_addr_plus_const(ctx, i->src1, half);
  cg_emitf("))\n");
}

static void emit_inst(wasm_func_ctx_t *ctx, ir_inst_t *i, int dispatch_mode, int indent) {
  switch (i->op) {
    case IR_NOP:
      return;
    case IR_LABEL:
      if (!dispatch_mode) wasm_unsupported_op(i->op);
      return;
    case IR_PARAM:
      if (i->src1.id != IR_VAL_IMM || i->src1.imm < -1) wasm_unsupported_op(i->op);
      int param_ordinal = func_param_ordinal_for_inst(ctx->f, i);
      if (i->src1.imm >= 0 && param_ordinal < 0) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (local.get $p%d))\n", i->dst.id,
                 i->src1.imm < 0 ? 0 : param_ordinal + func_has_hidden_ret_area(ctx->f));
      return;
    case IR_ALLOCA: {
      int off = find_alloca_offset(ctx, i->dst.id);
      if (off < 0) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (i32.add (local.get $fp) (i32.const %d)))\n", i->dst.id, off);
      return;
    }
    case IR_LOAD_IMM:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      emit_val_expr(ctx, i->src1);
      cg_emitf(")\n");
      if (!is_fp_type(i->dst.type)) set_vreg_const(ctx, i->dst.id, i->src1.imm);
      return;
    case IR_LOAD_FP_IMM:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      emit_val_expr(ctx, i->src1);
      cg_emitf(")\n");
      return;
    case IR_LOAD_STR: {
      int addr = data_addr_for_string_label(i->sym);
      if (addr < 0) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, addr);
      return;
    }
    case IR_LOAD_SYM: {
      if (psx_ctx_has_function_name(i->sym, i->sym_len)) {
        int func_idx = function_table_index_or_unsupported(i->sym, i->sym_len);
        wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, func_idx);
        set_vreg_func_ref(ctx, i->dst.id, i->sym, i->sym_len);
        return;
      }
      int addr = data_addr_for_global(i->sym, i->sym_len);
      wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, addr);
      set_vreg_global_ref(ctx, i->dst.id, psx_find_global_var(i->sym, i->sym_len), 0);
      return;
    }
    case IR_LOAD_TLV_ADDR: {
      int addr = data_addr_for_global(i->sym, i->sym_len);
      wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, addr);
      set_vreg_global_ref(ctx, i->dst.id, psx_find_global_var(i->sym, i->sym_len), 0);
      return;
    }
    case IR_ZEXT:
    case IR_SEXT:
    case IR_TRUNC:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      if (i->dst.type == IR_TY_I64 && i->src1.type != IR_TY_I64) {
        cg_emitf("(%s ", i->op == IR_ZEXT ? "i64.extend_i32_u" : "i64.extend_i32_s");
        emit_val_expr(ctx, i->src1);
        cg_emitf(")");
      } else if (i->dst.type != IR_TY_I64 && i->src1.type == IR_TY_I64) {
        cg_emitf("(i32.wrap_i64 ");
        emit_val_expr(ctx, i->src1);
        cg_emitf(")");
      } else {
        emit_val_expr(ctx, i->src1);
      }
      cg_emitf(")\n");
      return;
    case IR_I2F:
      wasm_emitf(indent, "(local.set $v%d (%s.convert_%s_%c ", i->dst.id,
                 wasm_fp_type_or_unsupported(i->dst.type),
                 wasm_int_type_or_unsupported(effective_val_type(ctx, i->src1)),
                 (i->is_unsigned || val_is_unsigned(ctx, i->src1)) ? 'u' : 's');
      emit_val_expr(ctx, i->src1);
      cg_emitf("))\n");
      return;
    case IR_F2I:
      wasm_emitf(indent, "(local.set $v%d (%s.trunc_%s_%c ", i->dst.id,
                 wasm_int_type_or_unsupported(i->dst.type),
                 wasm_fp_type_or_unsupported(effective_val_type(ctx, i->src1)),
                 i->is_unsigned ? 'u' : 's');
      emit_val_expr(ctx, i->src1);
      cg_emitf("))\n");
      return;
    case IR_F2F:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      if (i->dst.type == IR_TY_F64 && effective_val_type(ctx, i->src1) == IR_TY_F32) {
        cg_emitf("(f64.promote_f32 ");
        emit_val_expr(ctx, i->src1);
        cg_emitf(")");
      } else if (i->dst.type == IR_TY_F32 && effective_val_type(ctx, i->src1) == IR_TY_F64) {
        cg_emitf("(f32.demote_f64 ");
        emit_val_expr(ctx, i->src1);
        cg_emitf(")");
      } else {
        emit_val_expr(ctx, i->src1);
      }
      cg_emitf(")\n");
      return;
    case IR_LOAD:
      emit_load(ctx, i, indent);
      return;
    case IR_STORE:
      emit_store(ctx, i, indent);
      return;
    case IR_ATOMIC:
      emit_atomic_inst(ctx, i, indent);
      return;
    case IR_MEMCPY:
      emit_memcpy(ctx, i, indent);
      return;
    case IR_ALIGN_PTR: {
      int align = i->alloca_align > 0 ? i->alloca_align : 16;
      wasm_emitf(indent, "(local.set $v%d (i32.and (i32.add ", i->dst.id);
      emit_addr_expr(ctx, i->src1);
      cg_emitf(" (i32.const %d)) (i32.const %d)))\n", align - 1, -align);
      return;
    }
    case IR_VLA_ALLOC:
      wasm_emitf(indent, "(local.set $v%d (i32.sub (global.get $__stack_pointer) ", i->dst.id);
      cg_emitf("(i32.and (i32.add ");
      emit_val_expr_as(ctx, i->src1, IR_TY_I32);
      cg_emitf(" (i32.const 15)) (i32.const -16))))\n");
      wasm_emitf(indent, "(global.set $__stack_pointer (local.get $v%d))\n", i->dst.id);
      return;
    case IR_VA_ARG_AREA:
      wasm_emitf(indent, "(local.set $v%d (global.get $__ag_va_arg_area))\n", i->dst.id);
      return;
    case IR_LEA:
      wasm_emitf(indent, "(local.set $v%d (i32.add ", i->dst.id);
      emit_addr_expr(ctx, i->src1);
      cg_emitf(" ");
      emit_addr_expr(ctx, i->src2);
      cg_emitf("))\n");
      return;
    case IR_NEG:
      wasm_emitf(indent, "(local.set $v%d (%s.sub (%s.const 0) ", i->dst.id,
               i->dst.type == IR_TY_I64 ? "i64" : "i32",
               i->dst.type == IR_TY_I64 ? "i64" : "i32");
      emit_val_expr(ctx, i->src1);
      cg_emitf("))\n");
      return;
    case IR_NOT:
      wasm_emitf(indent, "(local.set $v%d (%s.xor ", i->dst.id, i->dst.type == IR_TY_I64 ? "i64" : "i32");
      emit_val_expr(ctx, i->src1);
      cg_emitf(" (%s.const -1)))\n", i->dst.type == IR_TY_I64 ? "i64" : "i32");
      return;
    case IR_FNEG:
      wasm_emitf(indent, "(local.set $v%d (%s.neg ", i->dst.id,
                 wasm_fp_type_or_unsupported(effective_val_type(ctx, i->src1)));
      emit_val_expr(ctx, i->src1);
      cg_emitf("))\n");
      return;
    case IR_FADD: case IR_FSUB: case IR_FMUL: case IR_FDIV:
    case IR_FEQ: case IR_FNE: case IR_FLT: case IR_FLE: {
      int is_cmp = (i->op == IR_FEQ || i->op == IR_FNE || i->op == IR_FLT || i->op == IR_FLE);
      ir_type_t op_ty = is_cmp ? effective_val_type(ctx, i->src1) : effective_val_type(ctx, i->dst);
      const char *op = wasm_fp_binop(i->op, op_ty);
      if (!op) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (%s ", i->dst.id, op);
      emit_val_expr_as(ctx, i->src1, op_ty);
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->src2, op_ty);
      cg_emitf("))\n");
      return;
    }
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_UDIV:
    case IR_MOD: case IR_UMOD: case IR_AND: case IR_OR: case IR_XOR:
    case IR_SHL: case IR_SHR: case IR_LSR:
    case IR_EQ: case IR_NE: case IR_LT: case IR_LE: case IR_ULT: case IR_ULE: {
      int is_cmp = (i->op == IR_EQ || i->op == IR_NE || i->op == IR_LT || i->op == IR_LE ||
                    i->op == IR_ULT || i->op == IR_ULE);
      ir_type_t src1_ty = effective_val_type(ctx, i->src1);
      ir_type_t src2_ty = effective_val_type(ctx, i->src2);
      ir_type_t op_ty = is_cmp ? ((src1_ty == IR_TY_I64 || src2_ty == IR_TY_I64) ? IR_TY_I64 : src1_ty)
                               : effective_val_type(ctx, i->dst);
      if (uses_ptr_value(ctx, i)) op_ty = IR_TY_I32;
      const char *op = wasm_binop(i->op, op_ty);
      if (!op) wasm_unsupported_op(i->op);
      if (op_ty == IR_TY_I64 &&
          src1_ty != IR_TY_PTR && src2_ty != IR_TY_PTR &&
          (i64_runtime_extension_unsupported(ctx, i->src1) ||
           ((i->op != IR_SHL && i->op != IR_SHR && i->op != IR_LSR) &&
            i64_runtime_extension_unsupported(ctx, i->src2)))) {
        wasm_unsupported_msg("runtime i32 to i64 extension in Wasm backend");
      }
      ir_type_t dst_ty = effective_val_type(ctx, i->dst);
      ir_type_t result_ty = is_cmp ? IR_TY_I32 : op_ty;
      int result_unsigned = uses_ptr_value(ctx, i) ||
                            i->op == IR_UDIV || i->op == IR_UMOD || i->op == IR_LSR ||
                            i->op == IR_ULT || i->op == IR_ULE;
      if ((i->op == IR_MOD || i->op == IR_UMOD) &&
          i->src2.id == IR_VAL_IMM && i->src2.imm == 0) {
        wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
        emit_wasm_type_cast_prefix(op_ty, dst_ty, result_unsigned);
        emit_val_expr_as(ctx, i->src1, op_ty);
        emit_wasm_type_cast_suffix(op_ty, dst_ty);
        cg_emitf(")\n");
        return;
      }
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      emit_wasm_type_cast_prefix(result_ty, dst_ty, result_unsigned);
      cg_emitf("(%s ", op);
      emit_val_expr_as(ctx, i->src1, op_ty);
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->src2, op_ty);
      cg_emitf(")");
      emit_wasm_type_cast_suffix(result_ty, dst_ty);
      cg_emitf(")\n");
      if ((i->op == IR_ADD || i->op == IR_SUB) && !is_cmp) {
        int base_off = 0;
        global_var_t *gv = get_vreg_global_ref(ctx, i->src1.id, &base_off);
        long long delta = 0;
        if (gv && get_vreg_const(ctx, i->src2, &delta)) {
          if (i->op == IR_SUB) delta = -delta;
          set_vreg_global_ref(ctx, i->dst.id, gv, base_off + (int)delta);
        } else if (i->op == IR_ADD && get_vreg_const(ctx, i->src1, &delta)) {
          gv = get_vreg_global_ref(ctx, i->src2.id, &base_off);
          if (gv) set_vreg_global_ref(ctx, i->dst.id, gv, base_off + (int)delta);
        }
      }
      return;
    }
    case IR_CALL:
      emit_call(ctx, i, indent);
      return;
    case IR_BR:
      if (!dispatch_mode) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $pc (i32.const %d))\n", i->label_id);
      wasm_emitf(indent, "(br $dispatch)\n");
      return;
    case IR_BR_COND:
      if (!dispatch_mode) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(if ");
      emit_val_expr_as(ctx, i->src1, IR_TY_I32);
      cg_emitf("\n");
      wasm_emitf(indent + 2, "(then (local.set $pc (i32.const %d)))\n", i->label_id);
      wasm_emitf(indent + 2, "(else (local.set $pc (i32.const %d)))\n", i->else_label_id);
      wasm_emitf(indent, ")\n");
      wasm_emitf(indent, "(br $dispatch)\n");
      return;
    case IR_RET:
      if (i->ret_complex_half > 0) {
        emit_complex_ret_copy(ctx, i, indent);
        if (ctx->frame_size > 0) wasm_emitf(indent, "(global.set $__stack_pointer (local.get $old_sp))\n");
        wasm_emitf(indent, "return\n");
        return;
      }
      if (ctx->frame_size > 0) wasm_emitf(indent, "(global.set $__stack_pointer (local.get $old_sp))\n");
      if (i->src1.id != IR_VAL_NONE) {
        wasm_emitf(indent, "(return ");
        emit_val_expr_as(ctx, i->src1, ctx->f->ret_type);
        cg_emitf(")\n");
      } else {
        wasm_emitf(indent, "return\n");
      }
      return;
    default:
      wasm_unsupported_op(i->op);
  }
}

static int func_param_count(ir_func_t *f) {
  int count = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_PARAM && i->src1.id == IR_VAL_IMM && i->src1.imm >= 0) {
        count++;
      }
    }
  }
  return count + func_has_hidden_ret_area(f);
}

static ir_type_t func_param_type(ir_func_t *f, int idx) {
  if (func_has_hidden_ret_area(f)) {
    if (idx == 0) return IR_TY_PTR;
    idx--;
  }
  ir_inst_t *param = func_param_inst_at_ordinal(f, idx);
  if (param) {
    return func_param_type_from_decl(f, idx, param->dst.type);
  }
  return IR_TY_I32;
}

static int block_has_terminator(ir_block_t *b) {
  ir_inst_t *tail = b ? b->tail : NULL;
  return tail && (tail->op == IR_BR || tail->op == IR_BR_COND || tail->op == IR_RET);
}

static void emit_func(ir_func_t *f) {
  wasm_func_ctx_t ctx = {0};
  ctx.f = f;
  analyze_func(&ctx);

  int nparams = func_param_count(f);
  wasm_emitf(2, "(func $%.*s", f->name_len, f->name);
  for (int p = 0; p < nparams; p++) {
    const char *pt = wasm_type(func_param_type(f, p));
    if (!pt) wasm_unsupported_msg("non-integer Wasm function parameter");
    cg_emitf(" (param $p%d %s)", p, pt);
  }
  if (f->ret_type != IR_TY_VOID && !func_has_hidden_ret_area(f)) {
    const char *rt = wasm_type(f->ret_type);
    if (!rt) wasm_unsupported_msg("non-integer Wasm function return");
    cg_emitf(" (result %s)", rt);
  }
  cg_emitf("\n");
  for (int v = 0; v < f->next_vreg_id; v++) {
    if (!ctx.vreg_type_seen[v]) continue;
    const char *vt = wasm_type(ctx.vreg_types[v]);
    if (vt) wasm_emitf(4, "(local $v%d %s)\n", v, vt);
  }
  wasm_emitf(4, "(local $fp i32)\n");
  wasm_emitf(4, "(local $old_sp i32)\n");
  wasm_emitf(4, "(local $old_va_arg_area i32)\n");
  wasm_emitf(4, "(local $atomic_tmp_i32 i32)\n");
  wasm_emitf(4, "(local $atomic_exp_i32 i32)\n");
  wasm_emitf(4, "(local $atomic_tmp_i64 i64)\n");
  wasm_emitf(4, "(local $atomic_exp_i64 i64)\n");
  if (ctx.has_control_flow) wasm_emitf(4, "(local $pc i32)\n");
  if (ctx.frame_size > 0) {
    wasm_emitf(4, "(local.set $old_sp (global.get $__stack_pointer))\n");
    wasm_emitf(4, "(local.set $fp (i32.sub (global.get $__stack_pointer) (i32.const %d)))\n",
               ctx.frame_size);
    wasm_emitf(4, "(global.set $__stack_pointer (local.get $fp))\n");
  }
  if (ctx.has_control_flow) {
    wasm_emitf(4, "(local.set $pc (i32.const %d))\n", f->entry ? f->entry->id : 0);
    wasm_emitf(4, "(block $exit\n");
    wasm_emitf(6, "(loop $dispatch\n");
    for (ir_block_t *b = f->entry; b; b = b->next) {
      wasm_emitf(8, "(if (i32.eq (local.get $pc) (i32.const %d))\n", b->id);
      wasm_emitf(10, "(then\n");
      for (ir_inst_t *i = b->head; i; i = i->next) emit_inst(&ctx, i, 1, 12);
      if (!block_has_terminator(b)) {
        if (b->next) {
          wasm_emitf(12, "(local.set $pc (i32.const %d))\n", b->next->id);
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
    for (ir_block_t *b = f->entry; b; b = b->next) {
      for (ir_inst_t *i = b->head; i; i = i->next) emit_inst(&ctx, i, 0, 4);
    }
  }
  wasm_emitf(2, ")\n");
  if (f->name_len == 4 && memcmp(f->name, "main", 4) == 0) {
    wasm_emitf(2, "(export \"main\" (func $main))\n");
  }

  free(ctx.allocas);
  free(ctx.global_func_states);
  free(ctx.vreg_type_seen);
  free(ctx.vreg_types);
  free(ctx.vreg_unsigned);
  free(ctx.vreg_func_ref_names);
  free(ctx.vreg_func_ref_name_lens);
  free(ctx.vreg_global_refs);
  free(ctx.vreg_global_ref_offsets);
  free(ctx.vreg_const_known);
  free(ctx.vreg_const_values);
}

void wasm32_module_begin(void) {
  g_data.next_data_off = WASM_STATIC_BASE;
  g_data.symbol_count = 0;
  g_func_table.ref_count = 0;
  g_func_table.needs_table = 0;
  cg_emitf("(module\n");
}

void wasm32_gen_ir_module(ir_module_t *m) {
  if (!m) return;
  ir_opt_const_fold(m);
  ir_opt_dce(m);
  for (ir_func_t *f = m->funcs; f; f = f->next) emit_func(f);
}

static void emit_wat_escaped_byte(unsigned char c) {
  if (c == '"' || c == '\\') {
    cg_emitf("\\%02x", (unsigned)c);
  } else if (c >= 0x20 && c <= 0x7e) {
    cg_emitf("%c", c);
  } else {
    cg_emitf("\\%02x", (unsigned)c);
  }
}

static void emit_string_literal_wat_byte(unsigned char byte, void *user) {
  (void)user;
  emit_wat_escaped_byte(byte);
}

static void emit_string_literal_data(string_lit_t *lit, void *user) {
  (void)user;
  psx_string_lit_view_t view = psx_string_lit_view(lit);
  int addr = data_addr_for_string_label(view.label);
  if (addr < 0) wasm_unsupported_msg("string literal label in Wasm backend");
  wasm_emitf(2, "(data (i32.const %d) \"", addr);
  tk_emit_string_literal_bytes(view.str, view.len, (int)view.char_width, true,
                               emit_string_literal_wat_byte, NULL);
  cg_emitf("\")\n");
}

static void emit_i32_data_bytes(int addr, long long value, int size) {
  wasm_emitf(2, "(data (i32.const %d) \"", addr);
  for (int i = 0; i < size; i++) emit_wat_escaped_byte((unsigned char)((uint64_t)value >> (8 * i)));
  cg_emitf("\")\n");
}

static void emit_fp_data_bytes(int addr, tk_float_kind_t fp_kind, double value) {
  psx_gvar_fp_bits_t bits;
  if (psx_gvar_fp_bit_pattern(fp_kind, value, &bits)) {
    emit_i32_data_bytes(addr, (long long)bits.bits, bits.size);
    return;
  }
  wasm_unsupported_msg("floating global initializer in Wasm backend");
}

static int data_addr_for_symbol_ref(psx_gvar_symbol_ref_t ref) {
  if (ref.kind == PSX_GVAR_SYMBOL_REF_STRING_LITERAL) {
    return data_addr_for_string_label(ref.symbol);
  }
  if (ref.kind == PSX_GVAR_SYMBOL_REF_NAMED &&
      psx_ctx_has_function_name(ref.symbol, ref.symbol_len)) {
    return function_table_index_or_unsupported(ref.symbol, ref.symbol_len);
  }
  if (ref.kind == PSX_GVAR_SYMBOL_REF_NAMED) {
    return data_addr_for_global(ref.symbol, ref.symbol_len);
  }
  return -1;
}

static uint64_t global_init_value_bits(psx_gvar_init_value_t value,
                                       const char *symbol_error,
                                       const char *float_error) {
  if (value.kind == PSX_GVAR_INIT_VALUE_FLOAT) {
    psx_gvar_fp_bits_t bits;
    if (!psx_gvar_fp_bit_pattern(value.fp_kind, value.fvalue, &bits)) {
      wasm_unsupported_msg(float_error);
    }
    return (uint64_t)bits.bits;
  }
  if (value.kind == PSX_GVAR_INIT_VALUE_SYMBOL) {
    int sym_addr = data_addr_for_symbol_ref(value.symbol_ref);
    if (sym_addr < 0) wasm_unsupported_msg(symbol_error);
    return (uint64_t)((long long)sym_addr + value.symbol_ref.addend);
  }
  return (uint64_t)value.value;
}

typedef struct {
  int total_size;
} wasm_init_slots_data_ctx_t;

static int emit_global_init_slot_value_data(void *user, int index,
                                            psx_gvar_init_slot_value_t slot_value,
                                            const psx_gvar_init_slots_layout_t *layout) {
  wasm_init_slots_data_ctx_t *ctx = user;
  int elem = layout->elem_size;
  uint64_t value = global_init_value_bits(
      slot_value, "symbol array initializer in Wasm backend",
      "floating global initializer in Wasm backend");
  int bytes = elem;
  if ((index + 1) * elem > ctx->total_size) bytes = ctx->total_size - index * elem;
  for (int b = 0; b < bytes; b++) emit_wat_escaped_byte((unsigned char)(value >> (8 * b)));
  return 1;
}

static void emit_global_init_values_data(global_var_t *gv, int addr, int size,
                                         const psx_gvar_init_slots_layout_t *slot_layout) {
  int elem = slot_layout->elem_size;
  if (elem != 1 && elem != 2 && elem != 4 && elem != 8) wasm_unsupported_msg("global element size in Wasm backend");
  wasm_emitf(2, "(data (i32.const %d) \"", addr);
  wasm_init_slots_data_ctx_t ctx = {.total_size = size};
  if (!psx_gvar_walk_init_slot_values(gv, slot_layout, slot_layout->elem_count,
                                      emit_global_init_slot_value_data, &ctx)) {
    wasm_unsupported_msg("global array initializer in Wasm backend");
  }
  cg_emitf("\")\n");
}

static void emit_global_init_member_value_data(global_var_t *gv, int idx, int addr,
                                               const tag_member_info_t *mi) {
  psx_gvar_init_member_value_t value = psx_gvar_init_member_value(gv, idx, mi);
  if (value.size != 1 && value.size != 2 && value.size != 4 && value.size != 8) {
    wasm_unsupported_msg("global member size in Wasm backend");
  }
  if (value.kind == PSX_GVAR_INIT_VALUE_FLOAT) {
    emit_fp_data_bytes(addr, value.fp_kind, value.fvalue);
  } else {
    uint64_t bits = global_init_value_bits(
        value, "symbol global struct initializer in Wasm backend",
        "floating global struct initializer in Wasm backend");
    emit_i32_data_bytes(addr, (long long)bits, value.size);
  }
}

static void emit_global_init_member_data(global_var_t *gv, int idx, int addr,
                                         const tag_member_info_t *mi) {
  if (!mi) wasm_unsupported_msg("global struct member initializer in Wasm backend");
  emit_global_init_member_value_data(gv, idx, addr, mi);
}

static void emit_global_bitfield_member_data(global_var_t *gv, int idx, int addr,
                                             const tag_member_info_t *mi) {
  if (!mi || mi->bit_width <= 0) {
    wasm_unsupported_msg("global bitfield initializer in Wasm backend");
  }
  unsigned long long packed = psx_gvar_init_slot_bitfield_bits(gv, idx,
                                                               mi->bit_width, mi->bit_offset);
  emit_i32_data_bytes(addr + mi->offset, (long long)packed, mi->type_size);
}

typedef struct {
  global_var_t *gv;
} wasm_global_aggregate_emit_ctx_t;

static void emit_global_walk_scalar(void *user, const tag_member_info_t *mi,
                                    int idx, long long offset) {
  wasm_global_aggregate_emit_ctx_t *ctx = user;
  emit_global_init_member_data(ctx->gv, idx, (int)offset, mi);
}

static void emit_global_walk_bitfield_unit(void *user,
                                           const psx_gvar_bitfield_unit_t *unit,
                                           long long base_offset) {
  (void)user;
  emit_i32_data_bytes((int)base_offset + unit->offset,
                      (long long)unit->packed, unit->size);
}

static void emit_global_walk_bitfield_member(void *user, const tag_member_info_t *mi,
                                             int idx, long long base_offset) {
  wasm_global_aggregate_emit_ctx_t *ctx = user;
  emit_global_bitfield_member_data(ctx->gv, idx, (int)base_offset, mi);
}

static const psx_gvar_aggregate_walk_ops_t wasm_global_aggregate_walk_ops = {
    .scalar = emit_global_walk_scalar,
    .bitfield_unit = emit_global_walk_bitfield_unit,
    .bitfield_member = emit_global_walk_bitfield_member,
};

static void emit_global_struct_data(global_var_t *gv, int addr) {
  wasm_global_aggregate_emit_ctx_t ctx = {.gv = gv};
  if (!psx_gvar_walk_aggregate_initializer(gv, addr,
                                           &wasm_global_aggregate_walk_ops, &ctx)) {
    wasm_unsupported_msg("global aggregate initializer in Wasm backend");
  }
}

typedef struct {
  global_var_t *gv;
  int addr;
  int size;
} wasm_global_init_emit_ctx_t;

static int emit_global_initializer_aggregate_data(void *user,
                                                  const psx_gvar_initializer_class_t *init_class) {
  (void)init_class;
  wasm_global_init_emit_ctx_t *ctx = user;
  emit_global_struct_data(ctx->gv, ctx->addr);
  return 1;
}

static int emit_global_initializer_slots_data(void *user,
                                              const psx_gvar_init_slots_layout_t *layout,
                                              const psx_gvar_initializer_class_t *init_class) {
  (void)init_class;
  wasm_global_init_emit_ctx_t *ctx = user;
  emit_global_init_values_data(ctx->gv, ctx->addr, ctx->size, layout);
  return 1;
}

static int emit_global_initializer_scalar_data(void *user,
                                               psx_gvar_init_scalar_value_t value,
                                               const psx_gvar_initializer_class_t *init_class) {
  wasm_global_init_emit_ctx_t *ctx = user;
  if (value.kind == PSX_GVAR_INIT_VALUE_FLOAT) {
    emit_fp_data_bytes(ctx->addr, value.fp_kind, value.fvalue);
    return 1;
  }
  if (value.kind == PSX_GVAR_INIT_VALUE_SYMBOL) {
    if (value.size != 1 && value.size != 2 && value.size != 4 && value.size != 8) {
      wasm_unsupported_msg("global size in Wasm backend");
    }
    uint64_t bits = global_init_value_bits(
        value, "global symbol initializer in Wasm backend",
        "floating global initializer in Wasm backend");
    emit_i32_data_bytes(ctx->addr, (long long)bits, value.size);
    return 1;
  }
  if ((!init_class->has_payload || value.value == 0) &&
      ctx->size != 1 && ctx->size != 2 && ctx->size != 4 && ctx->size != 8) {
    return 1;
  }
  if (ctx->size != 1 && ctx->size != 2 && ctx->size != 4 && ctx->size != 8) {
    wasm_unsupported_msg("global size in Wasm backend");
  }
  emit_i32_data_bytes(ctx->addr, value.value, value.size);
  return 1;
}

static const psx_gvar_initializer_visit_ops_t wasm_global_initializer_visit_ops = {
    .aggregate = emit_global_initializer_aggregate_data,
    .slots = emit_global_initializer_slots_data,
    .scalar = emit_global_initializer_scalar_data,
};

static void emit_global_data(global_var_t *gv, void *user) {
  (void)user;
  if (psx_gvar_is_extern_decl(gv)) return;
  int addr = data_addr_for_global(psx_gvar_name(gv), psx_gvar_name_len(gv));
  int size = psx_gvar_storage_size(gv, 4);
  wasm_global_init_emit_ctx_t ctx = {.gv = gv, .addr = addr, .size = size};
  if (!psx_gvar_visit_initializer(gv, 1, size, &wasm_global_initializer_visit_ops,
                                  &ctx)) {
    wasm_unsupported_msg("global initializer in Wasm backend");
  }
}

void wasm32_emit_data_segments(void) {
  ps_iter_string_literals(emit_string_literal_data, NULL);
  ps_iter_globals(emit_global_data, NULL);
}

static int has_undefined_function(const char *name, int len) {
  return psx_ctx_has_function_name((char *)name, len) &&
         !psx_ctx_is_function_defined((char *)name, len);
}

static int has_defined_function(const char *name, int len) {
  return psx_ctx_is_function_defined((char *)name, len);
}

static void emit_minimal_static_data_if_needed(void) {
  if (has_undefined_function("setlocale", 9) || has_undefined_function("localeconv", 10)) {
    wasm_data_symbol_t *c = intern_data_symbol("__ag_stub_locale_c", 18, 2, 1);
    wasm_data_symbol_t *dot = intern_data_symbol("__ag_stub_locale_dot", 20, 2, 1);
    wasm_data_symbol_t *lc = intern_data_symbol("__ag_stub_lconv", 15, 96, 4);
    wasm_emitf(2, "(data (i32.const %d) \"C\\00\")\n", c->addr);
    wasm_emitf(2, "(data (i32.const %d) \".\\00\")\n", dot->addr);
    emit_i32_data_bytes(lc->addr, dot->addr, 4);
  }
  if (has_undefined_function("localtime", 9) || has_undefined_function("gmtime", 6) ||
      has_undefined_function("ctime", 5)) {
    wasm_data_symbol_t *tm = intern_data_symbol("__ag_stub_tm", 12, 36, 4);
    emit_i32_data_bytes(tm->addr + 12, 1, 4);
    emit_i32_data_bytes(tm->addr + 20, 70, 4);
    emit_i32_data_bytes(tm->addr + 24, 4, 4);
  }
  if (has_undefined_function("asctime", 7) || has_undefined_function("ctime", 5) ||
      has_undefined_function("strftime", 8) || has_undefined_function("wcsftime", 8)) {
    wasm_data_symbol_t *buf = intern_data_symbol("__ag_stub_asctime_buf", 21, 26, 1);
    wasm_data_symbol_t *wday = intern_data_symbol("__ag_time_wday_names", 20, 21, 1);
    wasm_data_symbol_t *mon = intern_data_symbol("__ag_time_mon_names", 19, 36, 1);
    wasm_data_symbol_t *wday_full =
        intern_data_symbol("__ag_time_wday_full_names", (int)sizeof("__ag_time_wday_full_names") - 1, 57, 1);
    wasm_data_symbol_t *mon_full =
        intern_data_symbol("__ag_time_mon_full_names", (int)sizeof("__ag_time_mon_full_names") - 1, 86, 1);
    wasm_emitf(2, "(data (i32.const %d) \"Thu Jan  1 00:00:00 1970\\0a\\00\")\n", buf->addr);
    wasm_emitf(2, "(data (i32.const %d) \"SunMonTueWedThuFriSat\")\n", wday->addr);
    wasm_emitf(2, "(data (i32.const %d) \"JanFebMarAprMayJunJulAugSepOctNovDec\")\n", mon->addr);
    wasm_emitf(2, "(data (i32.const %d) \"Sunday\\00Monday\\00Tuesday\\00Wednesday\\00Thursday\\00Friday\\00Saturday\\00\")\n", wday_full->addr);
    wasm_emitf(2, "(data (i32.const %d) \"January\\00February\\00March\\00April\\00May\\00June\\00July\\00August\\00September\\00October\\00November\\00December\\00\")\n", mon_full->addr);
  }
}

static ir_type_t wasm_function_result_type_from_decl(char *name, int name_len) {
  psx_function_ret_info_t ret = psx_ctx_get_function_ret_info(name, name_len);
  if (ret.is_pointer || ret.is_funcptr) {
    return IR_TY_PTR;
  }
  if (ret.fp_kind == TK_FLOAT_KIND_FLOAT) return IR_TY_F32;
  if (ret.fp_kind >= TK_FLOAT_KIND_DOUBLE) return IR_TY_F64;
  if (ret.token_kind == TK_LONG) return IR_TY_I64;
  if (ret.token_kind == TK_VOID) return IR_TY_VOID;
  return IR_TY_I32;
}

static void emit_wasm_u64_dec_helper(void) {
  wasm_emitf(2, "(func $__ag_write_u64_dec (param $buf i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $div i64)\n");
    wasm_emitf(4, "(local $digit i64)\n");
    wasm_emitf(4, "(local $started i32)\n");
    wasm_emitf(4, "(local $count i32)\n");
    wasm_emitf(4, "(local.set $div (i64.const 1000000000000000000))\n");
    wasm_emitf(4, "(block $done\n");
    wasm_emitf(6, "(loop $loop\n");
    wasm_emitf(8, "(local.set $digit (i64.div_u (local.get $n) (local.get $div)))\n");
    wasm_emitf(8, "(if (i32.or (i32.or (local.get $started) (i64.ne (local.get $digit) (i64.const 0))) (i64.eq (local.get $div) (i64.const 1)))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(i32.store8 (i32.add (local.get $buf) (local.get $count)) (i32.wrap_i64 (i64.add (local.get $digit) (i64.const 48))))\n");
    wasm_emitf(12, "(local.set $count (i32.add (local.get $count) (i32.const 1)))\n");
    wasm_emitf(12, "(local.set $started (i32.const 1))\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(local.set $n (i64.rem_u (local.get $n) (local.get $div)))\n");
    wasm_emitf(8, "(if (i64.eq (local.get $div) (i64.const 1)) (then (br $done)))\n");
    wasm_emitf(8, "(local.set $div (i64.div_u (local.get $div) (i64.const 10)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(local.get $count)\n");
    wasm_emitf(2, ")\n");
}

static void emit_wasm_sprintf_stub(void) {
    wasm_emitf(2, "(func $sprintf (param $buf i32) (param $fmt i32) (result i32)\n");
    wasm_emitf(4, "(local $out i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $va i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $arg i64)\n");
    wasm_emitf(4, "(local $tmp i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local.set $out (local.get $buf))\n");
    wasm_emitf(4, "(local.set $p (local.get $fmt))\n");
    wasm_emitf(4, "(local.set $va (global.get $__ag_va_arg_area))\n");
    wasm_emitf(4, "(block $done\n");
    wasm_emitf(6, "(loop $loop\n");
    wasm_emitf(8, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(8, "(if (i32.ne (local.get $ch) (i32.const 37))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(i32.store8 (local.get $out) (local.get $ch))\n");
    wasm_emitf(12, "(local.set $out (i32.add (local.get $out) (i32.const 1)))\n");
    wasm_emitf(12, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(12, "(br $loop)\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(8, "(if (i32.eq (local.get $ch) (i32.const 37))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(i32.store8 (local.get $out) (i32.const 37))\n");
    wasm_emitf(12, "(local.set $out (i32.add (local.get $out) (i32.const 1)))\n");
    wasm_emitf(12, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(12, "(br $loop)\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(if (i32.and (i32.eq (local.get $ch) (i32.const 48)) (i32.and (i32.eq (i32.load8_u (i32.add (local.get $p) (i32.const 1))) (i32.const 50)) (i32.eq (i32.load8_u (i32.add (local.get $p) (i32.const 2))) (i32.const 100))))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(local.set $arg (i64.load (local.get $va)))\n");
    wasm_emitf(12, "(local.set $va (i32.add (local.get $va) (i32.const 8)))\n");
    wasm_emitf(12, "(if (i64.lt_s (local.get $arg) (i64.const 0))\n");
    wasm_emitf(14, "(then\n");
    wasm_emitf(16, "(i32.store8 (local.get $out) (i32.const 45))\n");
    wasm_emitf(16, "(local.set $out (i32.add (local.get $out) (i32.const 1)))\n");
    wasm_emitf(16, "(local.set $arg (i64.sub (i64.const 0) (local.get $arg)))\n");
    wasm_emitf(14, ")\n");
    wasm_emitf(12, ")\n");
    wasm_emitf(12, "(if (i64.lt_u (local.get $arg) (i64.const 10))\n");
    wasm_emitf(14, "(then\n");
    wasm_emitf(16, "(i32.store8 (local.get $out) (i32.const 48))\n");
    wasm_emitf(16, "(local.set $out (i32.add (local.get $out) (i32.const 1)))\n");
    wasm_emitf(14, ")\n");
    wasm_emitf(12, ")\n");
    wasm_emitf(12, "(local.set $tmp (call $__ag_write_u64_dec (local.get $out) (local.get $arg)))\n");
    wasm_emitf(12, "(local.set $out (i32.add (local.get $out) (local.get $tmp)))\n");
    wasm_emitf(12, "(local.set $p (i32.add (local.get $p) (i32.const 3)))\n");
    wasm_emitf(12, "(br $loop)\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(if (i32.eq (local.get $ch) (i32.const 100))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(local.set $arg (i64.load (local.get $va)))\n");
    wasm_emitf(12, "(local.set $va (i32.add (local.get $va) (i32.const 8)))\n");
    wasm_emitf(12, "(if (i64.lt_s (local.get $arg) (i64.const 0))\n");
    wasm_emitf(14, "(then\n");
    wasm_emitf(16, "(i32.store8 (local.get $out) (i32.const 45))\n");
    wasm_emitf(16, "(local.set $out (i32.add (local.get $out) (i32.const 1)))\n");
    wasm_emitf(16, "(local.set $arg (i64.sub (i64.const 0) (local.get $arg)))\n");
    wasm_emitf(14, ")\n");
    wasm_emitf(12, ")\n");
    wasm_emitf(12, "(local.set $tmp (call $__ag_write_u64_dec (local.get $out) (local.get $arg)))\n");
    wasm_emitf(12, "(local.set $out (i32.add (local.get $out) (local.get $tmp)))\n");
    wasm_emitf(12, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(12, "(br $loop)\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(if (i32.eq (local.get $ch) (i32.const 117))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(local.set $arg (i64.load (local.get $va)))\n");
    wasm_emitf(12, "(local.set $va (i32.add (local.get $va) (i32.const 8)))\n");
    wasm_emitf(12, "(local.set $tmp (call $__ag_write_u64_dec (local.get $out) (local.get $arg)))\n");
    wasm_emitf(12, "(local.set $out (i32.add (local.get $out) (local.get $tmp)))\n");
    wasm_emitf(12, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(12, "(br $loop)\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(if (i32.eq (local.get $ch) (i32.const 115))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(local.set $s (i32.wrap_i64 (i64.load (local.get $va))))\n");
    wasm_emitf(12, "(local.set $va (i32.add (local.get $va) (i32.const 8)))\n");
    wasm_emitf(12, "(block $str_done\n");
    wasm_emitf(14, "(loop $str_loop\n");
    wasm_emitf(16, "(local.set $ch (i32.load8_u (local.get $s)))\n");
    wasm_emitf(16, "(if (i32.eqz (local.get $ch)) (then (br $str_done)))\n");
    wasm_emitf(16, "(i32.store8 (local.get $out) (local.get $ch))\n");
    wasm_emitf(16, "(local.set $out (i32.add (local.get $out) (i32.const 1)))\n");
    wasm_emitf(16, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(16, "(br $str_loop)\n");
    wasm_emitf(14, ")\n");
    wasm_emitf(12, ")\n");
    wasm_emitf(12, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(12, "(br $loop)\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(if (i32.eq (local.get $ch) (i32.const 99))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(i32.store8 (local.get $out) (i32.wrap_i64 (i64.load (local.get $va))))\n");
    wasm_emitf(12, "(local.set $va (i32.add (local.get $va) (i32.const 8)))\n");
    wasm_emitf(12, "(local.set $out (i32.add (local.get $out) (i32.const 1)))\n");
    wasm_emitf(12, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(12, "(br $loop)\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(i32.store8 (local.get $out) (i32.const 37))\n");
    wasm_emitf(8, "(i32.store8 (i32.add (local.get $out) (i32.const 1)) (local.get $ch))\n");
    wasm_emitf(8, "(local.set $out (i32.add (local.get $out) (i32.const 2)))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(i32.store8 (local.get $out) (i32.const 0))\n");
    wasm_emitf(4, "(i32.sub (local.get $out) (local.get $buf))\n");
    wasm_emitf(2, ")\n");
}

static void emit_wasm_snprintf_stubs(void) {
    wasm_emitf(2, "(func $__ag_snputc (param $buf i32) (param $size i64) (param $pos i32) (param $ch i32) (result i32)\n");
    wasm_emitf(4, "(if (i32.and (i64.ne (local.get $size) (i64.const 0)) (i64.lt_u (i64.extend_i32_u (i32.add (local.get $pos) (i32.const 1))) (local.get $size)))\n");
    wasm_emitf(6, "(then (i32.store8 (i32.add (local.get $buf) (local.get $pos)) (local.get $ch)))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(i32.add (local.get $pos) (i32.const 1))\n");
    wasm_emitf(2, ")\n");
    wasm_emitf(2, "(func $__ag_snwrite_u64_dec (param $buf i32) (param $size i64) (param $pos i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $div i64)\n");
    wasm_emitf(4, "(local $digit i64)\n");
    wasm_emitf(4, "(local $started i32)\n");
    wasm_emitf(4, "(local.set $div (i64.const 1000000000000000000))\n");
    wasm_emitf(4, "(block $done\n");
    wasm_emitf(6, "(loop $loop\n");
    wasm_emitf(8, "(local.set $digit (i64.div_u (local.get $n) (local.get $div)))\n");
    wasm_emitf(8, "(if (i32.or (i32.or (local.get $started) (i64.ne (local.get $digit) (i64.const 0))) (i64.eq (local.get $div) (i64.const 1)))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(local.set $pos (call $__ag_snputc (local.get $buf) (local.get $size) (local.get $pos) (i32.wrap_i64 (i64.add (local.get $digit) (i64.const 48)))))\n");
    wasm_emitf(12, "(local.set $started (i32.const 1))\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(local.set $n (i64.rem_u (local.get $n) (local.get $div)))\n");
    wasm_emitf(8, "(if (i64.eq (local.get $div) (i64.const 1)) (then (br $done)))\n");
    wasm_emitf(8, "(local.set $div (i64.div_u (local.get $div) (i64.const 10)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(local.get $pos)\n");
    wasm_emitf(2, ")\n");
    wasm_emitf(2, "(func $snprintf (param $buf i32) (param $size i64) (param $fmt i32) (param $a i64) (param $b i64) (result i32)\n");
    wasm_emitf(4, "(local $pos i32)\n");
    wasm_emitf(4, "(local $arg i64)\n");
    wasm_emitf(4, "(local $nul i32)\n");
    wasm_emitf(4, "(block $finish\n");
    wasm_emitf(4, "(if (i32.and (i32.eq (i32.load8_u (i32.add (local.get $fmt) (i32.const 1))) (i32.const 48)) (i32.and (i32.eq (i32.load8_u (i32.add (local.get $fmt) (i32.const 2))) (i32.const 50)) (i32.eq (i32.load8_u (i32.add (local.get $fmt) (i32.const 3))) (i32.const 100))))\n");
    wasm_emitf(6, "(then\n");
    wasm_emitf(8, "(local.set $arg (local.get $a))\n");
    wasm_emitf(8, "(if (i64.lt_s (local.get $arg) (i64.const 0))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(local.set $pos (call $__ag_snputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 45)))\n");
    wasm_emitf(12, "(local.set $arg (i64.sub (i64.const 0) (local.get $arg)))\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(if (i64.lt_u (local.get $arg) (i64.const 10))\n");
    wasm_emitf(10, "(then (local.set $pos (call $__ag_snputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 48))))\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_snwrite_u64_dec (local.get $buf) (local.get $size) (local.get $pos) (local.get $arg)))\n");
    wasm_emitf(8, "(br $finish)\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(if (i32.and (i32.eq (i32.load8_u (i32.add (local.get $fmt) (i32.const 1))) (i32.const 122)) (i32.eq (i32.load8_u (i32.add (local.get $fmt) (i32.const 2))) (i32.const 117)))\n");
    wasm_emitf(6, "(then\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_snwrite_u64_dec (local.get $buf) (local.get $size) (local.get $pos) (local.get $a)))\n");
    wasm_emitf(8, "(br $finish)\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(if (i32.eq (i32.load8_u (i32.add (local.get $fmt) (i32.const 1))) (i32.const 117))\n");
    wasm_emitf(6, "(then\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_snwrite_u64_dec (local.get $buf) (local.get $size) (local.get $pos) (local.get $a)))\n");
    wasm_emitf(8, "(br $finish)\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(local.set $arg (local.get $a))\n");
    wasm_emitf(4, "(if (i64.lt_s (local.get $arg) (i64.const 0))\n");
    wasm_emitf(6, "(then\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_snputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 45)))\n");
    wasm_emitf(8, "(local.set $arg (i64.sub (i64.const 0) (local.get $arg)))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(local.set $pos (call $__ag_snwrite_u64_dec (local.get $buf) (local.get $size) (local.get $pos) (local.get $arg)))\n");
    wasm_emitf(4, "(if (i32.eq (i32.load8_u (i32.add (local.get $fmt) (i32.const 2))) (i32.const 45))\n");
    wasm_emitf(6, "(then\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_snputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 45)))\n");
    wasm_emitf(8, "(local.set $arg (local.get $b))\n");
    wasm_emitf(8, "(if (i64.lt_s (local.get $arg) (i64.const 0))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(local.set $pos (call $__ag_snputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 45)))\n");
    wasm_emitf(12, "(local.set $arg (i64.sub (i64.const 0) (local.get $arg)))\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_snwrite_u64_dec (local.get $buf) (local.get $size) (local.get $pos) (local.get $arg)))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(if (i64.ne (local.get $size) (i64.const 0))\n");
    wasm_emitf(6, "(then\n");
    wasm_emitf(8, "(if (result i32) (i64.lt_u (i64.extend_i32_u (local.get $pos)) (local.get $size))\n");
    wasm_emitf(10, "(then (local.set $nul (local.get $pos)) (local.get $pos))\n");
    wasm_emitf(10, "(else (local.set $nul (i32.wrap_i64 (i64.sub (local.get $size) (i64.const 1)))) (local.get $nul))\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(drop)\n");
    wasm_emitf(8, "(i32.store8 (i32.add (local.get $buf) (local.get $nul)) (i32.const 0))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(local.get $pos)\n");
    wasm_emitf(2, ")\n");
}

static void emit_wasm_swprintf_stub(void) {
    wasm_emitf(2, "(func $__ag_snwputc (param $buf i32) (param $size i64) (param $pos i32) (param $ch i32) (result i32)\n");
    wasm_emitf(4, "(if (i32.and (i64.ne (local.get $size) (i64.const 0)) (i64.lt_u (i64.extend_i32_u (i32.add (local.get $pos) (i32.const 1))) (local.get $size)))\n");
    wasm_emitf(6, "(then (i32.store (i32.add (local.get $buf) (i32.mul (local.get $pos) (i32.const 4))) (local.get $ch)))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(i32.add (local.get $pos) (i32.const 1))\n");
    wasm_emitf(2, ")\n");
    wasm_emitf(2, "(func $__ag_snwwrite_u64_dec (param $buf i32) (param $size i64) (param $pos i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $div i64)\n");
    wasm_emitf(4, "(local $digit i64)\n");
    wasm_emitf(4, "(local $started i32)\n");
    wasm_emitf(4, "(local.set $div (i64.const 1000000000000000000))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $digit (i64.div_u (local.get $n) (local.get $div)))\n");
    wasm_emitf(6, "(if (i32.or (i32.or (local.get $started) (i64.ne (local.get $digit) (i64.const 0))) (i64.eq (local.get $div) (i64.const 1))) (then\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_snwputc (local.get $buf) (local.get $size) (local.get $pos) (i32.wrap_i64 (i64.add (local.get $digit) (i64.const 48)))))\n");
    wasm_emitf(8, "(local.set $started (i32.const 1))\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(local.set $n (i64.rem_u (local.get $n) (local.get $div)))\n");
    wasm_emitf(6, "(if (i64.eq (local.get $div) (i64.const 1)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $div (i64.div_u (local.get $div) (i64.const 10)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $pos)\n");
    wasm_emitf(2, ")\n");
    wasm_emitf(2, "(func $__ag_swprintf_next_arg (param $which i32) (param $a i64) (param $b i64) (result i64)\n");
    wasm_emitf(4, "(if (result i64) (i32.eqz (local.get $which)) (then (local.get $a)) (else (local.get $b)))\n");
    wasm_emitf(2, ")\n");
    wasm_emitf(2, "(func $swprintf (param $buf i32) (param $size i64) (param $fmt i32) (param $a i64) (param $b i64) (result i32)\n");
    wasm_emitf(4, "(local $pos i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $arg i64)\n");
    wasm_emitf(4, "(local $arg_idx i32)\n");
    wasm_emitf(4, "(local $nul i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $fmt))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ch) (i32.const 37)) (then\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_snwputc (local.get $buf) (local.get $size) (local.get $pos) (local.get $ch)))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 37)) (then\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_snwputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 37)))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (i32.and (i32.eq (local.get $ch) (i32.const 48)) (i32.and (i32.eq (i32.load (i32.add (local.get $p) (i32.const 4))) (i32.const 50)) (i32.eq (i32.load (i32.add (local.get $p) (i32.const 8))) (i32.const 100)))) (then\n");
    wasm_emitf(8, "(local.set $arg (call $__ag_swprintf_next_arg (local.get $arg_idx) (local.get $a) (local.get $b)))\n");
    wasm_emitf(8, "(local.set $arg_idx (i32.add (local.get $arg_idx) (i32.const 1)))\n");
    wasm_emitf(8, "(if (i64.lt_s (local.get $arg) (i64.const 0)) (then\n");
    wasm_emitf(10, "(local.set $pos (call $__ag_snwputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 45)))\n");
    wasm_emitf(10, "(local.set $arg (i64.sub (i64.const 0) (local.get $arg)))\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(8, "(if (i64.lt_u (local.get $arg) (i64.const 10)) (then (local.set $pos (call $__ag_snwputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 48)))))\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_snwwrite_u64_dec (local.get $buf) (local.get $size) (local.get $pos) (local.get $arg)))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 12)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (i32.or (i32.eq (local.get $ch) (i32.const 100)) (i32.eq (local.get $ch) (i32.const 117))) (then\n");
    wasm_emitf(8, "(local.set $arg (call $__ag_swprintf_next_arg (local.get $arg_idx) (local.get $a) (local.get $b)))\n");
    wasm_emitf(8, "(local.set $arg_idx (i32.add (local.get $arg_idx) (i32.const 1)))\n");
    wasm_emitf(8, "(if (i32.eq (local.get $ch) (i32.const 100)) (then\n");
    wasm_emitf(10, "(if (i64.lt_s (local.get $arg) (i64.const 0)) (then\n");
    wasm_emitf(12, "(local.set $pos (call $__ag_snwputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 45)))\n");
    wasm_emitf(12, "(local.set $arg (i64.sub (i64.const 0) (local.get $arg)))\n");
    wasm_emitf(10, "))\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_snwwrite_u64_dec (local.get $buf) (local.get $size) (local.get $pos) (local.get $arg)))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(local.set $pos (call $__ag_snwputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 37)))\n");
    wasm_emitf(6, "(local.set $pos (call $__ag_snwputc (local.get $buf) (local.get $size) (local.get $pos) (local.get $ch)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i64.ne (local.get $size) (i64.const 0)) (then\n");
    wasm_emitf(6, "(if (result i32) (i64.lt_u (i64.extend_i32_u (local.get $pos)) (local.get $size))\n");
    wasm_emitf(8, "(then (local.set $nul (local.get $pos)) (local.get $pos))\n");
    wasm_emitf(8, "(else (local.set $nul (i32.wrap_i64 (i64.sub (local.get $size) (i64.const 1)))) (local.get $nul))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(6, "(drop)\n");
    wasm_emitf(6, "(i32.store (i32.add (local.get $buf) (i32.mul (local.get $nul) (i32.const 4))) (i32.const 0))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $pos)\n");
    wasm_emitf(2, ")\n");
}

static void emit_wasm_vsnprintf_stubs(void) {
    wasm_emitf(2, "(func $__ag_vsnputc (param $buf i32) (param $size i64) (param $pos i32) (param $ch i32) (result i32)\n");
    wasm_emitf(4, "(if (i32.and (i64.ne (local.get $size) (i64.const 0)) (i64.lt_u (i64.extend_i32_u (i32.add (local.get $pos) (i32.const 1))) (local.get $size)))\n");
    wasm_emitf(6, "(then (i32.store8 (i32.add (local.get $buf) (local.get $pos)) (local.get $ch)))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(i32.add (local.get $pos) (i32.const 1))\n");
    wasm_emitf(2, ")\n");
    wasm_emitf(2, "(func $__ag_vsnwrite_u64_dec (param $buf i32) (param $size i64) (param $pos i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $div i64)\n");
    wasm_emitf(4, "(local $digit i64)\n");
    wasm_emitf(4, "(local $started i32)\n");
    wasm_emitf(4, "(local.set $div (i64.const 1000000000000000000))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $digit (i64.div_u (local.get $n) (local.get $div)))\n");
    wasm_emitf(6, "(if (i32.or (i32.or (local.get $started) (i64.ne (local.get $digit) (i64.const 0))) (i64.eq (local.get $div) (i64.const 1))) (then\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_vsnputc (local.get $buf) (local.get $size) (local.get $pos) (i32.wrap_i64 (i64.add (local.get $digit) (i64.const 48)))))\n");
    wasm_emitf(8, "(local.set $started (i32.const 1))\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(local.set $n (i64.rem_u (local.get $n) (local.get $div)))\n");
    wasm_emitf(6, "(if (i64.eq (local.get $div) (i64.const 1)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $div (i64.div_u (local.get $div) (i64.const 10)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $pos)\n");
    wasm_emitf(2, ")\n");
    wasm_emitf(2, "(func $__ag_vsnprintf_impl (param $buf i32) (param $size i64) (param $fmt i32) (param $ap i64) (result i32)\n");
    wasm_emitf(4, "(local $pos i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $vap i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $arg i64)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $nul i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $fmt))\n");
    wasm_emitf(4, "(local.set $vap (i32.wrap_i64 (local.get $ap)))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ch) (i32.const 37)) (then\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_vsnputc (local.get $buf) (local.get $size) (local.get $pos) (local.get $ch)))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 37)) (then\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_vsnputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 37)))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (i32.and (i32.eq (local.get $ch) (i32.const 48)) (i32.and (i32.eq (i32.load8_u (i32.add (local.get $p) (i32.const 1))) (i32.const 50)) (i32.eq (i32.load8_u (i32.add (local.get $p) (i32.const 2))) (i32.const 100)))) (then\n");
    wasm_emitf(8, "(local.set $arg (i64.load (local.get $vap)))\n");
    wasm_emitf(8, "(local.set $vap (i32.add (local.get $vap) (i32.const 8)))\n");
    wasm_emitf(8, "(if (i64.lt_s (local.get $arg) (i64.const 0)) (then\n");
    wasm_emitf(10, "(local.set $pos (call $__ag_vsnputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 45)))\n");
    wasm_emitf(10, "(local.set $arg (i64.sub (i64.const 0) (local.get $arg)))\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(8, "(if (i64.lt_u (local.get $arg) (i64.const 10)) (then (local.set $pos (call $__ag_vsnputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 48)))))\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_vsnwrite_u64_dec (local.get $buf) (local.get $size) (local.get $pos) (local.get $arg)))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 3)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (i32.or (i32.eq (local.get $ch) (i32.const 100)) (i32.eq (local.get $ch) (i32.const 117))) (then\n");
    wasm_emitf(8, "(local.set $arg (i64.load (local.get $vap)))\n");
    wasm_emitf(8, "(local.set $vap (i32.add (local.get $vap) (i32.const 8)))\n");
    wasm_emitf(8, "(if (i32.eq (local.get $ch) (i32.const 100)) (then\n");
    wasm_emitf(10, "(if (i64.lt_s (local.get $arg) (i64.const 0)) (then\n");
    wasm_emitf(12, "(local.set $pos (call $__ag_vsnputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 45)))\n");
    wasm_emitf(12, "(local.set $arg (i64.sub (i64.const 0) (local.get $arg)))\n");
    wasm_emitf(10, "))\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_vsnwrite_u64_dec (local.get $buf) (local.get $size) (local.get $pos) (local.get $arg)))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 115)) (then\n");
    wasm_emitf(8, "(local.set $s (i32.wrap_i64 (i64.load (local.get $vap))))\n");
    wasm_emitf(8, "(local.set $vap (i32.add (local.get $vap) (i32.const 8)))\n");
    wasm_emitf(8, "(block $str_done (loop $str_loop\n");
    wasm_emitf(10, "(local.set $ch (i32.load8_u (local.get $s)))\n");
    wasm_emitf(10, "(if (i32.eqz (local.get $ch)) (then (br $str_done)))\n");
    wasm_emitf(10, "(local.set $pos (call $__ag_vsnputc (local.get $buf) (local.get $size) (local.get $pos) (local.get $ch)))\n");
    wasm_emitf(10, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(10, "(br $str_loop)\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 99)) (then\n");
    wasm_emitf(8, "(local.set $arg (i64.load (local.get $vap)))\n");
    wasm_emitf(8, "(local.set $vap (i32.add (local.get $vap) (i32.const 8)))\n");
    wasm_emitf(8, "(local.set $pos (call $__ag_vsnputc (local.get $buf) (local.get $size) (local.get $pos) (i32.wrap_i64 (local.get $arg))))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(local.set $pos (call $__ag_vsnputc (local.get $buf) (local.get $size) (local.get $pos) (i32.const 37)))\n");
    wasm_emitf(6, "(local.set $pos (call $__ag_vsnputc (local.get $buf) (local.get $size) (local.get $pos) (local.get $ch)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i64.ne (local.get $size) (i64.const 0)) (then\n");
    wasm_emitf(6, "(if (result i32) (i64.lt_u (i64.extend_i32_u (local.get $pos)) (local.get $size))\n");
    wasm_emitf(8, "(then (local.set $nul (local.get $pos)) (local.get $pos))\n");
    wasm_emitf(8, "(else (local.set $nul (i32.wrap_i64 (i64.sub (local.get $size) (i64.const 1)))) (local.get $nul))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(6, "(drop)\n");
    wasm_emitf(6, "(i32.store8 (i32.add (local.get $buf) (local.get $nul)) (i32.const 0))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $pos)\n");
    wasm_emitf(2, ")\n");
    if (has_undefined_function("vsnprintf", 9)) {
      wasm_emitf(2, "(func $vsnprintf (param $buf i32) (param $size i64) (param $fmt i32) (param $ap i64) (result i32)\n");
      wasm_emitf(4, "(call $__ag_vsnprintf_impl (local.get $buf) (local.get $size) (local.get $fmt) (local.get $ap))\n");
      wasm_emitf(2, ")\n");
    }
    if (has_undefined_function("vsprintf", 8)) {
      wasm_emitf(2, "(func $vsprintf (param $buf i32) (param $fmt i32) (param $ap i64) (result i32)\n");
      wasm_emitf(4, "(call $__ag_vsnprintf_impl (local.get $buf) (i64.const 2147483647) (local.get $fmt) (local.get $ap))\n");
      wasm_emitf(2, ")\n");
    }
    if (has_undefined_function("vprintf", 7)) {
      wasm_emitf(2, "(func $vprintf (param $fmt i32) (param $ap i64) (result i32)\n");
      wasm_emitf(4, "(call $__ag_vsnprintf_impl (i32.const 0) (i64.const 0) (local.get $fmt) (local.get $ap))\n");
      wasm_emitf(2, ")\n");
    }
    if (has_undefined_function("vfprintf", 8)) {
      wasm_emitf(2, "(func $vfprintf (param $stream i32) (param $fmt i32) (param $ap i64) (result i32)\n");
      wasm_emitf(4, "(call $__ag_vsnprintf_impl (i32.const 0) (i64.const 0) (local.get $fmt) (local.get $ap))\n");
      wasm_emitf(2, ")\n");
    }
}

static void emit_wasm_printf_stubs(void) {
  int slots_addr = intern_data_symbol("__ag_printf_va_slots", 20, 16, 8)->addr;
  if (has_undefined_function("printf", 6)) {
    wasm_emitf(2, "(func $printf (param $fmt i32) (param $a i64) (param $b i64) (result i32)\n");
    wasm_emitf(4, "(i64.store (i32.const %d) (local.get $a))\n", slots_addr);
    wasm_emitf(4, "(i64.store (i32.const %d) (local.get $b))\n", slots_addr + 8);
    wasm_emitf(4, "(call $__ag_vsnprintf_impl (i32.const 0) (i64.const 0) (local.get $fmt) (i64.const %d))\n", slots_addr);
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fprintf", 7)) {
    wasm_emitf(2, "(func $fprintf (param $stream i32) (param $fmt i32) (param $a i64) (param $b i64) (result i32)\n");
    wasm_emitf(4, "(i64.store (i32.const %d) (local.get $a))\n", slots_addr);
    wasm_emitf(4, "(i64.store (i32.const %d) (local.get $b))\n", slots_addr + 8);
    wasm_emitf(4, "(call $__ag_vsnprintf_impl (i32.const 0) (i64.const 0) (local.get $fmt) (i64.const %d))\n", slots_addr);
    wasm_emitf(2, ")\n");
  }
}

static void emit_wasm_vsscanf_stubs(void) {
  int slots_addr = intern_data_symbol("__ag_sscanf_va_slots", 20, 16, 8)->addr;
  wasm_emitf(2, "(func $__ag_scan_isspace (param $ch i32) (result i32)\n");
  wasm_emitf(4, "(i32.or (i32.or (i32.eq (local.get $ch) (i32.const 32)) (i32.eq (local.get $ch) (i32.const 9))) (i32.or (i32.eq (local.get $ch) (i32.const 10)) (i32.eq (local.get $ch) (i32.const 13))))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_scan_skip_spaces (param $p i32) (result i32)\n");
  wasm_emitf(4, "(block $done (loop $loop\n");
  wasm_emitf(6, "(if (i32.eqz (call $__ag_scan_isspace (i32.load8_u (local.get $p)))) (then (br $done)))\n");
  wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
  wasm_emitf(6, "(br $loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(local.get $p)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_scan_isdigit (param $ch i32) (result i32)\n");
  wasm_emitf(4, "(i32.and (i32.ge_u (local.get $ch) (i32.const 48)) (i32.le_u (local.get $ch) (i32.const 57)))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_scan_int (param $p i32) (param $dst i32) (param $is_signed i32) (result i32)\n");
  wasm_emitf(4, "(local $ch i32)\n");
  wasm_emitf(4, "(local $digits i32)\n");
  wasm_emitf(4, "(local $neg i32)\n");
  wasm_emitf(4, "(local $val i64)\n");
  wasm_emitf(4, "(local.set $p (call $__ag_scan_skip_spaces (local.get $p)))\n");
  wasm_emitf(4, "(local.set $ch (i32.load8_u (local.get $p)))\n");
  wasm_emitf(4, "(if (local.get $is_signed) (then\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 45)) (then\n");
  wasm_emitf(8, "(local.set $neg (i32.const 1))\n");
  wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $ch (i32.load8_u (local.get $p)))\n");
  wasm_emitf(6, ") (else (if (i32.eq (local.get $ch) (i32.const 43)) (then\n");
  wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $ch (i32.load8_u (local.get $p)))\n");
  wasm_emitf(6, "))))\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(block $done (loop $loop\n");
  wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
  wasm_emitf(6, "(if (i32.eqz (call $__ag_scan_isdigit (local.get $ch))) (then (br $done)))\n");
  wasm_emitf(6, "(local.set $val (i64.add (i64.mul (local.get $val) (i64.const 10)) (i64.extend_i32_u (i32.sub (local.get $ch) (i32.const 48)))))\n");
  wasm_emitf(6, "(local.set $digits (i32.add (local.get $digits) (i32.const 1)))\n");
  wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
  wasm_emitf(6, "(br $loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(if (i32.eqz (local.get $digits)) (then (return (i32.const 0))))\n");
  wasm_emitf(4, "(if (local.get $dst) (then\n");
  wasm_emitf(6, "(if (local.get $neg)\n");
  wasm_emitf(8, "(then (i32.store (local.get $dst) (i32.wrap_i64 (i64.sub (i64.const 0) (local.get $val)))))\n");
  wasm_emitf(8, "(else (i32.store (local.get $dst) (i32.wrap_i64 (local.get $val))))\n");
  wasm_emitf(6, ")\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(local.get $p)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_scan_word (param $p i32) (param $dst i32) (result i32)\n");
  wasm_emitf(4, "(local $ch i32)\n");
  wasm_emitf(4, "(local $out i32)\n");
  wasm_emitf(4, "(local $count i32)\n");
  wasm_emitf(4, "(local.set $p (call $__ag_scan_skip_spaces (local.get $p)))\n");
  wasm_emitf(4, "(local.set $out (local.get $dst))\n");
  wasm_emitf(4, "(local.set $ch (i32.load8_u (local.get $p)))\n");
  wasm_emitf(4, "(if (i32.or (i32.eqz (local.get $ch)) (call $__ag_scan_isspace (local.get $ch))) (then (return (i32.const 0))))\n");
  wasm_emitf(4, "(block $done (loop $loop\n");
  wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
  wasm_emitf(6, "(if (i32.or (i32.eqz (local.get $ch)) (call $__ag_scan_isspace (local.get $ch))) (then (br $done)))\n");
  wasm_emitf(6, "(if (local.get $out) (then (i32.store8 (local.get $out) (local.get $ch)) (local.set $out (i32.add (local.get $out) (i32.const 1)))))\n");
  wasm_emitf(6, "(local.set $count (i32.add (local.get $count) (i32.const 1)))\n");
  wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
  wasm_emitf(6, "(br $loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(if (local.get $out) (then (i32.store8 (local.get $out) (i32.const 0))))\n");
  wasm_emitf(4, "(if (i32.eqz (local.get $count)) (then (return (i32.const 0))))\n");
  wasm_emitf(4, "(local.get $p)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_scan_char (param $p i32) (param $dst i32) (result i32)\n");
  wasm_emitf(4, "(local $ch i32)\n");
  wasm_emitf(4, "(local.set $ch (i32.load8_u (local.get $p)))\n");
  wasm_emitf(4, "(if (i32.eqz (local.get $ch)) (then (return (i32.const 0))))\n");
  wasm_emitf(4, "(if (local.get $dst) (then (i32.store8 (local.get $dst) (local.get $ch))))\n");
  wasm_emitf(4, "(i32.add (local.get $p) (i32.const 1))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_vsscanf_impl (param $s i32) (param $fmt i32) (param $ap i64) (result i32)\n");
  wasm_emitf(4, "(local $p i32)\n");
  wasm_emitf(4, "(local $f i32)\n");
  wasm_emitf(4, "(local $vap i32)\n");
  wasm_emitf(4, "(local $ch i32)\n");
  wasm_emitf(4, "(local $spec i32)\n");
  wasm_emitf(4, "(local $dst i32)\n");
  wasm_emitf(4, "(local $next i32)\n");
  wasm_emitf(4, "(local $assigns i32)\n");
  wasm_emitf(4, "(local.set $p (local.get $s))\n");
  wasm_emitf(4, "(local.set $f (local.get $fmt))\n");
  wasm_emitf(4, "(local.set $vap (i32.wrap_i64 (local.get $ap)))\n");
  wasm_emitf(4, "(block $done (loop $loop\n");
  wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $f)))\n");
  wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
  wasm_emitf(6, "(if (call $__ag_scan_isspace (local.get $ch)) (then\n");
  wasm_emitf(8, "(block $fmt_space_done (loop $fmt_space\n");
  wasm_emitf(10, "(if (i32.eqz (call $__ag_scan_isspace (i32.load8_u (local.get $f)))) (then (br $fmt_space_done)))\n");
  wasm_emitf(10, "(local.set $f (i32.add (local.get $f) (i32.const 1)))\n");
  wasm_emitf(10, "(br $fmt_space)\n");
  wasm_emitf(8, "))\n");
  wasm_emitf(8, "(local.set $p (call $__ag_scan_skip_spaces (local.get $p)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.ne (local.get $ch) (i32.const 37)) (then\n");
  wasm_emitf(8, "(if (i32.ne (i32.load8_u (local.get $p)) (local.get $ch)) (then (br $done)))\n");
  wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $f (i32.add (local.get $f) (i32.const 1)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(local.set $f (i32.add (local.get $f) (i32.const 1)))\n");
  wasm_emitf(6, "(local.set $spec (i32.load8_u (local.get $f)))\n");
  wasm_emitf(6, "(if (i32.eqz (local.get $spec)) (then (br $done)))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $spec) (i32.const 37)) (then\n");
  wasm_emitf(8, "(if (i32.ne (i32.load8_u (local.get $p)) (i32.const 37)) (then (br $done)))\n");
  wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $f (i32.add (local.get $f) (i32.const 1)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(local.set $dst (i32.wrap_i64 (i64.load (local.get $vap))))\n");
  wasm_emitf(6, "(local.set $vap (i32.add (local.get $vap) (i32.const 8)))\n");
  wasm_emitf(6, "(if (i32.or (i32.eq (local.get $spec) (i32.const 100)) (i32.eq (local.get $spec) (i32.const 117))) (then\n");
  wasm_emitf(8, "(local.set $next (call $__ag_scan_int (local.get $p) (local.get $dst) (i32.eq (local.get $spec) (i32.const 100))))\n");
  wasm_emitf(8, "(if (i32.eqz (local.get $next)) (then (br $done)))\n");
  wasm_emitf(8, "(local.set $p (local.get $next))\n");
  wasm_emitf(8, "(local.set $assigns (i32.add (local.get $assigns) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $f (i32.add (local.get $f) (i32.const 1)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $spec) (i32.const 115)) (then\n");
  wasm_emitf(8, "(local.set $next (call $__ag_scan_word (local.get $p) (local.get $dst)))\n");
  wasm_emitf(8, "(if (i32.eqz (local.get $next)) (then (br $done)))\n");
  wasm_emitf(8, "(local.set $p (local.get $next))\n");
  wasm_emitf(8, "(local.set $assigns (i32.add (local.get $assigns) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $f (i32.add (local.get $f) (i32.const 1)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $spec) (i32.const 99)) (then\n");
  wasm_emitf(8, "(local.set $next (call $__ag_scan_char (local.get $p) (local.get $dst)))\n");
  wasm_emitf(8, "(if (i32.eqz (local.get $next)) (then (br $done)))\n");
  wasm_emitf(8, "(local.set $p (local.get $next))\n");
  wasm_emitf(8, "(local.set $assigns (i32.add (local.get $assigns) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $f (i32.add (local.get $f) (i32.const 1)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(br $done)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(local.get $assigns)\n");
  wasm_emitf(2, ")\n");
  if (has_undefined_function("vsscanf", 7)) {
    wasm_emitf(2, "(func $vsscanf (param $s i32) (param $fmt i32) (param $ap i64) (result i32)\n");
    wasm_emitf(4, "(call $__ag_vsscanf_impl (local.get $s) (local.get $fmt) (local.get $ap))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("sscanf", 6)) {
    wasm_emitf(2, "(func $sscanf (param $s i32) (param $fmt i32) (param $a i32) (param $b i32) (result i32)\n");
    wasm_emitf(4, "(i64.store (i32.const %d) (i64.extend_i32_u (local.get $a)))\n", slots_addr);
    wasm_emitf(4, "(i64.store (i32.const %d) (i64.extend_i32_u (local.get $b)))\n", slots_addr + 8);
    wasm_emitf(4, "(call $__ag_vsscanf_impl (local.get $s) (local.get $fmt) (i64.const %d))\n", slots_addr);
    wasm_emitf(2, ")\n");
  }
}

static void emit_wasm_swscanf_stub(void) {
  wasm_emitf(2, "(func $__ag_wscan_isspace (param $ch i32) (result i32)\n");
  wasm_emitf(4, "(i32.or (i32.or (i32.eq (local.get $ch) (i32.const 32)) (i32.eq (local.get $ch) (i32.const 9))) (i32.or (i32.eq (local.get $ch) (i32.const 10)) (i32.eq (local.get $ch) (i32.const 13))))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_wscan_skip_spaces (param $p i32) (result i32)\n");
  wasm_emitf(4, "(block $done (loop $loop\n");
  wasm_emitf(6, "(if (i32.eqz (call $__ag_wscan_isspace (i32.load (local.get $p)))) (then (br $done)))\n");
  wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
  wasm_emitf(6, "(br $loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(local.get $p)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_wscan_isdigit (param $ch i32) (result i32)\n");
  wasm_emitf(4, "(i32.and (i32.ge_u (local.get $ch) (i32.const 48)) (i32.le_u (local.get $ch) (i32.const 57)))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_wscan_int (param $p i32) (param $dst i32) (param $is_signed i32) (result i32)\n");
  wasm_emitf(4, "(local $ch i32)\n");
  wasm_emitf(4, "(local $digits i32)\n");
  wasm_emitf(4, "(local $neg i32)\n");
  wasm_emitf(4, "(local $val i64)\n");
  wasm_emitf(4, "(local.set $p (call $__ag_wscan_skip_spaces (local.get $p)))\n");
  wasm_emitf(4, "(local.set $ch (i32.load (local.get $p)))\n");
  wasm_emitf(4, "(if (local.get $is_signed) (then\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 45)) (then\n");
  wasm_emitf(8, "(local.set $neg (i32.const 1))\n");
  wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
  wasm_emitf(8, "(local.set $ch (i32.load (local.get $p)))\n");
  wasm_emitf(6, ") (else (if (i32.eq (local.get $ch) (i32.const 43)) (then\n");
  wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
  wasm_emitf(8, "(local.set $ch (i32.load (local.get $p)))\n");
  wasm_emitf(6, "))))\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(block $done (loop $loop\n");
  wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
  wasm_emitf(6, "(if (i32.eqz (call $__ag_wscan_isdigit (local.get $ch))) (then (br $done)))\n");
  wasm_emitf(6, "(local.set $val (i64.add (i64.mul (local.get $val) (i64.const 10)) (i64.extend_i32_u (i32.sub (local.get $ch) (i32.const 48)))))\n");
  wasm_emitf(6, "(local.set $digits (i32.add (local.get $digits) (i32.const 1)))\n");
  wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
  wasm_emitf(6, "(br $loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(if (i32.eqz (local.get $digits)) (then (return (i32.const 0))))\n");
  wasm_emitf(4, "(if (local.get $dst) (then\n");
  wasm_emitf(6, "(if (local.get $neg)\n");
  wasm_emitf(8, "(then (i32.store (local.get $dst) (i32.wrap_i64 (i64.sub (i64.const 0) (local.get $val)))))\n");
  wasm_emitf(8, "(else (i32.store (local.get $dst) (i32.wrap_i64 (local.get $val))))\n");
  wasm_emitf(6, ")\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(local.get $p)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_wscan_word (param $p i32) (param $dst i32) (result i32)\n");
  wasm_emitf(4, "(local $ch i32)\n");
  wasm_emitf(4, "(local $out i32)\n");
  wasm_emitf(4, "(local $count i32)\n");
  wasm_emitf(4, "(local.set $p (call $__ag_wscan_skip_spaces (local.get $p)))\n");
  wasm_emitf(4, "(local.set $out (local.get $dst))\n");
  wasm_emitf(4, "(local.set $ch (i32.load (local.get $p)))\n");
  wasm_emitf(4, "(if (i32.or (i32.eqz (local.get $ch)) (call $__ag_wscan_isspace (local.get $ch))) (then (return (i32.const 0))))\n");
  wasm_emitf(4, "(block $done (loop $loop\n");
  wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
  wasm_emitf(6, "(if (i32.or (i32.eqz (local.get $ch)) (call $__ag_wscan_isspace (local.get $ch))) (then (br $done)))\n");
  wasm_emitf(6, "(if (local.get $out) (then (i32.store (local.get $out) (local.get $ch)) (local.set $out (i32.add (local.get $out) (i32.const 4)))))\n");
  wasm_emitf(6, "(local.set $count (i32.add (local.get $count) (i32.const 1)))\n");
  wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
  wasm_emitf(6, "(br $loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(if (local.get $out) (then (i32.store (local.get $out) (i32.const 0))))\n");
  wasm_emitf(4, "(if (i32.eqz (local.get $count)) (then (return (i32.const 0))))\n");
  wasm_emitf(4, "(local.get $p)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_wscan_char (param $p i32) (param $dst i32) (result i32)\n");
  wasm_emitf(4, "(local $ch i32)\n");
  wasm_emitf(4, "(local.set $ch (i32.load (local.get $p)))\n");
  wasm_emitf(4, "(if (i32.eqz (local.get $ch)) (then (return (i32.const 0))))\n");
  wasm_emitf(4, "(if (local.get $dst) (then (i32.store (local.get $dst) (local.get $ch))))\n");
  wasm_emitf(4, "(i32.add (local.get $p) (i32.const 4))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_swscanf_next_arg (param $which i32) (param $a i32) (param $b i32) (result i32)\n");
  wasm_emitf(4, "(if (i32.eqz (local.get $which)) (then (return (local.get $a))))\n");
  wasm_emitf(4, "(if (i32.eq (local.get $which) (i32.const 1)) (then (return (local.get $b))))\n");
  wasm_emitf(4, "(i32.const 0)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $swscanf (param $s i32) (param $fmt i32) (param $a i32) (param $b i32) (result i32)\n");
  wasm_emitf(4, "(local $p i32)\n");
  wasm_emitf(4, "(local $f i32)\n");
  wasm_emitf(4, "(local $ch i32)\n");
  wasm_emitf(4, "(local $spec i32)\n");
  wasm_emitf(4, "(local $dst i32)\n");
  wasm_emitf(4, "(local $next i32)\n");
  wasm_emitf(4, "(local $assigns i32)\n");
  wasm_emitf(4, "(local.set $p (local.get $s))\n");
  wasm_emitf(4, "(local.set $f (local.get $fmt))\n");
  wasm_emitf(4, "(block $done (loop $loop\n");
  wasm_emitf(6, "(local.set $ch (i32.load (local.get $f)))\n");
  wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
  wasm_emitf(6, "(if (call $__ag_wscan_isspace (local.get $ch)) (then\n");
  wasm_emitf(8, "(block $fmt_space_done (loop $fmt_space\n");
  wasm_emitf(10, "(if (i32.eqz (call $__ag_wscan_isspace (i32.load (local.get $f)))) (then (br $fmt_space_done)))\n");
  wasm_emitf(10, "(local.set $f (i32.add (local.get $f) (i32.const 4)))\n");
  wasm_emitf(10, "(br $fmt_space)\n");
  wasm_emitf(8, "))\n");
  wasm_emitf(8, "(local.set $p (call $__ag_wscan_skip_spaces (local.get $p)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.ne (local.get $ch) (i32.const 37)) (then\n");
  wasm_emitf(8, "(if (i32.ne (i32.load (local.get $p)) (local.get $ch)) (then (br $done)))\n");
  wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
  wasm_emitf(8, "(local.set $f (i32.add (local.get $f) (i32.const 4)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(local.set $f (i32.add (local.get $f) (i32.const 4)))\n");
  wasm_emitf(6, "(local.set $spec (i32.load (local.get $f)))\n");
  wasm_emitf(6, "(if (i32.eqz (local.get $spec)) (then (br $done)))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $spec) (i32.const 37)) (then\n");
  wasm_emitf(8, "(if (i32.ne (i32.load (local.get $p)) (i32.const 37)) (then (br $done)))\n");
  wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
  wasm_emitf(8, "(local.set $f (i32.add (local.get $f) (i32.const 4)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(local.set $dst (call $__ag_swscanf_next_arg (local.get $assigns) (local.get $a) (local.get $b)))\n");
  wasm_emitf(6, "(if (i32.or (i32.eq (local.get $spec) (i32.const 100)) (i32.eq (local.get $spec) (i32.const 117))) (then\n");
  wasm_emitf(8, "(local.set $next (call $__ag_wscan_int (local.get $p) (local.get $dst) (i32.eq (local.get $spec) (i32.const 100))))\n");
  wasm_emitf(8, "(if (i32.eqz (local.get $next)) (then (br $done)))\n");
  wasm_emitf(8, "(local.set $p (local.get $next))\n");
  wasm_emitf(8, "(local.set $assigns (i32.add (local.get $assigns) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $f (i32.add (local.get $f) (i32.const 4)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $spec) (i32.const 115)) (then\n");
  wasm_emitf(8, "(local.set $next (call $__ag_wscan_word (local.get $p) (local.get $dst)))\n");
  wasm_emitf(8, "(if (i32.eqz (local.get $next)) (then (br $done)))\n");
  wasm_emitf(8, "(local.set $p (local.get $next))\n");
  wasm_emitf(8, "(local.set $assigns (i32.add (local.get $assigns) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $f (i32.add (local.get $f) (i32.const 4)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $spec) (i32.const 99)) (then\n");
  wasm_emitf(8, "(local.set $next (call $__ag_wscan_char (local.get $p) (local.get $dst)))\n");
  wasm_emitf(8, "(if (i32.eqz (local.get $next)) (then (br $done)))\n");
  wasm_emitf(8, "(local.set $p (local.get $next))\n");
  wasm_emitf(8, "(local.set $assigns (i32.add (local.get $assigns) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $f (i32.add (local.get $f) (i32.const 4)))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(br $done)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(local.get $assigns)\n");
  wasm_emitf(2, ")\n");
}

static void emit_wasm_strftime_stub(void) {
  int wday_addr = intern_data_symbol("__ag_time_wday_names", 20, 21, 1)->addr;
  int mon_addr = intern_data_symbol("__ag_time_mon_names", 19, 36, 1)->addr;
  int wday_full_addr =
      intern_data_symbol("__ag_time_wday_full_names", (int)sizeof("__ag_time_wday_full_names") - 1, 57, 1)->addr;
  int mon_full_addr =
      intern_data_symbol("__ag_time_mon_full_names", (int)sizeof("__ag_time_mon_full_names") - 1, 86, 1)->addr;
  wasm_emitf(2, "(func $__ag_strftime_putc (param $buf i32) (param $max i64) (param $pos i32) (param $ch i32) (result i32)\n");
  wasm_emitf(4, "(if (i32.ge_u (i32.add (local.get $pos) (i32.const 1)) (i32.wrap_i64 (local.get $max))) (then (return (i32.const -1))))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $pos)) (local.get $ch))\n");
  wasm_emitf(4, "(i32.add (local.get $pos) (i32.const 1))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_strftime_put2 (param $buf i32) (param $max i64) (param $pos i32) (param $v i32) (result i32)\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.div_u (local.get $v) (i32.const 10)) (i32.const 48))))\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (local.get $pos))))\n");
  wasm_emitf(4, "(call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.rem_u (local.get $v) (i32.const 10)) (i32.const 48)))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_strftime_put2p (param $buf i32) (param $max i64) (param $pos i32) (param $v i32) (param $pad i32) (result i32)\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $v) (i32.const 10))\n");
  wasm_emitf(6, "(then (local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (local.get $pad))))\n");
  wasm_emitf(6, "(else (local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.div_s (local.get $v) (i32.const 10)) (i32.const 48)))))\n");
  wasm_emitf(4, ")\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (local.get $pos))))\n");
  wasm_emitf(4, "(call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.rem_s (local.get $v) (i32.const 10)) (i32.const 48)))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_strftime_put4 (param $buf i32) (param $max i64) (param $pos i32) (param $v i32) (result i32)\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.rem_u (i32.div_u (local.get $v) (i32.const 1000)) (i32.const 10)) (i32.const 48))))\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (local.get $pos))))\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.rem_u (i32.div_u (local.get $v) (i32.const 100)) (i32.const 10)) (i32.const 48))))\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (local.get $pos))))\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.rem_u (i32.div_u (local.get $v) (i32.const 10)) (i32.const 10)) (i32.const 48))))\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (local.get $pos))))\n");
  wasm_emitf(4, "(call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.rem_u (local.get $v) (i32.const 10)) (i32.const 48)))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_strftime_put3n (param $buf i32) (param $max i64) (param $pos i32) (param $v i32) (result i32)\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.rem_u (i32.div_u (local.get $v) (i32.const 100)) (i32.const 10)) (i32.const 48))))\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (local.get $pos))))\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.rem_u (i32.div_u (local.get $v) (i32.const 10)) (i32.const 10)) (i32.const 48))))\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (local.get $pos))))\n");
  wasm_emitf(4, "(call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.rem_u (local.get $v) (i32.const 10)) (i32.const 48)))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_strftime_put3s (param $buf i32) (param $max i64) (param $pos i32) (param $src i32) (result i32)\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.load8_u (local.get $src))))\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (local.get $pos))))\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.load8_u (i32.add (local.get $src) (i32.const 1)))))\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (local.get $pos))))\n");
  wasm_emitf(4, "(call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.load8_u (i32.add (local.get $src) (i32.const 2))))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_strftime_putz (param $buf i32) (param $max i64) (param $pos i32) (param $src i32) (result i32)\n");
  wasm_emitf(4, "(local $ch i32)\n");
  wasm_emitf(4, "(block $done (loop $loop\n");
  wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $src)))\n");
  wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
  wasm_emitf(6, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (local.get $ch)))\n");
  wasm_emitf(6, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (local.get $pos))))\n");
  wasm_emitf(6, "(local.set $src (i32.add (local.get $src) (i32.const 1)))\n");
  wasm_emitf(6, "(br $loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(local.get $pos)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_strftime_wday_full (param $idx i32) (result i32)\n");
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 0)) (then (return (i32.const %d))))\n", wday_full_addr);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 1)) (then (return (i32.const %d))))\n", wday_full_addr + 7);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 2)) (then (return (i32.const %d))))\n", wday_full_addr + 14);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 3)) (then (return (i32.const %d))))\n", wday_full_addr + 22);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 4)) (then (return (i32.const %d))))\n", wday_full_addr + 32);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 5)) (then (return (i32.const %d))))\n", wday_full_addr + 41);
  wasm_emitf(4, "(i32.const %d)\n", wday_full_addr + 48);
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_strftime_mon_full (param $idx i32) (result i32)\n");
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 0)) (then (return (i32.const %d))))\n", mon_full_addr);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 1)) (then (return (i32.const %d))))\n", mon_full_addr + 8);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 2)) (then (return (i32.const %d))))\n", mon_full_addr + 17);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 3)) (then (return (i32.const %d))))\n", mon_full_addr + 23);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 4)) (then (return (i32.const %d))))\n", mon_full_addr + 29);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 5)) (then (return (i32.const %d))))\n", mon_full_addr + 33);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 6)) (then (return (i32.const %d))))\n", mon_full_addr + 38);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 7)) (then (return (i32.const %d))))\n", mon_full_addr + 43);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 8)) (then (return (i32.const %d))))\n", mon_full_addr + 50);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 9)) (then (return (i32.const %d))))\n", mon_full_addr + 60);
  wasm_emitf(4, "(if (i32.eq (local.get $idx) (i32.const 10)) (then (return (i32.const %d))))\n", mon_full_addr + 68);
  wasm_emitf(4, "(i32.const %d)\n", mon_full_addr + 77);
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_strftime_wday_for_year_start (param $year i32) (result i32)\n");
  wasm_emitf(4, "(local $w i32)\n");
  wasm_emitf(4, "(local.set $w (i32.wrap_i64 (i64.rem_s (i64.add (i64.const 4) (call $__ag_time_days_before_year (local.get $year))) (i64.const 7))))\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $w) (i32.const 0)) (then (local.set $w (i32.add (local.get $w) (i32.const 7)))))\n");
  wasm_emitf(4, "(local.get $w)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_strftime_iso_weeks_in_year (param $year i32) (result i32)\n");
  wasm_emitf(4, "(local $jan1 i32)\n");
  wasm_emitf(4, "(local.set $jan1 (call $__ag_strftime_wday_for_year_start (local.get $year)))\n");
  wasm_emitf(4, "(if (i32.eq (local.get $jan1) (i32.const 4)) (then (return (i32.const 53))))\n");
  wasm_emitf(4, "(if (i32.and (i32.eq (local.get $jan1) (i32.const 3)) (call $__ag_time_is_leap (local.get $year))) (then (return (i32.const 53))))\n");
  wasm_emitf(4, "(i32.const 52)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_strftime_iso_year_week (param $tm i32) (result i64)\n");
  wasm_emitf(4, "(local $year i32)\n");
  wasm_emitf(4, "(local $monday i32)\n");
  wasm_emitf(4, "(local $week i32)\n");
  wasm_emitf(4, "(local.set $year (i32.add (i32.load (i32.add (local.get $tm) (i32.const 20))) (i32.const 1900)))\n");
  wasm_emitf(4, "(local.set $monday (i32.rem_s (i32.add (i32.load (i32.add (local.get $tm) (i32.const 24))) (i32.const 6)) (i32.const 7)))\n");
  wasm_emitf(4, "(local.set $week (i32.div_s (i32.add (i32.sub (i32.load (i32.add (local.get $tm) (i32.const 28))) (local.get $monday)) (i32.const 10)) (i32.const 7)))\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $week) (i32.const 1))\n");
  wasm_emitf(6, "(then\n");
  wasm_emitf(8, "(local.set $year (i32.sub (local.get $year) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $week (call $__ag_strftime_iso_weeks_in_year (local.get $year)))\n");
  wasm_emitf(6, ")\n");
  wasm_emitf(6, "(else (if (i32.gt_s (local.get $week) (call $__ag_strftime_iso_weeks_in_year (local.get $year))) (then\n");
  wasm_emitf(8, "(local.set $year (i32.add (local.get $year) (i32.const 1)))\n");
  wasm_emitf(8, "(local.set $week (i32.const 1))\n");
  wasm_emitf(6, ")))\n");
  wasm_emitf(4, ")\n");
  wasm_emitf(4, "(i64.or (i64.shl (i64.extend_i32_s (local.get $year)) (i64.const 32)) (i64.extend_i32_u (local.get $week)))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $strftime (param $buf i32) (param $max i64) (param $fmt i32) (param $tm i32) (result i64)\n");
  wasm_emitf(4, "(local $pos i32)\n");
  wasm_emitf(4, "(local $p i32)\n");
  wasm_emitf(4, "(local $ch i32)\n");
  wasm_emitf(4, "(local $year i32)\n");
  wasm_emitf(4, "(local $idx i32)\n");
  wasm_emitf(4, "(local $src i32)\n");
  wasm_emitf(4, "(local $hour i32)\n");
  wasm_emitf(4, "(local $iso i64)\n");
  wasm_emitf(4, "(if (i32.or (i32.or (i32.eqz (local.get $buf)) (i64.eqz (local.get $max))) (i32.or (i32.eqz (local.get $fmt)) (i32.eqz (local.get $tm)))) (then (return (i64.const 0))))\n");
  wasm_emitf(4, "(local.set $p (local.get $fmt))\n");
  wasm_emitf(4, "(block $done (loop $loop\n");
  wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
  wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
  wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
  wasm_emitf(6, "(if (i32.ne (local.get $ch) (i32.const 37)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (local.get $ch)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
  wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (return (i64.const 0))))\n");
  wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 37)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 37)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 65)) (then\n");
  wasm_emitf(8, "(local.set $idx (i32.load (i32.add (local.get $tm) (i32.const 24))))\n");
  wasm_emitf(8, "(if (i32.or (i32.lt_s (local.get $idx) (i32.const 0)) (i32.gt_s (local.get $idx) (i32.const 6))) (then (local.set $idx (i32.const 6))))\n");
  wasm_emitf(8, "(local.set $src (call $__ag_strftime_wday_full (local.get $idx)))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putz (local.get $buf) (local.get $max) (local.get $pos) (local.get $src)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 97)) (then\n");
  wasm_emitf(8, "(local.set $idx (i32.load (i32.add (local.get $tm) (i32.const 24))))\n");
  wasm_emitf(8, "(if (i32.or (i32.lt_s (local.get $idx) (i32.const 0)) (i32.gt_s (local.get $idx) (i32.const 6))) (then (local.set $idx (i32.const 6))))\n");
  wasm_emitf(8, "(local.set $src (i32.add (i32.const %d) (i32.mul (local.get $idx) (i32.const 3))))\n", wday_addr);
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put3s (local.get $buf) (local.get $max) (local.get $pos) (local.get $src)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.or (i32.eq (local.get $ch) (i32.const 98)) (i32.eq (local.get $ch) (i32.const 104))) (then\n");
  wasm_emitf(8, "(local.set $idx (i32.load (i32.add (local.get $tm) (i32.const 16))))\n");
  wasm_emitf(8, "(if (i32.or (i32.lt_s (local.get $idx) (i32.const 0)) (i32.gt_s (local.get $idx) (i32.const 11))) (then (local.set $idx (i32.const 11))))\n");
  wasm_emitf(8, "(local.set $src (i32.add (i32.const %d) (i32.mul (local.get $idx) (i32.const 3))))\n", mon_addr);
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put3s (local.get $buf) (local.get $max) (local.get $pos) (local.get $src)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 66)) (then\n");
  wasm_emitf(8, "(local.set $idx (i32.load (i32.add (local.get $tm) (i32.const 16))))\n");
  wasm_emitf(8, "(if (i32.or (i32.lt_s (local.get $idx) (i32.const 0)) (i32.gt_s (local.get $idx) (i32.const 11))) (then (local.set $idx (i32.const 11))))\n");
  wasm_emitf(8, "(local.set $src (call $__ag_strftime_mon_full (local.get $idx)))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putz (local.get $buf) (local.get $max) (local.get $pos) (local.get $src)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 99)) (then\n");
  wasm_emitf(8, "(local.set $idx (i32.load (i32.add (local.get $tm) (i32.const 24))))\n");
  wasm_emitf(8, "(if (i32.or (i32.lt_s (local.get $idx) (i32.const 0)) (i32.gt_s (local.get $idx) (i32.const 6))) (then (local.set $idx (i32.const 6))))\n");
  wasm_emitf(8, "(local.set $src (i32.add (i32.const %d) (i32.mul (local.get $idx) (i32.const 3))))\n", wday_addr);
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put3s (local.get $buf) (local.get $max) (local.get $pos) (local.get $src)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 32)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $idx (i32.load (i32.add (local.get $tm) (i32.const 16))))\n");
  wasm_emitf(8, "(if (i32.or (i32.lt_s (local.get $idx) (i32.const 0)) (i32.gt_s (local.get $idx) (i32.const 11))) (then (local.set $idx (i32.const 11))))\n");
  wasm_emitf(8, "(local.set $src (i32.add (i32.const %d) (i32.mul (local.get $idx) (i32.const 3))))\n", mon_addr);
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put3s (local.get $buf) (local.get $max) (local.get $pos) (local.get $src)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 32)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 12)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 32)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 8)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 58)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 4)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 58)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (local.get $tm))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 32)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $year (i32.add (i32.load (i32.add (local.get $tm) (i32.const 20))) (i32.const 1900)))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put4 (local.get $buf) (local.get $max) (local.get $pos) (local.get $year)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 67)) (then\n");
  wasm_emitf(8, "(local.set $year (i32.add (i32.load (i32.add (local.get $tm) (i32.const 20))) (i32.const 1900)))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.div_s (local.get $year) (i32.const 100))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 101)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2p (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 12))) (i32.const 32)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 68)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.load (i32.add (local.get $tm) (i32.const 16))) (i32.const 1))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 47)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 12)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 47)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $year (i32.add (i32.load (i32.add (local.get $tm) (i32.const 20))) (i32.const 1900)))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.rem_s (local.get $year) (i32.const 100))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 73)) (then\n");
  wasm_emitf(8, "(local.set $hour (i32.rem_s (i32.load (i32.add (local.get $tm) (i32.const 8))) (i32.const 12)))\n");
  wasm_emitf(8, "(if (i32.eqz (local.get $hour)) (then (local.set $hour (i32.const 12))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (local.get $hour)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 106)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put3n (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.load (i32.add (local.get $tm) (i32.const 28))) (i32.const 1))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.or (i32.eq (local.get $ch) (i32.const 103)) (i32.or (i32.eq (local.get $ch) (i32.const 71)) (i32.eq (local.get $ch) (i32.const 86)))) (then\n");
  wasm_emitf(8, "(local.set $iso (call $__ag_strftime_iso_year_week (local.get $tm)))\n");
  wasm_emitf(8, "(if (i32.eq (local.get $ch) (i32.const 86))\n");
  wasm_emitf(10, "(then (local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.wrap_i64 (local.get $iso)))))\n");
  wasm_emitf(10, "(else\n");
  wasm_emitf(12, "(local.set $year (i32.wrap_i64 (i64.shr_s (local.get $iso) (i64.const 32))))\n");
  wasm_emitf(12, "(if (i32.eq (local.get $ch) (i32.const 103))\n");
  wasm_emitf(14, "(then (local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.rem_s (local.get $year) (i32.const 100)))))\n");
  wasm_emitf(14, "(else (local.set $pos (call $__ag_strftime_put4 (local.get $buf) (local.get $max) (local.get $pos) (local.get $year))))\n");
  wasm_emitf(12, ")\n");
  wasm_emitf(10, ")\n");
  wasm_emitf(8, ")\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 110)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 10)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 112)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (if (result i32) (i32.lt_s (i32.load (i32.add (local.get $tm) (i32.const 8))) (i32.const 12)) (then (i32.const 65)) (else (i32.const 80)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 77)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 82)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 8)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 58)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 4)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 114)) (then\n");
  wasm_emitf(8, "(local.set $hour (i32.rem_s (i32.load (i32.add (local.get $tm) (i32.const 8))) (i32.const 12)))\n");
  wasm_emitf(8, "(if (i32.eqz (local.get $hour)) (then (local.set $hour (i32.const 12))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (local.get $hour)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 58)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 4)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 58)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (local.get $tm))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 32)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (if (result i32) (i32.lt_s (i32.load (i32.add (local.get $tm) (i32.const 8))) (i32.const 12)) (then (i32.const 65)) (else (i32.const 80)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 77)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 116)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 9)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 85)) (then\n");
  wasm_emitf(8, "(local.set $idx (i32.load (i32.add (local.get $tm) (i32.const 24))))\n");
  wasm_emitf(8, "(if (i32.or (i32.lt_s (local.get $idx) (i32.const 0)) (i32.gt_s (local.get $idx) (i32.const 6))) (then (local.set $idx (i32.const 0))))\n");
  wasm_emitf(8, "(local.set $idx (i32.div_s (i32.sub (i32.add (i32.load (i32.add (local.get $tm) (i32.const 28))) (i32.const 7)) (local.get $idx)) (i32.const 7)))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (local.get $idx)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 117)) (then\n");
  wasm_emitf(8, "(local.set $idx (i32.load (i32.add (local.get $tm) (i32.const 24))))\n");
  wasm_emitf(8, "(if (i32.eqz (local.get $idx)) (then (local.set $idx (i32.const 7))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (local.get $idx) (i32.const 48))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 119)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.load (i32.add (local.get $tm) (i32.const 24))) (i32.const 48))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 87)) (then\n");
  wasm_emitf(8, "(local.set $idx (i32.load (i32.add (local.get $tm) (i32.const 24))))\n");
  wasm_emitf(8, "(if (i32.or (i32.lt_s (local.get $idx) (i32.const 0)) (i32.gt_s (local.get $idx) (i32.const 6))) (then (local.set $idx (i32.const 0))))\n");
  wasm_emitf(8, "(local.set $idx (i32.rem_s (i32.add (local.get $idx) (i32.const 6)) (i32.const 7)))\n");
  wasm_emitf(8, "(local.set $idx (i32.div_s (i32.sub (i32.add (i32.load (i32.add (local.get $tm) (i32.const 28))) (i32.const 7)) (local.get $idx)) (i32.const 7)))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (local.get $idx)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.or (i32.eq (local.get $ch) (i32.const 88)) (i32.eq (local.get $ch) (i32.const 120))) (then\n");
  wasm_emitf(8, "(if (i32.eq (local.get $ch) (i32.const 120)) (then\n");
  wasm_emitf(10, "(local.set $year (i32.add (i32.load (i32.add (local.get $tm) (i32.const 20))) (i32.const 1900)))\n");
  wasm_emitf(10, "(local.set $pos (call $__ag_strftime_put4 (local.get $buf) (local.get $max) (local.get $pos) (local.get $year)))\n");
  wasm_emitf(10, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(10, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 45)))\n");
  wasm_emitf(10, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(10, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.load (i32.add (local.get $tm) (i32.const 16))) (i32.const 1))))\n");
  wasm_emitf(10, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(10, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 45)))\n");
  wasm_emitf(10, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(10, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 12)))))\n");
  wasm_emitf(8, ")\n");
  wasm_emitf(8, "(else\n");
  wasm_emitf(10, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 8)))))\n");
  wasm_emitf(10, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(10, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 58)))\n");
  wasm_emitf(10, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(10, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 4)))))\n");
  wasm_emitf(10, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(10, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 58)))\n");
  wasm_emitf(10, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(10, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (local.get $tm))))\n");
  wasm_emitf(8, "))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 121)) (then\n");
  wasm_emitf(8, "(local.set $year (i32.add (i32.load (i32.add (local.get $tm) (i32.const 20))) (i32.const 1900)))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.rem_s (local.get $year) (i32.const 100))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 122)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 43)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put4 (local.get $buf) (local.get $max) (local.get $pos) (i32.const 0)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 90)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 85)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 84)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 67)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 89)) (then\n");
  wasm_emitf(8, "(local.set $year (i32.add (i32.load (i32.add (local.get $tm) (i32.const 20))) (i32.const 1900)))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put4 (local.get $buf) (local.get $max) (local.get $pos) (local.get $year)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 109)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.load (i32.add (local.get $tm) (i32.const 16))) (i32.const 1))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 100)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 12)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 72)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 8)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 77)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 4)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 83)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (local.get $tm))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 70)) (then\n");
  wasm_emitf(8, "(local.set $year (i32.add (i32.load (i32.add (local.get $tm) (i32.const 20))) (i32.const 1900)))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put4 (local.get $buf) (local.get $max) (local.get $pos) (local.get $year)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 45)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.add (i32.load (i32.add (local.get $tm) (i32.const 16))) (i32.const 1))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 45)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 12)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 84)) (then\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 8)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 58)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 4)))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 58)))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(local.set $pos (call $__ag_strftime_put2 (local.get $buf) (local.get $max) (local.get $pos) (i32.load (local.get $tm))))\n");
  wasm_emitf(8, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(8, "(br $loop)\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(6, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (i32.const 37)))\n");
  wasm_emitf(6, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(6, "(local.set $pos (call $__ag_strftime_putc (local.get $buf) (local.get $max) (local.get $pos) (local.get $ch)))\n");
  wasm_emitf(6, "(if (i32.lt_s (local.get $pos) (i32.const 0)) (then (return (i64.const 0))))\n");
  wasm_emitf(6, "(br $loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(if (i32.ge_u (local.get $pos) (i32.wrap_i64 (local.get $max))) (then (return (i64.const 0))))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $pos)) (i32.const 0))\n");
  wasm_emitf(4, "(i64.extend_i32_u (local.get $pos))\n");
  wasm_emitf(2, ")\n");
}

static void emit_wasm_wcsftime_stub(void) {
  int fmt_addr = intern_data_symbol("__ag_wcsftime_fmt_buf", (int)sizeof("__ag_wcsftime_fmt_buf") - 1, 256, 1)->addr;
  int out_addr = intern_data_symbol("__ag_wcsftime_out_buf", (int)sizeof("__ag_wcsftime_out_buf") - 1, 256, 1)->addr;
  wasm_emitf(2, "(func $wcsftime (param $dst i32) (param $max i64) (param $fmt i32) (param $tm i32) (result i64)\n");
  wasm_emitf(4, "(local $p i32)\n");
  wasm_emitf(4, "(local $o i32)\n");
  wasm_emitf(4, "(local $ch i32)\n");
  wasm_emitf(4, "(local $n i64)\n");
  wasm_emitf(4, "(local $i i32)\n");
  wasm_emitf(4, "(if (i32.or (i32.or (i32.eqz (local.get $dst)) (i64.eqz (local.get $max))) (i32.or (i32.eqz (local.get $fmt)) (i32.eqz (local.get $tm)))) (then (return (i64.const 0))))\n");
  wasm_emitf(4, "(local.set $p (local.get $fmt))\n");
  wasm_emitf(4, "(local.set $o (i32.const %d))\n", fmt_addr);
  wasm_emitf(4, "(block $fmt_done (loop $fmt_loop\n");
  wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
  wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $fmt_done)))\n");
  wasm_emitf(6, "(if (i32.gt_u (local.get $ch) (i32.const 255)) (then (return (i64.const 0))))\n");
  wasm_emitf(6, "(if (i32.ge_u (i32.sub (local.get $o) (i32.const %d)) (i32.const 255)) (then (return (i64.const 0))))\n", fmt_addr);
  wasm_emitf(6, "(i32.store8 (local.get $o) (local.get $ch))\n");
  wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
  wasm_emitf(6, "(local.set $o (i32.add (local.get $o) (i32.const 1)))\n");
  wasm_emitf(6, "(br $fmt_loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(i32.store8 (local.get $o) (i32.const 0))\n");
  wasm_emitf(4, "(local.set $n (call $strftime (i32.const %d) (i64.const 256) (i32.const %d) (local.get $tm)))\n", out_addr, fmt_addr);
  wasm_emitf(4, "(if (i64.eqz (local.get $n)) (then (return (i64.const 0))))\n");
  wasm_emitf(4, "(if (i64.ge_u (local.get $n) (local.get $max)) (then (return (i64.const 0))))\n");
  wasm_emitf(4, "(local.set $i (i32.const 0))\n");
  wasm_emitf(4, "(block $copy_done (loop $copy_loop\n");
  wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.wrap_i64 (local.get $n))) (then (br $copy_done)))\n");
  wasm_emitf(6, "(i32.store (i32.add (local.get $dst) (i32.mul (local.get $i) (i32.const 4))) (i32.load8_u (i32.add (i32.const %d) (local.get $i))))\n", out_addr);
  wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
  wasm_emitf(6, "(br $copy_loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(i32.store (i32.add (local.get $dst) (i32.mul (i32.wrap_i64 (local.get $n)) (i32.const 4))) (i32.const 0))\n");
  wasm_emitf(4, "(local.get $n)\n");
  wasm_emitf(2, ")\n");
}

static void emit_wasm_time_conversion_helpers(void) {
  wasm_emitf(2, "(func $__ag_time_is_leap (param $year i32) (result i32)\n");
  wasm_emitf(4, "(if (result i32) (i32.eqz (i32.rem_s (local.get $year) (i32.const 400)))\n");
  wasm_emitf(6, "(then (i32.const 1))\n");
  wasm_emitf(6, "(else (if (result i32) (i32.eqz (i32.rem_s (local.get $year) (i32.const 100)))\n");
  wasm_emitf(8, "(then (i32.const 0))\n");
  wasm_emitf(8, "(else (i32.eqz (i32.rem_s (local.get $year) (i32.const 4))))))\n");
  wasm_emitf(4, ")\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_time_days_in_month (param $year i32) (param $mon i32) (result i32)\n");
  wasm_emitf(4, "(if (i32.eq (local.get $mon) (i32.const 1)) (then (return (i32.add (i32.const 28) (call $__ag_time_is_leap (local.get $year))))))\n");
  wasm_emitf(4, "(if (i32.or (i32.or (i32.eq (local.get $mon) (i32.const 3)) (i32.eq (local.get $mon) (i32.const 5))) (i32.or (i32.eq (local.get $mon) (i32.const 8)) (i32.eq (local.get $mon) (i32.const 10)))) (then (return (i32.const 30))))\n");
  wasm_emitf(4, "(i32.const 31)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_time_days_before_year (param $year i32) (result i64)\n");
  wasm_emitf(4, "(local $y i32)\n");
  wasm_emitf(4, "(local $days i64)\n");
  wasm_emitf(4, "(if (i32.ge_s (local.get $year) (i32.const 1970))\n");
  wasm_emitf(6, "(then\n");
  wasm_emitf(8, "(local.set $y (i32.const 1970))\n");
  wasm_emitf(8, "(block $done (loop $loop\n");
  wasm_emitf(10, "(if (i32.ge_s (local.get $y) (local.get $year)) (then (br $done)))\n");
  wasm_emitf(10, "(local.set $days (i64.add (local.get $days) (i64.extend_i32_s (i32.add (i32.const 365) (call $__ag_time_is_leap (local.get $y))))))\n");
  wasm_emitf(10, "(local.set $y (i32.add (local.get $y) (i32.const 1)))\n");
  wasm_emitf(10, "(br $loop)\n");
  wasm_emitf(8, "))\n");
  wasm_emitf(6, ")\n");
  wasm_emitf(6, "(else\n");
  wasm_emitf(8, "(local.set $y (local.get $year))\n");
  wasm_emitf(8, "(block $done_neg (loop $loop_neg\n");
  wasm_emitf(10, "(if (i32.ge_s (local.get $y) (i32.const 1970)) (then (br $done_neg)))\n");
  wasm_emitf(10, "(local.set $days (i64.sub (local.get $days) (i64.extend_i32_s (i32.add (i32.const 365) (call $__ag_time_is_leap (local.get $y))))))\n");
  wasm_emitf(10, "(local.set $y (i32.add (local.get $y) (i32.const 1)))\n");
  wasm_emitf(10, "(br $loop_neg)\n");
  wasm_emitf(8, "))\n");
  wasm_emitf(6, "))\n");
  wasm_emitf(4, "(local.get $days)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_time_days_before_month (param $year i32) (param $mon i32) (result i64)\n");
  wasm_emitf(4, "(local $m i32)\n");
  wasm_emitf(4, "(local $days i64)\n");
  wasm_emitf(4, "(block $done (loop $loop\n");
  wasm_emitf(6, "(if (i32.ge_s (local.get $m) (local.get $mon)) (then (br $done)))\n");
  wasm_emitf(6, "(local.set $days (i64.add (local.get $days) (i64.extend_i32_s (call $__ag_time_days_in_month (local.get $year) (local.get $m)))))\n");
  wasm_emitf(6, "(local.set $m (i32.add (local.get $m) (i32.const 1)))\n");
  wasm_emitf(6, "(br $loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(local.get $days)\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_time_to_seconds (param $tm i32) (result i64)\n");
  wasm_emitf(4, "(local $year i32)\n");
  wasm_emitf(4, "(local $mon i32)\n");
  wasm_emitf(4, "(local $days i64)\n");
  wasm_emitf(4, "(if (i32.eqz (local.get $tm)) (then (return (i64.const -1))))\n");
  wasm_emitf(4, "(local.set $year (i32.add (i32.load (i32.add (local.get $tm) (i32.const 20))) (i32.const 1900)))\n");
  wasm_emitf(4, "(local.set $mon (i32.load (i32.add (local.get $tm) (i32.const 16))))\n");
  wasm_emitf(4, "(block $mon_neg_done (loop $mon_neg\n");
  wasm_emitf(6, "(if (i32.ge_s (local.get $mon) (i32.const 0)) (then (br $mon_neg_done)))\n");
  wasm_emitf(6, "(local.set $mon (i32.add (local.get $mon) (i32.const 12)))\n");
  wasm_emitf(6, "(local.set $year (i32.sub (local.get $year) (i32.const 1)))\n");
  wasm_emitf(6, "(br $mon_neg)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(block $mon_hi_done (loop $mon_hi\n");
  wasm_emitf(6, "(if (i32.lt_s (local.get $mon) (i32.const 12)) (then (br $mon_hi_done)))\n");
  wasm_emitf(6, "(local.set $mon (i32.sub (local.get $mon) (i32.const 12)))\n");
  wasm_emitf(6, "(local.set $year (i32.add (local.get $year) (i32.const 1)))\n");
  wasm_emitf(6, "(br $mon_hi)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(local.set $days (i64.add (call $__ag_time_days_before_year (local.get $year)) (call $__ag_time_days_before_month (local.get $year) (local.get $mon))))\n");
  wasm_emitf(4, "(local.set $days (i64.add (local.get $days) (i64.sub (i64.extend_i32_s (i32.load (i32.add (local.get $tm) (i32.const 12)))) (i64.const 1))))\n");
  wasm_emitf(4, "(i64.add (i64.add (i64.mul (local.get $days) (i64.const 86400)) (i64.mul (i64.extend_i32_s (i32.load (i32.add (local.get $tm) (i32.const 8)))) (i64.const 3600))) (i64.add (i64.mul (i64.extend_i32_s (i32.load (i32.add (local.get $tm) (i32.const 4)))) (i64.const 60)) (i64.extend_i32_s (i32.load (local.get $tm)))))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_time_from_seconds (param $t i64) (param $tm i32)\n");
  wasm_emitf(4, "(local $days i64)\n");
  wasm_emitf(4, "(local $rem i64)\n");
  wasm_emitf(4, "(local $year i32)\n");
  wasm_emitf(4, "(local $mon i32)\n");
  wasm_emitf(4, "(local $yday i32)\n");
  wasm_emitf(4, "(local $dim i32)\n");
  wasm_emitf(4, "(if (i32.eqz (local.get $tm)) (then (return)))\n");
  wasm_emitf(4, "(local.set $days (i64.div_s (local.get $t) (i64.const 86400)))\n");
  wasm_emitf(4, "(local.set $rem (i64.rem_s (local.get $t) (i64.const 86400)))\n");
  wasm_emitf(4, "(if (i64.lt_s (local.get $rem) (i64.const 0)) (then\n");
  wasm_emitf(6, "(local.set $rem (i64.add (local.get $rem) (i64.const 86400)))\n");
  wasm_emitf(6, "(local.set $days (i64.sub (local.get $days) (i64.const 1)))\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(i32.store (i32.add (local.get $tm) (i32.const 8)) (i32.wrap_i64 (i64.div_s (local.get $rem) (i64.const 3600))))\n");
  wasm_emitf(4, "(local.set $rem (i64.rem_s (local.get $rem) (i64.const 3600)))\n");
  wasm_emitf(4, "(i32.store (i32.add (local.get $tm) (i32.const 4)) (i32.wrap_i64 (i64.div_s (local.get $rem) (i64.const 60))))\n");
  wasm_emitf(4, "(i32.store (local.get $tm) (i32.wrap_i64 (i64.rem_s (local.get $rem) (i64.const 60))))\n");
  wasm_emitf(4, "(i32.store (i32.add (local.get $tm) (i32.const 24)) (i32.wrap_i64 (i64.rem_s (i64.add (local.get $days) (i64.const 4)) (i64.const 7))))\n");
  wasm_emitf(4, "(if (i32.lt_s (i32.load (i32.add (local.get $tm) (i32.const 24))) (i32.const 0)) (then (i32.store (i32.add (local.get $tm) (i32.const 24)) (i32.add (i32.load (i32.add (local.get $tm) (i32.const 24))) (i32.const 7)))))\n");
  wasm_emitf(4, "(local.set $year (i32.const 1970))\n");
  wasm_emitf(4, "(block $neg_done (loop $neg\n");
  wasm_emitf(6, "(if (i64.ge_s (local.get $days) (i64.const 0)) (then (br $neg_done)))\n");
  wasm_emitf(6, "(local.set $year (i32.sub (local.get $year) (i32.const 1)))\n");
  wasm_emitf(6, "(local.set $days (i64.add (local.get $days) (i64.extend_i32_s (i32.add (i32.const 365) (call $__ag_time_is_leap (local.get $year))))))\n");
  wasm_emitf(6, "(br $neg)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(block $year_done (loop $year_loop\n");
  wasm_emitf(6, "(local.set $dim (i32.add (i32.const 365) (call $__ag_time_is_leap (local.get $year))))\n");
  wasm_emitf(6, "(if (i64.lt_s (local.get $days) (i64.extend_i32_s (local.get $dim))) (then (br $year_done)))\n");
  wasm_emitf(6, "(local.set $days (i64.sub (local.get $days) (i64.extend_i32_s (local.get $dim))))\n");
  wasm_emitf(6, "(local.set $year (i32.add (local.get $year) (i32.const 1)))\n");
  wasm_emitf(6, "(br $year_loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(local.set $yday (i32.wrap_i64 (local.get $days)))\n");
  wasm_emitf(4, "(local.set $mon (i32.const 0))\n");
  wasm_emitf(4, "(block $mon_done (loop $mon_loop\n");
  wasm_emitf(6, "(local.set $dim (call $__ag_time_days_in_month (local.get $year) (local.get $mon)))\n");
  wasm_emitf(6, "(if (i64.lt_s (local.get $days) (i64.extend_i32_s (local.get $dim))) (then (br $mon_done)))\n");
  wasm_emitf(6, "(local.set $days (i64.sub (local.get $days) (i64.extend_i32_s (local.get $dim))))\n");
  wasm_emitf(6, "(local.set $mon (i32.add (local.get $mon) (i32.const 1)))\n");
  wasm_emitf(6, "(br $mon_loop)\n");
  wasm_emitf(4, "))\n");
  wasm_emitf(4, "(i32.store (i32.add (local.get $tm) (i32.const 12)) (i32.add (i32.wrap_i64 (local.get $days)) (i32.const 1)))\n");
  wasm_emitf(4, "(i32.store (i32.add (local.get $tm) (i32.const 16)) (local.get $mon))\n");
  wasm_emitf(4, "(i32.store (i32.add (local.get $tm) (i32.const 20)) (i32.sub (local.get $year) (i32.const 1900)))\n");
  wasm_emitf(4, "(i32.store (i32.add (local.get $tm) (i32.const 28)) (local.get $yday))\n");
  wasm_emitf(4, "(i32.store (i32.add (local.get $tm) (i32.const 32)) (i32.const 0))\n");
  wasm_emitf(2, ")\n");
}

static void emit_wasm_mktime_stub(void) {
  wasm_emitf(2, "(func $mktime (param $tm i32) (result i64)\n");
  wasm_emitf(4, "(local $t i64)\n");
  wasm_emitf(4, "(local.set $t (call $__ag_time_to_seconds (local.get $tm)))\n");
  wasm_emitf(4, "(call $__ag_time_from_seconds (local.get $t) (local.get $tm))\n");
  wasm_emitf(4, "(local.get $t)\n");
  wasm_emitf(2, ")\n");
}

static void emit_wasm_asctime_helpers(void) {
  int buf_addr = intern_data_symbol("__ag_stub_asctime_buf", 21, 26, 1)->addr;
  int wday_addr = intern_data_symbol("__ag_time_wday_names", 20, 21, 1)->addr;
  int mon_addr = intern_data_symbol("__ag_time_mon_names", 19, 36, 1)->addr;
  wasm_emitf(2, "(func $__ag_asctime_put3 (param $buf i32) (param $pos i32) (param $src i32) (result i32)\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $pos)) (i32.load8_u (local.get $src)))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (i32.add (local.get $pos) (i32.const 1))) (i32.load8_u (i32.add (local.get $src) (i32.const 1))))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (i32.add (local.get $pos) (i32.const 2))) (i32.load8_u (i32.add (local.get $src) (i32.const 2))))\n");
  wasm_emitf(4, "(i32.add (local.get $pos) (i32.const 3))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_asctime_put2 (param $buf i32) (param $pos i32) (param $v i32) (param $pad i32) (result i32)\n");
  wasm_emitf(4, "(if (i32.lt_s (local.get $v) (i32.const 10))\n");
  wasm_emitf(6, "(then (i32.store8 (i32.add (local.get $buf) (local.get $pos)) (local.get $pad)))\n");
  wasm_emitf(6, "(else (i32.store8 (i32.add (local.get $buf) (local.get $pos)) (i32.add (i32.div_s (local.get $v) (i32.const 10)) (i32.const 48))))\n");
  wasm_emitf(4, ")\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (i32.add (local.get $pos) (i32.const 1))) (i32.add (i32.rem_s (local.get $v) (i32.const 10)) (i32.const 48)))\n");
  wasm_emitf(4, "(i32.add (local.get $pos) (i32.const 2))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_asctime_put4 (param $buf i32) (param $pos i32) (param $v i32) (result i32)\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $pos)) (i32.add (i32.rem_s (i32.div_s (local.get $v) (i32.const 1000)) (i32.const 10)) (i32.const 48)))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (i32.add (local.get $pos) (i32.const 1))) (i32.add (i32.rem_s (i32.div_s (local.get $v) (i32.const 100)) (i32.const 10)) (i32.const 48)))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (i32.add (local.get $pos) (i32.const 2))) (i32.add (i32.rem_s (i32.div_s (local.get $v) (i32.const 10)) (i32.const 10)) (i32.const 48)))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (i32.add (local.get $pos) (i32.const 3))) (i32.add (i32.rem_s (local.get $v) (i32.const 10)) (i32.const 48)))\n");
  wasm_emitf(4, "(i32.add (local.get $pos) (i32.const 4))\n");
  wasm_emitf(2, ")\n");
  wasm_emitf(2, "(func $__ag_asctime_impl (param $tm i32) (result i32)\n");
  wasm_emitf(4, "(local $buf i32)\n");
  wasm_emitf(4, "(local $pos i32)\n");
  wasm_emitf(4, "(local $idx i32)\n");
  wasm_emitf(4, "(local $src i32)\n");
  wasm_emitf(4, "(if (i32.eqz (local.get $tm)) (then (return (i32.const 0))))\n");
  wasm_emitf(4, "(local.set $buf (i32.const %d))\n", buf_addr);
  wasm_emitf(4, "(local.set $idx (i32.load (i32.add (local.get $tm) (i32.const 24))))\n");
  wasm_emitf(4, "(if (i32.or (i32.lt_s (local.get $idx) (i32.const 0)) (i32.gt_s (local.get $idx) (i32.const 6))) (then (local.set $idx (i32.const 6))))\n");
  wasm_emitf(4, "(local.set $src (i32.add (i32.const %d) (i32.mul (local.get $idx) (i32.const 3))))\n", wday_addr);
  wasm_emitf(4, "(local.set $pos (call $__ag_asctime_put3 (local.get $buf) (local.get $pos) (local.get $src)))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $pos)) (i32.const 32))\n");
  wasm_emitf(4, "(local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
  wasm_emitf(4, "(local.set $idx (i32.load (i32.add (local.get $tm) (i32.const 16))))\n");
  wasm_emitf(4, "(if (i32.or (i32.lt_s (local.get $idx) (i32.const 0)) (i32.gt_s (local.get $idx) (i32.const 11))) (then (local.set $idx (i32.const 11))))\n");
  wasm_emitf(4, "(local.set $src (i32.add (i32.const %d) (i32.mul (local.get $idx) (i32.const 3))))\n", mon_addr);
  wasm_emitf(4, "(local.set $pos (call $__ag_asctime_put3 (local.get $buf) (local.get $pos) (local.get $src)))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $pos)) (i32.const 32))\n");
  wasm_emitf(4, "(local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_asctime_put2 (local.get $buf) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 12))) (i32.const 32)))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $pos)) (i32.const 32))\n");
  wasm_emitf(4, "(local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_asctime_put2 (local.get $buf) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 8))) (i32.const 48)))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $pos)) (i32.const 58))\n");
  wasm_emitf(4, "(local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_asctime_put2 (local.get $buf) (local.get $pos) (i32.load (i32.add (local.get $tm) (i32.const 4))) (i32.const 48)))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $pos)) (i32.const 58))\n");
  wasm_emitf(4, "(local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_asctime_put2 (local.get $buf) (local.get $pos) (i32.load (local.get $tm)) (i32.const 48)))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $pos)) (i32.const 32))\n");
  wasm_emitf(4, "(local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
  wasm_emitf(4, "(local.set $pos (call $__ag_asctime_put4 (local.get $buf) (local.get $pos) (i32.add (i32.load (i32.add (local.get $tm) (i32.const 20))) (i32.const 1900))))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $pos)) (i32.const 10))\n");
  wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (i32.add (local.get $pos) (i32.const 1))) (i32.const 0))\n");
  wasm_emitf(4, "(local.get $buf)\n");
  wasm_emitf(2, ")\n");
}

static void emit_minimal_libc_stubs(void) {
  emit_minimal_static_data_if_needed();
  if (has_undefined_function("puts", 4)) {
    wasm_emitf(2, "(func $puts (param $s i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $n i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $s)) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.eqz (i32.load8_u (local.get $p))) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $n (i32.add (local.get $n) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.add (local.get $n) (i32.const 1))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fputs", 5)) {
    wasm_emitf(2, "(func $fputs (param $s i32) (param $stream i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $n i32)\n");
    wasm_emitf(4, "(if (i32.or (i32.eqz (local.get $s)) (i32.eqz (local.get $stream))) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.eqz (i32.load8_u (local.get $p))) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $n (i32.add (local.get $n) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $n)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fputc", 5)) {
    wasm_emitf(2, "(func $fputc (param $c i64) (param $stream i32) (result i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $stream)) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(i32.wrap_i64 (local.get $c))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("putc", 4)) {
    wasm_emitf(2, "(func $putc (param $c i64) (param $stream i32) (result i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $stream)) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(i32.wrap_i64 (local.get $c))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("setvbuf", 7)) {
    wasm_emitf(2, "(func $setvbuf (param $stream i32) (param $buf i32) (param $mode i64) (param $size i64) (result i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $stream)) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(if (i32.or (i64.lt_s (local.get $mode) (i64.const 0)) (i64.gt_s (local.get $mode) (i64.const 2))) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("setbuf", 6)) {
    wasm_emitf(2, "(func $setbuf (param $stream i32) (param $buf i32))\n");
  }
  if (has_undefined_function("perror", 6)) {
    wasm_emitf(2, "(func $perror (param $s i32))\n");
  }
  if (has_undefined_function("fopen", 5)) {
    wasm_emitf(2, "(func $fopen (param i32 i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("freopen", 7)) {
    wasm_emitf(2, "(func $freopen (param i32 i32 i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("tmpfile", 7)) {
    wasm_emitf(2, "(func $tmpfile (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("tmpnam", 6)) {
    wasm_emitf(2, "(func $tmpnam (param i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("fdopen", 6)) {
    wasm_emitf(2, "(func $fdopen (param i64 i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("fclose", 6)) {
    wasm_emitf(2, "(func $fclose (param i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("remove", 6)) {
    wasm_emitf(2, "(func $remove (param i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("rename", 6)) {
    wasm_emitf(2, "(func $rename (param i32 i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("fflush", 6)) {
    wasm_emitf(2, "(func $fflush (param i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("fread", 5)) {
    wasm_emitf(2, "(func $fread (param i32 i64 i64 i32) (result i64) (i64.const 0))\n");
  }
  if (has_undefined_function("fwrite", 6)) {
    wasm_emitf(2, "(func $fwrite (param i32 i64 i64 i32) (result i64) (i64.const 0))\n");
  }
  if (has_undefined_function("fgetc", 5)) {
    wasm_emitf(2, "(func $fgetc (param i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("getc", 4)) {
    wasm_emitf(2, "(func $getc (param i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("getchar", 7)) {
    wasm_emitf(2, "(func $getchar (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("ungetc", 6)) {
    wasm_emitf(2, "(func $ungetc (param i64 i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("fgets", 5)) {
    wasm_emitf(2, "(func $fgets (param i32 i64 i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("getline", 7)) {
    wasm_emitf(2, "(func $getline (param i32 i32 i32) (result i64) (i64.const -1))\n");
  }
  if (has_undefined_function("fseek", 5)) {
    wasm_emitf(2, "(func $fseek (param $stream i32) (param $offset i64) (param $whence i64) (result i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $stream)) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(if (i32.or (i64.lt_s (local.get $whence) (i64.const 0)) (i64.gt_s (local.get $whence) (i64.const 2))) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("ftell", 5)) {
    wasm_emitf(2, "(func $ftell (param $stream i32) (result i64)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $stream)) (then (return (i64.const -1))))\n");
    wasm_emitf(4, "(i64.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fgetpos", 7)) {
    wasm_emitf(2, "(func $fgetpos (param $stream i32) (param $pos i32) (result i32)\n");
    wasm_emitf(4, "(if (i32.or (i32.eqz (local.get $stream)) (i32.eqz (local.get $pos))) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(i64.store (local.get $pos) (i64.const 0))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fsetpos", 7)) {
    wasm_emitf(2, "(func $fsetpos (param $stream i32) (param $pos i32) (result i32)\n");
    wasm_emitf(4, "(if (i32.or (i32.eqz (local.get $stream)) (i32.eqz (local.get $pos))) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("rewind", 6)) {
    wasm_emitf(2, "(func $rewind (param $stream i32))\n");
  }
  if (has_undefined_function("feof", 4)) {
    wasm_emitf(2, "(func $feof (param i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("ferror", 6)) {
    wasm_emitf(2, "(func $ferror (param i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("clearerr", 8)) {
    wasm_emitf(2, "(func $clearerr (param i32))\n");
  }
  if (has_undefined_function("__assert_rtn", 12)) {
    wasm_emitf(2, "(func $__assert_rtn (param i32 i32 i32 i32) (unreachable))\n");
  }
  if (has_undefined_function("__error", 7)) {
    int errno_addr = intern_data_symbol("__ag_stub_errno", 15, 4, 4)->addr;
    wasm_emitf(2, "(func $__error (result i32) (i32.const %d))\n", errno_addr);
  }
  if (has_undefined_function("exit", 4)) {
    wasm_emitf(2, "(func $exit (param $status i64) (unreachable))\n");
  }
  if (has_undefined_function("_Exit", 5)) {
    wasm_emitf(2, "(func $_Exit (param $status i64) (unreachable))\n");
  }
  if (has_undefined_function("quick_exit", 10)) {
    wasm_emitf(2, "(func $quick_exit (param $status i64) (unreachable))\n");
  }
  if (has_undefined_function("abort", 5)) {
    wasm_emitf(2, "(func $abort (unreachable))\n");
  }
  if (has_undefined_function("setjmp", 6)) {
    wasm_emitf(2, "(func $setjmp (param $env i64) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("longjmp", 7)) {
    wasm_emitf(2, "(func $longjmp (param $env i64) (param $val i64) (unreachable))\n");
  }
  int needs_format_dec_helper =
      has_undefined_function("snprintf", 8) || has_undefined_function("sprintf", 7);
  int need_rounding_mode_helper =
      has_undefined_function("nearbyint", 9) || has_undefined_function("nearbyintf", 10) ||
      has_undefined_function("nearbyintl", 10) || has_undefined_function("rint", 4) ||
      has_undefined_function("rintf", 5) || has_undefined_function("rintl", 5) ||
      has_undefined_function("lrint", 5) || has_undefined_function("lrintf", 6) ||
      has_undefined_function("lrintl", 6) || has_undefined_function("llrint", 6) ||
      has_undefined_function("llrintf", 7) || has_undefined_function("llrintl", 7);
  int need_fegetenv_stub =
      has_undefined_function("fegetenv", 8) || has_undefined_function("feholdexcept", 12);
  int need_fesetenv_stub =
      has_undefined_function("fesetenv", 8) || has_undefined_function("feupdateenv", 11);
  if (needs_format_dec_helper) {
    emit_wasm_u64_dec_helper();
  }
  if (has_undefined_function("strftime", 8) ||
      (has_undefined_function("wcsftime", 8) && !psx_ctx_is_function_defined("strftime", 8))) {
    emit_wasm_strftime_stub();
  }
  if (has_undefined_function("wcsftime", 8)) {
    emit_wasm_wcsftime_stub();
  }
  if (has_undefined_function("snprintf", 8)) {
    emit_wasm_snprintf_stubs();
  }
  if (has_undefined_function("sprintf", 7)) {
    emit_wasm_sprintf_stub();
  }
  if (has_undefined_function("printf", 6) || has_undefined_function("fprintf", 7) ||
      has_undefined_function("vprintf", 7) || has_undefined_function("vfprintf", 8) ||
      has_undefined_function("vsnprintf", 9) || has_undefined_function("vsprintf", 8)) {
    emit_wasm_vsnprintf_stubs();
  }
  if (has_undefined_function("printf", 6) || has_undefined_function("fprintf", 7)) {
    emit_wasm_printf_stubs();
  }
  if (has_undefined_function("sscanf", 6) || has_undefined_function("vsscanf", 7)) {
    emit_wasm_vsscanf_stubs();
  }
  if (has_undefined_function("scanf", 5)) {
    wasm_emitf(2, "(func $scanf (param $fmt i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("vscanf", 6)) {
    wasm_emitf(2, "(func $vscanf (param $fmt i32) (param $ap i64) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("fscanf", 6)) {
    wasm_emitf(2, "(func $fscanf (param $stream i32) (param $fmt i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("vfscanf", 7)) {
    wasm_emitf(2, "(func $vfscanf (param $stream i32) (param $fmt i32) (param $ap i64) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("swprintf", 8)) {
    emit_wasm_swprintf_stub();
  }
  if (has_undefined_function("swscanf", 7)) {
    emit_wasm_swscanf_stub();
  }
  if (has_undefined_function("imaxabs", 7)) {
    wasm_emitf(2, "(func $imaxabs (param $x i64) (result i64)\n");
    wasm_emitf(4, "(if (result i64) (i64.lt_s (local.get $x) (i64.const 0))\n");
    wasm_emitf(6, "(then (i64.sub (i64.const 0) (local.get $x)))\n");
    wasm_emitf(6, "(else (local.get $x))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("div", 3)) {
    wasm_emitf(2, "(func $div (param $numer i64) (param $denom i64) (result i64)\n");
    wasm_emitf(4, "(i64.or\n");
    wasm_emitf(6, "(i64.extend_i32_u (i32.wrap_i64 (i64.div_s (local.get $numer) (local.get $denom))))\n");
    wasm_emitf(6, "(i64.shl (i64.extend_i32_u (i32.wrap_i64 (i64.rem_s (local.get $numer) (local.get $denom)))) (i64.const 32))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("ldiv", 4)) {
    wasm_emitf(2, "(func $ldiv (param $ret i32) (param $numer i64) (param $denom i64)\n");
    wasm_emitf(4, "(i64.store (local.get $ret) (i64.div_s (local.get $numer) (local.get $denom)))\n");
    wasm_emitf(4, "(i64.store (i32.add (local.get $ret) (i32.const 8)) (i64.rem_s (local.get $numer) (local.get $denom)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("lldiv", 5)) {
    wasm_emitf(2, "(func $lldiv (param $ret i32) (param $numer i64) (param $denom i64)\n");
    wasm_emitf(4, "(i64.store (local.get $ret) (i64.div_s (local.get $numer) (local.get $denom)))\n");
    wasm_emitf(4, "(i64.store (i32.add (local.get $ret) (i32.const 8)) (i64.rem_s (local.get $numer) (local.get $denom)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("imaxdiv", 7)) {
    wasm_emitf(2, "(func $imaxdiv (param $ret i32) (param $numer i64) (param $denom i64)\n");
    wasm_emitf(4, "(i64.store (local.get $ret) (i64.div_s (local.get $numer) (local.get $denom)))\n");
    wasm_emitf(4, "(i64.store (i32.add (local.get $ret) (i32.const 8)) (i64.rem_s (local.get $numer) (local.get $denom)))\n");
    wasm_emitf(2, ")\n");
  }
  if (need_rounding_mode_helper || has_undefined_function("fegetround", 10) ||
      has_undefined_function("fesetround", 10) ||
      need_fegetenv_stub || need_fesetenv_stub ||
      has_undefined_function("feholdexcept", 12) || has_undefined_function("feupdateenv", 11)) {
    wasm_emitf(2, "(global $__ag_fe_round_mode (mut i32) (i32.const 0))\n");
  }
  int needs_fenv_except_flags =
      has_undefined_function("feclearexcept", 13) ||
      has_undefined_function("fegetexceptflag", 15) ||
      has_undefined_function("feraiseexcept", 13) ||
      has_undefined_function("fesetexceptflag", 15) ||
      has_undefined_function("fetestexcept", 12) ||
      need_fegetenv_stub || need_fesetenv_stub ||
      has_undefined_function("feholdexcept", 12) || has_undefined_function("feupdateenv", 11);
  if (needs_fenv_except_flags) {
    wasm_emitf(2, "(global $__ag_fe_except_flags (mut i32) (i32.const 0))\n");
  }
  if (has_undefined_function("feclearexcept", 13)) {
    wasm_emitf(2, "(func $feclearexcept (param $excepts i64) (result i32)\n");
    wasm_emitf(4, "(global.set $__ag_fe_except_flags\n");
    wasm_emitf(6, "(i32.and (global.get $__ag_fe_except_flags)\n");
    wasm_emitf(8, "(i32.xor (i32.wrap_i64 (local.get $excepts)) (i32.const -1))))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fegetexceptflag", 15)) {
    wasm_emitf(2, "(func $fegetexceptflag (param $flagp i32) (param $excepts i64) (result i32)\n");
    wasm_emitf(4, "(if (local.get $flagp) (then\n");
    wasm_emitf(6, "(i64.store (local.get $flagp)\n");
    wasm_emitf(8, "(i64.extend_i32_u (i32.and (global.get $__ag_fe_except_flags) (i32.wrap_i64 (local.get $excepts)))))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("feraiseexcept", 13)) {
    wasm_emitf(2, "(func $feraiseexcept (param $excepts i64) (result i32)\n");
    wasm_emitf(4, "(global.set $__ag_fe_except_flags\n");
    wasm_emitf(6, "(i32.or (global.get $__ag_fe_except_flags) (i32.wrap_i64 (local.get $excepts))))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fesetexceptflag", 15)) {
    wasm_emitf(2, "(func $fesetexceptflag (param $flagp i32) (param $excepts i64) (result i32)\n");
    wasm_emitf(4, "(local $mask i32)\n");
    wasm_emitf(4, "(local $flags i32)\n");
    wasm_emitf(4, "(local.set $mask (i32.wrap_i64 (local.get $excepts)))\n");
    wasm_emitf(4, "(if (local.get $flagp) (then (local.set $flags (i32.wrap_i64 (i64.load (local.get $flagp))))))\n");
    wasm_emitf(4, "(global.set $__ag_fe_except_flags\n");
    wasm_emitf(6, "(i32.or\n");
    wasm_emitf(8, "(i32.and (global.get $__ag_fe_except_flags) (i32.xor (local.get $mask) (i32.const -1)))\n");
    wasm_emitf(8, "(i32.and (local.get $flags) (local.get $mask))))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fetestexcept", 12)) {
    wasm_emitf(2, "(func $fetestexcept (param $mask i64) (result i32)\n");
    wasm_emitf(4, "(i32.and (global.get $__ag_fe_except_flags) (i32.wrap_i64 (local.get $mask)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fegetround", 10)) {
    wasm_emitf(2, "(func $fegetround (result i32) (global.get $__ag_fe_round_mode))\n");
  }
  if (has_undefined_function("fesetround", 10)) {
    wasm_emitf(2, "(func $fesetround (param $round i64) (result i32)\n");
    wasm_emitf(4, "(global.set $__ag_fe_round_mode (i32.wrap_i64 (local.get $round)))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (need_fegetenv_stub) {
    wasm_emitf(2, "(func $fegetenv (param $envp i32) (result i32)\n");
    wasm_emitf(4, "(if (local.get $envp) (then\n");
    wasm_emitf(6, "(i64.store (local.get $envp) (i64.extend_i32_u (global.get $__ag_fe_round_mode)))\n");
    wasm_emitf(6, "(i64.store (i32.add (local.get $envp) (i32.const 8)) (i64.extend_i32_u (global.get $__ag_fe_except_flags)))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("feholdexcept", 12)) {
    wasm_emitf(2, "(func $feholdexcept (param $envp i32) (result i32)\n");
    wasm_emitf(4, "(drop (call $fegetenv (local.get $envp)))\n");
    wasm_emitf(4, "(global.set $__ag_fe_except_flags (i32.const 0))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (need_fesetenv_stub) {
    wasm_emitf(2, "(func $fesetenv (param $envp i32) (result i32)\n");
    wasm_emitf(4, "(if (i32.eq (local.get $envp) (i32.const -1)) (then\n");
    wasm_emitf(6, "(global.set $__ag_fe_round_mode (i32.const 0))\n");
    wasm_emitf(6, "(global.set $__ag_fe_except_flags (i32.const 0))\n");
    wasm_emitf(4, ") (else\n");
    wasm_emitf(6, "(if (local.get $envp) (then\n");
    wasm_emitf(8, "(global.set $__ag_fe_round_mode (i32.wrap_i64 (i64.load (local.get $envp))))\n");
    wasm_emitf(8, "(global.set $__ag_fe_except_flags (i32.wrap_i64 (i64.load (i32.add (local.get $envp) (i32.const 8)))))\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("feupdateenv", 11)) {
    wasm_emitf(2, "(func $feupdateenv (param $envp i32) (result i32)\n");
    wasm_emitf(4, "(local $raised i32)\n");
    wasm_emitf(4, "(local.set $raised (global.get $__ag_fe_except_flags))\n");
    wasm_emitf(4, "(drop (call $fesetenv (local.get $envp)))\n");
    wasm_emitf(4, "(global.set $__ag_fe_except_flags (i32.or (global.get $__ag_fe_except_flags) (local.get $raised)))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  int need_iswctype_stub = has_undefined_function("iswctype", 8);
  int need_iswalnum_stub = has_undefined_function("iswalnum", 8) || need_iswctype_stub;
  int need_iswalpha_stub = has_undefined_function("iswalpha", 8) || need_iswalnum_stub || need_iswctype_stub;
  int need_iswblank_stub = has_undefined_function("iswblank", 8) || need_iswctype_stub;
  int need_iswcntrl_stub = has_undefined_function("iswcntrl", 8) || need_iswctype_stub;
  int need_iswdigit_stub = has_undefined_function("iswdigit", 8) || need_iswalnum_stub || need_iswctype_stub;
  int need_iswgraph_stub = has_undefined_function("iswgraph", 8) ||
                           has_undefined_function("iswpunct", 8) || need_iswctype_stub;
  int need_iswlower_stub = has_undefined_function("iswlower", 8) || need_iswctype_stub;
  int need_iswprint_stub = has_undefined_function("iswprint", 8) || need_iswctype_stub;
  int need_iswpunct_stub = has_undefined_function("iswpunct", 8) || need_iswctype_stub;
  int need_iswspace_stub = has_undefined_function("iswspace", 8) || need_iswctype_stub;
  int need_iswupper_stub = has_undefined_function("iswupper", 8) || need_iswctype_stub;
  int need_iswxdigit_stub = has_undefined_function("iswxdigit", 9) || need_iswctype_stub;
  int need_towlower_stub = has_undefined_function("towlower", 8) ||
                           has_undefined_function("towctrans", 9);
  int need_towupper_stub = has_undefined_function("towupper", 8) ||
                           has_undefined_function("towctrans", 9);
  int need_isalnum_stub = has_undefined_function("isalnum", 7) ||
                          has_undefined_function("ispunct", 7) ||
                          need_iswalnum_stub || need_iswpunct_stub;
  int need_isalpha_stub = has_undefined_function("isalpha", 7) || need_isalnum_stub ||
                          need_iswalpha_stub;
  int need_isdigit_stub = has_undefined_function("isdigit", 7) ||
                          has_undefined_function("isxdigit", 8) ||
                          need_isalnum_stub || need_iswdigit_stub || need_iswxdigit_stub;
  int need_islower_stub = has_undefined_function("islower", 7) ||
                          has_undefined_function("toupper", 7) || need_towupper_stub ||
                          need_isalpha_stub || need_iswlower_stub;
  int need_isupper_stub = has_undefined_function("isupper", 7) ||
                          has_undefined_function("tolower", 7) || need_towlower_stub ||
                          need_isalpha_stub || need_iswupper_stub;
  int need_isgraph_stub = has_undefined_function("isgraph", 7) ||
                          has_undefined_function("ispunct", 7) || need_iswgraph_stub ||
                          need_iswpunct_stub;
  if (need_isalpha_stub) {
    wasm_emitf(2, "(func $isalpha (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.or\n");
    wasm_emitf(6, "(i32.and (i64.ge_s (local.get $c) (i64.const 65)) (i64.le_s (local.get $c) (i64.const 90)))\n");
    wasm_emitf(6, "(i32.and (i64.ge_s (local.get $c) (i64.const 97)) (i64.le_s (local.get $c) (i64.const 122)))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (need_isdigit_stub) {
    wasm_emitf(2, "(func $isdigit (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i64.ge_s (local.get $c) (i64.const 48)) (i64.le_s (local.get $c) (i64.const 57)))\n");
    wasm_emitf(2, ")\n");
  }
  if (need_islower_stub) {
    wasm_emitf(2, "(func $islower (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i64.ge_s (local.get $c) (i64.const 97)) (i64.le_s (local.get $c) (i64.const 122)))\n");
    wasm_emitf(2, ")\n");
  }
  if (need_isupper_stub) {
    wasm_emitf(2, "(func $isupper (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i64.ge_s (local.get $c) (i64.const 65)) (i64.le_s (local.get $c) (i64.const 90)))\n");
    wasm_emitf(2, ")\n");
  }
  if (need_isalnum_stub) {
    wasm_emitf(2, "(func $isalnum (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.or (call $isalpha (local.get $c)) (call $isdigit (local.get $c)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("isblank", 7) || need_iswblank_stub) {
    wasm_emitf(2, "(func $isblank (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.or (i64.eq (local.get $c) (i64.const 32)) (i64.eq (local.get $c) (i64.const 9)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("iscntrl", 7) || need_iswcntrl_stub) {
    wasm_emitf(2, "(func $iscntrl (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.or (i32.and (i64.ge_s (local.get $c) (i64.const 0)) (i64.lt_s (local.get $c) (i64.const 32))) (i64.eq (local.get $c) (i64.const 127)))\n");
    wasm_emitf(2, ")\n");
  }
  if (need_isgraph_stub) {
    wasm_emitf(2, "(func $isgraph (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i64.ge_s (local.get $c) (i64.const 33)) (i64.le_s (local.get $c) (i64.const 126)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("isprint", 7) || need_iswprint_stub) {
    wasm_emitf(2, "(func $isprint (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i64.ge_s (local.get $c) (i64.const 32)) (i64.le_s (local.get $c) (i64.const 126)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("ispunct", 7) || need_iswpunct_stub) {
    wasm_emitf(2, "(func $ispunct (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.and (call $isgraph (local.get $c)) (i32.eqz (call $isalnum (local.get $c))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("isspace", 7) || need_iswspace_stub) {
    wasm_emitf(2, "(func $isspace (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.or (i64.eq (local.get $c) (i64.const 32))\n");
    wasm_emitf(6, "(i32.or (i64.eq (local.get $c) (i64.const 9))\n");
    wasm_emitf(8, "(i32.or (i64.eq (local.get $c) (i64.const 10))\n");
    wasm_emitf(10, "(i32.or (i64.eq (local.get $c) (i64.const 11))\n");
    wasm_emitf(12, "(i32.or (i64.eq (local.get $c) (i64.const 12)) (i64.eq (local.get $c) (i64.const 13)))))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("isxdigit", 8) || need_iswxdigit_stub) {
    wasm_emitf(2, "(func $isxdigit (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.or (call $isdigit (local.get $c))\n");
    wasm_emitf(6, "(i32.or (i32.and (i64.ge_s (local.get $c) (i64.const 65)) (i64.le_s (local.get $c) (i64.const 70)))\n");
    wasm_emitf(8, "(i32.and (i64.ge_s (local.get $c) (i64.const 97)) (i64.le_s (local.get $c) (i64.const 102)))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("tolower", 7) || need_towlower_stub) {
    wasm_emitf(2, "(func $tolower (param $c i64) (result i32)\n");
    wasm_emitf(4, "(if (result i32) (call $isupper (local.get $c))\n");
    wasm_emitf(6, "(then (i32.wrap_i64 (i64.add (local.get $c) (i64.const 32))))\n");
    wasm_emitf(6, "(else (i32.wrap_i64 (local.get $c)))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("toupper", 7) || need_towupper_stub) {
    wasm_emitf(2, "(func $toupper (param $c i64) (result i32)\n");
    wasm_emitf(4, "(if (result i32) (call $islower (local.get $c))\n");
    wasm_emitf(6, "(then (i32.wrap_i64 (i64.sub (local.get $c) (i64.const 32))))\n");
    wasm_emitf(6, "(else (i32.wrap_i64 (local.get $c)))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("abs", 3)) {
    wasm_emitf(2, "(func $abs (param $x i64) (result i32)\n");
    wasm_emitf(4, "(i32.wrap_i64 (if (result i64) (i64.lt_s (local.get $x) (i64.const 0))\n");
    wasm_emitf(6, "(then (i64.sub (i64.const 0) (local.get $x)))\n");
    wasm_emitf(6, "(else (local.get $x))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strlen", 6)) {
    ir_type_t ret_ty = wasm_function_result_type_from_decl("strlen", 6);
    const char *ret_wty = ret_ty == IR_TY_I64 ? "i64" : "i32";
    wasm_emitf(2, "(func $strlen (param $s i32) (result %s)\n", ret_wty);
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $n i64)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.eq (i32.load8_u (local.get $p)) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $n (i64.add (local.get $n) (i64.const 1)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    if (ret_ty == IR_TY_I64) {
      wasm_emitf(4, "(local.get $n)\n");
    } else {
      wasm_emitf(4, "(i32.wrap_i64 (local.get $n))\n");
    }
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strcpy", 6)) {
    wasm_emitf(2, "(func $strcpy (param $dst i32) (param $src i32) (result i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local.set $d (local.get $dst))\n");
    wasm_emitf(4, "(local.set $s (local.get $src))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $c (i32.load8_u (local.get $s)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $d) (local.get $c))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strncpy", 7)) {
    wasm_emitf(2, "(func $strncpy (param $dst i32) (param $src i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local $zeroing i32)\n");
    wasm_emitf(4, "(local.set $d (local.get $dst))\n");
    wasm_emitf(4, "(local.set $s (local.get $src))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $d) (i32.wrap_i64 (local.get $n))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $d) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(if (local.get $zeroing)\n");
    wasm_emitf(8, "(then (local.set $c (i32.const 0)))\n");
    wasm_emitf(8, "(else\n");
    wasm_emitf(10, "(local.set $c (i32.load8_u (local.get $s)))\n");
    wasm_emitf(10, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(10, "(if (i32.eq (local.get $c) (i32.const 0)) (then (local.set $zeroing (i32.const 1))))\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(6, "(i32.store8 (local.get $d) (local.get $c))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strcat", 6)) {
    wasm_emitf(2, "(func $strcat (param $dst i32) (param $src i32) (result i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local.set $d (local.get $dst))\n");
    wasm_emitf(4, "(local.set $s (local.get $src))\n");
    wasm_emitf(4, "(block $found (loop $find\n");
    wasm_emitf(6, "(if (i32.eq (i32.load8_u (local.get $d)) (i32.const 0)) (then (br $found)))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(6, "(br $find)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done (loop $copy\n");
    wasm_emitf(6, "(local.set $c (i32.load8_u (local.get $s)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $d) (local.get $c))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(6, "(br $copy)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strncat", 7)) {
    wasm_emitf(2, "(func $strncat (param $dst i32) (param $src i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $count i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(block $end_done (loop $end_loop\n");
    wasm_emitf(6, "(if (i32.eq (i32.load8_u (i32.add (local.get $dst) (local.get $end))) (i32.const 0)) (then (br $end_done)))\n");
    wasm_emitf(6, "(local.set $end (i32.add (local.get $end) (i32.const 1)))\n");
    wasm_emitf(6, "(br $end_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $copy_done (loop $copy_loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $count) (i32.wrap_i64 (local.get $n))) (then (br $copy_done)))\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (i32.add (local.get $src) (local.get $count))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $copy_done)))\n");
    wasm_emitf(6, "(i32.store8 (i32.add (i32.add (local.get $dst) (local.get $end)) (local.get $count)) (local.get $ch))\n");
    wasm_emitf(6, "(local.set $count (i32.add (local.get $count) (i32.const 1)))\n");
    wasm_emitf(6, "(br $copy_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.store8 (i32.add (i32.add (local.get $dst) (local.get $end)) (local.get $count)) (i32.const 0))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strcmp", 6)) {
    wasm_emitf(2, "(func $strcmp (param $a i32) (param $b i32) (result i32)\n");
    wasm_emitf(4, "(local $pa i32)\n");
    wasm_emitf(4, "(local $pb i32)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(local.set $pa (local.get $a))\n");
    wasm_emitf(4, "(local.set $pb (local.get $b))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ca (i32.load8_u (local.get $pa)))\n");
    wasm_emitf(6, "(local.set $cb (i32.load8_u (local.get $pb)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ca) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $pa (i32.add (local.get $pa) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $pb (i32.add (local.get $pb) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strncmp", 7)) {
    wasm_emitf(2, "(func $strncmp (param $a i32) (param $b i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $pa i32)\n");
    wasm_emitf(4, "(local $pb i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(local.set $pa (local.get $a))\n");
    wasm_emitf(4, "(local.set $pb (local.get $b))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $pa) (i32.wrap_i64 (local.get $n))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $pa) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $ca (i32.load8_u (local.get $pa)))\n");
    wasm_emitf(6, "(local.set $cb (i32.load8_u (local.get $pb)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ca) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $pa (i32.add (local.get $pa) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $pb (i32.add (local.get $pb) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strcoll", 7)) {
    wasm_emitf(2, "(func $strcoll (param $a i32) (param $b i32) (result i32)\n");
    wasm_emitf(4, "(local $pa i32)\n");
    wasm_emitf(4, "(local $pb i32)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(local.set $pa (local.get $a))\n");
    wasm_emitf(4, "(local.set $pb (local.get $b))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ca (i32.load8_u (local.get $pa)))\n");
    wasm_emitf(6, "(local.set $cb (i32.load8_u (local.get $pb)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ca)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $pa (i32.add (local.get $pa) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $pb (i32.add (local.get $pb) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strxfrm", 7)) {
    wasm_emitf(2, "(func $strxfrm (param $dst i32) (param $src i32) (param $n i64) (result i64)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $len i64)\n");
    wasm_emitf(4, "(local $copy i64)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $src))\n");
    wasm_emitf(4, "(block $len_done (loop $len_loop\n");
    wasm_emitf(6, "(if (i32.eqz (i32.load8_u (local.get $p))) (then (br $len_done)))\n");
    wasm_emitf(6, "(local.set $len (i64.add (local.get $len) (i64.const 1)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $len_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i64.gt_u (local.get $n) (i64.const 0)) (then\n");
    wasm_emitf(6, "(block $copy_done (loop $copy_loop\n");
    wasm_emitf(8, "(if (i64.ge_u (i64.add (local.get $copy) (i64.const 1)) (local.get $n)) (then (br $copy_done)))\n");
    wasm_emitf(8, "(local.set $ch (i32.load8_u (i32.add (local.get $src) (i32.wrap_i64 (local.get $copy)))))\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $ch)) (then (br $copy_done)))\n");
    wasm_emitf(8, "(i32.store8 (i32.add (local.get $dst) (i32.wrap_i64 (local.get $copy))) (local.get $ch))\n");
    wasm_emitf(8, "(local.set $copy (i64.add (local.get $copy) (i64.const 1)))\n");
    wasm_emitf(8, "(br $copy_loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(i32.store8 (i32.add (local.get $dst) (i32.wrap_i64 (local.get $copy))) (i32.const 0))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $len)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strchr", 6)) {
    wasm_emitf(2, "(func $strchr (param $s i32) (param $ch i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $c (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.wrap_i64 (local.get $ch))) (then (return (local.get $p))))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strrchr", 7)) {
    wasm_emitf(2, "(func $strrchr (param $s i32) (param $ch i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $last i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $c (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.wrap_i64 (local.get $ch))) (then (local.set $last (local.get $p))))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $last)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strstr", 6)) {
    wasm_emitf(2, "(func $strstr (param $haystack i32) (param $needle i32) (result i32)\n");
    wasm_emitf(4, "(local $h i32)\n");
    wasm_emitf(4, "(local $j i32)\n");
    wasm_emitf(4, "(local $hn i32)\n");
    wasm_emitf(4, "(local $nn i32)\n");
    wasm_emitf(4, "(if (i32.eqz (i32.load8_u (local.get $needle))) (then (return (local.get $haystack))))\n");
    wasm_emitf(4, "(local.set $h (local.get $haystack))\n");
    wasm_emitf(4, "(block $done (loop $outer\n");
    wasm_emitf(6, "(if (i32.eqz (i32.load8_u (local.get $h))) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $j (i32.const 0))\n");
    wasm_emitf(6, "(block $next (loop $inner\n");
    wasm_emitf(8, "(local.set $nn (i32.load8_u (i32.add (local.get $needle) (local.get $j))))\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $nn)) (then (return (local.get $h))))\n");
    wasm_emitf(8, "(local.set $hn (i32.load8_u (i32.add (local.get $h) (local.get $j))))\n");
    wasm_emitf(8, "(if (i32.ne (local.get $hn) (local.get $nn)) (then (br $next)))\n");
    wasm_emitf(8, "(local.set $j (i32.add (local.get $j) (i32.const 1)))\n");
    wasm_emitf(8, "(br $inner)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(local.set $h (i32.add (local.get $h) (i32.const 1)))\n");
    wasm_emitf(6, "(br $outer)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  int need_str_set_contains_stub = has_undefined_function("strspn", 6) ||
                                   has_undefined_function("strcspn", 7) ||
                                   has_undefined_function("strpbrk", 7);
  if (need_str_set_contains_stub) {
    wasm_emitf(2, "(func $__ag_str_contains (param $set i32) (param $ch i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $set))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $d (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $d)) (then (br $done)))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $d) (local.get $ch)) (then (return (i32.const 1))))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strspn", 6)) {
    wasm_emitf(2, "(func $strspn (param $s i32) (param $accept i32) (result i64)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $n i64)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(if (i32.eqz (call $__ag_str_contains (local.get $accept) (local.get $ch))) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $n (i64.add (local.get $n) (i64.const 1)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $n)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strcspn", 7)) {
    wasm_emitf(2, "(func $strcspn (param $s i32) (param $reject i32) (result i64)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $n i64)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(if (call $__ag_str_contains (local.get $reject) (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $n (i64.add (local.get $n) (i64.const 1)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $n)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strpbrk", 7)) {
    wasm_emitf(2, "(func $strpbrk (param $s i32) (param $accept i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(if (call $__ag_str_contains (local.get $accept) (local.get $ch)) (then (return (local.get $p))))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strtok", 6)) {
    wasm_emitf(2, "(global $__ag_strtok_next (mut i32) (i32.const 0))\n");
    wasm_emitf(2, "(func $__ag_strtok_is_delim (param $ch i32) (param $delim i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $d (i32.load8_u (i32.add (local.get $delim) (local.get $p))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $d)) (then (br $done)))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $d) (local.get $ch)) (then (return (i32.const 1))))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
    wasm_emitf(2, "(func $strtok (param $str i32) (param $delim i32) (result i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(if (i32.ne (local.get $str) (i32.const 0)) (then (local.set $s (local.get $str))) (else (local.set $s (global.get $__ag_strtok_next))))\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $s)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(block $skip_done (loop $skip_loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $s)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (global.set $__ag_strtok_next (i32.const 0)) (return (i32.const 0))))\n");
    wasm_emitf(6, "(if (i32.eqz (call $__ag_strtok_is_delim (local.get $ch) (local.get $delim))) (then (br $skip_done)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(6, "(br $skip_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $scan_done (loop $scan_loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (global.set $__ag_strtok_next (i32.const 0)) (return (local.get $s))))\n");
    wasm_emitf(6, "(if (call $__ag_strtok_is_delim (local.get $ch) (local.get $delim)) (then\n");
    wasm_emitf(8, "(i32.store8 (local.get $p) (i32.const 0))\n");
    wasm_emitf(8, "(global.set $__ag_strtok_next (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(return (local.get $s))\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $scan_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strerror", 8)) {
    wasm_data_symbol_t *ok = intern_data_symbol("__ag_stub_strerror_ok", 21, 9, 1);
    wasm_data_symbol_t *err = intern_data_symbol("__ag_stub_strerror", 18, 6, 1);
    wasm_emitf(2, "(data (i32.const %d) \"no error\\00\")\n", ok->addr);
    wasm_emitf(2, "(data (i32.const %d) \"error\\00\")\n", err->addr);
    wasm_emitf(2, "(func $strerror (param $errnum i64) (result i32)\n");
    wasm_emitf(4, "(if (i64.eqz (local.get $errnum)) (then (return (i32.const %d))))\n", ok->addr);
    wasm_emitf(4, "(i32.const %d)\n", err->addr);
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("putchar", 7)) {
    wasm_emitf(2, "(func $putchar (param $c i64) (result i32) (i32.wrap_i64 (local.get $c)))\n");
  }
  if (has_undefined_function("memset", 6)) {
    wasm_emitf(2, "(func $memset (param $dst i32) (param $ch i64) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $dst))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $p) (i32.wrap_i64 (local.get $n))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $p) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $p) (i32.wrap_i64 (local.get $ch)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("memcpy", 6)) {
    wasm_emitf(2, "(func $memcpy (param $dst i32) (param $src i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local.set $d (local.get $dst))\n");
    wasm_emitf(4, "(local.set $s (local.get $src))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $d) (i32.wrap_i64 (local.get $n))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $d) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $d) (i32.load8_u (local.get $s)))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("memmove", 7)) {
    wasm_emitf(2, "(func $memmove (param $dst i32) (param $src i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $count i32)\n");
    wasm_emitf(4, "(local.set $count (i32.wrap_i64 (local.get $n)))\n");
    wasm_emitf(4, "(if (i32.le_u (local.get $dst) (local.get $src)) (then\n");
    wasm_emitf(6, "(local.set $d (local.get $dst))\n");
    wasm_emitf(6, "(local.set $s (local.get $src))\n");
    wasm_emitf(6, "(local.set $end (i32.add (local.get $d) (local.get $count)))\n");
    wasm_emitf(6, "(block $forward_done (loop $forward_loop\n");
    wasm_emitf(8, "(if (i32.ge_u (local.get $d) (local.get $end)) (then (br $forward_done)))\n");
    wasm_emitf(8, "(i32.store8 (local.get $d) (i32.load8_u (local.get $s)))\n");
    wasm_emitf(8, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(8, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(8, "(br $forward_loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(4, ") (else\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $dst) (local.get $count)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $src) (local.get $count)))\n");
    wasm_emitf(6, "(block $backward_done (loop $backward_loop\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $count)) (then (br $backward_done)))\n");
    wasm_emitf(8, "(local.set $d (i32.sub (local.get $d) (i32.const 1)))\n");
    wasm_emitf(8, "(local.set $s (i32.sub (local.get $s) (i32.const 1)))\n");
    wasm_emitf(8, "(i32.store8 (local.get $d) (i32.load8_u (local.get $s)))\n");
    wasm_emitf(8, "(local.set $count (i32.sub (local.get $count) (i32.const 1)))\n");
    wasm_emitf(8, "(br $backward_loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("memchr", 6)) {
    wasm_emitf(2, "(func $memchr (param $s i32) (param $ch i64) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $needle i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $p) (i32.wrap_i64 (local.get $n))))\n");
    wasm_emitf(4, "(local.set $needle (i32.and (i32.wrap_i64 (local.get $ch)) (i32.const 255)))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $p) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(if (i32.eq (i32.load8_u (local.get $p)) (local.get $needle)) (then (return (local.get $p))))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("memcmp", 6)) {
    wasm_emitf(2, "(func $memcmp (param $a i32) (param $b i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $pa i32)\n");
    wasm_emitf(4, "(local $pb i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(local.set $pa (local.get $a))\n");
    wasm_emitf(4, "(local.set $pb (local.get $b))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $pa) (i32.wrap_i64 (local.get $n))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $pa) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $ca (i32.load8_u (local.get $pa)))\n");
    wasm_emitf(6, "(local.set $cb (i32.load8_u (local.get $pb)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(local.set $pa (i32.add (local.get $pa) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $pb (i32.add (local.get $pb) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  int need_strto64_stub = has_undefined_function("strtol", 6) || has_undefined_function("strtoul", 7) ||
                          has_undefined_function("strtoimax", 9) || has_undefined_function("strtoumax", 9) ||
                          has_undefined_function("strtoll", 7) || has_undefined_function("strtoull", 8) ||
                          has_undefined_function("atol", 4) || has_undefined_function("atoll", 5) ||
                          has_undefined_function("atoi", 4);
  if (need_strto64_stub) {
    wasm_emitf(2, "(func $__ag_strto64 (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $digit i64)\n");
    wasm_emitf(4, "(local $n i64)\n");
    wasm_emitf(4, "(local $neg i32)\n");
    wasm_emitf(4, "(local $any_digit i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $space_done (loop $space_loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.or (i32.eq (local.get $ch) (i32.const 32)) (i32.or (i32.eq (local.get $ch) (i32.const 9)) (i32.or (i32.eq (local.get $ch) (i32.const 10)) (i32.or (i32.eq (local.get $ch) (i32.const 11)) (i32.or (i32.eq (local.get $ch) (i32.const 12)) (i32.eq (local.get $ch) (i32.const 13))))))) (then\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(br $space_loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(br $space_done)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.eq (i32.load8_u (local.get $p)) (i32.const 45)) (then\n");
    wasm_emitf(6, "(local.set $neg (i32.const 1))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(4, ") (else (if (i32.eq (i32.load8_u (local.get $p)) (i32.const 43)) (then\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(4, "))))\n");
    wasm_emitf(4, "(if (i64.eqz (local.get $base)) (then (local.set $base (i64.const 10))))\n");
    wasm_emitf(4, "(if (i32.and (i64.eq (local.get $base) (i64.const 16)) (i32.and (i32.eq (i32.load8_u (local.get $p)) (i32.const 48)) (i32.or (i32.eq (i32.load8_u (i32.add (local.get $p) (i32.const 1))) (i32.const 120)) (i32.eq (i32.load8_u (i32.add (local.get $p) (i32.const 1))) (i32.const 88))))) (then\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 2)))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(local.set $digit (i64.const -1))\n");
    wasm_emitf(6, "(if (i32.and (i32.ge_u (local.get $ch) (i32.const 48)) (i32.le_u (local.get $ch) (i32.const 57))) (then\n");
    wasm_emitf(8, "(local.set $digit (i64.extend_i32_u (i32.sub (local.get $ch) (i32.const 48))))\n");
    wasm_emitf(6, ") (else (if (i32.and (i32.ge_u (local.get $ch) (i32.const 97)) (i32.le_u (local.get $ch) (i32.const 122))) (then\n");
    wasm_emitf(8, "(local.set $digit (i64.extend_i32_u (i32.sub (local.get $ch) (i32.const 87))))\n");
    wasm_emitf(6, ") (else (if (i32.and (i32.ge_u (local.get $ch) (i32.const 65)) (i32.le_u (local.get $ch) (i32.const 90))) (then\n");
    wasm_emitf(8, "(local.set $digit (i64.extend_i32_u (i32.sub (local.get $ch) (i32.const 55))))\n");
    wasm_emitf(6, "))))))\n");
    wasm_emitf(6, "(if (i64.lt_s (local.get $digit) (i64.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(if (i64.ge_s (local.get $digit) (local.get $base)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $any_digit (i32.const 1))\n");
    wasm_emitf(6, "(local.set $n (i64.add (i64.mul (local.get $n) (local.get $base)) (local.get $digit)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $any_digit)) (then\n");
    wasm_emitf(6, "(if (local.get $endptr) (then (i64.store (local.get $endptr) (i64.extend_i32_u (local.get $s)))))\n");
    wasm_emitf(6, "(return (i64.const 0))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (local.get $endptr) (then (i64.store (local.get $endptr) (i64.extend_i32_u (local.get $p)))))\n");
    wasm_emitf(4, "(if (result i64) (local.get $neg) (then (i64.sub (i64.const 0) (local.get $n))) (else (local.get $n)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strtol", 6)) {
    wasm_emitf(2, "(func $strtol (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(call $__ag_strto64 (local.get $s) (local.get $endptr) (local.get $base))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strtoul", 7)) {
    wasm_emitf(2, "(func $strtoul (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(call $__ag_strto64 (local.get $s) (local.get $endptr) (local.get $base))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strtoimax", 9)) {
    wasm_emitf(2, "(func $strtoimax (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(call $__ag_strto64 (local.get $s) (local.get $endptr) (local.get $base))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strtoumax", 9)) {
    wasm_emitf(2, "(func $strtoumax (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(call $__ag_strto64 (local.get $s) (local.get $endptr) (local.get $base))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strtoll", 7)) {
    wasm_emitf(2, "(func $strtoll (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(call $__ag_strto64 (local.get $s) (local.get $endptr) (local.get $base))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strtoull", 8)) {
    wasm_emitf(2, "(func $strtoull (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(call $__ag_strto64 (local.get $s) (local.get $endptr) (local.get $base))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("atol", 4)) {
    wasm_emitf(2, "(func $atol (param $s i32) (result i64)\n");
    wasm_emitf(4, "(call $__ag_strto64 (local.get $s) (i32.const 0) (i64.const 10))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("atoll", 5)) {
    wasm_emitf(2, "(func $atoll (param $s i32) (result i64)\n");
    wasm_emitf(4, "(call $__ag_strto64 (local.get $s) (i32.const 0) (i64.const 10))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("atoi", 4)) {
    wasm_emitf(2, "(func $atoi (param $s i32) (result i32)\n");
    wasm_emitf(4, "(i32.wrap_i64 (call $atol (local.get $s)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strtod", 6) || has_undefined_function("strtof", 6) ||
      has_undefined_function("strtold", 7) || has_undefined_function("atof", 4)) {
    wasm_emitf(2, "(func $__ag_strtod (param $s i32) (param $endptr i32) (result f64)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $sign f64)\n");
    wasm_emitf(4, "(local $acc f64)\n");
    wasm_emitf(4, "(local $place f64)\n");
    wasm_emitf(4, "(local $exp i64)\n");
    wasm_emitf(4, "(local $exp_sign i32)\n");
    wasm_emitf(4, "(local $exp_start i32)\n");
    wasm_emitf(4, "(local $have_exp i32)\n");
    wasm_emitf(4, "(local $any_digit i32)\n");
    wasm_emitf(4, "(local.set $sign (f64.const 1.0))\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $space_done (loop $space_loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.or (i32.eq (local.get $ch) (i32.const 32)) (i32.or (i32.eq (local.get $ch) (i32.const 9)) (i32.or (i32.eq (local.get $ch) (i32.const 10)) (i32.or (i32.eq (local.get $ch) (i32.const 11)) (i32.or (i32.eq (local.get $ch) (i32.const 12)) (i32.eq (local.get $ch) (i32.const 13))))))) (then\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(br $space_loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(br $space_done)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.eq (i32.load8_u (local.get $p)) (i32.const 45)) (then\n");
    wasm_emitf(6, "(local.set $sign (f64.const -1.0))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(4, ") (else (if (i32.eq (i32.load8_u (local.get $p)) (i32.const 43)) (then\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(4, "))))\n");
    wasm_emitf(4, "(block $int_done (loop $int_loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.or (i32.lt_u (local.get $ch) (i32.const 48)) (i32.gt_u (local.get $ch) (i32.const 57))) (then (br $int_done)))\n");
    wasm_emitf(6, "(local.set $any_digit (i32.const 1))\n");
    wasm_emitf(6, "(local.set $acc (f64.add (f64.mul (local.get $acc) (f64.const 10.0)) (f64.convert_i32_u (i32.sub (local.get $ch) (i32.const 48)))))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $int_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.eq (i32.load8_u (local.get $p)) (i32.const 46)) (then\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $place (f64.const 0.1))\n");
    wasm_emitf(6, "(block $frac_done (loop $frac_loop\n");
    wasm_emitf(8, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(8, "(if (i32.or (i32.lt_u (local.get $ch) (i32.const 48)) (i32.gt_u (local.get $ch) (i32.const 57))) (then (br $frac_done)))\n");
    wasm_emitf(8, "(local.set $any_digit (i32.const 1))\n");
    wasm_emitf(8, "(local.set $acc (f64.add (local.get $acc) (f64.mul (f64.convert_i32_u (i32.sub (local.get $ch) (i32.const 48))) (local.get $place))))\n");
    wasm_emitf(8, "(local.set $place (f64.div (local.get $place) (f64.const 10.0)))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(br $frac_loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(4, "(if (i32.or (i32.eq (local.get $ch) (i32.const 101)) (i32.eq (local.get $ch) (i32.const 69))) (then\n");
    wasm_emitf(6, "(local.set $exp_start (local.get $p))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $exp_sign (i32.const 1))\n");
    wasm_emitf(6, "(if (i32.eq (i32.load8_u (local.get $p)) (i32.const 45)) (then\n");
    wasm_emitf(8, "(local.set $exp_sign (i32.const -1))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, ") (else (if (i32.eq (i32.load8_u (local.get $p)) (i32.const 43)) (then\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "))))\n");
    wasm_emitf(6, "(block $exp_digits_done (loop $exp_digits_loop\n");
    wasm_emitf(8, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(8, "(if (i32.or (i32.lt_u (local.get $ch) (i32.const 48)) (i32.gt_u (local.get $ch) (i32.const 57))) (then (br $exp_digits_done)))\n");
    wasm_emitf(8, "(local.set $have_exp (i32.const 1))\n");
    wasm_emitf(8, "(local.set $exp (i64.add (i64.mul (local.get $exp) (i64.const 10)) (i64.extend_i32_u (i32.sub (local.get $ch) (i32.const 48)))))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(8, "(br $exp_digits_loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (local.get $have_exp) (then\n");
    wasm_emitf(8, "(block $scale_done (loop $scale_loop\n");
    wasm_emitf(10, "(if (i64.eqz (local.get $exp)) (then (br $scale_done)))\n");
    wasm_emitf(10, "(if (i32.lt_s (local.get $exp_sign) (i32.const 0))\n");
    wasm_emitf(12, "(then (local.set $acc (f64.div (local.get $acc) (f64.const 10.0))))\n");
    wasm_emitf(12, "(else (local.set $acc (f64.mul (local.get $acc) (f64.const 10.0))))\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(10, "(local.set $exp (i64.sub (local.get $exp) (i64.const 1)))\n");
    wasm_emitf(10, "(br $scale_loop)\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(6, ") (else (local.set $p (local.get $exp_start))))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $any_digit)) (then\n");
    wasm_emitf(6, "(if (local.get $endptr) (then (i64.store (local.get $endptr) (i64.extend_i32_u (local.get $s)))))\n");
    wasm_emitf(6, "(return (f64.const 0))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (local.get $endptr) (then (i64.store (local.get $endptr) (i64.extend_i32_u (local.get $p)))))\n");
    wasm_emitf(4, "(f64.mul (local.get $acc) (local.get $sign))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strtod", 6)) {
    wasm_emitf(2, "(func $strtod (param $s i32) (param $endptr i32) (result f64)\n");
    wasm_emitf(4, "(call $__ag_strtod (local.get $s) (local.get $endptr))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strtof", 6)) {
    wasm_emitf(2, "(func $strtof (param $s i32) (param $endptr i32) (result f32)\n");
    wasm_emitf(4, "(f32.demote_f64 (call $__ag_strtod (local.get $s) (local.get $endptr)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strtold", 7)) {
    wasm_emitf(2, "(func $strtold (param $s i32) (param $endptr i32) (result f64)\n");
    wasm_emitf(4, "(call $__ag_strtod (local.get $s) (local.get $endptr))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("atof", 4)) {
    wasm_emitf(2, "(func $atof (param $s i32) (result f64)\n");
    wasm_emitf(4, "(call $__ag_strtod (local.get $s) (i32.const 0))\n");
    wasm_emitf(2, ")\n");
  }
  int need_wcsto64_stub = has_undefined_function("wcstol", 6) ||
                          has_undefined_function("wcstoul", 7) ||
                          has_undefined_function("wcstoll", 7) ||
                          has_undefined_function("wcstoull", 8);
  if (need_wcsto64_stub) {
    wasm_emitf(2, "(func $__ag_wcsto64 (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $digit i64)\n");
    wasm_emitf(4, "(local $n i64)\n");
    wasm_emitf(4, "(local $neg i32)\n");
    wasm_emitf(4, "(local $any_digit i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $space_done (loop $space_loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.or (i32.eq (local.get $ch) (i32.const 32)) (i32.or (i32.eq (local.get $ch) (i32.const 9)) (i32.or (i32.eq (local.get $ch) (i32.const 10)) (i32.or (i32.eq (local.get $ch) (i32.const 11)) (i32.or (i32.eq (local.get $ch) (i32.const 12)) (i32.eq (local.get $ch) (i32.const 13))))))) (then\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(8, "(br $space_loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(br $space_done)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.eq (i32.load (local.get $p)) (i32.const 45)) (then\n");
    wasm_emitf(6, "(local.set $neg (i32.const 1))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(4, ") (else (if (i32.eq (i32.load (local.get $p)) (i32.const 43)) (then\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(4, "))))\n");
    wasm_emitf(4, "(if (i64.eqz (local.get $base)) (then (local.set $base (i64.const 10))))\n");
    wasm_emitf(4, "(if (i32.and (i64.eq (local.get $base) (i64.const 16)) (i32.and (i32.eq (i32.load (local.get $p)) (i32.const 48)) (i32.or (i32.eq (i32.load (i32.add (local.get $p) (i32.const 4))) (i32.const 120)) (i32.eq (i32.load (i32.add (local.get $p) (i32.const 4))) (i32.const 88))))) (then\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 8)))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
    wasm_emitf(6, "(local.set $digit (i64.const -1))\n");
    wasm_emitf(6, "(if (i32.and (i32.ge_u (local.get $ch) (i32.const 48)) (i32.le_u (local.get $ch) (i32.const 57))) (then\n");
    wasm_emitf(8, "(local.set $digit (i64.extend_i32_u (i32.sub (local.get $ch) (i32.const 48))))\n");
    wasm_emitf(6, ") (else (if (i32.and (i32.ge_u (local.get $ch) (i32.const 97)) (i32.le_u (local.get $ch) (i32.const 122))) (then\n");
    wasm_emitf(8, "(local.set $digit (i64.extend_i32_u (i32.sub (local.get $ch) (i32.const 87))))\n");
    wasm_emitf(6, ") (else (if (i32.and (i32.ge_u (local.get $ch) (i32.const 65)) (i32.le_u (local.get $ch) (i32.const 90))) (then\n");
    wasm_emitf(8, "(local.set $digit (i64.extend_i32_u (i32.sub (local.get $ch) (i32.const 55))))\n");
    wasm_emitf(6, "))))))\n");
    wasm_emitf(6, "(if (i64.lt_s (local.get $digit) (i64.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(if (i64.ge_s (local.get $digit) (local.get $base)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $any_digit (i32.const 1))\n");
    wasm_emitf(6, "(local.set $n (i64.add (i64.mul (local.get $n) (local.get $base)) (local.get $digit)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $any_digit)) (then\n");
    wasm_emitf(6, "(if (local.get $endptr) (then (i64.store (local.get $endptr) (i64.extend_i32_u (local.get $s)))))\n");
    wasm_emitf(6, "(return (i64.const 0))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (local.get $endptr) (then (i64.store (local.get $endptr) (i64.extend_i32_u (local.get $p)))))\n");
    wasm_emitf(4, "(if (result i64) (local.get $neg) (then (i64.sub (i64.const 0) (local.get $n))) (else (local.get $n)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcstol", 6)) {
    wasm_emitf(2, "(func $wcstol (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(call $__ag_wcsto64 (local.get $s) (local.get $endptr) (local.get $base))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcstoul", 7)) {
    wasm_emitf(2, "(func $wcstoul (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(call $__ag_wcsto64 (local.get $s) (local.get $endptr) (local.get $base))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcstoll", 7)) {
    wasm_emitf(2, "(func $wcstoll (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(call $__ag_wcsto64 (local.get $s) (local.get $endptr) (local.get $base))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcstoull", 8)) {
    wasm_emitf(2, "(func $wcstoull (param $s i32) (param $endptr i32) (param $base i64) (result i64)\n");
    wasm_emitf(4, "(call $__ag_wcsto64 (local.get $s) (local.get $endptr) (local.get $base))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcstod", 6) || has_undefined_function("wcstof", 6) ||
      has_undefined_function("wcstold", 7)) {
    wasm_emitf(2, "(func $wcstod (param $s i32) (param $endptr i32) (result f64)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $sign f64)\n");
    wasm_emitf(4, "(local $acc f64)\n");
    wasm_emitf(4, "(local $place f64)\n");
    wasm_emitf(4, "(local $any_digit i32)\n");
    wasm_emitf(4, "(local.set $sign (f64.const 1.0))\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $space_done (loop $space_loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.or (i32.eq (local.get $ch) (i32.const 32)) (i32.or (i32.eq (local.get $ch) (i32.const 9)) (i32.or (i32.eq (local.get $ch) (i32.const 10)) (i32.or (i32.eq (local.get $ch) (i32.const 11)) (i32.or (i32.eq (local.get $ch) (i32.const 12)) (i32.eq (local.get $ch) (i32.const 13))))))) (then\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(8, "(br $space_loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(br $space_done)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.eq (i32.load (local.get $p)) (i32.const 45)) (then\n");
    wasm_emitf(6, "(local.set $sign (f64.const -1.0))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(4, ") (else (if (i32.eq (i32.load (local.get $p)) (i32.const 43)) (then\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(4, "))))\n");
    wasm_emitf(4, "(block $int_done (loop $int_loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.or (i32.lt_u (local.get $ch) (i32.const 48)) (i32.gt_u (local.get $ch) (i32.const 57))) (then (br $int_done)))\n");
    wasm_emitf(6, "(local.set $any_digit (i32.const 1))\n");
    wasm_emitf(6, "(local.set $acc (f64.add (f64.mul (local.get $acc) (f64.const 10.0)) (f64.convert_i32_u (i32.sub (local.get $ch) (i32.const 48)))))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $int_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.eq (i32.load (local.get $p)) (i32.const 46)) (then\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(local.set $place (f64.const 0.1))\n");
    wasm_emitf(6, "(block $frac_done (loop $frac_loop\n");
    wasm_emitf(8, "(local.set $ch (i32.load (local.get $p)))\n");
    wasm_emitf(8, "(if (i32.or (i32.lt_u (local.get $ch) (i32.const 48)) (i32.gt_u (local.get $ch) (i32.const 57))) (then (br $frac_done)))\n");
    wasm_emitf(8, "(local.set $any_digit (i32.const 1))\n");
    wasm_emitf(8, "(local.set $acc (f64.add (local.get $acc) (f64.mul (f64.convert_i32_u (i32.sub (local.get $ch) (i32.const 48))) (local.get $place))))\n");
    wasm_emitf(8, "(local.set $place (f64.div (local.get $place) (f64.const 10.0)))\n");
    wasm_emitf(8, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(8, "(br $frac_loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $any_digit)) (then\n");
    wasm_emitf(6, "(if (local.get $endptr) (then (i64.store (local.get $endptr) (i64.extend_i32_u (local.get $s)))))\n");
    wasm_emitf(6, "(return (f64.const 0))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (local.get $endptr) (then (i64.store (local.get $endptr) (i64.extend_i32_u (local.get $p)))))\n");
    wasm_emitf(4, "(f64.mul (local.get $acc) (local.get $sign))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcstof", 6)) {
    wasm_emitf(2, "(func $wcstof (param $s i32) (param $endptr i32) (result f32)\n");
    wasm_emitf(4, "(f32.demote_f64 (call $wcstod (local.get $s) (local.get $endptr)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcstold", 7)) {
    wasm_emitf(2, "(func $wcstold (param $s i32) (param $endptr i32) (result f64)\n");
    wasm_emitf(4, "(call $wcstod (local.get $s) (local.get $endptr))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("atexit", 6)) {
    wasm_emitf(2, "(func $atexit (param $func i32) (result i32)\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("at_quick_exit", 13)) {
    wasm_emitf(2, "(func $at_quick_exit (param $func i32) (result i32)\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("getenv", 6)) {
    wasm_emitf(2, "(func $getenv (param $name i32) (result i32)\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("system", 6)) {
    wasm_emitf(2, "(func $system (param $command i32) (result i32)\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("realpath", 8)) {
    wasm_emitf(2, "(func $realpath (param $path i32) (param $resolved i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $q i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $path)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $resolved)) (then (return (local.get $path))))\n");
    wasm_emitf(4, "(local.set $p (local.get $path))\n");
    wasm_emitf(4, "(local.set $q (local.get $resolved))\n");
    wasm_emitf(4, "(loop $copy_loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $q) (local.get $ch))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (return (local.get $resolved))))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $q (i32.add (local.get $q) (i32.const 1)))\n");
    wasm_emitf(6, "(br $copy_loop)\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(local.get $resolved)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("time", 4)) {
    wasm_emitf(2, "(func $time (param $tloc i32) (result i64)\n");
    wasm_emitf(4, "(if (local.get $tloc) (then (i64.store (local.get $tloc) (i64.const 0))))\n");
    wasm_emitf(4, "(i64.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("clock", 5)) {
    wasm_emitf(2, "(func $clock (result i64)\n");
    wasm_emitf(4, "(i64.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("difftime", 8)) {
    wasm_emitf(2, "(func $difftime (param $end i64) (param $beginning i64) (result f64)\n");
    wasm_emitf(4, "(f64.convert_i64_s (i64.sub (local.get $end) (local.get $beginning)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("mktime", 6) || has_undefined_function("gmtime", 6) ||
      has_undefined_function("localtime", 9) || has_undefined_function("ctime", 5) ||
      has_undefined_function("strftime", 8) || has_undefined_function("wcsftime", 8)) {
    emit_wasm_time_conversion_helpers();
  }
  if (has_undefined_function("mktime", 6)) {
    emit_wasm_mktime_stub();
  }
  if (has_undefined_function("asctime", 7) || has_undefined_function("ctime", 5)) {
    emit_wasm_asctime_helpers();
  }
  if (has_undefined_function("timespec_get", 12)) {
    wasm_emitf(2, "(func $timespec_get (param $ts i32) (param $base i64) (result i32)\n");
    wasm_emitf(4, "(if (i64.ne (local.get $base) (i64.const 1)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $ts)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(i64.store (local.get $ts) (i64.const 0))\n");
    wasm_emitf(4, "(i64.store (i32.add (local.get $ts) (i32.const 8)) (i64.const 0))\n");
    wasm_emitf(4, "(i32.const 1)\n");
    wasm_emitf(2, ")\n");
  }
  int needs_signal_handlers =
      has_undefined_function("signal", 6) || has_undefined_function("raise", 5);
  int signal_handlers_addr = -1;
  if (needs_signal_handlers) {
    signal_handlers_addr = intern_data_symbol("__ag_signal_handlers", 20, 32 * 4, 4)->addr;
  }
  if (has_undefined_function("signal", 6)) {
    wasm_emitf(2, "(func $signal (param $sig i64) (param $handler i32) (result i32)\n");
    wasm_emitf(4, "(local $slot i32)\n");
    wasm_emitf(4, "(local $old i32)\n");
    wasm_emitf(4, "(if (i32.or (i64.lt_s (local.get $sig) (i64.const 0)) (i64.ge_s (local.get $sig) (i64.const 32))) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(local.set $slot (i32.add (i32.const %d) (i32.shl (i32.wrap_i64 (local.get $sig)) (i32.const 2))))\n",
               signal_handlers_addr);
    wasm_emitf(4, "(local.set $old (i32.load (local.get $slot)))\n");
    wasm_emitf(4, "(i32.store (local.get $slot) (local.get $handler))\n");
    wasm_emitf(4, "(local.get $old)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("raise", 5)) {
    g_func_table.needs_table = 1;
    wasm_emitf(2, "(func $raise (param $sig i64) (result i32)\n");
    wasm_emitf(4, "(local $handler i32)\n");
    wasm_emitf(4, "(if (i32.or (i64.lt_s (local.get $sig) (i64.const 0)) (i64.ge_s (local.get $sig) (i64.const 32))) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(local.set $handler (i32.load (i32.add (i32.const %d) (i32.shl (i32.wrap_i64 (local.get $sig)) (i32.const 2)))))\n",
               signal_handlers_addr);
    wasm_emitf(4, "(if (i32.eq (local.get $handler) (i32.const 1)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(if (local.get $handler) (then\n");
    wasm_emitf(6, "(call_indirect (param i64) (local.get $sig) (local.get $handler))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("labs", 4)) {
    wasm_emitf(2, "(func $labs (param $x i64) (result i64)\n");
    wasm_emitf(4, "(if (result i64) (i64.lt_s (local.get $x) (i64.const 0))\n");
    wasm_emitf(6, "(then (i64.sub (i64.const 0) (local.get $x)))\n");
    wasm_emitf(6, "(else (local.get $x))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("llabs", 5)) {
    wasm_emitf(2, "(func $llabs (param $x i64) (result i64)\n");
    wasm_emitf(4, "(if (result i64) (i64.lt_s (local.get $x) (i64.const 0))\n");
    wasm_emitf(6, "(then (i64.sub (i64.const 0) (local.get $x)))\n");
    wasm_emitf(6, "(else (local.get $x))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("rand", 4) || has_undefined_function("srand", 5)) {
    wasm_emitf(2, "(global $__ag_rand_state (mut i64) (i64.const 1))\n");
  }
  if (has_undefined_function("srand", 5)) {
    wasm_emitf(2, "(func $srand (param $seed i64) (global.set $__ag_rand_state (local.get $seed)))\n");
  }
  if (has_undefined_function("rand", 4)) {
    wasm_emitf(2, "(func $rand (result i32)\n");
    wasm_emitf(4, "(global.set $__ag_rand_state (i64.add (i64.mul (global.get $__ag_rand_state) (i64.const 1103515245)) (i64.const 12345)))\n");
    wasm_emitf(4, "(i32.and (i32.wrap_i64 (i64.shr_u (global.get $__ag_rand_state) (i64.const 16))) (i32.const 32767))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("malloc", 6)) {
    wasm_emitf(2, "(func $malloc (param $size i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local.set $p (global.get $__ag_heap_pointer))\n");
    wasm_emitf(4, "(global.set $__ag_heap_pointer (i32.add (global.get $__ag_heap_pointer) (i32.and (i32.add (i32.wrap_i64 (local.get $size)) (i32.const 7)) (i32.const -8))))\n");
    wasm_emitf(4, "(local.get $p)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("calloc", 6)) {
    wasm_emitf(2, "(func $calloc (param $nmemb i64) (param $size i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $q i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $bytes i32)\n");
    wasm_emitf(4, "(local.set $bytes (i32.wrap_i64 (i64.mul (local.get $nmemb) (local.get $size))))\n");
    wasm_emitf(4, "(local.set $p (global.get $__ag_heap_pointer))\n");
    wasm_emitf(4, "(local.set $q (local.get $p))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $p) (local.get $bytes)))\n");
    wasm_emitf(4, "(global.set $__ag_heap_pointer (i32.add (local.get $p) (i32.and (i32.add (local.get $bytes) (i32.const 7)) (i32.const -8))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $q) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $q) (i32.const 0))\n");
    wasm_emitf(6, "(local.set $q (i32.add (local.get $q) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $p)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("aligned_alloc", 13)) {
    wasm_emitf(2, "(func $aligned_alloc (param $alignment i64) (param $size i64) (result i32)\n");
    wasm_emitf(4, "(local $align i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(if (i64.eqz (local.get $alignment)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(local.set $align (i32.wrap_i64 (local.get $alignment)))\n");
    wasm_emitf(4, "(local.set $p (i32.and (i32.add (global.get $__ag_heap_pointer) (i32.sub (local.get $align) (i32.const 1))) (i32.xor (i32.sub (local.get $align) (i32.const 1)) (i32.const -1))))\n");
    wasm_emitf(4, "(global.set $__ag_heap_pointer (i32.add (local.get $p) (i32.and (i32.add (i32.wrap_i64 (local.get $size)) (i32.const 7)) (i32.const -8))))\n");
    wasm_emitf(4, "(local.get $p)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("realloc", 7)) {
    wasm_emitf(2, "(func $realloc (param $ptr i32) (param $size i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $bytes i32)\n");
    wasm_emitf(4, "(if (i64.eqz (local.get $size)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(local.set $bytes (i32.wrap_i64 (local.get $size)))\n");
    wasm_emitf(4, "(local.set $p (global.get $__ag_heap_pointer))\n");
    wasm_emitf(4, "(global.set $__ag_heap_pointer (i32.add (local.get $p) (i32.and (i32.add (local.get $bytes) (i32.const 7)) (i32.const -8))))\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $ptr)) (then (return (local.get $p))))\n");
    wasm_emitf(4, "(local.set $d (local.get $p))\n");
    wasm_emitf(4, "(local.set $s (local.get $ptr))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $p) (local.get $bytes)))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $d) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $d) (i32.load8_u (local.get $s)))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $p)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("free", 4)) {
    wasm_emitf(2, "(func $free (param i32))\n");
  }
  if (has_undefined_function("qsort", 5)) {
    g_func_table.needs_table = 1;
    wasm_emitf(2, "(func $qsort (param $base i32) (param $nmemb i64) (param $size i64) (param $compar i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(local $j i64)\n");
    wasm_emitf(4, "(local $k i64)\n");
    wasm_emitf(4, "(local $a i32)\n");
    wasm_emitf(4, "(local $b i32)\n");
    wasm_emitf(4, "(local $tmp i32)\n");
    wasm_emitf(4, "(if (i32.or (i32.eqz (local.get $base)) (i32.or (i64.le_s (local.get $nmemb) (i64.const 1)) (i64.le_s (local.get $size) (i64.const 0)))) (then (return)))\n");
    wasm_emitf(4, "(block $done (loop $outer\n");
    wasm_emitf(6, "(if (i64.ge_s (local.get $i) (local.get $nmemb)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $j (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(block $inner_done (loop $inner\n");
    wasm_emitf(8, "(if (i64.ge_s (local.get $j) (local.get $nmemb)) (then (br $inner_done)))\n");
    wasm_emitf(8, "(local.set $a (i32.add (local.get $base) (i32.wrap_i64 (i64.mul (local.get $i) (local.get $size)))))\n");
    wasm_emitf(8, "(local.set $b (i32.add (local.get $base) (i32.wrap_i64 (i64.mul (local.get $j) (local.get $size)))))\n");
    wasm_emitf(8, "(if (i32.gt_s (call_indirect (param i32 i32) (result i32) (local.get $a) (local.get $b) (local.get $compar)) (i32.const 0))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(local.set $k (i64.const 0))\n");
    wasm_emitf(12, "(block $swap_done (loop $swap\n");
    wasm_emitf(14, "(if (i64.ge_s (local.get $k) (local.get $size)) (then (br $swap_done)))\n");
    wasm_emitf(14, "(local.set $tmp (i32.load8_u (i32.add (local.get $a) (i32.wrap_i64 (local.get $k)))))\n");
    wasm_emitf(14, "(i32.store8 (i32.add (local.get $a) (i32.wrap_i64 (local.get $k))) (i32.load8_u (i32.add (local.get $b) (i32.wrap_i64 (local.get $k)))))\n");
    wasm_emitf(14, "(i32.store8 (i32.add (local.get $b) (i32.wrap_i64 (local.get $k))) (local.get $tmp))\n");
    wasm_emitf(14, "(local.set $k (i64.add (local.get $k) (i64.const 1)))\n");
    wasm_emitf(14, "(br $swap)\n");
    wasm_emitf(12, "))\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(local.set $j (i64.add (local.get $j) (i64.const 1)))\n");
    wasm_emitf(8, "(br $inner)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $outer)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("bsearch", 7)) {
    g_func_table.needs_table = 1;
    wasm_emitf(2, "(func $bsearch (param $key i32) (param $base i32) (param $nmemb i64) (param $size i64) (param $compar i32) (result i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(local $elem i32)\n");
    wasm_emitf(4, "(if (i32.or (i32.or (i32.eqz (local.get $key)) (i32.eqz (local.get $base))) (i64.le_s (local.get $size) (i64.const 0))) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i64.ge_s (local.get $i) (local.get $nmemb)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $elem (i32.add (local.get $base) (i32.wrap_i64 (i64.mul (local.get $i) (local.get $size)))))\n");
    wasm_emitf(6, "(if (i32.eqz (call_indirect (param i32 i32) (result i32) (local.get $key) (local.get $elem) (local.get $compar))) (then (return (local.get $elem))))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("setlocale", 9)) {
    int c_addr = intern_data_symbol("__ag_stub_locale_c", 18, 2, 1)->addr;
    wasm_emitf(2, "(func $setlocale (param i64 i32) (result i32) (i32.const %d))\n", c_addr);
  }
  if (has_undefined_function("localeconv", 10)) {
    int lc_addr = intern_data_symbol("__ag_stub_lconv", 15, 96, 4)->addr;
    wasm_emitf(2, "(func $localeconv (result i32) (i32.const %d))\n", lc_addr);
  }
  if (has_undefined_function("gmtime", 6)) {
    int tm_addr = intern_data_symbol("__ag_stub_tm", 12, 36, 4)->addr;
    wasm_emitf(2, "(func $gmtime (param $timer i32) (result i32)\n");
    wasm_emitf(4, "(local $t i64)\n");
    wasm_emitf(4, "(if (local.get $timer) (then (local.set $t (i64.load (local.get $timer)))))\n");
    wasm_emitf(4, "(call $__ag_time_from_seconds (local.get $t) (i32.const %d))\n", tm_addr);
    wasm_emitf(4, "(i32.const %d)\n", tm_addr);
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("localtime", 9)) {
    int tm_addr = intern_data_symbol("__ag_stub_tm", 12, 36, 4)->addr;
    wasm_emitf(2, "(func $localtime (param $timer i32) (result i32)\n");
    wasm_emitf(4, "(local $t i64)\n");
    wasm_emitf(4, "(if (local.get $timer) (then (local.set $t (i64.load (local.get $timer)))))\n");
    wasm_emitf(4, "(call $__ag_time_from_seconds (local.get $t) (i32.const %d))\n", tm_addr);
    wasm_emitf(4, "(i32.const %d)\n", tm_addr);
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("asctime", 7)) {
    wasm_emitf(2, "(func $asctime (param $tm i32) (result i32)\n");
    wasm_emitf(4, "(call $__ag_asctime_impl (local.get $tm))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("ctime", 5)) {
    int tm_addr = intern_data_symbol("__ag_stub_tm", 12, 36, 4)->addr;
    wasm_emitf(2, "(func $ctime (param $timer i32) (result i32)\n");
    wasm_emitf(4, "(local $t i64)\n");
    wasm_emitf(4, "(if (local.get $timer) (then (local.set $t (i64.load (local.get $timer)))))\n");
    wasm_emitf(4, "(call $__ag_time_from_seconds (local.get $t) (i32.const %d))\n", tm_addr);
    wasm_emitf(4, "(call $__ag_asctime_impl (i32.const %d))\n", tm_addr);
    wasm_emitf(2, ")\n");
  }
  if (need_iswalnum_stub) {
    wasm_emitf(2, "(func $iswalnum (param $c i64) (result i32) (call $isalnum (local.get $c)))\n");
  }
  if (need_iswalpha_stub) {
    wasm_emitf(2, "(func $iswalpha (param $c i64) (result i32) (call $isalpha (local.get $c)))\n");
  }
  if (need_iswblank_stub) {
    wasm_emitf(2, "(func $iswblank (param $c i64) (result i32) (call $isblank (local.get $c)))\n");
  }
  if (need_iswcntrl_stub) {
    wasm_emitf(2, "(func $iswcntrl (param $c i64) (result i32) (call $iscntrl (local.get $c)))\n");
  }
  if (need_iswdigit_stub) {
    wasm_emitf(2, "(func $iswdigit (param $c i64) (result i32) (call $isdigit (local.get $c)))\n");
  }
  if (need_iswgraph_stub) {
    wasm_emitf(2, "(func $iswgraph (param $c i64) (result i32) (call $isgraph (local.get $c)))\n");
  }
  if (need_iswlower_stub) {
    wasm_emitf(2, "(func $iswlower (param $c i64) (result i32) (call $islower (local.get $c)))\n");
  }
  if (need_iswprint_stub) {
    wasm_emitf(2, "(func $iswprint (param $c i64) (result i32) (call $isprint (local.get $c)))\n");
  }
  if (need_iswpunct_stub) {
    wasm_emitf(2, "(func $iswpunct (param $c i64) (result i32) (call $ispunct (local.get $c)))\n");
  }
  if (need_iswspace_stub) {
    wasm_emitf(2, "(func $iswspace (param $c i64) (result i32) (call $isspace (local.get $c)))\n");
  }
  if (need_iswupper_stub) {
    wasm_emitf(2, "(func $iswupper (param $c i64) (result i32) (call $isupper (local.get $c)))\n");
  }
  if (need_iswxdigit_stub) {
    wasm_emitf(2, "(func $iswxdigit (param $c i64) (result i32) (call $isxdigit (local.get $c)))\n");
  }
  if (need_towlower_stub) {
    wasm_emitf(2, "(func $towlower (param $c i64) (result i32) (call $tolower (local.get $c)))\n");
  }
  if (need_towupper_stub) {
    wasm_emitf(2, "(func $towupper (param $c i64) (result i32) (call $toupper (local.get $c)))\n");
  }
  int need_wctype_lookup_stub = has_undefined_function("wctype", 6) ||
                                has_undefined_function("wctrans", 7);
  if (need_wctype_lookup_stub) {
    wasm_data_symbol_t *alnum = intern_data_symbol("__ag_wctype_alnum", 17, 6, 1);
    wasm_data_symbol_t *alpha = intern_data_symbol("__ag_wctype_alpha", 17, 6, 1);
    wasm_data_symbol_t *blank = intern_data_symbol("__ag_wctype_blank", 17, 6, 1);
    wasm_data_symbol_t *cntrl = intern_data_symbol("__ag_wctype_cntrl", 17, 6, 1);
    wasm_data_symbol_t *digit = intern_data_symbol("__ag_wctype_digit", 17, 6, 1);
    wasm_data_symbol_t *graph = intern_data_symbol("__ag_wctype_graph", 17, 6, 1);
    wasm_data_symbol_t *lower = intern_data_symbol("__ag_wctype_lower", 17, 6, 1);
    wasm_data_symbol_t *print = intern_data_symbol("__ag_wctype_print", 17, 6, 1);
    wasm_data_symbol_t *punct = intern_data_symbol("__ag_wctype_punct", 17, 6, 1);
    wasm_data_symbol_t *space = intern_data_symbol("__ag_wctype_space", 17, 6, 1);
    wasm_data_symbol_t *upper = intern_data_symbol("__ag_wctype_upper", 17, 6, 1);
    wasm_data_symbol_t *xdigit = intern_data_symbol("__ag_wctype_xdigit", 18, 7, 1);
    wasm_data_symbol_t *tolower_s = intern_data_symbol("__ag_wctrans_tolower", 20, 8, 1);
    wasm_data_symbol_t *toupper_s = intern_data_symbol("__ag_wctrans_toupper", 20, 8, 1);
    wasm_emitf(2, "(data (i32.const %d) \"alnum\\00\")\n", alnum->addr);
    wasm_emitf(2, "(data (i32.const %d) \"alpha\\00\")\n", alpha->addr);
    wasm_emitf(2, "(data (i32.const %d) \"blank\\00\")\n", blank->addr);
    wasm_emitf(2, "(data (i32.const %d) \"cntrl\\00\")\n", cntrl->addr);
    wasm_emitf(2, "(data (i32.const %d) \"digit\\00\")\n", digit->addr);
    wasm_emitf(2, "(data (i32.const %d) \"graph\\00\")\n", graph->addr);
    wasm_emitf(2, "(data (i32.const %d) \"lower\\00\")\n", lower->addr);
    wasm_emitf(2, "(data (i32.const %d) \"print\\00\")\n", print->addr);
    wasm_emitf(2, "(data (i32.const %d) \"punct\\00\")\n", punct->addr);
    wasm_emitf(2, "(data (i32.const %d) \"space\\00\")\n", space->addr);
    wasm_emitf(2, "(data (i32.const %d) \"upper\\00\")\n", upper->addr);
    wasm_emitf(2, "(data (i32.const %d) \"xdigit\\00\")\n", xdigit->addr);
    wasm_emitf(2, "(data (i32.const %d) \"tolower\\00\")\n", tolower_s->addr);
    wasm_emitf(2, "(data (i32.const %d) \"toupper\\00\")\n", toupper_s->addr);
    wasm_emitf(2, "(func $__ag_streq (param $a i32) (param $b i32) (result i32)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ca (i32.load8_u (local.get $a)))\n");
    wasm_emitf(6, "(local.set $cb (i32.load8_u (local.get $b)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.const 0))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ca)) (then (return (i32.const 1))))\n");
    wasm_emitf(6, "(local.set $a (i32.add (local.get $a) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $b (i32.add (local.get $b) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wctype", 6)) {
    wasm_data_symbol_t *alnum = intern_data_symbol("__ag_wctype_alnum", 17, 6, 1);
    wasm_data_symbol_t *alpha = intern_data_symbol("__ag_wctype_alpha", 17, 6, 1);
    wasm_data_symbol_t *blank = intern_data_symbol("__ag_wctype_blank", 17, 6, 1);
    wasm_data_symbol_t *cntrl = intern_data_symbol("__ag_wctype_cntrl", 17, 6, 1);
    wasm_data_symbol_t *digit = intern_data_symbol("__ag_wctype_digit", 17, 6, 1);
    wasm_data_symbol_t *graph = intern_data_symbol("__ag_wctype_graph", 17, 6, 1);
    wasm_data_symbol_t *lower = intern_data_symbol("__ag_wctype_lower", 17, 6, 1);
    wasm_data_symbol_t *print = intern_data_symbol("__ag_wctype_print", 17, 6, 1);
    wasm_data_symbol_t *punct = intern_data_symbol("__ag_wctype_punct", 17, 6, 1);
    wasm_data_symbol_t *space = intern_data_symbol("__ag_wctype_space", 17, 6, 1);
    wasm_data_symbol_t *upper = intern_data_symbol("__ag_wctype_upper", 17, 6, 1);
    wasm_data_symbol_t *xdigit = intern_data_symbol("__ag_wctype_xdigit", 18, 7, 1);
    wasm_emitf(2, "(func $wctype (param $p i32) (result i32)\n");
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 1))))\n", alnum->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 2))))\n", alpha->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 3))))\n", blank->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 4))))\n", cntrl->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 5))))\n", digit->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 6))))\n", graph->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 7))))\n", lower->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 8))))\n", print->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 9))))\n", punct->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 10))))\n", space->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 11))))\n", upper->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 12))))\n", xdigit->addr);
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (need_iswctype_stub) {
    wasm_emitf(2, "(func $iswctype (param $c i64) (param $desc i64) (result i32)\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 1)) (then (return (call $isalnum (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 2)) (then (return (call $isalpha (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 3)) (then (return (call $isblank (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 4)) (then (return (call $iscntrl (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 5)) (then (return (call $isdigit (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 6)) (then (return (call $isgraph (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 7)) (then (return (call $islower (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 8)) (then (return (call $isprint (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 9)) (then (return (call $ispunct (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 10)) (then (return (call $isspace (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 11)) (then (return (call $isupper (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 12)) (then (return (call $isxdigit (local.get $c)))))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wctrans", 7)) {
    wasm_data_symbol_t *tolower_s = intern_data_symbol("__ag_wctrans_tolower", 20, 8, 1);
    wasm_data_symbol_t *toupper_s = intern_data_symbol("__ag_wctrans_toupper", 20, 8, 1);
    wasm_emitf(2, "(func $wctrans (param $p i32) (result i32)\n");
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 1))))\n", tolower_s->addr);
    wasm_emitf(4, "(if (call $__ag_streq (local.get $p) (i32.const %d)) (then (return (i32.const 2))))\n", toupper_s->addr);
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("towctrans", 9)) {
    wasm_emitf(2, "(func $towctrans (param $c i64) (param $desc i64) (result i32)\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 1)) (then (return (call $tolower (local.get $c)))))\n");
    wasm_emitf(4, "(if (i64.eq (local.get $desc) (i64.const 2)) (then (return (call $toupper (local.get $c)))))\n");
    wasm_emitf(4, "(i32.wrap_i64 (local.get $c))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcslen", 6)) {
    wasm_emitf(2, "(func $wcslen (param $s i32) (result i64)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $n i64)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.eq (i32.load (local.get $p)) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $n (i64.add (local.get $n) (i64.const 1)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $n)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcscpy", 6)) {
    wasm_emitf(2, "(func $wcscpy (param $dst i32) (param $src i32) (result i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local.set $d (local.get $dst))\n");
    wasm_emitf(4, "(local.set $s (local.get $src))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $s)))\n");
    wasm_emitf(6, "(i32.store (local.get $d) (local.get $ch))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 4)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $s) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcsncpy", 7)) {
    wasm_emitf(2, "(func $wcsncpy (param $dst i32) (param $src i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $ended i32)\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i64.ge_u (local.get $i) (local.get $n)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $ch (i32.const 0))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ended)) (then\n");
    wasm_emitf(8, "(local.set $ch (i32.load (i32.add (local.get $src) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(i32.store (i32.add (local.get $dst) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4)))) (local.get $ch))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (local.set $ended (i32.const 1))))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcscat", 6)) {
    wasm_emitf(2, "(func $wcscat (param $dst i32) (param $src i32) (result i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local.set $end (local.get $dst))\n");
    wasm_emitf(4, "(block $end_done (loop $end_loop\n");
    wasm_emitf(6, "(if (i32.eqz (i32.load (local.get $end))) (then (br $end_done)))\n");
    wasm_emitf(6, "(local.set $end (i32.add (local.get $end) (i32.const 4)))\n");
    wasm_emitf(6, "(br $end_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $s (local.get $src))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $s)))\n");
    wasm_emitf(6, "(i32.store (local.get $end) (local.get $ch))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $end (i32.add (local.get $end) (i32.const 4)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $s) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcsncat", 7)) {
    wasm_emitf(2, "(func $wcsncat (param $dst i32) (param $src i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local.set $end (local.get $dst))\n");
    wasm_emitf(4, "(block $end_done (loop $end_loop\n");
    wasm_emitf(6, "(if (i32.eqz (i32.load (local.get $end))) (then (br $end_done)))\n");
    wasm_emitf(6, "(local.set $end (i32.add (local.get $end) (i32.const 4)))\n");
    wasm_emitf(6, "(br $end_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i64.ge_u (local.get $i) (local.get $n)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $ch (i32.load (i32.add (local.get $src) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(i32.store (i32.add (local.get $end) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4)))) (local.get $ch))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.store (i32.add (local.get $end) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4)))) (i32.const 0))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcscmp", 6)) {
    wasm_emitf(2, "(func $wcscmp (param $a i32) (param $b i32) (result i32)\n");
    wasm_emitf(4, "(local $pa i32)\n");
    wasm_emitf(4, "(local $pb i32)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(local.set $pa (local.get $a))\n");
    wasm_emitf(4, "(local.set $pb (local.get $b))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ca (i32.load (local.get $pa)))\n");
    wasm_emitf(6, "(local.set $cb (i32.load (local.get $pb)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ca) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $pa (i32.add (local.get $pa) (i32.const 4)))\n");
    wasm_emitf(6, "(local.set $pb (i32.add (local.get $pb) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcsncmp", 7)) {
    wasm_emitf(2, "(func $wcsncmp (param $a i32) (param $b i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i64.ge_u (local.get $i) (local.get $n)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $ca (i32.load (i32.add (local.get $a) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(6, "(local.set $cb (i32.load (i32.add (local.get $b) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ca)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcscoll", 7)) {
    wasm_emitf(2, "(func $wcscoll (param $a i32) (param $b i32) (result i32)\n");
    wasm_emitf(4, "(local $pa i32)\n");
    wasm_emitf(4, "(local $pb i32)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(local.set $pa (local.get $a))\n");
    wasm_emitf(4, "(local.set $pb (local.get $b))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ca (i32.load (local.get $pa)))\n");
    wasm_emitf(6, "(local.set $cb (i32.load (local.get $pb)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ca)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $pa (i32.add (local.get $pa) (i32.const 4)))\n");
    wasm_emitf(6, "(local.set $pb (i32.add (local.get $pb) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcsxfrm", 7)) {
    wasm_emitf(2, "(func $wcsxfrm (param $dst i32) (param $src i32) (param $n i64) (result i64)\n");
    wasm_emitf(4, "(local $len i64)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(block $len_done (loop $len_loop\n");
    wasm_emitf(6, "(if (i32.eqz (i32.load (i32.add (local.get $src) (i32.wrap_i64 (i64.mul (local.get $len) (i64.const 4)))))) (then (br $len_done)))\n");
    wasm_emitf(6, "(local.set $len (i64.add (local.get $len) (i64.const 1)))\n");
    wasm_emitf(6, "(br $len_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.and (i32.ne (local.get $dst) (i32.const 0)) (i64.ne (local.get $n) (i64.const 0))) (then\n");
    wasm_emitf(6, "(block $copy_done (loop $copy_loop\n");
    wasm_emitf(8, "(if (i64.ge_u (i64.add (local.get $i) (i64.const 1)) (local.get $n)) (then (br $copy_done)))\n");
    wasm_emitf(8, "(local.set $ch (i32.load (i32.add (local.get $src) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $ch)) (then (br $copy_done)))\n");
    wasm_emitf(8, "(i32.store (i32.add (local.get $dst) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4)))) (local.get $ch))\n");
    wasm_emitf(8, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(8, "(br $copy_loop)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(i32.store (i32.add (local.get $dst) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4)))) (i32.const 0))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $len)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcschr", 6)) {
    wasm_emitf(2, "(func $wcschr (param $s i32) (param $ch i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $not_found (loop $loop\n");
    wasm_emitf(6, "(local.set $c (i32.load (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.wrap_i64 (local.get $ch))) (then (return (local.get $p))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $c)) (then (br $not_found)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcsrchr", 7)) {
    wasm_emitf(2, "(func $wcsrchr (param $s i32) (param $ch i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $found i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $c (i32.load (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.wrap_i64 (local.get $ch))) (then (local.set $found (local.get $p))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $c)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $found)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcsstr", 6)) {
    wasm_emitf(2, "(func $wcsstr (param $s i32) (param $sub i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $h i32)\n");
    wasm_emitf(4, "(local $n i32)\n");
    wasm_emitf(4, "(local $chh i32)\n");
    wasm_emitf(4, "(local $chn i32)\n");
    wasm_emitf(4, "(if (i32.eqz (i32.load (local.get $sub))) (then (return (local.get $s))))\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $not_found (loop $outer\n");
    wasm_emitf(6, "(if (i32.eqz (i32.load (local.get $p))) (then (br $not_found)))\n");
    wasm_emitf(6, "(local.set $h (local.get $p))\n");
    wasm_emitf(6, "(local.set $n (local.get $sub))\n");
    wasm_emitf(6, "(block $next (loop $inner\n");
    wasm_emitf(8, "(local.set $chn (i32.load (local.get $n)))\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $chn)) (then (return (local.get $p))))\n");
    wasm_emitf(8, "(local.set $chh (i32.load (local.get $h)))\n");
    wasm_emitf(8, "(if (i32.ne (local.get $chh) (local.get $chn)) (then (br $next)))\n");
    wasm_emitf(8, "(local.set $h (i32.add (local.get $h) (i32.const 4)))\n");
    wasm_emitf(8, "(local.set $n (i32.add (local.get $n) (i32.const 4)))\n");
    wasm_emitf(8, "(br $inner)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $outer)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcsspn", 6)) {
    wasm_emitf(2, "(func $wcsspn (param $s i32) (param $accept i32) (result i64)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $q i32)\n");
    wasm_emitf(4, "(local $a i32)\n");
    wasm_emitf(4, "(local $found i32)\n");
    wasm_emitf(4, "(block $done (loop $outer\n");
    wasm_emitf(6, "(local.set $ch (i32.load (i32.add (local.get $s) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $found (i32.const 0))\n");
    wasm_emitf(6, "(local.set $q (local.get $accept))\n");
    wasm_emitf(6, "(block $scan_done (loop $scan\n");
    wasm_emitf(8, "(local.set $a (i32.load (local.get $q)))\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $a)) (then (br $scan_done)))\n");
    wasm_emitf(8, "(if (i32.eq (local.get $a) (local.get $ch)) (then (local.set $found (i32.const 1)) (br $scan_done)))\n");
    wasm_emitf(8, "(local.set $q (i32.add (local.get $q) (i32.const 4)))\n");
    wasm_emitf(8, "(br $scan)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $found)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $outer)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $i)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcscspn", 7)) {
    wasm_emitf(2, "(func $wcscspn (param $s i32) (param $reject i32) (result i64)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $q i32)\n");
    wasm_emitf(4, "(local $r i32)\n");
    wasm_emitf(4, "(local $found i32)\n");
    wasm_emitf(4, "(block $done (loop $outer\n");
    wasm_emitf(6, "(local.set $ch (i32.load (i32.add (local.get $s) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $found (i32.const 0))\n");
    wasm_emitf(6, "(local.set $q (local.get $reject))\n");
    wasm_emitf(6, "(block $scan_done (loop $scan\n");
    wasm_emitf(8, "(local.set $r (i32.load (local.get $q)))\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $r)) (then (br $scan_done)))\n");
    wasm_emitf(8, "(if (i32.eq (local.get $r) (local.get $ch)) (then (local.set $found (i32.const 1)) (br $scan_done)))\n");
    wasm_emitf(8, "(local.set $q (i32.add (local.get $q) (i32.const 4)))\n");
    wasm_emitf(8, "(br $scan)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (local.get $found) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $outer)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $i)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcspbrk", 7)) {
    wasm_emitf(2, "(func $wcspbrk (param $s i32) (param $accept i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $q i32)\n");
    wasm_emitf(4, "(local $a i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $not_found (loop $outer\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $not_found)))\n");
    wasm_emitf(6, "(local.set $q (local.get $accept))\n");
    wasm_emitf(6, "(block $next (loop $scan\n");
    wasm_emitf(8, "(local.set $a (i32.load (local.get $q)))\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $a)) (then (br $next)))\n");
    wasm_emitf(8, "(if (i32.eq (local.get $a) (local.get $ch)) (then (return (local.get $p))))\n");
    wasm_emitf(8, "(local.set $q (i32.add (local.get $q) (i32.const 4)))\n");
    wasm_emitf(8, "(br $scan)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $outer)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcstok", 6)) {
    wasm_emitf(2, "(func $wcstok (param $s i32) (param $delim i32) (param $saveptr i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $tok i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $q i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $found i32)\n");
    wasm_emitf(4, "(if (local.get $s) (then (local.set $p (local.get $s))) (else (local.set $p (i32.load (local.get $saveptr)))))\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $p)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(block $skip_done (loop $skip\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (i32.store (local.get $saveptr) (i32.const 0)) (return (i32.const 0))))\n");
    wasm_emitf(6, "(local.set $found (i32.const 0))\n");
    wasm_emitf(6, "(local.set $q (local.get $delim))\n");
    wasm_emitf(6, "(block $skip_scan_done (loop $skip_scan\n");
    wasm_emitf(8, "(local.set $d (i32.load (local.get $q)))\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $d)) (then (br $skip_scan_done)))\n");
    wasm_emitf(8, "(if (i32.eq (local.get $d) (local.get $ch)) (then (local.set $found (i32.const 1)) (br $skip_scan_done)))\n");
    wasm_emitf(8, "(local.set $q (i32.add (local.get $q) (i32.const 4)))\n");
    wasm_emitf(8, "(br $skip_scan)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $found)) (then (br $skip_done)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $skip)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $tok (local.get $p))\n");
    wasm_emitf(4, "(block $tok_done (loop $tok_loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (i32.store (local.get $saveptr) (i32.const 0)) (return (local.get $tok))))\n");
    wasm_emitf(6, "(local.set $found (i32.const 0))\n");
    wasm_emitf(6, "(local.set $q (local.get $delim))\n");
    wasm_emitf(6, "(block $tok_scan_done (loop $tok_scan\n");
    wasm_emitf(8, "(local.set $d (i32.load (local.get $q)))\n");
    wasm_emitf(8, "(if (i32.eqz (local.get $d)) (then (br $tok_scan_done)))\n");
    wasm_emitf(8, "(if (i32.eq (local.get $d) (local.get $ch)) (then (local.set $found (i32.const 1)) (br $tok_scan_done)))\n");
    wasm_emitf(8, "(local.set $q (i32.add (local.get $q) (i32.const 4)))\n");
    wasm_emitf(8, "(br $tok_scan)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (local.get $found) (then (br $tok_done)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $tok_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.store (local.get $p) (i32.const 0))\n");
    wasm_emitf(4, "(i32.store (local.get $saveptr) (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(4, "(local.get $tok)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wmemcpy", 7)) {
    wasm_emitf(2, "(func $wmemcpy (param $dst i32) (param $src i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i64.ge_u (local.get $i) (local.get $n)) (then (br $done)))\n");
    wasm_emitf(6, "(i32.store (i32.add (local.get $dst) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4)))) (i32.load (i32.add (local.get $src) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wmemmove", 8)) {
    wasm_emitf(2, "(func $wmemmove (param $dst i32) (param $src i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(if (i32.lt_u (local.get $dst) (local.get $src))\n");
    wasm_emitf(6, "(then\n");
    wasm_emitf(8, "(block $done (loop $loop\n");
    wasm_emitf(10, "(if (i64.ge_u (local.get $i) (local.get $n)) (then (br $done)))\n");
    wasm_emitf(10, "(i32.store (i32.add (local.get $dst) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4)))) (i32.load (i32.add (local.get $src) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(10, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(10, "(br $loop)\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(6, "(else\n");
    wasm_emitf(8, "(local.set $i (local.get $n))\n");
    wasm_emitf(8, "(block $done (loop $loop\n");
    wasm_emitf(10, "(if (i64.eqz (local.get $i)) (then (br $done)))\n");
    wasm_emitf(10, "(local.set $i (i64.sub (local.get $i) (i64.const 1)))\n");
    wasm_emitf(10, "(i32.store (i32.add (local.get $dst) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4)))) (i32.load (i32.add (local.get $src) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(10, "(br $loop)\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wmemset", 7)) {
    wasm_emitf(2, "(func $wmemset (param $s i32) (param $ch i64) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i64.ge_u (local.get $i) (local.get $n)) (then (br $done)))\n");
    wasm_emitf(6, "(i32.store (i32.add (local.get $s) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4)))) (i32.wrap_i64 (local.get $ch)))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $s)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wmemcmp", 7)) {
    wasm_emitf(2, "(func $wmemcmp (param $a i32) (param $b i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i64.ge_u (local.get $i) (local.get $n)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $ca (i32.load (i32.add (local.get $a) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(6, "(local.set $cb (i32.load (i32.add (local.get $b) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wmemchr", 7)) {
    wasm_emitf(2, "(func $wmemchr (param $s i32) (param $ch i64) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(block $not_found (loop $loop\n");
    wasm_emitf(6, "(if (i64.ge_u (local.get $i) (local.get $n)) (then (br $not_found)))\n");
    wasm_emitf(6, "(if (i32.eq (i32.load (i32.add (local.get $s) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))) (i32.wrap_i64 (local.get $ch)))\n");
    wasm_emitf(8, "(then (return (i32.add (local.get $s) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (!has_defined_function("mbrtowc", 7) &&
      (has_undefined_function("mbrtowc", 7) || has_undefined_function("mbrlen", 6) ||
       has_undefined_function("mblen", 5) || has_undefined_function("mbtowc", 6))) {
    wasm_emitf(2, "(func $mbrtowc (param $pwc i32) (param $s i32) (param $n i64) (param $ps i32) (result i64)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $s)) (then (return (i64.const 0))))\n");
    wasm_emitf(4, "(if (i64.eqz (local.get $n)) (then (return (i64.const -2))))\n");
    wasm_emitf(4, "(local.set $ch (i32.load8_u (local.get $s)))\n");
    wasm_emitf(4, "(if (local.get $pwc) (then (i32.store (local.get $pwc) (local.get $ch))))\n");
    wasm_emitf(4, "(if (result i64) (i32.eqz (local.get $ch)) (then (i64.const 0)) (else (i64.const 1)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("mbrlen", 6)) {
    wasm_emitf(2, "(func $mbrlen (param $s i32) (param $n i64) (param $ps i32) (result i64)\n");
    wasm_emitf(4, "(call $mbrtowc (i32.const 0) (local.get $s) (local.get $n) (local.get $ps))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("mblen", 5)) {
    wasm_emitf(2, "(func $mblen (param $s i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $r i64)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $s)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(local.set $r (call $mbrtowc (i32.const 0) (local.get $s) (local.get $n) (i32.const 0)))\n");
    wasm_emitf(4, "(if (result i32) (i64.lt_s (local.get $r) (i64.const 0)) (then (i32.const -1)) (else (i32.wrap_i64 (local.get $r))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("mbtowc", 6)) {
    wasm_emitf(2, "(func $mbtowc (param $pwc i32) (param $s i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $r i64)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $s)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(local.set $r (call $mbrtowc (local.get $pwc) (local.get $s) (local.get $n) (i32.const 0)))\n");
    wasm_emitf(4, "(if (result i32) (i64.lt_s (local.get $r) (i64.const 0)) (then (i32.const -1)) (else (i32.wrap_i64 (local.get $r))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("mbsinit", 7)) {
    wasm_emitf(2, "(func $mbsinit (param $ps i32) (result i32)\n");
    wasm_emitf(4, "(i32.const 1)\n");
    wasm_emitf(2, ")\n");
  }
  if (!has_defined_function("wcrtomb", 7) &&
      (has_undefined_function("wcrtomb", 7) || has_undefined_function("wctomb", 6))) {
    wasm_emitf(2, "(func $wcrtomb (param $s i32) (param $wc i64) (param $ps i32) (result i64)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $s)) (then (return (i64.const 1))))\n");
    wasm_emitf(4, "(i32.store8 (local.get $s) (i32.wrap_i64 (local.get $wc)))\n");
    wasm_emitf(4, "(i64.const 1)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wctomb", 6)) {
    wasm_emitf(2, "(func $wctomb (param $s i32) (param $wc i64) (result i32)\n");
    wasm_emitf(4, "(local $r i64)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $s)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(local.set $r (call $wcrtomb (local.get $s) (local.get $wc) (i32.const 0)))\n");
    wasm_emitf(4, "(if (result i32) (i64.lt_s (local.get $r) (i64.const 0)) (then (i32.const -1)) (else (i32.wrap_i64 (local.get $r))))\n");
    wasm_emitf(2, ")\n");
  }
  if (!has_defined_function("mbsrtowcs", 9) &&
      (has_undefined_function("mbsrtowcs", 9) || has_undefined_function("mbstowcs", 8))) {
    wasm_emitf(2, "(func $mbsrtowcs (param $dst i32) (param $srcp i32) (param $len i64) (param $ps i32) (result i64)\n");
    wasm_emitf(4, "(local $src i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local.set $src (i32.wrap_i64 (i64.load (local.get $srcp))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i64.ge_u (local.get $i) (local.get $len)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (i32.add (local.get $src) (i32.wrap_i64 (local.get $i)))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(if (local.get $dst) (then (i32.store (i32.add (local.get $dst) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4)))) (local.get $ch))))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.and (i64.lt_u (local.get $i) (local.get $len)) (i32.eqz (i32.load8_u (i32.add (local.get $src) (i32.wrap_i64 (local.get $i)))))) (then\n");
    wasm_emitf(6, "(if (local.get $dst) (then (i32.store (i32.add (local.get $dst) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4)))) (i32.const 0))))\n");
    wasm_emitf(6, "(i64.store (local.get $srcp) (i64.const 0))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $i)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("mbstowcs", 8)) {
    int srcp_addr = intern_data_symbol("__ag_mbstowcs_srcp", 18, 8, 8)->addr;
    wasm_emitf(2, "(func $mbstowcs (param $dst i32) (param $src i32) (param $n i64) (result i64)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $src)) (then (return (i64.const -1))))\n");
    wasm_emitf(4, "(i64.store (i32.const %d) (i64.extend_i32_u (local.get $src)))\n", srcp_addr);
    wasm_emitf(4, "(call $mbsrtowcs (local.get $dst) (i32.const %d) (local.get $n) (i32.const 0))\n", srcp_addr);
    wasm_emitf(2, ")\n");
  }
  if (!has_defined_function("wcsrtombs", 9) &&
      (has_undefined_function("wcsrtombs", 9) || has_undefined_function("wcstombs", 8))) {
    wasm_emitf(2, "(func $wcsrtombs (param $dst i32) (param $srcp i32) (param $len i64) (param $ps i32) (result i64)\n");
    wasm_emitf(4, "(local $src i32)\n");
    wasm_emitf(4, "(local $i i64)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local.set $src (i32.wrap_i64 (i64.load (local.get $srcp))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i64.ge_u (local.get $i) (local.get $len)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $ch (i32.load (i32.add (local.get $src) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))\n");
    wasm_emitf(6, "(if (i32.eqz (local.get $ch)) (then (br $done)))\n");
    wasm_emitf(6, "(if (local.get $dst) (then (i32.store8 (i32.add (local.get $dst) (i32.wrap_i64 (local.get $i))) (local.get $ch))))\n");
    wasm_emitf(6, "(local.set $i (i64.add (local.get $i) (i64.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.and (i64.lt_u (local.get $i) (local.get $len)) (i32.eqz (i32.load (i32.add (local.get $src) (i32.wrap_i64 (i64.mul (local.get $i) (i64.const 4))))))) (then\n");
    wasm_emitf(6, "(if (local.get $dst) (then (i32.store8 (i32.add (local.get $dst) (i32.wrap_i64 (local.get $i))) (i32.const 0))))\n");
    wasm_emitf(6, "(i64.store (local.get $srcp) (i64.const 0))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $i)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcstombs", 8)) {
    int srcp_addr = intern_data_symbol("__ag_wcstombs_srcp", 18, 8, 8)->addr;
    wasm_emitf(2, "(func $wcstombs (param $dst i32) (param $src i32) (param $n i64) (result i64)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $src)) (then (return (i64.const -1))))\n");
    wasm_emitf(4, "(i64.store (i32.const %d) (i64.extend_i32_u (local.get $src)))\n", srcp_addr);
    wasm_emitf(4, "(call $wcsrtombs (local.get $dst) (i32.const %d) (local.get $n) (i32.const 0))\n", srcp_addr);
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("btowc", 5)) {
    wasm_emitf(2, "(func $btowc (param $c i64) (result i32)\n");
    wasm_emitf(4, "(if (result i32) (i64.eq (local.get $c) (i64.const -1)) (then (i32.const -1)) (else (i32.and (i32.wrap_i64 (local.get $c)) (i32.const 255))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wctob", 5)) {
    wasm_emitf(2, "(func $wctob (param $c i64) (result i32)\n");
    wasm_emitf(4, "(if (result i32) (i32.and (i64.ge_s (local.get $c) (i64.const 0)) (i64.le_s (local.get $c) (i64.const 255))) (then (i32.wrap_i64 (local.get $c))) (else (i32.const -1)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fgetwc", 6)) {
    wasm_emitf(2, "(func $fgetwc (param i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("getwc", 5)) {
    wasm_emitf(2, "(func $getwc (param i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("getwchar", 8)) {
    wasm_emitf(2, "(func $getwchar (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("ungetwc", 7)) {
    wasm_emitf(2, "(func $ungetwc (param i64 i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("fgetws", 6)) {
    wasm_emitf(2, "(func $fgetws (param i32 i64 i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("fputwc", 6)) {
    wasm_emitf(2, "(func $fputwc (param $wc i64) (param $stream i32) (result i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $stream)) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(i32.wrap_i64 (local.get $wc))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("putwc", 5)) {
    wasm_emitf(2, "(func $putwc (param $wc i64) (param $stream i32) (result i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $stream)) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(i32.wrap_i64 (local.get $wc))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("putwchar", 8)) {
    wasm_emitf(2, "(func $putwchar (param $wc i64) (result i32)\n");
    wasm_emitf(4, "(i32.wrap_i64 (local.get $wc))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fputws", 6)) {
    wasm_emitf(2, "(func $fputws (param $s i32) (param $stream i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $count i32)\n");
    wasm_emitf(4, "(if (i32.or (i32.eqz (local.get $s)) (i32.eqz (local.get $stream))) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.eqz (i32.load (local.get $p))) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $count (i32.add (local.get $count) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $count)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fwide", 5)) {
    wasm_emitf(2, "(func $fwide (param $stream i32) (param $mode i64) (result i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $stream)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(if (i64.gt_s (local.get $mode) (i64.const 0)) (then (return (i32.const 1))))\n");
    wasm_emitf(4, "(if (i64.lt_s (local.get $mode) (i64.const 0)) (then (return (i32.const -1))))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("mbrtoc16", 8)) {
    wasm_emitf(2, "(func $mbrtoc16 (param $pc16 i32) (param $s i32) (param $n i64) (param $ps i32) (result i64)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $s)) (then (return (i64.const 0))))\n");
    wasm_emitf(4, "(if (i64.eqz (local.get $n)) (then (return (i64.const -2))))\n");
    wasm_emitf(4, "(local.set $ch (i32.load8_u (local.get $s)))\n");
    wasm_emitf(4, "(if (local.get $pc16) (then (i32.store16 (local.get $pc16) (local.get $ch))))\n");
    wasm_emitf(4, "(if (result i64) (i32.eqz (local.get $ch)) (then (i64.const 0)) (else (i64.const 1)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("c16rtomb", 8)) {
    wasm_emitf(2, "(func $c16rtomb (param $s i32) (param $c16 i64) (param $ps i32) (result i64)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $s)) (then (return (i64.const 1))))\n");
    wasm_emitf(4, "(i32.store8 (local.get $s) (i32.wrap_i64 (local.get $c16)))\n");
    wasm_emitf(4, "(i64.const 1)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("mbrtoc32", 8)) {
    wasm_emitf(2, "(func $mbrtoc32 (param $pc32 i32) (param $s i32) (param $n i64) (param $ps i32) (result i64)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $s)) (then (return (i64.const 0))))\n");
    wasm_emitf(4, "(if (i64.eqz (local.get $n)) (then (return (i64.const -2))))\n");
    wasm_emitf(4, "(local.set $ch (i32.load8_u (local.get $s)))\n");
    wasm_emitf(4, "(if (local.get $pc32) (then (i32.store (local.get $pc32) (local.get $ch))))\n");
    wasm_emitf(4, "(if (result i64) (i32.eqz (local.get $ch)) (then (i64.const 0)) (else (i64.const 1)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("c32rtomb", 8)) {
    wasm_emitf(2, "(func $c32rtomb (param $s i32) (param $c32 i64) (param $ps i32) (result i64)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $s)) (then (return (i64.const 1))))\n");
    wasm_emitf(4, "(i32.store8 (local.get $s) (i32.wrap_i64 (local.get $c32)))\n");
    wasm_emitf(4, "(i64.const 1)\n");
    wasm_emitf(2, ")\n");
  }
  int atan_defined = psx_ctx_has_function_name("atan", 4) &&
                     psx_ctx_is_function_defined("atan", 4);
  int atan2_defined = psx_ctx_has_function_name("atan2", 5) &&
                      psx_ctx_is_function_defined("atan2", 5);
  int need_atan2_stub = has_undefined_function("atan2", 5) ||
                        has_undefined_function("atan2f", 6) ||
                        has_undefined_function("atan2l", 6) ||
                        ((has_undefined_function("asin", 4) ||
                          has_undefined_function("asinf", 5) ||
                          has_undefined_function("asinl", 5) ||
                          has_undefined_function("acosf", 5) ||
                          has_undefined_function("acosl", 5) ||
                          has_undefined_function("acos", 4)) &&
                         !atan2_defined);
  int need_atan_stub = has_undefined_function("atan", 4) ||
                       has_undefined_function("atanf", 5) ||
                       has_undefined_function("atanl", 5) ||
                       (need_atan2_stub && !atan_defined);
  int need_atan = need_atan_stub || need_atan2_stub;
  int need_asinh_stub = has_undefined_function("asinh", 5) ||
                        has_undefined_function("asinhf", 6) ||
                        has_undefined_function("asinhl", 6);
  int need_acosh_stub = has_undefined_function("acosh", 5) ||
                        has_undefined_function("acoshf", 6) ||
                        has_undefined_function("acoshl", 6);
  int need_atanh_stub = has_undefined_function("atanh", 5) ||
                        has_undefined_function("atanhf", 6) ||
                        has_undefined_function("atanhl", 6);
  int need_sinh_stub = has_undefined_function("sinh", 4) ||
                       has_undefined_function("sinhf", 5) ||
                       has_undefined_function("sinhl", 5);
  int need_cosh_stub = has_undefined_function("cosh", 4) ||
                       has_undefined_function("coshf", 5) ||
                       has_undefined_function("coshl", 5);
  int need_tanh_stub = has_undefined_function("tanh", 4) ||
                       has_undefined_function("tanhf", 5) ||
                       has_undefined_function("tanhl", 5);
  int need_exp2_stub = has_undefined_function("exp2", 4) ||
                       has_undefined_function("exp2f", 5) ||
                       has_undefined_function("exp2l", 5);
  int need_expm1_stub = has_undefined_function("expm1", 5) ||
                        has_undefined_function("expm1f", 6) ||
                        has_undefined_function("expm1l", 6);
  int need_pow_stub = has_undefined_function("pow", 3) ||
                      has_undefined_function("powf", 4) ||
                      has_undefined_function("powl", 4);
  int need_cbrt_stub = has_undefined_function("cbrt", 4) ||
                       has_undefined_function("cbrtf", 5) ||
                       has_undefined_function("cbrtl", 5);
  int need_erf_family_stub = has_undefined_function("erf", 3) ||
                             has_undefined_function("erff", 4) ||
                             has_undefined_function("erfl", 4) ||
                             has_undefined_function("erfc", 4) ||
                             has_undefined_function("erfcf", 5) ||
                             has_undefined_function("erfcl", 5);
  if (need_atan) {
    wasm_emitf(2, "(func $__ag_atan_core (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $x2 f64)\n");
    wasm_emitf(4, "(local $term f64)\n");
    wasm_emitf(4, "(local $sum f64)\n");
    wasm_emitf(4, "(local $den f64)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(local $neg i32)\n");
    wasm_emitf(4, "(local.set $x2 (f64.mul (local.get $x) (local.get $x)))\n");
    wasm_emitf(4, "(local.set $term (local.get $x))\n");
    wasm_emitf(4, "(local.set $sum (local.get $x))\n");
    wasm_emitf(4, "(local.set $den (f64.const 3))\n");
    wasm_emitf(4, "(local.set $neg (i32.const 1))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.const 80)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $term (f64.mul (local.get $term) (local.get $x2)))\n");
    wasm_emitf(6, "(if (local.get $neg)\n");
    wasm_emitf(8, "(then (local.set $sum (f64.sub (local.get $sum) (f64.div (local.get $term) (local.get $den)))))\n");
    wasm_emitf(8, "(else (local.set $sum (f64.add (local.get $sum) (f64.div (local.get $term) (local.get $den)))))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(6, "(local.set $neg (i32.xor (local.get $neg) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $den (f64.add (local.get $den) (f64.const 2)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $sum)\n");
    wasm_emitf(2, ")\n");
  }
  if (need_atan_stub) {
    wasm_emitf(2, "(func $atan (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (result f64) (f64.gt (local.get $x) (f64.const 1))\n");
    wasm_emitf(6, "(then (f64.sub (f64.const 1.5707963267948966) (call $__ag_atan_core (f64.div (f64.const 1) (local.get $x)))))\n");
    wasm_emitf(6, "(else (if (result f64) (f64.lt (local.get $x) (f64.const -1))\n");
    wasm_emitf(8, "(then (f64.sub (f64.const -1.5707963267948966) (call $__ag_atan_core (f64.div (f64.const 1) (local.get $x)))))\n");
    wasm_emitf(8, "(else (if (result f64) (f64.gt (local.get $x) (f64.const 0.5))\n");
    wasm_emitf(10, "(then (f64.add (f64.const 0.7853981633974483) (call $__ag_atan_core (f64.div (f64.sub (local.get $x) (f64.const 1)) (f64.add (local.get $x) (f64.const 1))))))\n");
    wasm_emitf(10, "(else (if (result f64) (f64.lt (local.get $x) (f64.const -0.5))\n");
    wasm_emitf(12, "(then (f64.sub (f64.const -0.7853981633974483) (call $__ag_atan_core (f64.div (f64.add (local.get $x) (f64.const 1)) (f64.sub (f64.const 1) (local.get $x))))))\n");
    wasm_emitf(12, "(else (call $__ag_atan_core (local.get $x)))\n");
    wasm_emitf(10, "))\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (need_atan2_stub) {
    wasm_emitf(2, "(func $atan2 (param $y f64) (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $y) (local.get $y)) (then (return (local.get $y))))\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (i32.and (f64.eq (local.get $x) (f64.const 0)) (f64.eq (local.get $y) (f64.const 0))) (then\n");
    wasm_emitf(6, "(if (f64.lt (f64.div (f64.const 1) (local.get $x)) (f64.const 0))\n");
    wasm_emitf(8, "(then (return (if (result f64) (f64.lt (f64.div (f64.const 1) (local.get $y)) (f64.const 0)) (then (f64.const -3.141592653589793)) (else (f64.const 3.141592653589793)))))\n");
    wasm_emitf(8, "(else (return (local.get $y)))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.and (f64.eq (f64.abs (local.get $x)) (f64.const inf)) (f64.eq (f64.abs (local.get $y)) (f64.const inf))) (then\n");
    wasm_emitf(6, "(if (f64.gt (local.get $x) (f64.const 0))\n");
    wasm_emitf(8, "(then (return (if (result f64) (f64.lt (local.get $y) (f64.const 0)) (then (f64.const -0.7853981633974483)) (else (f64.const 0.7853981633974483)))))\n");
    wasm_emitf(8, "(else (return (if (result f64) (f64.lt (local.get $y) (f64.const 0)) (then (f64.const -2.356194490192345)) (else (f64.const 2.356194490192345)))))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (f64.eq (f64.abs (local.get $y)) (f64.const inf)) (then (return (if (result f64) (f64.lt (local.get $y) (f64.const 0)) (then (f64.const -1.5707963267948966)) (else (f64.const 1.5707963267948966))))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const inf)) (then (return (if (result f64) (f64.lt (local.get $y) (f64.const 0)) (then (f64.const -0)) (else (f64.const 0))))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const -inf)) (then (return (if (result f64) (f64.lt (local.get $y) (f64.const 0)) (then (f64.const -3.141592653589793)) (else (f64.const 3.141592653589793))))))\n");
    wasm_emitf(4, "(if (result f64) (f64.gt (local.get $x) (f64.const 0))\n");
    wasm_emitf(6, "(then (call $atan (f64.div (local.get $y) (local.get $x))))\n");
    wasm_emitf(6, "(else (if (result f64) (f64.lt (local.get $x) (f64.const 0))\n");
    wasm_emitf(8, "(then (if (result f64) (f64.ge (local.get $y) (f64.const 0))\n");
    wasm_emitf(10, "(then (f64.add (call $atan (f64.div (local.get $y) (local.get $x))) (f64.const 3.141592653589793)))\n");
    wasm_emitf(10, "(else (f64.sub (call $atan (f64.div (local.get $y) (local.get $x))) (f64.const 3.141592653589793)))\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(8, "(else (if (result f64) (f64.gt (local.get $y) (f64.const 0))\n");
    wasm_emitf(10, "(then (f64.const 1.5707963267948966))\n");
    wasm_emitf(10, "(else (if (result f64) (f64.lt (local.get $y) (f64.const 0)) (then (f64.const -1.5707963267948966)) (else (f64.const 0))))\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  int exp_defined = psx_ctx_has_function_name("exp", 3) &&
                    psx_ctx_is_function_defined("exp", 3);
  int need_exp_stub = has_undefined_function("exp", 3) ||
                      has_undefined_function("expf", 4) ||
                      has_undefined_function("expl", 4) ||
                      need_exp2_stub || need_expm1_stub || need_pow_stub ||
                      need_cbrt_stub || need_erf_family_stub ||
                      ((need_sinh_stub || need_cosh_stub || need_tanh_stub) && !exp_defined);
  if (need_exp_stub) {
    wasm_emitf(2, "(func $exp (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $r f64)\n");
    wasm_emitf(4, "(local $term f64)\n");
    wasm_emitf(4, "(local $sum f64)\n");
    wasm_emitf(4, "(local $n f64)\n");
    wasm_emitf(4, "(local $k i32)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.gt (local.get $x) (f64.const 709.782712893384)) (then (return (f64.const inf))))\n");
    wasm_emitf(4, "(if (f64.lt (local.get $x) (f64.const -745.1332191019411)) (then (return (f64.const 0))))\n");
    wasm_emitf(4, "(local.set $r (local.get $x))\n");
    wasm_emitf(4, "(block $done_pos (loop $pos\n");
    wasm_emitf(6, "(if (f64.le (local.get $r) (f64.const 0.5)) (then (br $done_pos)))\n");
    wasm_emitf(6, "(local.set $r (f64.sub (local.get $r) (f64.const 0.6931471805599453)))\n");
    wasm_emitf(6, "(local.set $k (i32.add (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $pos)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_neg (loop $neg\n");
    wasm_emitf(6, "(if (f64.ge (local.get $r) (f64.const -0.5)) (then (br $done_neg)))\n");
    wasm_emitf(6, "(local.set $r (f64.add (local.get $r) (f64.const 0.6931471805599453)))\n");
    wasm_emitf(6, "(local.set $k (i32.sub (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $neg)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $term (f64.const 1))\n");
    wasm_emitf(4, "(local.set $sum (f64.const 1))\n");
    wasm_emitf(4, "(local.set $n (f64.const 1))\n");
    wasm_emitf(4, "(block $done_series (loop $series\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.const 28)) (then (br $done_series)))\n");
    wasm_emitf(6, "(local.set $term (f64.div (f64.mul (local.get $term) (local.get $r)) (local.get $n)))\n");
    wasm_emitf(6, "(local.set $sum (f64.add (local.get $sum) (local.get $term)))\n");
    wasm_emitf(6, "(local.set $n (f64.add (local.get $n) (f64.const 1)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $series)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_scale_pos (loop $scale_pos\n");
    wasm_emitf(6, "(if (i32.le_s (local.get $k) (i32.const 0)) (then (br $done_scale_pos)))\n");
    wasm_emitf(6, "(local.set $sum (f64.mul (local.get $sum) (f64.const 2)))\n");
    wasm_emitf(6, "(local.set $k (i32.sub (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $scale_pos)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_scale_neg (loop $scale_neg\n");
    wasm_emitf(6, "(if (i32.ge_s (local.get $k) (i32.const 0)) (then (br $done_scale_neg)))\n");
    wasm_emitf(6, "(local.set $sum (f64.mul (local.get $sum) (f64.const 0.5)))\n");
    wasm_emitf(6, "(local.set $k (i32.add (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $scale_neg)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $sum)\n");
    wasm_emitf(2, ")\n");
  }
  int log_defined = psx_ctx_has_function_name("log", 3) &&
                    psx_ctx_is_function_defined("log", 3);
  int need_log_stub = has_undefined_function("log", 3) ||
                      has_undefined_function("logf", 4) ||
                      has_undefined_function("logl", 4) ||
                      need_asinh_stub || need_acosh_stub || need_atanh_stub ||
                      need_pow_stub || need_cbrt_stub ||
                      has_undefined_function("log1p", 5) ||
                      has_undefined_function("log1pf", 6) ||
                      has_undefined_function("log1pl", 6) ||
                      ((has_undefined_function("log2", 4) ||
                        has_undefined_function("log2f", 5) ||
                        has_undefined_function("log2l", 5) ||
                        has_undefined_function("log10f", 6) ||
                        has_undefined_function("log10l", 6) ||
                        has_undefined_function("log10", 5)) &&
                       !log_defined);
  if (need_log_stub) {
    wasm_emitf(2, "(func $log (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $z f64)\n");
    wasm_emitf(4, "(local $z2 f64)\n");
    wasm_emitf(4, "(local $term f64)\n");
    wasm_emitf(4, "(local $sum f64)\n");
    wasm_emitf(4, "(local $den f64)\n");
    wasm_emitf(4, "(local $k i32)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const inf)) (then (return (f64.const inf))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (f64.const -inf))))\n");
    wasm_emitf(4, "(if (f64.lt (local.get $x) (f64.const 0)) (then (return (f64.div (f64.const 0) (f64.const 0)))))\n");
    wasm_emitf(4, "(block $done_hi (loop $hi\n");
    wasm_emitf(6, "(if (f64.le (local.get $x) (f64.const 2)) (then (br $done_hi)))\n");
    wasm_emitf(6, "(local.set $x (f64.mul (local.get $x) (f64.const 0.5)))\n");
    wasm_emitf(6, "(local.set $k (i32.add (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $hi)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_lo (loop $lo\n");
    wasm_emitf(6, "(if (f64.ge (local.get $x) (f64.const 0.5)) (then (br $done_lo)))\n");
    wasm_emitf(6, "(local.set $x (f64.mul (local.get $x) (f64.const 2)))\n");
    wasm_emitf(6, "(local.set $k (i32.sub (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $lo)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $z (f64.div (f64.sub (local.get $x) (f64.const 1)) (f64.add (local.get $x) (f64.const 1))))\n");
    wasm_emitf(4, "(local.set $z2 (f64.mul (local.get $z) (local.get $z)))\n");
    wasm_emitf(4, "(local.set $term (local.get $z))\n");
    wasm_emitf(4, "(local.set $sum (local.get $z))\n");
    wasm_emitf(4, "(local.set $den (f64.const 3))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.const 80)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $term (f64.mul (local.get $term) (local.get $z2)))\n");
    wasm_emitf(6, "(local.set $sum (f64.add (local.get $sum) (f64.div (local.get $term) (local.get $den))))\n");
    wasm_emitf(6, "(local.set $den (f64.add (local.get $den) (f64.const 2)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(f64.add (f64.mul (f64.const 2) (local.get $sum)) (f64.mul (f64.convert_i32_s (local.get $k)) (f64.const 0.6931471805599453)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("log2", 4) || has_undefined_function("log2f", 5) ||
      has_undefined_function("log2l", 5)) {
    wasm_emitf(2, "(func $log2 (param $x f64) (result f64)\n");
    wasm_emitf(4, "(f64.div (call $log (local.get $x)) (f64.const 0.6931471805599453))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("log2f", 5)) {
    wasm_emitf(2, "(func $log2f (param $x f32) (result f32) (f32.demote_f64 (call $log2 (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("log2l", 5)) {
    wasm_emitf(2, "(func $log2l (param $x f64) (result f64) (call $log2 (local.get $x)))\n");
  }
  if (has_undefined_function("log10", 5) || has_undefined_function("log10f", 6) ||
      has_undefined_function("log10l", 6)) {
    wasm_emitf(2, "(func $log10 (param $x f64) (result f64)\n");
    wasm_emitf(4, "(f64.div (call $log (local.get $x)) (f64.const 2.302585092994046))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("log10f", 6)) {
    wasm_emitf(2, "(func $log10f (param $x f32) (result f32) (f32.demote_f64 (call $log10 (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("log10l", 6)) {
    wasm_emitf(2, "(func $log10l (param $x f64) (result f64) (call $log10 (local.get $x)))\n");
  }
  if (need_exp2_stub) {
    wasm_emitf(2, "(func $exp2 (param $x f64) (result f64)\n");
    wasm_emitf(4, "(call $exp (f64.mul (local.get $x) (f64.const 0.6931471805599453)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("exp2f", 5)) {
    wasm_emitf(2, "(func $exp2f (param $x f32) (result f32) (f32.demote_f64 (call $exp2 (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("exp2l", 5)) {
    wasm_emitf(2, "(func $exp2l (param $x f64) (result f64) (call $exp2 (local.get $x)))\n");
  }
  if (need_expm1_stub) {
    wasm_emitf(2, "(func $expm1 (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.lt (f64.abs (local.get $x)) (f64.const 1.0e-8)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(f64.sub (call $exp (local.get $x)) (f64.const 1))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("expm1f", 6)) {
    wasm_emitf(2, "(func $expm1f (param $x f32) (result f32) (f32.demote_f64 (call $expm1 (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("expm1l", 6)) {
    wasm_emitf(2, "(func $expm1l (param $x f64) (result f64) (call $expm1 (local.get $x)))\n");
  }
  if (has_undefined_function("log1p", 5) || has_undefined_function("log1pf", 6) ||
      has_undefined_function("log1pl", 6)) {
    wasm_emitf(2, "(func $log1p (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.lt (f64.abs (local.get $x)) (f64.const 1.0e-8)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(call $log (f64.add (f64.const 1) (local.get $x)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("log1pf", 6)) {
    wasm_emitf(2, "(func $log1pf (param $x f32) (result f32) (f32.demote_f64 (call $log1p (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("log1pl", 6)) {
    wasm_emitf(2, "(func $log1pl (param $x f64) (result f64) (call $log1p (local.get $x)))\n");
  }
  int sin_defined = psx_ctx_has_function_name("sin", 3) &&
                    psx_ctx_is_function_defined("sin", 3);
  int need_sin_stub = has_undefined_function("sin", 3) ||
                      has_undefined_function("sinf", 4) ||
                      has_undefined_function("sinl", 4) ||
                      (has_undefined_function("tan", 3) && !sin_defined) ||
                      (has_undefined_function("tanf", 4) && !sin_defined) ||
                      (has_undefined_function("tanl", 4) && !sin_defined);
  if (need_sin_stub) {
    wasm_emitf(2, "(func $sin (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $x2 f64)\n");
    wasm_emitf(4, "(local $term f64)\n");
    wasm_emitf(4, "(local $sum f64)\n");
    wasm_emitf(4, "(local $den f64)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.eq (f64.abs (local.get $x)) (f64.const inf)) (then (return (f64.div (f64.const 0) (f64.const 0)))))\n");
    wasm_emitf(4, "(block $done_hi (loop $hi\n");
    wasm_emitf(6, "(if (f64.le (local.get $x) (f64.const 3.141592653589793)) (then (br $done_hi)))\n");
    wasm_emitf(6, "(local.set $x (f64.sub (local.get $x) (f64.const 6.283185307179586)))\n");
    wasm_emitf(6, "(br $hi)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_lo (loop $lo\n");
    wasm_emitf(6, "(if (f64.ge (local.get $x) (f64.const -3.141592653589793)) (then (br $done_lo)))\n");
    wasm_emitf(6, "(local.set $x (f64.add (local.get $x) (f64.const 6.283185307179586)))\n");
    wasm_emitf(6, "(br $lo)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (f64.gt (local.get $x) (f64.const 1.5707963267948966)) (then (local.set $x (f64.sub (f64.const 3.141592653589793) (local.get $x)))))\n");
    wasm_emitf(4, "(if (f64.lt (local.get $x) (f64.const -1.5707963267948966)) (then (local.set $x (f64.sub (f64.const -3.141592653589793) (local.get $x)))))\n");
    wasm_emitf(4, "(local.set $x2 (f64.mul (local.get $x) (local.get $x)))\n");
    wasm_emitf(4, "(local.set $term (local.get $x))\n");
    wasm_emitf(4, "(local.set $sum (local.get $x))\n");
    wasm_emitf(4, "(local.set $i (i32.const 1))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.const 14)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $den (f64.convert_i32_s (i32.mul (i32.mul (local.get $i) (i32.const 2)) (i32.add (i32.mul (local.get $i) (i32.const 2)) (i32.const 1)))))\n");
    wasm_emitf(6, "(local.set $term (f64.div (f64.neg (f64.mul (local.get $term) (local.get $x2))) (local.get $den)))\n");
    wasm_emitf(6, "(local.set $sum (f64.add (local.get $sum) (local.get $term)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $sum)\n");
    wasm_emitf(2, ")\n");
  }
  int cos_defined = psx_ctx_has_function_name("cos", 3) &&
                    psx_ctx_is_function_defined("cos", 3);
  int need_cos_stub = has_undefined_function("cos", 3) ||
                      has_undefined_function("cosf", 4) ||
                      has_undefined_function("cosl", 4) ||
                      (has_undefined_function("tan", 3) && !cos_defined) ||
                      (has_undefined_function("tanf", 4) && !cos_defined) ||
                      (has_undefined_function("tanl", 4) && !cos_defined);
  if (need_cos_stub) {
    wasm_emitf(2, "(func $cos (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $x2 f64)\n");
    wasm_emitf(4, "(local $term f64)\n");
    wasm_emitf(4, "(local $sum f64)\n");
    wasm_emitf(4, "(local $den f64)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(local $sign f64)\n");
    wasm_emitf(4, "(local.set $sign (f64.const 1))\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.eq (f64.abs (local.get $x)) (f64.const inf)) (then (return (f64.div (f64.const 0) (f64.const 0)))))\n");
    wasm_emitf(4, "(block $done_hi (loop $hi\n");
    wasm_emitf(6, "(if (f64.le (local.get $x) (f64.const 3.141592653589793)) (then (br $done_hi)))\n");
    wasm_emitf(6, "(local.set $x (f64.sub (local.get $x) (f64.const 6.283185307179586)))\n");
    wasm_emitf(6, "(br $hi)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_lo (loop $lo\n");
    wasm_emitf(6, "(if (f64.ge (local.get $x) (f64.const -3.141592653589793)) (then (br $done_lo)))\n");
    wasm_emitf(6, "(local.set $x (f64.add (local.get $x) (f64.const 6.283185307179586)))\n");
    wasm_emitf(6, "(br $lo)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (f64.gt (local.get $x) (f64.const 1.5707963267948966)) (then (local.set $x (f64.sub (f64.const 3.141592653589793) (local.get $x))) (local.set $sign (f64.const -1))))\n");
    wasm_emitf(4, "(if (f64.lt (local.get $x) (f64.const -1.5707963267948966)) (then (local.set $x (f64.sub (f64.const -3.141592653589793) (local.get $x))) (local.set $sign (f64.const -1))))\n");
    wasm_emitf(4, "(local.set $x2 (f64.mul (local.get $x) (local.get $x)))\n");
    wasm_emitf(4, "(local.set $term (f64.const 1))\n");
    wasm_emitf(4, "(local.set $sum (f64.const 1))\n");
    wasm_emitf(4, "(local.set $i (i32.const 1))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.const 14)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $den (f64.convert_i32_s (i32.mul (i32.sub (i32.mul (local.get $i) (i32.const 2)) (i32.const 1)) (i32.mul (local.get $i) (i32.const 2)))))\n");
    wasm_emitf(6, "(local.set $term (f64.div (f64.neg (f64.mul (local.get $term) (local.get $x2))) (local.get $den)))\n");
    wasm_emitf(6, "(local.set $sum (f64.add (local.get $sum) (local.get $term)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(f64.mul (local.get $sign) (local.get $sum))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("tan", 3) || has_undefined_function("tanf", 4) ||
      has_undefined_function("tanl", 4)) {
    wasm_emitf(2, "(func $tan (param $x f64) (result f64)\n");
    wasm_emitf(4, "(f64.div (call $sin (local.get $x)) (call $cos (local.get $x)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("sinf", 4)) {
    wasm_emitf(2, "(func $sinf (param $x f32) (result f32) (f32.demote_f64 (call $sin (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("sinl", 4)) {
    wasm_emitf(2, "(func $sinl (param $x f64) (result f64) (call $sin (local.get $x)))\n");
  }
  if (has_undefined_function("cosf", 4)) {
    wasm_emitf(2, "(func $cosf (param $x f32) (result f32) (f32.demote_f64 (call $cos (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("cosl", 4)) {
    wasm_emitf(2, "(func $cosl (param $x f64) (result f64) (call $cos (local.get $x)))\n");
  }
  if (has_undefined_function("tanf", 4)) {
    wasm_emitf(2, "(func $tanf (param $x f32) (result f32) (f32.demote_f64 (call $tan (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("tanl", 4)) {
    wasm_emitf(2, "(func $tanl (param $x f64) (result f64) (call $tan (local.get $x)))\n");
  }
  if (need_sinh_stub) {
    wasm_emitf(2, "(func $sinh (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.lt (f64.abs (local.get $x)) (f64.const 1.0e-8)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(f64.mul (f64.const 0.5) (f64.sub (call $exp (local.get $x)) (call $exp (f64.neg (local.get $x)))))\n");
    wasm_emitf(2, ")\n");
  }
  if (need_cosh_stub) {
    wasm_emitf(2, "(func $cosh (param $x f64) (result f64)\n");
    wasm_emitf(4, "(f64.mul (f64.const 0.5) (f64.add (call $exp (local.get $x)) (call $exp (f64.neg (local.get $x)))))\n");
    wasm_emitf(2, ")\n");
  }
  if (need_tanh_stub) {
    wasm_emitf(2, "(func $tanh (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.lt (f64.abs (local.get $x)) (f64.const 1.0e-8)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.gt (local.get $x) (f64.const 20)) (then (return (f64.const 1))))\n");
    wasm_emitf(4, "(if (f64.lt (local.get $x) (f64.const -20)) (then (return (f64.const -1))))\n");
    wasm_emitf(4, "(f64.div (f64.sub (call $exp (local.get $x)) (call $exp (f64.neg (local.get $x)))) (f64.add (call $exp (local.get $x)) (call $exp (f64.neg (local.get $x)))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("sinhf", 5)) {
    wasm_emitf(2, "(func $sinhf (param $x f32) (result f32) (f32.demote_f64 (call $sinh (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("sinhl", 5)) {
    wasm_emitf(2, "(func $sinhl (param $x f64) (result f64) (call $sinh (local.get $x)))\n");
  }
  if (has_undefined_function("coshf", 5)) {
    wasm_emitf(2, "(func $coshf (param $x f32) (result f32) (f32.demote_f64 (call $cosh (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("coshl", 5)) {
    wasm_emitf(2, "(func $coshl (param $x f64) (result f64) (call $cosh (local.get $x)))\n");
  }
  if (has_undefined_function("tanhf", 5)) {
    wasm_emitf(2, "(func $tanhf (param $x f32) (result f32) (f32.demote_f64 (call $tanh (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("tanhl", 5)) {
    wasm_emitf(2, "(func $tanhl (param $x f64) (result f64) (call $tanh (local.get $x)))\n");
  }
  if (need_asinh_stub) {
    wasm_emitf(2, "(func $asinh (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $ax f64)\n");
    wasm_emitf(4, "(local $r f64)\n");
    wasm_emitf(4, "(if (f64.lt (f64.abs (local.get $x)) (f64.const 0.0001)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(local.set $ax (f64.abs (local.get $x)))\n");
    wasm_emitf(4, "(if (f64.gt (local.get $ax) (f64.const 1.0e154))\n");
    wasm_emitf(6, "(then (local.set $r (f64.add (call $log (local.get $ax)) (f64.const 0.6931471805599453))))\n");
    wasm_emitf(6, "(else (local.set $r (call $log (f64.add (local.get $ax) (call $sqrt (f64.add (f64.mul (local.get $ax) (local.get $ax)) (f64.const 1)))))))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(if (result f64) (f64.lt (local.get $x) (f64.const 0)) (then (f64.neg (local.get $r))) (else (local.get $r)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("asinhf", 6)) {
    wasm_emitf(2, "(func $asinhf (param $x f32) (result f32) (f32.demote_f64 (call $asinh (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("asinhl", 6)) {
    wasm_emitf(2, "(func $asinhl (param $x f64) (result f64) (call $asinh (local.get $x)))\n");
  }
  if (need_acosh_stub) {
    wasm_emitf(2, "(func $acosh (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.lt (local.get $x) (f64.const 1)) (then (return (call $sqrt (f64.const -1)))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 1)) (then (return (f64.const 0))))\n");
    wasm_emitf(4, "(if (f64.gt (local.get $x) (f64.const 1.0e154)) (then (return (f64.add (call $log (local.get $x)) (f64.const 0.6931471805599453)))))\n");
    wasm_emitf(4, "(call $log (f64.add (local.get $x) (f64.mul (call $sqrt (f64.sub (local.get $x) (f64.const 1))) (call $sqrt (f64.add (local.get $x) (f64.const 1))))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("acoshf", 6)) {
    wasm_emitf(2, "(func $acoshf (param $x f32) (result f32) (f32.demote_f64 (call $acosh (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("acoshl", 6)) {
    wasm_emitf(2, "(func $acoshl (param $x f64) (result f64) (call $acosh (local.get $x)))\n");
  }
  if (need_atanh_stub) {
    wasm_emitf(2, "(func $atanh (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $ax f64)\n");
    wasm_emitf(4, "(local.set $ax (f64.abs (local.get $x)))\n");
    wasm_emitf(4, "(if (f64.gt (local.get $ax) (f64.const 1)) (then (return (call $sqrt (f64.const -1)))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 1)) (then (return (f64.const inf))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const -1)) (then (return (f64.const -inf))))\n");
    wasm_emitf(4, "(if (f64.lt (local.get $ax) (f64.const 0.0001)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(f64.mul (f64.const 0.5) (call $log (f64.div (f64.add (f64.const 1) (local.get $x)) (f64.sub (f64.const 1) (local.get $x)))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("atanhf", 6)) {
    wasm_emitf(2, "(func $atanhf (param $x f32) (result f32) (f32.demote_f64 (call $atanh (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("atanhl", 6)) {
    wasm_emitf(2, "(func $atanhl (param $x f64) (result f64) (call $atanh (local.get $x)))\n");
  }
  if (has_undefined_function("sqrt", 4) || has_undefined_function("sqrtl", 5) ||
      has_undefined_function("hypot", 5) || has_undefined_function("hypotf", 6) ||
      has_undefined_function("hypotl", 6) ||
      need_asinh_stub || need_acosh_stub || need_atanh_stub ||
      has_undefined_function("asin", 4) || has_undefined_function("asinf", 5) ||
      has_undefined_function("asinl", 5) || has_undefined_function("acos", 4) ||
      has_undefined_function("acosf", 5) || has_undefined_function("acosl", 5) ||
      has_undefined_function("pow", 3) || has_undefined_function("powf", 4) ||
      has_undefined_function("powl", 4)) {
    wasm_emitf(2, "(func $sqrt (param $x f64) (result f64) (f64.sqrt (local.get $x)))\n");
  }
  if (has_undefined_function("sqrtf", 5)) {
    wasm_emitf(2, "(func $sqrtf (param $x f32) (result f32) (f32.sqrt (local.get $x)))\n");
  }
  if (has_undefined_function("sqrtl", 5)) {
    wasm_emitf(2, "(func $sqrtl (param $x f64) (result f64) (call $sqrt (local.get $x)))\n");
  }
  if (has_undefined_function("asin", 4) || has_undefined_function("asinf", 5) ||
      has_undefined_function("asinl", 5)) {
    wasm_emitf(2, "(func $asin (param $x f64) (result f64)\n");
    wasm_emitf(4, "(call $atan2 (local.get $x) (call $sqrt (f64.sub (f64.const 1) (f64.mul (local.get $x) (local.get $x)))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("acos", 4) || has_undefined_function("acosf", 5) ||
      has_undefined_function("acosl", 5)) {
    wasm_emitf(2, "(func $acos (param $x f64) (result f64)\n");
    wasm_emitf(4, "(call $atan2 (call $sqrt (f64.sub (f64.const 1) (f64.mul (local.get $x) (local.get $x)))) (local.get $x))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("asinf", 5)) {
    wasm_emitf(2, "(func $asinf (param $x f32) (result f32) (f32.demote_f64 (call $asin (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("asinl", 5)) {
    wasm_emitf(2, "(func $asinl (param $x f64) (result f64) (call $asin (local.get $x)))\n");
  }
  if (has_undefined_function("acosf", 5)) {
    wasm_emitf(2, "(func $acosf (param $x f32) (result f32) (f32.demote_f64 (call $acos (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("acosl", 5)) {
    wasm_emitf(2, "(func $acosl (param $x f64) (result f64) (call $acos (local.get $x)))\n");
  }
  if (has_undefined_function("atanf", 5)) {
    wasm_emitf(2, "(func $atanf (param $x f32) (result f32) (f32.demote_f64 (call $atan (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("atanl", 5)) {
    wasm_emitf(2, "(func $atanl (param $x f64) (result f64) (call $atan (local.get $x)))\n");
  }
  if (has_undefined_function("atan2f", 6)) {
    wasm_emitf(2, "(func $atan2f (param $y f32) (param $x f32) (result f32) (f32.demote_f64 (call $atan2 (f64.promote_f32 (local.get $y)) (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("atan2l", 6)) {
    wasm_emitf(2, "(func $atan2l (param $y f64) (param $x f64) (result f64) (call $atan2 (local.get $y) (local.get $x)))\n");
  }
  if (has_undefined_function("fmod", 4) || has_undefined_function("fmodf", 5) ||
      has_undefined_function("fmodl", 5)) {
    wasm_emitf(2, "(func $fmod (param $x f64) (param $y f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.ne (local.get $y) (local.get $y)) (then (return (local.get $y))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $y) (f64.const 0)) (then (return (f64.div (f64.const 0) (f64.const 0)))))\n");
    wasm_emitf(4, "(if (f64.eq (f64.abs (local.get $x)) (f64.const inf)) (then (return (f64.div (f64.const 0) (f64.const 0)))))\n");
    wasm_emitf(4, "(if (f64.eq (f64.abs (local.get $y)) (f64.const inf)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(f64.sub (local.get $x) (f64.mul (f64.trunc (f64.div (local.get $x) (local.get $y))) (local.get $y)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fmodf", 5)) {
    wasm_emitf(2, "(func $fmodf (param $x f32) (param $y f32) (result f32)\n");
    wasm_emitf(4, "(f32.demote_f64 (call $fmod (f64.promote_f32 (local.get $x)) (f64.promote_f32 (local.get $y))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fmodl", 5)) {
    wasm_emitf(2, "(func $fmodl (param $x f64) (param $y f64) (result f64) (call $fmod (local.get $x) (local.get $y)))\n");
  }
  if (need_cbrt_stub) {
    wasm_emitf(2, "(func $cbrt (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $r f64)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.eq (f64.abs (local.get $x)) (f64.const inf)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(local.set $r (call $exp (f64.div (call $log (f64.abs (local.get $x))) (f64.const 3))))\n");
    wasm_emitf(4, "(if (result f64) (f64.lt (local.get $x) (f64.const 0)) (then (f64.neg (local.get $r))) (else (local.get $r)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("cbrtf", 5)) {
    wasm_emitf(2, "(func $cbrtf (param $x f32) (result f32) (f32.demote_f64 (call $cbrt (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("cbrtl", 5)) {
    wasm_emitf(2, "(func $cbrtl (param $x f64) (result f64) (call $cbrt (local.get $x)))\n");
  }
  if (has_undefined_function("pow", 3) || has_undefined_function("powf", 4) ||
      has_undefined_function("powl", 4)) {
    wasm_emitf(2, "(func $pow (param $x f64) (param $y f64) (result f64)\n");
    wasm_emitf(4, "(local $n i32)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(local $r f64)\n");
    wasm_emitf(4, "(local $odd i32)\n");
    wasm_emitf(4, "(local $neg i32)\n");
    wasm_emitf(4, "(if (f64.eq (local.get $y) (f64.const 0)) (then (return (f64.const 1))))\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.ne (local.get $y) (local.get $y)) (then (return (local.get $y))))\n");
    wasm_emitf(4, "(local.set $neg (f64.lt (f64.div (f64.const 1) (local.get $x)) (f64.const 0)))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $y) (f64.const 0.5)) (then (return (call $sqrt (local.get $x)))))\n");
    wasm_emitf(4, "(if (f64.ne (local.get $y) (f64.trunc (local.get $y))) (then\n");
    wasm_emitf(6, "(if (i32.and (local.get $neg) (f64.ne (local.get $x) (f64.const 0))) (then (return (f64.div (f64.const 0) (f64.const 0)))))\n");
    wasm_emitf(6, "(return (call $exp (f64.mul (local.get $y) (call $log (local.get $x)))))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (f64.gt (f64.abs (local.get $y)) (f64.const 2147483647)) (then\n");
    wasm_emitf(6, "(local.set $odd (i64.ne (i64.and (i64.trunc_f64_s (f64.abs (local.get $y))) (i64.const 1)) (i64.const 0)))\n");
    wasm_emitf(6, "(if (f64.gt (f64.abs (local.get $x)) (f64.const 1)) (then\n");
    wasm_emitf(8, "(if (f64.gt (local.get $y) (f64.const 0))\n");
    wasm_emitf(10, "(then (return (if (result f64) (i32.and (local.get $neg) (local.get $odd)) (then (f64.const -inf)) (else (f64.const inf)))))\n");
    wasm_emitf(10, "(else (return (if (result f64) (i32.and (local.get $neg) (local.get $odd)) (then (f64.const -0)) (else (f64.const 0)))))\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(if (f64.lt (f64.abs (local.get $x)) (f64.const 1)) (then\n");
    wasm_emitf(8, "(if (f64.gt (local.get $y) (f64.const 0))\n");
    wasm_emitf(10, "(then (return (if (result f64) (i32.and (local.get $neg) (local.get $odd)) (then (f64.const -0)) (else (f64.const 0)))))\n");
    wasm_emitf(10, "(else (return (if (result f64) (i32.and (local.get $neg) (local.get $odd)) (then (f64.const -inf)) (else (f64.const inf)))))\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $n (i32.trunc_f64_s (local.get $y)))\n");
    wasm_emitf(4, "(local.set $odd (i32.ne (i32.and (local.get $n) (i32.const 1)) (i32.const 0)))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then\n");
    wasm_emitf(6, "(if (i32.lt_s (local.get $n) (i32.const 0))\n");
    wasm_emitf(8, "(then (return (if (result f64) (i32.and (local.get $neg) (local.get $odd)) (then (f64.const -inf)) (else (f64.const inf)))))\n");
    wasm_emitf(8, "(else (return (if (result f64) (i32.and (local.get $neg) (local.get $odd)) (then (f64.const -0)) (else (f64.const 0)))))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (i32.lt_s (local.get $n) (i32.const 0)) (then\n");
    wasm_emitf(6, "(local.set $n (i32.sub (i32.const 0) (local.get $n)))\n");
    wasm_emitf(6, "(local.set $r (f64.const 1))\n");
    wasm_emitf(6, "(block $done_neg (loop $loop_neg\n");
    wasm_emitf(8, "(if (i32.ge_s (local.get $i) (local.get $n)) (then (br $done_neg)))\n");
    wasm_emitf(8, "(local.set $r (f64.mul (local.get $r) (local.get $x)))\n");
    wasm_emitf(8, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(8, "(br $loop_neg)\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(6, "(return (f64.div (f64.const 1) (local.get $r)))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $r (f64.const 1))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_s (local.get $i) (local.get $n)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $r (f64.mul (local.get $r) (local.get $x)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $r)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("powf", 4)) {
    wasm_emitf(2, "(func $powf (param $x f32) (param $y f32) (result f32)\n");
    wasm_emitf(4, "(f32.demote_f64 (call $pow (f64.promote_f32 (local.get $x)) (f64.promote_f32 (local.get $y))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("powl", 4)) {
    wasm_emitf(2, "(func $powl (param $x f64) (param $y f64) (result f64) (call $pow (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("fabs", 4)) {
    wasm_emitf(2, "(func $fabs (param $x f64) (result f64) (f64.abs (local.get $x)))\n");
  }
  if (has_undefined_function("fabsf", 5)) {
    wasm_emitf(2, "(func $fabsf (param $x f32) (result f32) (f32.abs (local.get $x)))\n");
  }
  if (has_undefined_function("fabsl", 5)) {
    wasm_emitf(2, "(func $fabsl (param $x f64) (result f64) (f64.abs (local.get $x)))\n");
  }
  if (has_undefined_function("floor", 5)) {
    wasm_emitf(2, "(func $floor (param $x f64) (result f64) (f64.floor (local.get $x)))\n");
  }
  if (has_undefined_function("ceil", 4)) {
    wasm_emitf(2, "(func $ceil (param $x f64) (result f64) (f64.ceil (local.get $x)))\n");
  }
  if (has_undefined_function("round", 5) || has_undefined_function("roundl", 6)) {
    wasm_emitf(2, "(func $round (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (result f64) (f64.lt (local.get $x) (f64.const 0))\n");
    wasm_emitf(6, "(then (f64.ceil (f64.sub (local.get $x) (f64.const 0.5))))\n");
    wasm_emitf(6, "(else (f64.floor (f64.add (local.get $x) (f64.const 0.5))))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("trunc", 5)) {
    wasm_emitf(2, "(func $trunc (param $x f64) (result f64) (f64.trunc (local.get $x)))\n");
  }
  if (has_undefined_function("floorf", 6)) {
    wasm_emitf(2, "(func $floorf (param $x f32) (result f32) (f32.floor (local.get $x)))\n");
  }
  if (has_undefined_function("ceilf", 5)) {
    wasm_emitf(2, "(func $ceilf (param $x f32) (result f32) (f32.ceil (local.get $x)))\n");
  }
  if (has_undefined_function("roundf", 6)) {
    wasm_emitf(2, "(func $roundf (param $x f32) (result f32)\n");
    wasm_emitf(4, "(if (result f32) (f32.lt (local.get $x) (f32.const 0))\n");
    wasm_emitf(6, "(then (f32.ceil (f32.sub (local.get $x) (f32.const 0.5))))\n");
    wasm_emitf(6, "(else (f32.floor (f32.add (local.get $x) (f32.const 0.5))))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("floorl", 6)) {
    wasm_emitf(2, "(func $floorl (param $x f64) (result f64) (f64.floor (local.get $x)))\n");
  }
  if (has_undefined_function("ceill", 5)) {
    wasm_emitf(2, "(func $ceill (param $x f64) (result f64) (f64.ceil (local.get $x)))\n");
  }
  if (has_undefined_function("roundl", 6)) {
    wasm_emitf(2, "(func $roundl (param $x f64) (result f64) (call $round (local.get $x)))\n");
  }
  if (has_undefined_function("truncf", 6)) {
    wasm_emitf(2, "(func $truncf (param $x f32) (result f32) (f32.trunc (local.get $x)))\n");
  }
  if (has_undefined_function("truncl", 6)) {
    wasm_emitf(2, "(func $truncl (param $x f64) (result f64) (f64.trunc (local.get $x)))\n");
  }
  if (has_undefined_function("expf", 4)) {
    wasm_emitf(2, "(func $expf (param $x f32) (result f32) (f32.demote_f64 (call $exp (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("expl", 4)) {
    wasm_emitf(2, "(func $expl (param $x f64) (result f64) (call $exp (local.get $x)))\n");
  }
  if (has_undefined_function("logf", 4)) {
    wasm_emitf(2, "(func $logf (param $x f32) (result f32) (f32.demote_f64 (call $log (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("logl", 4)) {
    wasm_emitf(2, "(func $logl (param $x f64) (result f64) (call $log (local.get $x)))\n");
  }
  if (has_undefined_function("hypot", 5)) {
    wasm_emitf(2, "(func $hypot (param $x f64) (param $y f64) (result f64)\n");
    wasm_emitf(4, "(if (i32.or (f64.eq (f64.abs (local.get $x)) (f64.const inf)) (f64.eq (f64.abs (local.get $y)) (f64.const inf))) (then (return (f64.const inf))))\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.ne (local.get $y) (local.get $y)) (then (return (local.get $y))))\n");
    wasm_emitf(4, "(call $sqrt (f64.add (f64.mul (local.get $x) (local.get $x)) (f64.mul (local.get $y) (local.get $y))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("hypotf", 6)) {
    wasm_emitf(2, "(func $hypotf (param $x f32) (param $y f32) (result f32) (f32.demote_f64 (call $hypot (f64.promote_f32 (local.get $x)) (f64.promote_f32 (local.get $y)))))\n");
  }
  if (has_undefined_function("hypotl", 6)) {
    wasm_emitf(2, "(func $hypotl (param $x f64) (param $y f64) (result f64) (call $hypot (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("fdim", 4) || has_undefined_function("fdiml", 5)) {
    wasm_emitf(2, "(func $fdim (param $x f64) (param $y f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.ne (local.get $y) (local.get $y)) (then (return (local.get $y))))\n");
    wasm_emitf(4, "(if (result f64) (f64.gt (local.get $x) (local.get $y))\n");
    wasm_emitf(6, "(then (f64.sub (local.get $x) (local.get $y)))\n");
    wasm_emitf(6, "(else (f64.const 0))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fdimf", 5)) {
    wasm_emitf(2, "(func $fdimf (param $x f32) (param $y f32) (result f32)\n");
    wasm_emitf(4, "(if (f32.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f32.ne (local.get $y) (local.get $y)) (then (return (local.get $y))))\n");
    wasm_emitf(4, "(if (result f32) (f32.gt (local.get $x) (local.get $y))\n");
    wasm_emitf(6, "(then (f32.sub (local.get $x) (local.get $y)))\n");
    wasm_emitf(6, "(else (f32.const 0))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fdiml", 5)) {
    wasm_emitf(2, "(func $fdiml (param $x f64) (param $y f64) (result f64) (call $fdim (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("fma", 3) || has_undefined_function("fmal", 4)) {
    wasm_emitf(2, "(func $fma (param $x f64) (param $y f64) (param $z f64) (result f64)\n");
    wasm_emitf(4, "(f64.add (f64.mul (local.get $x) (local.get $y)) (local.get $z))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fmaf", 4)) {
    wasm_emitf(2, "(func $fmaf (param $x f32) (param $y f32) (param $z f32) (result f32)\n");
    wasm_emitf(4, "(f32.add (f32.mul (local.get $x) (local.get $y)) (local.get $z))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fmal", 4)) {
    wasm_emitf(2, "(func $fmal (param $x f64) (param $y f64) (param $z f64) (result f64) (call $fma (local.get $x) (local.get $y) (local.get $z)))\n");
  }
  if (has_undefined_function("frexp", 5) || has_undefined_function("frexpf", 6) ||
      has_undefined_function("frexpl", 6)) {
    wasm_emitf(2, "(func $frexp (param $x f64) (param $exp i32) (result f64)\n");
    wasm_emitf(4, "(local $ax f64)\n");
    wasm_emitf(4, "(local $e i32)\n");
    wasm_emitf(4, "(if (i32.eqz (local.get $exp)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (i32.or (f64.eq (local.get $x) (f64.const 0)) (i32.or (f64.ne (local.get $x) (local.get $x)) (f64.eq (f64.abs (local.get $x)) (f64.const inf)))) (then\n");
    wasm_emitf(6, "(i32.store (local.get $exp) (i32.const 0))\n");
    wasm_emitf(6, "(return (local.get $x))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $ax (f64.abs (local.get $x)))\n");
    wasm_emitf(4, "(block $high_done (loop $high_loop\n");
    wasm_emitf(6, "(if (f64.lt (local.get $ax) (f64.const 1)) (then (br $high_done)))\n");
    wasm_emitf(6, "(local.set $ax (f64.div (local.get $ax) (f64.const 2)))\n");
    wasm_emitf(6, "(local.set $e (i32.add (local.get $e) (i32.const 1)))\n");
    wasm_emitf(6, "(br $high_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $low_done (loop $low_loop\n");
    wasm_emitf(6, "(if (f64.ge (local.get $ax) (f64.const 0.5)) (then (br $low_done)))\n");
    wasm_emitf(6, "(local.set $ax (f64.mul (local.get $ax) (f64.const 2)))\n");
    wasm_emitf(6, "(local.set $e (i32.sub (local.get $e) (i32.const 1)))\n");
    wasm_emitf(6, "(br $low_loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.store (local.get $exp) (local.get $e))\n");
    wasm_emitf(4, "(f64.copysign (local.get $ax) (local.get $x))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("frexpf", 6)) {
    wasm_emitf(2, "(func $frexpf (param $x f32) (param $exp i32) (result f32)\n");
    wasm_emitf(4, "(f32.demote_f64 (call $frexp (f64.promote_f32 (local.get $x)) (local.get $exp)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("frexpl", 6)) {
    wasm_emitf(2, "(func $frexpl (param $x f64) (param $exp i32) (result f64) (call $frexp (local.get $x) (local.get $exp)))\n");
  }
  if (has_undefined_function("fmin", 4) || has_undefined_function("fminl", 5)) {
    wasm_emitf(2, "(func $fmin (param $x f64) (param $y f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $y))))\n");
    wasm_emitf(4, "(if (f64.ne (local.get $y) (local.get $y)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(f64.min (local.get $x) (local.get $y))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fminf", 5)) {
    wasm_emitf(2, "(func $fminf (param $x f32) (param $y f32) (result f32)\n");
    wasm_emitf(4, "(if (f32.ne (local.get $x) (local.get $x)) (then (return (local.get $y))))\n");
    wasm_emitf(4, "(if (f32.ne (local.get $y) (local.get $y)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(f32.min (local.get $x) (local.get $y))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fminl", 5)) {
    wasm_emitf(2, "(func $fminl (param $x f64) (param $y f64) (result f64) (call $fmin (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("fmax", 4) || has_undefined_function("fmaxl", 5)) {
    wasm_emitf(2, "(func $fmax (param $x f64) (param $y f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $y))))\n");
    wasm_emitf(4, "(if (f64.ne (local.get $y) (local.get $y)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(f64.max (local.get $x) (local.get $y))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fmaxf", 5)) {
    wasm_emitf(2, "(func $fmaxf (param $x f32) (param $y f32) (result f32)\n");
    wasm_emitf(4, "(if (f32.ne (local.get $x) (local.get $x)) (then (return (local.get $y))))\n");
    wasm_emitf(4, "(if (f32.ne (local.get $y) (local.get $y)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(f32.max (local.get $x) (local.get $y))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fmaxl", 5)) {
    wasm_emitf(2, "(func $fmaxl (param $x f64) (param $y f64) (result f64) (call $fmax (local.get $x) (local.get $y)))\n");
  }
  int need_isunordered_stub =
      has_undefined_function("isunordered", 11) || has_undefined_function("isgreater", 9) ||
      has_undefined_function("isgreaterequal", 14) || has_undefined_function("isless", 6) ||
      has_undefined_function("islessequal", 11) || has_undefined_function("islessgreater", 13);
  int need_isnan_stub =
      has_undefined_function("isnan", 5) || has_undefined_function("isfinite", 8) ||
      has_undefined_function("fpclassify", 10) || need_isunordered_stub;
  int need_isinf_stub =
      has_undefined_function("isinf", 5) || has_undefined_function("isfinite", 8) ||
      has_undefined_function("fpclassify", 10);
  int need_fpclassify_stub =
      has_undefined_function("fpclassify", 10) || has_undefined_function("isnormal", 8);
  if (!has_defined_function("isnan", 5) && need_isnan_stub) {
    wasm_emitf(2, "(func $isnan (param $x f64) (result i32)\n");
    wasm_emitf(4, "(f64.ne (local.get $x) (local.get $x))\n");
    wasm_emitf(2, ")\n");
  }
  if (!has_defined_function("isinf", 5) && need_isinf_stub) {
    wasm_emitf(2, "(func $isinf (param $x f64) (result i32)\n");
    wasm_emitf(4, "(f64.eq (f64.abs (local.get $x)) (f64.const inf))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("isfinite", 8)) {
    wasm_emitf(2, "(func $isfinite (param $x f64) (result i32)\n");
    wasm_emitf(4, "(i32.eqz (i32.or (call $isnan (local.get $x)) (call $isinf (local.get $x))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("signbit", 7)) {
    wasm_emitf(2, "(func $signbit (param $x f64) (result i32)\n");
    wasm_emitf(4, "(i64.lt_s (i64.reinterpret_f64 (local.get $x)) (i64.const 0))\n");
    wasm_emitf(2, ")\n");
  }
  if (!has_defined_function("fpclassify", 10) && need_fpclassify_stub) {
    wasm_emitf(2, "(func $fpclassify (param $x f64) (result i32)\n");
    wasm_emitf(4, "(local $ax f64)\n");
    wasm_emitf(4, "(if (call $isnan (local.get $x)) (then (return (i32.const 0))))\n");
    wasm_emitf(4, "(if (call $isinf (local.get $x)) (then (return (i32.const 1))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (i32.const 2))))\n");
    wasm_emitf(4, "(local.set $ax (f64.abs (local.get $x)))\n");
    wasm_emitf(4, "(if (result i32) (f64.lt (local.get $ax) (f64.const 2.2250738585072014e-308)) (then (i32.const 3)) (else (i32.const 4)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("isnormal", 8)) {
    wasm_emitf(2, "(func $isnormal (param $x f64) (result i32)\n");
    wasm_emitf(4, "(i32.eq (call $fpclassify (local.get $x)) (i32.const 4))\n");
    wasm_emitf(2, ")\n");
  }
  if (!has_defined_function("isunordered", 11) && need_isunordered_stub) {
    wasm_emitf(2, "(func $isunordered (param $x f64) (param $y f64) (result i32)\n");
    wasm_emitf(4, "(i32.or (call $isnan (local.get $x)) (call $isnan (local.get $y)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("isgreater", 9)) {
    wasm_emitf(2, "(func $isgreater (param $x f64) (param $y f64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i32.eqz (call $isunordered (local.get $x) (local.get $y))) (f64.gt (local.get $x) (local.get $y)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("isgreaterequal", 14)) {
    wasm_emitf(2, "(func $isgreaterequal (param $x f64) (param $y f64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i32.eqz (call $isunordered (local.get $x) (local.get $y))) (f64.ge (local.get $x) (local.get $y)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("isless", 6)) {
    wasm_emitf(2, "(func $isless (param $x f64) (param $y f64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i32.eqz (call $isunordered (local.get $x) (local.get $y))) (f64.lt (local.get $x) (local.get $y)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("islessequal", 11)) {
    wasm_emitf(2, "(func $islessequal (param $x f64) (param $y f64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i32.eqz (call $isunordered (local.get $x) (local.get $y))) (f64.le (local.get $x) (local.get $y)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("islessgreater", 13)) {
    wasm_emitf(2, "(func $islessgreater (param $x f64) (param $y f64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i32.eqz (call $isunordered (local.get $x) (local.get $y))) (f64.ne (local.get $x) (local.get $y)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("copysign", 8)) {
    wasm_emitf(2, "(func $copysign (param $x f64) (param $y f64) (result f64) (f64.copysign (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("copysignf", 9)) {
    wasm_emitf(2, "(func $copysignf (param $x f32) (param $y f32) (result f32) (f32.copysign (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("copysignl", 9)) {
    wasm_emitf(2, "(func $copysignl (param $x f64) (param $y f64) (result f64) (f64.copysign (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("nan", 3) || has_undefined_function("nanl", 4)) {
    wasm_emitf(2, "(func $nan (param $tagp i32) (result f64)\n");
    wasm_emitf(4, "(f64.div (f64.const 0) (f64.const 0))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("nanf", 4)) {
    wasm_emitf(2, "(func $nanf (param $tagp i32) (result f32)\n");
    wasm_emitf(4, "(f32.div (f32.const 0) (f32.const 0))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("nanl", 4)) {
    wasm_emitf(2, "(func $nanl (param $tagp i32) (result f64) (call $nan (local.get $tagp)))\n");
  }
  if (has_undefined_function("erf", 3) || has_undefined_function("erff", 4) ||
      has_undefined_function("erfl", 4)) {
    wasm_emitf(2, "(func $erf (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $ax f64)\n");
    wasm_emitf(4, "(local $t f64)\n");
    wasm_emitf(4, "(local $poly f64)\n");
    wasm_emitf(4, "(local $tail f64)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const inf)) (then (return (f64.const 1))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const -inf)) (then (return (f64.const -1))))\n");
    wasm_emitf(4, "(local.set $ax (f64.abs (local.get $x)))\n");
    wasm_emitf(4, "(local.set $t (f64.div (f64.const 1) (f64.add (f64.const 1) (f64.mul (f64.const 0.3275911) (local.get $ax)))))\n");
    wasm_emitf(4, "(local.set $poly (f64.mul (f64.add (f64.mul (f64.add (f64.mul (f64.add (f64.mul (f64.add (f64.mul (f64.const 1.061405429) (local.get $t)) (f64.const -1.453152027)) (local.get $t)) (f64.const 1.421413741)) (local.get $t)) (f64.const -0.284496736)) (local.get $t)) (f64.const 0.254829592)) (local.get $t)))\n");
    wasm_emitf(4, "(local.set $tail (f64.mul (local.get $poly) (call $exp (f64.neg (f64.mul (local.get $ax) (local.get $ax))))))\n");
    wasm_emitf(4, "(if (result f64) (f64.lt (local.get $x) (f64.const 0)) (then (f64.sub (local.get $tail) (f64.const 1))) (else (f64.sub (f64.const 1) (local.get $tail))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("erff", 4)) {
    wasm_emitf(2, "(func $erff (param $x f32) (result f32) (f32.demote_f64 (call $erf (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("erfl", 4)) {
    wasm_emitf(2, "(func $erfl (param $x f64) (result f64) (call $erf (local.get $x)))\n");
  }
  if (has_undefined_function("erfc", 4) || has_undefined_function("erfcf", 5) ||
      has_undefined_function("erfcl", 5)) {
    wasm_emitf(2, "(func $erfc (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $ax f64)\n");
    wasm_emitf(4, "(local $t f64)\n");
    wasm_emitf(4, "(local $poly f64)\n");
    wasm_emitf(4, "(local $tail f64)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (f64.const 1))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const inf)) (then (return (f64.const 0))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const -inf)) (then (return (f64.const 2))))\n");
    wasm_emitf(4, "(local.set $ax (f64.abs (local.get $x)))\n");
    wasm_emitf(4, "(local.set $t (f64.div (f64.const 1) (f64.add (f64.const 1) (f64.mul (f64.const 0.3275911) (local.get $ax)))))\n");
    wasm_emitf(4, "(local.set $poly (f64.mul (f64.add (f64.mul (f64.add (f64.mul (f64.add (f64.mul (f64.add (f64.mul (f64.const 1.061405429) (local.get $t)) (f64.const -1.453152027)) (local.get $t)) (f64.const 1.421413741)) (local.get $t)) (f64.const -0.284496736)) (local.get $t)) (f64.const 0.254829592)) (local.get $t)))\n");
    wasm_emitf(4, "(local.set $tail (f64.mul (local.get $poly) (call $exp (f64.neg (f64.mul (local.get $ax) (local.get $ax))))))\n");
    wasm_emitf(4, "(if (result f64) (f64.lt (local.get $x) (f64.const 0)) (then (f64.sub (f64.const 2) (local.get $tail))) (else (local.get $tail)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("erfcf", 5)) {
    wasm_emitf(2, "(func $erfcf (param $x f32) (result f32) (f32.demote_f64 (call $erfc (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("erfcl", 5)) {
    wasm_emitf(2, "(func $erfcl (param $x f64) (result f64) (call $erfc (local.get $x)))\n");
  }
  if (need_rounding_mode_helper) {
    wasm_emitf(2, "(func $__ag_round_current (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (result f64) (i32.eq (global.get $__ag_fe_round_mode) (i32.const 4194304))\n");
    wasm_emitf(6, "(then (f64.ceil (local.get $x)))\n");
    wasm_emitf(6, "(else (if (result f64) (i32.eq (global.get $__ag_fe_round_mode) (i32.const 8388608))\n");
    wasm_emitf(8, "(then (f64.floor (local.get $x)))\n");
    wasm_emitf(8, "(else (if (result f64) (i32.eq (global.get $__ag_fe_round_mode) (i32.const 12582912))\n");
    wasm_emitf(10, "(then (f64.trunc (local.get $x)))\n");
    wasm_emitf(10, "(else (f64.nearest (local.get $x)))\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("nearbyint", 9) || has_undefined_function("nearbyintl", 10)) {
    wasm_emitf(2, "(func $nearbyint (param $x f64) (result f64) (call $__ag_round_current (local.get $x)))\n");
  }
  if (has_undefined_function("nearbyintf", 10)) {
    wasm_emitf(2, "(func $nearbyintf (param $x f32) (result f32) (f32.demote_f64 (call $__ag_round_current (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("nearbyintl", 10)) {
    wasm_emitf(2, "(func $nearbyintl (param $x f64) (result f64) (call $nearbyint (local.get $x)))\n");
  }
  if (has_undefined_function("rint", 4) || has_undefined_function("rintl", 5)) {
    wasm_emitf(2, "(func $rint (param $x f64) (result f64) (call $__ag_round_current (local.get $x)))\n");
  }
  if (has_undefined_function("rintf", 5)) {
    wasm_emitf(2, "(func $rintf (param $x f32) (result f32) (f32.demote_f64 (call $__ag_round_current (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("rintl", 5)) {
    wasm_emitf(2, "(func $rintl (param $x f64) (result f64) (call $rint (local.get $x)))\n");
  }
  if (has_undefined_function("lrint", 5) || has_undefined_function("lrintl", 6) ||
      has_undefined_function("llrint", 6) || has_undefined_function("llrintl", 7)) {
    wasm_emitf(2, "(func $lrint (param $x f64) (result i64) (i64.trunc_f64_s (call $__ag_round_current (local.get $x))))\n");
  }
  if (has_undefined_function("lrintf", 6)) {
    wasm_emitf(2, "(func $lrintf (param $x f32) (result i64) (call $lrint (f64.promote_f32 (local.get $x))))\n");
  }
  if (has_undefined_function("lrintl", 6)) {
    wasm_emitf(2, "(func $lrintl (param $x f64) (result i64) (call $lrint (local.get $x)))\n");
  }
  if (has_undefined_function("llrint", 6)) {
    wasm_emitf(2, "(func $llrint (param $x f64) (result i64) (call $lrint (local.get $x)))\n");
  }
  if (has_undefined_function("llrintf", 7)) {
    wasm_emitf(2, "(func $llrintf (param $x f32) (result i64) (call $lrint (f64.promote_f32 (local.get $x))))\n");
  }
  if (has_undefined_function("llrintl", 7)) {
    wasm_emitf(2, "(func $llrintl (param $x f64) (result i64) (call $lrint (local.get $x)))\n");
  }
  if (has_undefined_function("lround", 6) || has_undefined_function("lroundl", 7) ||
      has_undefined_function("llround", 7) || has_undefined_function("llroundl", 8)) {
    wasm_emitf(2, "(func $lround (param $x f64) (result i64)\n");
    wasm_emitf(4, "(i64.trunc_f64_s (if (result f64) (f64.lt (local.get $x) (f64.const 0)) (then (f64.ceil (f64.sub (local.get $x) (f64.const 0.5)))) (else (f64.floor (f64.add (local.get $x) (f64.const 0.5))))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("lroundf", 7)) {
    wasm_emitf(2, "(func $lroundf (param $x f32) (result i64) (call $lround (f64.promote_f32 (local.get $x))))\n");
  }
  if (has_undefined_function("lroundl", 7)) {
    wasm_emitf(2, "(func $lroundl (param $x f64) (result i64) (call $lround (local.get $x)))\n");
  }
  if (has_undefined_function("llround", 7)) {
    wasm_emitf(2, "(func $llround (param $x f64) (result i64) (call $lround (local.get $x)))\n");
  }
  if (has_undefined_function("llroundf", 8)) {
    wasm_emitf(2, "(func $llroundf (param $x f32) (result i64) (call $lround (f64.promote_f32 (local.get $x))))\n");
  }
  if (has_undefined_function("llroundl", 8)) {
    wasm_emitf(2, "(func $llroundl (param $x f64) (result i64) (call $lround (local.get $x)))\n");
  }
  int need_remainder_stub =
      has_undefined_function("remainder", 9) || has_undefined_function("remainderf", 10) ||
      has_undefined_function("remainderl", 10) || has_undefined_function("remquo", 6) ||
      has_undefined_function("remquof", 7) || has_undefined_function("remquol", 7);
  if (need_remainder_stub) {
    wasm_emitf(2, "(func $remainder (param $x f64) (param $y f64) (result f64)\n");
    wasm_emitf(4, "(f64.sub (local.get $x) (f64.mul (f64.nearest (f64.div (local.get $x) (local.get $y))) (local.get $y)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("remainderf", 10)) {
    wasm_emitf(2, "(func $remainderf (param $x f32) (param $y f32) (result f32) (f32.demote_f64 (call $remainder (f64.promote_f32 (local.get $x)) (f64.promote_f32 (local.get $y)))))\n");
  }
  if (has_undefined_function("remainderl", 10)) {
    wasm_emitf(2, "(func $remainderl (param $x f64) (param $y f64) (result f64) (call $remainder (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("remquo", 6) || has_undefined_function("remquol", 7)) {
    wasm_emitf(2, "(func $remquo (param $x f64) (param $y f64) (param $quo i32) (result f64)\n");
    wasm_emitf(4, "(local $q i32)\n");
    wasm_emitf(4, "(if (f64.gt (f64.abs (f64.div (local.get $x) (local.get $y))) (f64.const 2147483647)) (then\n");
    wasm_emitf(6, "(i32.store (local.get $quo) (i32.const 0))\n");
    wasm_emitf(6, "(return (call $remainder (local.get $x) (local.get $y)))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $q (i32.trunc_f64_s (f64.nearest (f64.div (local.get $x) (local.get $y)))))\n");
    wasm_emitf(4, "(i32.store (local.get $quo) (i32.and (local.get $q) (i32.const 7)))\n");
    wasm_emitf(4, "(call $remainder (local.get $x) (local.get $y))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("remquof", 7)) {
    wasm_emitf(2, "(func $remquof (param $x f32) (param $y f32) (param $quo i32) (result f32)\n");
    wasm_emitf(4, "(local $q i32)\n");
    wasm_emitf(4, "(if (f32.gt (f32.abs (f32.div (local.get $x) (local.get $y))) (f32.const 2147483647)) (then\n");
    wasm_emitf(6, "(i32.store (local.get $quo) (i32.const 0))\n");
    wasm_emitf(6, "(return (f32.demote_f64 (call $remainder (f64.promote_f32 (local.get $x)) (f64.promote_f32 (local.get $y)))))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $q (i32.trunc_f32_s (f32.nearest (f32.div (local.get $x) (local.get $y)))))\n");
    wasm_emitf(4, "(i32.store (local.get $quo) (i32.and (local.get $q) (i32.const 7)))\n");
    wasm_emitf(4, "(f32.demote_f64 (call $remainder (f64.promote_f32 (local.get $x)) (f64.promote_f32 (local.get $y))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("remquol", 7)) {
    wasm_emitf(2, "(func $remquol (param $x f64) (param $y f64) (param $quo i32) (result f64) (call $remquo (local.get $x) (local.get $y) (local.get $quo)))\n");
  }
  if (has_undefined_function("modf", 4) || has_undefined_function("modfl", 5)) {
    wasm_emitf(2, "(func $modf (param $x f64) (param $iptr i32) (result f64)\n");
    wasm_emitf(4, "(local $whole f64)\n");
    wasm_emitf(4, "(local.set $whole (f64.trunc (local.get $x)))\n");
    wasm_emitf(4, "(f64.store (local.get $iptr) (local.get $whole))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (local.get $whole)) (then (return (f64.mul (f64.const 0) (local.get $x)))))\n");
    wasm_emitf(4, "(f64.sub (local.get $x) (local.get $whole))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("modff", 5)) {
    wasm_emitf(2, "(func $modff (param $x f32) (param $iptr i32) (result f32)\n");
    wasm_emitf(4, "(local $whole f32)\n");
    wasm_emitf(4, "(local.set $whole (f32.trunc (local.get $x)))\n");
    wasm_emitf(4, "(f32.store (local.get $iptr) (local.get $whole))\n");
    wasm_emitf(4, "(if (f32.eq (local.get $x) (local.get $whole)) (then (return (f32.mul (f32.const 0) (local.get $x)))))\n");
    wasm_emitf(4, "(f32.sub (local.get $x) (local.get $whole))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("modfl", 5)) {
    wasm_emitf(2, "(func $modfl (param $x f64) (param $iptr i32) (result f64) (call $modf (local.get $x) (local.get $iptr)))\n");
  }
  int need_scalbn_stub =
      has_undefined_function("scalbn", 6) || has_undefined_function("scalbnf", 7) ||
      has_undefined_function("scalbnl", 7) || has_undefined_function("scalbln", 7) ||
      has_undefined_function("scalblnf", 8) || has_undefined_function("scalblnl", 8) ||
      has_undefined_function("ldexp", 5) || has_undefined_function("ldexpf", 6) ||
      has_undefined_function("ldexpl", 6);
  if (need_scalbn_stub) {
    wasm_emitf(2, "(func $scalbn (param $x f64) (param $n i64) (result f64)\n");
    wasm_emitf(4, "(f64.mul (local.get $x) (call $exp2 (f64.convert_i64_s (local.get $n))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("scalbnf", 7) || has_undefined_function("scalblnf", 8) ||
      has_undefined_function("ldexpf", 6)) {
    wasm_emitf(2, "(func $scalbnf (param $x f32) (param $n i64) (result f32) (f32.demote_f64 (call $scalbn (f64.promote_f32 (local.get $x)) (local.get $n))))\n");
  }
  if (has_undefined_function("scalbnl", 7) || has_undefined_function("scalblnl", 8) ||
      has_undefined_function("ldexpl", 6)) {
    wasm_emitf(2, "(func $scalbnl (param $x f64) (param $n i64) (result f64) (call $scalbn (local.get $x) (local.get $n)))\n");
  }
  if (has_undefined_function("scalbln", 7)) {
    wasm_emitf(2, "(func $scalbln (param $x f64) (param $n i64) (result f64) (call $scalbn (local.get $x) (local.get $n)))\n");
  }
  if (has_undefined_function("scalblnf", 8)) {
    wasm_emitf(2, "(func $scalblnf (param $x f32) (param $n i64) (result f32) (call $scalbnf (local.get $x) (local.get $n)))\n");
  }
  if (has_undefined_function("scalblnl", 8)) {
    wasm_emitf(2, "(func $scalblnl (param $x f64) (param $n i64) (result f64) (call $scalbn (local.get $x) (local.get $n)))\n");
  }
  if (has_undefined_function("ldexp", 5)) {
    wasm_emitf(2, "(func $ldexp (param $x f64) (param $n i64) (result f64) (call $scalbn (local.get $x) (local.get $n)))\n");
  }
  if (has_undefined_function("ldexpf", 6)) {
    wasm_emitf(2, "(func $ldexpf (param $x f32) (param $n i64) (result f32) (call $scalbnf (local.get $x) (local.get $n)))\n");
  }
  if (has_undefined_function("ldexpl", 6)) {
    wasm_emitf(2, "(func $ldexpl (param $x f64) (param $n i64) (result f64) (call $scalbn (local.get $x) (local.get $n)))\n");
  }
  if (has_undefined_function("ilogb", 5) || has_undefined_function("ilogbf", 6) ||
      has_undefined_function("ilogbl", 6) || has_undefined_function("logb", 4) ||
      has_undefined_function("logbf", 5) || has_undefined_function("logbl", 5)) {
    wasm_emitf(2, "(func $ilogb (param $x f64) (result i32)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (i32.const -2147483648))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (i32.const -2147483648))))\n");
    wasm_emitf(4, "(if (f64.eq (f64.abs (local.get $x)) (f64.const inf)) (then (return (i32.const 2147483647))))\n");
    wasm_emitf(4, "(i32.trunc_f64_s (f64.floor (call $log2 (f64.abs (local.get $x)))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("ilogbf", 6)) {
    wasm_emitf(2, "(func $ilogbf (param $x f32) (result i32) (call $ilogb (f64.promote_f32 (local.get $x))))\n");
  }
  if (has_undefined_function("ilogbl", 6)) {
    wasm_emitf(2, "(func $ilogbl (param $x f64) (result i32) (call $ilogb (local.get $x)))\n");
  }
  if (has_undefined_function("logb", 4) || has_undefined_function("logbf", 5) ||
      has_undefined_function("logbl", 5)) {
    wasm_emitf(2, "(func $logb (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (f64.ne (local.get $x) (local.get $x)) (then (return (local.get $x))))\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (f64.const -inf))))\n");
    wasm_emitf(4, "(if (f64.eq (f64.abs (local.get $x)) (f64.const inf)) (then (return (f64.const inf))))\n");
    wasm_emitf(4, "(f64.convert_i32_s (call $ilogb (local.get $x)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("logbf", 5)) {
    wasm_emitf(2, "(func $logbf (param $x f32) (result f32) (f32.demote_f64 (call $logb (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("logbl", 5)) {
    wasm_emitf(2, "(func $logbl (param $x f64) (result f64) (call $logb (local.get $x)))\n");
  }
}

static int wasm_align_up_int(int value, int align) {
  if (align <= 0) return value;
  int rem = value % align;
  if (rem == 0) return value;
  return value + (align - rem);
}

static void emit_memory_layout_decls(void) {
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

void wasm32_module_end(void) {
  emit_minimal_libc_stubs();
  emit_function_table();
  emit_memory_layout_decls();
  cg_emitf(")\n");
}
