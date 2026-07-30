#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  SEC_CUSTOM = 0,
  SEC_TYPE = 1,
  SEC_IMPORT = 2,
  SEC_FUNCTION = 3,
  SEC_TABLE = 4,
  SEC_MEMORY = 5,
  SEC_GLOBAL = 6,
  SEC_EXPORT = 7,
  SEC_ELEM = 9,
  SEC_CODE = 10,
  SEC_DATA = 11,

  R_WASM_FUNCTION_INDEX_LEB = 0,
  R_WASM_TABLE_INDEX_SLEB = 1,
  R_WASM_TABLE_INDEX_I32 = 2,
  R_WASM_MEMORY_ADDR_LEB = 3,
  R_WASM_MEMORY_ADDR_I32 = 5,
  R_WASM_TYPE_INDEX_LEB = 6,
  R_WASM_GLOBAL_INDEX_LEB = 7,

  SYM_FUNCTION = 0,
  SYM_DATA = 1,
  SYM_GLOBAL = 2,

  SYM_BINDING_LOCAL = 0x2,
  SYM_UNDEFINED = 0x10,

  LINK_SEGMENT_INFO = 5,
  LINK_SYMBOL_TABLE = 8,

  AGC_FUNCTION_FLAG_PARAMETERS_UNSPECIFIED = 1u << 0,

  RUNTIME_SCRATCH_BASE = 32768,
};

static const char *DEFAULT_RUNTIME_OBJECT = "build/libagc_runtime.o";
static const char *runtime_object_path(void) {
  const char *path = getenv("AGC_WASM_RUNTIME_OBJECT");
  return path && path[0] ? path : DEFAULT_RUNTIME_OBJECT;
}

typedef struct {
  unsigned char *data;
  size_t len;
  size_t cap;
  size_t max_len;
} buf_t;

typedef struct {
  const unsigned char *p;
  size_t len;
  size_t pos;
  const char *where;
} rd_t;

typedef struct {
  char *s;
  int len;
} str_t;

typedef struct {
  unsigned char *raw;
  size_t raw_len;
} type_t;

typedef struct {
  str_t name;
  int type_index;
  int defined;
  unsigned char *body;
  size_t body_len;
  size_t code_payload_off;
  int final_type;
  int final_index;
  int final_table_index;
} func_t;

typedef struct {
  str_t name;
  unsigned char *bytes;
  size_t size;
  size_t alloc_size;
  size_t data_payload_off;
  int align_log2;
  int defined;
  uint32_t final_addr;
} data_seg_t;

typedef struct {
  str_t name;
  int final_index;
} global_sym_t;

typedef struct {
  int kind;
  int flags;
  str_t name;
  int index;
  uint32_t data_offset;
  uint32_t data_size;
} symbol_t;

typedef struct {
  int type;
  uint32_t offset;
  uint32_t symbol;
  int32_t addend;
  int is_code;
} reloc_t;

typedef struct {
  str_t name;
  str_t signature;
} c_signature_t;

enum {
  AGC_DATA_FLAG_THREAD_LOCAL = 1u << 0,
};

typedef struct {
  str_t name;
  str_t c_signature;
  str_t layout_signature;
  uint32_t flags;
  uint32_t requested_alignment;
  int has_object_properties;
} data_signature_t;

typedef struct {
  str_t name;
  uint32_t flags;
} function_flags_t;

typedef struct {
  str_t path;
  type_t *types;
  int type_count;
  int type_cap;
  int *type_map;
  func_t *funcs;
  int func_count;
  int func_cap;
  data_seg_t *data;
  int data_count;
  int data_cap;
  global_sym_t *globals;
  int global_count;
  int global_cap;
  symbol_t *symbols;
  int symbol_count;
  int symbol_cap;
  reloc_t *relocs;
  int reloc_count;
  int reloc_cap;
  c_signature_t *c_signatures;
  int c_signature_count;
  int c_signature_cap;
  c_signature_t *abi_layout_signatures;
  int abi_layout_signature_count;
  int abi_layout_signature_cap;
  int has_abi_layout_section;
  uint32_t abi_layout_version;
  data_signature_t *data_signatures;
  int data_signature_count;
  int data_signature_cap;
  int has_data_signature_section;
  uint32_t data_layout_version;
  function_flags_t *function_flags;
  int function_flag_count;
  int function_flag_cap;
  int has_function_flags;
  int code_section_index;
  int data_section_index;
  int imports_table;
  str_t continuation_entry;
  str_t continuation_condition;
  str_t continuation_step;
  str_t continuation_start;
  str_t continuation_resume;
  str_t continuation_status;
  str_t continuation_result;
  int has_continuation;
} object_t;

typedef struct {
  object_t *obj;
  int func_index;
  str_t name;
  int type_index;
  int final_index;
} final_import_t;

typedef struct {
  object_t *obj;
  int func_index;
} final_func_t;

typedef struct {
  object_t *obj;
  int func_index;
  int final_func_index;
  int table_index;
} final_table_func_t;

typedef struct {
  object_t *obj;
  int data_index;
} final_data_t;

typedef struct {
  str_t name;
  int final_index;
  uint32_t init_value;
} final_global_t;

typedef struct {
  str_t name;
  int func_index;
} export_func_t;

typedef struct {
  const char *name;
  const char *signature;
} export_spec_t;

enum {
  LINK_OPT_MAX_MEMORY = 1u << 0,
  LINK_OPT_MAX_TABLE = 1u << 1,
  LINK_OPT_STDIO_WRITE_IMPORT = 1u << 2,
};

typedef struct {
  uint32_t flags;
  uint32_t initial_memory_pages;
  uint32_t maximum_memory_pages;
  uint32_t stack_size;
  uint32_t maximum_table_elements;
  uint64_t stdio_write_import_module_addr;
  uint64_t stdio_write_import_name_addr;
} linker_options_t;

static linker_options_t default_linker_options(void) {
  linker_options_t options = {0};
  options.initial_memory_pages = 1024;
  return options;
}

static void die(const char *msg) {
  fprintf(stderr, "ag_wasm_link: %s\n", msg);
  exit(1);
}

static void dief(const char *fmt, const char *arg) {
  fprintf(stderr, "ag_wasm_link: ");
  fprintf(stderr, fmt, arg);
  fprintf(stderr, "\n");
  exit(1);
}

static void die_link_diagnostic_one_object(
    const char *code, str_t subject, int object_index) {
  fprintf(stderr, "ag_wasm_link: AGC_LINK_DIAGNOSTIC\t%s\t%.*s\t%d\n",
          code, subject.len, subject.s, object_index);
  exit(1);
}

static void die_link_diagnostic_two_objects(
    const char *code, str_t subject,
    int first_object_index, int second_object_index) {
  fprintf(stderr, "ag_wasm_link: AGC_LINK_DIAGNOSTIC\t%s\t%.*s\t%d\t%d\n",
          code, subject.len, subject.s,
          first_object_index, second_object_index);
  exit(1);
}

static void die_link_diagnostic_missing_export(
    const char *export_name, int is_signed) {
  fprintf(stderr,
          "ag_wasm_link: AGC_LINK_DIAGNOSTIC\tAGC_LINK_MISSING_EXPORT\t%s\t%d\n",
          export_name, is_signed ? 1 : 0);
  exit(1);
}

static void *xmalloc(size_t n) {
  void *p = malloc(n ? n : 1);
  if (!p) die("out of memory");
  return p;
}

static void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n ? n : 1);
  if (!q) die("out of memory");
  return q;
}

static str_t str_dup(const char *s, int len) {
  str_t out;
  out.s = xmalloc((size_t)len + 1);
  memcpy(out.s, s, (size_t)len);
  out.s[len] = '\0';
  out.len = len;
  return out;
}

static int str_eq(str_t a, str_t b) {
  return a.len == b.len && a.s && b.s && memcmp(a.s, b.s, (size_t)a.len) == 0;
}

static int str_empty(str_t s) {
  return s.len == 0 || !s.s;
}

static void buf_reserve(buf_t *b, size_t add) {
  if (add > SIZE_MAX - b->len) die("Wasm output size overflow");
  if (b->max_len && b->len + add > b->max_len) {
    die("AGC_LIMIT_MAX_LINKED_WASM_BYTES: linked Wasm byte limit exceeded");
  }
  if (b->len + add <= b->cap) return;
  size_t cap = b->cap
      ? (b->cap > SIZE_MAX / 2 ? SIZE_MAX : b->cap * 2)
      : 256;
  while (cap < b->len + add && cap <= SIZE_MAX / 2) cap *= 2;
  if (cap < b->len + add) cap = b->len + add;
  if (b->max_len && cap > b->max_len) cap = b->max_len;
  b->data = xrealloc(b->data, cap);
  b->cap = cap;
}

static void buf_u8(buf_t *b, unsigned v) {
  buf_reserve(b, 1);
  b->data[b->len++] = (unsigned char)v;
}

static void buf_bytes(buf_t *b, const void *p, size_t n) {
  if (!n) return;
  buf_reserve(b, n);
  memcpy(b->data + b->len, p, n);
  b->len += n;
}

static void buf_u32le(buf_t *b, uint32_t v) {
  buf_u8(b, v & 0xff);
  buf_u8(b, (v >> 8) & 0xff);
  buf_u8(b, (v >> 16) & 0xff);
  buf_u8(b, (v >> 24) & 0xff);
}

static void buf_uleb(buf_t *b, uint32_t v) {
  do {
    unsigned char c = (unsigned char)(v & 0x7f);
    v >>= 7;
    if (v) c |= 0x80;
    buf_u8(b, c);
  } while (v);
}

static size_t buf_uleb5(buf_t *b, uint32_t v) {
  size_t off = b->len;
  for (int i = 0; i < 5; i++) {
    unsigned char c = (unsigned char)(v & 0x7f);
    v >>= 7;
    if (i != 4) c |= 0x80;
    buf_u8(b, c);
  }
  return off;
}

static void buf_sleb_i32(buf_t *b, int32_t v) {
  int more = 1;
  while (more) {
    unsigned char c = (unsigned char)(v & 0x7f);
    int sign = c & 0x40;
    uint32_t shifted = (uint32_t)v >> 7;
    if (v < 0) shifted |= (~(uint32_t)0) << (32 - 7);
    v = (int32_t)shifted;
    more = !((v == 0 && !sign) || (v == -1 && sign));
    if (more) c |= 0x80;
    buf_u8(b, c);
  }
}

static void buf_str(buf_t *b, str_t s) {
  buf_uleb(b, (uint32_t)s.len);
  buf_bytes(b, s.s, (size_t)s.len);
}

static void emit_section(buf_t *out, int id, buf_t *payload) {
  buf_u8(out, (unsigned)id);
  buf_uleb(out, (uint32_t)payload->len);
  buf_bytes(out, payload->data, payload->len);
}

static uint32_t rd_uleb(rd_t *r) {
  uint32_t v = 0;
  int shift = 0;
  for (;;) {
    if (r->pos >= r->len) dief("truncated %s", r->where);
    unsigned char c = r->p[r->pos++];
    v |= (uint32_t)(c & 0x7f) << shift;
    if (!(c & 0x80)) return v;
    shift += 7;
    if (shift > 35) dief("bad uleb in %s", r->where);
  }
}

static int32_t rd_sleb(rd_t *r) {
  int32_t v = 0;
  int shift = 0;
  unsigned char c = 0;
  do {
    if (r->pos >= r->len) dief("truncated %s", r->where);
    c = r->p[r->pos++];
    v |= (int32_t)(c & 0x7f) << shift;
    shift += 7;
  } while (c & 0x80);
  if ((shift < 32) && (c & 0x40)) v |= -((int32_t)1 << shift);
  return v;
}

static str_t rd_str_dup(rd_t *r) {
  uint32_t n = rd_uleb(r);
  if (r->pos + n > r->len) dief("truncated string in %s", r->where);
  str_t s = str_dup((const char *)r->p + r->pos, (int)n);
  r->pos += n;
  return s;
}

static void rd_skip(rd_t *r, size_t n) {
  if (r->pos + n > r->len) dief("truncated %s", r->where);
  r->pos += n;
}

static unsigned char *rd_bytes_dup(rd_t *r, size_t n) {
  if (r->pos + n > r->len) dief("truncated %s", r->where);
  unsigned char *p = xmalloc(n);
  memcpy(p, r->p + r->pos, n);
  r->pos += n;
  return p;
}

static void patch_uleb5(unsigned char *p, uint32_t v) {
  for (int i = 0; i < 5; i++) {
    unsigned char c = (unsigned char)(v & 0x7f);
    v >>= 7;
    if (i != 4) c |= 0x80;
    p[i] = c;
  }
}

static void patch_u32le(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v & 0xff);
  p[1] = (unsigned char)((v >> 8) & 0xff);
  p[2] = (unsigned char)((v >> 16) & 0xff);
  p[3] = (unsigned char)((v >> 24) & 0xff);
}

#define PUSH(arr, count, cap, val) do { \
  if ((count) == (cap)) { \
    (cap) = (cap) ? (cap) * 2 : 16; \
    (arr) = xrealloc((arr), (size_t)(cap) * sizeof(*(arr))); \
  } \
  (arr)[(count)++] = (val); \
} while (0)

static int push_type_copy(type_t **arr, int *count, int *cap, const unsigned char *raw, size_t raw_len) {
  if (*count == *cap) {
    *cap = *cap ? *cap * 2 : 16;
    *arr = xrealloc(*arr, (size_t)*cap * sizeof(**arr));
  }
  type_t *slot = &(*arr)[*count];
  slot->raw_len = raw_len;
  slot->raw = xmalloc(raw_len);
  memcpy(slot->raw, raw, raw_len);
  return (*count)++;
}

static unsigned char *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) dief("failed to open %s", path);
  if (fseek(f, 0, SEEK_END) != 0) dief("failed to seek %s", path);
  long n = ftell(f);
  if (n < 0) dief("failed to tell %s", path);
  rewind(f);
  unsigned char *p = xmalloc((size_t)n);
  if (fread(p, 1, (size_t)n, f) != (size_t)n) dief("failed to read %s", path);
  fclose(f);
  *out_len = (size_t)n;
  return p;
}

static void parse_type_section(object_t *o, rd_t sec) {
  uint32_t n = rd_uleb(&sec);
  for (uint32_t i = 0; i < n; i++) {
    size_t start = sec.pos;
    if (sec.pos >= sec.len || sec.p[sec.pos++] != 0x60) die("unsupported non-function type");
    uint32_t np = rd_uleb(&sec);
    rd_skip(&sec, np);
    uint32_t nr = rd_uleb(&sec);
    rd_skip(&sec, nr);
    push_type_copy(&o->types, &o->type_count, &o->type_cap, sec.p + start, sec.pos - start);
  }
}

static void parse_import_section(object_t *o, rd_t sec) {
  uint32_t n = rd_uleb(&sec);
  int func_index = 0;
  int global_index = 0;
  for (uint32_t i = 0; i < n; i++) {
    str_t module = rd_str_dup(&sec);
    str_t name = rd_str_dup(&sec);
    (void)module;
    if (sec.pos >= sec.len) die("truncated import kind");
    int kind = sec.p[sec.pos++];
    if (kind == 0) {
      func_t f = {0};
      f.name = name;
      f.type_index = (int)rd_uleb(&sec);
      f.defined = 0;
      PUSH(o->funcs, o->func_count, o->func_cap, f);
      func_index++;
    } else if (kind == 1) {
      o->imports_table = 1;
      rd_skip(&sec, 1);
      uint32_t flags = rd_uleb(&sec);
      rd_uleb(&sec);
      if (flags & 1) rd_uleb(&sec);
    } else if (kind == 2) {
      uint32_t flags = rd_uleb(&sec);
      rd_uleb(&sec);
      if (flags & 1) rd_uleb(&sec);
    } else if (kind == 3) {
      rd_skip(&sec, 2);
      global_sym_t g = {0};
      g.name = name;
      g.final_index = -1;
      PUSH(o->globals, o->global_count, o->global_cap, g);
      global_index++;
    } else {
      die("unsupported import kind");
    }
  }
  (void)func_index;
  (void)global_index;
}

static void parse_function_section(object_t *o, rd_t sec) {
  uint32_t n = rd_uleb(&sec);
  for (uint32_t i = 0; i < n; i++) {
    func_t f = {0};
    f.type_index = (int)rd_uleb(&sec);
    f.defined = 1;
    PUSH(o->funcs, o->func_count, o->func_cap, f);
  }
}

static void parse_code_section(object_t *o, rd_t sec, int noncustom_index) {
  o->code_section_index = noncustom_index;
  uint32_t n = rd_uleb(&sec);
  int first_defined = 0;
  while (first_defined < o->func_count && !o->funcs[first_defined].defined) first_defined++;
  for (uint32_t i = 0; i < n; i++) {
    size_t size_leb_start = sec.pos;
    uint32_t body_size = rd_uleb(&sec);
    size_t body_start = sec.pos;
    if (body_start + body_size > sec.len) die("truncated code body");
    int fi = first_defined + (int)i;
    if (fi >= o->func_count) die("code/function section mismatch");
    o->funcs[fi].code_payload_off = body_start;
    o->funcs[fi].body_len = body_size;
    o->funcs[fi].body = xmalloc(body_size);
    memcpy(o->funcs[fi].body, sec.p + body_start, body_size);
    sec.pos = body_start + body_size;
    (void)size_leb_start;
  }
}

static void parse_data_section(object_t *o, rd_t sec, int noncustom_index) {
  o->data_section_index = noncustom_index;
  uint32_t n = rd_uleb(&sec);
  for (uint32_t i = 0; i < n; i++) {
    if (sec.pos >= sec.len) die("truncated data segment");
    int flags = sec.p[sec.pos++];
    if (flags != 0) die("only active memory-0 data segments are supported");
    if (sec.pos >= sec.len || sec.p[sec.pos++] != 0x41) die("unsupported data offset expr");
    (void)rd_uleb(&sec);
    if (sec.pos >= sec.len || sec.p[sec.pos++] != 0x0b) die("unsupported data offset expr");
    uint32_t sz = rd_uleb(&sec);
    data_seg_t d = {0};
    d.defined = 1;
    d.align_log2 = 0;
    d.data_payload_off = sec.pos;
    d.size = sz;
    d.bytes = rd_bytes_dup(&sec, sz);
    PUSH(o->data, o->data_count, o->data_cap, d);
  }
}

static void parse_linking_section(object_t *o, rd_t sec) {
  (void)rd_uleb(&sec); /* version */
  while (sec.pos < sec.len) {
    int sub = sec.p[sec.pos++];
    uint32_t sub_size = rd_uleb(&sec);
    if (sec.pos + sub_size > sec.len) die("truncated linking subsection");
    rd_t ss = {sec.p + sec.pos, sub_size, 0, "linking subsection"};
    sec.pos += sub_size;
    if (sub == LINK_SYMBOL_TABLE) {
      uint32_t n = rd_uleb(&ss);
      for (uint32_t i = 0; i < n; i++) {
        symbol_t sym = {0};
        if (ss.pos >= ss.len) die("truncated symbol table");
        sym.kind = ss.p[ss.pos++];
        sym.flags = (int)rd_uleb(&ss);
        if (sym.kind == SYM_FUNCTION) {
          sym.index = (int)rd_uleb(&ss);
          sym.name = rd_str_dup(&ss);
          if (sym.index >= 0 && sym.index < o->func_count && !o->funcs[sym.index].name.s) {
            o->funcs[sym.index].name = str_dup(sym.name.s, sym.name.len);
          }
        } else if (sym.kind == SYM_DATA) {
          sym.name = rd_str_dup(&ss);
          if (!(sym.flags & SYM_UNDEFINED)) {
            int seg = (int)rd_uleb(&ss);
            uint32_t off = rd_uleb(&ss);
            uint32_t size = rd_uleb(&ss);
            sym.index = seg;
            sym.data_offset = off;
            sym.data_size = size;
            if (seg >= 0 && seg < o->data_count && !o->data[seg].name.s) {
              o->data[seg].name = str_dup(sym.name.s, sym.name.len);
            }
            if (seg >= 0 && seg < o->data_count) {
              size_t need = (size_t)off + (size_t)size;
              if (need > o->data[seg].alloc_size) o->data[seg].alloc_size = need;
            }
          } else {
            data_seg_t d = {0};
            d.name = str_dup(sym.name.s, sym.name.len);
            d.defined = 0;
            sym.index = o->data_count;
            PUSH(o->data, o->data_count, o->data_cap, d);
          }
        } else if (sym.kind == SYM_GLOBAL) {
          sym.index = (int)rd_uleb(&ss);
          sym.name = rd_str_dup(&ss);
          if (sym.index >= 0 && sym.index < o->global_count && !o->globals[sym.index].name.s) {
            o->globals[sym.index].name = str_dup(sym.name.s, sym.name.len);
          }
        } else {
          die("unsupported symbol kind");
        }
        PUSH(o->symbols, o->symbol_count, o->symbol_cap, sym);
      }
    } else if (sub == LINK_SEGMENT_INFO) {
      uint32_t n = rd_uleb(&ss);
      for (uint32_t i = 0; i < n; i++) {
        str_t name = rd_str_dup(&ss);
        int align = (int)rd_uleb(&ss);
        (void)rd_uleb(&ss);
        if ((int)i < o->data_count) {
          if (!o->data[i].name.s) o->data[i].name = str_dup(name.s, name.len);
          o->data[i].align_log2 = align;
        }
      }
    }
  }
}

static void parse_reloc_section(object_t *o, rd_t sec, int is_code) {
  int target = (int)rd_uleb(&sec);
  int expected = is_code ? o->code_section_index : o->data_section_index;
  if (expected < 0) die(is_code ? "reloc.CODE without Code section" : "reloc.DATA without Data section");
  if (target != expected) die(is_code ? "reloc.CODE targets wrong section" : "reloc.DATA targets wrong section");
  uint32_t n = rd_uleb(&sec);
  for (uint32_t i = 0; i < n; i++) {
    reloc_t r = {0};
    r.is_code = is_code;
    r.type = (int)rd_uleb(&sec);
    r.offset = rd_uleb(&sec);
    r.symbol = rd_uleb(&sec);
    if (r.type == R_WASM_MEMORY_ADDR_LEB || r.type == R_WASM_MEMORY_ADDR_I32) {
      r.addend = rd_sleb(&sec);
    }
    PUSH(o->relocs, o->reloc_count, o->reloc_cap, r);
  }
}

static void parse_c_signature_section(object_t *o, rd_t sec) {
  uint32_t version = rd_uleb(&sec);
  if (version != 1) die("unsupported agc.c_signature version");
  uint32_t count = rd_uleb(&sec);
  for (uint32_t i = 0; i < count; i++) {
    c_signature_t entry = {rd_str_dup(&sec), rd_str_dup(&sec)};
    if (str_empty(entry.name) || str_empty(entry.signature))
      die("invalid agc.c_signature entry");
    for (int prev = 0; prev < o->c_signature_count; prev++) {
      if (str_eq(o->c_signatures[prev].name, entry.name))
        dief("duplicate C signature metadata: %s", entry.name.s);
    }
    PUSH(o->c_signatures, o->c_signature_count, o->c_signature_cap, entry);
  }
  if (sec.pos != sec.len) die("trailing bytes in agc.c_signature section");
}

