#include "semantic_ctx.h"
#include "diag.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>
#include <string.h>

#define PCTX_HASH_BUCKETS 256

typedef struct goto_ref_t goto_ref_t;
struct goto_ref_t {
  goto_ref_t *next_all;
  char *name;
  int len;
  token_t *tok;
};

typedef struct label_def_t label_def_t;
struct label_def_t {
  label_def_t *next_hash;
  char *name;
  int len;
  token_t *tok;
};

typedef struct tag_type_t tag_type_t;
struct tag_type_t {
  tag_type_t *next_hash;
  token_kind_t kind;
  char *name;
  int len;
  int member_count;
  int size;
  int align;       // struct/union のアラインメント (_Alignof 用、agg_align)。0 = 未設定。
  int scope_depth;
};
typedef struct tag_member_t tag_member_t;
struct tag_member_t {
  tag_member_t *next_hash;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  char *member_name;
  int member_len;
  int offset;
  int type_size;
  int deref_size;
  int array_len;
  token_kind_t member_tag_kind;
  char *member_tag_name;
  int member_tag_len;
  int member_is_tag_pointer;
  int bit_width;    // ビットフィールド幅（0: 非ビットフィールド）
  int bit_offset;   // ストレージユニット内ビット位置
  int bit_is_signed;
  tk_float_kind_t fp_kind;  // float/double メンバの種別 (FP store/load 用)
  int is_bool;              // 1: _Bool メンバ (代入を 0/1 に正規化する)
  int is_unsigned;          // 1: unsigned メンバ (load を zero-extend する)
  int outer_stride;         // 多次元配列メンバの最外次元バイトストライド（0: 非多次元）
  /* 3 次元以上の配列メンバの中間段ストライド (1 段 subscript 後の要素サイズ)。
   * 0 = 2 次元以下 (outer_stride / elem_size の 2 段で済む)。 */
  int mid_stride;
  /* 多次元 char 配列メンバ (`char c[2][2][3]`) の各次元サイズ。最外側から arr_ndim
   * 段。グローバル brace init の再帰展開で 1 段ずつ消費する。0 = 非多次元 char。 */
  int arr_dims[8];
  int arr_ndim;
  /* array-of-pointer-to-array メンバ (`int (*p[M])[N]`) の各要素ポインタが指す配列の
   * 全バイト数 (= N * elem)。0 = 通常のポインタ配列。 */
  int ptr_array_pointee_bytes;
  int decl_order;
  int scope_depth;
};

typedef struct enum_const_t enum_const_t;
struct enum_const_t {
  enum_const_t *next_hash;
  char *name;
  int len;
  long long value;
  int scope_depth;
};
typedef struct typedef_name_t typedef_name_t;
struct typedef_name_t {
  typedef_name_t *next_hash;
  char *name;
  int len;
  token_kind_t base_kind;
  int elem_size;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int is_pointer;
  // 多段ポインタ typedef (`typedef int **PP`) の `*` 段数。0 のときは未設定
  // (互換: is_pointer なら 1 段とみなす)。getter psx_ctx_get_typedef_pointer_levels 参照。
  int pointer_levels;
  int sizeof_size;
  int pointee_const_qualified;
  int pointee_volatile_qualified;
  int is_unsigned;
  // typedef した型が配列型 (例: `typedef int row_t[3]`) のときに 1。
  // 不完全配列 `typedef int A[]` でも 1（sizeof_size は 0）。
  // 仮引数 `row_t *a` を `(*a)[N]` 相当として扱うため、判別が必要。
  int is_array;
  // 配列の最も外側 `[N]` の N。多次元 `typedef int M[3][4]` で
  // 仮引数 `M *p` の mid_stride を求めるのに使う (= sizeof_size / first_dim)。
  int array_first_dim;
  // 多次元 typedef 配列の全次元数。例: `typedef int M[2][3][4]` で array_dim_count=3。
  // 0 のときは未知 (互換用; ex3 API で初めてセットされる)。
  int array_dim_count;
  // 多次元 typedef 配列の各次元のサイズ。array_dims[0] が最も外側。
  // 上限 8 次元 (実用上十分)。
  int array_dims[8];
  int scope_depth;
};
typedef struct func_name_t func_name_t;
struct func_name_t {
  func_name_t *next_hash;
  char *name;
  int len;
  int ret_struct_size; // 構造体戻り値サイズ（0: 非構造体）
  token_kind_t ret_tag_kind;
  char *ret_tag_name;
  int ret_tag_len;
  // 戻り値が float/double のときに保持する。`(int)f()` キャストで
  // codegen に ND_FP_TO_INT (fcvtzs) を挿入させるために必要。
  tk_float_kind_t ret_fp_kind;
  /* 1: 戻り値型が _Complex。呼び出し側 funcall ノードの is_complex 伝播に使う
   * (HFA 戻り値 d0/d1 の受け取り)。 */
  int ret_is_complex;
  // variadic 関数 (`...` を持つ) かどうかと、固定引数の個数。
  // Apple ARM64 ABI に従い caller は variadic 引数を stack に積むため、
  // 呼び出し側 codegen で nargs_fixed を境に register / stack を切り替える。
  int is_variadic;
  int nargs_fixed;
  /* 1: 戻り値型が void。代入や初期化での使用を検出するのに使う (C11 6.5.16)。 */
  int is_ret_void;
  /* 戻り値型の基底情報。再宣言で型が異なる場合のエラー検出 (C11 6.7p3) に使う。
   * ret_set_once が 0 のうちは比較せず初回値として記録する。 */
  int ret_set_once;
  token_kind_t ret_token_kind;
  int ret_is_pointer;
  /* 戻り値型のポインタ段数 (`int *g()`=1, `int **g()`=2, 非ポインタ=0)。ret_is_pointer は
   * bool で段数を持たないため、多段ポインタ戻り `int **g(); **g()` の deref を正しい
   * 幅 (1 段目=ポインタ8B / 最内=基底型) で組むために別途保持する。 */
  int ret_pointer_levels;
  /* 1: 戻り値型が unsigned。`unsigned` は ret_token_kind が TK_INT に正規化され
   * 符号性が落ちるため別途保持する。呼び出し側 funcall ノードの is_unsigned 伝播
   * (ULT/ULE 比較選択) に使う。 */
  int ret_is_unsigned;
  /* 戻り値型が「配列へのポインタ」`int (*f())[N]` のときの先頭次元 N (それ以外は 0)。
   * 呼び出し結果 `f()[i]` の行ストライドを N*elem にするのに使う (0 なら通常のポインタ戻り)。 */
  int ret_pointee_array_first_dim;
  /* 仮引数 i の fp_kind (float/double/none) を保持。呼び出し側 IR builder が
   * `f(1)` のような int 実引数を double 仮引数に渡すケースで I2F キャスト
   * を挿入するために使う。 16 個まで track (それ以降は NONE のままで暗黙
   * 変換なし — 既存挙動)。 */
  unsigned char param_fp_kinds[16];
  int param_fp_kinds_count;
  /* 仮引数 i が整数スカラのときの幅 (1/2/4 → 4, 8 → 8、0 = 非整数/未記録)。
   * 呼び出し側 IR builder が `f(7.9)` のような fp 実引数を整数仮引数に渡すケースで
   * F2I (fcvtzs) を挿入するために使う。fp_kind と排他 (fp 仮引数は 0)。 */
  unsigned char param_int_sizes[16];
  int param_int_sizes_count;
  /* 同名関数の宣言と定義でシグネチャ (引数数 / 可変長 / 引数型カテゴリ) が一致するかの照合用。
   * 0 のうちは初回値として記録、以降は比較する (C11 6.7p4)。
   * param_categories[i] は引数 i の型カテゴリ (PSX_PCAT_*; 0=未設定)。 */
  int nargs_set_once;
  unsigned char param_categories[16];
  /* 1: この関数名はすでに本体定義済み。2 度目の定義を E3064 で弾くために使う
   * (C11 6.9p3、`int f(){...} int f(){...}` 等)。プロトタイプ宣言 `int f(int);`
   * のみではこのフラグは立たない。 */
  int is_defined;
};

