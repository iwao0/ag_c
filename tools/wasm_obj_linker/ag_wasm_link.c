#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  SEC_CUSTOM = 0,
  SEC_TYPE = 1,
  SEC_IMPORT = 2,
  SEC_FUNCTION = 3,
  SEC_MEMORY = 5,
  SEC_GLOBAL = 6,
  SEC_EXPORT = 7,
  SEC_CODE = 10,
  SEC_DATA = 11,

  R_WASM_FUNCTION_INDEX_LEB = 0,
  R_WASM_TABLE_INDEX_SLEB = 1,
  R_WASM_TABLE_INDEX_I32 = 2,
  R_WASM_MEMORY_ADDR_LEB = 3,
  R_WASM_MEMORY_ADDR_I32 = 5,
  R_WASM_GLOBAL_INDEX_LEB = 7,

  SYM_FUNCTION = 0,
  SYM_DATA = 1,
  SYM_GLOBAL = 2,

  SYM_BINDING_LOCAL = 0x2,
  SYM_UNDEFINED = 0x10,

  LINK_SEGMENT_INFO = 5,
  LINK_SYMBOL_TABLE = 8,
};

typedef struct {
  unsigned char *data;
  size_t len;
  size_t cap;
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
} func_t;

typedef struct {
  str_t name;
  unsigned char *bytes;
  size_t size;
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
} symbol_t;

typedef struct {
  int type;
  uint32_t offset;
  uint32_t symbol;
  int32_t addend;
  int is_code;
} reloc_t;

typedef struct {
  str_t path;
  type_t *types;
  int type_count;
  int type_cap;
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
  int code_section_index;
  int data_section_index;
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
  int data_index;
} final_data_t;

typedef struct {
  str_t name;
  int final_index;
  uint32_t init_value;
} final_global_t;

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