static void parse_abi_layout_section(object_t *o, rd_t sec) {
  if (o->has_abi_layout_section)
    die("duplicate agc.abi_layout section");
  uint32_t version = rd_uleb(&sec);
  if (version != 1 && version != 2 && version != 3)
    die("unsupported agc.abi_layout version");
  uint32_t count = rd_uleb(&sec);
  for (uint32_t i = 0; i < count; i++) {
    c_signature_t entry = {
        rd_str_dup(&sec), rd_str_dup(&sec)};
    if (str_empty(entry.name) ||
        str_empty(entry.signature))
      die("invalid agc.abi_layout entry");
    for (int previous = 0;
         previous < o->abi_layout_signature_count;
         previous++) {
      if (str_eq(
              o->abi_layout_signatures[previous].name,
              entry.name))
        dief(
            "duplicate ABI layout metadata: %s",
            entry.name.s);
    }
    PUSH(
        o->abi_layout_signatures,
        o->abi_layout_signature_count,
        o->abi_layout_signature_cap, entry);
  }
  if (sec.pos != sec.len)
    die("trailing bytes in agc.abi_layout section");
  o->has_abi_layout_section = 1;
  o->abi_layout_version = version;
}

static void parse_data_signature_section(object_t *o, rd_t sec) {
  if (o->has_data_signature_section)
    die("duplicate agc.data_signature section");
  uint32_t version = rd_uleb(&sec);
  if (version != 1 && version != 2)
    die("unsupported agc.data_signature version");
  uint32_t layout_version = rd_uleb(&sec);
  if (layout_version != 3)
    die("unsupported agc.data_signature layout version");
  uint32_t count = rd_uleb(&sec);
  for (uint32_t index = 0; index < count; index++) {
    data_signature_t entry = {
        .name = rd_str_dup(&sec),
        .c_signature = rd_str_dup(&sec),
        .layout_signature = rd_str_dup(&sec),
    };
    if (version >= 2) {
      entry.flags = rd_uleb(&sec);
      entry.requested_alignment = rd_uleb(&sec);
      entry.has_object_properties = 1;
    }
    if (str_empty(entry.name) ||
        str_empty(entry.c_signature) ||
        str_empty(entry.layout_signature) ||
        (entry.flags & ~AGC_DATA_FLAG_THREAD_LOCAL) ||
        (entry.requested_alignment != 0 &&
         (entry.requested_alignment &
          (entry.requested_alignment - 1)) != 0))
      die("invalid agc.data_signature entry");
    for (int previous = 0;
         previous < o->data_signature_count; previous++) {
      if (str_eq(
              o->data_signatures[previous].name,
              entry.name))
        dief(
            "duplicate data signature metadata: %s",
            entry.name.s);
    }
    PUSH(
        o->data_signatures,
        o->data_signature_count,
        o->data_signature_cap, entry);
  }
  if (sec.pos != sec.len)
    die("trailing bytes in agc.data_signature section");
  o->has_data_signature_section = 1;
  o->data_layout_version = layout_version;
}

static void parse_function_flags_section(object_t *o, rd_t sec) {
  if (o->has_function_flags)
    die("duplicate agc.function_flags section");
  uint32_t version = rd_uleb(&sec);
  if (version != 1)
    die("unsupported agc.function_flags version");
  uint32_t count = rd_uleb(&sec);
  for (uint32_t i = 0; i < count; i++) {
    function_flags_t entry = {rd_str_dup(&sec), rd_uleb(&sec)};
    if (str_empty(entry.name) || entry.flags == 0 ||
        (entry.flags &
         ~AGC_FUNCTION_FLAG_PARAMETERS_UNSPECIFIED) != 0)
      die("invalid agc.function_flags entry");
    for (int prev = 0; prev < o->function_flag_count; prev++) {
      if (str_eq(o->function_flags[prev].name, entry.name))
        dief("duplicate function flags metadata: %s", entry.name.s);
    }
    PUSH(
        o->function_flags, o->function_flag_count,
        o->function_flag_cap, entry);
  }
  if (sec.pos != sec.len)
    die("trailing bytes in agc.function_flags section");
  o->has_function_flags = 1;
}

static void validate_function_flags_metadata(const object_t *o) {
  for (int flag_index = 0;
       flag_index < o->function_flag_count; flag_index++) {
    str_t name = o->function_flags[flag_index].name;
    int has_function = 0;
    int has_c_signature = 0;
    for (int function_index = 0;
         function_index < o->func_count; function_index++) {
      if (str_eq(o->funcs[function_index].name, name)) {
        has_function = 1;
        break;
      }
    }
    for (int signature_index = 0;
         signature_index < o->c_signature_count;
         signature_index++) {
      if (str_eq(o->c_signatures[signature_index].name, name)) {
        has_c_signature = 1;
        break;
      }
    }
    if (!has_function || !has_c_signature)
      dief("orphan function flags metadata: %s", name.s);
  }
}

static void validate_abi_layout_metadata(const object_t *o) {
  for (int layout_index = 0;
       layout_index < o->abi_layout_signature_count;
       layout_index++) {
    str_t name = o->abi_layout_signatures[layout_index].name;
    int has_function = 0;
    int has_c_signature = 0;
    for (int function_index = 0;
         function_index < o->func_count; function_index++) {
      if (str_eq(o->funcs[function_index].name, name)) {
        has_function = 1;
        break;
      }
    }
    for (int signature_index = 0;
         signature_index < o->c_signature_count;
         signature_index++) {
      if (str_eq(o->c_signatures[signature_index].name, name)) {
        has_c_signature = 1;
        break;
      }
    }
    if (!has_function || !has_c_signature)
      dief("orphan ABI layout metadata: %s", name.s);
  }
}

static void validate_data_signature_metadata(const object_t *o) {
  for (int signature_index = 0;
       signature_index < o->data_signature_count;
       signature_index++) {
    str_t name = o->data_signatures[signature_index].name;
    int has_data_symbol = 0;
    for (int symbol_index = 0;
         symbol_index < o->symbol_count; symbol_index++) {
      const symbol_t *symbol = &o->symbols[symbol_index];
      if (symbol->kind == SYM_DATA &&
          !(symbol->flags & SYM_BINDING_LOCAL) &&
          str_eq(symbol->name, name)) {
        has_data_symbol = 1;
        break;
      }
    }
    if (!has_data_symbol)
      dief("orphan data signature metadata: %s", name.s);
  }
}

static void parse_continuation_section(object_t *o, rd_t sec) {
  if (o->has_continuation) die("duplicate agc.continuation section");
  uint32_t version = rd_uleb(&sec);
  if (version != 1) die("unsupported agc.continuation version");
  o->continuation_entry = rd_str_dup(&sec);
  o->continuation_condition = rd_str_dup(&sec);
  o->continuation_step = rd_str_dup(&sec);
  o->continuation_start = rd_str_dup(&sec);
  o->continuation_resume = rd_str_dup(&sec);
  o->continuation_status = rd_str_dup(&sec);
  o->continuation_result = rd_str_dup(&sec);
  if (str_empty(o->continuation_entry) ||
      str_empty(o->continuation_condition) ||
      str_empty(o->continuation_step) ||
      str_empty(o->continuation_start) ||
      str_empty(o->continuation_resume) ||
      str_empty(o->continuation_status) ||
      str_empty(o->continuation_result))
    die("invalid agc.continuation metadata");
  if (sec.pos != sec.len)
    die("trailing bytes in agc.continuation section");
  o->has_continuation = 1;
}

static object_t parse_object_bytes(const char *path, const unsigned char *file, size_t len) {
  if (len < 8 || memcmp(file, "\0asm", 4) != 0) dief("not a wasm object: %s", path);
  object_t o;
  memset(&o, 0, sizeof(o));
  o.path = str_dup(path, (int)strlen(path));
  o.code_section_index = -1;
  o.data_section_index = -1;
  rd_t r = {file, len, 8, path};
  int noncustom_index = 0;
  while (r.pos < r.len) {
    int id = r.p[r.pos++];
    uint32_t sz = rd_uleb(&r);
    if (r.pos + sz > r.len) dief("truncated section in %s", path);
    rd_t sec = {r.p + r.pos, sz, 0, path};
    r.pos += sz;
    int section_index = noncustom_index;
    if (id != SEC_CUSTOM) noncustom_index++;
    if (id == SEC_TYPE) parse_type_section(&o, sec);
    else if (id == SEC_IMPORT) parse_import_section(&o, sec);
    else if (id == SEC_FUNCTION) parse_function_section(&o, sec);
    else if (id == SEC_CODE) parse_code_section(&o, sec, section_index);
    else if (id == SEC_DATA) parse_data_section(&o, sec, section_index);
    else if (id == SEC_CUSTOM) {
      str_t name = rd_str_dup(&sec);
      if (name.len == 7 && memcmp(name.s, "linking", 7) == 0) {
        parse_linking_section(&o, sec);
      } else if (name.len == 10 && memcmp(name.s, "reloc.CODE", 10) == 0) {
        parse_reloc_section(&o, sec, 1);
      } else if (name.len == 10 && memcmp(name.s, "reloc.DATA", 10) == 0) {
        parse_reloc_section(&o, sec, 0);
      } else if (name.len == 15 && memcmp(name.s, "agc.c_signature", 15) == 0) {
        parse_c_signature_section(&o, sec);
      } else if (name.len == 14 &&
                 memcmp(name.s, "agc.abi_layout", 14) == 0) {
        parse_abi_layout_section(&o, sec);
      } else if (name.len == 18 &&
                 memcmp(name.s, "agc.data_signature", 18) == 0) {
        parse_data_signature_section(&o, sec);
      } else if (name.len == 18 && memcmp(name.s, "agc.function_flags", 18) == 0) {
        parse_function_flags_section(&o, sec);
      } else if (name.len == 16 && memcmp(name.s, "agc.continuation", 16) == 0) {
        parse_continuation_section(&o, sec);
      }
    }
  }
  validate_function_flags_metadata(&o);
  validate_abi_layout_metadata(&o);
  validate_data_signature_metadata(&o);
  return o;
}

static object_t parse_object(const char *path) {
  size_t len = 0;
  unsigned char *file = read_file(path, &len);
  object_t o = parse_object_bytes(path, file, len);
  free(file);
  return o;
}

static int intern_type(type_t **types, int *count, int *cap, const type_t *t) {
  for (int i = 0; i < *count; i++) {
    if ((*types)[i].raw_len == t->raw_len &&
        memcmp((*types)[i].raw, t->raw, t->raw_len) == 0) {
      return i;
    }
  }
  return push_type_copy(types, count, cap, t->raw, t->raw_len);
}

static void build_object_type_map(object_t *o, type_t **types, int *count, int *cap) {
  if (o->type_map) return;
  o->type_map = xmalloc((size_t)o->type_count * sizeof(*o->type_map));
  for (int i = 0; i < o->type_count; i++) {
    o->type_map[i] = intern_type(types, count, cap, &o->types[i]);
  }
}

static int type_equal(const type_t *a, const type_t *b) {
  return a->raw_len == b->raw_len && memcmp(a->raw, b->raw, a->raw_len) == 0;
}

static unsigned char wasm_type_result_valtype(const type_t *t);

static const c_signature_t *find_c_signature(
    const object_t *obj, str_t name);
static const c_signature_t *find_abi_layout_signature(
    const object_t *obj, str_t name);
static const data_signature_t *find_data_signature(
    const object_t *obj, str_t name);

static uint32_t find_function_flags(
    const object_t *obj, str_t name) {
  for (int index = 0; index < obj->function_flag_count; index++) {
    if (str_eq(obj->function_flags[index].name, name))
      return obj->function_flags[index].flags;
  }
  return 0;
}

typedef struct {
  int end;
  int has_compatible_integer;
  char integer_kind;
  uint32_t integer_bits;
} canonical_enum_type_t;

typedef struct {
  uint32_t bound;
  int element_start;
} canonical_array_type_t;

typedef struct {
  char kind;
  str_t tag;
  int end;
  int has_shape;
  int is_complete;
  int body_start;
  int body_length;
  uint32_t member_count;
} canonical_record_type_t;

typedef struct {
  str_t name;
  str_t bit_descriptor;
  int has_alignment_specifier;
  uint32_t requested_alignment;
  str_t type;
} canonical_record_member_t;

static int canonical_decimal(
    str_t signature, int *offset, uint32_t *value) {
  if (!offset || !value || *offset < 0 ||
      *offset >= signature.len ||
      signature.s[*offset] < '0' ||
      signature.s[*offset] > '9')
    return 0;
  uint32_t result = 0;
  do {
    uint32_t digit =
        (uint32_t)(signature.s[*offset] - '0');
    if (result > (UINT32_MAX - digit) / 10u) return 0;
    result = result * 10u + digit;
    (*offset)++;
  } while (
      *offset < signature.len &&
      signature.s[*offset] >= '0' &&
      signature.s[*offset] <= '9');
  *value = result;
  return 1;
}

static int canonical_integer_type_at(
    str_t signature, int offset, char *kind,
    uint32_t *bits, int *end) {
  if (offset < 0 || offset >= signature.len ||
      (signature.s[offset] != 'i' &&
       signature.s[offset] != 'u'))
    return 0;
  char parsed_kind = signature.s[offset++];
  uint32_t parsed_bits = 0;
  if (!canonical_decimal(
          signature, &offset, &parsed_bits))
    return 0;
  if (kind) *kind = parsed_kind;
  if (bits) *bits = parsed_bits;
  if (end) *end = offset;
  return 1;
}

static int canonical_enum_type_at(
    str_t signature, int offset,
    canonical_enum_type_t *parsed) {
  if (!parsed || offset < 0 ||
      offset + 2 > signature.len ||
      signature.s[offset] != 'e')
    return 0;
  canonical_enum_type_t result = {0};
  int index = offset + 2;
  if (signature.s[offset + 1] == '<') {
    if (!canonical_integer_type_at(
            signature, index, &result.integer_kind,
            &result.integer_bits, &index) ||
        index >= signature.len ||
        signature.s[index] != '>')
      return 0;
    result.end = index + 1;
    result.has_compatible_integer = 1;
    *parsed = result;
    return 1;
  }
  if (signature.s[offset + 1] != '{') return 0;
  uint32_t name_length = 0;
  if (!canonical_decimal(
          signature, &index, &name_length) ||
      index >= signature.len ||
      signature.s[index++] != ':' ||
      name_length > (uint32_t)(signature.len - index))
    return 0;
  index += (int)name_length;
  if (index < signature.len &&
      signature.s[index] == '}') {
    result.end = index + 1;
    *parsed = result;
    return 1;
  }
  if (index >= signature.len ||
      signature.s[index++] != ':' ||
      !canonical_integer_type_at(
          signature, index, &result.integer_kind,
          &result.integer_bits, &index) ||
      index >= signature.len ||
      signature.s[index] != '}')
    return 0;
  result.end = index + 1;
  result.has_compatible_integer = 1;
  *parsed = result;
  return 1;
}

static int canonical_array_type_at(
    str_t signature, int offset,
    canonical_array_type_t *parsed) {
  if (!parsed || offset < 0 ||
      offset >= signature.len ||
      signature.s[offset] != 'a')
    return 0;
  canonical_array_type_t result = {0};
  int index = offset + 1;
  if (!canonical_decimal(
          signature, &index, &result.bound) ||
      index >= signature.len ||
      signature.s[index++] != '<')
    return 0;
  result.element_start = index;
  *parsed = result;
  return 1;
}

static int canonical_record_type_at(
    str_t signature, int offset,
    canonical_record_type_t *parsed) {
  if (!parsed || offset < 0 ||
      offset + 2 > signature.len ||
      (signature.s[offset] != 's' &&
       signature.s[offset] != 'u') ||
      signature.s[offset + 1] != '{')
    return 0;
  canonical_record_type_t result = {
      .kind = signature.s[offset],
  };
  int index = offset + 2;
  uint32_t tag_length = 0;
  if (!canonical_decimal(
          signature, &index, &tag_length) ||
      index >= signature.len ||
      signature.s[index++] != ':' ||
      tag_length > (uint32_t)(signature.len - index))
    return 0;
  result.tag = (str_t){
      signature.s + index, (int)tag_length};
  index += (int)tag_length;
  if (index >= signature.len ||
      signature.s[index++] != '}')
    return 0;
  result.end = index;
  if (index >= signature.len ||
      signature.s[index] != '[') {
    *parsed = result;
    return 1;
  }

  int body_start = index + 1;
  int header_index = body_start;
  uint32_t complete = 0;
  uint32_t member_count = 0;
  if (!canonical_decimal(
          signature, &header_index, &complete) ||
      complete > 1 ||
      header_index >= signature.len ||
      signature.s[header_index++] != ':' ||
      !canonical_decimal(
          signature, &header_index, &member_count))
    return 0;

  int depth = 1;
  int body_end = -1;
  for (int cursor = body_start;
       cursor < signature.len; cursor++) {
    if (signature.s[cursor] == '[') {
      depth++;
    } else if (signature.s[cursor] == ']') {
      depth--;
      if (depth == 0) {
        body_end = cursor;
        break;
      }
    }
  }
  if (body_end < 0) return 0;
  result.has_shape = 1;
  result.is_complete = complete != 0;
  result.body_start = body_start;
  result.body_length = body_end - body_start;
  result.member_count = member_count;
  result.end = body_end + 1;
  *parsed = result;
  return 1;
}

static int canonical_type_signatures_compatible(
    str_t left, str_t right);

static int canonical_record_body_members(
    str_t body, uint32_t expected_count,
    canonical_record_member_t **members_out) {
  if (!members_out) return 0;
  *members_out = NULL;
  int offset = 0;
  uint32_t complete = 0;
  uint32_t member_count = 0;
  if (!canonical_decimal(body, &offset, &complete) ||
      complete != 1 ||
      offset >= body.len ||
      body.s[offset++] != ':' ||
      !canonical_decimal(body, &offset, &member_count) ||
      member_count != expected_count ||
      member_count > (uint32_t)body.len ||
      (size_t)member_count >
          SIZE_MAX / sizeof(canonical_record_member_t))
    return 0;

  canonical_record_member_t *members = NULL;
  if (member_count > 0)
    members = xmalloc(
        (size_t)member_count * sizeof(*members));
  for (uint32_t index = 0; index < member_count; index++) {
    if (offset >= body.len || body.s[offset++] != '|') {
      free(members);
      return 0;
    }
    uint32_t name_length = 0;
    if (!canonical_decimal(
            body, &offset, &name_length) ||
        offset >= body.len ||
        body.s[offset++] != ':' ||
        name_length > (uint32_t)(body.len - offset)) {
      free(members);
      return 0;
    }
    members[index].name = (str_t){
        body.s + offset, (int)name_length};
    offset += (int)name_length;
    if (offset >= body.len || body.s[offset++] != ':') {
      free(members);
      return 0;
    }

    int bit_start = offset;
    if (offset < body.len && body.s[offset] == '-')
      offset++;
    int digit_start = offset;
    while (offset < body.len &&
           body.s[offset] >= '0' &&
           body.s[offset] <= '9')
      offset++;
    if (offset == digit_start ||
        offset >= body.len ||
        (body.s[offset] != 's' &&
         body.s[offset] != 'u')) {
      free(members);
      return 0;
    }
    offset++;
    members[index].bit_descriptor = (str_t){
        body.s + bit_start, offset - bit_start};
    if (offset >= body.len || body.s[offset++] != ':') {
      free(members);
      return 0;
    }

    if (offset >= body.len || body.s[offset++] != 'm') {
      free(members);
      return 0;
    }
    uint32_t has_alignment_specifier = 0;
    uint32_t requested_alignment = 0;
    if (!canonical_decimal(
            body, &offset, &has_alignment_specifier) ||
        has_alignment_specifier > 1 ||
        offset >= body.len ||
        body.s[offset++] != ':' ||
        !canonical_decimal(
            body, &offset, &requested_alignment) ||
        (!has_alignment_specifier &&
         requested_alignment != 0) ||
        offset >= body.len ||
        body.s[offset++] != ':') {
      free(members);
      return 0;
    }
    members[index].has_alignment_specifier =
        has_alignment_specifier != 0;
    members[index].requested_alignment =
        requested_alignment;

    int type_start = offset;
    int record_depth = 0;
    while (offset < body.len) {
      if (body.s[offset] == '[') {
        record_depth++;
      } else if (body.s[offset] == ']') {
        if (record_depth == 0) {
          free(members);
          return 0;
        }
        record_depth--;
      } else if (body.s[offset] == '|' &&
                 record_depth == 0) {
        break;
      }
      offset++;
    }
    if (record_depth != 0 || offset == type_start) {
      free(members);
      return 0;
    }
    members[index].type = (str_t){
        body.s + type_start, offset - type_start};
  }
  if (offset != body.len) {
    free(members);
    return 0;
  }
  *members_out = members;
  return 1;
}

static int canonical_union_members_correspond(
    const canonical_record_member_t *left,
    const canonical_record_member_t *right) {
  return left && right &&
         str_eq(left->name, right->name) &&
         str_eq(
             left->bit_descriptor,
             right->bit_descriptor) &&
         left->has_alignment_specifier ==
             right->has_alignment_specifier &&
         left->requested_alignment ==
             right->requested_alignment &&
         canonical_type_signatures_compatible(
             left->type, right->type);
}

static int canonical_union_augment_matching(
    uint32_t left_index,
    const canonical_record_member_t *left_members,
    uint32_t right_count,
    const canonical_record_member_t *right_members,
    int *right_to_left, unsigned char *visited) {
  for (uint32_t right_index = 0;
       right_index < right_count; right_index++) {
    if (visited[right_index] ||
        !canonical_union_members_correspond(
            &left_members[left_index],
            &right_members[right_index]))
      continue;
    visited[right_index] = 1;
    if (right_to_left[right_index] < 0 ||
        canonical_union_augment_matching(
            (uint32_t)right_to_left[right_index],
            left_members, right_count, right_members,
            right_to_left, visited)) {
      right_to_left[right_index] = (int)left_index;
      return 1;
    }
  }
  return 0;
}

static int canonical_union_bodies_compatible(
    str_t left, uint32_t left_count,
    str_t right, uint32_t right_count) {
  if (left_count != right_count ||
      (size_t)right_count > SIZE_MAX / sizeof(int))
    return 0;
  canonical_record_member_t *left_members = NULL;
  canonical_record_member_t *right_members = NULL;
  if (!canonical_record_body_members(
          left, left_count, &left_members) ||
      !canonical_record_body_members(
          right, right_count, &right_members)) {
    free(left_members);
    free(right_members);
    return 0;
  }
  int *right_to_left = NULL;
  unsigned char *visited = NULL;
  if (right_count > 0) {
    right_to_left = xmalloc(
        (size_t)right_count * sizeof(*right_to_left));
    visited = xmalloc((size_t)right_count);
    for (uint32_t index = 0; index < right_count; index++)
      right_to_left[index] = -1;
  }
  int compatible = 1;
  for (uint32_t left_index = 0;
       left_index < left_count && compatible;
       left_index++) {
    memset(visited, 0, (size_t)right_count);
    compatible = canonical_union_augment_matching(
        left_index, left_members,
        right_count, right_members,
        right_to_left, visited);
  }
  free(visited);
  free(right_to_left);
  free(left_members);
  free(right_members);
  return compatible;
}

