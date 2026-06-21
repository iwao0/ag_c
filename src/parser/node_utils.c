#include "node_utils.h"
#include "semantic_ctx.h"
#include "arena.h"
#include "diag.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"

static node_mem_t *as_mem(node_t *node) { return (node_mem_t *)node; }
static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }
static inline token_t *curtok(void) { return tk_get_current_token(); }

/* 関数のポインタ戻り値 (`int *g(); g()[i]` / `g()+i`) の pointee サイズ (= deref_size /
 * subscript・ポインタ算術のスケール)。直接呼び出しのみ。非ポインタ戻り・間接呼び出し・
 * 不明は 0。parser はポインタ戻り値の pointee 型を覚えていないので semantic ctx の
 * 戻り値型 (tag / token_kind) から導出する。多段ポインタ戻り (`int **g()`) は ret が段数を
 * 持たないため基底型サイズになる (既存の制約)。 */
static int funcall_ret_pointee_size(node_t *node) {
  node_func_t *fn = (node_func_t *)node;
  if (fn->callee != NULL || !fn->funcname) return 0;
  if (!psx_ctx_get_function_ret_is_pointer(fn->funcname, fn->funcname_len)) return 0;
  token_kind_t tag = TK_EOF; char *tn = NULL; int tl = 0;
  psx_ctx_get_function_ret_tag(fn->funcname, fn->funcname_len, &tag, &tn, &tl);
  if (tag != TK_EOF) {
    int ss = psx_ctx_get_function_ret_struct_size(fn->funcname, fn->funcname_len);
    return ss > 0 ? ss : 8;
  }
  switch (psx_ctx_get_function_ret_token_kind(fn->funcname, fn->funcname_len)) {
    case TK_CHAR: return 1;
    case TK_SHORT: return 2;
    case TK_LONG: return 8;
    case TK_FLOAT: return 4;
    case TK_DOUBLE: return 8;
    default: return 4;  /* int / その他 */
  }
}