static void buf_reserve(buf_t *b, size_t add) {
  if (b->len + add <= b->cap) return;
  size_t cap = b->cap ? b->cap * 2 : 256;
  while (cap < b->len + add) cap *= 2;
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

static void buf_sleb_i32(buf_t *b, int32_t v) {
  int more = 1;
  while (more) {
    unsigned char c = (unsigned char)(v & 0x7f);
    int sign = c & 0x40;
    v >>= 7;
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
    type_t t = {0};
    t.raw_len = sec.pos - start;
    t.raw = xmalloc(t.raw_len);
    memcpy(t.raw, sec.p + start, t.raw_len);
    PUSH(o->types, o->type_count, o->type_cap, t);
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
            (void)rd_uleb(&ss); /* offset */
            (void)rd_uleb(&ss); /* size */
            sym.index = seg;
            if (seg >= 0 && seg < o->data_count && !o->data[seg].name.s) {
              o->data[seg].name = str_dup(sym.name.s, sym.name.len);
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
  (void)target;
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

static object_t parse_object(const char *path) {
  size_t len = 0;
  unsigned char *file = read_file(path, &len);
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
    if (id != SEC_CUSTOM) noncustom_index++;
    if (id == SEC_TYPE) parse_type_section(&o, sec);
    else if (id == SEC_IMPORT) parse_import_section(&o, sec);
    else if (id == SEC_FUNCTION) parse_function_section(&o, sec);
    else if (id == SEC_CODE) parse_code_section(&o, sec, noncustom_index);
    else if (id == SEC_DATA) parse_data_section(&o, sec, noncustom_index);
    else if (id == SEC_CUSTOM) {
      str_t name = rd_str_dup(&sec);
      if (name.len == 7 && memcmp(name.s, "linking", 7) == 0) {
        parse_linking_section(&o, sec);
      } else if (name.len == 10 && memcmp(name.s, "reloc.CODE", 10) == 0) {
        parse_reloc_section(&o, sec, 1);
      } else if (name.len == 10 && memcmp(name.s, "reloc.DATA", 10) == 0) {
        parse_reloc_section(&o, sec, 0);
      }
    }
  }
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
  type_t copy = {0};
  copy.raw_len = t->raw_len;
  copy.raw = xmalloc(copy.raw_len);
  memcpy(copy.raw, t->raw, copy.raw_len);
  PUSH(*types, *count, *cap, copy);
  return *count - 1;
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

static int find_defined_data(object_t *objs, int obj_count, str_t name, object_t **out_obj, int *out_data) {
  for (int oi = 0; oi < obj_count; oi++) {
    for (int si = 0; si < objs[oi].symbol_count; si++) {
      symbol_t *sym = &objs[oi].symbols[si];
      if (sym->kind != SYM_DATA || (sym->flags & (SYM_UNDEFINED | SYM_BINDING_LOCAL))) continue;
      if (str_eq(sym->name, name)) {
        if (out_obj) *out_obj = &objs[oi];
        if (out_data) *out_data = sym->index;
        return 1;
      }
    }
  }
  return 0;
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

static uint32_t align_to_u32(uint32_t v, uint32_t a) {
  if (a <= 1) return v;
  return (v + a - 1) / a * a;
}

static int find_import(final_import_t *imports, int count, str_t name, int type_index) {
  for (int i = 0; i < count; i++) {
    if (imports[i].type_index == type_index && str_eq(imports[i].name, name)) return i;
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
      if (find_defined_func(objs, obj_count, sym->name, &def_obj, &def_func)) continue;
      if (sym->index < 0 || sym->index >= o->func_count) die("bad undefined function symbol index");
      int final_type = intern_type(types, type_count, type_cap, &o->types[o->funcs[sym->index].type_index]);
      if (find_import(*imports, *import_count, sym->name, final_type) >= 0) continue;
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

static int final_func_index_for_symbol(object_t *objs, int obj_count, symbol_t *sym,
                                       final_import_t **imports, int *import_count,
                                       type_t **types, int *type_count, int *type_cap) {
  object_t *def_obj = NULL;
  int def_func = -1;
  if (sym->kind != SYM_FUNCTION) die("function relocation does not point at function symbol");
  if (!(sym->flags & SYM_UNDEFINED)) {
    die("internal error: defined function symbol should already have a final index");
  }
  if (find_defined_func(objs, obj_count, sym->name, &def_obj, &def_func)) {
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

static uint32_t final_data_addr_for_symbol(object_t *objs, int obj_count, object_t *cur,
                                           symbol_t *sym, int32_t addend) {
  object_t *def_obj = cur;
  int data_index = sym->index;
  if (sym->kind != SYM_DATA) die("memory relocation does not point at data symbol");
  if (sym->flags & SYM_UNDEFINED) {
    if (!find_defined_data(objs, obj_count, sym->name, &def_obj, &data_index)) {
      dief("unresolved data symbol: %s", sym->name.s);
    }
  }
  if (data_index < 0 || data_index >= def_obj->data_count || !def_obj->data[data_index].defined) {
    dief("bad data symbol: %s", sym->name.s);
  }
  return (uint32_t)((int64_t)def_obj->data[data_index].final_addr + addend);
}

static void patch_object_relocations(object_t *objs, int obj_count,
                                     final_import_t **imports, int *import_count,
                                     type_t **types, int *type_count, int *type_cap,
                                     final_global_t **globals, int *global_count, int *global_cap) {
  for (int oi = 0; oi < obj_count; oi++) {
    object_t *o = &objs[oi];
    for (int ri = 0; ri < o->reloc_count; ri++) {
      reloc_t *r = &o->relocs[ri];
      symbol_t *sym = reloc_symbol(o, r);
      if (r->type == R_WASM_TABLE_INDEX_SLEB || r->type == R_WASM_TABLE_INDEX_I32) {
        die("table/function-pointer relocations are not supported by ag_wasm_link v1");
      }
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
        if (r->type == R_WASM_FUNCTION_INDEX_LEB) {
          uint32_t idx = 0;
          if (sym->flags & SYM_UNDEFINED) {
            idx = (uint32_t)final_func_index_for_symbol(objs, obj_count, sym, imports, import_count,
                                                        types, type_count, type_cap);
          } else {
            if (sym->index < 0 || sym->index >= o->func_count) die("bad function symbol index");
            idx = (uint32_t)o->funcs[sym->index].final_index;
          }
          patch_uleb5(fn->body + body_off, idx);
        } else if (r->type == R_WASM_MEMORY_ADDR_LEB) {
          uint32_t addr = final_data_addr_for_symbol(objs, obj_count, o, sym, r->addend);
          patch_uleb5(fn->body + body_off, addr);
        } else if (r->type == R_WASM_GLOBAL_INDEX_LEB) {
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
          uint32_t addr = final_data_addr_for_symbol(objs, obj_count, o, sym, r->addend);
          patch_u32le(seg->bytes + data_off, addr);
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

static void build_module(const char *out_path, const char *export_name,
                         object_t *objs, int obj_count) {
  type_t *types = NULL;
  int type_count = 0, type_cap = 0;
  final_func_t *defs = NULL;
  int def_count = 0, def_cap = 0;
  final_import_t *imports = NULL;
  int import_count = 0, import_cap = 0;
  final_data_t *datas = NULL;
  int data_count = 0, data_cap = 0;
  final_global_t *globals = NULL;
  int global_count = 0, global_cap = 0;

  for (int oi = 0; oi < obj_count; oi++) {
    for (int fi = 0; fi < objs[oi].func_count; fi++) {
      func_t *f = &objs[oi].funcs[fi];
      f->final_type = intern_type(&types, &type_count, &type_cap, &objs[oi].types[f->type_index]);
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

  uint32_t mem = 1024;
  for (int i = 0; i < data_count; i++) {
    data_seg_t *d = &datas[i].obj->data[datas[i].data_index];
    uint32_t align = d->align_log2 > 0 && d->align_log2 < 31 ? (uint32_t)1 << d->align_log2 : 1;
    mem = align_to_u32(mem, align);
    d->final_addr = mem;
    mem += (uint32_t)d->size;
  }

  patch_object_relocations(objs, obj_count, &imports, &import_count,
                           &types, &type_count, &type_cap, &globals, &global_count, &global_cap);
  uint32_t min_memory = mem > 65536 ? mem : 65536;
  uint32_t memory_pages = align_to_u32(min_memory, 65536) / 65536;
  uint32_t stack_top = memory_pages * 65536;
  for (int i = 0; i < global_count; i++) {
    if (is_stack_pointer_name(globals[i].name)) globals[i].init_value = stack_top;
  }

  buf_t out = {0};
  buf_u32le(&out, 0x6d736100);
  buf_u32le(&out, 1);

  buf_t sec = {0};
  buf_uleb(&sec, (uint32_t)type_count);
  for (int i = 0; i < type_count; i++) buf_bytes(&sec, types[i].raw, types[i].raw_len);
  emit_section(&out, SEC_TYPE, &sec);
  free(sec.data); sec = (buf_t){0};

  if (import_count > 0) {
    buf_uleb(&sec, (uint32_t)import_count);
    for (int i = 0; i < import_count; i++) {
      buf_str(&sec, str_dup("env", 3));
      buf_str(&sec, imports[i].name);
      buf_u8(&sec, 0);
      buf_uleb(&sec, (uint32_t)imports[i].type_index);
    }
    emit_section(&out, SEC_IMPORT, &sec);
    free(sec.data); sec = (buf_t){0};
  }

  if (def_count > 0) {
    buf_uleb(&sec, (uint32_t)def_count);
    for (int i = 0; i < def_count; i++) {
      func_t *f = &defs[i].obj->funcs[defs[i].func_index];
      buf_uleb(&sec, (uint32_t)f->final_type);
    }
    emit_section(&out, SEC_FUNCTION, &sec);
    free(sec.data); sec = (buf_t){0};
  }

  buf_uleb(&sec, 1);
  buf_u8(&sec, 0);
  buf_uleb(&sec, memory_pages);
  emit_section(&out, SEC_MEMORY, &sec);
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
    emit_section(&out, SEC_GLOBAL, &sec);
    free(sec.data); sec = (buf_t){0};
  }

  int export_func = -1;
  if (export_name) {
    str_t ex = str_dup(export_name, (int)strlen(export_name));
    for (int i = 0; i < def_count; i++) {
      func_t *f = &defs[i].obj->funcs[defs[i].func_index];
      if (str_eq(f->name, ex)) export_func = f->final_index;
    }
    if (export_func < 0) dief("export not found: %s", export_name);
  }
  buf_uleb(&sec, (uint32_t)(1 + (export_func >= 0 ? 1 : 0)));
  buf_str(&sec, str_dup("memory", 6));
  buf_u8(&sec, 2);
  buf_uleb(&sec, 0);
  if (export_func >= 0) {
    str_t ex = str_dup(export_name, (int)strlen(export_name));
    buf_str(&sec, ex);
    buf_u8(&sec, 0);
    buf_uleb(&sec, (uint32_t)export_func);
  }
  emit_section(&out, SEC_EXPORT, &sec);
  free(sec.data); sec = (buf_t){0};

  if (def_count > 0) {
    buf_uleb(&sec, (uint32_t)def_count);
    for (int i = 0; i < def_count; i++) {
      func_t *f = &defs[i].obj->funcs[defs[i].func_index];
      buf_uleb(&sec, (uint32_t)f->body_len);
      buf_bytes(&sec, f->body, f->body_len);
    }
    emit_section(&out, SEC_CODE, &sec);
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
    emit_section(&out, SEC_DATA, &sec);
    free(sec.data); sec = (buf_t){0};
  }

  write_output(out_path, &out);
}

static void usage(void) {
  fprintf(stderr, "usage: ag_wasm_link --no-entry --export=main -o out.wasm a.o b.o ...\n");
  exit(2);
}

int main(int argc, char **argv) {
  const char *out = NULL;
  const char *export_name = NULL;
  const char **inputs = xmalloc((size_t)argc * sizeof(char *));
  int input_count = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-o") == 0) {
      if (++i >= argc) usage();
      out = argv[i];
    } else if (strncmp(argv[i], "--export=", 9) == 0) {
      export_name = argv[i] + 9;
    } else if (strcmp(argv[i], "--no-entry") == 0) {
      /* accepted for wasm-ld-shaped command lines */
    } else if (argv[i][0] == '-') {
      usage();
    } else {
      inputs[input_count++] = argv[i];
    }
  }
  if (!out || input_count == 0) usage();
  object_t *objs = xmalloc((size_t)input_count * sizeof(object_t));
  for (int i = 0; i < input_count; i++) objs[i] = parse_object(inputs[i]);
  build_module(out, export_name, objs, input_count);
  return 0;
}