static int canonical_type_signatures_compatible(
    str_t left, str_t right) {
  int left_offset = 0;
  int right_offset = 0;
  while (left_offset < left.len &&
         right_offset < right.len) {
    canonical_record_type_t left_record = {0};
    canonical_record_type_t right_record = {0};
    int left_is_record = canonical_record_type_at(
        left, left_offset, &left_record);
    int right_is_record = canonical_record_type_at(
        right, right_offset, &right_record);
    if (left_is_record || right_is_record) {
      if (!left_is_record || !right_is_record ||
          left_record.kind != right_record.kind ||
          !str_eq(left_record.tag, right_record.tag))
        return 0;
      if (left_record.has_shape &&
          right_record.has_shape &&
          left_record.is_complete &&
          right_record.is_complete) {
        str_t left_body = {
            left.s + left_record.body_start,
            left_record.body_length};
        str_t right_body = {
            right.s + right_record.body_start,
            right_record.body_length};
        if (left_record.kind == 'u') {
          if (!canonical_union_bodies_compatible(
                  left_body, left_record.member_count,
                  right_body, right_record.member_count))
            return 0;
        } else if (!canonical_type_signatures_compatible(
                       left_body, right_body)) {
          return 0;
        }
      }
      left_offset = left_record.end;
      right_offset = right_record.end;
      continue;
    }
    canonical_enum_type_t left_enum = {0};
    canonical_enum_type_t right_enum = {0};
    int left_is_enum = canonical_enum_type_at(
        left, left_offset, &left_enum);
    int right_is_enum = canonical_enum_type_at(
        right, right_offset, &right_enum);
    if (left_is_enum && right_is_enum) {
      int left_length = left_enum.end - left_offset;
      int right_length = right_enum.end - right_offset;
      if (left_length != right_length ||
          memcmp(
              left.s + left_offset,
              right.s + right_offset,
              (size_t)left_length) != 0)
        return 0;
      left_offset = left_enum.end;
      right_offset = right_enum.end;
      continue;
    }
    if (left_is_enum || right_is_enum) {
      canonical_enum_type_t enumeration =
          left_is_enum ? left_enum : right_enum;
      str_t integer_signature =
          left_is_enum ? right : left;
      int integer_offset =
          left_is_enum ? right_offset : left_offset;
      char integer_kind = 0;
      uint32_t integer_bits = 0;
      int integer_end = 0;
      if (!enumeration.has_compatible_integer ||
          !canonical_integer_type_at(
              integer_signature, integer_offset,
              &integer_kind, &integer_bits, &integer_end) ||
          enumeration.integer_kind != integer_kind ||
          enumeration.integer_bits != integer_bits)
        return 0;
      if (left_is_enum) {
        left_offset = left_enum.end;
        right_offset = integer_end;
      } else {
        left_offset = integer_end;
        right_offset = right_enum.end;
      }
      continue;
    }
    canonical_array_type_t left_array = {0};
    canonical_array_type_t right_array = {0};
    int left_is_array = canonical_array_type_at(
        left, left_offset, &left_array);
    int right_is_array = canonical_array_type_at(
        right, right_offset, &right_array);
    if (left_is_array || right_is_array) {
      if (!left_is_array || !right_is_array ||
          (left_array.bound != 0 &&
           right_array.bound != 0 &&
           left_array.bound != right_array.bound))
        return 0;
      left_offset = left_array.element_start;
      right_offset = right_array.element_start;
      continue;
    }
    if (left.s[left_offset] != right.s[right_offset])
      return 0;
    left_offset++;
    right_offset++;
  }
  return left_offset == left.len &&
         right_offset == right.len;
}

static int canonical_function_signature_parameter_list(
    str_t signature, int *parameter_list_offset, int *is_empty) {
  if (signature.len < 2 || signature.s[signature.len - 1] != ')')
    return 0;
  int depth = 0;
  for (int index = signature.len - 1; index >= 0; index--) {
    if (signature.s[index] == ')') {
      depth++;
    } else if (signature.s[index] == '(') {
      depth--;
      if (depth == 0) {
        if (parameter_list_offset)
          *parameter_list_offset = index;
        if (is_empty)
          *is_empty = index + 1 == signature.len - 1;
        return 1;
      }
      if (depth < 0) return 0;
    }
  }
  return 0;
}

static int canonical_parameter_unchanged_by_default_promotions(
    const char *parameter, int length) {
  int index = 0;
  int is_atomic = 0;
  while (index < length &&
         (parameter[index] == 'k' || parameter[index] == 'V' ||
          parameter[index] == 'A' || parameter[index] == 'R')) {
    if (parameter[index] == 'A') is_atomic = 1;
    index++;
  }
  if (index >= length ||
      (length - index == 3 &&
       memcmp(parameter + index, "...", 3) == 0))
    return 0;
  if (is_atomic) return 1;
  char kind = parameter[index++];
  if (kind == 'b' || kind == 'c')
    return 0;
  if (kind != 'f' && kind != 'i' && kind != 'u')
    return 1;
  if (kind == 'u' && index < length &&
      (parameter[index] == '{' || parameter[index] == '[' ||
       parameter[index] == 'l'))
    return 1;
  int bits = 0;
  int has_bits = 0;
  while (index < length &&
         parameter[index] >= '0' && parameter[index] <= '9') {
    has_bits = 1;
    bits = bits * 10 + parameter[index++] - '0';
  }
  if (!has_bits) return 1;
  return kind == 'f' ? bits >= 64 : bits >= 32;
}

static int canonical_parameters_unchanged_by_default_promotions(
    str_t signature, int parameter_list_offset) {
  int parameter_start = parameter_list_offset + 1;
  int depth = 0;
  for (int index = parameter_start; index < signature.len; index++) {
    char ch = signature.s[index];
    if (index == signature.len - 1) {
      if (depth != 0) return 0;
      return index == parameter_start ||
             canonical_parameter_unchanged_by_default_promotions(
                 signature.s + parameter_start,
                 index - parameter_start);
    }
    if (ch == '(' || ch == '<' || ch == '{' || ch == '[') {
      depth++;
    } else if (ch == ')' || ch == '>' || ch == '}' || ch == ']') {
      if (depth == 0) return 0;
      depth--;
    } else if (ch == ',' && depth == 0) {
      if (!canonical_parameter_unchanged_by_default_promotions(
              signature.s + parameter_start,
              index - parameter_start))
        return 0;
      parameter_start = index + 1;
    }
  }
  return 0;
}

static int unspecified_function_signature_matches(
    const c_signature_t *unspecified, uint32_t unspecified_flags,
    const type_t *unspecified_type,
    const c_signature_t *specified, const type_t *specified_type) {
  if (!(unspecified_flags &
        AGC_FUNCTION_FLAG_PARAMETERS_UNSPECIFIED))
    return 0;
  int unspecified_offset = 0;
  int specified_offset = 0;
  int unspecified_is_empty = 0;
  if (!canonical_function_signature_parameter_list(
          unspecified->signature, &unspecified_offset,
          &unspecified_is_empty) ||
      !canonical_function_signature_parameter_list(
          specified->signature, &specified_offset, NULL) ||
      !unspecified_is_empty ||
      !canonical_type_signatures_compatible(
          (str_t){
              unspecified->signature.s,
              unspecified_offset},
          (str_t){
              specified->signature.s,
              specified_offset}) ||
      !canonical_parameters_unchanged_by_default_promotions(
          specified->signature, specified_offset))
    return 0;
  return wasm_type_result_valtype(unspecified_type) ==
         wasm_type_result_valtype(specified_type);
}

static int abi_layout_decimal_equal(
    str_t left, int *left_offset,
    str_t right, int *right_offset) {
  uint32_t left_value = 0;
  uint32_t right_value = 0;
  return canonical_decimal(
             left, left_offset, &left_value) &&
         canonical_decimal(
             right, right_offset, &right_value) &&
         left_value == right_value;
}

static int abi_layout_signed_decimal_equal(
    str_t left, int *left_offset,
    str_t right, int *right_offset) {
  int left_negative =
      *left_offset < left.len &&
      left.s[*left_offset] == '-';
  int right_negative =
      *right_offset < right.len &&
      right.s[*right_offset] == '-';
  if (left_negative != right_negative) return 0;
  if (left_negative) {
    (*left_offset)++;
    (*right_offset)++;
  }
  return abi_layout_decimal_equal(
      left, left_offset, right, right_offset);
}

static int abi_layout_array_bound_compatible(
    str_t left, int *left_offset,
    str_t right, int *right_offset) {
  uint32_t left_bound = 0;
  uint32_t right_bound = 0;
  return canonical_decimal(
             left, left_offset, &left_bound) &&
         canonical_decimal(
             right, right_offset, &right_bound) &&
         (left_bound == right_bound ||
          left_bound == 0 || right_bound == 0);
}

static int abi_layout_skip_parameter_list(
    str_t signature, int *offset);
static int abi_layout_parameter_lists_compatible(
    str_t left, int *left_offset,
    str_t right, int *right_offset);

static int abi_layout_skip_type(
    str_t signature, int *offset) {
  if (!offset || *offset < 0 ||
      *offset >= signature.len)
    return 0;
  char kind = signature.s[(*offset)++];
  if (kind == '?' || kind == 'v' || kind == 'r')
    return 1;
  if (kind == 'c') {
    return abi_layout_skip_type(signature, offset) &&
           *offset < signature.len &&
           signature.s[(*offset)++] == '(' &&
           abi_layout_skip_parameter_list(
               signature, offset);
  }
  uint32_t ignored = 0;
  if (kind == 'x' || kind == 'f') {
    return canonical_decimal(
               signature, offset, &ignored) &&
           *offset < signature.len &&
           signature.s[(*offset)++] == ':' &&
           canonical_decimal(
               signature, offset, &ignored);
  }
  if (kind == 'p') {
    if (!canonical_decimal(
            signature, offset, &ignored) ||
        *offset >= signature.len ||
        signature.s[(*offset)++] != ':' ||
        !canonical_decimal(
            signature, offset, &ignored))
      return 0;
    if (*offset < signature.len &&
        signature.s[*offset] == '[') {
      (*offset)++;
      if (!abi_layout_skip_type(signature, offset) ||
          *offset >= signature.len ||
          signature.s[(*offset)++] != ']')
        return 0;
    }
    return 1;
  }
  if (kind == 'a') {
    return canonical_decimal(
               signature, offset, &ignored) &&
           *offset < signature.len &&
           signature.s[(*offset)++] == '[' &&
           abi_layout_skip_type(signature, offset) &&
           *offset < signature.len &&
           signature.s[(*offset)++] == ']';
  }
  if (kind != 's' && kind != 'u') return 0;
  for (int field = 0; field < 3; field++) {
    if (!canonical_decimal(
            signature, offset, &ignored))
      return 0;
    if (field < 2 &&
        (*offset >= signature.len ||
         signature.s[(*offset)++] != ':'))
      return 0;
  }
  if (*offset >= signature.len ||
      signature.s[(*offset)++] != '{')
    return 0;
  if (*offset < signature.len &&
      signature.s[*offset] == '}') {
    (*offset)++;
    return 1;
  }
  for (;;) {
    if (!canonical_decimal(
            signature, offset, &ignored) ||
        *offset >= signature.len ||
        signature.s[(*offset)++] != ':' ||
        !canonical_decimal(
            signature, offset, &ignored) ||
        *offset >= signature.len ||
        signature.s[(*offset)++] != ':')
      return 0;
    if (*offset < signature.len &&
        signature.s[*offset] == '-')
      (*offset)++;
    if (!canonical_decimal(
            signature, offset, &ignored) ||
        *offset >= signature.len ||
        signature.s[(*offset)++] != ':' ||
        !abi_layout_skip_type(signature, offset) ||
        *offset >= signature.len)
      return 0;
    char delimiter = signature.s[(*offset)++];
    if (delimiter == '}') return 1;
    if (delimiter != '|') return 0;
  }
}

static int abi_layout_types_compatible(
    str_t left, int *left_offset,
    str_t right, int *right_offset) {
  if (!left_offset || !right_offset ||
      *left_offset < 0 || *right_offset < 0 ||
      *left_offset >= left.len ||
      *right_offset >= right.len)
    return 0;
  if (left.s[*left_offset] == '?') {
    (*left_offset)++;
    return abi_layout_skip_type(right, right_offset);
  }
  if (right.s[*right_offset] == '?') {
    (*right_offset)++;
    return abi_layout_skip_type(left, left_offset);
  }
  char kind = left.s[(*left_offset)++];
  if (kind != right.s[(*right_offset)++]) return 0;
  if (kind == 'v' || kind == 'r') return 1;
  if (kind == 'c') {
    return abi_layout_types_compatible(
               left, left_offset,
               right, right_offset) &&
           *left_offset < left.len &&
           *right_offset < right.len &&
           left.s[(*left_offset)++] == '(' &&
           right.s[(*right_offset)++] == '(' &&
           abi_layout_parameter_lists_compatible(
               left, left_offset,
               right, right_offset);
  }
  if (kind == 'x' || kind == 'f') {
    return abi_layout_decimal_equal(
               left, left_offset,
               right, right_offset) &&
           *left_offset < left.len &&
           *right_offset < right.len &&
           left.s[(*left_offset)++] == ':' &&
           right.s[(*right_offset)++] == ':' &&
           abi_layout_decimal_equal(
               left, left_offset,
               right, right_offset);
  }
  if (kind == 'p') {
    if (!abi_layout_decimal_equal(
            left, left_offset,
            right, right_offset) ||
        *left_offset >= left.len ||
        *right_offset >= right.len ||
        left.s[(*left_offset)++] != ':' ||
        right.s[(*right_offset)++] != ':' ||
        !abi_layout_decimal_equal(
            left, left_offset,
            right, right_offset))
      return 0;
    int left_nested =
        *left_offset < left.len &&
        left.s[*left_offset] == '[';
    int right_nested =
        *right_offset < right.len &&
        right.s[*right_offset] == '[';
    if (left_nested != right_nested) return 0;
    if (!left_nested) return 1;
    (*left_offset)++;
    (*right_offset)++;
    return abi_layout_types_compatible(
               left, left_offset,
               right, right_offset) &&
           *left_offset < left.len &&
           *right_offset < right.len &&
           left.s[(*left_offset)++] == ']' &&
           right.s[(*right_offset)++] == ']';
  }
  if (kind == 'a') {
    if (!abi_layout_array_bound_compatible(
            left, left_offset,
            right, right_offset) ||
        *left_offset >= left.len ||
        *right_offset >= right.len ||
        left.s[(*left_offset)++] != '[' ||
        right.s[(*right_offset)++] != '[' ||
        !abi_layout_types_compatible(
            left, left_offset,
            right, right_offset) ||
        *left_offset >= left.len ||
        *right_offset >= right.len ||
        left.s[(*left_offset)++] != ']' ||
        right.s[(*right_offset)++] != ']')
      return 0;
    return 1;
  }
  if (kind != 's' && kind != 'u') return 0;
  for (int field = 0; field < 3; field++) {
    if (!abi_layout_decimal_equal(
            left, left_offset,
            right, right_offset))
      return 0;
    if (field < 2 &&
        (*left_offset >= left.len ||
         *right_offset >= right.len ||
         left.s[(*left_offset)++] != ':' ||
         right.s[(*right_offset)++] != ':'))
      return 0;
  }
  if (*left_offset >= left.len ||
      *right_offset >= right.len ||
      left.s[(*left_offset)++] != '{' ||
      right.s[(*right_offset)++] != '{')
    return 0;
  for (;;) {
    int left_end =
        *left_offset < left.len &&
        left.s[*left_offset] == '}';
    int right_end =
        *right_offset < right.len &&
        right.s[*right_offset] == '}';
    if (left_end || right_end) {
      if (left_end != right_end) return 0;
      (*left_offset)++;
      (*right_offset)++;
      return 1;
    }
    if (!abi_layout_decimal_equal(
            left, left_offset,
            right, right_offset) ||
        *left_offset >= left.len ||
        *right_offset >= right.len ||
        left.s[(*left_offset)++] != ':' ||
        right.s[(*right_offset)++] != ':' ||
        !abi_layout_decimal_equal(
            left, left_offset,
            right, right_offset) ||
        *left_offset >= left.len ||
        *right_offset >= right.len ||
        left.s[(*left_offset)++] != ':' ||
        right.s[(*right_offset)++] != ':' ||
        !abi_layout_signed_decimal_equal(
            left, left_offset,
            right, right_offset) ||
        *left_offset >= left.len ||
        *right_offset >= right.len ||
        left.s[(*left_offset)++] != ':' ||
        right.s[(*right_offset)++] != ':' ||
        !abi_layout_types_compatible(
            left, left_offset,
            right, right_offset) ||
        *left_offset >= left.len ||
        *right_offset >= right.len)
      return 0;
    char left_delimiter = left.s[(*left_offset)++];
    char right_delimiter = right.s[(*right_offset)++];
    if (left_delimiter != right_delimiter ||
        (left_delimiter != '|' &&
         left_delimiter != '}'))
      return 0;
    if (left_delimiter == '}') return 1;
  }
}

static int abi_layout_whole_parameter_wildcard(
    str_t signature, int offset) {
  return offset >= 0 &&
         offset + 1 < signature.len &&
         signature.s[offset] == '?' &&
         signature.s[offset + 1] == ')';
}

static int abi_layout_at_ellipsis(
    str_t signature, int offset) {
  return offset >= 0 && offset + 2 < signature.len &&
         signature.s[offset] == '.' &&
         signature.s[offset + 1] == '.' &&
         signature.s[offset + 2] == '.';
}

static int abi_layout_skip_parameter_list(
    str_t signature, int *offset) {
  if (!offset || *offset < 0 ||
      *offset >= signature.len)
    return 0;
  if (abi_layout_whole_parameter_wildcard(
          signature, *offset)) {
    *offset += 2;
    return 1;
  }
  if (signature.s[*offset] == ')') {
    (*offset)++;
    return 1;
  }
  for (;;) {
    if (abi_layout_at_ellipsis(signature, *offset)) {
      *offset += 3;
    } else if (!abi_layout_skip_type(
                   signature, offset)) {
      return 0;
    }
    if (*offset >= signature.len) return 0;
    char delimiter = signature.s[(*offset)++];
    if (delimiter == ')') return 1;
    if (delimiter != ',') return 0;
  }
}

static int abi_layout_parameter_lists_compatible(
    str_t left, int *left_offset,
    str_t right, int *right_offset) {
  if (!left_offset || !right_offset ||
      *left_offset < 0 || *right_offset < 0)
    return 0;
  if (abi_layout_whole_parameter_wildcard(
          left, *left_offset)) {
    *left_offset += 2;
    return abi_layout_skip_parameter_list(
        right, right_offset);
  }
  if (abi_layout_whole_parameter_wildcard(
          right, *right_offset)) {
    *right_offset += 2;
    return abi_layout_skip_parameter_list(
        left, left_offset);
  }
  for (;;) {
    if (*left_offset >= left.len ||
        *right_offset >= right.len)
      return 0;
    if (left.s[*left_offset] == ')' ||
        right.s[*right_offset] == ')') {
      if (left.s[(*left_offset)++] != ')' ||
          right.s[(*right_offset)++] != ')')
        return 0;
      return 1;
    }
    int left_ellipsis =
        abi_layout_at_ellipsis(left, *left_offset);
    int right_ellipsis =
        abi_layout_at_ellipsis(right, *right_offset);
    if (left_ellipsis || right_ellipsis) {
      if (left_ellipsis != right_ellipsis) return 0;
      *left_offset += 3;
      *right_offset += 3;
    } else if (!abi_layout_types_compatible(
                   left, left_offset,
                   right, right_offset)) {
      return 0;
    }
    if (*left_offset >= left.len ||
        *right_offset >= right.len)
      return 0;
    char left_delimiter = left.s[(*left_offset)++];
    char right_delimiter = right.s[(*right_offset)++];
    if (left_delimiter != right_delimiter ||
        (left_delimiter != ',' &&
         left_delimiter != ')'))
      return 0;
    if (left_delimiter == ')') return 1;
  }
}

static int abi_layout_signatures_compatible(
    const c_signature_t *left_entry,
    uint32_t left_version,
    const c_signature_t *right_entry,
    uint32_t right_version) {
  if (!left_entry || !right_entry) return 1;
  str_t left = left_entry->signature;
  str_t right = right_entry->signature;
  if (str_eq(left, right)) return 1;
  if (left_version != right_version) return 1;
  int left_offset = 0;
  int right_offset = 0;
  if (left.len < 4 || right.len < 4 ||
      left.s[left_offset++] != 'F' ||
      right.s[right_offset++] != 'F' ||
      !abi_layout_types_compatible(
          left, &left_offset,
          right, &right_offset) ||
      left_offset >= left.len ||
      right_offset >= right.len ||
      left.s[left_offset++] != '(' ||
      right.s[right_offset++] != '(')
    return 0;
  return abi_layout_parameter_lists_compatible(
             left, &left_offset,
             right, &right_offset) &&
         left_offset == left.len &&
         right_offset == right.len;
}

static int canonical_qualifier_prefix_length(str_t signature) {
  int length = 0;
  while (length < signature.len &&
         (signature.s[length] == 'k' ||
          signature.s[length] == 'V' ||
          signature.s[length] == 'A' ||
          signature.s[length] == 'R'))
    length++;
  return length;
}

static int canonical_callback_object_function_types(
    str_t left, str_t right,
    str_t *left_function, str_t *right_function) {
  int left_qualifiers =
      canonical_qualifier_prefix_length(left);
  int right_qualifiers =
      canonical_qualifier_prefix_length(right);
  if (left_qualifiers != right_qualifiers ||
      memcmp(
          left.s, right.s,
          (size_t)left_qualifiers) != 0)
    return 0;
  left.s += left_qualifiers;
  left.len -= left_qualifiers;
  right.s += right_qualifiers;
  right.len -= right_qualifiers;

  if (left.len >= 3 && right.len >= 3 &&
      left.s[0] == 'p' && right.s[0] == 'p' &&
      left.s[1] == '<' && right.s[1] == '<' &&
      left.s[left.len - 1] == '>' &&
      right.s[right.len - 1] == '>') {
    return canonical_callback_object_function_types(
        (str_t){left.s + 2, left.len - 3},
        (str_t){right.s + 2, right.len - 3},
        left_function, right_function);
  }

  canonical_array_type_t left_array = {0};
  canonical_array_type_t right_array = {0};
  int left_is_array =
      canonical_array_type_at(left, 0, &left_array);
  int right_is_array =
      canonical_array_type_at(right, 0, &right_array);
  if (left_is_array || right_is_array) {
    if (!left_is_array || !right_is_array ||
        left.s[left.len - 1] != '>' ||
        right.s[right.len - 1] != '>' ||
        (left_array.bound != 0 &&
         right_array.bound != 0 &&
         left_array.bound != right_array.bound))
      return 0;
    return canonical_callback_object_function_types(
        (str_t){
            left.s + left_array.element_start,
            left.len - left_array.element_start - 1},
        (str_t){
            right.s + right_array.element_start,
            right.len - right_array.element_start - 1},
        left_function, right_function);
  }

  if (!canonical_function_signature_parameter_list(
          left, NULL, NULL) ||
      !canonical_function_signature_parameter_list(
          right, NULL, NULL))
    return 0;
  if (left_function) *left_function = left;
  if (right_function) *right_function = right;
  return 1;
}