int ps_node_type_size(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.type_size;
    case ND_GVAR: return as_mem(node)->type_size;
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_PTR_CAST:
      return as_mem(node)->type_size;
    case ND_COMMA:
      return ps_node_type_size(node->rhs);
    case ND_TERNARY: {
      int l = ps_node_type_size(node->rhs);
      if (l > 0) return l;
      return ps_node_type_size(((node_ctrl_t *)node)->els);
    }
    case ND_FUNCALL: {
      /* 関数呼び出し: 戻り値の型サイズを semantic ctx から推定する。
       *   struct 戻り値 (ret_struct_size > 0)  → そのサイズ
       *   float                                → 4
       *   double / long double (lowered to d)  → 8
       *   それ以外 (int / pointer 等)          → 4 (int)
       * ポインタ戻り値 (`char *get(void)`) の sizeof は本来 8 だが、
       * parser がポインタ戻り値かを覚えていないので int と区別がつかない。
       * 既存 fixture でこのケースは使われていないため一旦 4 にしている。 */
      if (node->ret_struct_size > 0) return node->ret_struct_size;
      /* ポインタ戻り値 (`int *g()`) は値が 8 バイト (`sizeof(g())`==8)。 */
      {
        node_func_t *fn = (node_func_t *)node;
        if (fn->callee == NULL && fn->funcname &&
            psx_ctx_get_function_ret_is_pointer(fn->funcname, fn->funcname_len))
          return 8;
      }
      if (node->fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
      if (node->fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
      return 4;
    }
    /* 算術/論理演算: ポインタ算術 (ptr ± int) なら 8、それ以外は
     * C11 6.3.1.8 通常算術変換に従い、両オペランドのうち広い方を返す。
     * ND_NUM のように type_size を持たないノードでは 0 が返るので、int (4) に
     * 落とす。`sizeof(a+b)` や `sizeof(n++)` で 8 になる誤りを防ぐ。 */
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITOR:
    case ND_BITXOR:
    case ND_SHL:
    case ND_SHR: {
      if (ps_node_is_pointer(node)) return 8;
      int l = ps_node_type_size(node->lhs);
      int r = ps_node_type_size(node->rhs);
      int m = l > r ? l : r;
      return m > 0 ? m : 4;
    }
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC: {
      int s = ps_node_type_size(node->lhs);
      return s > 0 ? s : 4;
    }
    case ND_LT: case ND_LE:
    case ND_EQ: case ND_NE:
    case ND_LOGAND: case ND_LOGOR:
      return 4; /* 比較/論理結果は int (C11 6.5.8/9) */
    case ND_NUM: {
      /* 整数/浮動小数リテラルの型サイズ。従来 0 を返し sizeof_expr_node の既定 8 に
       * 落ちて `sizeof(0)`/`sizeof(1L+2)` が誤っていた。fp_kind と long サフィックスで
       * 判定する (int=4, long/long long=8, float=4, double=8)。 */
      node_num_t *n = (node_num_t *)node;
      if (n->base.fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
      if (n->base.fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
      return n->int_is_long ? 8 : 4;
    }
    default:
      return 0;
  }
}

int ps_node_deref_size(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.deref_size;
    case ND_GVAR: return as_mem(node)->deref_size;
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_PTR_CAST:
      return as_mem(node)->deref_size;
    case ND_COMMA:
      return ps_node_deref_size(node->rhs);
    /* 条件演算子: ポインタ側分岐の deref_size を引き継ぐ
     * (`(c ? p : q)[i]` の要素サイズ決定に必要)。 */
    case ND_TERNARY: {
      int l = ps_node_deref_size(node->rhs);
      if (l > 0) return l;
      return ps_node_deref_size(((node_ctrl_t *)node)->els);
    }
    /* ND_ADD/SUB の結果がポインタなら、ポインタ側の deref_size を引き継ぐ。 */
    case ND_ADD:
    case ND_SUB: {
      int l = ps_node_deref_size(node->lhs);
      if (l > 0) return l;
      return ps_node_deref_size(node->rhs);
    }
    /* `p++` 等の inc/dec はオペランドの deref_size をそのまま継承する。
     * `*p++` で deref のロード幅 (= pointee サイズ) を正しく決めるのに必要。 */
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return ps_node_deref_size(node->lhs);
    /* ポインタ戻り値の関数 `int *g(); g()[i]` / `g()+i`: pointee サイズを返さないと
     * 添字/ポインタ算術がスケールせず 1 バイト加算になる (miscompile/SIGSEGV)。
     * 配列へのポインタ戻り `int (*f())[N]` では pointee は配列 (N*base) なので行ストライドを返す。 */
    case ND_FUNCALL: {
      int base = funcall_ret_pointee_size(node);
      node_func_t *fn = (node_func_t *)node;
      if (base > 0 && fn->callee == NULL && fn->funcname) {
        int fd = psx_ctx_get_function_ret_pointee_array_first_dim(fn->funcname, fn->funcname_len);
        if (fd > 0) return fd * base;
        /* 多段ポインタ戻り `int **g()`: `*g()` の結果はまだポインタ (8B) なので、
         * 1 段目 deref のロード幅 / 添字スケールは基底型でなく 8 を返す。最内基底型は
         * psx_node_base_deref_size が別途返す。 */
        if (psx_ctx_get_function_ret_pointer_levels(fn->funcname, fn->funcname_len) >= 2)
          return 8;
      }
      return base;
    }
    default:
      return 0;
  }
}

int ps_node_is_pointer(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.is_pointer;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_PTR_CAST:
      return as_mem(node)->is_pointer;
    case ND_COMMA:
      return ps_node_is_pointer(node->rhs);
    /* C11 6.5.15: 条件演算子の結果は両オペランドがポインタなら
     * ポインタ。`(c ? p : q)[i]` の subscript 判定で必要。 */
    case ND_TERNARY:
      return ps_node_is_pointer(node->rhs) ||
             ps_node_is_pointer(((node_ctrl_t *)node)->els);
    /* C11 6.5.6: ポインタ + 整数 / 整数 + ポインタ / ポインタ - 整数 の結果
     * もポインタ。新規 ND_ADD/SUB ノードに is_pointer 属性を直接書けない
     * (psx_node_new_binary は node_t を作る) ので、子を見て判定する。 */
    case ND_ADD:
      return ps_node_is_pointer(node->lhs) || ps_node_is_pointer(node->rhs);
    case ND_SUB:
      /* ポインタ - ポインタ は ptrdiff_t (整数) なので除外。
       * ポインタ - 整数 のみポインタ扱い。 */
      if (ps_node_is_pointer(node->lhs) && ps_node_is_pointer(node->rhs)) return 0;
      return ps_node_is_pointer(node->lhs);
    /* 関数呼び出しの戻り値型がポインタ (`int *get(void); get()[0]`) なら、
     * その式は配列/ポインタ。subscript チェックを通すために 1 を返す。 */
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      if (fn->callee == NULL && fn->funcname) {
        return psx_ctx_get_function_ret_is_pointer(fn->funcname, fn->funcname_len);
      }
      return 0;
    }
    case ND_FUNCREF:
      /* 関数シンボルは関数ポインタ値。 */
      return 1;
    default:
      return 0;
  }
}