static goto_ref_t *goto_refs_all = NULL;
static label_def_t *label_defs_by_bucket[PCTX_HASH_BUCKETS];
static tag_type_t *tag_types_by_bucket[PCTX_HASH_BUCKETS];
static tag_member_t *tag_members_by_bucket[PCTX_HASH_BUCKETS];
static enum_const_t *enum_consts_by_bucket[PCTX_HASH_BUCKETS];
static typedef_name_t *typedefs_by_bucket[PCTX_HASH_BUCKETS];
static func_name_t *func_names_by_bucket[PCTX_HASH_BUCKETS];
static int tag_scope_depth = 0;
static int tag_member_decl_order = 0;

static unsigned psx_ctx_hash_name(const char *name, int len) {
  // djb2 variant
  unsigned h = 5381u;
  for (int i = 0; i < len; i++) {
    h = ((h << 5) + h) ^ (unsigned char)name[i];
  }
  return h & (PCTX_HASH_BUCKETS - 1u);
}

static unsigned psx_ctx_hash_tag(token_kind_t kind, const char *name, int len) {
  unsigned h = (unsigned)kind * 131u;
  for (int i = 0; i < len; i++) {
    h = (h * 33u) ^ (unsigned char)name[i];
  }
  return h & (PCTX_HASH_BUCKETS - 1u);
}

/* 翻訳単位 (program) の境界で関数名テーブルを初期化する。
 * テストでは fork() 経由で複数のプログラムを 1 プロセス内で解析するため、
 * 関数戻り値型チェック等が前テストの登録に引きずられないようにする。 */
void psx_ctx_reset_function_names(void) {
  memset(func_names_by_bucket, 0, sizeof(func_names_by_bucket));
}

/* タグの完全型定義状態をソフトリセット (member_count を 0 に戻す)。これにより、同一プロセス
 * 内で複数回 ps_program_from を呼ぶユニットテストで前回パースの "struct S 完全定義済み"
 * 状態が今回パースに漏れず、再定義チェックが誤発火しない。 */
void psx_ctx_reset_tag_diag_state(void) {
  for (unsigned i = 0; i < PCTX_HASH_BUCKETS; i++) {
    for (tag_type_t *t = tag_types_by_bucket[i]; t; t = t->next_hash) {
      t->member_count = 0;
    }
  }
}

/* 各 parse 開始時に呼ぶ、関数名テーブルの「ソフトリセット」: 累積状態 (関数情報) は残し、
 * 同一 parse 内でのみ意味を持つ診断フラグ (is_defined、nargs_set_once、ret_set_once、
 * param_categories) のみクリアする。これにより同一プロセス内で複数回 ps_program_from
 * を呼ぶユニットテストで前回パースの "function defined" 状態が今回パースに漏れない。 */
void psx_ctx_reset_function_diag_state(void) {
  for (unsigned i = 0; i < PCTX_HASH_BUCKETS; i++) {
    for (func_name_t *f = func_names_by_bucket[i]; f; f = f->next_hash) {
      f->is_defined = 0;
      f->nargs_set_once = 0;
      f->ret_set_once = 0;
      for (int k = 0; k < 16; k++) f->param_categories[k] = 0;
    }
  }
}