static int abi_layout_callback_parameters_unspecified(
    str_t signature, int *is_unspecified) {
  if (is_unspecified) *is_unspecified = 0;
  if (signature.len <= 0) return 0;
  int offset = 0;
  if (signature.s[offset] == 'a') {
    uint32_t ignored_bound = 0;
    offset++;
    if (!canonical_decimal(
            signature, &offset, &ignored_bound) ||
        offset >= signature.len ||
        signature.s[offset++] != '[' ||
        signature.s[signature.len - 1] != ']')
      return 0;
    return abi_layout_callback_parameters_unspecified(
        (str_t){
            signature.s + offset,
            signature.len - offset - 1},
        is_unspecified);
  }
  if (signature.s[offset] == 'p') {
    uint32_t ignored_size = 0;
    uint32_t ignored_alignment = 0;
    offset++;
    if (!canonical_decimal(
            signature, &offset, &ignored_size) ||
        offset >= signature.len ||
        signature.s[offset++] != ':' ||
        !canonical_decimal(
            signature, &offset, &ignored_alignment) ||
        offset >= signature.len ||
        signature.s[offset++] != '[' ||
        signature.s[signature.len - 1] != ']')
      return 0;
    return abi_layout_callback_parameters_unspecified(
        (str_t){
            signature.s + offset,
            signature.len - offset - 1},
        is_unspecified);
  }
  if (signature.s[offset++] != 'c' ||
      !abi_layout_skip_type(signature, &offset) ||
      offset >= signature.len ||
      signature.s[offset++] != '(')
    return 0;
  int unspecified =
      offset + 1 < signature.len &&
      signature.s[offset] == '?' &&
      signature.s[offset + 1] == ')';
  if (!abi_layout_skip_parameter_list(
          signature, &offset) ||
      offset != signature.len)
    return 0;
  if (is_unspecified) *is_unspecified = unspecified;
  return 1;
}

static int canonical_unspecified_function_type_matches(
    str_t unspecified, str_t specified) {
  int unspecified_parameters = 0;
  int specified_parameters = 0;
  int unspecified_is_empty = 0;
  if (!canonical_function_signature_parameter_list(
          unspecified, &unspecified_parameters,
          &unspecified_is_empty) ||
      !canonical_function_signature_parameter_list(
          specified, &specified_parameters, NULL) ||
      !unspecified_is_empty ||
      !canonical_type_signatures_compatible(
          (str_t){
              unspecified.s, unspecified_parameters},
          (str_t){
              specified.s, specified_parameters}) ||
      !canonical_parameters_unchanged_by_default_promotions(
          specified, specified_parameters))
    return 0;
  return 1;
}

static int data_c_signatures_compatible(
    const data_signature_t *left_entry,
    const data_signature_t *right_entry) {
  if (canonical_type_signatures_compatible(
          left_entry->c_signature,
          right_entry->c_signature))
    return 1;
  str_t left_function = {0};
  str_t right_function = {0};
  if (!canonical_callback_object_function_types(
          left_entry->c_signature,
          right_entry->c_signature,
          &left_function, &right_function))
    return 0;
  int left_unspecified = 0;
  int right_unspecified = 0;
  if (!abi_layout_callback_parameters_unspecified(
          left_entry->layout_signature,
          &left_unspecified) ||
      !abi_layout_callback_parameters_unspecified(
          right_entry->layout_signature,
          &right_unspecified) ||
      left_unspecified == right_unspecified)
    return 0;
  return left_unspecified
             ? canonical_unspecified_function_type_matches(
                   left_function, right_function)
             : canonical_unspecified_function_type_matches(
                   right_function, left_function);
}

static int data_signatures_compatible(
    const data_signature_t *left_entry,
    uint32_t left_layout_version,
    const data_signature_t *right_entry,
    uint32_t right_layout_version) {
  if (!left_entry || !right_entry) return 1;
  if (left_entry->has_object_properties &&
      right_entry->has_object_properties &&
      ((left_entry->flags ^ right_entry->flags) &
       AGC_DATA_FLAG_THREAD_LOCAL))
    return 0;
  if (left_entry->has_object_properties &&
      right_entry->has_object_properties &&
      left_entry->requested_alignment != 0 &&
      left_entry->requested_alignment !=
          right_entry->requested_alignment)
    return 0;
  if (!data_c_signatures_compatible(
          left_entry, right_entry))
    return 0;
  if (str_eq(
          left_entry->layout_signature,
          right_entry->layout_signature))
    return 1;
  if (left_layout_version != right_layout_version)
    return 1;
  int left_offset = 0;
  int right_offset = 0;
  return abi_layout_types_compatible(
             left_entry->layout_signature, &left_offset,
             right_entry->layout_signature, &right_offset) &&
         left_offset == left_entry->layout_signature.len &&
         right_offset == right_entry->layout_signature.len;
}

static int func_signature_matches(object_t *ref_obj, int ref_func,
                                  object_t *def_obj, int def_func) {
  if (ref_func < 0 || ref_func >= ref_obj->func_count ||
      def_func < 0 || def_func >= def_obj->func_count) {
    return 0;
  }
  int ref_type = ref_obj->funcs[ref_func].type_index;
  int def_type = def_obj->funcs[def_func].type_index;
  if (ref_type < 0 || ref_type >= ref_obj->type_count ||
      def_type < 0 || def_type >= def_obj->type_count) {
    return 0;
  }
  const type_t *reference_type = &ref_obj->types[ref_type];
  const type_t *definition_type = &def_obj->types[def_type];
  int wasm_types_equal =
      type_equal(reference_type, definition_type);
  const c_signature_t *reference = find_c_signature(
      ref_obj, ref_obj->funcs[ref_func].name);
  const c_signature_t *definition = find_c_signature(
      def_obj, def_obj->funcs[def_func].name);
  const c_signature_t *reference_layout =
      find_abi_layout_signature(
          ref_obj, ref_obj->funcs[ref_func].name);
  const c_signature_t *definition_layout =
      find_abi_layout_signature(
          def_obj, def_obj->funcs[def_func].name);
  int layouts_compatible =
      abi_layout_signatures_compatible(
          reference_layout, ref_obj->abi_layout_version,
          definition_layout, def_obj->abi_layout_version);
  if (!reference || !definition)
    return wasm_types_equal && layouts_compatible;
  if (canonical_type_signatures_compatible(
          reference->signature, definition->signature))
    return wasm_types_equal && layouts_compatible;
  uint32_t reference_flags = find_function_flags(
      ref_obj, ref_obj->funcs[ref_func].name);
  uint32_t definition_flags = find_function_flags(
      def_obj, def_obj->funcs[def_func].name);
  return layouts_compatible &&
         (unspecified_function_signature_matches(
              reference, reference_flags, reference_type,
              definition, definition_type) ||
          unspecified_function_signature_matches(
              definition, definition_flags, definition_type,
              reference, reference_type));
}

static void check_duplicate_definitions(object_t *objs, int obj_count) {
  for (int oi = 0; oi < obj_count; oi++) {
    if (!objs[oi].has_continuation) continue;
    for (int oj = oi + 1; oj < obj_count; oj++) {
      if (!objs[oj].has_continuation ||
          !str_eq(objs[oi].continuation_entry,
                  objs[oj].continuation_entry)) {
        continue;
      }
      die_link_diagnostic_two_objects(
          "AGC_LINK_DUPLICATE_CONTINUATION_ENTRY",
          objs[oi].continuation_entry, oi, oj);
    }
  }
  for (int oi = 0; oi < obj_count; oi++) {
    for (int si = 0; si < objs[oi].symbol_count; si++) {
      symbol_t *a = &objs[oi].symbols[si];
      if ((a->kind != SYM_FUNCTION && a->kind != SYM_DATA) ||
          (a->flags & (SYM_UNDEFINED | SYM_BINDING_LOCAL)) ||
          str_empty(a->name)) {
        continue;
      }
      for (int oj = oi; oj < obj_count; oj++) {
        int sj_start = (oj == oi) ? si + 1 : 0;
        for (int sj = sj_start; sj < objs[oj].symbol_count; sj++) {
          symbol_t *b = &objs[oj].symbols[sj];
          if (b->kind != a->kind ||
              (b->flags & (SYM_UNDEFINED | SYM_BINDING_LOCAL)) ||
              str_empty(b->name)) {
            continue;
          }
          if (!str_eq(a->name, b->name)) continue;
          die_link_diagnostic_two_objects(
              "AGC_LINK_DUPLICATE_SYMBOL", a->name, oi, oj);
        }
      }
    }
  }
}

static int find_defined_func(object_t *objs, int obj_count, str_t name, object_t **out_obj, int *out_func) {
  for (int oi = 0; oi < obj_count; oi++) {
    for (int fi = 0; fi < objs[oi].func_count; fi++) {
      symbol_t *sym = NULL;
      for (int si = 0; si < objs[oi].symbol_count; si++) {
        if (objs[oi].symbols[si].kind == SYM_FUNCTION && objs[oi].symbols[si].index == fi) {
          sym = &objs[oi].symbols[si];
          break;
        }
      }
      if (!sym || !objs[oi].funcs[fi].defined || (sym->flags & SYM_BINDING_LOCAL)) continue;
      if (str_eq(sym->name, name)) {
        if (out_obj) *out_obj = &objs[oi];
        if (out_func) *out_func = fi;
        return 1;
      }
    }
  }
  return 0;
}

static int find_defined_data(object_t *objs, int obj_count, str_t name, object_t **out_obj,
                             int *out_data, uint32_t *out_offset) {
  for (int oi = 0; oi < obj_count; oi++) {
    for (int si = 0; si < objs[oi].symbol_count; si++) {
      symbol_t *sym = &objs[oi].symbols[si];
      if (sym->kind != SYM_DATA || (sym->flags & (SYM_UNDEFINED | SYM_BINDING_LOCAL))) continue;
      if (str_eq(sym->name, name)) {
        if (out_obj) *out_obj = &objs[oi];
        if (out_data) *out_data = sym->index;
        if (out_offset) *out_offset = sym->data_offset;
        return 1;
      }
    }
  }
  return 0;
}

static int str_eq_lit(str_t a, const char *b) {
  int n = (int)strlen(b);
  return a.len == n && a.s && memcmp(a.s, b, (size_t)n) == 0;
}

typedef enum {
  RUNTIME_SYMBOL_BRIDGE,
  RUNTIME_SYMBOL_SYNTHETIC,
} runtime_symbol_kind_t;

typedef enum {
  RUNTIME_SIGNATURE_EXACT,
  RUNTIME_SIGNATURE_CALLER,
} runtime_signature_kind_t;

enum {
  RUNTIME_AVAILABLE_WASM32_JS = 1u << 0,
  RUNTIME_AVAILABLE_WASM32_OBJECT_LINKER = 1u << 1,
  RUNTIME_AVAILABLE_WASM32_OBJECT_RUNTIME = 1u << 2,
};

typedef struct {
  const char *name;
  const char *target;
  runtime_symbol_kind_t kind;
  runtime_signature_kind_t signature_kind;
  const unsigned char *param_types;
  uint32_t param_count;
  unsigned char result_type;
  unsigned char memory_read;
  unsigned char memory_write;
  unsigned char availability;
  const char *import_namespace;
} runtime_symbol_manifest_entry_t;

#include "runtime/generated/runtime-symbols.inc"

static unsigned char wasm_type_result_valtype(const type_t *t);
static uint32_t wasm_type_param_count(type_t *t);
static unsigned char wasm_type_param_valtype(type_t *t, uint32_t idx);

static const runtime_symbol_manifest_entry_t *find_runtime_func_symbol(str_t name) {
  size_t count = sizeof(agc_runtime_function_symbols) / sizeof(agc_runtime_function_symbols[0]);
  for (size_t i = 0; i < count; i++) {
    if (str_eq_lit(name, agc_runtime_function_symbols[i].name)) {
      return &agc_runtime_function_symbols[i];
    }
  }
  return NULL;
}

static int is_runtime_data_symbol(str_t name) {
  size_t count = sizeof(agc_runtime_data_symbols) / sizeof(agc_runtime_data_symbols[0]);
  for (size_t i = 0; i < count; i++) {
    if (str_eq_lit(name, agc_runtime_data_symbols[i])) return 1;
  }
  return 0;
}

static int is_runtime_func_symbol(str_t name) {
  return find_runtime_func_symbol(name) != NULL;
}

static int runtime_manifest_signature_matches(
    const runtime_symbol_manifest_entry_t *entry, type_t *type,
    int allow_integer_width_conversion) {
  if (!entry || !type ||
      entry->signature_kind == RUNTIME_SIGNATURE_CALLER)
    return entry && type;
  if (wasm_type_param_count(type) != entry->param_count) return 0;
  for (uint32_t i = 0; i < entry->param_count; i++) {
    unsigned char actual = wasm_type_param_valtype(type, i);
    unsigned char expected = entry->param_types[i];
    if (actual == expected) continue;
    int integer_pair =
        (actual == 0x7f || actual == 0x7e) &&
        (expected == 0x7f || expected == 0x7e);
    if (!allow_integer_width_conversion || !integer_pair) return 0;
  }
  unsigned char actual_result = wasm_type_result_valtype(type);
  if (actual_result == entry->result_type) return 1;
  int integer_pair =
      (actual_result == 0x7f || actual_result == 0x7e) &&
      (entry->result_type == 0x7f || entry->result_type == 0x7e);
  return allow_integer_width_conversion && integer_pair;
}

static int is_unsupported_control_flow_symbol(str_t name) {
  return str_eq_lit(name, "setjmp") || str_eq_lit(name, "longjmp") ||
         str_eq_lit(name, "__agc_runtime_setjmp") ||
         str_eq_lit(name, "__agc_runtime_longjmp");
}

static int runtime_has_data(object_t *runtime, str_t name) {
  for (int i = 0; i < runtime->symbol_count; i++) {
    symbol_t *sym = &runtime->symbols[i];
    if (sym->kind == SYM_DATA && !(sym->flags & SYM_UNDEFINED) && str_eq(sym->name, name)) return 1;
  }
  return 0;
}

static int runtime_has_func(object_t *runtime, str_t name) {
  for (int i = 0; i < runtime->symbol_count; i++) {
    symbol_t *sym = &runtime->symbols[i];
    if (sym->kind == SYM_FUNCTION && !(sym->flags & SYM_UNDEFINED) && str_eq(sym->name, name)) return 1;
  }
  return 0;
}

static unsigned char wasm_type_result_valtype(const type_t *t) {
  rd_t r = {t->raw, t->raw_len, 0, "runtime stub type"};
  if (r.pos >= r.len || r.p[r.pos++] != 0x60) die("bad runtime stub function type");
  uint32_t np = rd_uleb(&r);
  rd_skip(&r, np);
  uint32_t nr = rd_uleb(&r);
  if (nr == 0) return 0;
  if (nr != 1 || r.pos >= r.len) die("unsupported runtime stub result type");
  return r.p[r.pos++];
}

static uint32_t wasm_type_param_count(type_t *t) {
  rd_t r = {t->raw, t->raw_len, 0, "runtime stub type"};
  if (r.pos >= r.len || r.p[r.pos++] != 0x60) die("bad runtime stub function type");
  return rd_uleb(&r);
}

static unsigned char wasm_type_param_valtype(type_t *t, uint32_t idx) {
  rd_t r = {t->raw, t->raw_len, 0, "runtime stub type"};
  if (r.pos >= r.len || r.p[r.pos++] != 0x60) die("bad runtime stub function type");
  uint32_t np = rd_uleb(&r);
  if (idx >= np) die("runtime stub parameter index out of range");
  for (uint32_t p = 0; p < np; p++) {
    if (r.pos >= r.len) die("truncated runtime stub parameter type");
    unsigned char ty = r.p[r.pos++];
    if (p == idx) return ty;
  }
  die("runtime stub parameter index out of range");
  return 0;
}

static uint32_t runtime_param_count(type_t *type, uint32_t min, str_t name) {
  uint32_t n = wasm_type_param_count(type);
  if (n < min) dief("runtime stub signature mismatch: %s", name.s);
  return n;
}

static void emit_i32_from_param(buf_t *b, type_t *type, uint32_t idx) {
  unsigned char ty = wasm_type_param_valtype(type, idx);
  buf_u8(b, 0x20);      /* local.get */
  buf_uleb(b, idx);
  if (ty == 0x7e) {
    buf_u8(b, 0xa7);    /* i32.wrap_i64 */
  } else if (ty != 0x7f) {
    die("runtime stub expects integer parameter");
  }
}

static void emit_return_i32_as_result(buf_t *b, type_t *type) {
  unsigned char result = wasm_type_result_valtype(type);
  if (result == 0x7e) {
    buf_u8(b, 0xad);    /* i64.extend_i32_u */
  } else if (result != 0x7f && result != 0) {
    die("runtime stub expects integer result");
  }
}

static int make_printf_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "printf") && !str_eq_lit(name, "fprintf")) return 0;
  if (wasm_type_result_valtype(type) != 0x7f) return 0;
  uint32_t fmt_param = str_eq_lit(name, "fprintf") ? 1u : 0u;
  if (wasm_type_param_count(type) <= fmt_param) return 0;
  unsigned char fmt_ty = wasm_type_param_valtype(type, fmt_param);
  if (fmt_ty != 0x7e && fmt_ty != 0x7f) return 0;

  uint32_t addr_local = wasm_type_param_count(type);
  uint32_t len_local = addr_local + 1;
  buf_uleb(b, 1);       /* local decl group count */
  buf_uleb(b, 2);       /* addr, len */
  buf_u8(b, 0x7f);      /* i32 */
  buf_u8(b, 0x20);      /* local.get fmt_param */
  buf_uleb(b, fmt_param);
  if (fmt_ty == 0x7e) buf_u8(b, 0xa7); /* i32.wrap_i64 */
  buf_u8(b, 0x21);      /* local.set addr */
  buf_uleb(b, addr_local);
  buf_u8(b, 0x41);      /* i32.const 0 */
  buf_sleb_i32(b, 0);
  buf_u8(b, 0x21);      /* local.set len */
  buf_uleb(b, len_local);
  buf_u8(b, 0x02);      /* block */
  buf_u8(b, 0x40);
  buf_u8(b, 0x03);      /* loop */
  buf_u8(b, 0x40);
  buf_u8(b, 0x20);      /* local.get addr */
  buf_uleb(b, addr_local);
  buf_u8(b, 0x2d);      /* i32.load8_u */
  buf_uleb(b, 0);
  buf_uleb(b, 0);
  buf_u8(b, 0x45);      /* i32.eqz */
  buf_u8(b, 0x0d);      /* br_if 1 */
  buf_uleb(b, 1);
  buf_u8(b, 0x20);      /* local.get len */
  buf_uleb(b, len_local);
  buf_u8(b, 0x41);      /* i32.const 1 */
  buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);      /* i32.add */
  buf_u8(b, 0x21);      /* local.set len */
  buf_uleb(b, len_local);
  buf_u8(b, 0x20);      /* local.get addr */
  buf_uleb(b, addr_local);
  buf_u8(b, 0x41);      /* i32.const 1 */
  buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);      /* i32.add */
  buf_u8(b, 0x21);      /* local.set addr */
  buf_uleb(b, addr_local);
  buf_u8(b, 0x0c);      /* br 0 */
  buf_uleb(b, 0);
  buf_u8(b, 0x0b);      /* end loop */
  buf_u8(b, 0x0b);      /* end block */
  buf_u8(b, 0x20);      /* local.get len */
  buf_uleb(b, len_local);
  return 1;
}

static int make_host_write_memory_sink_body(str_t name, type_t *type,
                                            buf_t *b) {
  if (!str_eq_lit(name, "__agc_host_write")) return 0;
  runtime_param_count(type, 3, name);
  if (wasm_type_result_valtype(type) != 0x7f) return 0;
  buf_uleb(b, 0);
  emit_i32_from_param(b, type, 2);
  return 1;
}

static int make_strlen_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "strlen")) return 0;
  runtime_param_count(type, 1, name);
  uint32_t addr = wasm_type_param_count(type);
  uint32_t len = addr + 1;
  buf_uleb(b, 1);
  buf_uleb(b, 2);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0);
  buf_u8(b, 0x21); buf_uleb(b, addr);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, len);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, addr);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x45);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, len);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, len);
  buf_u8(b, 0x20); buf_uleb(b, addr);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, addr);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, len);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_strcmp_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "strcmp")) return 0;
  runtime_param_count(type, 2, name);
  uint32_t a = wasm_type_param_count(type);
  uint32_t c = a + 2;
  uint32_t d = a + 3;
  buf_uleb(b, 1);
  buf_uleb(b, 4);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0);
  buf_u8(b, 0x21); buf_uleb(b, a);
  emit_i32_from_param(b, type, 1);
  buf_u8(b, 0x21); buf_uleb(b, a + 1);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, a + 1);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, d);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, d);
  buf_u8(b, 0x47);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, d);
  buf_u8(b, 0x6b);
  emit_return_i32_as_result(b, type);
  buf_u8(b, 0x0f);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x45);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  emit_return_i32_as_result(b, type);
  buf_u8(b, 0x0f);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, a);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, a);
  buf_u8(b, 0x20); buf_uleb(b, a + 1);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, a + 1);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_memset_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "memset")) return 0;
  runtime_param_count(type, 3, name);
  uint32_t dst = wasm_type_param_count(type);
  uint32_t val = dst + 1;
  uint32_t n = dst + 2;
  uint32_t i = dst + 3;
  buf_uleb(b, 1);
  buf_uleb(b, 4);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0); buf_u8(b, 0x21); buf_uleb(b, dst);
  emit_i32_from_param(b, type, 1); buf_u8(b, 0x21); buf_uleb(b, val);
  emit_i32_from_param(b, type, 2); buf_u8(b, 0x21); buf_uleb(b, n);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0); buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x20); buf_uleb(b, n);
  buf_u8(b, 0x4f);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, dst);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x20); buf_uleb(b, val);
  buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, dst);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_memcpy_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "memcpy")) return 0;
  runtime_param_count(type, 3, name);
  uint32_t dst = wasm_type_param_count(type);
  uint32_t src = dst + 1;
  uint32_t n = dst + 2;
  uint32_t i = dst + 3;
  buf_uleb(b, 1);
  buf_uleb(b, 4);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0); buf_u8(b, 0x21); buf_uleb(b, dst);
  emit_i32_from_param(b, type, 1); buf_u8(b, 0x21); buf_uleb(b, src);
  emit_i32_from_param(b, type, 2); buf_u8(b, 0x21); buf_uleb(b, n);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0); buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x20); buf_uleb(b, n);
  buf_u8(b, 0x4f);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, dst);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x20); buf_uleb(b, src);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, dst);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_abs_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "abs")) return 0;
  runtime_param_count(type, 1, name);
  uint32_t x = wasm_type_param_count(type);
  buf_uleb(b, 1);
  buf_uleb(b, 1);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0);
  buf_u8(b, 0x21); buf_uleb(b, x);
  buf_u8(b, 0x20); buf_uleb(b, x);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  buf_u8(b, 0x48);
  buf_u8(b, 0x04); buf_u8(b, 0x7f);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  buf_u8(b, 0x20); buf_uleb(b, x);
  buf_u8(b, 0x6b);
  buf_u8(b, 0x05);
  buf_u8(b, 0x20); buf_uleb(b, x);
  buf_u8(b, 0x0b);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_ctype_stub_body(str_t name, type_t *type, buf_t *b) {
  int is_digit = str_eq_lit(name, "isdigit") || str_eq_lit(name, "iswdigit");
  int is_alpha = str_eq_lit(name, "isalpha") || str_eq_lit(name, "iswalpha");
  int is_upper = str_eq_lit(name, "toupper") || str_eq_lit(name, "towupper");
  if (!is_digit && !is_alpha && !is_upper) return 0;
  runtime_param_count(type, 1, name);
  uint32_t x = wasm_type_param_count(type);
  buf_uleb(b, 1);
  buf_uleb(b, 1);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0);
  buf_u8(b, 0x21); buf_uleb(b, x);
  if (is_digit) {
    buf_u8(b, 0x20); buf_uleb(b, x);
    buf_u8(b, 0x41); buf_sleb_i32(b, '0');
    buf_u8(b, 0x4e);
    buf_u8(b, 0x20); buf_uleb(b, x);
    buf_u8(b, 0x41); buf_sleb_i32(b, '9');
    buf_u8(b, 0x4c);
    buf_u8(b, 0x71);
  } else if (is_alpha) {
    buf_u8(b, 0x20); buf_uleb(b, x);
    buf_u8(b, 0x41); buf_sleb_i32(b, 32);
    buf_u8(b, 0x72);
    buf_u8(b, 0x21); buf_uleb(b, x);
    buf_u8(b, 0x20); buf_uleb(b, x);
    buf_u8(b, 0x41); buf_sleb_i32(b, 'a');
    buf_u8(b, 0x4e);
    buf_u8(b, 0x20); buf_uleb(b, x);
    buf_u8(b, 0x41); buf_sleb_i32(b, 'z');
    buf_u8(b, 0x4c);
    buf_u8(b, 0x71);
  } else {
    buf_u8(b, 0x20); buf_uleb(b, x);
    buf_u8(b, 0x41); buf_sleb_i32(b, 'a');
    buf_u8(b, 0x4e);
    buf_u8(b, 0x20); buf_uleb(b, x);
    buf_u8(b, 0x41); buf_sleb_i32(b, 'z');
    buf_u8(b, 0x4c);
    buf_u8(b, 0x71);
    buf_u8(b, 0x04); buf_u8(b, 0x7f);
    buf_u8(b, 0x20); buf_uleb(b, x);
    buf_u8(b, 0x41); buf_sleb_i32(b, 32);
    buf_u8(b, 0x6b);
    buf_u8(b, 0x05);
    buf_u8(b, 0x20); buf_uleb(b, x);
    buf_u8(b, 0x0b);
  }
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_malloc_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "malloc")) return 0;
  runtime_param_count(type, 1, name);
  buf_uleb(b, 0);
  buf_u8(b, 0x41);
  buf_sleb_i32(b, RUNTIME_SCRATCH_BASE);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_free_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "free")) return 0;
  runtime_param_count(type, 1, name);
  buf_uleb(b, 0);
  return 1;
}