int psx_node_pointer_qual_levels(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.pointer_qual_levels;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_PTR_CAST:
      return as_mem(node)->pointer_qual_levels;
    case ND_COMMA:
      return psx_node_pointer_qual_levels(node->rhs);
    /* 多段ポインタ戻り `int **g()` の funcall: 段数 (>=2) を返し、build_unary_deref_node の
     * pql>=2 分岐に乗せて `*g()` を「1 段減らしたポインタ」として組ませる。単段ポインタ戻り
     * (`int *g()`) は従来どおり 0 を返し挙動を変えない (ps_node_is_pointer 側で別途ポインタ判定)。 */
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      if (fn->callee == NULL && fn->funcname) {
        int lv = psx_ctx_get_function_ret_pointer_levels(fn->funcname, fn->funcname_len);
        if (lv >= 2) return lv;
      }
      return 0;
    }
    default:
      return 0;
  }
}

int psx_node_base_deref_size(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.base_deref_size;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_PTR_CAST:
      return as_mem(node)->base_deref_size;
    case ND_COMMA:
      return psx_node_base_deref_size(node->rhs);
    /* 多段ポインタ戻り `int **g()` の funcall: 最内基底型サイズ (int=4) を返す。
     * build_unary_deref_node の pql>=2 分岐が最終 deref のロード幅に使う。 */
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      if (fn->callee == NULL && fn->funcname &&
          psx_ctx_get_function_ret_pointer_levels(fn->funcname, fn->funcname_len) >= 2)
        return funcall_ret_pointee_size(node);
      return 0;
    }
    default:
      return 0;
  }
}

tk_float_kind_t psx_node_pointee_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  switch (node->kind) {
    case ND_LVAR: return (tk_float_kind_t)as_lvar(node)->mem.pointee_fp_kind;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_PTR_CAST:
      return (tk_float_kind_t)as_mem(node)->pointee_fp_kind;
    case ND_COMMA:
      return psx_node_pointee_fp_kind(node->rhs);
    /* ポインタ算術 (`a + 1`) / inc・dec (`a++`) の結果も同じ pointee を指す。
     * `*(a+1)` 等の deref が fp load になるよう pointee_fp_kind を継承する。 */
    case ND_ADD:
    case ND_SUB: {
      tk_float_kind_t l = psx_node_pointee_fp_kind(node->lhs);
      if (l != TK_FLOAT_KIND_NONE) return l;
      return psx_node_pointee_fp_kind(node->rhs);
    }
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return psx_node_pointee_fp_kind(node->lhs);
    /* `double *g(); g()[i]` の subscript を fp load にするため、ポインタ戻り値の
     * pointee fp 種別を返す。 */
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      if (fn->callee != NULL || !fn->funcname) return TK_FLOAT_KIND_NONE;
      if (!psx_ctx_get_function_ret_is_pointer(fn->funcname, fn->funcname_len))
        return TK_FLOAT_KIND_NONE;
      token_kind_t rk = psx_ctx_get_function_ret_token_kind(fn->funcname, fn->funcname_len);
      if (rk == TK_FLOAT) return TK_FLOAT_KIND_FLOAT;
      if (rk == TK_DOUBLE) return TK_FLOAT_KIND_DOUBLE;
      return TK_FLOAT_KIND_NONE;
    }
    default:
      return TK_FLOAT_KIND_NONE;
  }
}

/* pointer-to-VLA (`int (*p)[m]`) の行ストライドスロット (実行時値) のフレームオフセット。
 * 無ければ 0。ポインタ算術 (`p + 1`) のスケールに使う。ND_ADD/SUB は被演算子を辿る。 */
int psx_node_vla_row_stride_frame_off(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.vla_row_stride_frame_off;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ADDR:
      return as_mem(node)->vla_row_stride_frame_off;
    case ND_ADD:
    case ND_SUB: {
      int l = psx_node_vla_row_stride_frame_off(node->lhs);
      if (l != 0) return l;
      return psx_node_vla_row_stride_frame_off(node->rhs);
    }
    default:
      return 0;
  }
}