void psx_ctx_reset_function_scope(void) {
  goto_refs_all = NULL;
  memset(label_defs_by_bucket, 0, sizeof(label_defs_by_bucket));
  tag_scope_depth = 0;
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    tag_type_t **tt = &tag_types_by_bucket[i];
    while (*tt) {
      if ((*tt)->scope_depth > 0) {
        *tt = (*tt)->next_hash;
        continue;
      }
      tt = &(*tt)->next_hash;
    }
    tag_member_t **tm = &tag_members_by_bucket[i];
    while (*tm) {
      if ((*tm)->scope_depth > 0) {
        *tm = (*tm)->next_hash;
        continue;
      }
      tm = &(*tm)->next_hash;
    }
    enum_const_t **ec = &enum_consts_by_bucket[i];
    while (*ec) {
      if ((*ec)->scope_depth > 0) {
        *ec = (*ec)->next_hash;
        continue;
      }
      ec = &(*ec)->next_hash;
    }
    typedef_name_t **td = &typedefs_by_bucket[i];
    while (*td) {
      if ((*td)->scope_depth > 0) {
        *td = (*td)->next_hash;
        continue;
      }
      td = &(*td)->next_hash;
    }
  }
}

void psx_ctx_enter_block_scope(void) {
  tag_scope_depth++;
}

void psx_ctx_leave_block_scope(void) {
  if (tag_scope_depth <= 0) return;
  int old_depth = tag_scope_depth;
  tag_scope_depth--;
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    tag_type_t **pp = &tag_types_by_bucket[i];
    while (*pp) {
      tag_type_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    typedef_name_t **pp = &typedefs_by_bucket[i];
    while (*pp) {
      typedef_name_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    tag_member_t **pp = &tag_members_by_bucket[i];
    while (*pp) {
      tag_member_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    enum_const_t **pp = &enum_consts_by_bucket[i];
    while (*pp) {
      enum_const_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
}

void psx_ctx_register_goto_ref(char *name, int len, token_t *tok) {
  goto_ref_t *g = calloc(1, sizeof(goto_ref_t));
  g->name = name;
  g->len = len;
  g->tok = tok;
  g->next_all = goto_refs_all;
  goto_refs_all = g;
}

void psx_ctx_register_label_def(char *name, int len, token_t *tok) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (label_def_t *d = label_defs_by_bucket[bucket]; d; d = d->next_hash) {
    if (d->len == len && strncmp(d->name, name, (size_t)len) == 0) {
      psx_diag_duplicate_with_name(tok, diag_text_for(DIAG_TEXT_LABEL), name, len);
    }
  }
  label_def_t *d = calloc(1, sizeof(label_def_t));
  d->name = name;
  d->len = len;
  d->tok = tok;
  d->next_hash = label_defs_by_bucket[bucket];
  label_defs_by_bucket[bucket] = d;
}

void psx_ctx_validate_goto_refs(void) {
  for (goto_ref_t *g = goto_refs_all; g; g = g->next_all) {
    unsigned bucket = psx_ctx_hash_name(g->name, g->len);
    int found = 0;
    for (label_def_t *d = label_defs_by_bucket[bucket]; d; d = d->next_hash) {
      if (d->len == g->len && strncmp(d->name, g->name, (size_t)g->len) == 0) {
        found = 1;
        break;
      }
    }
    if (!found) {
      psx_diag_ctx(g->tok, "goto", diag_message_for(DIAG_ERR_PARSER_GOTO_LABEL_UNDEFINED),
                   g->len, g->name);
    }
  }
}

// tag_types_by_bucket から (kind, name, len) に一致するエントリを返す。なければ NULL。
static tag_type_t *find_tag_type(token_kind_t kind, char *name, int len) {
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  for (tag_type_t *t = tag_types_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->kind == kind && t->len == len && strncmp(t->name, name, (size_t)len) == 0) {
      return t;
    }
  }
  return NULL;
}

bool psx_ctx_has_tag_type(token_kind_t kind, char *name, int len) {
  return find_tag_type(kind, name, len) != NULL;
}

void psx_ctx_define_tag_type(token_kind_t kind, char *name, int len) {
  psx_ctx_define_tag_type_with_layout(kind, name, len, 0, 0);
}

void psx_ctx_define_tag_type_with_members(token_kind_t kind, char *name, int len, int member_count) {
  psx_ctx_define_tag_type_with_layout(kind, name, len, member_count, member_count > 0 ? 8 : 0);
}

/* struct/union レイアウト計算 (struct_layout.c) が agg_align をここへ預け、直後の
 * psx_ctx_define_tag_type_with_layout が tag に書き込む。tag がテーブルに登録される
 * のは define 時なので、レイアウト中に直接 tag へ書けない。 */
static int g_pending_tag_align = 0;
void psx_ctx_set_pending_tag_align(int align) { g_pending_tag_align = align; }

void psx_ctx_define_tag_type_with_layout(token_kind_t kind, char *name, int len, int member_count, int tag_size) {
  int pending_align = g_pending_tag_align;
  g_pending_tag_align = 0;
  tag_type_t *existing = find_tag_type(kind, name, len);
  /* 同じスコープでの再宣言 (前方宣言 `struct S;` → 定義 `struct S{...}`) のみ既存を update する。
   * 内側スコープに同名タグを別レイアウトで宣言した場合 (`struct S{int a;}` 外側 → ブロック内
   * `struct S{double x;}`) は新規エントリとして先頭挿入し、leave_block_scope で削除されるよう
   * scope_depth を立てる。find_tag_type は先頭から最初の一致を返すので、内側 shadow が優先される。 */
  if (existing && existing->scope_depth == tag_scope_depth) {
    /* C11 6.7.2.1p1 / 6.7.2.2p2 / 6.7.2.3p3: 同一スコープでの完全型タグの再定義は不可。
     * 既存もメンバを持っている (= 完全型) のに、今回も新しいメンバを持っている (= 完全型) なら
     * 二重定義。一方が前方宣言なら従来どおり update。 */
    if (existing->member_count > 0 && member_count > 0) {
      /* psx_diag は ctx 経由でしか呼べない (semantic_ctx は diag 抽象を持たない) ため
       * フラグだけ立てて呼び出し側で診断する案もあるが、ここでは diag_emit_tokf を直接使う。 */
      /* 実装簡略化: 検出専用の API を別 fn に分けるのでなく、ここで diag_emit を呼ぶ。
       * caller (struct_layout / enum_const) は curtok() 位置で診断する。 */
      diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, NULL,
                     "タグ '%.*s' は同一スコープで再定義されています (C11 6.7.2)",
                     len, name);
      return;
    }
    if (member_count > existing->member_count) existing->member_count = member_count;
    if (tag_size > existing->size) existing->size = tag_size;
    if (pending_align > existing->align) existing->align = pending_align;
    return;
  }
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  tag_type_t *t = calloc(1, sizeof(tag_type_t));
  t->kind = kind;
  t->name = name;
  t->len = len;
  t->member_count = member_count;
  t->size = tag_size;
  t->align = pending_align;
  t->scope_depth = tag_scope_depth;
  t->next_hash = tag_types_by_bucket[bucket];
  tag_types_by_bucket[bucket] = t;
}

int psx_ctx_get_tag_member_count(token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type(kind, name, len);
  return t ? t->member_count : -1;
}

int psx_ctx_get_tag_size(token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type(kind, name, len);
  return t ? t->size : -1;
}

int psx_ctx_get_tag_align(token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type(kind, name, len);
  return (t && t->align > 0) ? t->align : -1;
}

void psx_ctx_add_tag_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                            const tag_member_info_t *desc) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(desc->name, desc->len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == desc->len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, desc->name, (size_t)desc->len) == 0 &&
        m->scope_depth == tag_scope_depth) {
      m->offset = desc->offset;
      m->type_size = desc->type_size;
      m->deref_size = desc->deref_size;
      m->array_len = desc->array_len;
      m->member_tag_kind = desc->tag_kind;
      m->member_tag_name = desc->tag_name;
      m->member_tag_len = desc->tag_len;
      m->member_is_tag_pointer = desc->is_tag_pointer;
      m->bit_width = desc->bit_width;
      m->bit_offset = desc->bit_offset;
      m->bit_is_signed = desc->bit_is_signed;
      return;
    }
  }
  tag_member_t *m = calloc(1, sizeof(tag_member_t));
  m->tag_kind = tag_kind;
  m->tag_name = tag_name;
  m->tag_len = tag_len;
  m->member_name = desc->name;
  m->member_len = desc->len;
  m->offset = desc->offset;
  m->type_size = desc->type_size;
  m->deref_size = desc->deref_size;
  m->array_len = desc->array_len;
  m->member_tag_kind = desc->tag_kind;
  m->member_tag_name = desc->tag_name;
  m->member_tag_len = desc->tag_len;
  m->member_is_tag_pointer = desc->is_tag_pointer;
  m->bit_width = desc->bit_width;
  m->bit_offset = desc->bit_offset;
  m->bit_is_signed = desc->bit_is_signed;
  m->decl_order = tag_member_decl_order++;
  m->scope_depth = tag_scope_depth;
  m->next_hash = tag_members_by_bucket[bucket];
  tag_members_by_bucket[bucket] = m;
}