static int make_calloc_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "calloc")) return 0;
  runtime_param_count(type, 2, name);
  uint32_t n = wasm_type_param_count(type);
  uint32_t i = n + 1;
  buf_uleb(b, 1);
  buf_uleb(b, 2);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0);
  emit_i32_from_param(b, type, 1);
  buf_u8(b, 0x6c);      /* i32.mul */
  buf_u8(b, 0x21); buf_uleb(b, n);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x20); buf_uleb(b, n);
  buf_u8(b, 0x4f);      /* i32.ge_u */
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x41); buf_sleb_i32(b, RUNTIME_SCRATCH_BASE);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x41);
  buf_sleb_i32(b, RUNTIME_SCRATCH_BASE);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_atoi_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "atoi")) return 0;
  runtime_param_count(type, 1, name);
  uint32_t p = wasm_type_param_count(type);
  uint32_t acc = p + 1;
  uint32_t sign = p + 2;
  uint32_t c = p + 3;
  buf_uleb(b, 1);
  buf_uleb(b, 4);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0);
  buf_u8(b, 0x21); buf_uleb(b, p);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, acc);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x21); buf_uleb(b, sign);

  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, p);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x41); buf_sleb_i32(b, ' ');
  buf_u8(b, 0x47);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, p);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, p);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);

  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x41); buf_sleb_i32(b, '-');
  buf_u8(b, 0x46);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  buf_u8(b, 0x41); buf_sleb_i32(b, -1);
  buf_u8(b, 0x21); buf_uleb(b, sign);
  buf_u8(b, 0x20); buf_uleb(b, p);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, p);
  buf_u8(b, 0x05);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x41); buf_sleb_i32(b, '+');
  buf_u8(b, 0x46);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, p);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, p);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);

  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, p);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x41); buf_sleb_i32(b, '0');
  buf_u8(b, 0x49);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x41); buf_sleb_i32(b, '9');
  buf_u8(b, 0x4b);
  buf_u8(b, 0x72);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, acc);
  buf_u8(b, 0x41); buf_sleb_i32(b, 10);
  buf_u8(b, 0x6c);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x41); buf_sleb_i32(b, '0');
  buf_u8(b, 0x6b);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, acc);
  buf_u8(b, 0x20); buf_uleb(b, p);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, p);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, acc);
  buf_u8(b, 0x20); buf_uleb(b, sign);
  buf_u8(b, 0x6c);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_strcpy_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "strcpy")) return 0;
  runtime_param_count(type, 2, name);
  uint32_t dst = wasm_type_param_count(type);
  uint32_t src = dst + 1;
  uint32_t i = dst + 2;
  uint32_t c = dst + 3;
  buf_uleb(b, 1);
  buf_uleb(b, 4);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0); buf_u8(b, 0x21); buf_uleb(b, dst);
  emit_i32_from_param(b, type, 1); buf_u8(b, 0x21); buf_uleb(b, src);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0); buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, src);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, dst);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x45);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, dst);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_strncpy_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "strncpy")) return 0;
  runtime_param_count(type, 3, name);
  uint32_t dst = wasm_type_param_count(type);
  uint32_t src = dst + 1;
  uint32_t n = dst + 2;
  uint32_t i = dst + 3;
  uint32_t c = dst + 4;
  uint32_t ended = dst + 5;
  buf_uleb(b, 1);
  buf_uleb(b, 6);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0); buf_u8(b, 0x21); buf_uleb(b, dst);
  emit_i32_from_param(b, type, 1); buf_u8(b, 0x21); buf_uleb(b, src);
  emit_i32_from_param(b, type, 2); buf_u8(b, 0x21); buf_uleb(b, n);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0); buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0); buf_u8(b, 0x21); buf_uleb(b, ended);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x20); buf_uleb(b, n);
  buf_u8(b, 0x4f);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, ended);
  buf_u8(b, 0x45);
  buf_u8(b, 0x04); buf_u8(b, 0x7f);
  buf_u8(b, 0x20); buf_uleb(b, src);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x05);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x21); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, dst);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x45);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x21); buf_uleb(b, ended);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, dst);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_strcat_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "strcat")) return 0;
  runtime_param_count(type, 2, name);
  uint32_t dst = wasm_type_param_count(type);
  uint32_t src = dst + 1;
  uint32_t end = dst + 2;
  uint32_t i = dst + 3;
  uint32_t c = dst + 4;
  buf_uleb(b, 1);
  buf_uleb(b, 5);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0); buf_u8(b, 0x21); buf_uleb(b, dst);
  emit_i32_from_param(b, type, 1); buf_u8(b, 0x21); buf_uleb(b, src);
  buf_u8(b, 0x20); buf_uleb(b, dst); buf_u8(b, 0x21); buf_uleb(b, end);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, end);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x45);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, end);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, end);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0); buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, src);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, end);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x45);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, dst);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_strncmp_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "strncmp")) return 0;
  runtime_param_count(type, 3, name);
  uint32_t a = wasm_type_param_count(type);
  uint32_t c = a + 3;
  uint32_t d = a + 4;
  uint32_t i = a + 5;
  buf_uleb(b, 1);
  buf_uleb(b, 6);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0); buf_u8(b, 0x21); buf_uleb(b, a);
  emit_i32_from_param(b, type, 1); buf_u8(b, 0x21); buf_uleb(b, a + 1);
  emit_i32_from_param(b, type, 2); buf_u8(b, 0x21); buf_uleb(b, a + 2);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0); buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x20); buf_uleb(b, a + 2);
  buf_u8(b, 0x4f);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, a);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, a + 1);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, d);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, d);
  buf_u8(b, 0x47);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, d);
  buf_u8(b, 0x6b);
  emit_return_i32_as_result(b, type);
  buf_u8(b, 0x0f);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x45);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  emit_return_i32_as_result(b, type);
  buf_u8(b, 0x0f);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_memcmp_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "memcmp")) return 0;
  runtime_param_count(type, 3, name);
  uint32_t a = wasm_type_param_count(type);
  uint32_t c = a + 3;
  uint32_t d = a + 4;
  uint32_t i = a + 5;
  buf_uleb(b, 1);
  buf_uleb(b, 6);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0); buf_u8(b, 0x21); buf_uleb(b, a);
  emit_i32_from_param(b, type, 1); buf_u8(b, 0x21); buf_uleb(b, a + 1);
  emit_i32_from_param(b, type, 2); buf_u8(b, 0x21); buf_uleb(b, a + 2);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0); buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x20); buf_uleb(b, a + 2);
  buf_u8(b, 0x4f);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, a);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, a + 1);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, d);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, d);
  buf_u8(b, 0x47);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, d);
  buf_u8(b, 0x6b);
  emit_return_i32_as_result(b, type);
  buf_u8(b, 0x0f);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, i);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, i);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_strchr_stub_body(str_t name, type_t *type, buf_t *b) {
  int reverse = str_eq_lit(name, "strrchr");
  if (!str_eq_lit(name, "strchr") && !reverse) return 0;
  runtime_param_count(type, 2, name);
  uint32_t p = wasm_type_param_count(type);
  uint32_t ch = p + 1;
  uint32_t cur = p + 2;
  uint32_t c = p + 3;
  uint32_t found = p + 4;
  buf_uleb(b, 1);
  buf_uleb(b, 5);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0); buf_u8(b, 0x21); buf_uleb(b, p);
  emit_i32_from_param(b, type, 1); buf_u8(b, 0x21); buf_uleb(b, ch);
  buf_u8(b, 0x20); buf_uleb(b, p); buf_u8(b, 0x21); buf_uleb(b, cur);
  buf_u8(b, 0x41); buf_sleb_i32(b, 0); buf_u8(b, 0x21); buf_uleb(b, found);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, cur);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  buf_u8(b, 0x21); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x20); buf_uleb(b, ch);
  buf_u8(b, 0x41); buf_sleb_i32(b, 255);
  buf_u8(b, 0x71);
  buf_u8(b, 0x46);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  buf_u8(b, 0x20); buf_uleb(b, cur);
  if (reverse) {
    buf_u8(b, 0x21); buf_uleb(b, found);
  } else {
    emit_return_i32_as_result(b, type);
    buf_u8(b, 0x0f);
  }
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, c);
  buf_u8(b, 0x45);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  buf_u8(b, 0x20); buf_uleb(b, cur);
  buf_u8(b, 0x41); buf_sleb_i32(b, 1);
  buf_u8(b, 0x6a);
  buf_u8(b, 0x21); buf_uleb(b, cur);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x20); buf_uleb(b, found);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_putchar_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "putchar")) return 0;
  runtime_param_count(type, 1, name);
  buf_uleb(b, 0);
  emit_i32_from_param(b, type, 0);
  emit_return_i32_as_result(b, type);
  return 1;
}

static void emit_local_get(buf_t *b, uint32_t idx) {
  buf_u8(b, 0x20);
  buf_uleb(b, idx);
}

static void emit_local_set(buf_t *b, uint32_t idx) {
  buf_u8(b, 0x21);
  buf_uleb(b, idx);
}

static void emit_i32_const(buf_t *b, int32_t value) {
  buf_u8(b, 0x41);
  buf_sleb_i32(b, value);
}

static void emit_snprintf_write_byte_const(buf_t *b, uint32_t out, uint32_t count, int ch,
                                           int count_output) {
  emit_local_get(b, out);
  emit_i32_const(b, ch);
  buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0); /* i32.store8 */
  emit_local_get(b, out);
  emit_i32_const(b, 1);
  buf_u8(b, 0x6a); /* i32.add */
  emit_local_set(b, out);
  if (count_output) {
    emit_local_get(b, count);
    emit_i32_const(b, 1);
    buf_u8(b, 0x6a); /* i32.add */
    emit_local_set(b, count);
  }
}

static void emit_snprintf_write_byte_local(buf_t *b, uint32_t out, uint32_t count,
                                           uint32_t ch) {
  emit_local_get(b, out);
  emit_local_get(b, ch);
  buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0); /* i32.store8 */
  emit_local_get(b, out);
  emit_i32_const(b, 1);
  buf_u8(b, 0x6a); /* i32.add */
  emit_local_set(b, out);
  emit_local_get(b, count);
  emit_i32_const(b, 1);
  buf_u8(b, 0x6a); /* i32.add */
  emit_local_set(b, count);
}

static void emit_snprintf_write_cstr(buf_t *b, uint32_t out, uint32_t count,
                                     uint32_t ptr, uint32_t ch) {
  buf_u8(b, 0x02); buf_u8(b, 0x40); /* block */
  buf_u8(b, 0x03); buf_u8(b, 0x40); /* loop */
  emit_local_get(b, ptr);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0); /* i32.load8_u */
  emit_local_set(b, ch);
  emit_local_get(b, ch);
  buf_u8(b, 0x45);                  /* i32.eqz */
  buf_u8(b, 0x0d); buf_uleb(b, 1);  /* br_if block */
  emit_snprintf_write_byte_local(b, out, count, ch);
  emit_local_get(b, ptr);
  emit_i32_const(b, 1);
  buf_u8(b, 0x6a);                  /* i32.add */
  emit_local_set(b, ptr);
  buf_u8(b, 0x0c); buf_uleb(b, 0);  /* br loop */
  buf_u8(b, 0x0b);                  /* end loop */
  buf_u8(b, 0x0b);                  /* end block */
}

static void emit_snprintf_write_u32_decimal(buf_t *b, uint32_t out, uint32_t count,
                                            uint32_t arg, uint32_t divisor,
                                            uint32_t started, uint32_t digit) {
  emit_i32_const(b, 1000000000);
  emit_local_set(b, divisor);
  emit_i32_const(b, 0);
  emit_local_set(b, started);

  buf_u8(b, 0x02); buf_u8(b, 0x40); /* block */
  buf_u8(b, 0x03); buf_u8(b, 0x40); /* loop */
  emit_local_get(b, divisor);
  buf_u8(b, 0x45);                  /* i32.eqz */
  buf_u8(b, 0x0d); buf_uleb(b, 1);  /* br_if block */

  emit_local_get(b, arg);
  emit_local_get(b, divisor);
  buf_u8(b, 0x6e);                  /* i32.div_u */
  emit_local_set(b, digit);

  emit_local_get(b, digit);
  buf_u8(b, 0x45);                  /* i32.eqz */
  buf_u8(b, 0x45);                  /* i32.eqz */
  emit_local_get(b, started);
  buf_u8(b, 0x72);                  /* i32.or */
  emit_local_get(b, divisor);
  emit_i32_const(b, 1);
  buf_u8(b, 0x46);                  /* i32.eq */
  buf_u8(b, 0x72);                  /* i32.or */
  buf_u8(b, 0x04); buf_u8(b, 0x40); /* if */
  emit_local_get(b, out);
  emit_local_get(b, digit);
  emit_i32_const(b, '0');
  buf_u8(b, 0x6a);                  /* i32.add */
  buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0); /* i32.store8 */
  emit_local_get(b, out);
  emit_i32_const(b, 1);
  buf_u8(b, 0x6a);                  /* i32.add */
  emit_local_set(b, out);
  emit_local_get(b, count);
  emit_i32_const(b, 1);
  buf_u8(b, 0x6a);                  /* i32.add */
  emit_local_set(b, count);
  emit_i32_const(b, 1);
  emit_local_set(b, started);
  buf_u8(b, 0x0b);                  /* end if */

  emit_local_get(b, arg);
  emit_local_get(b, divisor);
  buf_u8(b, 0x70);                  /* i32.rem_u */
  emit_local_set(b, arg);
  emit_local_get(b, divisor);
  emit_i32_const(b, 10);
  buf_u8(b, 0x6e);                  /* i32.div_u */
  emit_local_set(b, divisor);
  buf_u8(b, 0x0c); buf_uleb(b, 0);  /* br loop */
  buf_u8(b, 0x0b);                  /* end loop */
  buf_u8(b, 0x0b);                  /* end block */
}

static void emit_snprintf_write_i32_decimal(buf_t *b, uint32_t out, uint32_t count,
                                            uint32_t arg, uint32_t divisor,
                                            uint32_t started, uint32_t digit) {
  emit_local_get(b, arg);
  emit_i32_const(b, 0);
  buf_u8(b, 0x48);                  /* i32.lt_s */
  buf_u8(b, 0x04); buf_u8(b, 0x40); /* if */
  emit_snprintf_write_byte_const(b, out, count, '-', 1);
  emit_i32_const(b, 0);
  emit_local_get(b, arg);
  buf_u8(b, 0x6b);                  /* i32.sub */
  emit_local_set(b, arg);
  buf_u8(b, 0x0b);                  /* end if */
  emit_snprintf_write_u32_decimal(b, out, count, arg, divisor, started, digit);
}

static void emit_snprintf_load_arg(buf_t *b, uint32_t va, uint32_t arg, int slot) {
  emit_local_get(b, va);
  if (slot != 0) {
    emit_i32_const(b, slot * 8);
    buf_u8(b, 0x6a);                /* i32.add */
  }
  buf_u8(b, 0x29); buf_uleb(b, 3); buf_uleb(b, 0); /* i64.load */
  buf_u8(b, 0xa7);                  /* i32.wrap_i64 */
  emit_local_set(b, arg);
}

static void emit_snprintf_return_count(buf_t *b, type_t *type, uint32_t out, uint32_t count) {
  emit_snprintf_write_byte_const(b, out, count, 0, 0);
  emit_local_get(b, count);
  emit_return_i32_as_result(b, type);
  buf_u8(b, 0x0f);                  /* return */
}

static int make_snprintf_stub_body(str_t name, type_t *type, buf_t *b, size_t *va_global_imm_off) {
  if (!str_eq_lit(name, "snprintf")) return 0;
  runtime_param_count(type, 3, name);
  uint32_t params = wasm_type_param_count(type);
  uint32_t dst = params;
  uint32_t size = dst + 1;
  uint32_t fmt = dst + 2;
  uint32_t va = dst + 3;
  uint32_t out = dst + 4;
  uint32_t count = dst + 5;
  uint32_t arg = dst + 6;
  uint32_t divisor = dst + 7;
  uint32_t started = dst + 8;
  uint32_t digit = dst + 9;

  buf_uleb(b, 1);
  buf_uleb(b, 10);
  buf_u8(b, 0x7f);

  emit_i32_from_param(b, type, 0); emit_local_set(b, dst);
  emit_i32_from_param(b, type, 1); emit_local_set(b, size);
  emit_i32_from_param(b, type, 2); emit_local_set(b, fmt);
  buf_u8(b, 0x23); /* global.get __ag_va_arg_area */
  *va_global_imm_off = buf_uleb5(b, 0);
  emit_local_set(b, va);
  emit_local_get(b, dst); emit_local_set(b, out);
  emit_i32_const(b, 0); emit_local_set(b, count);

  (void)size;

  /* "%%" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_write_byte_const(b, out, count, '%', 1);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%d-%d" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'd'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  emit_local_get(b, fmt); emit_i32_const(b, 2); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '-'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  emit_local_get(b, fmt); emit_i32_const(b, 3); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  emit_local_get(b, fmt); emit_i32_const(b, 4); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'd'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_snprintf_write_i32_decimal(b, out, count, arg, divisor, started, digit);
  emit_snprintf_write_byte_const(b, out, count, '-', 1);
  emit_snprintf_load_arg(b, va, arg, 1);
  emit_snprintf_write_i32_decimal(b, out, count, arg, divisor, started, digit);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%zu" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'z'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  emit_local_get(b, fmt); emit_i32_const(b, 2); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'u'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_snprintf_write_u32_decimal(b, out, count, arg, divisor, started, digit);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%s" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 's'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_snprintf_write_cstr(b, out, count, arg, digit);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%c" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'c'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_snprintf_write_byte_local(b, out, count, arg);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%02d" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '0'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  emit_local_get(b, fmt); emit_i32_const(b, 2); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '2'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  emit_local_get(b, fmt); emit_i32_const(b, 3); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'd'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_local_get(b, arg);
  emit_i32_const(b, 0);
  buf_u8(b, 0x48);                  /* i32.lt_s */
  buf_u8(b, 0x04); buf_u8(b, 0x40); /* if */
  emit_snprintf_write_i32_decimal(b, out, count, arg, divisor, started, digit);
  buf_u8(b, 0x05);                  /* else */
  emit_local_get(b, arg);
  emit_i32_const(b, 10);
  buf_u8(b, 0x49);                  /* i32.lt_u */
  buf_u8(b, 0x04); buf_u8(b, 0x40); /* if */
  emit_snprintf_write_byte_const(b, out, count, '0', 1);
  buf_u8(b, 0x0b);                  /* end if */
  emit_snprintf_write_u32_decimal(b, out, count, arg, divisor, started, digit);
  buf_u8(b, 0x0b);                  /* end if */
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%u" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'u'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_snprintf_write_u32_decimal(b, out, count, arg, divisor, started, digit);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%d" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'd'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_snprintf_write_i32_decimal(b, out, count, arg, divisor, started, digit);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  emit_i32_const(b, 0);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_imaxabs_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "imaxabs")) return 0;
  runtime_param_count(type, 1, name);
  if (wasm_type_param_valtype(type, 0) != 0x7e || wasm_type_result_valtype(type) != 0x7e) {
    die("runtime stub signature mismatch: imaxabs");
  }
  buf_uleb(b, 0);
  emit_local_get(b, 0);
  buf_u8(b, 0x42); buf_sleb_i32(b, 0); /* i64.const 0 */
  buf_u8(b, 0x53);                     /* i64.lt_s */
  buf_u8(b, 0x04); buf_u8(b, 0x7e);    /* if (result i64) */
  buf_u8(b, 0x42); buf_sleb_i32(b, 0); /* i64.const 0 */
  emit_local_get(b, 0);
  buf_u8(b, 0x7d);                     /* i64.sub */
  buf_u8(b, 0x05);                     /* else */
  emit_local_get(b, 0);
  buf_u8(b, 0x0b);                     /* end */
  return 1;
}

static int make_fenv_stub_body(str_t name, type_t *type, buf_t *b) {
  if (str_eq_lit(name, "feclearexcept")) {
    runtime_param_count(type, 1, name);
    buf_uleb(b, 0);
    emit_i32_const(b, 0);
    emit_return_i32_as_result(b, type);
    return 1;
  }
  if (str_eq_lit(name, "fetestexcept")) {
    runtime_param_count(type, 1, name);
    buf_uleb(b, 0);
    emit_i32_from_param(b, type, 0);
    emit_return_i32_as_result(b, type);
    return 1;
  }
  return 0;
}