void psx_node_get_tag_type(node_t *node, token_kind_t *tag_kind, char **tag_name, int *tag_len, int *is_tag_pointer) {
  token_kind_t kind = TK_EOF;
  char *name = NULL;
  int len = 0;
  int ptr = 0;
  if (node) {
    switch (node->kind) {
      case ND_LVAR:
        kind = as_lvar(node)->mem.tag_kind;
        name = as_lvar(node)->mem.tag_name;
        len = as_lvar(node)->mem.tag_len;
        ptr = as_lvar(node)->mem.is_tag_pointer;
        break;
      case ND_GVAR:
      case ND_DEREF:
      case ND_ADDR:
      case ND_STRING:
      case ND_PTR_CAST:
        kind = as_mem(node)->tag_kind;
        name = as_mem(node)->tag_name;
        len = as_mem(node)->tag_len;
        ptr = as_mem(node)->is_tag_pointer;
        break;
      case ND_ASSIGN:
        /* 代入式の結果は左辺の型。ノード自身に tag が無い (複合代入 `p += n` 等)
         * 場合は左辺から継承して `(p += n)->m` を解決できるようにする。 */
        kind = as_mem(node)->tag_kind;
        name = as_mem(node)->tag_name;
        len = as_mem(node)->tag_len;
        ptr = as_mem(node)->is_tag_pointer;
        if (kind == TK_EOF) {
          psx_node_get_tag_type(node->lhs, &kind, &name, &len, &ptr);
        }
        break;
      case ND_COMMA:
        psx_node_get_tag_type(node->rhs, &kind, &name, &len, &ptr);
        break;
      /* `p + n` のようなポインタ算術: tag info を pointer 側 (lhs) から継承する。
       * `(p+1)->x` や `(p+i).x` (`.` は通常 lvalue のみだが parser が許す形) で
       * tag が引けないと arrow/dot がエラーになる。 */
      case ND_ADD:
      case ND_SUB:
        psx_node_get_tag_type(node->lhs, &kind, &name, &len, &ptr);
        if (kind == TK_EOF) {
          psx_node_get_tag_type(node->rhs, &kind, &name, &len, &ptr);
        }
        break;
      /* `(cond ? a : b).x` 等の struct ternary 結果からメンバアクセスする際、
       * 両分岐は同型 struct のはずなので then 側から tag を引く。 */
      case ND_TERNARY: {
        node_ctrl_t *t = (node_ctrl_t *)node;
        psx_node_get_tag_type(t->base.rhs, &kind, &name, &len, &ptr);
        if (kind == TK_EOF && t->els) {
          psx_node_get_tag_type(t->els, &kind, &name, &len, &ptr);
        }
        break;
      }
      case ND_FUNCALL: {
        node_func_t *fn = (node_func_t *)node;
        if (fn->callee == NULL && fn->funcname) {
          psx_ctx_get_function_ret_tag(fn->funcname, fn->funcname_len, &kind, &name, &len);
          /* 戻り型が struct/union ポインタ (`struct N *get(void)`) なら is_tag_pointer
           * を立てる。`get()->m` の `->` 判定に必要 (以前は常に 0 で誤判定していた)。 */
          ptr = psx_ctx_get_function_ret_is_pointer(fn->funcname, fn->funcname_len);
        } else if (fn->callee) {
          /* 間接呼び出し (関数ポインタ経由) `op(41).v` / `op(41)->v`: callee の funcptr
           * 変数は tag フィールドに戻り tag を保持する (`struct R (*op)(int)` → tag=R)。
           * funcptr 自身の is_tag_pointer は「変数がポインタ」を表し常に 1 なので使わず、
           * 戻り値がポインタか否かは pointer_qual_levels で判定する
           * (pql=1: 値戻り `struct R (*op)()` → ptr=0 / pql>=2: ポインタ戻り
           * `struct R *(*op)()` → ptr=1)。これがないと funcall ノードの tag が引けず
           * `.`/`->` が E3005 になっていた。 */
          psx_node_get_tag_type(fn->callee, &kind, &name, &len, NULL);
          if (kind != TK_EOF)
            ptr = psx_node_pointer_qual_levels(fn->callee) >= 2 ? 1 : 0;
        }
        break;
      }
      /* `(++p)->m` / `(p++)->m`: inc/dec はオペランドと同じ型なので tag を継承する。 */
      case ND_PRE_INC:
      case ND_PRE_DEC:
      case ND_POST_INC:
      case ND_POST_DEC:
        psx_node_get_tag_type(node->lhs, &kind, &name, &len, &ptr);
        break;
      default:
        break;
    }
  }
  if (tag_kind) *tag_kind = kind;
  if (tag_name) *tag_name = name;
  if (tag_len) *tag_len = len;
  if (is_tag_pointer) *is_tag_pointer = ptr;
}