void psx_ctx_set_tag_member_fp_kind(token_kind_t tag_kind, char *tag_name, int tag_len,
                                     char *member_name, int member_len,
                                     tk_float_kind_t fp_kind) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      m->fp_kind = fp_kind;
      return;
    }
  }
}

void psx_ctx_set_tag_member_is_bool(token_kind_t tag_kind, char *tag_name, int tag_len,
                                     char *member_name, int member_len, int is_bool) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      m->is_bool = is_bool ? 1 : 0;
      return;
    }
  }
}

void psx_ctx_set_tag_member_outer_stride(token_kind_t tag_kind, char *tag_name, int tag_len,
                                          char *member_name, int member_len, int outer_stride) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      m->outer_stride = outer_stride;
      return;
    }
  }
}

void psx_ctx_set_tag_member_mid_stride(token_kind_t tag_kind, char *tag_name, int tag_len,
                                       char *member_name, int member_len, int mid_stride) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      m->mid_stride = mid_stride;
      return;
    }
  }
}

void psx_ctx_set_tag_member_arr_dims(token_kind_t tag_kind, char *tag_name, int tag_len,
                                     char *member_name, int member_len,
                                     const int *dims, int ndim) {
  if (ndim < 0) ndim = 0;
  if (ndim > 8) ndim = 8;
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      for (int i = 0; i < 8; i++) m->arr_dims[i] = 0;
      for (int i = 0; i < ndim; i++) m->arr_dims[i] = dims[i];
      m->arr_ndim = ndim;
      return;
    }
  }
}

void psx_ctx_set_tag_member_ptr_array_pointee_bytes(token_kind_t tag_kind, char *tag_name, int tag_len,
                                                     char *member_name, int member_len, int bytes) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      m->ptr_array_pointee_bytes = bytes;
      return;
    }
  }
}

void psx_ctx_set_tag_member_is_unsigned(token_kind_t tag_kind, char *tag_name, int tag_len,
                                        char *member_name, int member_len, int is_unsigned) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      m->is_unsigned = is_unsigned ? 1 : 0;
      return;
    }
  }
}

static int cmp_tag_member_ptr(const void *a, const void *b) {
  const tag_member_t *ma = *(const tag_member_t * const *)a;
  const tag_member_t *mb = *(const tag_member_t * const *)b;
  if (ma->offset != mb->offset) return (ma->offset < mb->offset) ? -1 : 1;
  if (ma->decl_order != mb->decl_order) return (ma->decl_order < mb->decl_order) ? -1 : 1;
  return 0;
}

/* tag_member_t の全属性を tag_member_info_t へ写す。get/find_tag_member_info が
 * メンバを 1 つ特定したあとに使う (旧実装の複数 getter 呼び分けを 1 箇所に集約)。 */