static int make_locale_stub_body(str_t name, type_t *type, buf_t *b) {
  if (str_eq_lit(name, "setlocale")) {
    runtime_param_count(type, 2, name);
    buf_uleb(b, 0);
    emit_i32_const(b, RUNTIME_SCRATCH_BASE + 112);
    emit_i32_const(b, 'C');
    buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0); /* i32.store8 */
    emit_i32_const(b, RUNTIME_SCRATCH_BASE + 113);
    emit_i32_const(b, 0);
    buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0); /* i32.store8 */
    emit_i32_const(b, RUNTIME_SCRATCH_BASE + 112);
    emit_return_i32_as_result(b, type);
    return 1;
  }
  if (str_eq_lit(name, "localeconv")) {
    buf_uleb(b, 0);
    emit_i32_const(b, RUNTIME_SCRATCH_BASE);
    emit_i32_const(b, RUNTIME_SCRATCH_BASE + 96);
    buf_u8(b, 0x36); buf_uleb(b, 2); buf_uleb(b, 0); /* i32.store */
    emit_i32_const(b, RUNTIME_SCRATCH_BASE + 96);
    emit_i32_const(b, '.');
    buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0); /* i32.store8 */
    emit_i32_const(b, RUNTIME_SCRATCH_BASE + 97);
    emit_i32_const(b, 0);
    buf_u8(b, 0x3a); buf_uleb(b, 0); buf_uleb(b, 0); /* i32.store8 */
    emit_i32_const(b, RUNTIME_SCRATCH_BASE);
    emit_return_i32_as_result(b, type);
    return 1;
  }
  return 0;
}

static int make_wcslen_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "wcslen")) return 0;
  runtime_param_count(type, 1, name);
  uint32_t p = wasm_type_param_count(type);
  uint32_t n = p + 1;
  buf_uleb(b, 1);
  buf_uleb(b, 2);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0); emit_local_set(b, p);
  emit_i32_const(b, 0); emit_local_set(b, n);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  emit_local_get(b, p);
  buf_u8(b, 0x28); buf_uleb(b, 2); buf_uleb(b, 0); /* i32.load */
  buf_u8(b, 0x45);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  emit_local_get(b, n); emit_i32_const(b, 1); buf_u8(b, 0x6a); emit_local_set(b, n);
  emit_local_get(b, p); emit_i32_const(b, 4); buf_u8(b, 0x6a); emit_local_set(b, p);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  emit_local_get(b, n);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_wcscpy_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "wcscpy")) return 0;
  runtime_param_count(type, 2, name);
  uint32_t dst = wasm_type_param_count(type);
  uint32_t src = dst + 1;
  uint32_t d = dst + 2;
  uint32_t s = dst + 3;
  uint32_t ch = dst + 4;
  buf_uleb(b, 1);
  buf_uleb(b, 5);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0); emit_local_set(b, dst);
  emit_i32_from_param(b, type, 1); emit_local_set(b, src);
  emit_local_get(b, dst); emit_local_set(b, d);
  emit_local_get(b, src); emit_local_set(b, s);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  emit_local_get(b, s);
  buf_u8(b, 0x28); buf_uleb(b, 2); buf_uleb(b, 0); /* i32.load */
  emit_local_set(b, ch);
  emit_local_get(b, d);
  emit_local_get(b, ch);
  buf_u8(b, 0x36); buf_uleb(b, 2); buf_uleb(b, 0); /* i32.store */
  emit_local_get(b, ch);
  buf_u8(b, 0x45);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  emit_local_get(b, d); emit_i32_const(b, 4); buf_u8(b, 0x6a); emit_local_set(b, d);
  emit_local_get(b, s); emit_i32_const(b, 4); buf_u8(b, 0x6a); emit_local_set(b, s);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  emit_local_get(b, dst);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_wcscmp_stub_body(str_t name, type_t *type, buf_t *b) {
  if (!str_eq_lit(name, "wcscmp")) return 0;
  runtime_param_count(type, 2, name);
  uint32_t a = wasm_type_param_count(type);
  uint32_t bptr = a + 1;
  uint32_t ca = a + 2;
  uint32_t cb = a + 3;
  buf_uleb(b, 1);
  buf_uleb(b, 4);
  buf_u8(b, 0x7f);
  emit_i32_from_param(b, type, 0); emit_local_set(b, a);
  emit_i32_from_param(b, type, 1); emit_local_set(b, bptr);
  buf_u8(b, 0x02); buf_u8(b, 0x40);
  buf_u8(b, 0x03); buf_u8(b, 0x40);
  emit_local_get(b, a);
  buf_u8(b, 0x28); buf_uleb(b, 2); buf_uleb(b, 0); /* i32.load */
  emit_local_set(b, ca);
  emit_local_get(b, bptr);
  buf_u8(b, 0x28); buf_uleb(b, 2); buf_uleb(b, 0); /* i32.load */
  emit_local_set(b, cb);
  emit_local_get(b, ca); emit_local_get(b, cb); buf_u8(b, 0x47);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_local_get(b, ca); emit_local_get(b, cb); buf_u8(b, 0x6b);
  emit_return_i32_as_result(b, type);
  buf_u8(b, 0x0f);
  buf_u8(b, 0x0b);
  emit_local_get(b, ca);
  buf_u8(b, 0x45);
  buf_u8(b, 0x0d); buf_uleb(b, 1);
  emit_local_get(b, a); emit_i32_const(b, 4); buf_u8(b, 0x6a); emit_local_set(b, a);
  emit_local_get(b, bptr); emit_i32_const(b, 4); buf_u8(b, 0x6a); emit_local_set(b, bptr);
  buf_u8(b, 0x0c); buf_uleb(b, 0);
  buf_u8(b, 0x0b);
  buf_u8(b, 0x0b);
  emit_i32_const(b, 0);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_sprintf_stub_body(str_t name, type_t *type, buf_t *b, size_t *va_global_imm_off) {
  if (!str_eq_lit(name, "sprintf")) return 0;
  runtime_param_count(type, 2, name);
  uint32_t params = wasm_type_param_count(type);
  uint32_t dst = params;
  uint32_t fmt = dst + 1;
  uint32_t va = dst + 2;
  uint32_t out = dst + 3;
  uint32_t count = dst + 4;
  uint32_t arg = dst + 5;
  uint32_t divisor = dst + 6;
  uint32_t started = dst + 7;
  uint32_t digit = dst + 8;

  buf_uleb(b, 1);
  buf_uleb(b, 9);
  buf_u8(b, 0x7f);

  emit_i32_from_param(b, type, 0); emit_local_set(b, dst);
  emit_i32_from_param(b, type, 1); emit_local_set(b, fmt);
  buf_u8(b, 0x23); /* global.get __ag_va_arg_area */
  *va_global_imm_off = buf_uleb5(b, 0);
  emit_local_set(b, va);
  emit_local_get(b, dst); emit_local_set(b, out);
  emit_i32_const(b, 0); emit_local_set(b, count);

  /* "%%" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_write_byte_const(b, out, count, '%', 1);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%s" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 's'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_snprintf_write_cstr(b, out, count, arg, digit);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%c" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'c'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_snprintf_write_byte_local(b, out, count, arg);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%02d" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '0'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  emit_local_get(b, fmt); emit_i32_const(b, 2); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '2'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  emit_local_get(b, fmt); emit_i32_const(b, 3); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'd'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_local_get(b, arg);
  emit_i32_const(b, 0);
  buf_u8(b, 0x48);                  /* i32.lt_s */
  buf_u8(b, 0x04); buf_u8(b, 0x40); /* if */
  emit_snprintf_write_i32_decimal(b, out, count, arg, divisor, started, digit);
  buf_u8(b, 0x05);                  /* else */
  emit_local_get(b, arg);
  emit_i32_const(b, 10);
  buf_u8(b, 0x49);                  /* i32.lt_u */
  buf_u8(b, 0x04); buf_u8(b, 0x40); /* if */
  emit_snprintf_write_byte_const(b, out, count, '0', 1);
  buf_u8(b, 0x0b);                  /* end if */
  emit_snprintf_write_u32_decimal(b, out, count, arg, divisor, started, digit);
  buf_u8(b, 0x0b);                  /* end if */
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%u" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'u'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_snprintf_write_u32_decimal(b, out, count, arg, divisor, started, digit);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  /* "%d" */
  emit_local_get(b, fmt); buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, '%'); buf_u8(b, 0x46);
  emit_local_get(b, fmt); emit_i32_const(b, 1); buf_u8(b, 0x6a);
  buf_u8(b, 0x2d); buf_uleb(b, 0); buf_uleb(b, 0);
  emit_i32_const(b, 'd'); buf_u8(b, 0x46); buf_u8(b, 0x71);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  emit_snprintf_load_arg(b, va, arg, 0);
  emit_snprintf_write_i32_decimal(b, out, count, arg, divisor, started, digit);
  emit_snprintf_return_count(b, type, out, count);
  buf_u8(b, 0x0b);

  emit_i32_const(b, 0);
  emit_return_i32_as_result(b, type);
  return 1;
}

static int make_math_header_stub_body(str_t name, type_t *type, buf_t *b) {
  if (str_eq_lit(name, "sqrt") || str_eq_lit(name, "__ag_complex_sqrt")) {
    runtime_param_count(type, 1, name);
    if (wasm_type_param_valtype(type, 0) != 0x7c || wasm_type_result_valtype(type) != 0x7c) return 0;
    buf_uleb(b, 0);
    emit_local_get(b, 0);
    buf_u8(b, 0x9f); /* f64.sqrt */
    return 1;
  }
  if (str_eq_lit(name, "sqrtf")) {
    runtime_param_count(type, 1, name);
    if (wasm_type_param_valtype(type, 0) != 0x7d || wasm_type_result_valtype(type) != 0x7d) return 0;
    buf_uleb(b, 0);
    emit_local_get(b, 0);
    buf_u8(b, 0x91); /* f32.sqrt */
    return 1;
  }
  if (str_eq_lit(name, "pow")) {
    runtime_param_count(type, 2, name);
    if (wasm_type_result_valtype(type) != 0x7c) return 0;
    buf_uleb(b, 0);
    buf_u8(b, 0x44); /* f64.const */
    buf_u32le(b, 0);
    buf_u32le(b, 0x40900000u); /* 1024.0 */
    return 1;
  }
  if (str_eq_lit(name, "fabs")) {
    runtime_param_count(type, 1, name);
    if (wasm_type_param_valtype(type, 0) != 0x7c || wasm_type_result_valtype(type) != 0x7c) return 0;
    buf_uleb(b, 0);
    emit_local_get(b, 0);
    buf_u8(b, 0x99); /* f64.abs */
    return 1;
  }
  return 0;
}

static int make_file_stub_body(str_t name, type_t *type, buf_t *b) {
  if (str_eq_lit(name, "fgetc") || str_eq_lit(name, "getc")) {
    runtime_param_count(type, 1, name);
    buf_uleb(b, 0);
    buf_u8(b, 0x41);
    buf_sleb_i32(b, -1);
    emit_return_i32_as_result(b, type);
    return 1;
  }
  if (str_eq_lit(name, "fgets")) {
    runtime_param_count(type, 3, name);
    buf_uleb(b, 0);
    buf_u8(b, 0x41);
    buf_sleb_i32(b, 0);
    emit_return_i32_as_result(b, type);
    return 1;
  }
  return 0;
}

static unsigned char *make_runtime_stub_body(str_t name, type_t *type, size_t *out_len,
                                             size_t *out_va_global_imm_off) {
  buf_t b = {0};
  *out_va_global_imm_off = (size_t)-1;
  if (str_eq_lit(name, "__assert_rtn")) {
    buf_uleb(&b, 0); /* local decl count */
    buf_u8(&b, 0x00); /* unreachable */
  } else if (str_eq_lit(name, "__agc_runtime_trap")) {
    buf_uleb(&b, 0); /* local decl count */
    buf_u8(&b, 0x00); /* unreachable */
  } else if (make_host_write_memory_sink_body(name, type, &b)) {
  } else if (make_printf_stub_body(name, type, &b)) {
  } else if (make_strlen_stub_body(name, type, &b)) {
  } else if (make_strcmp_stub_body(name, type, &b)) {
  } else if (make_memset_stub_body(name, type, &b)) {
  } else if (make_memcpy_stub_body(name, type, &b)) {
  } else if (make_abs_stub_body(name, type, &b)) {
  } else if (make_ctype_stub_body(name, type, &b)) {
  } else if (make_imaxabs_stub_body(name, type, &b)) {
  } else if (make_fenv_stub_body(name, type, &b)) {
  } else if (make_locale_stub_body(name, type, &b)) {
  } else if (make_malloc_stub_body(name, type, &b)) {
  } else if (make_free_stub_body(name, type, &b)) {
  } else if (make_calloc_stub_body(name, type, &b)) {
  } else if (make_atoi_stub_body(name, type, &b)) {
  } else if (make_strcpy_stub_body(name, type, &b)) {
  } else if (make_strncpy_stub_body(name, type, &b)) {
  } else if (make_strcat_stub_body(name, type, &b)) {
  } else if (make_strncmp_stub_body(name, type, &b)) {
  } else if (make_memcmp_stub_body(name, type, &b)) {
  } else if (make_strchr_stub_body(name, type, &b)) {
  } else if (make_putchar_stub_body(name, type, &b)) {
  } else if (make_sprintf_stub_body(name, type, &b, out_va_global_imm_off)) {
  } else if (make_snprintf_stub_body(name, type, &b, out_va_global_imm_off)) {
  } else if (make_wcslen_stub_body(name, type, &b)) {
  } else if (make_wcscpy_stub_body(name, type, &b)) {
  } else if (make_wcscmp_stub_body(name, type, &b)) {
  } else if (make_math_header_stub_body(name, type, &b)) {
  } else if (make_file_stub_body(name, type, &b)) {
  } else {
    buf_uleb(&b, 0); /* local decl count */
    unsigned char result = wasm_type_result_valtype(type);
    if (result == 0x7f) {
      buf_u8(&b, 0x41);
      buf_sleb_i32(&b, 1);
    } else if (result == 0x7e) {
      buf_u8(&b, 0x42);
      buf_sleb_i32(&b, 1);
    } else if (result == 0x7d) {
      buf_u8(&b, 0x43);
      buf_u32le(&b, 0);
    } else if (result == 0x7c) {
      buf_u8(&b, 0x44);
      buf_u32le(&b, 0);
      buf_u32le(&b, 0);
    } else if (result != 0) {
      die("unsupported runtime stub result type");
    }
  }
  buf_u8(&b, 0x0b);
  *out_len = b.len;
  return b.data;
}

static void add_runtime_data_symbol(object_t *runtime, str_t name) {
  if (runtime_has_data(runtime, name)) return;
  data_seg_t d = {0};
  d.name = str_dup(name.s, name.len);
  d.bytes = xmalloc(4);
  memset(d.bytes, 0, 4);
  d.size = 4;
  d.alloc_size = 4;
  d.align_log2 = 2;
  d.defined = 1;
  int data_index = runtime->data_count;
  PUSH(runtime->data, runtime->data_count, runtime->data_cap, d);

  symbol_t sym = {0};
  sym.kind = SYM_DATA;
  sym.name = str_dup(name.s, name.len);
  sym.index = data_index;
  sym.data_offset = 0;
  sym.data_size = 4;
  PUSH(runtime->symbols, runtime->symbol_count, runtime->symbol_cap, sym);
}

static size_t runtime_next_code_payload_off(object_t *runtime) {
  size_t off = 1; /* synthetic code section function-count LEB */
  for (int i = 0; i < runtime->func_count; i++) {
    if (!runtime->funcs[i].defined) continue;
    off += 5 + runtime->funcs[i].body_len; /* body size LEB + body payload */
  }
  return off + 5; /* next body payload start after its size LEB */
}

static int runtime_global_symbol(object_t *runtime, const char *name) {
  str_t n = str_dup(name, (int)strlen(name));
  for (int i = 0; i < runtime->symbol_count; i++) {
    symbol_t *sym = &runtime->symbols[i];
    if (sym->kind == SYM_GLOBAL && str_eq(sym->name, n)) return i;
  }

  global_sym_t g = {0};
  g.name = str_dup(n.s, n.len);
  g.final_index = -1;
  int global_index = runtime->global_count;
  PUSH(runtime->globals, runtime->global_count, runtime->global_cap, g);

  symbol_t sym = {0};
  sym.kind = SYM_GLOBAL;
  sym.flags = SYM_UNDEFINED;
  sym.name = str_dup(n.s, n.len);
  sym.index = global_index;
  int sym_index = runtime->symbol_count;
  PUSH(runtime->symbols, runtime->symbol_count, runtime->symbol_cap, sym);
  return sym_index;
}

static void add_runtime_code_reloc(object_t *runtime, int type, size_t code_payload_off,
                                   size_t body_off, int symbol) {
  reloc_t r = {0};
  r.is_code = 1;
  r.type = type;
  r.offset = (uint32_t)(code_payload_off + body_off);
  r.symbol = (uint32_t)symbol;
  PUSH(runtime->relocs, runtime->reloc_count, runtime->reloc_cap, r);
}

static int add_runtime_undefined_func_symbol(object_t *runtime, str_t name, type_t *type) {
  for (int i = 0; i < runtime->symbol_count; i++) {
    symbol_t *sym = &runtime->symbols[i];
    if (sym->kind == SYM_FUNCTION && (sym->flags & SYM_UNDEFINED) && str_eq(sym->name, name)) return i;
  }

  int type_index = push_type_copy(&runtime->types, &runtime->type_count, &runtime->type_cap,
                                  type->raw, type->raw_len);

  func_t f = {0};
  f.name = str_dup(name.s, name.len);
  f.type_index = type_index;
  f.defined = 0;
  int func_index = runtime->func_count;
  PUSH(runtime->funcs, runtime->func_count, runtime->func_cap, f);

  symbol_t sym = {0};
  sym.kind = SYM_FUNCTION;
  sym.flags = SYM_UNDEFINED;
  sym.name = str_dup(name.s, name.len);
  sym.index = func_index;
  int sym_index = runtime->symbol_count;
  PUSH(runtime->symbols, runtime->symbol_count, runtime->symbol_cap, sym);
  return sym_index;
}

static int emit_runtime_libc_bridge(object_t *objs, int obj_count, object_t *runtime,
                                    str_t name, type_t *caller_type, buf_t *b,
                                    int *out_target_sym, size_t *out_call_imm_off) {
  const runtime_symbol_manifest_entry_t *entry = find_runtime_func_symbol(name);
  if (!entry || entry->kind != RUNTIME_SYMBOL_BRIDGE) return 0;
  const char *target_lit = entry->target;

  str_t target_name = str_dup(target_lit, (int)strlen(target_lit));
  object_t *target_obj = NULL;
  int target_func = -1;
  if (!find_defined_func(objs, obj_count, target_name, &target_obj, &target_func)) return 0;
  type_t *target_type = &target_obj->types[target_obj->funcs[target_func].type_index];
  if (!runtime_manifest_signature_matches(entry, target_type, 0)) {
    fprintf(stderr, "runtime implementation signature mismatch: %.*s -> %s\n",
            name.len, name.s, target_lit);
    exit(1);
  }
  if (!runtime_manifest_signature_matches(entry, caller_type, 1)) return 0;
  uint32_t caller_params = wasm_type_param_count(caller_type);
  uint32_t target_params = wasm_type_param_count(target_type);
  if (caller_params != target_params) return 0;
  unsigned char caller_result = wasm_type_result_valtype(caller_type);
  unsigned char target_result = wasm_type_result_valtype(target_type);
  if (caller_result != target_result) {
    int integer_pair = (caller_result == 0x7f || caller_result == 0x7e) &&
                       (target_result == 0x7f || target_result == 0x7e);
    if (!integer_pair) return 0;
  }

  buf_uleb(b, 0);
  for (uint32_t i = 0; i < caller_params; i++) {
    unsigned char src = wasm_type_param_valtype(caller_type, i);
    unsigned char dst = wasm_type_param_valtype(target_type, i);
    emit_local_get(b, i);
    if (src == dst) {
      continue;
    } else if (src == 0x7f && dst == 0x7e) {
      buf_u8(b, 0xad); /* i64.extend_i32_u */
    } else if (src == 0x7e && dst == 0x7f) {
      buf_u8(b, 0xa7); /* i32.wrap_i64 */
    } else {
      return 0;
    }
  }
  buf_u8(b, 0x10); /* call */
  *out_call_imm_off = buf_uleb5(b, 0);
  if (target_result == 0x7f && caller_result == 0x7e) {
    buf_u8(b, 0xad); /* i64.extend_i32_u */
  } else if (target_result == 0x7e && caller_result == 0x7f) {
    buf_u8(b, 0xa7); /* i32.wrap_i64 */
  }
  *out_target_sym = add_runtime_undefined_func_symbol(runtime, target_name, target_type);
  return 1;
}

static void add_runtime_func_symbol(object_t *objs, int obj_count, object_t *runtime,
                                    str_t name, type_t *type) {
  if (runtime_has_func(runtime, name)) return;
  int type_index = push_type_copy(&runtime->types, &runtime->type_count, &runtime->type_cap,
                                  type->raw, type->raw_len);

  func_t f = {0};
  f.name = str_dup(name.s, name.len);
  f.type_index = type_index;
  f.defined = 1;
  f.code_payload_off = runtime_next_code_payload_off(runtime);
  size_t va_global_imm_off = (size_t)-1;
  int target_sym = -1;
  size_t call_imm_off = (size_t)-1;
  buf_t bridge = {0};
  if (emit_runtime_libc_bridge(objs, obj_count, runtime, name, &runtime->types[type_index],
                               &bridge, &target_sym, &call_imm_off)) {
    buf_u8(&bridge, 0x0b);
    f.body = bridge.data;
    f.body_len = bridge.len;
  } else {
    f.body = make_runtime_stub_body(name, &runtime->types[type_index], &f.body_len, &va_global_imm_off);
  }
  int func_index = runtime->func_count;
  PUSH(runtime->funcs, runtime->func_count, runtime->func_cap, f);
  if (call_imm_off != (size_t)-1) {
    add_runtime_code_reloc(runtime, R_WASM_FUNCTION_INDEX_LEB, f.code_payload_off, call_imm_off, target_sym);
  }
  if (va_global_imm_off != (size_t)-1) {
    int sym = runtime_global_symbol(runtime, "__ag_va_arg_area");
    add_runtime_code_reloc(runtime, R_WASM_GLOBAL_INDEX_LEB, f.code_payload_off, va_global_imm_off, sym);
  }

  symbol_t sym = {0};
  sym.kind = SYM_FUNCTION;
  sym.name = str_dup(name.s, name.len);
  sym.index = func_index;
  PUSH(runtime->symbols, runtime->symbol_count, runtime->symbol_cap, sym);
}