static int node_is_unsigned(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.is_unsigned;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
      return as_mem(node)->is_unsigned;
    default: return node->is_unsigned;
  }
}

/* node_is_unsigned の公開ラッパ。IR builder が比較の符号 (通常算術変換) を
 * 決める際、オペランドの符号を ND_LVAR の mem.is_unsigned まで含めて判定する
 * ために使う。生の node->is_unsigned は LVAR/GVAR では 0 のままなので不可。 */
int ps_node_is_unsigned(node_t *node) { return node_is_unsigned(node); }

/* node の符号フラグを設定する (node_is_unsigned が読むフィールドに一致させる)。
 * `(int)u` / `(unsigned)i` キャストで結果の符号を確定するのに使う。 */
void psx_node_set_unsigned(node_t *node, int is_unsigned) {
  if (!node) return;
  int u = is_unsigned ? 1 : 0;
  switch (node->kind) {
    case ND_LVAR: as_lvar(node)->mem.is_unsigned = u; break;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
      as_mem(node)->is_unsigned = u; break;
    default: node->is_unsigned = u; break;
  }
}

node_t *psx_node_new_binary(node_kind_t kind, node_t *lhs, node_t *rhs) {
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  if (lhs && lhs->fp_kind) node->fp_kind = lhs->fp_kind;
  if (rhs && rhs->fp_kind > node->fp_kind) node->fp_kind = rhs->fp_kind;

  if (kind == ND_EQ || kind == ND_NE || kind == ND_LT || kind == ND_LE ||
      kind == ND_LOGAND || kind == ND_LOGOR ||
      kind == ND_BITAND || kind == ND_BITXOR || kind == ND_BITOR ||
      kind == ND_SHL || kind == ND_SHR) {
    node->fp_kind = TK_FLOAT_KIND_NONE;
  }
  // unsigned伝播: どちらかがunsignedなら結果もunsigned
  if (node_is_unsigned(lhs) || node_is_unsigned(rhs)) {
    node->is_unsigned = 1;
  }
  // _Complex伝播: どちらかが_Complexなら結果も_Complex
  if ((lhs && lhs->is_complex) || (rhs && rhs->is_complex)) {
    node->is_complex = 1;
  }
  return node;
}

node_t *psx_node_new_num(long long val) {
  node_num_t *node = arena_alloc(sizeof(node_num_t));
  node->base.kind = ND_NUM;
  node->val = val;
  return (node_t *)node;
}

node_t *psx_node_new_lvar(int offset) {
  node_lvar_t *node = arena_alloc(sizeof(node_lvar_t));
  node->mem.base.kind = ND_LVAR;
  node->offset = offset;
  node->mem.type_size = 8;
  return (node_t *)node;
}

node_t *psx_node_new_lvar_typed(int offset, int type_size) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar(offset);
  node->mem.type_size = type_size;
  return (node_t *)node;
}

node_mem_t *psx_node_new_assign(node_t *lhs, node_t *rhs) {
  /* C11 6.5.16: 代入の RHS は void 型であってはならない。
   * 直接呼び出し ND_FUNCALL のみチェック (間接呼び出しは型情報未保持)。 */
  if (rhs && rhs->kind == ND_FUNCALL) {
    node_func_t *fn = (node_func_t *)rhs;
    if (fn->callee == NULL && fn->funcname &&
        psx_ctx_is_function_ret_void(fn->funcname, fn->funcname_len)) {
      psx_diag_ctx(tk_get_current_token(), "assign",
                   "void 戻り値関数の結果は代入/初期化に使えません: '%.*s' (C11 6.5.16)",
                   fn->funcname_len, fn->funcname);
    }
  }
  node_mem_t *node = arena_alloc(sizeof(node_mem_t));
  node->base.kind = ND_ASSIGN;
  node->base.lhs = lhs;
  node->base.rhs = rhs;
  node->base.fp_kind = lhs ? lhs->fp_kind : TK_FLOAT_KIND_NONE;
  if (lhs && lhs->is_complex) {
    node->base.is_complex = 1;
  }
  if (lhs && lhs->is_atomic) {
    node->base.is_atomic = 1;
  }
  return node;
}