static void fill_tag_member_info(const tag_member_t *m, tag_member_info_t *out) {
  out->name = m->member_name;
  out->len = m->member_len;
  out->offset = m->offset;
  out->type_size = m->type_size;
  out->deref_size = m->deref_size;
  out->array_len = m->array_len;
  out->tag_kind = m->member_tag_kind;
  out->tag_name = m->member_tag_name;
  out->tag_len = m->member_tag_len;
  out->is_tag_pointer = m->member_is_tag_pointer;
  out->bit_width = m->bit_width;
  out->bit_offset = m->bit_offset;
  out->bit_is_signed = m->bit_is_signed;
  out->fp_kind = m->fp_kind;
  out->is_bool = m->is_bool;
  out->is_unsigned = m->is_unsigned;
  out->outer_stride = m->outer_stride;
  out->mid_stride = m->mid_stride;
  for (int i = 0; i < 8; i++) out->arr_dims[i] = m->arr_dims[i];
  out->arr_ndim = m->arr_ndim;
  out->ptr_array_pointee_bytes = m->ptr_array_pointee_bytes;
}

/* 内部実装: scope_depth が指定 (>=0) ならその深度に固定、負なら find_tag_type の
 * 最も内側 tag の scope_depth を使う。 */
static bool get_tag_member_info_impl(token_kind_t kind, char *name, int len,
                                     int scope_depth, int index, tag_member_info_t *out) {
  if (!out) return false;
  int target_scope = scope_depth;
  if (target_scope < 0) {
    tag_type_t *tt = find_tag_type(kind, name, len);
    if (!tt) return false;
    target_scope = tt->scope_depth;
  }
  int cap = 8;
  int n = 0;
  tag_member_t **members = calloc((size_t)cap, sizeof(tag_member_t *));
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    for (tag_member_t *m = tag_members_by_bucket[i]; m; m = m->next_hash) {
      if (m->tag_kind != kind || m->tag_len != len) continue;
      if (strncmp(m->tag_name, name, (size_t)len) != 0) continue;
      if (m->scope_depth != target_scope) continue;
      if (n >= cap) {
        cap *= 2;
        members = realloc(members, (size_t)cap * sizeof(tag_member_t *));
      }
      members[n++] = m;
    }
  }
  if (n == 0 || index < 0 || index >= n) {
    free(members);
    return false;
  }
  qsort(members, (size_t)n, sizeof(tag_member_t *), cmp_tag_member_ptr);
  fill_tag_member_info(members[index], out);
  free(members);
  return true;
}

static bool find_tag_member_info_impl(token_kind_t kind, char *name, int len,
                                      int scope_depth,
                                      char *member_name, int member_len, tag_member_info_t *out) {
  if (!out) return false;
  int target_scope = scope_depth;
  if (target_scope < 0) {
    tag_type_t *tt = find_tag_type(kind, name, len);
    if (!tt) return false;
    target_scope = tt->scope_depth;
  }
  unsigned bucket = (psx_ctx_hash_tag(kind, name, len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == kind && m->tag_len == len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, name, (size_t)len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0 &&
        m->scope_depth == target_scope) {
      fill_tag_member_info(m, out);
      return true;
    }
  }
  return false;
}

/* tag の index 番目 (offset 昇順) のメンバ全属性を取得する。最も内側 tag の scope_depth に
 * 固定 (shadow 対応)。 */
bool psx_ctx_get_tag_member_info(token_kind_t kind, char *name, int len, int index,
                                  tag_member_info_t *out) {
  return get_tag_member_info_impl(kind, name, len, -1, index, out);
}

/* 名前検索版の統合 API。 */
bool psx_ctx_find_tag_member_info(token_kind_t kind, char *name, int len,
                                   char *member_name, int member_len,
                                   tag_member_info_t *out) {
  return find_tag_member_info_impl(kind, name, len, -1, member_name, member_len, out);
}

/* 特定 scope_depth に固定した版。タグ shadowing の応用形で、変数の宣言時 scope を引数で
 * 指定してその scope のメンバを引くのに使う。 */
bool psx_ctx_get_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                          int scope_depth, int index,
                                          tag_member_info_t *out) {
  return get_tag_member_info_impl(kind, name, len, scope_depth, index, out);
}

bool psx_ctx_find_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                           int scope_depth,
                                           char *member_name, int member_len,
                                           tag_member_info_t *out) {
  return find_tag_member_info_impl(kind, name, len, scope_depth, member_name, member_len, out);
}

int psx_ctx_get_tag_scope_depth(token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type(kind, name, len);
  return t ? t->scope_depth : -1;
}

int psx_ctx_get_tag_member_count_at_scope(token_kind_t kind, char *name, int len, int scope_depth) {
  /* 該当スコープの tag を線形検索 (find_tag_type は最も内側を返すので使えない)。 */
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  for (tag_type_t *t = tag_types_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->kind == kind && t->len == len &&
        t->scope_depth == scope_depth &&
        strncmp(t->name, name, (size_t)len) == 0) {
      return t->member_count;
    }
  }
  return -1;
}

// 任意のスコープから名前一致の enum_const を返す。なければ NULL。
static enum_const_t *find_enum_const(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (enum_const_t *e = enum_consts_by_bucket[bucket]; e; e = e->next_hash) {
    if (e->len == len && strncmp(e->name, name, (size_t)len) == 0) {
      return e;
    }
  }
  return NULL;
}

// 現スコープ深度に限った検索（同名再定義の検出用）。
static enum_const_t *find_enum_const_in_current_scope(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (enum_const_t *e = enum_consts_by_bucket[bucket]; e; e = e->next_hash) {
    if (e->scope_depth == tag_scope_depth && e->len == len &&
        strncmp(e->name, name, (size_t)len) == 0) {
      return e;
    }
  }
  return NULL;
}

/* enum 定数を登録する。
 * 戻り値: 1 = 新規登録に成功、0 = 同名定数が既に同スコープにあった (重複)。
 * 重複時はテーブルを変更しない (呼び出し元で診断を出す)。 */