static int maybe_add_main_wrapper(object_t *objs, int obj_count, const char *export_name,
                                  object_t *runtime, func_t **out_wrapper) {
  *out_wrapper = NULL;
  if (!export_name || strcmp(export_name, "main") != 0) return 0;
  str_t main_name = str_dup("main", 4);
  object_t *main_obj = NULL;
  int main_func = -1;
  if (!find_defined_func(objs, obj_count, main_name, &main_obj, &main_func)) return 0;
  func_t *main_f = &main_obj->funcs[main_func];
  if (main_f->type_index < 0 || main_f->type_index >= main_obj->type_count) die("bad main type index");
  type_t *main_type = &main_obj->types[main_f->type_index];
  if (wasm_type_param_count(main_type) == 0) return 0;
  if (wasm_type_result_valtype(main_type) != 0x7f) return 0;

  unsigned char wrapper_type_raw[] = {0x60, 0x00, 0x01, 0x7f}; /* () -> i32 */
  int type_index = push_type_copy(&runtime->types, &runtime->type_count, &runtime->type_cap,
                                  wrapper_type_raw, sizeof(wrapper_type_raw));

  func_t f = {0};
  f.name = str_dup("main", 4);
  f.type_index = type_index;
  f.defined = 1;
  int func_index = runtime->func_count;
  PUSH(runtime->funcs, runtime->func_count, runtime->func_cap, f);
  *out_wrapper = &runtime->funcs[func_index];
  return 1;
}

static void fill_main_wrapper_body(func_t *wrapper, type_t *main_type, int main_final_index) {
  buf_t b = {0};
  buf_uleb(&b, 0); /* local decl count */
  uint32_t params = wasm_type_param_count(main_type);
  for (uint32_t i = 0; i < params; i++) {
    unsigned char ty = wasm_type_param_valtype(main_type, i);
    if (ty == 0x7e) {
      buf_u8(&b, 0x42);
      buf_sleb_i32(&b, 0);
    } else if (ty == 0x7f) {
      buf_u8(&b, 0x41);
      buf_sleb_i32(&b, 0);
    } else {
      die("unsupported main wrapper parameter type");
    }
  }
  buf_u8(&b, 0x10); /* call */
  buf_uleb(&b, (uint32_t)main_final_index);
  buf_u8(&b, 0x0b);
  wrapper->body = b.data;
  wrapper->body_len = b.len;
}

static void synthesize_runtime_object(object_t *objs, int obj_count,
                                      object_t *runtime,
                                      int use_memory_stdio_sink) {
  const char *runtime_path = "<ag_wasm_link_runtime>";
  runtime->path = str_dup(runtime_path, (int)strlen(runtime_path));
  runtime->code_section_index = -1;
  runtime->data_section_index = -1;
  for (int oi = 0; oi < obj_count; oi++) {
    object_t *o = &objs[oi];
    for (int si = 0; si < o->symbol_count; si++) {
      symbol_t *sym = &o->symbols[si];
      if (sym->kind == SYM_DATA && (sym->flags & SYM_UNDEFINED) &&
          is_runtime_data_symbol(sym->name) &&
          !find_defined_data(objs, obj_count, sym->name, NULL, NULL, NULL)) {
        add_runtime_data_symbol(runtime, sym->name);
      } else if (sym->kind == SYM_FUNCTION && (sym->flags & SYM_UNDEFINED) &&
                 use_memory_stdio_sink &&
                 str_eq_lit(sym->name, "__agc_host_write") &&
                 !find_defined_func(objs, obj_count, sym->name, NULL, NULL)) {
        if (sym->index < 0 || sym->index >= o->func_count)
          die("bad stdio host write symbol index");
        int type_index = o->funcs[sym->index].type_index;
        if (type_index < 0 || type_index >= o->type_count)
          die("bad stdio host write type index");
        add_runtime_func_symbol(objs, obj_count, runtime, sym->name,
                                &o->types[type_index]);
      } else if (sym->kind == SYM_FUNCTION && (sym->flags & SYM_UNDEFINED) &&
                 is_unsupported_control_flow_symbol(sym->name) &&
                 !find_defined_func(objs, obj_count, sym->name, NULL, NULL)) {
        dief("unsupported C control-flow function: %s requires compiler support", sym->name.s);
      } else if (sym->kind == SYM_FUNCTION && (sym->flags & SYM_UNDEFINED) &&
                 is_runtime_func_symbol(sym->name) &&
                 !find_defined_func(objs, obj_count, sym->name, NULL, NULL)) {
        if (sym->index < 0 || sym->index >= o->func_count) die("bad undefined function symbol index");
        int type_index = o->funcs[sym->index].type_index;
        if (type_index < 0 || type_index >= o->type_count) die("bad function type index");
        add_runtime_func_symbol(objs, obj_count, runtime, sym->name, &o->types[type_index]);
      }
    }
  }
}

static int find_final_global(final_global_t *globals, int count, str_t name) {
  for (int i = 0; i < count; i++) if (str_eq(globals[i].name, name)) return i;
  return -1;
}

static uint32_t default_global_init(str_t name) {
  if (name.len == 15 && memcmp(name.s, "__stack_pointer", 15) == 0) return 65536;
  return 0;
}

static int is_stack_pointer_name(str_t name) {
  return name.len == 15 && memcmp(name.s, "__stack_pointer", 15) == 0;
}

static symbol_t *reloc_symbol(object_t *o, reloc_t *r) {
  if (r->symbol >= (uint32_t)o->symbol_count) die("relocation symbol index out of range");
  return &o->symbols[r->symbol];
}

static uint32_t checked_add_u32(uint32_t a, uint32_t b, const char *msg) {
  if (b > UINT32_MAX - a) die(msg);
  return a + b;
}

static uint32_t checked_add_i32(uint32_t a, int32_t b, const char *msg) {
  if (b < 0) {
    uint32_t neg = (uint32_t)(-(int64_t)b);
    if (a < neg) die(msg);
    return a - neg;
  }
  return checked_add_u32(a, (uint32_t)b, msg);
}

static uint32_t align_to_u32_checked(uint32_t v, uint32_t a, const char *msg) {
  if (a <= 1) return v;
  uint32_t rem = v % a;
  if (rem == 0) return v;
  return checked_add_u32(v, a - rem, msg);
}

static int find_import(final_import_t *imports, int count, str_t name, int type_index) {
  for (int i = 0; i < count; i++) {
    if (imports[i].type_index == type_index && str_eq(imports[i].name, name)) return i;
  }
  return -1;
}

static int find_import_by_name(final_import_t *imports, int count, str_t name) {
  for (int i = 0; i < count; i++) {
    if (str_eq(imports[i].name, name)) return i;
  }
  return -1;
}

static void add_unresolved_function_imports(object_t *objs, int obj_count,
                                            final_import_t **imports, int *import_count, int *import_cap,
                                            type_t **types, int *type_count, int *type_cap) {
  for (int oi = 0; oi < obj_count; oi++) {
    object_t *o = &objs[oi];
    for (int si = 0; si < o->symbol_count; si++) {
      symbol_t *sym = &o->symbols[si];
      if (sym->kind != SYM_FUNCTION || !(sym->flags & SYM_UNDEFINED)) continue;
      object_t *def_obj = NULL;
      int def_func = -1;
      if (sym->index < 0 || sym->index >= o->func_count) die("bad undefined function symbol index");
      if (find_defined_func(objs, obj_count, sym->name, &def_obj, &def_func)) {
        if (!func_signature_matches(o, sym->index, def_obj, def_func)) {
          dief("function signature mismatch: %s", sym->name.s);
        }
        continue;
      }
      int final_type = intern_type(types, type_count, type_cap, &o->types[o->funcs[sym->index].type_index]);
      int existing =
          find_import(*imports, *import_count, sym->name, final_type);
      if (existing >= 0) {
        if (!func_signature_matches(
                o, sym->index, (*imports)[existing].obj,
                (*imports)[existing].func_index))
          dief("function signature mismatch: %s", sym->name.s);
        continue;
      }
      int existing_name = find_import_by_name(*imports, *import_count, sym->name);
      if (existing_name >= 0 && (*imports)[existing_name].type_index != final_type) {
        dief("function signature mismatch: %s", sym->name.s);
      }
      final_import_t imp = {0};
      imp.obj = o;
      imp.func_index = sym->index;
      imp.name = str_dup(sym->name.s, sym->name.len);
      imp.type_index = final_type;
      imp.final_index = *import_count;
      PUSH(*imports, *import_count, *import_cap, imp);
    }
  }
}

static int final_func_index_for_symbol(object_t *objs, int obj_count, object_t *cur, symbol_t *sym,
                                       final_import_t **imports, int *import_count,
                                       type_t **types, int *type_count, int *type_cap) {
  object_t *def_obj = NULL;
  int def_func = -1;
  if (sym->kind != SYM_FUNCTION) die("function relocation does not point at function symbol");
  if (!(sym->flags & SYM_UNDEFINED)) {
    die("internal error: defined function symbol should already have a final index");
  }
  if (sym->index < 0 || sym->index >= cur->func_count) die("bad undefined function symbol index");
  if (find_defined_func(objs, obj_count, sym->name, &def_obj, &def_func)) {
    if (!func_signature_matches(cur, sym->index, def_obj, def_func)) {
      dief("function signature mismatch: %s", sym->name.s);
    }
    return def_obj->funcs[def_func].final_index;
  }
  int obj_type = -1;
  for (int oi = 0; oi < obj_count; oi++) {
    for (int fi = 0; fi < objs[oi].func_count; fi++) {
      if (!objs[oi].funcs[fi].defined && str_eq(objs[oi].funcs[fi].name, sym->name)) {
        obj_type = intern_type(types, type_count, type_cap,
                               &objs[oi].types[objs[oi].funcs[fi].type_index]);
        int existing = find_import(*imports, *import_count, sym->name, obj_type);
        if (existing >= 0) return (*imports)[existing].final_index;
        dief("uncollected function import: %s", sym->name.s);
      }
    }
  }
  dief("unresolved function symbol: %s", sym->name.s);
  return -1;
}

static int final_func_index_for_reloc_symbol(object_t *objs, int obj_count, object_t *cur,
                                             symbol_t *sym,
                                             final_import_t **imports, int *import_count,
                                             type_t **types, int *type_count, int *type_cap) {
  if (sym->kind != SYM_FUNCTION) die("function relocation does not point at function symbol");
  if (sym->flags & SYM_UNDEFINED) {
    return final_func_index_for_symbol(objs, obj_count, cur, sym, imports, import_count,
                                       types, type_count, type_cap);
  }
  if (sym->index < 0 || sym->index >= cur->func_count) die("bad function symbol index");
  return cur->funcs[sym->index].final_index;
}

static func_t *func_for_reloc_symbol(object_t *objs, int obj_count, object_t *cur, symbol_t *sym,
                                     object_t **out_obj) {
  if (sym->kind != SYM_FUNCTION) die("function relocation does not point at function symbol");
  if (sym->flags & SYM_UNDEFINED) {
    object_t *def_obj = NULL;
    int def_func = -1;
    if (find_defined_func(objs, obj_count, sym->name, &def_obj, &def_func)) {
      if (out_obj) *out_obj = def_obj;
      return &def_obj->funcs[def_func];
    }
  }
  if (sym->index < 0 || sym->index >= cur->func_count) die("bad function symbol index");
  if (out_obj) *out_obj = cur;
  return &cur->funcs[sym->index];
}

static uint32_t final_data_addr_for_symbol(object_t *objs, int obj_count, object_t *cur,
                                           symbol_t *sym, int32_t addend) {
  object_t *def_obj = cur;
  int data_index = sym->index;
  uint32_t symbol_offset = sym->data_offset;
  if (sym->kind != SYM_DATA) die("memory relocation does not point at data symbol");
  if (sym->flags & SYM_UNDEFINED) {
    if (!find_defined_data(objs, obj_count, sym->name, &def_obj, &data_index, &symbol_offset)) {
      dief("unresolved data symbol: %s", sym->name.s);
    }
    const data_signature_t *reference =
        find_data_signature(cur, sym->name);
    const data_signature_t *definition =
        find_data_signature(def_obj, sym->name);
    if (symbol_offset == 0 &&
        !is_runtime_data_symbol(sym->name) &&
        !data_signatures_compatible(
            reference, cur->data_layout_version,
            definition, def_obj->data_layout_version))
      dief("data signature mismatch: %s", sym->name.s);
  }
  if (data_index < 0 || data_index >= def_obj->data_count || !def_obj->data[data_index].defined) {
    dief("bad data symbol: %s", sym->name.s);
  }
  uint32_t addr = checked_add_u32(def_obj->data[data_index].final_addr, symbol_offset,
                                  "data relocation address overflow");
  return checked_add_i32(addr, addend, "data relocation address overflow");
}

static int intern_table_func(final_table_func_t **table_funcs, int *table_count, int *table_cap,
                             object_t *obj, int func_index, int final_func_index) {
  func_t *f = &obj->funcs[func_index];
  if (f->final_table_index > 0) return f->final_table_index;
  for (int i = 0; i < *table_count; i++) {
    if ((*table_funcs)[i].final_func_index == final_func_index) {
      f->final_table_index = (*table_funcs)[i].table_index;
      return f->final_table_index;
    }
  }
  final_table_func_t tf = {0};
  tf.obj = obj;
  tf.func_index = func_index;
  tf.final_func_index = final_func_index;
  tf.table_index = *table_count + 2;
  f->final_table_index = tf.table_index;
  PUSH(*table_funcs, *table_count, *table_cap, tf);
  return tf.table_index;
}

static int table_index_for_symbol(object_t *objs, int obj_count, object_t *cur, symbol_t *sym,
                                  final_import_t **imports, int *import_count,
                                  type_t **types, int *type_count, int *type_cap,
                                  final_table_func_t **table_funcs, int *table_count, int *table_cap) {
  int final_func_index = final_func_index_for_reloc_symbol(objs, obj_count, cur, sym, imports,
                                                          import_count, types, type_count, type_cap);
  object_t *target_obj = NULL;
  func_t *f = func_for_reloc_symbol(objs, obj_count, cur, sym, &target_obj);
  return intern_table_func(table_funcs, table_count, table_cap, target_obj,
                           (int)(f - target_obj->funcs), final_func_index);
}

static void patch_object_relocations(object_t *objs, int obj_count,
                                     final_import_t **imports, int *import_count,
                                     type_t **types, int *type_count, int *type_cap,
                                     final_global_t **globals, int *global_count, int *global_cap,
                                     final_table_func_t **table_funcs, int *table_count, int *table_cap) {
  for (int oi = 0; oi < obj_count; oi++) {
    object_t *o = &objs[oi];
    for (int ri = 0; ri < o->reloc_count; ri++) {
      reloc_t *r = &o->relocs[ri];
      if (r->is_code) {
        func_t *fn = NULL;
        size_t body_off = 0;
        for (int fi = 0; fi < o->func_count; fi++) {
          func_t *cand = &o->funcs[fi];
          if (!cand->defined) continue;
          if (r->offset >= cand->code_payload_off &&
              r->offset < cand->code_payload_off + cand->body_len) {
            fn = cand;
            body_off = r->offset - cand->code_payload_off;
            break;
          }
        }
        if (!fn) die("code relocation offset does not map to a function body");
        if (body_off + 5 > fn->body_len) die("code relocation immediate out of range");
        if (r->type == R_WASM_TYPE_INDEX_LEB) {
          if (r->symbol >= (uint32_t)o->type_count) die("type relocation index out of range");
          patch_uleb5(fn->body + body_off, (uint32_t)o->type_map[r->symbol]);
        } else if (r->type == R_WASM_FUNCTION_INDEX_LEB) {
          symbol_t *sym = reloc_symbol(o, r);
          uint32_t idx = 0;
          if (sym->flags & SYM_UNDEFINED) {
            idx = (uint32_t)final_func_index_for_symbol(objs, obj_count, o, sym, imports, import_count,
                                                        types, type_count, type_cap);
          } else {
            if (sym->index < 0 || sym->index >= o->func_count) die("bad function symbol index");
            idx = (uint32_t)o->funcs[sym->index].final_index;
          }
          patch_uleb5(fn->body + body_off, idx);
        } else if (r->type == R_WASM_TABLE_INDEX_SLEB) {
          symbol_t *sym = reloc_symbol(o, r);
          uint32_t idx = (uint32_t)table_index_for_symbol(objs, obj_count, o, sym, imports, import_count,
                                                          types, type_count, type_cap,
                                                          table_funcs, table_count, table_cap);
          patch_uleb5(fn->body + body_off, idx);
        } else if (r->type == R_WASM_MEMORY_ADDR_LEB) {
          symbol_t *sym = reloc_symbol(o, r);
          uint32_t addr = final_data_addr_for_symbol(objs, obj_count, o, sym, r->addend);
          patch_uleb5(fn->body + body_off, addr);
        } else if (r->type == R_WASM_GLOBAL_INDEX_LEB) {
          symbol_t *sym = reloc_symbol(o, r);
          if (sym->kind != SYM_GLOBAL) die("global relocation does not point at global symbol");
          int gi = find_final_global(*globals, *global_count, sym->name);
          if (gi < 0) {
            final_global_t g = {0};
            g.name = str_dup(sym->name.s, sym->name.len);
            g.init_value = default_global_init(g.name);
            g.final_index = *global_count;
            PUSH(*globals, *global_count, *global_cap, g);
            gi = g.final_index;
          }
          patch_uleb5(fn->body + body_off, (uint32_t)gi);
        } else {
          die("unsupported code relocation type");
        }
      } else {
        data_seg_t *seg = NULL;
        size_t data_off = 0;
        for (int di = 0; di < o->data_count; di++) {
          data_seg_t *cand = &o->data[di];
          if (!cand->defined) continue;
          if (r->offset >= cand->data_payload_off &&
              r->offset < cand->data_payload_off + cand->size) {
            seg = cand;
            data_off = r->offset - cand->data_payload_off;
            break;
          }
        }
        if (!seg || data_off + 4 > seg->size) die("data relocation offset out of range");
        if (r->type == R_WASM_MEMORY_ADDR_I32) {
          symbol_t *sym = reloc_symbol(o, r);
          uint32_t addr = final_data_addr_for_symbol(objs, obj_count, o, sym, r->addend);
          patch_u32le(seg->bytes + data_off, addr);
        } else if (r->type == R_WASM_TABLE_INDEX_I32) {
          symbol_t *sym = reloc_symbol(o, r);
          uint32_t idx = (uint32_t)table_index_for_symbol(objs, obj_count, o, sym, imports, import_count,
                                                          types, type_count, type_cap,
                                                          table_funcs, table_count, table_cap);
          patch_u32le(seg->bytes + data_off, idx);
        } else {
          die("unsupported data relocation type");
        }
      }
    }
  }
}

static void write_output(const char *path, buf_t *out) {
  FILE *f = fopen(path, "wb");
  if (!f) dief("failed to open output: %s", path);
  if (fwrite(out->data, 1, out->len, f) != out->len) dief("failed to write output: %s", path);
  fclose(f);
}

static int export_list_contains(const export_spec_t *exports, int export_count,
                                const char *name) {
  for (int i = 0; i < export_count; i++) {
    if (strcmp(exports[i].name, name) == 0) return 1;
  }
  return 0;
}

static const c_signature_t *find_c_signature(const object_t *obj, str_t name) {
  for (int i = 0; i < obj->c_signature_count; i++) {
    if (str_eq(obj->c_signatures[i].name, name)) return &obj->c_signatures[i];
  }
  return NULL;
}

static const c_signature_t *find_abi_layout_signature(
    const object_t *obj, str_t name) {
  for (int index = 0;
       index < obj->abi_layout_signature_count;
       index++) {
    if (str_eq(
            obj->abi_layout_signatures[index].name, name))
      return &obj->abi_layout_signatures[index];
  }
  return NULL;
}

static const data_signature_t *find_data_signature(
    const object_t *obj, str_t name) {
  for (int index = 0;
       index < obj->data_signature_count; index++) {
    if (str_eq(obj->data_signatures[index].name, name))
      return &obj->data_signatures[index];
  }
  return NULL;
}

static int find_symbol_kind(object_t *objs, int obj_count, str_t name,
                            int kind, int require_undefined) {
  for (int oi = 0; oi < obj_count; oi++) {
    for (int si = 0; si < objs[oi].symbol_count; si++) {
      symbol_t *sym = &objs[oi].symbols[si];
      if (sym->kind != kind || !str_eq(sym->name, name)) continue;
      if (require_undefined < 0 ||
          ((sym->flags & SYM_UNDEFINED) != 0) == require_undefined) {
        return 1;
      }
    }
  }
  return 0;
}

static void validate_export_c_signature(const export_spec_t *export_spec,
                                        object_t *objs, int obj_count) {
  if (!export_spec->signature) return;
  str_t name = str_dup(export_spec->name, (int)strlen(export_spec->name));
  object_t *def_obj = NULL;
  int def_func = -1;
  if (!find_defined_func(objs, obj_count, name, &def_obj, &def_func)) {
    if (find_defined_data(objs, obj_count, name, NULL, NULL, NULL))
      dief("signed export refers to a data symbol: %s", export_spec->name);
    if (find_symbol_kind(objs, obj_count, name, SYM_FUNCTION, 1))
      dief("signed export refers to an import-only function: %s", export_spec->name);
    if (find_symbol_kind(objs, obj_count, name, SYM_DATA, 1))
      dief("signed export refers to an undefined data symbol: %s", export_spec->name);
    die_link_diagnostic_missing_export(export_spec->name, 1);
  }
  const c_signature_t *actual = find_c_signature(def_obj, name);
  if (!actual) dief("C signature metadata not found for export: %s", export_spec->name);
  if ((int)strlen(export_spec->signature) != actual->signature.len ||
      memcmp(export_spec->signature, actual->signature.s,
             (size_t)actual->signature.len) != 0) {
    fprintf(stderr,
            "ag_wasm_link: export C signature mismatch for %s: expected %s, actual %s\n",
            export_spec->name, export_spec->signature, actual->signature.s);
    exit(1);
  }
  (void)def_func;
}

static void patch_runtime_layout_value(final_data_t *datas, int data_count,
                                       const char *name, uint64_t value) {
  size_t name_len = strlen(name);
  for (int i = 0; i < data_count; i++) {
    data_seg_t *data = &datas[i].obj->data[datas[i].data_index];
    if ((size_t)data->name.len != name_len ||
        memcmp(data->name.s, name, name_len) != 0) continue;
    if (data->size < 8) die("runtime layout value has invalid size");
    for (int byte = 0; byte < 8; byte++)
      data->bytes[byte] = (unsigned char)(value >> (8 * byte));
    return;
  }
}

static int has_runtime_layout_value(
    final_data_t *datas, int data_count, const char *name) {
  size_t name_len = strlen(name);
  for (int i = 0; i < data_count; i++) {
    data_seg_t *data = &datas[i].obj->data[datas[i].data_index];
    if ((size_t)data->name.len == name_len &&
        memcmp(data->name.s, name, name_len) == 0)
      return 1;
  }
  return 0;
}