void psx_node_reject_const_assign(node_t *node, const char *op) {
  (void)op;
  if (!node) return;
  if (node->kind == ND_LVAR || node->kind == ND_GVAR) {
    node_mem_t *mem = as_mem(node);
    /* ag_c の慣習: ポインタ変数の is_const_qualified は「pointee の const」を
     * 表す (_Generic の判定等で利用)。「変数自身の const」は
     * pointer_const_qual_mask の bit 0 で保持される。
     * したがって p = q を拒否するのはこのビットが立っているときのみ
     * (`int * const p;` のケース)。非ポインタ変数は従来通り
     * is_const_qualified を見る (`const int x = 5; x = 10;` を拒否)。 */
    int self_const = mem->is_pointer ? (int)(mem->pointer_const_qual_mask & 1u)
                                      : mem->is_const_qualified;
    if (self_const) {
      diag_emit_tokf(DIAG_ERR_PARSER_CONST_ASSIGNMENT, curtok(),
                     diag_message_for(DIAG_ERR_PARSER_CONST_ASSIGNMENT));
    }
  }
}

static int node_pointee_is_const(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR:
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_PTR_CAST: {
      node_mem_t *m = (node_mem_t *)node;
      return m->is_tag_pointer && m->is_const_qualified;
    }
    case ND_COMMA:
      return node_pointee_is_const(node->rhs);
    default:
      return 0;
  }
}

void psx_node_reject_const_qual_discard(node_t *lhs, node_t *rhs) {
  if (!lhs || !rhs) return;
  if (lhs->kind != ND_LVAR && lhs->kind != ND_GVAR) return;
  node_mem_t *lhs_mem = as_mem(lhs);
  if (!lhs_mem->is_tag_pointer) return;
  if (lhs_mem->is_const_qualified) return;
  if (node_pointee_is_const(rhs)) {
    diag_emit_tokf(DIAG_ERR_PARSER_CONST_QUAL_DISCARD, curtok(),
                   diag_message_for(DIAG_ERR_PARSER_CONST_QUAL_DISCARD));
  }
}

void psx_node_expect_lvalue(node_t *node, const char *op) {
  if (!node || (node->kind != ND_LVAR && node->kind != ND_DEREF && node->kind != ND_GVAR)) {
    diag_emit_tokf(DIAG_ERR_PARSER_LVALUE_REQUIRED, curtok(),
                   diag_message_for(DIAG_ERR_PARSER_LVALUE_REQUIRED), (char *)op);
  }
}

void psx_node_expect_incdec_target(node_t *node, const char *op) {
  psx_node_expect_lvalue(node, op);
  psx_node_reject_const_assign(node, op);
  /* C11 6.5.2.4 / 6.5.3.1: ++ / -- の対象は実数型 (整数・浮動小数点) または
   * ポインタ型でよい。float / double も許可する。 */
}

node_t *psx_node_new_compound_assign(node_t *lhs, node_kind_t op_kind, node_t *rhs, const char *op) {
  psx_node_expect_lvalue(lhs, op);
  psx_node_reject_const_assign(lhs, op);
  /* C11 6.5.16.2p3: `p += n` でポインタ算術するときは、rhs を要素サイズ倍に
   * スケーリングする。`add()` 経路と挙動を揃える。 */
  if ((op_kind == ND_ADD || op_kind == ND_SUB) && ps_node_is_pointer(lhs)) {
    int ds = ps_node_deref_size(lhs);
    if (ds > 1) {
      rhs = psx_node_new_binary(ND_MUL, rhs, psx_node_new_num(ds));
    }
  }
  node_t *op_expr = psx_node_new_binary(op_kind, lhs, rhs);
  /* C11 6.3.1.2: _Bool への (複合) 代入は結果を 0/1 に正規化する。
   * 通常代入と同様、op の結果を (result != 0) で包む。 */
  int lhs_is_bool = 0;
  if (lhs && (lhs->kind == ND_LVAR || lhs->kind == ND_DEREF || lhs->kind == ND_GVAR)) {
    lhs_is_bool = ((node_mem_t *)lhs)->is_bool;
  }
  if (lhs_is_bool) {
    op_expr = psx_node_new_binary(ND_NE, op_expr, psx_node_new_num(0));
  }
  node_mem_t *assign_node = psx_node_new_assign(lhs, op_expr);
  assign_node->type_size = ps_node_type_size(lhs);
  assign_node->base.fp_kind = lhs ? lhs->fp_kind : 0;
  return (node_t *)assign_node;
}