int psx_ctx_define_enum_const(char *name, int len, long long value) {
  enum_const_t *existing = find_enum_const_in_current_scope(name, len);
  if (existing) {
    return 0;
  }
  unsigned bucket = psx_ctx_hash_name(name, len);
  enum_const_t *e = calloc(1, sizeof(enum_const_t));
  e->name = name;
  e->len = len;
  e->value = value;
  e->scope_depth = tag_scope_depth;
  e->next_hash = enum_consts_by_bucket[bucket];
  enum_consts_by_bucket[bucket] = e;
  return 1;
}

bool psx_ctx_find_enum_const(char *name, int len, long long *out_value) {
  enum_const_t *e = find_enum_const(name, len);
  if (!e) return false;
  if (out_value) *out_value = e->value;
  return true;
}

// 任意のスコープから名前一致の typedef を返す。なければ NULL。
static typedef_name_t *find_typedef(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (typedef_name_t *t = typedefs_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->len == len && strncmp(t->name, name, (size_t)len) == 0) {
      return t;
    }
  }
  return NULL;
}

// 現スコープ深度に限った検索（同名再定義の検出用）。
static typedef_name_t *find_typedef_in_current_scope(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (typedef_name_t *t = typedefs_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->scope_depth == tag_scope_depth && t->len == len &&
        strncmp(t->name, name, (size_t)len) == 0) {
      return t;
    }
  }
  return NULL;
}

static void assign_typedef_fields(typedef_name_t *t, const psx_typedef_info_t *info) {
  t->base_kind = info->base_kind;
  t->elem_size = info->elem_size;
  t->fp_kind = info->fp_kind;
  t->tag_kind = info->tag_kind;
  t->tag_name = info->tag_name;
  t->tag_len = info->tag_len;
  t->is_pointer = info->is_pointer;
  t->sizeof_size = info->sizeof_size;
  t->pointee_const_qualified = info->pointee_const_qualified;
  t->pointee_volatile_qualified = info->pointee_volatile_qualified;
  t->is_unsigned = info->is_unsigned;
  t->is_array = info->is_array;
  t->array_first_dim = info->array_first_dim;
}

int psx_ctx_define_typedef_name(char *name, int len, const psx_typedef_info_t *info) {
  typedef_name_t *existing = find_typedef_in_current_scope(name, len);
  /* C11 6.7p3: typedef は同じ型なら再宣言可。違う型なら error。
   * 比較するフィールドは「型の identity」を成すもの。tag_name は同じ ptr で
   * あるはずなので ptr 比較で十分 (parser が tag を共有させている)。 */
  if (existing) {
    int n_new = (info->array_dim_count < 0) ? 0 : info->array_dim_count;
    if (n_new > 8) n_new = 8;
    int same = (existing->base_kind == info->base_kind &&
                existing->elem_size == info->elem_size &&
                existing->fp_kind == info->fp_kind &&
                existing->tag_kind == info->tag_kind &&
                existing->tag_name == info->tag_name &&
                existing->tag_len == info->tag_len &&
                existing->is_pointer == info->is_pointer &&
                existing->sizeof_size == info->sizeof_size &&
                existing->pointee_const_qualified == info->pointee_const_qualified &&
                existing->pointee_volatile_qualified == info->pointee_volatile_qualified &&
                existing->is_unsigned == info->is_unsigned &&
                existing->is_array == info->is_array &&
                existing->array_first_dim == info->array_first_dim &&
                existing->array_dim_count == n_new);
    if (same) {
      for (int i = 0; i < n_new; i++) {
        if (existing->array_dims[i] != info->array_dims[i]) { same = 0; break; }
      }
    }
    if (!same) return 0;
    return 1;  /* 同じ型なら登録済みのままで OK */
  }
  unsigned bucket = psx_ctx_hash_name(name, len);
  typedef_name_t *t = calloc(1, sizeof(typedef_name_t));
  t->name = name;
  t->len = len;
  t->scope_depth = tag_scope_depth;
  t->next_hash = typedefs_by_bucket[bucket];
  typedefs_by_bucket[bucket] = t;
  assign_typedef_fields(t, info);
  int n = (info->array_dim_count < 0) ? 0 : info->array_dim_count;
  if (n > 8) n = 8;
  t->array_dim_count = n;
  for (int i = 0; i < n; i++) t->array_dims[i] = info->array_dims[i];
  for (int i = n; i < 8; i++) t->array_dims[i] = 0;
  return 1;
}

bool psx_ctx_find_typedef_sizeof(char *name, int len, int *out_sizeof_size) {
  typedef_name_t *t = find_typedef(name, len);
  if (!t) return false;
  if (out_sizeof_size) *out_sizeof_size = t->sizeof_size;
  return true;
}

void psx_ctx_set_typedef_pointer_levels(char *name, int len, int levels) {
  typedef_name_t *t = find_typedef(name, len);
  if (t) t->pointer_levels = levels;
}

int psx_ctx_get_typedef_pointer_levels(char *name, int len) {
  typedef_name_t *t = find_typedef(name, len);
  if (!t) return 0;
  if (t->pointer_levels > 0) return t->pointer_levels;
  return t->is_pointer ? 1 : 0;  /* 未設定の単段ポインタ互換 */
}

bool psx_ctx_find_typedef_name(char *name, int len, psx_typedef_info_t *out) {
  typedef_name_t *t = find_typedef(name, len);
  if (!t) return false;
  if (out) {
    out->base_kind = t->base_kind;
    out->elem_size = t->elem_size;
    out->fp_kind = t->fp_kind;
    out->tag_kind = t->tag_kind;
    out->tag_name = t->tag_name;
    out->tag_len = t->tag_len;
    out->is_pointer = t->is_pointer;
    out->sizeof_size = t->sizeof_size;
    out->pointee_const_qualified = t->pointee_const_qualified;
    out->pointee_volatile_qualified = t->pointee_volatile_qualified;
    out->is_unsigned = t->is_unsigned;
    out->is_array = t->is_array;
    out->array_first_dim = t->array_first_dim;
    int n = t->array_dim_count;
    if (n > 8) n = 8;
    out->array_dim_count = n;
    for (int i = 0; i < n; i++) out->array_dims[i] = t->array_dims[i];
    for (int i = n; i < 8; i++) out->array_dims[i] = 0;
  }
  return true;
}

