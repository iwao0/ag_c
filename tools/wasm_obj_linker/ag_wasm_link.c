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

  RUNTIME_SCRATCH_BASE = 32768,
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
  int code_section_index;
  int data_section_index;
  int imports_table;
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

static int str_empty(str_t s) {
  return s.len == 0 || !s.s;
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
  return type_equal(&ref_obj->types[ref_type], &def_obj->types[def_type]);
}

static void check_duplicate_definitions(object_t *objs, int obj_count) {
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
          if (str_eq(a->name, b->name)) dief("duplicate symbol definition: %s", a->name.s);
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

static int is_runtime_data_symbol(str_t name) {
  return str_eq_lit(name, "__stdinp") || str_eq_lit(name, "__stdoutp") ||
         str_eq_lit(name, "__stderrp");
}

static int is_runtime_func_symbol(str_t name) {
  return str_eq_lit(name, "printf") || str_eq_lit(name, "fprintf") ||
         str_eq_lit(name, "__assert_rtn") ||
         str_eq_lit(name, "strlen") || str_eq_lit(name, "strcmp") ||
         str_eq_lit(name, "memset") || str_eq_lit(name, "memcpy") ||
         str_eq_lit(name, "abs") || str_eq_lit(name, "isdigit") ||
         str_eq_lit(name, "isalpha") || str_eq_lit(name, "toupper") ||
         str_eq_lit(name, "malloc") || str_eq_lit(name, "free") ||
         str_eq_lit(name, "calloc") || str_eq_lit(name, "atoi") ||
         str_eq_lit(name, "strcpy") || str_eq_lit(name, "strncpy") ||
         str_eq_lit(name, "strcat") || str_eq_lit(name, "strncmp") ||
         str_eq_lit(name, "strchr") || str_eq_lit(name, "strrchr") ||
         str_eq_lit(name, "memcmp") || str_eq_lit(name, "putchar") ||
         str_eq_lit(name, "sin");
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

static unsigned char wasm_type_result_valtype(type_t *t) {
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
  int is_digit = str_eq_lit(name, "isdigit");
  int is_alpha = str_eq_lit(name, "isalpha");
  int is_upper = str_eq_lit(name, "toupper");
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

static unsigned char *make_runtime_stub_body(str_t name, type_t *type, size_t *out_len) {
  buf_t b = {0};
  if (str_eq_lit(name, "__assert_rtn")) {
    buf_uleb(&b, 0); /* local decl count */
    buf_u8(&b, 0x00); /* unreachable */
  } else if (make_printf_stub_body(name, type, &b)) {
  } else if (make_strlen_stub_body(name, type, &b)) {
  } else if (make_strcmp_stub_body(name, type, &b)) {
  } else if (make_memset_stub_body(name, type, &b)) {
  } else if (make_memcpy_stub_body(name, type, &b)) {
  } else if (make_abs_stub_body(name, type, &b)) {
  } else if (make_ctype_stub_body(name, type, &b)) {
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

static void add_runtime_func_symbol(object_t *runtime, str_t name, type_t *type) {
  if (runtime_has_func(runtime, name)) return;
  type_t t = {0};
  t.raw_len = type->raw_len;
  t.raw = xmalloc(t.raw_len);
  memcpy(t.raw, type->raw, t.raw_len);
  int type_index = runtime->type_count;
  PUSH(runtime->types, runtime->type_count, runtime->type_cap, t);

  func_t f = {0};
  f.name = str_dup(name.s, name.len);
  f.type_index = type_index;
  f.defined = 1;
  f.body = make_runtime_stub_body(name, &runtime->types[type_index], &f.body_len);
  int func_index = runtime->func_count;
  PUSH(runtime->funcs, runtime->func_count, runtime->func_cap, f);

  symbol_t sym = {0};
  sym.kind = SYM_FUNCTION;
  sym.name = str_dup(name.s, name.len);
  sym.index = func_index;
  PUSH(runtime->symbols, runtime->symbol_count, runtime->symbol_cap, sym);
}

static void synthesize_runtime_object(object_t *objs, int obj_count, object_t *runtime) {
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
                 is_runtime_func_symbol(sym->name) &&
                 !find_defined_func(objs, obj_count, sym->name, NULL, NULL)) {
        if (sym->index < 0 || sym->index >= o->func_count) die("bad undefined function symbol index");
        int type_index = o->funcs[sym->index].type_index;
        if (type_index < 0 || type_index >= o->type_count) die("bad function type index");
        add_runtime_func_symbol(runtime, sym->name, &o->types[type_index]);
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
      if (find_import(*imports, *import_count, sym->name, final_type) >= 0) continue;
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
  tf.table_index = *table_count + 1;
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

static void build_module(const char *out_path, const char *export_name,
                         object_t *objs, int obj_count) {
  check_duplicate_definitions(objs, obj_count);
  object_t runtime;
  memset(&runtime, 0, sizeof(runtime));
  synthesize_runtime_object(objs, obj_count, &runtime);
  if (runtime.func_count > 0 || runtime.data_count > 0) {
    object_t *with_runtime = xmalloc((size_t)(obj_count + 1) * sizeof(*with_runtime));
    memcpy(with_runtime, objs, (size_t)obj_count * sizeof(*with_runtime));
    with_runtime[obj_count++] = runtime;
    objs = with_runtime;
  }

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
  uint32_t min_memory = mem > 65536 ? mem : 65536;
  uint32_t memory_pages = align_to_u32_checked(min_memory, 65536, "memory layout overflow") / 65536;
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

  if (needs_table || table_count > 0) {
    buf_uleb(&sec, 1);
    buf_u8(&sec, 0x70);
    buf_u8(&sec, 0);
    buf_uleb(&sec, (uint32_t)table_count + 1);
    emit_section(&out, SEC_TABLE, &sec);
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

  if (table_count > 0) {
    buf_uleb(&sec, 1);
    buf_u8(&sec, 0);
    buf_u8(&sec, 0x41);
    buf_sleb_i32(&sec, 1);
    buf_u8(&sec, 0x0b);
    buf_uleb(&sec, (uint32_t)table_count);
    for (int i = 0; i < table_count; i++) {
      buf_uleb(&sec, (uint32_t)table_funcs[i].final_func_index);
    }
    emit_section(&out, SEC_ELEM, &sec);
    free(sec.data); sec = (buf_t){0};
  }

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