static void validate_continuation_metadata(object_t *objs, int obj_count) {
  object_t *owner = NULL;
  for (int i = 0; i < obj_count; i++) {
    if (!objs[i].has_continuation) continue;
    if (owner) die("multiple continuation entries in linked objects");
    owner = &objs[i];
  }
  if (!owner) return;
  str_t required = owner->continuation_step;
  if (!find_defined_func(objs, obj_count, required, NULL, NULL))
    dief("continuation metadata function not found: %s", required.s);
  required = owner->continuation_start;
  if (!find_defined_func(objs, obj_count, required, NULL, NULL))
    dief("continuation metadata function not found: %s", required.s);
  required = owner->continuation_resume;
  if (!find_defined_func(objs, obj_count, required, NULL, NULL))
    dief("continuation metadata function not found: %s", required.s);
  required = owner->continuation_status;
  if (!find_defined_func(objs, obj_count, required, NULL, NULL))
    dief("continuation metadata function not found: %s", required.s);
  required = owner->continuation_result;
  if (!find_defined_func(objs, obj_count, required, NULL, NULL))
    dief("continuation metadata function not found: %s", required.s);
  for (int oi = 0; oi < obj_count; oi++) {
    for (int si = 0; si < objs[oi].symbol_count; si++) {
      symbol_t *symbol = &objs[oi].symbols[si];
      if (symbol->kind == SYM_FUNCTION &&
          (symbol->flags & SYM_UNDEFINED) &&
          str_eq(symbol->name, owner->continuation_condition)) {
        die_link_diagnostic_one_object(
            "AGC_LINK_FRAME_CONDITION_OUTSIDE_LOOP",
            symbol->name, oi);
      }
      if (symbol->kind == SYM_FUNCTION &&
          (symbol->flags & SYM_UNDEFINED) &&
          str_eq(symbol->name, owner->continuation_entry)) {
        dief("continuation entry cannot be called from C: %s",
             symbol->name.s);
      }
    }
  }
}

static void build_module_into(buf_t *out, const export_spec_t *exports, int export_count,
                              object_t *objs, int obj_count, int use_stdlib,
                              const linker_options_t *requested_options,
                              size_t max_output_bytes) {
  linker_options_t options = requested_options ? *requested_options : default_linker_options();
  if ((options.flags & LINK_OPT_MAX_MEMORY) &&
      options.initial_memory_pages > options.maximum_memory_pages) {
    die("initial memory pages exceed maximum memory pages");
  }
  check_duplicate_definitions(objs, obj_count);
  validate_continuation_metadata(objs, obj_count);
  object_t runtime;
  memset(&runtime, 0, sizeof(runtime));
  if (use_stdlib) {
    synthesize_runtime_object(
        objs, obj_count, &runtime,
        (options.flags & LINK_OPT_STDIO_WRITE_IMPORT) == 0);
  }
  func_t *main_wrapper = NULL;
  maybe_add_main_wrapper(objs, obj_count,
                         export_list_contains(exports, export_count, "main") ? "main" : NULL,
                         &runtime, &main_wrapper);
  if (runtime.func_count > 0 || runtime.data_count > 0) {
    object_t *with_runtime = xmalloc((size_t)(obj_count + 1) * sizeof(*with_runtime));
    memcpy(with_runtime, objs, (size_t)obj_count * sizeof(*with_runtime));
    with_runtime[obj_count++] = runtime;
    objs = with_runtime;
  }
  for (int ei = 0; ei < export_count; ei++)
    validate_export_c_signature(&exports[ei], objs, obj_count);

  type_t *types = NULL;
  int type_count = 0, type_cap = 0;
  final_func_t *defs = NULL;
  int def_count = 0, def_cap = 0;
  final_import_t *imports = NULL;
  int import_count = 0, import_cap = 0;
  final_data_t *datas = NULL;
  int data_count = 0, data_cap = 0;
  final_table_func_t *table_funcs = NULL;
  int table_count = 0, table_cap = 0;
  final_global_t *globals = NULL;
  int global_count = 0, global_cap = 0;

  for (int oi = 0; oi < obj_count; oi++) {
    build_object_type_map(&objs[oi], &types, &type_count, &type_cap);
    for (int fi = 0; fi < objs[oi].func_count; fi++) {
      func_t *f = &objs[oi].funcs[fi];
      if (f->type_index < 0 || f->type_index >= objs[oi].type_count) die("bad function type index");
      f->final_type = objs[oi].type_map[f->type_index];
      if (f->defined) {
        final_func_t d = {&objs[oi], fi};
        PUSH(defs, def_count, def_cap, d);
      }
    }
    for (int di = 0; di < objs[oi].data_count; di++) {
      if (objs[oi].data[di].defined) {
        final_data_t d = {&objs[oi], di};
        PUSH(datas, data_count, data_cap, d);
      }
    }
  }

  add_unresolved_function_imports(objs, obj_count, &imports, &import_count, &import_cap,
                                  &types, &type_count, &type_cap);

  for (int i = 0; i < def_count; i++) defs[i].obj->funcs[defs[i].func_index].final_index = import_count + i;
  if (main_wrapper) {
    type_t *main_type = NULL;
    int main_final_index = -1;
    for (int i = 0; i < def_count; i++) {
      func_t *f = &defs[i].obj->funcs[defs[i].func_index];
      if (f == main_wrapper) continue;
      if (str_eq_lit(f->name, "main")) {
        main_type = &defs[i].obj->types[f->type_index];
        main_final_index = f->final_index;
      }
    }
    if (!main_type || main_final_index < 0) die("main wrapper target not found");
    fill_main_wrapper_body(main_wrapper, main_type, main_final_index);
  }

  int needs_table = 0;
  for (int oi = 0; oi < obj_count; oi++) {
    if (objs[oi].imports_table) needs_table = 1;
  }

  uint32_t mem = 1024;
  for (int i = 0; i < data_count; i++) {
    data_seg_t *d = &datas[i].obj->data[datas[i].data_index];
    uint32_t align = d->align_log2 > 0 && d->align_log2 < 31 ? (uint32_t)1 << d->align_log2 : 1;
    mem = align_to_u32_checked(mem, align, "memory layout overflow");
    d->final_addr = mem;
    size_t alloc_size = d->alloc_size > d->size ? d->alloc_size : d->size;
    if (alloc_size > UINT32_MAX) die("memory layout overflow");
    mem = checked_add_u32(mem, (uint32_t)alloc_size, "memory layout overflow");
  }

  patch_object_relocations(objs, obj_count, &imports, &import_count,
                           &types, &type_count, &type_cap, &globals, &global_count, &global_cap,
                           &table_funcs, &table_count, &table_cap);
  uint32_t heap_data_end = mem;
  if (has_runtime_layout_value(datas, data_count, "ag_rt_heap") &&
      heap_data_end < 8u * 1024u * 1024u)
    heap_data_end = 8u * 1024u * 1024u;
  uint64_t required_bytes = (uint64_t)heap_data_end + options.stack_size;
  if (required_bytes > UINT32_MAX) die("memory layout including stack exceeds Wasm32 address space");
  uint32_t required_pages = (uint32_t)((required_bytes + 65535u) / 65536u);
  uint32_t memory_pages = required_pages > options.initial_memory_pages
                              ? required_pages : options.initial_memory_pages;
  if (memory_pages > 65535u) die("initial memory pages exceed usable Wasm32 address space");
  if ((options.flags & LINK_OPT_MAX_MEMORY) &&
      memory_pages > options.maximum_memory_pages) {
    die("memory requirement exceeds maximum memory pages");
  }
  uint32_t table_initial = (uint32_t)table_count + 2;
  if ((needs_table || table_count > 0) &&
      (options.flags & LINK_OPT_MAX_TABLE) &&
      table_initial > options.maximum_table_elements) {
    die("table requirement exceeds maximum table elements");
  }
  uint32_t stack_top = memory_pages * 65536u;
  uint32_t heap_base = align_to_u32_checked(
      heap_data_end, 16, "memory layout overflow");
  uint32_t heap_limit = stack_top - options.stack_size;
  if (heap_base > heap_limit) die("linked data overlaps reserved stack");
  patch_runtime_layout_value(datas, data_count, "ag_rt_heap", heap_base);
  patch_runtime_layout_value(datas, data_count, "ag_rt_memory_limit_bytes", heap_limit);
  for (int i = 0; i < global_count; i++) {
    if (is_stack_pointer_name(globals[i].name)) globals[i].init_value = stack_top;
  }

  *out = (buf_t){.max_len = max_output_bytes};
  buf_u32le(out, 0x6d736100);
  buf_u32le(out, 1);

  buf_t sec = {0};
  buf_uleb(&sec, (uint32_t)type_count);
  for (int i = 0; i < type_count; i++) buf_bytes(&sec, types[i].raw, types[i].raw_len);
  emit_section(out, SEC_TYPE, &sec);
  free(sec.data); sec = (buf_t){0};

  if (import_count > 0) {
    buf_uleb(&sec, (uint32_t)import_count);
    for (int i = 0; i < import_count; i++) {
      str_t import_module = str_dup("env", 3);
      str_t import_name = imports[i].name;
      if ((options.flags & LINK_OPT_STDIO_WRITE_IMPORT) &&
          str_eq_lit(import_name, "__agc_host_write")) {
        const char *module = (const char *)(uintptr_t)
            options.stdio_write_import_module_addr;
        const char *name = (const char *)(uintptr_t)
            options.stdio_write_import_name_addr;
        if (!module || !module[0] || !name || !name[0])
          die("invalid stdio host write import option");
        import_module = str_dup(module, (int)strlen(module));
        import_name = str_dup(name, (int)strlen(name));
      }
      buf_str(&sec, import_module);
      buf_str(&sec, import_name);
      buf_u8(&sec, 0);
      buf_uleb(&sec, (uint32_t)imports[i].type_index);
    }
    emit_section(out, SEC_IMPORT, &sec);
    free(sec.data); sec = (buf_t){0};
  }

  if (def_count > 0) {
    buf_uleb(&sec, (uint32_t)def_count);
    for (int i = 0; i < def_count; i++) {
      func_t *f = &defs[i].obj->funcs[defs[i].func_index];
      buf_uleb(&sec, (uint32_t)f->final_type);
    }
    emit_section(out, SEC_FUNCTION, &sec);
    free(sec.data); sec = (buf_t){0};
  }

  if (needs_table || table_count > 0) {
    buf_uleb(&sec, 1);
    buf_u8(&sec, 0x70);
    buf_u8(&sec, (options.flags & LINK_OPT_MAX_TABLE) ? 1 : 0);
    buf_uleb(&sec, table_initial);
    if (options.flags & LINK_OPT_MAX_TABLE)
      buf_uleb(&sec, options.maximum_table_elements);
    emit_section(out, SEC_TABLE, &sec);
    free(sec.data); sec = (buf_t){0};
  }

  buf_uleb(&sec, 1);
  buf_u8(&sec, (options.flags & LINK_OPT_MAX_MEMORY) ? 1 : 0);
  buf_uleb(&sec, memory_pages);
  if (options.flags & LINK_OPT_MAX_MEMORY)
    buf_uleb(&sec, options.maximum_memory_pages);
  emit_section(out, SEC_MEMORY, &sec);
  free(sec.data); sec = (buf_t){0};

  if (global_count > 0) {
    buf_uleb(&sec, (uint32_t)global_count);
    for (int i = 0; i < global_count; i++) {
      buf_u8(&sec, 0x7f);
      buf_u8(&sec, 1);
      buf_u8(&sec, 0x41);
      buf_sleb_i32(&sec, (int32_t)globals[i].init_value);
      buf_u8(&sec, 0x0b);
    }
    emit_section(out, SEC_GLOBAL, &sec);
    free(sec.data); sec = (buf_t){0};
  }

  export_func_t *export_funcs = NULL;
  int export_func_count = 0, export_func_cap = 0;
  for (int ei = 0; ei < export_count; ei++) {
    for (int prev = 0; prev < ei; prev++) {
      if (strcmp(exports[prev].name, exports[ei].name) == 0) {
        dief("duplicate export: %s", exports[ei].name);
      }
    }
    int export_func = -1;
    str_t ex = str_dup(exports[ei].name, (int)strlen(exports[ei].name));
    for (int i = 0; i < def_count; i++) {
      func_t *f = &defs[i].obj->funcs[defs[i].func_index];
      if (str_eq(f->name, ex)) export_func = f->final_index;
    }
    if (export_func < 0)
      die_link_diagnostic_missing_export(exports[ei].name, 0);
    export_func_t ef = {ex, export_func};
    PUSH(export_funcs, export_func_count, export_func_cap, ef);
  }
  buf_uleb(&sec, (uint32_t)(1 + export_func_count));
  buf_str(&sec, str_dup("memory", 6));
  buf_u8(&sec, 2);
  buf_uleb(&sec, 0);
  for (int i = 0; i < export_func_count; i++) {
    buf_str(&sec, export_funcs[i].name);
    buf_u8(&sec, 0);
    buf_uleb(&sec, (uint32_t)export_funcs[i].func_index);
  }
  emit_section(out, SEC_EXPORT, &sec);
  free(sec.data); sec = (buf_t){0};

  if (table_count > 0) {
    buf_uleb(&sec, 1);
    buf_u8(&sec, 0);
    buf_u8(&sec, 0x41);
    buf_sleb_i32(&sec, 2);
    buf_u8(&sec, 0x0b);
    buf_uleb(&sec, (uint32_t)table_count);
    for (int i = 0; i < table_count; i++) {
      buf_uleb(&sec, (uint32_t)table_funcs[i].final_func_index);
    }
    emit_section(out, SEC_ELEM, &sec);
    free(sec.data); sec = (buf_t){0};
  }

  if (def_count > 0) {
    buf_uleb(&sec, (uint32_t)def_count);
    for (int i = 0; i < def_count; i++) {
      func_t *f = &defs[i].obj->funcs[defs[i].func_index];
      buf_uleb(&sec, (uint32_t)f->body_len);
      buf_bytes(&sec, f->body, f->body_len);
    }
    emit_section(out, SEC_CODE, &sec);
    free(sec.data); sec = (buf_t){0};
  }

  if (data_count > 0) {
    buf_uleb(&sec, (uint32_t)data_count);
    for (int i = 0; i < data_count; i++) {
      data_seg_t *d = &datas[i].obj->data[datas[i].data_index];
      buf_u8(&sec, 0);
      buf_u8(&sec, 0x41);
      buf_sleb_i32(&sec, (int32_t)d->final_addr);
      buf_u8(&sec, 0x0b);
      buf_uleb(&sec, (uint32_t)d->size);
      buf_bytes(&sec, d->bytes, d->size);
    }
    emit_section(out, SEC_DATA, &sec);
    free(sec.data); sec = (buf_t){0};
  }

}

static void build_module(const char *out_path, const export_spec_t *exports, int export_count,
                         object_t *objs, int obj_count, int use_stdlib,
                         const linker_options_t *options) {
  buf_t out;
  build_module_into(&out, exports, export_count, objs, obj_count, use_stdlib, options, 0);
  write_output(out_path, &out);
}

typedef struct {
  long ptr;
  long len;
} api_slice_t;

typedef struct {
  long name;
  long signature;
} api_export_t;

static long link_objects_api(long inputs_addr, int input_count,
                             long exports_addr, int export_count,
                             int use_stdlib, long options_addr,
                             long max_output_bytes,
                             int exports_have_signatures,
                             long out_len_addr) {
  if (!inputs_addr || input_count <= 0 || input_count > 4096 || export_count < 0 || export_count > 4096) {
    die("invalid linker API arguments");
  }
  api_slice_t *inputs = (api_slice_t *)(uintptr_t)inputs_addr;
  long *out_len = out_len_addr ? (long *)(uintptr_t)out_len_addr : NULL;
  if (!out_len) die("invalid linker API output length pointer");

  object_t *objs = xmalloc((size_t)input_count * sizeof(*objs));
  for (int i = 0; i < input_count; i++) {
    if (!inputs[i].ptr || inputs[i].len < 8) die("invalid linker API object slice");
    char name[32];
    snprintf(name, sizeof(name), "input%d.o", i);
    objs[i] = parse_object_bytes(name, (const unsigned char *)(uintptr_t)inputs[i].ptr,
                                 (size_t)inputs[i].len);
  }

  export_spec_t *exports = xmalloc((size_t)(export_count + 1) * sizeof(*exports));
  for (int i = 0; i < export_count; i++) {
    if (!exports_addr) die("invalid linker API export name");
    if (exports_have_signatures) {
      api_export_t *api_exports = (api_export_t *)(uintptr_t)exports_addr;
      if (!api_exports[i].name) die("invalid linker API export name");
      exports[i].name = (const char *)(uintptr_t)api_exports[i].name;
      exports[i].signature = api_exports[i].signature
                                   ? (const char *)(uintptr_t)api_exports[i].signature
                                   : NULL;
    } else {
      long *export_ptrs = (long *)(uintptr_t)exports_addr;
      if (!export_ptrs[i]) die("invalid linker API export name");
      exports[i].name = (const char *)(uintptr_t)export_ptrs[i];
      exports[i].signature = NULL;
    }
  }

  buf_t out;
  const linker_options_t *options = options_addr
                                        ? (const linker_options_t *)(uintptr_t)options_addr
                                        : NULL;
  build_module_into(&out, exports, export_count, objs, input_count, use_stdlib, options,
                    max_output_bytes > 0 ? (size_t)max_output_bytes : 0);
  unsigned char *ret = xmalloc(out.len);
  memcpy(ret, out.data, out.len);
  *out_len = (long)out.len;
  return (long)(uintptr_t)ret;
}

long agc_wasm_link_objects(long inputs_addr, int input_count,
                           long exports_addr, int export_count,
                           int use_stdlib, long out_len_addr) {
  return link_objects_api(inputs_addr, input_count, exports_addr, export_count,
                          use_stdlib, 0, 0, 0, out_len_addr);
}

long agc_wasm_link_objects_with_options(long inputs_addr, int input_count,
                                        long exports_addr, int export_count,
                                        int use_stdlib, long options_addr,
                                        long out_len_addr) {
  return link_objects_api(inputs_addr, input_count, exports_addr, export_count,
                          use_stdlib, options_addr, 0, 0, out_len_addr);
}

long agc_wasm_link_objects_with_resource_limits(long inputs_addr, int input_count,
                                                long exports_addr, int export_count,
                                                int use_stdlib, long options_addr,
                                                long max_output_bytes,
                                                long out_len_addr) {
  return link_objects_api(inputs_addr, input_count, exports_addr, export_count,
                          use_stdlib, options_addr, max_output_bytes, 0, out_len_addr);
}

long agc_wasm_link_objects_with_export_signatures(
    long inputs_addr, int input_count, long exports_addr, int export_count,
    int use_stdlib, long options_addr, long max_output_bytes,
    long out_len_addr) {
  return link_objects_api(inputs_addr, input_count, exports_addr, export_count,
                          use_stdlib, options_addr, max_output_bytes, 1,
                          out_len_addr);
}

static void usage(void) {
  fprintf(stderr,
          "usage: ag_wasm_link [--nostdlib] --no-entry [--export=name ...] "
          "[--initial-memory-pages=N] [--maximum-memory-pages=N] "
          "[--stack-size=N] [--maximum-table-elements=N] "
          "[--stdio-write-import-module=NAME] "
          "[--stdio-write-import-name=NAME] "
          "-o out.wasm a.o b.o ...\n");
  exit(2);
}

static uint32_t parse_u32_option(const char *text, const char *name) {
  if (!text || !text[0]) dief("missing value for %s", name);
  uint64_t value = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    if (*p < '0' || *p > '9') dief("invalid numeric value for %s", name);
    value = value * 10u + (uint64_t)(*p - '0');
    if (value > UINT32_MAX) dief("numeric value out of range for %s", name);
  }
  return (uint32_t)value;
}

static int file_exists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

static int input_contains(const char **inputs, int count, const char *path) {
  for (int i = 0; i < count; i++) {
    if (strcmp(inputs[i], path) == 0) return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  const char *out = NULL;
  export_spec_t *exports = xmalloc(((size_t)argc + 1) * sizeof(*exports));
  int export_count = 0, export_cap = argc + 1;
  int use_stdlib = 1;
  linker_options_t options = default_linker_options();
  const char *stdio_write_import_module = "env";
  const char *stdio_write_import_name = "__agc_host_write";
  const char **inputs = xmalloc(((size_t)argc + 1) * sizeof(char *));
  int input_count = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-o") == 0) {
      if (++i >= argc) usage();
      out = argv[i];
    } else if (strncmp(argv[i], "--export=", 9) == 0) {
      const char *name = argv[i] + 9;
      if (!*name) usage();
      export_spec_t export_spec = {name, NULL};
      PUSH(exports, export_count, export_cap, export_spec);
    } else if (strcmp(argv[i], "--export") == 0) {
      if (++i >= argc || !argv[i][0]) usage();
      export_spec_t export_spec = {argv[i], NULL};
      PUSH(exports, export_count, export_cap, export_spec);
    } else if (strcmp(argv[i], "--no-entry") == 0) {
      /* accepted for wasm-ld-shaped command lines */
    } else if (strcmp(argv[i], "--nostdlib") == 0) {
      use_stdlib = 0;
    } else if (strncmp(argv[i], "--initial-memory-pages=", 23) == 0) {
      options.initial_memory_pages =
          parse_u32_option(argv[i] + 23, "--initial-memory-pages");
    } else if (strncmp(argv[i], "--maximum-memory-pages=", 23) == 0) {
      options.maximum_memory_pages =
          parse_u32_option(argv[i] + 23, "--maximum-memory-pages");
      options.flags |= LINK_OPT_MAX_MEMORY;
    } else if (strncmp(argv[i], "--stack-size=", 13) == 0) {
      options.stack_size = parse_u32_option(argv[i] + 13, "--stack-size");
    } else if (strncmp(argv[i], "--maximum-table-elements=", 25) == 0) {
      options.maximum_table_elements =
          parse_u32_option(argv[i] + 25, "--maximum-table-elements");
      options.flags |= LINK_OPT_MAX_TABLE;
    } else if (strncmp(argv[i], "--stdio-write-import-module=", 28) == 0) {
      stdio_write_import_module = argv[i] + 28;
      if (!stdio_write_import_module[0]) usage();
      options.flags |= LINK_OPT_STDIO_WRITE_IMPORT;
    } else if (strncmp(argv[i], "--stdio-write-import-name=", 26) == 0) {
      stdio_write_import_name = argv[i] + 26;
      if (!stdio_write_import_name[0]) usage();
      options.flags |= LINK_OPT_STDIO_WRITE_IMPORT;
    } else if (argv[i][0] == '-') {
      usage();
    } else {
      inputs[input_count++] = argv[i];
    }
  }
  if (options.flags & LINK_OPT_STDIO_WRITE_IMPORT) {
    options.stdio_write_import_module_addr =
        (uint64_t)(uintptr_t)stdio_write_import_module;
    options.stdio_write_import_name_addr =
        (uint64_t)(uintptr_t)stdio_write_import_name;
  }
  if (!out || input_count == 0) usage();
  const char *runtime_path = runtime_object_path();
  if (use_stdlib && !input_contains(inputs, input_count, runtime_path)) {
    if (!file_exists(runtime_path)) {
      dief("default runtime object not found: %s (run make build/libagc_runtime.o or pass --nostdlib)",
           runtime_path);
    }
    inputs[input_count++] = runtime_path;
  }
  object_t *objs = xmalloc((size_t)input_count * sizeof(object_t));
  for (int i = 0; i < input_count; i++) objs[i] = parse_object(inputs[i]);
  build_module(out, exports, export_count, objs, input_count, use_stdlib, &options);
  return 0;
}