bool psx_ctx_is_typedef_name_token(token_t *tok) {
  if (!tok || tok->kind != TK_IDENT) return false;
  token_ident_t *id = (token_ident_t *)tok;
  return psx_ctx_find_typedef_name(id->str, id->len, NULL);
}

void psx_ctx_define_function_name(char *name, int len) {
  psx_ctx_define_function_name_with_ret(name, len, 0);
}

// 任意のスコープから名前一致の関数名エントリを返す。なければ NULL。
static func_name_t *find_function_name(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (func_name_t *f = func_names_by_bucket[bucket]; f; f = f->next_hash) {
    if (f->len == len && strncmp(f->name, name, (size_t)len) == 0) {
      return f;
    }
  }
  return NULL;
}

void psx_ctx_define_function_name_with_ret(char *name, int len, int ret_struct_size) {
  func_name_t *existing = find_function_name(name, len);
  if (existing) {
    existing->ret_struct_size = ret_struct_size; // 更新
    return;
  }
  unsigned bucket = psx_ctx_hash_name(name, len);
  func_name_t *f = calloc(1, sizeof(func_name_t));
  f->name = name;
  f->len = len;
  f->ret_struct_size = ret_struct_size;
  f->ret_tag_kind = TK_EOF;
  f->ret_tag_name = NULL;
  f->ret_tag_len = 0;
  f->next_hash = func_names_by_bucket[bucket];
  func_names_by_bucket[bucket] = f;
}

void psx_ctx_set_function_ret_tag(char *name, int len, token_kind_t tag_kind, char *tag_name, int tag_len) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  f->ret_tag_kind = tag_kind;
  f->ret_tag_name = tag_name;
  f->ret_tag_len = tag_len;
}

bool psx_ctx_has_function_name(char *name, int len) {
  return find_function_name(name, len) != NULL;
}

int psx_ctx_get_function_ret_struct_size(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return f ? f->ret_struct_size : 0;
}

void psx_ctx_set_function_ret_fp_kind(char *name, int len, tk_float_kind_t fp_kind) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  f->ret_fp_kind = fp_kind;
}

tk_float_kind_t psx_ctx_get_function_ret_fp_kind(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return f ? f->ret_fp_kind : TK_FLOAT_KIND_NONE;
}

void psx_ctx_set_function_ret_is_complex(char *name, int len, int is_complex) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  f->ret_is_complex = is_complex ? 1 : 0;
}

int psx_ctx_get_function_ret_is_complex(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return f ? f->ret_is_complex : 0;
}

void psx_ctx_set_function_param_fp_kind(char *name, int len, int param_idx,
                                         tk_float_kind_t fp_kind) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  if (param_idx < 0 || param_idx >= 16) return;
  f->param_fp_kinds[param_idx] = (unsigned char)fp_kind;
  if (param_idx + 1 > f->param_fp_kinds_count) {
    f->param_fp_kinds_count = param_idx + 1;
  }
}

tk_float_kind_t psx_ctx_get_function_param_fp_kind(char *name, int len, int param_idx) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return TK_FLOAT_KIND_NONE;
  if (param_idx < 0 || param_idx >= f->param_fp_kinds_count) return TK_FLOAT_KIND_NONE;
  return (tk_float_kind_t)f->param_fp_kinds[param_idx];
}

void psx_ctx_set_function_param_int_size(char *name, int len, int param_idx,
                                         int size) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  if (param_idx < 0 || param_idx >= 16) return;
  f->param_int_sizes[param_idx] = (unsigned char)size;
  if (param_idx + 1 > f->param_int_sizes_count) {
    f->param_int_sizes_count = param_idx + 1;
  }
}

int psx_ctx_get_function_param_int_size(char *name, int len, int param_idx) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return 0;
  if (param_idx < 0 || param_idx >= f->param_int_sizes_count) return 0;
  return (int)f->param_int_sizes[param_idx];
}

void psx_ctx_set_function_variadic(char *name, int len, int is_variadic, int nargs_fixed) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  f->is_variadic = is_variadic;
  f->nargs_fixed = nargs_fixed;
}

/* C11 6.7p4: 同名関数の再宣言で「引数の個数」「可変長性」が異なるかチェックする。
 * 初回呼び出しは記録、以降は比較。一致 (または初回) なら 1、不一致なら 0。 */
int psx_ctx_track_function_nargs(char *name, int len, int nargs, int is_variadic) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return 1;
  if (!f->nargs_set_once) {
    f->nargs_set_once = 1;
    f->nargs_fixed = nargs;
    f->is_variadic = is_variadic ? 1 : 0;
    return 1;
  }
  if (f->nargs_fixed == nargs && f->is_variadic == (is_variadic ? 1 : 0)) {
    return 1;
  }
  return 0;
}

/* 同名関数の本体定義が初回かどうかをチェック・記録する (C11 6.9p3)。
 * 初回 (まだ立っていない) なら 1 を返してフラグを立てる、すでに定義済みなら 0 を返す。 */
int psx_ctx_track_function_defined(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return 1;
  if (f->is_defined) return 0;
  f->is_defined = 1;
  return 1;
}

/* C11 6.7p4: 同名関数の再宣言で引数 i のカテゴリ (粗粒度) が異なるかチェックする。
 * 初回 (UNSET) のときは記録、以降は比較。 */
int psx_ctx_track_function_param_category(char *name, int len, int idx, int category) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return 1;
  if (idx < 0 || idx >= 16) return 1;
  if (category == PSX_PCAT_UNSET) return 1;  /* 引数型を判定できないケース (typedef 経由等) は素通し */
  if (f->param_categories[idx] == PSX_PCAT_UNSET) {
    f->param_categories[idx] = (unsigned char)category;
    return 1;
  }
  if (f->param_categories[idx] == (unsigned char)category) return 1;
  return 0;
}

void psx_ctx_set_function_ret_void(char *name, int len, int is_void) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  f->is_ret_void = is_void ? 1 : 0;
}

bool psx_ctx_is_function_ret_void(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return f && f->is_ret_void != 0;
}

/* 関数の戻り値型 (基底 token_kind と pointer フラグ) を登録/比較する。
 * 既に同名で登録があれば、新しい値と異なるか確認する。
 * 戻り値: 1 = OK (新規 or 互換)、0 = 衝突 (呼び出し元で診断発行)。 */
int psx_ctx_get_function_ret_is_pointer(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return (f && f->ret_set_once) ? f->ret_is_pointer : 0;
}

/* 関数の戻り値型トークン (TK_INT / TK_LONG 等) を返す。未登録なら TK_EOF。
 * IR builder が戻り値の幅 (long → 8 バイト) を決めるのに使う。 */
token_kind_t psx_ctx_get_function_ret_token_kind(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return (f && f->ret_set_once) ? f->ret_token_kind : TK_EOF;
}

/* 戻り値型の unsigned 性を記録/取得する。ret_token_kind とは別管理 (unsigned は
 * TK_INT に潰れるため)。 */
void psx_ctx_set_function_ret_unsigned(char *name, int len, int is_unsigned) {
  func_name_t *f = find_function_name(name, len);
  if (f) f->ret_is_unsigned = is_unsigned ? 1 : 0;
}
int psx_ctx_get_function_ret_is_unsigned(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return f ? f->ret_is_unsigned : 0;
}

void psx_ctx_set_function_ret_pointee_array_first_dim(char *name, int len, int first_dim) {
  func_name_t *f = find_function_name(name, len);
  if (f) f->ret_pointee_array_first_dim = first_dim;
}
int psx_ctx_get_function_ret_pointee_array_first_dim(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return f ? f->ret_pointee_array_first_dim : 0;
}

/* 戻り値型のポインタ段数 (`int **g()`=2)。未設定/非ポインタは 0。多段ポインタ戻りの
 * deref 幅決定 (node_utils の funcall 経路) に使う。 */
void psx_ctx_set_function_ret_pointer_levels(char *name, int len, int levels) {
  func_name_t *f = find_function_name(name, len);
  if (f) f->ret_pointer_levels = levels;
}
int psx_ctx_get_function_ret_pointer_levels(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return f ? f->ret_pointer_levels : 0;
}

int psx_ctx_track_function_ret_type(char *name, int len,
                                     token_kind_t ret_token_kind, int ret_is_pointer) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return 1;
  if (!f->ret_set_once) {
    f->ret_set_once = 1;
    f->ret_token_kind = ret_token_kind;
    f->ret_is_pointer = ret_is_pointer ? 1 : 0;
    return 1;
  }
  if (f->ret_token_kind == ret_token_kind &&
      f->ret_is_pointer == (ret_is_pointer ? 1 : 0)) {
    return 1;
  }
  return 0;
}

bool psx_ctx_get_function_is_variadic(char *name, int len, int *out_nargs_fixed) {
  func_name_t *f = find_function_name(name, len);
  if (!f) {
    if (out_nargs_fixed) *out_nargs_fixed = 0;
    return false;
  }
  if (out_nargs_fixed) *out_nargs_fixed = f->nargs_fixed;
  return f->is_variadic != 0;
}

void psx_ctx_get_function_ret_tag(char *name, int len, token_kind_t *out_tag_kind,
                                  char **out_tag_name, int *out_tag_len) {
  if (out_tag_kind) *out_tag_kind = TK_EOF;
  if (out_tag_name) *out_tag_name = NULL;
  if (out_tag_len) *out_tag_len = 0;
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  if (out_tag_kind) *out_tag_kind = f->ret_tag_kind;
  if (out_tag_name) *out_tag_name = f->ret_tag_name;
  if (out_tag_len) *out_tag_len = f->ret_tag_len;
}

bool psx_ctx_is_type_token(token_kind_t kind) {
  return kind == TK_INT || kind == TK_CHAR || kind == TK_VOID || kind == TK_SHORT ||
         kind == TK_LONG || kind == TK_FLOAT || kind == TK_DOUBLE ||
         kind == TK_BOOL || kind == TK_SIGNED || kind == TK_UNSIGNED ||
         kind == TK_COMPLEX || kind == TK_IMAGINARY;
}

bool psx_ctx_is_tag_keyword(token_kind_t kind) {
  return kind == TK_STRUCT || kind == TK_UNION || kind == TK_ENUM;
}

int psx_ctx_scalar_type_size(token_kind_t kind) {
  switch (kind) {
    case TK_CHAR: return 1;
    case TK_BOOL: return 1;
    case TK_SHORT: return 2;
    case TK_INT:
    case TK_SIGNED:
    case TK_UNSIGNED:
    case TK_FLOAT:
      return 4;
    case TK_LONG:
    case TK_DOUBLE:
      return 8;
    default:
      return 8;
  }
}

void psx_ctx_get_type_info(token_kind_t kind, bool *is_type_token, int *scalar_size) {
  bool is_type = false;
  int size = 8;
  switch (kind) {
    case TK_CHAR:
    case TK_BOOL:
      is_type = true;
      size = 1;
      break;
    case TK_SHORT:
      is_type = true;
      size = 2;
      break;
    case TK_INT:
    case TK_SIGNED:
    case TK_UNSIGNED:
    case TK_FLOAT:
      is_type = true;
      size = 4;
      break;
    case TK_LONG:
    case TK_DOUBLE:
      is_type = true;
      size = 8;
      break;
    case TK_VOID:
      is_type = true;
      size = 8;
      break;
    default:
      break;
  }
  if (is_type_token) *is_type_token = is_type;
  if (scalar_size) *scalar_size = size;
}
