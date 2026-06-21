/*
 * AST → IR ビルダー (Phase 2 最小版)。
 *
 * 対応 AST node:
 *   ND_FUNCDEF (引数なし、戻り値は i32)
 *   ND_BLOCK
 *   ND_RETURN
 *   ND_NUM   (整数のみ、i32 として扱う)
 *   ND_LVAR  (整数 i32 のみ)
 *   ND_ASSIGN (LVAR 左辺、整数式右辺)
 *   ND_ADD / ND_SUB / ND_MUL / ND_DIV / ND_MOD
 *
 * 対応外の node を踏むと ctx->failed を立て、build_module は NULL を返す。
 *
 * ローカル変数モデル: 各 lvar offset に対し IR_ALLOCA で frame slot を確保し、
 * その vreg (ポインタ) を offset→vreg マップに記録する。
 * LVAR 参照は LOAD、ASSIGN は STORE。
 */

#include "ir_builder.h"
#include "ir.h"
/* Phase C2: parser の公開 API は parser_public.h 1 本に集約。
 * internal ヘッダへの直接 include は禁止 (parser_public.h が必要に応じて
 * transitively 取り込む形で内部実装の変更を吸収する)。 */
#include "../parser/parser_public.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LVARS 256
#define MAX_LOOP_DEPTH 32
#define MAX_CASES 256
#define MAX_LABELS 64

/* break/continue 用ループスタック。それぞれ「飛び先ブロック」。 */
typedef struct {
  ir_block_t *continue_block;
  ir_block_t *break_block;
} loop_ctx_t;

/* 現在 build 中の switch 内で「AST の case/default node → IR block」のマップ。
 * ND_CASE / ND_DEFAULT が body の中に現れたとき、対応する block へ
 * switch_to_new_block する。switch はネスト可能なので退避用配列もある。 */
typedef struct {
  void *ast_node;       /* node_case_t* または node_default_t* */
  ir_block_t *block;
} case_map_entry_t;

/* goto/label 解決用: 関数内の label 名 → IR block マップ。
 * 関数 build 開始時に AST 内の全 ND_LABEL を pre-walk して登録する。 */
typedef struct {
  const char *name;
  int name_len;
  ir_block_t *block;
} label_map_entry_t;

typedef struct {
  ir_module_t *m;
  ir_func_t *f;
  /* 現在処理中の関数 AST。lvars リストを引くため。 */
  node_func_t *cur_fn;
  int failed;
  /* offset → ALLOCA vreg (ポインタ) のマップ */
  int lvar_offset[MAX_LVARS];
  int lvar_vreg[MAX_LVARS];
  int lvar_count;
  loop_ctx_t loops[MAX_LOOP_DEPTH];
  int loop_depth;
  case_map_entry_t case_map[MAX_CASES];
  int case_map_count;
  label_map_entry_t labels[MAX_LABELS];
  int label_count;
} ir_build_ctx_t;

static void fail(ir_build_ctx_t *ctx, const char *msg) {
  if (ctx->failed) return;
  ctx->failed = 1;
  /* AG_USE_IR=1 が明示されたときのみ verbose にメッセージを出す。
   * default では silent に fallback (Phase 7a 以降)。 */
  const char *use_ir = getenv("AG_USE_IR");
  if (use_ir && strcmp(use_ir, "1") == 0) {
    fprintf(stderr, "ir_build: unsupported: %s\n", msg);
  }
}

static int find_alloca_vreg(ir_build_ctx_t *ctx, int offset) {
  for (int i = 0; i < ctx->lvar_count; i++) {
    if (ctx->lvar_offset[i] == offset) return ctx->lvar_vreg[i];
  }
  return -1;
}

/* ND_LVAR.offset を含む lvar_t (= 親 owner) を探す。struct メンバアクセスでは
 * `s.m` の ND_LVAR が (s.offset + member_offset) を持つので、ここで `s` 全体を
 * 引き当てる。スカラ lvar は自分自身を返す (delta = 0)。
 * 現在処理中の関数の lvars リスト (parser が保存) を walk する。 */
static lvar_t *find_owning_lvar(ir_build_ctx_t *ctx, int offset) {
  lvar_t *head = ctx && ctx->cur_fn ? ctx->cur_fn->lvars : NULL;
  for (lvar_t *var = head; var; var = var->next_all) {
    int sz = var->size > 0 ? var->size : 1;
    if (var->offset <= offset && offset < var->offset + sz) return var;
  }
  return NULL;
}

/* スカラ要素 (struct member 含む) の「ロード時の値の IR 型」。
 * mem.type_size を見る。配列名は別経路 (ND_ADDR 経由) を通る。 */
static ir_type_t elem_value_type(int type_size) {
  if (type_size >= 8) return IR_TY_PTR;
  if (type_size == 4) return IR_TY_I32;
  if (type_size == 2) return IR_TY_I16;
  return IR_TY_I8;
}

static ir_type_t lvar_value_type(node_lvar_t *lv) {
  /* float/double 変数は base.fp_kind で判定 */
  unsigned fpk = lv->mem.base.fp_kind;
  if (fpk == TK_FLOAT_KIND_FLOAT) return IR_TY_F32;
  if (fpk >= TK_FLOAT_KIND_DOUBLE) return IR_TY_F64;
  int elem = lv->mem.type_size > 0 ? lv->mem.type_size : 4;
  return elem_value_type(elem);
}

static int is_fp_type(ir_type_t t) {
  return t == IR_TY_F32 || t == IR_TY_F64;
}

/* node->fp_kind から IR の浮動小数型を返すヘルパ (Phase A3 リファクタリング)。
 * 非浮動小数 (TK_FLOAT_KIND_NONE) なら IR_TY_I32 を返す。
 * 呼び出し側で type_size に応じた整数型 (I8/I16/I32/PTR) に上書きすること。 */
static ir_type_t ir_type_from_node(node_t *node) {
  if (node->fp_kind == TK_FLOAT_KIND_FLOAT) return IR_TY_F32;
  if (node->fp_kind >= TK_FLOAT_KIND_DOUBLE) return IR_TY_F64;
  return IR_TY_I32;
}

/* owner (= lvar_t の親) に対して ALLOCA を 1 回だけ確保し、その vreg を返す。
 * 同じ owner offset で再度呼ばれたら既存 vreg を返す。 */
static int alloca_for_owner(ir_build_ctx_t *ctx, lvar_t *var) {
  if (!var) {
    fail(ctx, "owning lvar not found");
    return -1;
  }
  int existing = find_alloca_vreg(ctx, var->offset);
  if (existing >= 0) return existing;
  if (ctx->lvar_count >= MAX_LVARS) {
    fail(ctx, "too many local variables");
    return -1;
  }
  int size = var->size > 0 ? var->size : 4;
  int elem = var->elem_size > 0 ? var->elem_size : 4;
  int align = (elem >= 8) ? 8 : (elem >= 4 ? 4 : (elem >= 2 ? 2 : 1));
  /* struct のような複合型は 8B align を優先 (簡略化) */
  if (size >= 8 && align < 8) align = 8;
  /* _Alignas(N) で明示指定された align は natural より強い (大きい) ものを尊重。 */
  if (var->align_bytes > align) align = var->align_bytes;
  /* 過剰整列ローカル (_Alignas(>16))。x29 は 16 整列のみで `x29 + 固定オフセット`
   * では >16 整列にできない。予備領域 (size + A) を確保し、実行時にアドレスを A へ
   * 丸めた vreg を base にする (IR_ALIGN_PTR)。16 以下は従来どおり。 */
  int over_aligned = (var->align_bytes > 16);
  int alloc_size = over_aligned ? size + var->align_bytes : size;
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_ALLOCA);
  inst->dst = ir_val_vreg(v, IR_TY_PTR);
  inst->alloca_size = alloc_size;
  inst->alloca_align = align;
  ir_func_append_inst(ctx->f, inst);
  int base = v;
  if (over_aligned) {
    int av = ir_func_new_vreg(ctx->f);
    ir_inst_t *ap = ir_inst_new(IR_ALIGN_PTR);
    ap->dst = ir_val_vreg(av, IR_TY_PTR);
    ap->src1 = ir_val_vreg(v, IR_TY_PTR);
    ap->alloca_align = var->align_bytes;  /* 丸め先アライメント A */
    ir_func_append_inst(ctx->f, ap);
    base = av;  /* lvar の base は丸め後アドレス */
  }
  ctx->lvar_offset[ctx->lvar_count] = var->offset;
  ctx->lvar_vreg[ctx->lvar_count] = base;
  ctx->lvar_count++;
  return base;
}

/* v を target_ty に変換した vreg を返す。型が同じならそのまま。
 * 整数⇔浮動小数点は IR_I2F / IR_F2I、float⇔double は IR_F2F、
 * i32 → i64 等の整数幅変換は SEXT/ZEXT/TRUNC。
 * ポインタ型は他とは混ぜず、unsigned 拡張は呼び出し元の責任とする。 */
static ir_val_t coerce_to_type(ir_build_ctx_t *ctx, ir_val_t v, ir_type_t target_ty) {
  if (v.type == target_ty) return v;
  int v_is_fp = is_fp_type(v.type);
  int t_is_fp = is_fp_type(target_ty);
  /* int → float */
  if (!v_is_fp && t_is_fp) {
    int dst = ir_func_new_vreg(ctx->f);
    ir_inst_t *inst = ir_inst_new(IR_I2F);
    inst->dst = ir_val_vreg(dst, target_ty);
    inst->src1 = v;
    ir_func_append_inst(ctx->f, inst);
    return ir_val_vreg(dst, target_ty);
  }
  /* float → int */
  if (v_is_fp && !t_is_fp) {
    int dst = ir_func_new_vreg(ctx->f);
    ir_inst_t *inst = ir_inst_new(IR_F2I);
    inst->dst = ir_val_vreg(dst, target_ty);
    inst->src1 = v;
    ir_func_append_inst(ctx->f, inst);
    return ir_val_vreg(dst, target_ty);
  }
  /* float ↔ float (f32 ↔ f64) */
  if (v_is_fp && t_is_fp) {
    int dst = ir_func_new_vreg(ctx->f);
    ir_inst_t *inst = ir_inst_new(IR_F2F);
    inst->dst = ir_val_vreg(dst, target_ty);
    inst->src1 = v;
    ir_func_append_inst(ctx->f, inst);
    return ir_val_vreg(dst, target_ty);
  }
  /* 即値: 型 tag を更新するだけで良い (mov w/x で適切な幅にロードされる)。
   * PTR 特殊ケースより前に処理しないと、ir_val_vreg(IR_VAL_IMM, ...) で imm 値を
   * 失い、`p = c ? &x : 0` の null 分岐が garbage になる。 */
  if (v.id == IR_VAL_IMM) {
    v.type = target_ty;
    return v;
  }
  /* 整数同士の幅変換 (PTR <-> i32/i64 もここ通る; 実体は同じ 64bit reg なので
   * 値だけ流用)。型 tag は新しい dst でラップする。 */
  if (target_ty == IR_TY_PTR || v.type == IR_TY_PTR) {
    return ir_val_vreg(v.id, target_ty);
  }
  /* 32 → 64 拡張: SEXT を入れる (符号情報なしのとき signed 拡張で C 規約に合う)。 */
  if (ir_type_size(target_ty) > ir_type_size(v.type)) {
    int dst = ir_func_new_vreg(ctx->f);
    ir_inst_t *inst = ir_inst_new(IR_SEXT);
    inst->dst = ir_val_vreg(dst, target_ty);
    inst->src1 = v;
    ir_func_append_inst(ctx->f, inst);
    return ir_val_vreg(dst, target_ty);
  }
  /* 64 → 32 縮小: TRUNC を入れる。 */
  if (ir_type_size(target_ty) < ir_type_size(v.type)) {
    int dst = ir_func_new_vreg(ctx->f);
    ir_inst_t *inst = ir_inst_new(IR_TRUNC);
    inst->dst = ir_val_vreg(dst, target_ty);
    inst->src1 = v;
    ir_func_append_inst(ctx->f, inst);
    return ir_val_vreg(dst, target_ty);
  }
  /* fall-through: 同サイズなら tag だけ更新 (例: 内部表現の都合) */
  v.type = target_ty;
  return v;
}

/* 整数即値を LOAD_IMM で生成し vreg を返す。 */
__attribute__((unused))
static int emit_load_imm(ir_build_ctx_t *ctx, long long imm, ir_type_t ty) {
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_LOAD_IMM);
  inst->dst = ir_val_vreg(v, ty);
  inst->src1 = ir_val_imm(ty, imm);
  ir_func_append_inst(ctx->f, inst);
  return v;
}

/* 2 項演算を emit して結果 vreg を返す。 */
static int emit_binop(ir_build_ctx_t *ctx, ir_op_t op, ir_val_t a, ir_val_t b, ir_type_t ty) {
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(op);
  inst->dst = ir_val_vreg(v, ty);
  inst->src1 = a;
  inst->src2 = b;
  ir_func_append_inst(ctx->f, inst);
  return v;
}

/* val を i32 の真偽値 (val != 0) に変換する。fp 値は FNE against 0.0 で比較する
 * (整数 NE のまま分岐/正規化すると codegen が fp の bit を整数再解釈し、spill された
 * 4B float を 8B load して 0.0 を真と誤判定していた)。整数はそのまま NE。 */
static ir_val_t emit_truthiness(ir_build_ctx_t *ctx, ir_val_t val) {
  if (is_fp_type(val.type)) {
    int zero = ir_func_new_vreg(ctx->f);
    ir_inst_t *zi = ir_inst_new(IR_LOAD_FP_IMM);
    zi->dst = ir_val_vreg(zero, val.type);
    zi->src1 = ir_val_fp_imm(val.type, 0.0);
    ir_func_append_inst(ctx->f, zi);
    int b = emit_binop(ctx, IR_FNE, val, ir_val_vreg(zero, val.type), IR_TY_I32);
    return ir_val_vreg(b, IR_TY_I32);
  }
  int b = emit_binop(ctx, IR_NE, val, ir_val_imm(val.type, 0), IR_TY_I32);
  return ir_val_vreg(b, IR_TY_I32);
}

/* ND_LVAR.offset → 「実際にアクセスすべきアドレスを持つ vreg」を返す。
 * struct メンバの場合は owner ALLOCA + delta (= IR_LEA) を構築する。 */
static int address_of_lvar(ir_build_ctx_t *ctx, int offset) {
  lvar_t *owner = find_owning_lvar(ctx, offset);
  if (!owner) {
    fail(ctx, "lvar not found");
    return -1;
  }
  int base_vreg = alloca_for_owner(ctx, owner);
  if (base_vreg < 0) return -1;
  int delta = offset - owner->offset;
  if (delta == 0) return base_vreg;
  /* base + delta */
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *lea = ir_inst_new(IR_LEA);
  lea->dst = ir_val_vreg(v, IR_TY_PTR);
  lea->src1 = ir_val_vreg(base_vreg, IR_TY_PTR);
  lea->src2 = ir_val_imm(IR_TY_I64, delta);
  ir_func_append_inst(ctx->f, lea);
  return v;
}

/* グローバル変数 (ND_GVAR) のアドレスをシンボルロードで得て vreg を返す。 */
static int address_of_gvar(ir_build_ctx_t *ctx, node_gvar_t *gv) {
  int v_addr = ir_func_new_vreg(ctx->f);
  ir_inst_t *sym = ir_inst_new(gv->is_thread_local ? IR_LOAD_TLV_ADDR : IR_LOAD_SYM);
  sym->dst = ir_val_vreg(v_addr, IR_TY_PTR);
  sym->sym = gv->name;
  sym->sym_len = gv->name_len;
  ir_func_append_inst(ctx->f, sym);
  return v_addr;
}

/* forward decl: build_expr 内で短絡評価/ternary 用に分岐 helper を呼ぶため。 */
static void emit_br(ir_build_ctx_t *ctx, ir_block_t *target);
static void emit_br_cond(ir_build_ctx_t *ctx, ir_val_t cond,
                          ir_block_t *t_block, ir_block_t *f_block);
static void switch_to_new_block(ir_build_ctx_t *ctx, ir_block_t *b);

static ir_val_t build_expr(ir_build_ctx_t *ctx, node_t *node);

/* build_expr の case 別ヘルパ群 (Phase B1 リファクタリング)。
 * 各 build_node_<kind> は build_expr の 1 case 分の処理を担当し、
 * dispatch (switch) は呼び出すだけにする。 */
static ir_val_t build_node_string(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_gvar(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_num(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_lvar(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_assign(ir_build_ctx_t *ctx, node_t *node);
/* build_node_assign の分岐別サブヘルパ */
static ir_val_t build_assign_complex(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_assign_struct(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_assign_to_lvar(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_assign_to_gvar(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_assign_to_deref(ir_build_ctx_t *ctx, node_t *node);
/* bitfield store: *ptr の bit[bo, bo+bw) を rhs で上書きし、rhs (masking 前) を返す。 */
static ir_val_t emit_bitfield_store(ir_build_ctx_t *ctx, ir_val_t ptr, ir_val_t rhs,
                                     int bit_width, int bit_offset);
/* bitfield load: *ptr の bit[bo, bo+bw) を取り出す (符号付きなら sign extend)。
 * bit_offset+bit_width が 32 を超える (long bitfield) ときは 64bit で扱う。 */
static ir_val_t emit_bitfield_load(ir_build_ctx_t *ctx, ir_val_t ptr,
                                    int bit_width, int bit_offset, int is_signed);
static ir_val_t build_node_addr(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_deref(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_funcref(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_binop(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_funcall(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_comma(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_logand_or(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_ternary(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_fp_to_int(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_int_to_fp(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_fneg(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_creal_cimag(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_inc_dec(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_va_arg_area(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_ptr_cast(ir_build_ctx_t *ctx, node_t *node);
static ir_val_t build_node_vla_alloc(ir_build_ctx_t *ctx, node_t *node);

/* build_stmt の case 別ヘルパ群 (build_expr 分割と同パターン)。 */
static void build_stmt_block(ir_build_ctx_t *ctx, node_t *node);
static void build_stmt_return(ir_build_ctx_t *ctx, node_t *node);
static void build_stmt_if(ir_build_ctx_t *ctx, node_t *node);
static void build_stmt_while(ir_build_ctx_t *ctx, node_t *node);
static void build_stmt_do_while(ir_build_ctx_t *ctx, node_t *node);
static void build_stmt_for(ir_build_ctx_t *ctx, node_t *node);
static void build_stmt_switch(ir_build_ctx_t *ctx, node_t *node);
static void build_stmt_case_default(ir_build_ctx_t *ctx, node_t *node);
static void build_stmt_break(ir_build_ctx_t *ctx, node_t *node);
static void build_stmt_continue(ir_build_ctx_t *ctx, node_t *node);
static void build_stmt_goto(ir_build_ctx_t *ctx, node_t *node);
static void build_stmt_label(ir_build_ctx_t *ctx, node_t *node);

/* _Complex 用ヘルパ: 式 `node` の値 (実部, 虚部) を *dst, *(dst + half) に書く。
 * fp_ty = IR_TY_F64 または IR_TY_F32、half = 8 or 4。
 * node が is_complex=0 のスカラ式なら虚部に 0.0 を書く (scalar→complex promotion)。 */
static void build_complex_to(ir_build_ctx_t *ctx, node_t *node, int dst_ptr_vreg,
                              ir_type_t fp_ty, int half) {
  if (!node || ctx->failed) return;
  /* 複素数 compound literal `(double _Complex){re,im}` は COMMA(init, ref) 形。
   * init (実部/虚部の store) を評価してから、rhs (複素数 lvar 参照) を複素数として
   * dst へコピーする。 */
  if (node->kind == ND_COMMA && node->rhs && node->rhs->is_complex) {
    build_expr(ctx, node->lhs);
    if (ctx->failed) return;
    build_complex_to(ctx, node->rhs, dst_ptr_vreg, fp_ty, half);
    return;
  }
  /* scalar → complex promotion: re = scalar 値、im = 0.0 */
  if (!node->is_complex) {
    ir_val_t v = build_expr(ctx, node);
    if (ctx->failed) return;
    /* 必要なら fp_ty に変換 */
    if (!is_fp_type(v.type)) {
      int cv = ir_func_new_vreg(ctx->f);
      ir_inst_t *inst = ir_inst_new(IR_I2F);
      inst->dst = ir_val_vreg(cv, fp_ty); inst->src1 = v;
      ir_func_append_inst(ctx->f, inst);
      v = ir_val_vreg(cv, fp_ty);
    } else if (v.type != fp_ty) {
      int cv = ir_func_new_vreg(ctx->f);
      ir_inst_t *inst = ir_inst_new(IR_F2F);
      inst->dst = ir_val_vreg(cv, fp_ty); inst->src1 = v;
      ir_func_append_inst(ctx->f, inst);
      v = ir_val_vreg(cv, fp_ty);
    }
    /* store re */
    ir_inst_t *st_re = ir_inst_new(IR_STORE);
    st_re->src1 = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
    st_re->src2 = v;
    ir_func_append_inst(ctx->f, st_re);
    /* store im = 0.0 */
    int v_zero = ir_func_new_vreg(ctx->f);
    ir_inst_t *zi = ir_inst_new(IR_LOAD_FP_IMM);
    zi->dst = ir_val_vreg(v_zero, fp_ty);
    zi->src1 = ir_val_fp_imm(fp_ty, 0.0);
    ir_func_append_inst(ctx->f, zi);
    int im_ptr = ir_func_new_vreg(ctx->f);
    ir_inst_t *lea = ir_inst_new(IR_LEA);
    lea->dst = ir_val_vreg(im_ptr, IR_TY_PTR);
    lea->src1 = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
    lea->src2 = ir_val_imm(IR_TY_I32, half);
    ir_func_append_inst(ctx->f, lea);
    ir_inst_t *st_im = ir_inst_new(IR_STORE);
    st_im->src1 = ir_val_vreg(im_ptr, IR_TY_PTR);
    st_im->src2 = ir_val_vreg(v_zero, fp_ty);
    ir_func_append_inst(ctx->f, st_im);
    return;
  }
  /* 源と先で fp 種別が異なる複素数 (double _Complex ↔ float _Complex 変換)。
   * まず源の fp 種別で temp slot に materialize し (この再帰呼び出しでは
   * src_fp_ty == fp_ty となり下の通常経路に乗る)、各成分を F2F 変換して dst へ
   * 格納する。これにより memcpy が壊す変換を全ノード種別で正しく扱う。 */
  ir_type_t src_fp_ty = (node->fp_kind == TK_FLOAT_KIND_FLOAT) ? IR_TY_F32 : IR_TY_F64;
  if (src_fp_ty != fp_ty) {
    int src_half = (src_fp_ty == IR_TY_F32) ? 4 : 8;
    int slot = ir_func_new_vreg(ctx->f);
    ir_inst_t *al = ir_inst_new(IR_ALLOCA);
    al->dst = ir_val_vreg(slot, IR_TY_PTR);
    al->alloca_size = 2 * src_half;
    al->alloca_align = 8;
    ir_func_append_inst(ctx->f, al);
    build_complex_to(ctx, node, slot, src_fp_ty, src_half);
    if (ctx->failed) return;
    for (int part = 0; part < 2; part++) {
      int src_p = slot, dst_p = dst_ptr_vreg;
      if (part == 1) {
        int sp = ir_func_new_vreg(ctx->f);
        ir_inst_t *ls = ir_inst_new(IR_LEA);
        ls->dst = ir_val_vreg(sp, IR_TY_PTR);
        ls->src1 = ir_val_vreg(slot, IR_TY_PTR);
        ls->src2 = ir_val_imm(IR_TY_I32, src_half);
        ir_func_append_inst(ctx->f, ls);
        src_p = sp;
        int dp = ir_func_new_vreg(ctx->f);
        ir_inst_t *ld = ir_inst_new(IR_LEA);
        ld->dst = ir_val_vreg(dp, IR_TY_PTR);
        ld->src1 = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
        ld->src2 = ir_val_imm(IR_TY_I32, half);
        ir_func_append_inst(ctx->f, ld);
        dst_p = dp;
      }
      int vsrc = ir_func_new_vreg(ctx->f);
      ir_inst_t *lo = ir_inst_new(IR_LOAD);
      lo->dst = ir_val_vreg(vsrc, src_fp_ty);
      lo->src1 = ir_val_vreg(src_p, IR_TY_PTR);
      ir_func_append_inst(ctx->f, lo);
      int vcv = ir_func_new_vreg(ctx->f);
      ir_inst_t *cv = ir_inst_new(IR_F2F);
      cv->dst = ir_val_vreg(vcv, fp_ty);
      cv->src1 = ir_val_vreg(vsrc, src_fp_ty);
      ir_func_append_inst(ctx->f, cv);
      ir_inst_t *so = ir_inst_new(IR_STORE);
      so->src1 = ir_val_vreg(dst_p, IR_TY_PTR);
      so->src2 = ir_val_vreg(vcv, fp_ty);
      ir_func_append_inst(ctx->f, so);
    }
    return;
  }
  /* LVAR / DEREF / GVAR: src のアドレスを得て 2*half バイト memcpy */
  if (node->kind == ND_LVAR) {
    int src_ptr = address_of_lvar(ctx, ((node_lvar_t *)node)->offset);
    if (src_ptr < 0) return;
    ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
    cp->src1 = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
    cp->src2 = ir_val_vreg(src_ptr, IR_TY_PTR);
    cp->alloca_size = 2 * half;
    ir_func_append_inst(ctx->f, cp);
    return;
  }
  if (node->kind == ND_DEREF) {
    ir_val_t ptr = build_expr(ctx, node->lhs);
    if (ctx->failed) return;
    ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
    cp->src1 = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
    cp->src2 = ptr;
    cp->alloca_size = 2 * half;
    ir_func_append_inst(ctx->f, cp);
    return;
  }
  if (node->kind == ND_FUNCALL) {
    /* _Complex を返す関数呼び出し: build_node_funcall が d0/d1 を書き戻した
     * slot の PTR を返すので、そこから 2*half バイト memcpy する。 */
    ir_val_t ptr = build_expr(ctx, node);
    if (ctx->failed) return;
    ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
    cp->src1 = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
    cp->src2 = ptr;
    cp->alloca_size = 2 * half;
    ir_func_append_inst(ctx->f, cp);
    return;
  }
  if (node->kind == ND_GVAR) {
    node_gvar_t *gv = (node_gvar_t *)node;
    int v_addr = ir_func_new_vreg(ctx->f);
    ir_inst_t *sym = ir_inst_new(gv->is_thread_local ? IR_LOAD_TLV_ADDR : IR_LOAD_SYM);
    sym->dst = ir_val_vreg(v_addr, IR_TY_PTR);
    sym->sym = gv->name; sym->sym_len = gv->name_len;
    ir_func_append_inst(ctx->f, sym);
    ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
    cp->src1 = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
    cp->src2 = ir_val_vreg(v_addr, IR_TY_PTR);
    cp->alloca_size = 2 * half;
    ir_func_append_inst(ctx->f, cp);
    return;
  }
  /* 算術 +, -, *, / : 左右を temp slot に eval してから component-wise op。
   * 等価/不等価/単項などは複素値を生み出さないので対象外。 */
  if (node->kind == ND_ADD || node->kind == ND_SUB ||
      node->kind == ND_MUL || node->kind == ND_DIV) {
    /* 左右をそれぞれ 2*half バイトの temp slot に書く */
    int slot_size = 2 * half;
    int lhs_slot = ir_func_new_vreg(ctx->f);
    ir_inst_t *al_l = ir_inst_new(IR_ALLOCA);
    al_l->dst = ir_val_vreg(lhs_slot, IR_TY_PTR);
    al_l->alloca_size = slot_size; al_l->alloca_align = 8;
    ir_func_append_inst(ctx->f, al_l);
    build_complex_to(ctx, node->lhs, lhs_slot, fp_ty, half);
    if (ctx->failed) return;
    int rhs_slot = ir_func_new_vreg(ctx->f);
    ir_inst_t *al_r = ir_inst_new(IR_ALLOCA);
    al_r->dst = ir_val_vreg(rhs_slot, IR_TY_PTR);
    al_r->alloca_size = slot_size; al_r->alloca_align = 8;
    ir_func_append_inst(ctx->f, al_r);
    build_complex_to(ctx, node->rhs, rhs_slot, fp_ty, half);
    if (ctx->failed) return;
    /* load 4 components: lhs.re, lhs.im, rhs.re, rhs.im */
    int v_lr = ir_func_new_vreg(ctx->f);
    ir_inst_t *ld_lr = ir_inst_new(IR_LOAD);
    ld_lr->dst = ir_val_vreg(v_lr, fp_ty);
    ld_lr->src1 = ir_val_vreg(lhs_slot, IR_TY_PTR);
    ir_func_append_inst(ctx->f, ld_lr);
    int lhs_im_ptr = ir_func_new_vreg(ctx->f);
    ir_inst_t *lea_li = ir_inst_new(IR_LEA);
    lea_li->dst = ir_val_vreg(lhs_im_ptr, IR_TY_PTR);
    lea_li->src1 = ir_val_vreg(lhs_slot, IR_TY_PTR);
    lea_li->src2 = ir_val_imm(IR_TY_I32, half);
    ir_func_append_inst(ctx->f, lea_li);
    int v_li = ir_func_new_vreg(ctx->f);
    ir_inst_t *ld_li = ir_inst_new(IR_LOAD);
    ld_li->dst = ir_val_vreg(v_li, fp_ty);
    ld_li->src1 = ir_val_vreg(lhs_im_ptr, IR_TY_PTR);
    ir_func_append_inst(ctx->f, ld_li);
    int v_rr = ir_func_new_vreg(ctx->f);
    ir_inst_t *ld_rr = ir_inst_new(IR_LOAD);
    ld_rr->dst = ir_val_vreg(v_rr, fp_ty);
    ld_rr->src1 = ir_val_vreg(rhs_slot, IR_TY_PTR);
    ir_func_append_inst(ctx->f, ld_rr);
    int rhs_im_ptr = ir_func_new_vreg(ctx->f);
    ir_inst_t *lea_ri = ir_inst_new(IR_LEA);
    lea_ri->dst = ir_val_vreg(rhs_im_ptr, IR_TY_PTR);
    lea_ri->src1 = ir_val_vreg(rhs_slot, IR_TY_PTR);
    lea_ri->src2 = ir_val_imm(IR_TY_I32, half);
    ir_func_append_inst(ctx->f, lea_ri);
    int v_ri = ir_func_new_vreg(ctx->f);
    ir_inst_t *ld_ri = ir_inst_new(IR_LOAD);
    ld_ri->dst = ir_val_vreg(v_ri, fp_ty);
    ld_ri->src1 = ir_val_vreg(rhs_im_ptr, IR_TY_PTR);
    ir_func_append_inst(ctx->f, ld_ri);
    /* dst.im pointer */
    int dst_im_ptr = ir_func_new_vreg(ctx->f);
    ir_inst_t *lea_di = ir_inst_new(IR_LEA);
    lea_di->dst = ir_val_vreg(dst_im_ptr, IR_TY_PTR);
    lea_di->src1 = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
    lea_di->src2 = ir_val_imm(IR_TY_I32, half);
    ir_func_append_inst(ctx->f, lea_di);
    int dst_re_v, dst_im_v;
    if (node->kind == ND_ADD || node->kind == ND_SUB) {
      ir_op_t op = (node->kind == ND_ADD) ? IR_FADD : IR_FSUB;
      dst_re_v = emit_binop(ctx, op, ir_val_vreg(v_lr, fp_ty), ir_val_vreg(v_rr, fp_ty), fp_ty);
      dst_im_v = emit_binop(ctx, op, ir_val_vreg(v_li, fp_ty), ir_val_vreg(v_ri, fp_ty), fp_ty);
    } else if (node->kind == ND_MUL) {
      /* (lr+li*i)(rr+ri*i) = (lr*rr - li*ri) + (lr*ri + li*rr)*i */
      int m1 = emit_binop(ctx, IR_FMUL, ir_val_vreg(v_lr, fp_ty), ir_val_vreg(v_rr, fp_ty), fp_ty);
      int m2 = emit_binop(ctx, IR_FMUL, ir_val_vreg(v_li, fp_ty), ir_val_vreg(v_ri, fp_ty), fp_ty);
      dst_re_v = emit_binop(ctx, IR_FSUB, ir_val_vreg(m1, fp_ty), ir_val_vreg(m2, fp_ty), fp_ty);
      int m3 = emit_binop(ctx, IR_FMUL, ir_val_vreg(v_lr, fp_ty), ir_val_vreg(v_ri, fp_ty), fp_ty);
      int m4 = emit_binop(ctx, IR_FMUL, ir_val_vreg(v_li, fp_ty), ir_val_vreg(v_rr, fp_ty), fp_ty);
      dst_im_v = emit_binop(ctx, IR_FADD, ir_val_vreg(m3, fp_ty), ir_val_vreg(m4, fp_ty), fp_ty);
    } else { /* ND_DIV */
      /* denom = rr^2 + ri^2; re = (lr*rr + li*ri)/denom; im = (li*rr - lr*ri)/denom */
      int rr2 = emit_binop(ctx, IR_FMUL, ir_val_vreg(v_rr, fp_ty), ir_val_vreg(v_rr, fp_ty), fp_ty);
      int ri2 = emit_binop(ctx, IR_FMUL, ir_val_vreg(v_ri, fp_ty), ir_val_vreg(v_ri, fp_ty), fp_ty);
      int denom = emit_binop(ctx, IR_FADD, ir_val_vreg(rr2, fp_ty), ir_val_vreg(ri2, fp_ty), fp_ty);
      int m1 = emit_binop(ctx, IR_FMUL, ir_val_vreg(v_lr, fp_ty), ir_val_vreg(v_rr, fp_ty), fp_ty);
      int m2 = emit_binop(ctx, IR_FMUL, ir_val_vreg(v_li, fp_ty), ir_val_vreg(v_ri, fp_ty), fp_ty);
      int re_num = emit_binop(ctx, IR_FADD, ir_val_vreg(m1, fp_ty), ir_val_vreg(m2, fp_ty), fp_ty);
      dst_re_v = emit_binop(ctx, IR_FDIV, ir_val_vreg(re_num, fp_ty), ir_val_vreg(denom, fp_ty), fp_ty);
      int m3 = emit_binop(ctx, IR_FMUL, ir_val_vreg(v_li, fp_ty), ir_val_vreg(v_rr, fp_ty), fp_ty);
      int m4 = emit_binop(ctx, IR_FMUL, ir_val_vreg(v_lr, fp_ty), ir_val_vreg(v_ri, fp_ty), fp_ty);
      int im_num = emit_binop(ctx, IR_FSUB, ir_val_vreg(m3, fp_ty), ir_val_vreg(m4, fp_ty), fp_ty);
      dst_im_v = emit_binop(ctx, IR_FDIV, ir_val_vreg(im_num, fp_ty), ir_val_vreg(denom, fp_ty), fp_ty);
    }
    /* store dst.re, dst.im */
    ir_inst_t *st_re = ir_inst_new(IR_STORE);
    st_re->src1 = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
    st_re->src2 = ir_val_vreg(dst_re_v, fp_ty);
    ir_func_append_inst(ctx->f, st_re);
    ir_inst_t *st_im = ir_inst_new(IR_STORE);
    st_im->src1 = ir_val_vreg(dst_im_ptr, IR_TY_PTR);
    st_im->src2 = ir_val_vreg(dst_im_v, fp_ty);
    ir_func_append_inst(ctx->f, st_im);
    return;
  }
  fail(ctx, "unsupported complex expression kind");
}

/* 式を IR にビルドし、結果値 (vreg or immediate) を返す。 */
static ir_val_t build_expr(ir_build_ctx_t *ctx, node_t *node) {
  if (!node || ctx->failed) return ir_val_none();
  switch (node->kind) {
    case ND_STRING: return build_node_string(ctx, node);
    case ND_GVAR: return build_node_gvar(ctx, node);
    case ND_NUM: return build_node_num(ctx, node);
    case ND_LVAR: return build_node_lvar(ctx, node);
    case ND_ASSIGN: return build_node_assign(ctx, node);
    case ND_ADDR: return build_node_addr(ctx, node);
    case ND_DEREF: return build_node_deref(ctx, node);
    case ND_FUNCALL: return build_node_funcall(ctx, node);
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_BITAND: case ND_BITOR: case ND_BITXOR:
    case ND_SHL: case ND_SHR:
    case ND_LT: case ND_LE: case ND_EQ: case ND_NE:
      return build_node_binop(ctx, node);
    case ND_FP_TO_INT: return build_node_fp_to_int(ctx, node);
    case ND_INT_TO_FP: return build_node_int_to_fp(ctx, node);
    case ND_FNEG: return build_node_fneg(ctx, node);
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC: return build_node_inc_dec(ctx, node);
    case ND_VA_ARG_AREA: return build_node_va_arg_area(ctx, node);
    case ND_COMMA: return build_node_comma(ctx, node);
    case ND_PTR_CAST: return build_node_ptr_cast(ctx, node);
    case ND_CREAL:
    case ND_CIMAG: return build_node_creal_cimag(ctx, node);
    case ND_VLA_ALLOC: return build_node_vla_alloc(ctx, node);
    case ND_FUNCREF: return build_node_funcref(ctx, node);
    case ND_LOGAND:
    case ND_LOGOR: return build_node_logand_or(ctx, node);
    case ND_TERNARY: return build_node_ternary(ctx, node);
    default:
      fail(ctx, "unsupported expression node");
      return ir_val_none();
  }
}

/* -------- Phase B1: build_expr の lvalue 系 case ヘルパ -------- */

static ir_val_t build_node_string(ir_build_ctx_t *ctx, node_t *node) {
  /* 文字列リテラル: コンパイル時に登録された .LC<id> ラベルのアドレスを返す。 */
  node_string_t *s = (node_string_t *)node;
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_LOAD_STR);
  inst->dst = ir_val_vreg(v, IR_TY_PTR);
  inst->sym = s->string_label;
  inst->sym_len = s->string_label ? (int)strlen(s->string_label) : 0;
  ir_func_append_inst(ctx->f, inst);
  return inst->dst;
}

static ir_val_t build_node_gvar(ir_build_ctx_t *ctx, node_t *node) {
  /* グローバル変数 (スカラ): _<name>@PAGE/@PAGEOFF でアドレスを取って load。
   * 配列 / 構造体のグローバル変数は parser が ND_ADDR(ND_GVAR) で包む。
   * thread_local の場合は @TLVPPAGE 経由で動的にアドレス解決する。 */
  node_gvar_t *gv = (node_gvar_t *)node;
  int v_addr = ir_func_new_vreg(ctx->f);
  ir_inst_t *sym = ir_inst_new(gv->is_thread_local ? IR_LOAD_TLV_ADDR
                                                   : IR_LOAD_SYM);
  sym->dst = ir_val_vreg(v_addr, IR_TY_PTR);
  sym->sym = gv->name;
  sym->sym_len = gv->name_len;
  ir_func_append_inst(ctx->f, sym);
  /* load (型は node の fp_kind / type_size から判定) */
  ir_type_t load_ty = ir_type_from_node(node);
  if (load_ty == IR_TY_I32) {
    int sz = gv->mem.type_size > 0 ? gv->mem.type_size : 4;
    load_ty = (sz >= 8) ? IR_TY_PTR : (sz == 4 ? IR_TY_I32 : (sz == 2 ? IR_TY_I16 : IR_TY_I8));
  }
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *ld = ir_inst_new(IR_LOAD);
  ld->dst = ir_val_vreg(v, load_ty);
  ld->src1 = ir_val_vreg(v_addr, IR_TY_PTR);
  ld->is_unsigned_load = gv->mem.is_unsigned ? 1 : 0;
  ir_func_append_inst(ctx->f, ld);
  return ld->dst;
}

static ir_val_t build_node_num(ir_build_ctx_t *ctx, node_t *node) {
  node_num_t *n = (node_num_t *)node;
  /* float/double リテラル */
  if (n->base.fp_kind > 0) {
    ir_type_t ty = ir_type_from_node(&n->base);
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *inst = ir_inst_new(IR_LOAD_FP_IMM);
    inst->dst = ir_val_vreg(v, ty);
    inst->src1 = ir_val_fp_imm(ty, n->fval);
    ir_func_append_inst(ctx->f, inst);
    return inst->dst;
  }
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_LOAD_IMM);
  /* 32bit に収まらないリテラル (long / long long) は i64 で生成する。i32 のままだと
   * 後段で 64bit へ拡張する際 sxtw が下位 32bit を符号拡張して上位を捨ててしまう。
   * 境界は [INT32_MIN, UINT32_MAX]: この範囲は 32bit レジスタのビットパターンで
   * 符号付き/符号なしどちらにも解釈でき、既存の 32bit unsigned リテラル
   * (0xFFFFFFFFu 等) の扱いを変えない。 */
  ir_type_t ity = (n->int_is_long || n->val > 0xFFFFFFFFLL ||
                   n->val < (-2147483647LL - 1))
                    ? IR_TY_I64 : IR_TY_I32;
  inst->dst = ir_val_vreg(v, ity);
  inst->src1 = ir_val_imm(ity, n->val);
  ir_func_append_inst(ctx->f, inst);
  return inst->dst;
}

static ir_val_t build_node_lvar(ir_build_ctx_t *ctx, node_t *node) {
  node_lvar_t *lv = (node_lvar_t *)node;
  ir_type_t vty = lvar_value_type(lv);
  int ptr_vreg = address_of_lvar(ctx, lv->offset);
  if (ptr_vreg < 0) return ir_val_none();
  /* bitfield 読み出し:
   *   v_load   = *ptr  (storage unit、通常 i32)
   *   v_shr    = v_load >> bit_offset
   *   v_masked = v_shr & ((1<<bw)-1)
   *   signed: さらに sign extend ((v ^ sign_bit) - sign_bit) */
  int bw = lv->mem.bit_width;
  if (bw > 0) {
    return emit_bitfield_load(ctx, ir_val_vreg(ptr_vreg, IR_TY_PTR),
                              bw, lv->mem.bit_offset, lv->mem.bit_is_signed);
  }
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_LOAD);
  inst->dst = ir_val_vreg(v, vty);
  inst->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
  inst->is_unsigned_load = lv->mem.is_unsigned ? 1 : 0;
  ir_func_append_inst(ctx->f, inst);
  return inst->dst;
}

/* struct/union 値を「間接 (memcpy / ret_area / アドレス渡し)」で扱うべきサイズか。
 * 1/2/4/8 バイトは 1 つのレジスタにきれいに収まり値ロードできるが、3/5/6/7 や >8 は
 * 先頭メンバ幅しか復元できず壊れるため間接経路に回す。3/5/6/7 は scalar に存在しない
 * サイズなので struct/union/配列を一意に表す。 */
static int cg_size_needs_indirect_struct(int sz) {
  return sz > 0 && sz != 1 && sz != 2 && sz != 4 && sz != 8;
}

/* 式ツリーに副作用 (代入/インクリメント/関数呼出/VLA確保) が無いか。
 * sub-int 代入結果の再ロードでアドレス式を再評価してよいか判定するのに使う。 */
static int expr_side_effect_free(node_t *n) {
  if (!n) return 1;
  switch (n->kind) {
    case ND_ASSIGN:
    case ND_PRE_INC: case ND_PRE_DEC:
    case ND_POST_INC: case ND_POST_DEC:
    case ND_FUNCALL:
    case ND_VLA_ALLOC:
      return 0;
    default: break;
  }
  if (!expr_side_effect_free(n->lhs)) return 0;
  if (!expr_side_effect_free(n->rhs)) return 0;
  return 1;
}

static ir_val_t build_node_assign(ir_build_ctx_t *ctx, node_t *node) {
  if (!node->lhs) {
    fail(ctx, "assign without target");
    return ir_val_none();
  }
  if (node->is_complex) return build_assign_complex(ctx, node);
  /* struct 値代入: ND_ASSIGN.type_size が要素サイズを超える (>8) か、レジスタに
   * きれいに乗らないサイズ (3/5/6/7) なら memcpy 経路。 */
  if (cg_size_needs_indirect_struct(((node_mem_t *)node)->type_size))
    return build_assign_struct(ctx, node);
  switch (node->lhs->kind) {
    case ND_LVAR:  return build_assign_to_lvar(ctx, node);
    case ND_GVAR:  return build_assign_to_gvar(ctx, node);
    case ND_DEREF: return build_assign_to_deref(ctx, node);
    default:
      fail(ctx, "assign target is not LVAR or DEREF");
      return ir_val_none();
  }
}

/* bitfield 書き込みの共通実装:
 *   v_old   = *ptr
 *   v_clr   = v_old & ~(mask << bit_offset)
 *   v_rhs_m = rhs & mask
 *   v_shl   = v_rhs_m << bit_offset
 *   *ptr    = v_clr | v_shl
 * 代入式の値は rhs (masking 前) を返す。 */
/* bitfield のストレージユニットを 32bit で扱うか 64bit で扱うか。
 * bit_offset+bit_width が 32 を超えるフィールド (unsigned long a:40 等) は
 * 32bit ロード/ストアでは上位ビットを失うため 64bit で扱う。 */
static ir_type_t bitfield_unit_type(int bit_width, int bit_offset) {
  return (bit_offset + bit_width > 32) ? IR_TY_I64 : IR_TY_I32;
}

/* bitfield の mask 定数を生成する。64bit 単位 (long bitfield) では大きなマスク
 * (例 0xFFFFFF0000000000) を AND/OR の即値オペランドに直接渡すと codegen が
 * 下位 32bit しか materialize できず隣接フィールドを破壊するため、LOAD_IMM で
 * レジスタに展開してから渡す。32bit 単位は従来どおり即値で良い。 */
static ir_val_t bf_const(ir_build_ctx_t *ctx, ir_type_t ty, long long v) {
  if (ty != IR_TY_I64) return ir_val_imm(ty, v);
  int r = ir_func_new_vreg(ctx->f);
  ir_inst_t *li = ir_inst_new(IR_LOAD_IMM);
  li->dst = ir_val_vreg(r, ty);
  li->src1 = ir_val_imm(ty, v);
  ir_func_append_inst(ctx->f, li);
  return ir_val_vreg(r, ty);
}

static ir_val_t emit_bitfield_load(ir_build_ctx_t *ctx, ir_val_t ptr,
                                    int bit_width, int bit_offset, int is_signed) {
  ir_type_t ty = bitfield_unit_type(bit_width, bit_offset);
  int ty_bits = (ty == IR_TY_I64) ? 64 : 32;
  int v_load = ir_func_new_vreg(ctx->f);
  ir_inst_t *ld = ir_inst_new(IR_LOAD);
  ld->dst = ir_val_vreg(v_load, ty);
  ld->src1 = ptr;
  ir_func_append_inst(ctx->f, ld);
  ir_val_t cur = ir_val_vreg(v_load, ty);
  if (bit_offset > 0) {
    int v_shr = emit_binop(ctx, IR_SHR, cur, ir_val_imm(ty, bit_offset), ty);
    cur = ir_val_vreg(v_shr, ty);
  }
  long long mask = (bit_width >= 64) ? -1LL : ((1LL << bit_width) - 1);
  int v_masked = emit_binop(ctx, IR_AND, cur, bf_const(ctx, ty, mask), ty);
  cur = ir_val_vreg(v_masked, ty);
  if (is_signed && bit_width < ty_bits) {
    long long sign_bit = 1LL << (bit_width - 1);
    int v_xor = emit_binop(ctx, IR_XOR, cur, bf_const(ctx, ty, sign_bit), ty);
    int v_sub = emit_binop(ctx, IR_SUB,
                            ir_val_vreg(v_xor, ty), bf_const(ctx, ty, sign_bit), ty);
    cur = ir_val_vreg(v_sub, ty);
  }
  return cur;
}

static ir_val_t emit_bitfield_store(ir_build_ctx_t *ctx, ir_val_t ptr, ir_val_t rhs,
                                     int bit_width, int bit_offset) {
  ir_type_t ty = bitfield_unit_type(bit_width, bit_offset);
  long long mask = (bit_width >= 64) ? -1LL : ((1LL << bit_width) - 1);
  long long inv_mask = ~(mask << bit_offset);
  int v_old = ir_func_new_vreg(ctx->f);
  ir_inst_t *ld = ir_inst_new(IR_LOAD);
  ld->dst = ir_val_vreg(v_old, ty);
  ld->src1 = ptr;
  ir_func_append_inst(ctx->f, ld);
  int v_clr = emit_binop(ctx, IR_AND,
                          ir_val_vreg(v_old, ty),
                          bf_const(ctx, ty, inv_mask), ty);
  ir_val_t rhs_int = rhs;
  rhs_int.type = ty;
  int v_rhs_m = emit_binop(ctx, IR_AND, rhs_int,
                            bf_const(ctx, ty, mask), ty);
  ir_val_t cur = ir_val_vreg(v_rhs_m, ty);
  if (bit_offset > 0) {
    int v_shl = emit_binop(ctx, IR_SHL, cur,
                            ir_val_imm(ty, bit_offset), ty);
    cur = ir_val_vreg(v_shl, ty);
  }
  int v_new = emit_binop(ctx, IR_OR,
                          ir_val_vreg(v_clr, ty), cur, ty);
  ir_inst_t *st = ir_inst_new(IR_STORE);
  st->src1 = ptr;
  st->src2 = ir_val_vreg(v_new, ty);
  ir_func_append_inst(ctx->f, st);
  return rhs;
}

/* _Complex 代入: rhs を 2 成分として lhs slot に書き込む。
 * 算術 (a+b) も build_complex_to が再帰的に temp slot 経由で評価する。 */
static ir_val_t build_assign_complex(ir_build_ctx_t *ctx, node_t *node) {
  ir_type_t fp_ty = ir_type_from_node(node);
  int half = (fp_ty == IR_TY_F32) ? 4 : 8;
  int dst_ptr_vreg = -1;
  if (node->lhs->kind == ND_LVAR) {
    dst_ptr_vreg = address_of_lvar(ctx, ((node_lvar_t *)node->lhs)->offset);
  } else if (node->lhs->kind == ND_DEREF) {
    ir_val_t ptr = build_expr(ctx, node->lhs->lhs);
    if (ctx->failed) return ir_val_none();
    if (ptr.id >= 0) dst_ptr_vreg = ptr.id;
  } else if (node->lhs->kind == ND_GVAR) {
    node_gvar_t *gv = (node_gvar_t *)node->lhs;
    int v_addr = ir_func_new_vreg(ctx->f);
    ir_inst_t *sym = ir_inst_new(gv->is_thread_local ? IR_LOAD_TLV_ADDR : IR_LOAD_SYM);
    sym->dst = ir_val_vreg(v_addr, IR_TY_PTR);
    sym->sym = gv->name; sym->sym_len = gv->name_len;
    ir_func_append_inst(ctx->f, sym);
    dst_ptr_vreg = v_addr;
  } else {
    fail(ctx, "complex assign dst not LVAR/DEREF/GVAR");
    return ir_val_none();
  }
  if (dst_ptr_vreg < 0) return ir_val_none();
  build_complex_to(ctx, node->rhs, dst_ptr_vreg, fp_ty, half);
  return ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
}

/* struct (>8B) 値代入。dst/src アドレスを得て memcpy。
 * rhs が >8B struct 戻り値の関数呼出なら戻り値を dst へ直接書かせる。 */
static ir_val_t build_assign_struct(ir_build_ctx_t *ctx, node_t *node) {
  int assign_size = ((node_mem_t *)node)->type_size;
  int dst_ptr_vreg = -1;
  if (node->lhs->kind == ND_LVAR) {
    dst_ptr_vreg = address_of_lvar(ctx, ((node_lvar_t *)node->lhs)->offset);
  } else if (node->lhs->kind == ND_DEREF) {
    ir_val_t ptr = build_expr(ctx, node->lhs->lhs);
    if (ctx->failed) return ir_val_none();
    if (ptr.id >= 0) dst_ptr_vreg = ptr.id;
  } else if (node->lhs->kind == ND_GVAR) {
    dst_ptr_vreg = address_of_gvar(ctx, (node_gvar_t *)node->lhs);
  } else {
    fail(ctx, "struct assign dst not LVAR/DEREF/GVAR");
    return ir_val_none();
  }
  if (dst_ptr_vreg < 0) return ir_val_none();
  /* rhs が間接返し (>8B / 3/5/6/7B) struct 戻り値の関数呼び出しなら、戻り値を dst へ
   * 直接書かせる。 */
  if (node->rhs && node->rhs->kind == ND_FUNCALL &&
      cg_size_needs_indirect_struct(node->rhs->ret_struct_size)) {
    node_func_t *callee = (node_func_t *)node->rhs;
    if (callee->callee || callee->is_variadic || callee->nargs > 8) {
      fail(ctx, "indirect/variadic/many-arg struct-return call");
      return ir_val_none();
    }
    ir_val_t *cargs = NULL;
    if (callee->nargs > 0) {
      cargs = calloc((size_t)callee->nargs, sizeof(ir_val_t));
      for (int i = 0; i < callee->nargs; i++) {
        node_t *arg = callee->args[i];
        /* compound literal `(struct V){...}` (ND_COMMA(init, temp lvar)): init を
         * 評価してから temp lvar を struct ソースとして扱う (主 funcall 経路と同じ)。 */
        if (arg && arg->kind == ND_COMMA && arg->rhs && arg->rhs->kind == ND_LVAR) {
          node_lvar_t *rlv = (node_lvar_t *)arg->rhs;
          lvar_t *owner_cl = find_owning_lvar(ctx, rlv->offset);
          int cl_sz = owner_cl ? owner_cl->size : rlv->mem.type_size;
          if (!rlv->mem.is_pointer &&
              (cl_sz > 8 || cg_size_needs_indirect_struct(cl_sz))) {
            (void)build_expr(ctx, arg->lhs);
            if (ctx->failed) return ir_val_none();
            arg = arg->rhs;
          }
        }
        int arg_full_size = 0;
        if (arg && arg->kind == ND_LVAR) {
          lvar_t *owner = find_owning_lvar(ctx, ((node_lvar_t *)arg)->offset);
          if (owner) arg_full_size = owner->size;
          if (arg_full_size == 0) arg_full_size = ((node_lvar_t *)arg)->mem.type_size;
        }
        if (cg_size_needs_indirect_struct(arg_full_size) &&
            arg && arg->kind == ND_LVAR) {
          int src_ptr = address_of_lvar(ctx, ((node_lvar_t *)arg)->offset);
          if (src_ptr < 0) return ir_val_none();
          int tmp = ir_func_new_vreg(ctx->f);
          ir_inst_t *ia = ir_inst_new(IR_ALLOCA);
          ia->dst = ir_val_vreg(tmp, IR_TY_PTR);
          ia->alloca_size = arg_full_size;
          ia->alloca_align = 8;
          ir_func_append_inst(ctx->f, ia);
          ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
          cp->src1 = ir_val_vreg(tmp, IR_TY_PTR);
          cp->src2 = ir_val_vreg(src_ptr, IR_TY_PTR);
          cp->alloca_size = arg_full_size;
          ir_func_append_inst(ctx->f, cp);
          cargs[i] = ir_val_vreg(tmp, IR_TY_PTR);
        } else {
          cargs[i] = build_expr(ctx, arg);
          if (ctx->failed) return ir_val_none();
        }
      }
    }
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *call = ir_inst_new(IR_CALL);
    call->dst = ir_val_vreg(v, IR_TY_I32);
    call->sym = callee->funcname;
    call->sym_len = callee->funcname_len;
    call->args = cargs;
    call->nargs = callee->nargs;
    call->ret_struct_size = node->rhs->ret_struct_size;
    call->ret_struct_area = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
    ir_func_append_inst(ctx->f, call);
    return ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
  }
  /* src のアドレスを得る (通常の struct から struct コピー) */
  node_t *src = node->rhs;
  /* struct compound literal `(struct S){...}` は ND_COMMA(init, temp struct lvalue)。
   * init (メンバストア) を評価してから temp lvalue を struct ソースとして扱う。
   * これがないと src 判定が LVAR/DEREF/GVAR に当たらず compile-fail していた
   * (`s = (struct S){9,8,7}` で >8B のとき)。 */
  if (src && src->kind == ND_COMMA && src->rhs) {
    (void)build_expr(ctx, src->lhs);
    if (ctx->failed) return ir_val_none();
    src = src->rhs;
  }
  int src_ptr_vreg = -1;
  if (src && src->kind == ND_LVAR) {
    src_ptr_vreg = address_of_lvar(ctx, ((node_lvar_t *)src)->offset);
  } else if (src && src->kind == ND_DEREF) {
    ir_val_t ptr = build_expr(ctx, src->lhs);
    if (ctx->failed) return ir_val_none();
    if (ptr.id >= 0) src_ptr_vreg = ptr.id;
  } else if (src && src->kind == ND_GVAR) {
    src_ptr_vreg = address_of_gvar(ctx, (node_gvar_t *)src);
  } else {
    fail(ctx, "struct assign src not LVAR/DEREF/GVAR");
    return ir_val_none();
  }
  if (src_ptr_vreg < 0) return ir_val_none();
  ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
  cp->src1 = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
  cp->src2 = ir_val_vreg(src_ptr_vreg, IR_TY_PTR);
  cp->alloca_size = assign_size;
  ir_func_append_inst(ctx->f, cp);
  return ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
}

static ir_val_t build_assign_to_lvar(ir_build_ctx_t *ctx, node_t *node) {
  node_lvar_t *lv = (node_lvar_t *)node->lhs;
  ir_type_t vty = lvar_value_type(lv);
  int ptr_vreg = address_of_lvar(ctx, lv->offset);
  if (ptr_vreg < 0) return ir_val_none();
  ir_val_t rhs = build_expr(ctx, node->rhs);
  if (ctx->failed) return ir_val_none();
  /* float ↔ double の暗黙変換 */
  if (is_fp_type(vty) && is_fp_type(rhs.type) && vty != rhs.type) {
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *cv = ir_inst_new(IR_F2F);
    cv->dst = ir_val_vreg(v, vty);
    cv->src1 = rhs;
    ir_func_append_inst(ctx->f, cv);
    rhs = ir_val_vreg(v, vty);
  }
  int bw = lv->mem.bit_width;
  if (bw > 0) {
    return emit_bitfield_store(ctx, ir_val_vreg(ptr_vreg, IR_TY_PTR), rhs,
                                bw, lv->mem.bit_offset);
  }
  /* 通常のスカラ代入 (int→float のような昇格 / float→int の縮小も対応) */
  rhs = coerce_to_type(ctx, rhs, vty);
  ir_inst_t *st = ir_inst_new(IR_STORE);
  st->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
  st->src2 = rhs;
  ir_func_append_inst(ctx->f, st);
  /* 代入式の値は「lvalue の型に変換した値」(C11 6.5.16p3)。sub-int (char/short)
   * lvalue では rhs をそのまま返すと幅・符号が反映されず、`b = a = 300` (a が
   * unsigned char) で外側 b が 300 のままになる (IMM は coerce で切り詰められず、
   * 値拡張も常に SEXT)。格納後に lvalue を再ロードして正しい幅・符号の値を返す
   * (load 経路は ldrb/ldrsb を符号性どおり選ぶ。結果未使用なら DCE が除去)。 */
  if (!is_fp_type(vty) && ir_type_size(vty) < 4) {
    ir_val_t reloaded = build_expr(ctx, node->lhs);
    if (!ctx->failed && reloaded.id != IR_VAL_NONE) return reloaded;
  }
  return rhs;
}

static ir_val_t build_assign_to_gvar(ir_build_ctx_t *ctx, node_t *node) {
  node_gvar_t *gv = (node_gvar_t *)node->lhs;
  ir_type_t vty = ir_type_from_node(node->lhs);
  if (vty == IR_TY_I32) {
    int sz = gv->mem.type_size > 0 ? gv->mem.type_size : 4;
    vty = (sz >= 8) ? IR_TY_PTR : (sz == 4 ? IR_TY_I32 : (sz == 2 ? IR_TY_I16 : IR_TY_I8));
  }
  int v_addr = ir_func_new_vreg(ctx->f);
  ir_inst_t *sym = ir_inst_new(gv->is_thread_local ? IR_LOAD_TLV_ADDR
                                                   : IR_LOAD_SYM);
  sym->dst = ir_val_vreg(v_addr, IR_TY_PTR);
  sym->sym = gv->name;
  sym->sym_len = gv->name_len;
  ir_func_append_inst(ctx->f, sym);
  ir_val_t rhs = build_expr(ctx, node->rhs);
  if (ctx->failed) return ir_val_none();
  rhs = coerce_to_type(ctx, rhs, vty);
  ir_inst_t *st = ir_inst_new(IR_STORE);
  st->src1 = ir_val_vreg(v_addr, IR_TY_PTR);
  st->src2 = rhs;
  ir_func_append_inst(ctx->f, st);
  /* sub-int lvalue の代入式の値は格納後の値 (幅・符号変換済み)。再ロードで正しい
   * 値を返す (build_assign_to_lvar と同じ。`b = g = 300` の b を 44 にする)。 */
  if (!is_fp_type(vty) && ir_type_size(vty) < 4) {
    ir_val_t reloaded = build_expr(ctx, node->lhs);
    if (!ctx->failed && reloaded.id != IR_VAL_NONE) return reloaded;
  }
  return rhs;
}

static ir_val_t build_assign_to_deref(ir_build_ctx_t *ctx, node_t *node) {
  /* *p = rhs。p が struct メンバアクセス由来なら bit_width が乗る。 */
  node_mem_t *mm = (node_mem_t *)node->lhs;
  int bw = mm->bit_width;
  ir_val_t ptr = build_expr(ctx, node->lhs->lhs);
  if (ctx->failed) return ir_val_none();
  ir_val_t rhs = build_expr(ctx, node->rhs);
  if (ctx->failed) return ir_val_none();
  if (bw > 0) {
    return emit_bitfield_store(ctx, ptr, rhs, bw, mm->bit_offset);
  }
  /* DEREF の type_size と fp_kind から書き込み幅を決める。 */
  ir_type_t vty = ir_type_from_node(node->lhs);
  if (vty == IR_TY_I32) {
    if (mm->type_size >= 8) vty = IR_TY_PTR;
    else if (mm->type_size == 2) vty = IR_TY_I16;
    else if (mm->type_size == 1) vty = IR_TY_I8;
  }
  rhs = coerce_to_type(ctx, rhs, vty);
  ir_inst_t *st = ir_inst_new(IR_STORE);
  st->src1 = ptr;
  st->src2 = rhs;
  ir_func_append_inst(ctx->f, st);
  /* sub-int lvalue: 代入式の値は格納後の幅・符号変換済みの値。アドレス式に副作用が
   * なければ lvalue を再ロードして正しい値を返す (`b = s.x = 300` の b を 44 に)。
   * `*p++ = v` 等アドレスに副作用があるときは二重評価を避け rhs を返す。 */
  if (!is_fp_type(vty) && ir_type_size(vty) < 4 &&
      node->lhs->lhs && expr_side_effect_free(node->lhs->lhs)) {
    ir_val_t reloaded = build_expr(ctx, node->lhs);
    if (!ctx->failed && reloaded.id != IR_VAL_NONE) return reloaded;
  }
  return rhs;
}

static ir_val_t build_node_addr(ir_build_ctx_t *ctx, node_t *node) {
  /* &*x = x */
  if (node->lhs && node->lhs->kind == ND_DEREF) {
    return build_expr(ctx, node->lhs->lhs);
  }
  /* &gvar: グローバル変数のアドレス (= LOAD_SYM のみ、load しない) */
  if (node->lhs && node->lhs->kind == ND_GVAR) {
    node_gvar_t *gv = (node_gvar_t *)node->lhs;
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *sym = ir_inst_new(gv->is_thread_local ? IR_LOAD_TLV_ADDR
                                                     : IR_LOAD_SYM);
    sym->dst = ir_val_vreg(v, IR_TY_PTR);
    sym->sym = gv->name;
    sym->sym_len = gv->name_len;
    ir_func_append_inst(ctx->f, sym);
    return sym->dst;
  }
  if (!node->lhs || node->lhs->kind != ND_LVAR) {
    fail(ctx, "& of non-lvar (Phase 4b unsupported)");
    return ir_val_none();
  }
  node_lvar_t *lv = (node_lvar_t *)node->lhs;
  int ptr_vreg = address_of_lvar(ctx, lv->offset);
  if (ptr_vreg < 0) return ir_val_none();
  return ir_val_vreg(ptr_vreg, IR_TY_PTR);
}

static ir_val_t build_node_deref(ir_build_ctx_t *ctx, node_t *node) {
  ir_val_t ptr = build_expr(ctx, node->lhs);
  if (ctx->failed) return ir_val_none();
  /* bitfield 読み出し: bit_width > 0 のとき struct メンバが bitfield。 */
  node_mem_t *mm = (node_mem_t *)node;
  int bw = mm->bit_width;
  if (bw > 0) {
    return emit_bitfield_load(ctx, ptr, bw, mm->bit_offset, mm->bit_is_signed);
  }
  /* 配列が式中でポインタへ崩壊するケース: load せず address (ptr) を返す。
   *  - struct の配列メンバ `s.v` (is_pointer=1, deref_size>0, type_size>8)
   *  - 多次元配列の途中次元 `m[i]` / `*m` (例 `int m[3][4]` の行): subscript/
   *    unary deref が is_pointer を立てないが、type_size>8 (= 配列全体サイズ)
   *    かつ deref_size>0 (要素ストライドを持つ=配列実体) なら同様に崩壊させる。
   * これにより `int *q = m[0];` / `*(*(m+1)+2)` / `**m` が正しく動く。struct 値は
   * deref_size=0 なのでここに該当せず、従来どおり値ロードされる。
   *  - 行全体が 8 バイト以下の小さい配列の行 (`int m[2][2]` の `m[i]` = 8B、
   *    `char c[2][3]` の `c[i]` = 3B 等) は type_size>8 から漏れ、行をスカラとして
   *    load し不正アドレスを deref していた。プレーン配列の行は is_pointer=0 かつ
   *    type_size (= 行全体) > deref_size (= 要素サイズ) で見分けて崩壊させる
   *    (loaded ポインタ値 `*pp` は is_pointer=1 なので除外され従来どおり load)。 */
  if (mm->deref_size > 0 &&
      (mm->type_size > 8 ||
       (!mm->is_pointer && mm->type_size > mm->deref_size))) {
    return ptr;
  }
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_LOAD);
  /* deref 後の型: fp_kind を最優先。それ以外は type_size で判定
   * (関数ポインタ配列等で 8B 要素を i32 と誤判定しないように)。 */
  ir_type_t load_ty = ir_type_from_node(node);
  if (load_ty == IR_TY_I32) {
    if (mm->type_size >= 8) load_ty = IR_TY_PTR;
    else if (mm->type_size == 2) load_ty = IR_TY_I16;
    else if (mm->type_size == 1) load_ty = IR_TY_I8;
  }
  inst->dst = ir_val_vreg(v, load_ty);
  inst->src1 = ptr;
  inst->is_unsigned_load = mm->is_unsigned ? 1 : 0;
  ir_func_append_inst(ctx->f, inst);
  return inst->dst;
}

static ir_val_t build_node_funcref(ir_build_ctx_t *ctx, node_t *node) {
  /* 関数シンボル参照 (関数ポインタ値)。`_<funcname>` のアドレスを vreg に。 */
  node_funcref_t *fr = (node_funcref_t *)node;
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *sym = ir_inst_new(IR_LOAD_SYM);
  sym->dst = ir_val_vreg(v, IR_TY_PTR);
  sym->sym = fr->funcname;
  sym->sym_len = fr->funcname_len;
  ir_func_append_inst(ctx->f, sym);
  return ir_val_vreg(v, IR_TY_PTR);
}

/* -------- Phase B1: build_expr の算術/比較系 case ヘルパ -------- */

/* _Complex EQ/NE 比較: 両辺を 2 成分 (実部/虚部) として temp slot に書き出してから
 * (re == re) && (im == im) を計算する。ND_NE は最後に XOR で反転。
 * ND_LT/LE は _Complex に対して未定義なので扱わない (build_node_binop で
 * EQ/NE のときだけ呼ばれる)。 */
static ir_val_t build_complex_cmp(ir_build_ctx_t *ctx, node_t *node) {
  unsigned fpk = (node->lhs ? node->lhs->fp_kind : 0);
  if (node->rhs && node->rhs->fp_kind > fpk) fpk = node->rhs->fp_kind;
  ir_type_t fp_ty = (fpk == TK_FLOAT_KIND_FLOAT) ? IR_TY_F32 : IR_TY_F64;
  int half = (fp_ty == IR_TY_F32) ? 4 : 8;
  int slot_size = 2 * half;
  int l_slot = ir_func_new_vreg(ctx->f);
  ir_inst_t *al_l = ir_inst_new(IR_ALLOCA);
  al_l->dst = ir_val_vreg(l_slot, IR_TY_PTR);
  al_l->alloca_size = slot_size; al_l->alloca_align = 8;
  ir_func_append_inst(ctx->f, al_l);
  build_complex_to(ctx, node->lhs, l_slot, fp_ty, half);
  if (ctx->failed) return ir_val_none();
  int r_slot = ir_func_new_vreg(ctx->f);
  ir_inst_t *al_r = ir_inst_new(IR_ALLOCA);
  al_r->dst = ir_val_vreg(r_slot, IR_TY_PTR);
  al_r->alloca_size = slot_size; al_r->alloca_align = 8;
  ir_func_append_inst(ctx->f, al_r);
  build_complex_to(ctx, node->rhs, r_slot, fp_ty, half);
  if (ctx->failed) return ir_val_none();
  /* load 4 components and compare */
  int v_lr = ir_func_new_vreg(ctx->f);
  ir_inst_t *ld1 = ir_inst_new(IR_LOAD);
  ld1->dst = ir_val_vreg(v_lr, fp_ty); ld1->src1 = ir_val_vreg(l_slot, IR_TY_PTR);
  ir_func_append_inst(ctx->f, ld1);
  int v_rr = ir_func_new_vreg(ctx->f);
  ir_inst_t *ld2 = ir_inst_new(IR_LOAD);
  ld2->dst = ir_val_vreg(v_rr, fp_ty); ld2->src1 = ir_val_vreg(r_slot, IR_TY_PTR);
  ir_func_append_inst(ctx->f, ld2);
  int l_im_ptr = ir_func_new_vreg(ctx->f);
  ir_inst_t *lea_li = ir_inst_new(IR_LEA);
  lea_li->dst = ir_val_vreg(l_im_ptr, IR_TY_PTR);
  lea_li->src1 = ir_val_vreg(l_slot, IR_TY_PTR);
  lea_li->src2 = ir_val_imm(IR_TY_I32, half);
  ir_func_append_inst(ctx->f, lea_li);
  int v_li = ir_func_new_vreg(ctx->f);
  ir_inst_t *ld3 = ir_inst_new(IR_LOAD);
  ld3->dst = ir_val_vreg(v_li, fp_ty); ld3->src1 = ir_val_vreg(l_im_ptr, IR_TY_PTR);
  ir_func_append_inst(ctx->f, ld3);
  int r_im_ptr = ir_func_new_vreg(ctx->f);
  ir_inst_t *lea_ri = ir_inst_new(IR_LEA);
  lea_ri->dst = ir_val_vreg(r_im_ptr, IR_TY_PTR);
  lea_ri->src1 = ir_val_vreg(r_slot, IR_TY_PTR);
  lea_ri->src2 = ir_val_imm(IR_TY_I32, half);
  ir_func_append_inst(ctx->f, lea_ri);
  int v_ri = ir_func_new_vreg(ctx->f);
  ir_inst_t *ld4 = ir_inst_new(IR_LOAD);
  ld4->dst = ir_val_vreg(v_ri, fp_ty); ld4->src1 = ir_val_vreg(r_im_ptr, IR_TY_PTR);
  ir_func_append_inst(ctx->f, ld4);
  int cmp_re = emit_binop(ctx, IR_FEQ,
                           ir_val_vreg(v_lr, fp_ty), ir_val_vreg(v_rr, fp_ty), IR_TY_I32);
  int cmp_im = emit_binop(ctx, IR_FEQ,
                           ir_val_vreg(v_li, fp_ty), ir_val_vreg(v_ri, fp_ty), IR_TY_I32);
  int eq = emit_binop(ctx, IR_AND,
                       ir_val_vreg(cmp_re, IR_TY_I32),
                       ir_val_vreg(cmp_im, IR_TY_I32), IR_TY_I32);
  if (node->kind == ND_NE) {
    /* != is logical NOT of EQ: XOR with 1 */
    int neq = emit_binop(ctx, IR_XOR,
                          ir_val_vreg(eq, IR_TY_I32),
                          ir_val_imm(IR_TY_I32, 1), IR_TY_I32);
    return ir_val_vreg(neq, IR_TY_I32);
  }
  return ir_val_vreg(eq, IR_TY_I32);
}

static ir_val_t build_node_binop(ir_build_ctx_t *ctx, node_t *node) {
  /* _Complex EQ/NE 比較は専用経路 (build_complex_cmp) に分岐。 */
  if ((node->kind == ND_EQ || node->kind == ND_NE) &&
      ((node->lhs && node->lhs->is_complex) || (node->rhs && node->rhs->is_complex))) {
    return build_complex_cmp(ctx, node);
  }
  ir_val_t l = build_expr(ctx, node->lhs);
  if (ctx->failed) return ir_val_none();
  ir_val_t r = build_expr(ctx, node->rhs);
  if (ctx->failed) return ir_val_none();
  int is_fp = is_fp_type(l.type) || is_fp_type(r.type);
  /* unsigned 演算は SHR→LSR、DIV→UDIV、MOD→UMOD、LT/LE→ULT/ULE に振り分ける。
   * 結果型の is_unsigned (parser が伝播) と、片側が unsigned LVAR/GVAR/...
   * の場合の両方を考慮する。 */
  int unsig = node->is_unsigned ||
              (node->lhs && node->lhs->is_unsigned) ||
              (node->rhs && node->rhs->is_unsigned);
  /* 比較 (LT/LE) と除算/剰余 (DIV/MOD) の符号は C11 6.3.1.8 の通常算術変換に
   * 従って別途決める。上の unsig (どちらかが unsigned なら unsigned) は rank を
   * 無視しており、`long < unsigned int` や `long / unsigned int`、
   * `unsigned char < int` を誤って符号なし演算にしてしまう。
   * 整数昇格: int 未満 (i8/i16) は符号付き int になるので幅<4 は符号付き 4byte 扱い。
   * 両辺の符号が異なる場合、符号なし側の幅が符号付き側の幅以上のときのみ符号なし。
   * 広い符号付き型は狭い符号なし型の全値を表現できるため符号付き演算になる。 */
  int lsz = ir_type_size(l.type), rsz = ir_type_size(r.type);
  int lu = (lsz >= 4) && psx_node_is_unsigned(node->lhs);
  int ru = (rsz >= 4) && psx_node_is_unsigned(node->rhs);
  int lw = lsz < 4 ? 4 : lsz, rw = rsz < 4 ? 4 : rsz;
  int uac_unsig;  /* 通常算術変換 (usual arithmetic conversion) の符号 */
  if (lu == ru) {
    uac_unsig = lu;
  } else {
    int uw = lu ? lw : rw;  /* 符号なし側の幅 */
    int sw = lu ? rw : lw;  /* 符号付き側の幅 */
    uac_unsig = uw >= sw;
  }
  ir_op_t op = IR_ADD;
  switch (node->kind) {
    case ND_ADD: op = is_fp ? IR_FADD : IR_ADD; break;
    case ND_SUB: op = is_fp ? IR_FSUB : IR_SUB; break;
    case ND_MUL: op = is_fp ? IR_FMUL : IR_MUL; break;
    case ND_DIV: op = is_fp ? IR_FDIV : (uac_unsig ? IR_UDIV : IR_DIV); break;
    case ND_MOD: op = uac_unsig ? IR_UMOD : IR_MOD; break;  /* float mod は未対応 */
    case ND_BITAND: op = IR_AND; break;
    case ND_BITOR:  op = IR_OR;  break;
    case ND_BITXOR: op = IR_XOR; break;
    case ND_SHL:    op = IR_SHL; break;
    case ND_SHR:    op = unsig ? IR_LSR : IR_SHR; break;
    case ND_LT:  op = is_fp ? IR_FLT : (uac_unsig ? IR_ULT : IR_LT); break;
    case ND_LE:  op = is_fp ? IR_FLE : (uac_unsig ? IR_ULE : IR_LE); break;
    case ND_EQ:  op = is_fp ? IR_FEQ : IR_EQ; break;
    case ND_NE:  op = is_fp ? IR_FNE : IR_NE; break;
    default: break;
  }
  ir_type_t result_ty;
  if (node->kind == ND_LT || node->kind == ND_LE ||
      node->kind == ND_EQ || node->kind == ND_NE) {
    result_ty = IR_TY_I32;
  } else if (is_fp) {
    result_ty = (l.type == IR_TY_F64 || r.type == IR_TY_F64) ? IR_TY_F64 : IR_TY_F32;
  } else {
    /* 整数演算: いずれかのオペランドが 64bit (long / long long / pointer) なら
     * 結果も 64bit。さもないと i32。i32 のままだと後段で 64bit へ拡張する際に
     * sxtw が下位 32bit へ切り詰めてしまう (例 `(c)? a+a : 0`、a が long)。 */
    int wide = (ir_type_size(l.type) >= 8) || (ir_type_size(r.type) >= 8);
    result_ty = wide ? IR_TY_I64 : IR_TY_I32;
  }
  /* 浮動小数点演算: 整数 src を float に昇格 */
  if (is_fp) {
    ir_type_t fp_ty = (node->kind == ND_LT || node->kind == ND_LE ||
                       node->kind == ND_EQ || node->kind == ND_NE)
                        ? ((l.type == IR_TY_F64 || r.type == IR_TY_F64) ? IR_TY_F64 : IR_TY_F32)
                        : result_ty;
    if (!is_fp_type(l.type)) {
      int v = ir_func_new_vreg(ctx->f);
      ir_inst_t *cv = ir_inst_new(IR_I2F);
      cv->dst = ir_val_vreg(v, fp_ty);
      cv->src1 = l;
      ir_func_append_inst(ctx->f, cv);
      l = ir_val_vreg(v, fp_ty);
    } else if (l.type != fp_ty) {
      int v = ir_func_new_vreg(ctx->f);
      ir_inst_t *cv = ir_inst_new(IR_F2F);
      cv->dst = ir_val_vreg(v, fp_ty);
      cv->src1 = l;
      ir_func_append_inst(ctx->f, cv);
      l = ir_val_vreg(v, fp_ty);
    }
    if (!is_fp_type(r.type)) {
      int v = ir_func_new_vreg(ctx->f);
      ir_inst_t *cv = ir_inst_new(IR_I2F);
      cv->dst = ir_val_vreg(v, fp_ty);
      cv->src1 = r;
      ir_func_append_inst(ctx->f, cv);
      r = ir_val_vreg(v, fp_ty);
    } else if (r.type != fp_ty) {
      int v = ir_func_new_vreg(ctx->f);
      ir_inst_t *cv = ir_inst_new(IR_F2F);
      cv->dst = ir_val_vreg(v, fp_ty);
      cv->src1 = r;
      ir_func_append_inst(ctx->f, cv);
      r = ir_val_vreg(v, fp_ty);
    }
  }
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(op);
  inst->dst = ir_val_vreg(v, result_ty);
  inst->src1 = l;
  inst->src2 = r;
  ir_func_append_inst(ctx->f, inst);
  /* unsigned int (32bit) の加減乗・左シフトは上位ビットへ桁上がり/桁あふれし得る。
   * codegen は 64bit レジスタで演算するため、結果を 0xFFFFFFFF でマスクして 32bit へ
   * 折り返す (C11 6.2.5p9: 符号なしは 2^32 で wrap)。これをしないと `(x+1)==0`
   * (x=0xFFFFFFFF) のように格納/キャストを経ない直接使用で値が壊れる。
   * 符号付きはオーバーフローが UB なので対象外 (符号拡張のまま)。 */
  if (result_ty == IR_TY_I32 && uac_unsig &&
      (node->kind == ND_ADD || node->kind == ND_SUB ||
       node->kind == ND_MUL || node->kind == ND_SHL)) {
    int vm = ir_func_new_vreg(ctx->f);
    ir_inst_t *mask = ir_inst_new(IR_AND);
    mask->dst = ir_val_vreg(vm, IR_TY_I32);
    mask->src1 = inst->dst;
    mask->src2 = ir_val_imm(IR_TY_I32, 0xFFFFFFFF);
    ir_func_append_inst(ctx->f, mask);
    return mask->dst;
  }
  return inst->dst;
}

/* -------- Phase B1: build_expr の制御系 case ヘルパ -------- */

/* `__ag_atomic_*(...)` 組込みを IR_ATOMIC に下ろす (stdatomic.h が使う)。
 * 幅と pointee の符号は第 1 引数 (obj ポインタ) の deref_size / pointee_is_unsigned
 * から取る。全操作 seq_cst 強度。 */
static ir_val_t build_node_atomic_intrinsic(ir_build_ctx_t *ctx, node_func_t *fn) {
  const char *suf = fn->funcname + 12;          /* "__ag_atomic_" の後ろ */
  int sl = fn->funcname_len - 12;

  if (sl == 5 && memcmp(suf, "fence", 5) == 0) {
    ir_inst_t *a = ir_inst_new(IR_ATOMIC);
    a->atomic_kind = IR_ATOMIC_FENCE;
    ir_func_append_inst(ctx->f, a);
    return ir_val_imm(IR_TY_I32, 0);
  }
  if (fn->nargs < 1) { fail(ctx, "__ag_atomic intrinsic missing pointer arg"); return ir_val_none(); }

  node_t *parg = fn->args[0];
  int width = ((node_mem_t *)parg)->deref_size;
  if (width != 1 && width != 2 && width != 4 && width != 8) width = 4;
  /* 符号: `&var` (ND_ADDR) なら被参照変数の値の符号、ポインタ式ならその
   * pointee_is_unsigned。1/2 バイト atomic の load 旧値の符号拡張に使う。 */
  int unsigned_p = ((node_mem_t *)parg)->pointee_is_unsigned ? 1 : 0;
  if (parg->kind == ND_ADDR && parg->lhs) {
    unsigned_p = ((node_mem_t *)parg->lhs)->is_unsigned ? 1 : 0;
  }
  ir_type_t rty = (width == 8) ? IR_TY_I64 : IR_TY_I32;
  ir_val_t ptr = build_expr(ctx, parg);
  if (ctx->failed) return ir_val_none();

  if (sl == 4 && memcmp(suf, "load", 4) == 0) {
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *a = ir_inst_new(IR_ATOMIC);
    a->atomic_kind = IR_ATOMIC_LOAD; a->atomic_width = (unsigned char)width;
    a->is_unsigned_load = (unsigned char)unsigned_p;
    a->src1 = ptr; a->dst = ir_val_vreg(v, rty);
    ir_func_append_inst(ctx->f, a);
    return a->dst;
  }
  if (sl == 5 && memcmp(suf, "store", 5) == 0) {
    ir_val_t val = build_expr(ctx, fn->args[1]);
    if (ctx->failed) return ir_val_none();
    ir_inst_t *a = ir_inst_new(IR_ATOMIC);
    a->atomic_kind = IR_ATOMIC_STORE; a->atomic_width = (unsigned char)width;
    a->src1 = ptr; a->src2 = val;
    ir_func_append_inst(ctx->f, a);
    return ir_val_imm(IR_TY_I32, 0);
  }
  if (sl == 3 && memcmp(suf, "cas", 3) == 0) {
    ir_val_t exp = build_expr(ctx, fn->args[1]);   /* expected の PTR */
    if (ctx->failed) return ir_val_none();
    ir_val_t des = build_expr(ctx, fn->args[2]);   /* desired 値 */
    if (ctx->failed) return ir_val_none();
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *a = ir_inst_new(IR_ATOMIC);
    a->atomic_kind = IR_ATOMIC_CAS; a->atomic_width = (unsigned char)width;
    a->src1 = ptr; a->src2 = exp; a->src3 = des; a->dst = ir_val_vreg(v, IR_TY_I32);
    ir_func_append_inst(ctx->f, a);
    return a->dst;
  }
  int rmwop = -1;
  if (sl == 8 && memcmp(suf, "exchange", 8) == 0) rmwop = IR_ARMW_XCHG;
  else if (sl == 9 && memcmp(suf, "fetch_add", 9) == 0) rmwop = IR_ARMW_ADD;
  else if (sl == 9 && memcmp(suf, "fetch_sub", 9) == 0) rmwop = IR_ARMW_SUB;
  else if (sl == 8 && memcmp(suf, "fetch_or", 8) == 0) rmwop = IR_ARMW_OR;
  else if (sl == 9 && memcmp(suf, "fetch_and", 9) == 0) rmwop = IR_ARMW_AND;
  else if (sl == 9 && memcmp(suf, "fetch_xor", 9) == 0) rmwop = IR_ARMW_XOR;
  if (rmwop >= 0) {
    ir_val_t val = build_expr(ctx, fn->args[1]);
    if (ctx->failed) return ir_val_none();
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *a = ir_inst_new(IR_ATOMIC);
    a->atomic_kind = IR_ATOMIC_RMW; a->atomic_rmw_op = (unsigned char)rmwop;
    a->atomic_width = (unsigned char)width; a->is_unsigned_load = (unsigned char)unsigned_p;
    a->src1 = ptr; a->src2 = val; a->dst = ir_val_vreg(v, rty);
    ir_func_append_inst(ctx->f, a);
    return a->dst;
  }
  fail(ctx, "unknown __ag_atomic intrinsic");
  return ir_val_none();
}

static ir_val_t build_node_funcall(ir_build_ctx_t *ctx, node_t *node) {
  node_func_t *fn = (node_func_t *)node;
  if (!fn->callee && fn->funcname && fn->funcname_len > 12 &&
      memcmp(fn->funcname, "__ag_atomic_", 12) == 0) {
    return build_node_atomic_intrinsic(ctx, fn);
  }
  /* 間接呼び出し: callee 式を pre-evaluate しておく (引数評価より先に
   * vreg に確定させる方が安全)。AST 経路と同じ評価順序を守るため。
   * 間接呼び出しは現状 variadic 非対応とする (関数ポインタ型に variadic
   * 情報が乗っていない簡略化)。 */
  ir_val_t callee_v = ir_val_none();
  if (fn->callee) {
    callee_v = build_expr(ctx, fn->callee);
    if (ctx->failed) return ir_val_none();
  }
  /* callee が variadic か確認。直接呼出は prototype から、間接呼出は関数ポインタ
   * lvar に記録した is_variadic_funcptr から判定する (Apple ARM64: 可変長引数は
   * stack 渡し。間接呼出でも同じ ABI が要る)。 */
  int is_variadic_call = 0;
  int nargs_fixed = fn->nargs;
  if (!fn->callee) {
    int fixed = 0;
    if (psx_ctx_get_function_is_variadic(fn->funcname, fn->funcname_len, &fixed) &&
        fixed < fn->nargs) {
      is_variadic_call = 1;
      nargs_fixed = fixed;
    }
  } else if (fn->callee->kind == ND_LVAR) {
    lvar_t *cl = find_owning_lvar(ctx, ((node_lvar_t *)fn->callee)->offset);
    if (cl && cl->is_variadic_funcptr && cl->funcptr_nargs_fixed < fn->nargs) {
      is_variadic_call = 1;
      nargs_fixed = cl->funcptr_nargs_fixed;
    }
  }
  /* 9 個以降の int 引数は codegen 側 IR_CALL が stack に積むので、ここでは
   * 制限せず通す (Apple ARM64 ABI)。float/double が 9 番目以降になる場合は
   * 未対応のままだが、本テスト用途では int のみ。 */
  if (is_variadic_call && nargs_fixed > 8) {
    fail(ctx, "more than 8 fixed args in variadic call (Phase 7e unsupported)");
    return ir_val_none();
  }
  ir_val_t *cargs = NULL;
  /* _Complex 値引数は 2 FP レジスタ (re, im) に展開されるので、cargs は引数あたり
   * 最大 2 エントリを確保し、argc で実エントリ数を数える。 */
  int argc = 0;
  if (fn->nargs > 0) {
    cargs = calloc((size_t)(2 * fn->nargs), sizeof(ir_val_t));
    for (int i = 0; i < fn->nargs; i++) {
      node_t *arg = fn->args[i];
      /* compound literal `(struct V){...}` は ND_COMMA(init, ND_LVAR temp)。
       * struct 値引数のときは init (要素ストア) を先に評価してから temp lvar を
       * struct 引数として扱う。これをしないと先頭 8B を値ロードして渡し、9-16B
       * struct の x1 分 (= 後半メンバ) が落ちていた。 */
      if (arg && arg->kind == ND_COMMA && arg->rhs && arg->rhs->kind == ND_LVAR) {
        node_lvar_t *rlv = (node_lvar_t *)arg->rhs;
        lvar_t *owner_cl = find_owning_lvar(ctx, rlv->offset);
        int cl_sz = owner_cl ? owner_cl->size : rlv->mem.type_size;
        if (cg_size_needs_indirect_struct(cl_sz) && !rlv->mem.is_pointer) {
          (void)build_expr(ctx, arg->lhs);
          if (ctx->failed) return ir_val_none();
          arg = arg->rhs;
        }
      }
      /* _Complex 値引数 (HFA): re→d{n}/s{n}, im→d{n+1}/s{n+1}。一時 slot に
       * {re,im} を materialize し、2 つの FP 値として push する。codegen の fp_idx
       * カウンタが連続 2 レジスタへ割り当てる。 */
      if (arg && arg->is_complex) {
        ir_type_t fp_ty = (arg->fp_kind == TK_FLOAT_KIND_FLOAT) ? IR_TY_F32 : IR_TY_F64;
        int half = (fp_ty == IR_TY_F32) ? 4 : 8;
        int slot = ir_func_new_vreg(ctx->f);
        ir_inst_t *al = ir_inst_new(IR_ALLOCA);
        al->dst = ir_val_vreg(slot, IR_TY_PTR);
        al->alloca_size = 2 * half;
        al->alloca_align = 8;
        ir_func_append_inst(ctx->f, al);
        build_complex_to(ctx, arg, slot, fp_ty, half);
        if (ctx->failed) return ir_val_none();
        for (int part = 0; part < 2; part++) {
          int pp = slot;
          if (part == 1) {
            pp = ir_func_new_vreg(ctx->f);
            ir_inst_t *lea = ir_inst_new(IR_LEA);
            lea->dst = ir_val_vreg(pp, IR_TY_PTR);
            lea->src1 = ir_val_vreg(slot, IR_TY_PTR);
            lea->src2 = ir_val_imm(IR_TY_I32, half);
            ir_func_append_inst(ctx->f, lea);
          }
          int lv = ir_func_new_vreg(ctx->f);
          ir_inst_t *ld = ir_inst_new(IR_LOAD);
          ld->dst = ir_val_vreg(lv, fp_ty);
          ld->src1 = ir_val_vreg(pp, IR_TY_PTR);
          ir_func_append_inst(ctx->f, ld);
          cargs[argc++] = ir_val_vreg(lv, fp_ty);
        }
        continue;
      }
      int arg_full_size = 0;
      /* struct/union 値で、サイズが 1/2/4/8 の clean なスカラロードに乗らない
       * (3/5/6/7 バイト) ものは、値ロードすると先頭メンバ幅しか読めず壊れる
       * (`{char;short;uchar}` の 6B が 1B 扱い)。これを >8B と同様にアドレス渡し
       * させるためのフラグ。 */
      int struct_needs_ptr = 0;
      if (arg && arg->kind == ND_LVAR) {
        node_lvar_t *lv = (node_lvar_t *)arg;
        /* VLA / 配列 / pointer は decay 後の値が 8B なので struct 経路に
         * 入れない。lvar->size は記述子サイズ (16/24) のことがある。 */
        if (!lv->mem.is_pointer) {
          lvar_t *owner = find_owning_lvar(ctx, lv->offset);
          if (owner) arg_full_size = owner->size;
          if (arg_full_size == 0) arg_full_size = lv->mem.type_size;
          /* 非 clean サイズ (3/5/6/7) は scalar に存在しないので struct/union 値で
             確定。配列は ND_ADDR へ decay し ND_LVAR では来ないため除外不要。 */
          if ((!owner || (!owner->is_array && owner->pointer_qual_levels == 0)) &&
              cg_size_needs_indirect_struct(arg_full_size) && arg_full_size <= 8) {
            struct_needs_ptr = 1;
          }
        } else {
          arg_full_size = lv->mem.type_size;
        }
      } else if (arg && arg->kind == ND_DEREF) {
        /* struct 値の subscript / メンバアクセス (`arr[i]`, `s.member`) は ND_DEREF。
         * tag を持ち >8B ならアドレス渡しの struct 引数として扱う。 */
        node_mem_t *m = (node_mem_t *)arg;
        if (m->tag_kind != TK_EOF && !m->is_tag_pointer && !m->is_pointer) {
          arg_full_size = m->type_size;
          if (arg_full_size != 1 && arg_full_size != 2 &&
              arg_full_size != 4 && arg_full_size != 8) {
            struct_needs_ptr = 1;
          }
        }
      } else if (arg && arg->kind == ND_FUNCALL && arg->ret_struct_size > 8) {
        /* >8B struct を返す関数呼び出しを直接 struct 引数に (`sum(make())`)。
         * build_node_funcall が ret_area を確保しそのアドレスを返すので、それを
         * そのまま渡す (新規 area なので memcpy 不要)。 */
        arg_full_size = arg->ret_struct_size;
      }
      if (arg->kind == ND_FUNCALL && arg_full_size > 8) {
        ir_val_t a = build_expr(ctx, arg);
        if (ctx->failed) return ir_val_none();
        cargs[argc++] = ir_val_vreg(a.id, IR_TY_PTR);
        continue;
      }
      if (arg_full_size > 8 || struct_needs_ptr) {
        /* struct 引数: 一時 frame slot に memcpy し、そのアドレスを渡す。
         * src アドレスは ND_LVAR なら lvar slot、ND_DEREF ならその lhs (= 計算済み
         * アドレス式) を評価して得る。 */
        int src_ptr;
        if (arg->kind == ND_DEREF) {
          ir_val_t a = build_expr(ctx, arg->lhs);
          if (ctx->failed) return ir_val_none();
          src_ptr = a.id;
        } else {
          src_ptr = address_of_lvar(ctx, ((node_lvar_t *)arg)->offset);
        }
        if (src_ptr < 0) return ir_val_none();
        int tmp_vreg = ir_func_new_vreg(ctx->f);
        ir_inst_t *ia = ir_inst_new(IR_ALLOCA);
        ia->dst = ir_val_vreg(tmp_vreg, IR_TY_PTR);
        ia->alloca_size = arg_full_size;
        ia->alloca_align = 8;
        ir_func_append_inst(ctx->f, ia);
        ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
        cp->src1 = ir_val_vreg(tmp_vreg, IR_TY_PTR);
        cp->src2 = ir_val_vreg(src_ptr, IR_TY_PTR);
        cp->alloca_size = arg_full_size;
        ir_func_append_inst(ctx->f, cp);
        cargs[argc++] = ir_val_vreg(tmp_vreg, IR_TY_PTR);
      } else {
        ir_val_t cv = build_expr(ctx, arg);
        if (ctx->failed) return ir_val_none();
        /* 直接呼び出しで、callee の i 番目仮引数が float/double なら
         * 実引数を I2F / F2F で変換する (`f(1)` で 1 を double に昇格)。
         * 可変長部分 (idx >= nargs_fixed) は default argument promotion の
         * 規則上 int は double に昇格すべきだが、ABI 側で整数レジスタで
         * 渡される現状実装と整合が取れないため触らない。 */
        if (!fn->callee && i < nargs_fixed) {
          tk_float_kind_t pfk = psx_ctx_get_function_param_fp_kind(
              fn->funcname, fn->funcname_len, i);
          if (pfk != TK_FLOAT_KIND_NONE) {
            ir_type_t target = (pfk == TK_FLOAT_KIND_FLOAT) ? IR_TY_F32 : IR_TY_F64;
            cv = coerce_to_type(ctx, cv, target);
          }
        }
        cargs[argc++] = cv;
      }
    }
  }
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *call = ir_inst_new(IR_CALL);
  /* 戻り値型を fp_kind 対応 (関数呼び出しの式 node に fp_kind が乗ってる) */
  ir_type_t ret_ty = ir_type_from_node(node);
  /* 直接呼び出しで戻り値が long / pointer のとき、呼び出し側でも 8 バイト値として
   * 扱う。i32 のままだと `h(x) * 2` のように戻り値を使う演算が 32bit で行われ
   * 上位ビットが落ちる (h が long を返す場合)。 */
  if (ret_ty == IR_TY_I32 && !fn->callee && fn->funcname) {
    if (psx_ctx_get_function_ret_is_pointer(fn->funcname, fn->funcname_len)) {
      ret_ty = IR_TY_PTR;
    } else {
      token_kind_t rk = psx_ctx_get_function_ret_token_kind(fn->funcname, fn->funcname_len);
      if (rk != TK_EOF && psx_ctx_scalar_type_size(rk) >= 8) ret_ty = IR_TY_I64;
    }
  }
  call->dst = ir_val_vreg(v, ret_ty);
  if (fn->callee) {
    call->callee = callee_v;
    call->sym = NULL;
    call->sym_len = 0;
  } else {
    call->sym = fn->funcname;
    call->sym_len = fn->funcname_len;
  }
  call->args = cargs;
  call->nargs = argc;
  call->is_variadic_call = is_variadic_call;
  call->nargs_fixed = nargs_fixed;
  /* _Complex 戻り値 (HFA): 呼び出し後 d0/d1 (s0/s1) を一時 slot に書き戻し、その
   * slot の PTR を複素数値の参照として返す (build_complex_to の ND_DEREF 経路等が
   * 受け取れる)。 */
  if (node->is_complex && !fn->callee) {
    int half = (ir_type_from_node(node) == IR_TY_F32) ? 4 : 8;
    int slot = ir_func_new_vreg(ctx->f);
    ir_inst_t *ia = ir_inst_new(IR_ALLOCA);
    ia->dst = ir_val_vreg(slot, IR_TY_PTR);
    ia->alloca_size = 2 * half;
    ia->alloca_align = 8;
    ir_func_append_inst(ctx->f, ia);
    call->dst = ir_val_vreg(slot, IR_TY_PTR);
    call->ret_complex_half = (unsigned char)half;
    ir_func_append_inst(ctx->f, call);
    return ir_val_vreg(slot, IR_TY_PTR);
  }
  /* >8B struct 戻り値を値文脈 (引数 / 式) で使う場合: 呼び出し側で ret_area を
   * 確保し x8 で渡す。戻り値はその area のアドレス (PTR)。struct 代入の直接 rhs は
   * build_assign_struct がインラインで処理するためここには来ない。 */
  int struct_ret_area = -1;
  if (cg_size_needs_indirect_struct(node->ret_struct_size) && !fn->callee) {
    struct_ret_area = ir_func_new_vreg(ctx->f);
    ir_inst_t *ia = ir_inst_new(IR_ALLOCA);
    ia->dst = ir_val_vreg(struct_ret_area, IR_TY_PTR);
    ia->alloca_size = node->ret_struct_size;
    ia->alloca_align = 8;
    ir_func_append_inst(ctx->f, ia);
    call->ret_struct_size = node->ret_struct_size;
    call->ret_struct_area = ir_val_vreg(struct_ret_area, IR_TY_PTR);
  }
  ir_func_append_inst(ctx->f, call);
  if (struct_ret_area >= 0) return ir_val_vreg(struct_ret_area, IR_TY_PTR);
  return call->dst;
}

static ir_val_t build_node_comma(ir_build_ctx_t *ctx, node_t *node) {
  /* (a, b): a を評価して値を捨て、b を評価してその値を返す */
  if (node->lhs) {
    (void)build_expr(ctx, node->lhs);
    if (ctx->failed) return ir_val_none();
  }
  if (node->rhs) return build_expr(ctx, node->rhs);
  return ir_val_none();
}

static ir_val_t build_node_logand_or(ir_build_ctx_t *ctx, node_t *node) {
  /* 短絡評価。結果は i32 (0/1)。temp slot に書いて merge で LOAD。
   *   a && b: 既定 0、a が真なら b、b が真なら 1。
   *   a || b: 既定 1、a が偽なら b、b が真なら 1。 */
  int is_and = (node->kind == ND_LOGAND);
  int slot_vreg = ir_func_new_vreg(ctx->f);
  ir_inst_t *al = ir_inst_new(IR_ALLOCA);
  al->dst = ir_val_vreg(slot_vreg, IR_TY_PTR);
  al->alloca_size = 4;
  al->alloca_align = 4;
  ir_func_append_inst(ctx->f, al);
  /* 既定値を STORE */
  ir_inst_t *st0 = ir_inst_new(IR_STORE);
  st0->src1 = ir_val_vreg(slot_vreg, IR_TY_PTR);
  st0->src2 = ir_val_imm(IR_TY_I32, is_and ? 0 : 1);
  ir_func_append_inst(ctx->f, st0);
  ir_block_t *eval_rhs_b = ir_block_new(ctx->f);
  ir_block_t *merge_b = ir_block_new(ctx->f);
  ir_val_t l = build_expr(ctx, node->lhs);
  if (ctx->failed) return ir_val_none();
  if (is_and) {
    emit_br_cond(ctx, l, eval_rhs_b, merge_b);
  } else {
    emit_br_cond(ctx, l, merge_b, eval_rhs_b);
  }
  switch_to_new_block(ctx, eval_rhs_b);
  ir_val_t r = build_expr(ctx, node->rhs);
  if (ctx->failed) return ir_val_none();
  /* 0/1 に正規化: r != 0 (fp は FNE で比較)。 */
  ir_val_t v_norm = emit_truthiness(ctx, r);
  ir_inst_t *st1 = ir_inst_new(IR_STORE);
  st1->src1 = ir_val_vreg(slot_vreg, IR_TY_PTR);
  st1->src2 = v_norm;
  ir_func_append_inst(ctx->f, st1);
  emit_br(ctx, merge_b);
  switch_to_new_block(ctx, merge_b);
  int v_res = ir_func_new_vreg(ctx->f);
  ir_inst_t *ld = ir_inst_new(IR_LOAD);
  ld->dst = ir_val_vreg(v_res, IR_TY_I32);
  ld->src1 = ir_val_vreg(slot_vreg, IR_TY_PTR);
  ir_func_append_inst(ctx->f, ld);
  return ir_val_vreg(v_res, IR_TY_I32);
}

/* 三項演算子の分岐値が 64bit 整数 (long / long long、または 32bit に収まらない
 * リテラル) かを判定する。ポインタ・浮動小数は別経路で扱うので対象外。
 * 64bit 分岐があるのに結果型を i32 と誤判定すると、8 バイト値が 4 バイト slot へ
 * 切り詰められる (例 `(c)?5:10000000000L` が下位 32bit になる)。 */
static int ternary_branch_is_wide_int(node_t *n) {
  if (!n) return 0;
  if (n->fp_kind != TK_FLOAT_KIND_NONE) return 0;
  if (n->kind == ND_NUM) {
    long long v = ((node_num_t *)n)->val;
    /* build_node_num と同じ境界: [INT32_MIN, UINT32_MAX] は 32bit で扱う。 */
    return v > 0xFFFFFFFFLL || v < (-2147483647LL - 1);
  }
  /* 入れ子三項 (`a ? x : (b ? bigL : y)`): 内側の分岐に 64bit があれば結果も 64bit。
   * psx_node_type_size は NUM リテラルを 0 とみなすため、再帰でリテラルを拾う。 */
  if (n->kind == ND_TERNARY) {
    return ternary_branch_is_wide_int(n->rhs) ||
           ternary_branch_is_wide_int(((node_ctrl_t *)n)->els);
  }
  return psx_node_type_size(n) >= 8;
}

static ir_val_t build_node_ternary(ir_build_ctx_t *ctx, node_t *node) {
  /* cond ? rhs : els 。各分岐で eval して temp slot に STORE、merge で LOAD。
   * 結果型は fp_kind から推定 (整数のみ or float/double)。
   * struct ternary 等は今のところサポート外で fall through する。 */
  node_ctrl_t *c = (node_ctrl_t *)node;
  if (!c->els) {
    fail(ctx, "ternary without else");
    return ir_val_none();
  }
  ir_type_t res_ty = ir_type_from_node(node);
  int slot_size = (res_ty == IR_TY_F64) ? 8 : 4;
  /* ポインタ三項 (関数ポインタや int* など): 8 バイト slot で扱う。
   * 子ノードのいずれかがポインタなら結果もポインタ。 */
  /* `&x` (ND_ADDR) は常にポインタ値だが is_pointer フラグを持たないことがあるため
   * 明示的に判定に加える (`(c ? &a : &b)->m` が 8 バイトで扱われるように)。 */
  if (res_ty == IR_TY_I32 &&
      (psx_node_is_pointer(node->rhs) || psx_node_is_pointer(c->els) ||
       node->rhs->kind == ND_FUNCREF || node->rhs->kind == ND_ADDR ||
       (c->els && (c->els->kind == ND_FUNCREF || c->els->kind == ND_ADDR)))) {
    res_ty = IR_TY_PTR;
    slot_size = 8;
  }
  /* long / long long 分岐: 結果は 64bit 整数。8 バイト slot で扱う。 */
  if (res_ty == IR_TY_I32 &&
      (ternary_branch_is_wide_int(node->rhs) || ternary_branch_is_wide_int(c->els))) {
    res_ty = IR_TY_I64;
    slot_size = 8;
  }
  int slot_vreg = ir_func_new_vreg(ctx->f);
  ir_inst_t *al = ir_inst_new(IR_ALLOCA);
  al->dst = ir_val_vreg(slot_vreg, IR_TY_PTR);
  al->alloca_size = slot_size;
  al->alloca_align = slot_size >= 8 ? 8 : 4;
  ir_func_append_inst(ctx->f, al);
  ir_val_t cond = build_expr(ctx, node->lhs);
  if (ctx->failed) return ir_val_none();
  ir_block_t *then_b = ir_block_new(ctx->f);
  ir_block_t *else_b = ir_block_new(ctx->f);
  ir_block_t *merge_b = ir_block_new(ctx->f);
  emit_br_cond(ctx, cond, then_b, else_b);
  /* then */
  switch_to_new_block(ctx, then_b);
  ir_val_t vt = build_expr(ctx, node->rhs);
  if (ctx->failed) return ir_val_none();
  /* 型変換: 結果型が fp で値が int なら I2F、逆も */
  if (is_fp_type(res_ty) && !is_fp_type(vt.type)) {
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *cv = ir_inst_new(IR_I2F);
    cv->dst = ir_val_vreg(v, res_ty); cv->src1 = vt;
    ir_func_append_inst(ctx->f, cv);
    vt = ir_val_vreg(v, res_ty);
  } else if (is_fp_type(res_ty) && is_fp_type(vt.type) && vt.type != res_ty) {
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *cv = ir_inst_new(IR_F2F);
    cv->dst = ir_val_vreg(v, res_ty); cv->src1 = vt;
    ir_func_append_inst(ctx->f, cv);
    vt = ir_val_vreg(v, res_ty);
  }
  /* 8 バイト結果 (ポインタ / long) で分岐値が狭い整数 (例 null pointer constant
   * `0` = i32、または int 分岐) の場合、slot は 8 バイトなので 8 バイト幅へ拡張
   * してから STORE する。さもないと 4 バイトのみ書かれ、merge の 8 バイト LOAD で
   * 上位 4 バイトが garbage になる。 */
  if ((res_ty == IR_TY_PTR || res_ty == IR_TY_I64) && vt.type != res_ty) {
    vt = coerce_to_type(ctx, vt, res_ty);
  } else if (res_ty == IR_TY_I32 && !is_fp_type(vt.type) && ir_type_size(vt.type) < 4) {
    /* sub-int (char/short) 分岐: slot は 4 バイトなので full-width で store する。
     * strb だと上位 3 バイトが未初期化のまま残り、merge の 4 バイト ldrsw が garbage を
     * 読む (`c ? int : char` が誤値)。値は load 時に既に符号/ゼロ拡張済みなので型 tag を
     * 結果型 (I32) に付け替えるだけ (SEXT を挿入すると unsigned char 分岐を誤って符号
     * 拡張する)。 */
    vt.type = IR_TY_I32;
  }
  ir_inst_t *st_t = ir_inst_new(IR_STORE);
  st_t->src1 = ir_val_vreg(slot_vreg, IR_TY_PTR);
  st_t->src2 = vt;
  ir_func_append_inst(ctx->f, st_t);
  emit_br(ctx, merge_b);
  /* else */
  switch_to_new_block(ctx, else_b);
  ir_val_t ve = build_expr(ctx, c->els);
  if (ctx->failed) return ir_val_none();
  if (is_fp_type(res_ty) && !is_fp_type(ve.type)) {
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *cv = ir_inst_new(IR_I2F);
    cv->dst = ir_val_vreg(v, res_ty); cv->src1 = ve;
    ir_func_append_inst(ctx->f, cv);
    ve = ir_val_vreg(v, res_ty);
  } else if (is_fp_type(res_ty) && is_fp_type(ve.type) && ve.type != res_ty) {
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *cv = ir_inst_new(IR_F2F);
    cv->dst = ir_val_vreg(v, res_ty); cv->src1 = ve;
    ir_func_append_inst(ctx->f, cv);
    ve = ir_val_vreg(v, res_ty);
  }
  if ((res_ty == IR_TY_PTR || res_ty == IR_TY_I64) && ve.type != res_ty) {
    ve = coerce_to_type(ctx, ve, res_ty);
  } else if (res_ty == IR_TY_I32 && !is_fp_type(ve.type) && ir_type_size(ve.type) < 4) {
    /* sub-int (char/short) 分岐の full-width store (then 側と同じ。詳細は上のコメント)。 */
    ve.type = IR_TY_I32;
  }
  ir_inst_t *st_e = ir_inst_new(IR_STORE);
  st_e->src1 = ir_val_vreg(slot_vreg, IR_TY_PTR);
  st_e->src2 = ve;
  ir_func_append_inst(ctx->f, st_e);
  emit_br(ctx, merge_b);
  /* merge */
  switch_to_new_block(ctx, merge_b);
  int v_res = ir_func_new_vreg(ctx->f);
  ir_inst_t *ld = ir_inst_new(IR_LOAD);
  ld->dst = ir_val_vreg(v_res, res_ty);
  ld->src1 = ir_val_vreg(slot_vreg, IR_TY_PTR);
  ir_func_append_inst(ctx->f, ld);
  return ir_val_vreg(v_res, res_ty);
}

/* -------- Phase B1: build_expr の残余 case ヘルパ -------- */

static ir_val_t build_node_fp_to_int(ir_build_ctx_t *ctx, node_t *node) {
  ir_val_t v = build_expr(ctx, node->lhs);
  if (ctx->failed) return ir_val_none();
  int dst = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_F2I);
  inst->dst = ir_val_vreg(dst, IR_TY_I32);
  inst->src1 = v;
  ir_func_append_inst(ctx->f, inst);
  return inst->dst;
}

/* GNU __real__ / __imag__: 複素数 lhs の実部/虚部を取り出す。複素数式は temp slot に
 * materialize し ({re, im} 連続) re=offset 0 / im=offset half を fp load する。
 * 実数オペランドでは __real__ x = x、__imag__ x = 0.0。任意の式 (rvalue) に効く。 */
static ir_val_t build_node_creal_cimag(ir_build_ctx_t *ctx, node_t *node) {
  int is_real = (node->kind == ND_CREAL);
  node_t *operand = node->lhs;
  ir_type_t fp_ty = (node->fp_kind == TK_FLOAT_KIND_FLOAT) ? IR_TY_F32 : IR_TY_F64;
  int half = (fp_ty == IR_TY_F32) ? 4 : 8;
  if (!operand || !operand->is_complex) {
    /* 実数オペランド: __real__ は値そのもの、__imag__ は 0.0。 */
    if (is_real) return build_expr(ctx, operand);
    int z = ir_func_new_vreg(ctx->f);
    ir_inst_t *zi = ir_inst_new(IR_LOAD_FP_IMM);
    zi->dst = ir_val_vreg(z, fp_ty);
    zi->src1 = ir_val_fp_imm(fp_ty, 0.0);
    ir_func_append_inst(ctx->f, zi);
    return ir_val_vreg(z, fp_ty);
  }
  int slot = ir_func_new_vreg(ctx->f);
  ir_inst_t *al = ir_inst_new(IR_ALLOCA);
  al->dst = ir_val_vreg(slot, IR_TY_PTR);
  al->alloca_size = 2 * half;
  al->alloca_align = 8;
  ir_func_append_inst(ctx->f, al);
  build_complex_to(ctx, operand, slot, fp_ty, half);
  if (ctx->failed) return ir_val_none();
  int part_ptr = slot;
  if (!is_real) {
    int imp = ir_func_new_vreg(ctx->f);
    ir_inst_t *lea = ir_inst_new(IR_LEA);
    lea->dst = ir_val_vreg(imp, IR_TY_PTR);
    lea->src1 = ir_val_vreg(slot, IR_TY_PTR);
    lea->src2 = ir_val_imm(IR_TY_I32, half);
    ir_func_append_inst(ctx->f, lea);
    part_ptr = imp;
  }
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *ld = ir_inst_new(IR_LOAD);
  ld->dst = ir_val_vreg(v, fp_ty);
  ld->src1 = ir_val_vreg(part_ptr, IR_TY_PTR);
  ir_func_append_inst(ctx->f, ld);
  return ir_val_vreg(v, fp_ty);
}

/* 浮動小数の単項マイナス `-x` — 符号ビット反転 (IR_FNEG)。`0.0 - x` (IR_FSUB) では
 * x が +0.0 のとき +0.0 になり -0.0 を生成できないため専用に lowering する。 */
static ir_val_t build_node_fneg(ir_build_ctx_t *ctx, node_t *node) {
  ir_val_t v = build_expr(ctx, node->lhs);
  if (ctx->failed) return ir_val_none();
  ir_type_t ty = (node->fp_kind == TK_FLOAT_KIND_FLOAT) ? IR_TY_F32 : IR_TY_F64;
  v = coerce_to_type(ctx, v, ty);
  int dst = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_FNEG);
  inst->dst = ir_val_vreg(dst, ty);
  inst->src1 = v;
  ir_func_append_inst(ctx->f, inst);
  return inst->dst;
}

/* `(double)i` / `(float)x` — 整数または別幅FPを目的のFP型へ変換する。
 * coerce_to_type が int→fp は IR_I2F、float↔double は IR_F2F、同型は no-op を担う。 */
static ir_val_t build_node_int_to_fp(ir_build_ctx_t *ctx, node_t *node) {
  ir_val_t v = build_expr(ctx, node->lhs);
  if (ctx->failed) return ir_val_none();
  ir_type_t target = (node->fp_kind == TK_FLOAT_KIND_FLOAT) ? IR_TY_F32 : IR_TY_F64;
  return coerce_to_type(ctx, v, target);
}

static ir_val_t build_node_inc_dec(ir_build_ctx_t *ctx, node_t *node) {
  /* ++x / x++ / --x / x-- — target は ND_LVAR / ND_DEREF / ND_GVAR を許可する。
   *   ND_LVAR : address_of_lvar で frame アドレスを得る。
   *   ND_DEREF: target->lhs を eval して得たポインタをそのまま使う
   *             ((**pp)++ や cast 経由の `((T*)p)->m++` がここに来る)。
   *   ND_GVAR : @PAGE/PAGEOFF (TLS なら TLV) でアドレスを取る。
   * pointer の inc/dec は parser が +/- step を scale 済みであることに依存。 */
  node_t *target = node->lhs;
  if (!target) {
    fail(ctx, "inc/dec without target");
    return ir_val_none();
  }
  int ptr_vreg = -1;
  ir_type_t vty = IR_TY_I32;
  if (target->kind == ND_LVAR) {
    node_lvar_t *lv = (node_lvar_t *)target;
    vty = lvar_value_type(lv);
    ptr_vreg = address_of_lvar(ctx, lv->offset);
  } else if (target->kind == ND_DEREF) {
    ir_val_t p = build_expr(ctx, target->lhs);
    if (ctx->failed) return ir_val_none();
    ptr_vreg = p.id;
    /* load/store 幅は DEREF node の mem.type_size と fp_kind から決める。 */
    node_mem_t *mm = (node_mem_t *)target;
    vty = ir_type_from_node(target);
    if (vty == IR_TY_I32) {
      if (mm->type_size >= 8) vty = IR_TY_PTR;
      else if (mm->type_size == 2) vty = IR_TY_I16;
      else if (mm->type_size == 1) vty = IR_TY_I8;
    }
  } else if (target->kind == ND_GVAR) {
    node_gvar_t *gv = (node_gvar_t *)target;
    vty = ir_type_from_node(target);
    if (vty == IR_TY_I32) {
      int sz = gv->mem.type_size > 0 ? gv->mem.type_size : 4;
      vty = (sz >= 8) ? IR_TY_PTR : (sz == 4 ? IR_TY_I32 : (sz == 2 ? IR_TY_I16 : IR_TY_I8));
    }
    int v_addr = ir_func_new_vreg(ctx->f);
    ir_inst_t *sym = ir_inst_new(gv->is_thread_local ? IR_LOAD_TLV_ADDR : IR_LOAD_SYM);
    sym->dst = ir_val_vreg(v_addr, IR_TY_PTR);
    sym->sym = gv->name;
    sym->sym_len = gv->name_len;
    ir_func_append_inst(ctx->f, sym);
    ptr_vreg = v_addr;
  } else {
    fail(ctx, "inc/dec target not LVAR/DEREF/GVAR");
    return ir_val_none();
  }
  if (ptr_vreg < 0) return ir_val_none();
  /* load 現在値 */
  int v_old = ir_func_new_vreg(ctx->f);
  ir_inst_t *ld = ir_inst_new(IR_LOAD);
  ld->dst = ir_val_vreg(v_old, vty);
  ld->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
  ir_func_append_inst(ctx->f, ld);
  /* step: スカラは 1、ポインタ (deref_size > 1) は pointee サイズ。
   * `short *p; p++` を 2 バイトステップにするため deref_size を参照する。 */
  long long step = 1;
  if (psx_node_is_pointer(target)) {
    int ds = psx_node_deref_size(target);
    if (ds > 1) step = ds;
  }
  int is_inc = (node->kind == ND_PRE_INC || node->kind == ND_POST_INC);
  /* float / double の ++/-- は fp 専用オペコード (IR_FADD/IR_FSUB) を使い、step は
   * 1.0 とする。整数 1 を I2F で fp へ変換した値を加減算する (整数即値をそのまま
   * fadd するとビットパターンとして誤解釈されるため)。 */
  ir_op_t binop;
  ir_val_t step_val;
  if (is_fp_type(vty)) {
    binop = is_inc ? IR_FADD : IR_FSUB;
    int one_v = emit_load_imm(ctx, 1, IR_TY_I32);
    step_val = coerce_to_type(ctx, ir_val_vreg(one_v, IR_TY_I32), vty);
  } else {
    binop = is_inc ? IR_ADD : IR_SUB;
    step_val = ir_val_imm(vty, step);
  }
  int v_new = emit_binop(ctx, binop, ir_val_vreg(v_old, vty), step_val, vty);
  ir_inst_t *st = ir_inst_new(IR_STORE);
  st->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
  st->src2 = ir_val_vreg(v_new, vty);
  ir_func_append_inst(ctx->f, st);
  int is_pre = (node->kind == ND_PRE_INC || node->kind == ND_PRE_DEC);
  return is_pre ? ir_val_vreg(v_new, vty) : ir_val_vreg(v_old, vty);
}

static ir_val_t build_node_va_arg_area(ir_build_ctx_t *ctx, node_t *node) {
  (void)node;
  /* stdarg.h の va_start マクロが参照する builtin。
   * stack 上の variadic 引数領域 = x29 + total_size (callee の frame の top)。 */
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_VA_ARG_AREA);
  inst->dst = ir_val_vreg(v, IR_TY_PTR);
  ir_func_append_inst(ctx->f, inst);
  return inst->dst;
}

static ir_val_t build_node_ptr_cast(ir_build_ctx_t *ctx, node_t *node) {
  /* (type *)expr ポインタキャスト。値は変えず、後段の deref 用に
   * pointee_fp_kind 等を保持するためのラッパ。IR では lhs をそのまま eval。 */
  if (!node->lhs) return ir_val_none();
  ir_val_t v = build_expr(ctx, node->lhs);
  if (ctx->failed) return ir_val_none();
  /* `(long)unsigned_int` の zero-extend ラッパ: lhs (I32) を I64 へ ZEXT する。
   * coerce_to_type は常に SEXT なので unsigned widen には使えず、ここで明示挿入する。
   * これにより `(long)u + (long)u` の二項演算が I64 で計算され (I32 ラップマスク回避)、
   * 2^32 を超える和が正しくなる。 */
  if (((node_mem_t *)node)->widen_zext_i64 && v.type != IR_TY_I64 && !is_fp_type(v.type)) {
    int d = ir_func_new_vreg(ctx->f);
    ir_inst_t *zx = ir_inst_new(IR_ZEXT);
    zx->dst = ir_val_vreg(d, IR_TY_I64);
    zx->src1 = v;
    ir_func_append_inst(ctx->f, zx);
    return ir_val_vreg(d, IR_TY_I64);
  }
  return v;
}

static ir_val_t build_node_vla_alloc(ir_build_ctx_t *ctx, node_t *node) {
  /* VLA 動的確保: parser は ND_VLA_ALLOC を init_chain (comma) の一部として
   * 配置する。type_size = descriptor lvar の frame offset。
   *   slot[0] = base pointer
   *   slot[8] = byte size  (sizeof(vla) で読まれる)
   *   slot[16]= row stride (2D runtime inner のみ。rsf != 0)
   *   1D     : lhs = total bytes (n * elem_size)
   *   2D const: lhs = total bytes (n * outer_stride)
   *   2D rt  : lhs = outer_count(n), rhs = row_stride(m * elem) */
  node_mem_t *am = (node_mem_t *)node;
  int desc_offset = am->type_size;
  int rsf = am->vla_row_stride_frame_off;
  int desc_ptr = address_of_lvar(ctx, desc_offset);
  if (desc_ptr < 0) return ir_val_none();
  ir_val_t total_size;
  if (rsf > 0) {
    ir_val_t row_stride = build_expr(ctx, node->rhs);
    if (ctx->failed) return ir_val_none();
    ir_val_t outer_count = build_expr(ctx, node->lhs);
    if (ctx->failed) return ir_val_none();
    int v_total = emit_binop(ctx, IR_MUL, outer_count, row_stride, IR_TY_I32);
    total_size = ir_val_vreg(v_total, IR_TY_I32);
    /* row_stride を desc+16 へ */
    int rs_ptr = address_of_lvar(ctx, rsf);
    if (rs_ptr < 0) return ir_val_none();
    /* i32 → i64 にゼロ拡張してから 8B store */
    int v_rs64 = ir_func_new_vreg(ctx->f);
    ir_inst_t *zext = ir_inst_new(IR_ZEXT);
    zext->dst = ir_val_vreg(v_rs64, IR_TY_I64);
    zext->src1 = row_stride;
    ir_func_append_inst(ctx->f, zext);
    ir_inst_t *st_rs = ir_inst_new(IR_STORE);
    st_rs->src1 = ir_val_vreg(rs_ptr, IR_TY_PTR);
    st_rs->src2 = ir_val_vreg(v_rs64, IR_TY_I64);
    ir_func_append_inst(ctx->f, st_rs);
  } else {
    total_size = build_expr(ctx, node->lhs);
    if (ctx->failed) return ir_val_none();
  }
  /* total_size を i64 に拡張して slot+8 に store */
  int v_sz64 = ir_func_new_vreg(ctx->f);
  ir_inst_t *zext_sz = ir_inst_new(IR_ZEXT);
  zext_sz->dst = ir_val_vreg(v_sz64, IR_TY_I64);
  zext_sz->src1 = total_size;
  ir_func_append_inst(ctx->f, zext_sz);
  int size_slot_ptr = ir_func_new_vreg(ctx->f);
  ir_inst_t *lea_sz = ir_inst_new(IR_LEA);
  lea_sz->dst = ir_val_vreg(size_slot_ptr, IR_TY_PTR);
  lea_sz->src1 = ir_val_vreg(desc_ptr, IR_TY_PTR);
  lea_sz->src2 = ir_val_imm(IR_TY_I32, 8);
  ir_func_append_inst(ctx->f, lea_sz);
  ir_inst_t *st_sz = ir_inst_new(IR_STORE);
  st_sz->src1 = ir_val_vreg(size_slot_ptr, IR_TY_PTR);
  st_sz->src2 = ir_val_vreg(v_sz64, IR_TY_I64);
  ir_func_append_inst(ctx->f, st_sz);
  /* IR_VLA_ALLOC: sp 動的確保。dst = 新しい base pointer。 */
  int v_base = ir_func_new_vreg(ctx->f);
  ir_inst_t *va = ir_inst_new(IR_VLA_ALLOC);
  va->dst = ir_val_vreg(v_base, IR_TY_PTR);
  va->src1 = total_size;
  ir_func_append_inst(ctx->f, va);
  /* base ptr を slot[0] に store */
  ir_inst_t *st_base = ir_inst_new(IR_STORE);
  st_base->src1 = ir_val_vreg(desc_ptr, IR_TY_PTR);
  st_base->src2 = ir_val_vreg(v_base, IR_TY_PTR);
  ir_func_append_inst(ctx->f, st_base);
  /* AST 経路では式として 0 を返すので合わせる。 */
  return ir_val_imm(IR_TY_I32, 0);
}

/* 現在ブロックの末尾に「分岐 / return」が無ければ自動で BR を補う。
 * これは if/while/for の各分岐の最後に呼ぶ。 */
static int cur_block_is_terminated(ir_func_t *f) {
  if (!f->cur_block || !f->cur_block->tail) return 0;
  ir_op_t op = f->cur_block->tail->op;
  return op == IR_BR || op == IR_BR_COND || op == IR_RET;
}

static void emit_br(ir_build_ctx_t *ctx, ir_block_t *target) {
  if (cur_block_is_terminated(ctx->f)) return;
  ir_inst_t *br = ir_inst_new(IR_BR);
  br->label_id = target->id;
  ir_func_append_inst(ctx->f, br);
}

static void emit_br_cond(ir_build_ctx_t *ctx, ir_val_t cond,
                          ir_block_t *t_block, ir_block_t *f_block) {
  if (cur_block_is_terminated(ctx->f)) return;
  /* fp 値を条件に使う (`f ? a : b` / `if (f)` / `while (f)`) 場合は (cond != 0.0) に
   * 変換してから分岐する。fp vreg をそのまま整数条件として分岐に渡すと codegen が
   * float の bit を整数として再解釈し (spill された 4B float を 8B 整数 load して
   * 上位 32bit に garbage を拾い) 0.0 が真と判定されることがあった。 */
  if (is_fp_type(cond.type)) {
    cond = emit_truthiness(ctx, cond);
  }
  ir_inst_t *br = ir_inst_new(IR_BR_COND);
  br->src1 = cond;
  br->label_id = t_block->id;
  br->else_label_id = f_block->id;
  ir_func_append_inst(ctx->f, br);
}

/* 新しいブロックに切り替える。先頭に IR_LABEL を置いておく
 * (codegen 側でラベル定義として出力)。 */
static void switch_to_new_block(ir_build_ctx_t *ctx, ir_block_t *b) {
  ir_func_switch_block(ctx->f, b);
  ir_inst_t *lbl = ir_inst_new(IR_LABEL);
  lbl->label_id = b->id;
  ir_func_append_inst(ctx->f, lbl);
}

static void push_loop(ir_build_ctx_t *ctx, ir_block_t *cont_b, ir_block_t *break_b) {
  if (ctx->loop_depth >= MAX_LOOP_DEPTH) {
    fail(ctx, "loop nesting too deep");
    return;
  }
  ctx->loops[ctx->loop_depth].continue_block = cont_b;
  ctx->loops[ctx->loop_depth].break_block = break_b;
  ctx->loop_depth++;
}

static void pop_loop(ir_build_ctx_t *ctx) {
  if (ctx->loop_depth > 0) ctx->loop_depth--;
}

/* 現在の switch スコープに「AST node → IR block」を登録する。
 * 同じ AST node には常に同じ block を返す。 */
static ir_block_t *register_case_block(ir_build_ctx_t *ctx, void *ast_node) {
  for (int i = 0; i < ctx->case_map_count; i++) {
    if (ctx->case_map[i].ast_node == ast_node) return ctx->case_map[i].block;
  }
  if (ctx->case_map_count >= MAX_CASES) {
    fail(ctx, "too many case labels in switch");
    return NULL;
  }
  ir_block_t *b = ir_block_new(ctx->f);
  ctx->case_map[ctx->case_map_count].ast_node = ast_node;
  ctx->case_map[ctx->case_map_count].block = b;
  ctx->case_map_count++;
  return b;
}

static ir_block_t *find_case_block(ir_build_ctx_t *ctx, void *ast_node) {
  for (int i = 0; i < ctx->case_map_count; i++) {
    if (ctx->case_map[i].ast_node == ast_node) return ctx->case_map[i].block;
  }
  return NULL;
}

/* AST body を pre-walk して全 ND_CASE / ND_DEFAULT を集める。
 * 各 case に IR block を割り当て (case_map に登録)、走査用の配列に詰める。 */
static void collect_case_labels(ir_build_ctx_t *ctx, node_t *body,
                                  node_case_t **cases, int *case_count,
                                  node_default_t **out_default) {
  if (!body) return;
  if (body->kind == ND_SWITCH) return; /* ネストした switch は別スコープ */
  if (body->kind == ND_CASE) {
    if (*case_count < MAX_CASES) {
      cases[(*case_count)++] = (node_case_t *)body;
    }
    register_case_block(ctx, body);
    /* case ラベル自身は文を持たないことが多いが、ag_c の AST では
     * 次の文へつながる構造になっているので fall-through 探索のため
     * lhs/rhs を再帰する */
  }
  if (body->kind == ND_DEFAULT) {
    *out_default = (node_default_t *)body;
    register_case_block(ctx, body);
  }
  if (body->kind == ND_BLOCK) {
    node_block_t *blk = (node_block_t *)body;
    if (blk->body) {
      for (int i = 0; blk->body[i]; i++) {
        collect_case_labels(ctx, blk->body[i], cases, case_count, out_default);
      }
    }
    return;
  }
  /* if/while/for/do-while の中の case も拾う (C 仕様上 switch スコープ内) */
  if (body->lhs) collect_case_labels(ctx, body->lhs, cases, case_count, out_default);
  if (body->rhs) collect_case_labels(ctx, body->rhs, cases, case_count, out_default);
  if (body->kind == ND_IF || body->kind == ND_FOR) {
    node_ctrl_t *c = (node_ctrl_t *)body;
    if (c->els) collect_case_labels(ctx, c->els, cases, case_count, out_default);
    if (c->init) collect_case_labels(ctx, c->init, cases, case_count, out_default);
    if (c->inc) collect_case_labels(ctx, c->inc, cases, case_count, out_default);
  }
}

/* label 名 → IR block の検索/登録。pre-walk 中に新規 ND_LABEL を見つけたら
 * 新しい block を割り当てて登録する。同名は同じ block を返す。 */
static ir_block_t *lookup_label_block(ir_build_ctx_t *ctx, const char *name, int name_len) {
  for (int i = 0; i < ctx->label_count; i++) {
    if (ctx->labels[i].name_len == name_len &&
        memcmp(ctx->labels[i].name, name, (size_t)name_len) == 0) {
      return ctx->labels[i].block;
    }
  }
  return NULL;
}

static ir_block_t *register_label_block(ir_build_ctx_t *ctx, const char *name, int name_len) {
  ir_block_t *existing = lookup_label_block(ctx, name, name_len);
  if (existing) return existing;
  if (ctx->label_count >= MAX_LABELS) {
    fail(ctx, "too many labels in function");
    return NULL;
  }
  ir_block_t *b = ir_block_new(ctx->f);
  ctx->labels[ctx->label_count].name = name;
  ctx->labels[ctx->label_count].name_len = name_len;
  ctx->labels[ctx->label_count].block = b;
  ctx->label_count++;
  return b;
}

/* 関数本体を pre-walk して全 ND_LABEL に IR block を割り当てる。
 * (goto が前方参照する label にも対応するため。) */
static void collect_labels(ir_build_ctx_t *ctx, node_t *body) {
  if (!body || ctx->failed) return;
  if (body->kind == ND_LABEL) {
    node_jump_t *j = (node_jump_t *)body;
    register_label_block(ctx, j->name, j->name_len);
  }
  if (body->kind == ND_BLOCK) {
    node_block_t *blk = (node_block_t *)body;
    if (blk->body) {
      for (int i = 0; blk->body[i]; i++) collect_labels(ctx, blk->body[i]);
    }
    return;
  }
  if (body->lhs) collect_labels(ctx, body->lhs);
  if (body->rhs) collect_labels(ctx, body->rhs);
  if (body->kind == ND_IF || body->kind == ND_FOR) {
    node_ctrl_t *c = (node_ctrl_t *)body;
    if (c->els) collect_labels(ctx, c->els);
    if (c->init) collect_labels(ctx, c->init);
    if (c->inc) collect_labels(ctx, c->inc);
  }
  if (body->kind == ND_CASE || body->kind == ND_DEFAULT) {
    /* case/default 内も body へ展開される (rhs を walk 済み) */
  }
}

static void build_stmt(ir_build_ctx_t *ctx, node_t *node) {
  if (!node || ctx->failed) return;
  switch (node->kind) {
    case ND_BLOCK:    build_stmt_block(ctx, node); return;
    case ND_RETURN:   build_stmt_return(ctx, node); return;
    case ND_NUM:      /* 副作用のない単独式 (宣言由来のプレースホルダ等) */ return;
    case ND_ASSIGN:
    case ND_FUNCALL:
    case ND_COMMA:    /* 式文 stmt: 式を評価して値を捨てる */
                      (void)build_expr(ctx, node); return;
    case ND_IF:       build_stmt_if(ctx, node); return;
    case ND_WHILE:    build_stmt_while(ctx, node); return;
    case ND_DO_WHILE: build_stmt_do_while(ctx, node); return;
    case ND_FOR:      build_stmt_for(ctx, node); return;
    case ND_SWITCH:   build_stmt_switch(ctx, node); return;
    case ND_CASE:
    case ND_DEFAULT:  build_stmt_case_default(ctx, node); return;
    case ND_BREAK:    build_stmt_break(ctx, node); return;
    case ND_CONTINUE: build_stmt_continue(ctx, node); return;
    case ND_GOTO:     build_stmt_goto(ctx, node); return;
    case ND_LABEL:    build_stmt_label(ctx, node); return;
    default:
      /* それ以外は「副作用ありの式文」として扱う。build_expr が unsupported
       * なら failed が立って fallback。 */
      (void)build_expr(ctx, node);
      return;
  }
}

/* -------- build_stmt の case ヘルパ -------- */

static void build_stmt_block(ir_build_ctx_t *ctx, node_t *node) {
  node_block_t *b = (node_block_t *)node;
  if (b->body) {
    for (int i = 0; b->body[i]; i++) {
      build_stmt(ctx, b->body[i]);
      if (ctx->failed) return;
    }
  }
}

static void build_stmt_return(ir_build_ctx_t *ctx, node_t *node) {
  /* struct 戻り値: *ret_area = node->lhs を memcpy して void return。 */
  if (ctx->f->ret_struct_size > 0 && node->lhs) {
    int src_ptr = -1;
    node_t *src = node->lhs;
    /* compound literal `return (struct V){...};` は ND_COMMA(init, struct lvalue)。
     * init (要素ストア) を評価してから rhs を struct ソースとして扱う。 */
    if (src->kind == ND_COMMA && src->rhs) {
      (void)build_expr(ctx, src->lhs);
      if (ctx->failed) return;
      src = src->rhs;
    }
    if (src->kind == ND_LVAR) {
      src_ptr = address_of_lvar(ctx, ((node_lvar_t *)src)->offset);
    } else if (src->kind == ND_DEREF) {
      ir_val_t p = build_expr(ctx, src->lhs);
      if (ctx->failed) return;
      src_ptr = p.id;
    } else if (src->kind == ND_FUNCALL) {
      /* `return make(...)`: >8B struct を返す funccall を直接 return する。
       * build_node_funcall は間接 struct 戻り値で ret_area を確保しそのアドレス (PTR)
       * を返すので、それを memcpy ソースにする (一旦変数へ入れると動いていた)。 */
      ir_val_t p = build_expr(ctx, src);
      if (ctx->failed) return;
      src_ptr = p.id;
    } else {
      fail(ctx, "struct return value is not LVAR/DEREF");
      return;
    }
    if (src_ptr < 0) return;
    ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
    cp->src1 = ir_val_vreg(ctx->f->ret_area_vreg, IR_TY_PTR);
    cp->src2 = ir_val_vreg(src_ptr, IR_TY_PTR);
    cp->alloca_size = ctx->f->ret_struct_size;
    ir_func_append_inst(ctx->f, cp);
    ir_inst_t *inst = ir_inst_new(IR_RET);
    inst->src1 = ir_val_none();
    ir_func_append_inst(ctx->f, inst);
    return;
  }
  /* _Complex 戻り値 (HFA): 戻り式を temp slot に {re,im} で materialize し、
   * IR_RET に slot の PTR と half を渡す (codegen が re→d0/s0, im→d1/s1)。 */
  if (ctx->f->ret_complex_half > 0 && node->lhs) {
    ir_type_t fp_ty = (ctx->f->ret_complex_half == 4) ? IR_TY_F32 : IR_TY_F64;
    int half = ctx->f->ret_complex_half;
    int slot = ir_func_new_vreg(ctx->f);
    ir_inst_t *al = ir_inst_new(IR_ALLOCA);
    al->dst = ir_val_vreg(slot, IR_TY_PTR);
    al->alloca_size = 2 * half;
    al->alloca_align = 8;
    ir_func_append_inst(ctx->f, al);
    build_complex_to(ctx, node->lhs, slot, fp_ty, half);
    if (ctx->failed) return;
    ir_inst_t *inst = ir_inst_new(IR_RET);
    inst->src1 = ir_val_vreg(slot, IR_TY_PTR);
    inst->ret_complex_half = (unsigned char)half;
    ir_func_append_inst(ctx->f, inst);
    return;
  }
  ir_val_t v = ir_val_none();
  if (node->lhs) {
    v = build_expr(ctx, node->lhs);
    if (ctx->failed) return;
    /* 戻り値を関数の戻り型へ変換する (C11 6.8.6.4: 代入と同じ変換)。
     * `double f(){ return 7; }` の int→double (I2F) などがここで挟まる。 */
    v = coerce_to_type(ctx, v, ctx->f->ret_type);
  } else {
    v = ir_val_imm(IR_TY_I32, 0);
  }
  ir_inst_t *inst = ir_inst_new(IR_RET);
  inst->src1 = v;
  ir_func_append_inst(ctx->f, inst);
}

static void build_stmt_if(ir_build_ctx_t *ctx, node_t *node) {
  node_ctrl_t *c = (node_ctrl_t *)node;
  ir_val_t cond = build_expr(ctx, node->lhs);
  if (ctx->failed) return;
  ir_block_t *then_b = ir_block_new(ctx->f);
  ir_block_t *merge_b = ir_block_new(ctx->f);
  ir_block_t *else_b = c->els ? ir_block_new(ctx->f) : merge_b;
  emit_br_cond(ctx, cond, then_b, else_b);
  /* then 節 */
  switch_to_new_block(ctx, then_b);
  build_stmt(ctx, node->rhs);
  if (ctx->failed) return;
  emit_br(ctx, merge_b);
  /* else 節 */
  if (c->els) {
    switch_to_new_block(ctx, else_b);
    build_stmt(ctx, c->els);
    if (ctx->failed) return;
    emit_br(ctx, merge_b);
  }
  switch_to_new_block(ctx, merge_b);
}

static void build_stmt_while(ir_build_ctx_t *ctx, node_t *node) {
  ir_block_t *cond_b = ir_block_new(ctx->f);
  ir_block_t *body_b = ir_block_new(ctx->f);
  ir_block_t *exit_b = ir_block_new(ctx->f);
  emit_br(ctx, cond_b);
  switch_to_new_block(ctx, cond_b);
  ir_val_t cv = build_expr(ctx, node->lhs);
  if (ctx->failed) return;
  emit_br_cond(ctx, cv, body_b, exit_b);
  push_loop(ctx, cond_b, exit_b);
  switch_to_new_block(ctx, body_b);
  build_stmt(ctx, node->rhs);
  pop_loop(ctx);
  if (ctx->failed) return;
  emit_br(ctx, cond_b);
  switch_to_new_block(ctx, exit_b);
}

static void build_stmt_do_while(ir_build_ctx_t *ctx, node_t *node) {
  ir_block_t *body_b = ir_block_new(ctx->f);
  ir_block_t *cond_b = ir_block_new(ctx->f);
  ir_block_t *exit_b = ir_block_new(ctx->f);
  emit_br(ctx, body_b);
  push_loop(ctx, cond_b, exit_b);
  switch_to_new_block(ctx, body_b);
  build_stmt(ctx, node->rhs);
  pop_loop(ctx);
  if (ctx->failed) return;
  emit_br(ctx, cond_b);
  switch_to_new_block(ctx, cond_b);
  ir_val_t cv = build_expr(ctx, node->lhs);
  if (ctx->failed) return;
  emit_br_cond(ctx, cv, body_b, exit_b);
  switch_to_new_block(ctx, exit_b);
}

static void build_stmt_for(ir_build_ctx_t *ctx, node_t *node) {
  node_ctrl_t *c = (node_ctrl_t *)node;
  if (c->init) {
    build_stmt(ctx, c->init);
    if (ctx->failed) return;
  }
  ir_block_t *cond_b = ir_block_new(ctx->f);
  ir_block_t *body_b = ir_block_new(ctx->f);
  ir_block_t *step_b = ir_block_new(ctx->f);
  ir_block_t *exit_b = ir_block_new(ctx->f);
  emit_br(ctx, cond_b);
  switch_to_new_block(ctx, cond_b);
  if (node->lhs) {
    ir_val_t cv = build_expr(ctx, node->lhs);
    if (ctx->failed) return;
    emit_br_cond(ctx, cv, body_b, exit_b);
  } else {
    /* 条件式なし: 無条件で body へ */
    emit_br(ctx, body_b);
  }
  push_loop(ctx, step_b, exit_b);
  switch_to_new_block(ctx, body_b);
  build_stmt(ctx, node->rhs);
  pop_loop(ctx);
  if (ctx->failed) return;
  emit_br(ctx, step_b);
  switch_to_new_block(ctx, step_b);
  if (c->inc) {
    build_stmt(ctx, c->inc);
    if (ctx->failed) return;
  }
  emit_br(ctx, cond_b);
  switch_to_new_block(ctx, exit_b);
}

static void build_stmt_switch(ir_build_ctx_t *ctx, node_t *node) {
  /* 制御値を eval。連続比較方式で各 case と比較し、一致すれば対応 block へ
   * 飛ぶ。最後に default (なければ end) へ無条件 jump。
   * body 内の ND_CASE/ND_DEFAULT は IR_LABEL として配置される。
   * break はループスタックの break_block に登録した end_block へ。 */
  ir_val_t control = build_expr(ctx, node->lhs);
  if (ctx->failed) return;
  /* 退避: 外側 switch の case_map を保存 */
  case_map_entry_t saved[MAX_CASES];
  int saved_count = ctx->case_map_count;
  memcpy(saved, ctx->case_map, sizeof(case_map_entry_t) * (size_t)saved_count);
  ctx->case_map_count = 0;
  /* collect: body 内の case/default に block を割り当て */
  node_case_t *cases[MAX_CASES];
  int case_count = 0;
  node_default_t *default_node = NULL;
  collect_case_labels(ctx, node->rhs, cases, &case_count, &default_node);
  ir_block_t *end_block = ir_block_new(ctx->f);
  /* dispatch */
  for (int i = 0; i < case_count; i++) {
    int v_cmp = ir_func_new_vreg(ctx->f);
    ir_inst_t *eq = ir_inst_new(IR_EQ);
    eq->dst = ir_val_vreg(v_cmp, IR_TY_I32);
    eq->src1 = control;
    eq->src2 = ir_val_imm(IR_TY_I32, cases[i]->val);
    ir_func_append_inst(ctx->f, eq);
    ir_block_t *case_b = find_case_block(ctx, cases[i]);
    ir_block_t *next_b = ir_block_new(ctx->f);
    ir_inst_t *brc = ir_inst_new(IR_BR_COND);
    brc->src1 = ir_val_vreg(v_cmp, IR_TY_I32);
    brc->label_id = case_b->id;
    brc->else_label_id = next_b->id;
    ir_func_append_inst(ctx->f, brc);
    switch_to_new_block(ctx, next_b);
  }
  /* 全 case 不一致なら default or end へ */
  ir_block_t *fallback = default_node ? find_case_block(ctx, default_node) : end_block;
  ir_inst_t *br = ir_inst_new(IR_BR);
  br->label_id = fallback->id;
  ir_func_append_inst(ctx->f, br);
  /* body (case/default は build_stmt 内で switch_to_new_block)。
   * switch 内の continue は外側ループの continue_block へ抜ける必要があるので、
   * 外側のものを継承する。 */
  ir_block_t *outer_cont = (ctx->loop_depth > 0)
                             ? ctx->loops[ctx->loop_depth - 1].continue_block
                             : NULL;
  push_loop(ctx, outer_cont, end_block);
  build_stmt(ctx, node->rhs);
  pop_loop(ctx);
  if (!ctx->failed) emit_br(ctx, end_block);
  switch_to_new_block(ctx, end_block);
  /* case_map を復元 */
  ctx->case_map_count = saved_count;
  memcpy(ctx->case_map, saved, sizeof(case_map_entry_t) * (size_t)saved_count);
}

static void build_stmt_case_default(ir_build_ctx_t *ctx, node_t *node) {
  ir_block_t *case_b = find_case_block(ctx, node);
  if (!case_b) {
    fail(ctx, "case/default outside switch");
    return;
  }
  emit_br(ctx, case_b);
  switch_to_new_block(ctx, case_b);
  /* ag_c の parser は `case N: stmt` を ND_CASE.rhs = stmt として格納する。
   * rhs (= ラベル後の文) を続けて build する。 */
  if (node->rhs) build_stmt(ctx, node->rhs);
}

static void build_stmt_break(ir_build_ctx_t *ctx, node_t *node) {
  (void)node;
  if (ctx->loop_depth == 0) {
    fail(ctx, "break outside loop");
    return;
  }
  emit_br(ctx, ctx->loops[ctx->loop_depth - 1].break_block);
  /* break 以降の文は到達不能だが、新しいブロックに移動して残りの文を別に保持。 */
  ir_block_t *dead = ir_block_new(ctx->f);
  switch_to_new_block(ctx, dead);
}

static void build_stmt_continue(ir_build_ctx_t *ctx, node_t *node) {
  (void)node;
  if (ctx->loop_depth == 0) {
    fail(ctx, "continue outside loop");
    return;
  }
  emit_br(ctx, ctx->loops[ctx->loop_depth - 1].continue_block);
  ir_block_t *dead = ir_block_new(ctx->f);
  switch_to_new_block(ctx, dead);
}

static void build_stmt_goto(ir_build_ctx_t *ctx, node_t *node) {
  node_jump_t *j = (node_jump_t *)node;
  ir_block_t *target = lookup_label_block(ctx, j->name, j->name_len);
  if (!target) {
    /* 通常は collect_labels で登録済みのはず。未登録の goto は不正。 */
    fail(ctx, "goto target label not found");
    return;
  }
  emit_br(ctx, target);
  ir_block_t *dead = ir_block_new(ctx->f);
  switch_to_new_block(ctx, dead);
}

static void build_stmt_label(ir_build_ctx_t *ctx, node_t *node) {
  node_jump_t *j = (node_jump_t *)node;
  ir_block_t *target = lookup_label_block(ctx, j->name, j->name_len);
  if (!target) {
    target = register_label_block(ctx, j->name, j->name_len);
    if (!target) return;
  }
  emit_br(ctx, target);
  switch_to_new_block(ctx, target);
  /* ラベル直後の文 (parser は `label: stmt` を ND_LABEL.rhs に格納) */
  if (node->rhs) build_stmt(ctx, node->rhs);
}

/* 仮引数を IR_PARAM で受け取り、frame slot に保存する (ALLOCA + STORE)。
 * struct 引数は呼び出し側がポインタを渡してくる前提で MEMCPY する。
 * 戻り値: 失敗時 0、成功時 1 (ctx->failed を立てるので 0 だけで判断可)。 */
static int setup_function_params(ir_build_ctx_t *ctx, node_func_t *fn) {
  int int_arg_idx = 0;
  int fp_arg_idx = 0;
  for (int i = 0; i < fn->nargs; i++) {
    node_t *arg = fn->args[i];
    if (!arg || arg->kind != ND_LVAR) {
      fail(ctx, "non-lvar parameter (Phase 4a unsupported)");
      return 0;
    }
    node_lvar_t *lv = (node_lvar_t *)arg;
    lvar_t *owner = find_owning_lvar(ctx, lv->offset);
    int param_full_size = owner && owner->size > 0 ? owner->size : lv->mem.type_size;
    /* caller と同じ判定: struct/union 値で 1/2/4/8 でないサイズ (3/5/6/7) は
     * アドレス渡しで受け取る (register 値ロードだと先頭メンバ幅しか復元できない)。 */
    int struct_param_needs_ptr =
        owner && owner->tag_kind != TK_EOF && !owner->is_tag_pointer &&
        !owner->is_array && owner->pointer_qual_levels == 0 &&
        param_full_size != 1 && param_full_size != 2 &&
        param_full_size != 4 && param_full_size != 8;
    if ((param_full_size > 8 || struct_param_needs_ptr) && !(owner && owner->is_complex)) {
      /* struct 引数 (Apple ARM64 ABI 簡略版): 呼び出し側が一時 buffer に copy
       * したポインタを x{int_idx} で渡してくる前提。 */
      int param_vreg = ir_func_new_vreg(ctx->f);
      ir_inst_t *p = ir_inst_new(IR_PARAM);
      p->dst = ir_val_vreg(param_vreg, IR_TY_PTR);
      p->src1 = ir_val_imm(IR_TY_I32, int_arg_idx++);
      ir_func_append_inst(ctx->f, p);
      int slot_vreg = address_of_lvar(ctx, lv->offset);
      if (slot_vreg < 0) return 0;
      ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
      cp->src1 = ir_val_vreg(slot_vreg, IR_TY_PTR);
      cp->src2 = ir_val_vreg(param_vreg, IR_TY_PTR);
      cp->alloca_size = param_full_size;
      ir_func_append_inst(ctx->f, cp);
      continue;
    }
    if (owner && owner->is_complex) {
      /* _Complex 引数 (HFA): re→d{n}/s{n}, im→d{n+1}/s{n+1} の 2 FP レジスタで
       * 受け取り、slot+0 / slot+half に格納する (AAPCS64)。 */
      ir_type_t pty = (owner->fp_kind == TK_FLOAT_KIND_FLOAT) ? IR_TY_F32 : IR_TY_F64;
      int half = (pty == IR_TY_F32) ? 4 : 8;
      int base_ptr = address_of_lvar(ctx, lv->offset);
      if (base_ptr < 0) return 0;
      for (int part = 0; part < 2; part++) {
        int pv = ir_func_new_vreg(ctx->f);
        ir_inst_t *p = ir_inst_new(IR_PARAM);
        p->dst = ir_val_vreg(pv, pty);
        p->src1 = ir_val_imm(IR_TY_I32, fp_arg_idx++);
        ir_func_append_inst(ctx->f, p);
        int dst_p = base_ptr;
        if (part == 1) {
          int lp = ir_func_new_vreg(ctx->f);
          ir_inst_t *lea = ir_inst_new(IR_LEA);
          lea->dst = ir_val_vreg(lp, IR_TY_PTR);
          lea->src1 = ir_val_vreg(base_ptr, IR_TY_PTR);
          lea->src2 = ir_val_imm(IR_TY_I32, half);
          ir_func_append_inst(ctx->f, lea);
          dst_p = lp;
        }
        ir_inst_t *st = ir_inst_new(IR_STORE);
        st->src1 = ir_val_vreg(dst_p, IR_TY_PTR);
        st->src2 = ir_val_vreg(pv, pty);
        ir_func_append_inst(ctx->f, st);
      }
      continue;
    }
    ir_type_t vty = lvar_value_type(lv);
    int reg_idx = is_fp_type(vty) ? fp_arg_idx++ : int_arg_idx++;
    int param_vreg = ir_func_new_vreg(ctx->f);
    ir_inst_t *p = ir_inst_new(IR_PARAM);
    p->dst = ir_val_vreg(param_vreg, vty);
    p->src1 = ir_val_imm(IR_TY_I32, reg_idx);
    ir_func_append_inst(ctx->f, p);
    int ptr_vreg = address_of_lvar(ctx, lv->offset);
    if (ptr_vreg < 0) return 0;
    ir_inst_t *st = ir_inst_new(IR_STORE);
    st->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
    st->src2 = ir_val_vreg(param_vreg, vty);
    ir_func_append_inst(ctx->f, st);
  }
  return 1;
}

/* 2D VLA-as-param (`int g[n][m]`) のため row stride を関数 entry で計算:
 *   *[vla_row_stride_frame_off] = *[vla_row_stride_src_offset] * elem_size
 * src は先行パラメータ (例 m) で、既に setup_function_params で frame slot に
 * 格納済み。subscript の make_subscript_scaled_offset が vla_rsf 経路で読む。 */
static int emit_vla_row_stride_for_params(ir_build_ctx_t *ctx, node_func_t *fn) {
  for (lvar_t *var = fn->lvars; var; var = var->next_all) {
    if (!var->is_param) continue;
    if (!var->is_vla) continue;
    /* 行ストライドスロット (__rs_<param>) があるかだけで判定する。
     * vla_row_stride_src_offset == 0 は「内側次元が第1パラメータ (offset 0)」の
     * 正当なケースであり、これを「未設定」と誤判定して行ストライド計算を飛ばすと
     * `int f(int n,int a[n][n])` で未初期化スロットを読み SIGSEGV していた。
     * frame_off と src_offset は register_vla_array_param で対で設定される。 */
    if (var->vla_row_stride_frame_off == 0) continue;
    int src_ptr = address_of_lvar(ctx, var->vla_row_stride_src_offset);
    if (src_ptr < 0) return 0;
    int v_m = ir_func_new_vreg(ctx->f);
    ir_inst_t *ld = ir_inst_new(IR_LOAD);
    ld->dst = ir_val_vreg(v_m, IR_TY_I32);
    ld->src1 = ir_val_vreg(src_ptr, IR_TY_PTR);
    ir_func_append_inst(ctx->f, ld);
    /* v_stride = m * elem_size (i32 域内で計算) */
    int v_stride = emit_binop(ctx, IR_MUL,
                              ir_val_vreg(v_m, IR_TY_I32),
                              ir_val_imm(IR_TY_I32, var->vla_row_stride_elem_size),
                              IR_TY_I32);
    /* zero-extend to i64 for store as pointer-step (frame slot は 8B) */
    int v_s64 = ir_func_new_vreg(ctx->f);
    ir_inst_t *zx = ir_inst_new(IR_ZEXT);
    zx->dst = ir_val_vreg(v_s64, IR_TY_I64);
    zx->src1 = ir_val_vreg(v_stride, IR_TY_I32);
    ir_func_append_inst(ctx->f, zx);
    int rs_ptr = address_of_lvar(ctx, var->vla_row_stride_frame_off);
    if (rs_ptr < 0) return 0;
    ir_inst_t *st2 = ir_inst_new(IR_STORE);
    st2->src1 = ir_val_vreg(rs_ptr, IR_TY_PTR);
    st2->src2 = ir_val_vreg(v_s64, IR_TY_I64);
    ir_func_append_inst(ctx->f, st2);
  }
  return 1;
}

/* 関数本体末尾に必要に応じて暗黙 `return 0` を補う。
 * main: 末尾が IR_RET でなければ補う (AST 直 codegen 互換)。
 * main 以外: 末尾が IR_RET / IR_BR でなければ安全のため補う
 *           (ABI 上 caller が戻り値を期待していなければ実害なし)。 */
static void emit_implicit_return_if_missing(ir_build_ctx_t *ctx, node_func_t *fn) {
  int is_main = (fn->funcname_len == 4 &&
                 fn->funcname && memcmp(fn->funcname, "main", 4) == 0);
  ir_inst_t *tail = ctx->f->cur_block ? ctx->f->cur_block->tail : NULL;
  ir_op_t tail_op = tail ? tail->op : IR_NOP;
  int needs_ret = is_main
      ? (!tail || tail_op != IR_RET)
      : (!tail || (tail_op != IR_RET && tail_op != IR_BR));
  if (needs_ret) {
    ir_inst_t *r = ir_inst_new(IR_RET);
    r->src1 = ir_val_imm(IR_TY_I32, 0);
    ir_func_append_inst(ctx->f, r);
  }
}

static int build_function(ir_build_ctx_t *ctx, node_func_t *fn) {
  /* >8 個の引数: 9 個目以降は stack 渡し。idx >= 8 を IR_PARAM の src1 に渡し、
   * codegen 側で [x29 + total_size + (idx-8)*8] から load する。 */
  /* 関数戻り値型: fp_kind 対応 */
  ir_type_t ret_ty = ir_type_from_node(&fn->base);
  /* ポインタ戻り値 (`struct N *getp(...)` 等) は 8 バイト。i32 のままだと
   * return 時に coerce_to_type が i64 のポインタ値を i32 へ TRUNC して
   * 上位 32bit を捨ててしまう。 */
  if (ret_ty == IR_TY_I32 &&
      psx_ctx_get_function_ret_is_pointer(fn->funcname, fn->funcname_len)) {
    ret_ty = IR_TY_PTR;
  }
  /* long / long long 戻り値も 8 バイト。同様に i32 だと return 時に i64 値が
   * 切り詰められる (`long add(long,long){ return a+b; }` 等)。 */
  if (ret_ty == IR_TY_I32) {
    token_kind_t rk = psx_ctx_get_function_ret_token_kind(fn->funcname, fn->funcname_len);
    if (rk != TK_EOF && psx_ctx_scalar_type_size(rk) >= 8) {
      ret_ty = IR_TY_I64;
    }
  }
  ctx->f = ir_func_new(ctx->m, fn->funcname, fn->funcname_len, ret_ty);
  /* _Complex 戻り値 (HFA): re→d0/s0, im→d1/s1。half=8(double)/4(float)。 */
  if (fn->base.is_complex) {
    ctx->f->ret_complex_half = (fn->base.fp_kind == TK_FLOAT_KIND_FLOAT) ? 4 : 8;
  }
  ctx->f->is_variadic = fn->is_variadic;
  ctx->f->is_static = fn->is_static;
  ctx->f->nargs_fixed = fn->nargs;
  ctx->cur_fn = fn;
  ctx->lvar_count = 0;
  ctx->loop_depth = 0;
  ctx->label_count = 0;
  /* >8B struct 戻り値 (および 3/5/6/7B の非 clean サイズ) の関数では prologue で
   * x8 を受け取る (Apple ARM64 ABI 簡略版)。1/2/4/8B のみ x0 で 1 レジスタ返却。
   * 非 clean サイズを scalar 返ししていたため `{char;short;uchar}` 戻り値が先頭
   * メンバ幅 (1B) しか復元できず壊れていた。 */
  ctx->f->ret_struct_size =
      cg_size_needs_indirect_struct(fn->base.ret_struct_size) ? fn->base.ret_struct_size : 0;
  if (ctx->f->ret_struct_size > 0) {
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *p = ir_inst_new(IR_PARAM);
    p->dst = ir_val_vreg(v, IR_TY_PTR);
    /* src1.imm = -1 で「x8 を受け取る」を表す。codegen で特別扱い。 */
    p->src1 = ir_val_imm(IR_TY_I32, -1);
    ir_func_append_inst(ctx->f, p);
    ctx->f->ret_area_vreg = v;
  }
  /* 仮引数: IR_PARAM で第 i 引数を受け取り、ALLOCA + STORE で frame slot に保存。
   * 以降の本体で LVAR 参照されたときに通常の LOAD が走るようになる。 */
  if (!setup_function_params(ctx, fn)) return 0;
  if (!emit_vla_row_stride_for_params(ctx, fn)) return 0;
  /* 全ローカル変数の ALLOCA をエントリブロックで前もって発行する。
   * 遅延発行だと「最初の参照が分岐内」のとき、未到達経路では vreg が
   * 未初期化となり、別経路から参照すると壊れる (struct ternary 等)。
   * fn->lvars には全スコープの lvar が next_all で連なっている。 */
  for (lvar_t *var = fn->lvars; var; var = var->next_all) {
    if (var->is_param) continue;  /* parameter は既に param/alloca/store した */
    (void)alloca_for_owner(ctx, var);
    if (ctx->failed) return 0;
  }
  /* goto 前方参照対応: 本体内の全 ND_LABEL に IR block を事前割り当て */
  collect_labels(ctx, fn->base.rhs);
  if (ctx->failed) return 0;
  build_stmt(ctx, fn->base.rhs);
  if (ctx->failed) return 0;
  emit_implicit_return_if_missing(ctx, fn);
  return 1;
}

ir_module_t *ir_build_module(node_t **code) {
  ir_build_ctx_t ctx = {0};
  ctx.m = ir_module_new();
  if (!code) return ctx.m;
  for (int i = 0; code[i]; i++) {
    node_t *n = code[i];
    if (n->kind != ND_FUNCDEF) {
      fail(&ctx, "top-level node is not a function definition");
      return NULL;
    }
    if (!build_function(&ctx, (node_func_t *)n)) return NULL;
  }
  return ctx.m;
}

static int g_ir_dump_enabled(void) {
  const char *e = getenv("AG_DUMP_IR");
  return e && strcmp(e, "1") == 0;
}

/* 関数 1 つを「単一関数モジュールへ build → emit (最適化+codegen) → 即解放」する。
 * 関数ごとストリーミングの中核。全関数の IR を同時保持しないので IR のピークメモリは
 * 「最大の 1 関数」になる。関数間に IR 依存はない (呼び出しは sym 名で解決) ため、
 * 出力はバッチ版 (ir_build_module) と一致する。emit は通常 gen_ir_module
 * (1 関数モジュールに const_fold/dce + gen_func)。fn は ND_FUNCDEF。成功 1 / エラー 0。 */
int ir_build_emit_function(node_t *fn, void (*emit_module)(ir_module_t *)) {
  if (!fn || fn->kind != ND_FUNCDEF) return 0;
  ir_build_ctx_t ctx = {0};
  ctx.m = ir_module_new();
  if (!build_function(&ctx, (node_func_t *)fn)) {
    ir_module_free(ctx.m);
    return 0;
  }
  if (g_ir_dump_enabled()) {
    char *buf = malloc(1 << 16);
    ir_print_module_to_buf(ctx.m, buf, 1 << 16);
    fprintf(stderr, "%s", buf);
    free(buf);
  }
  if (emit_module) emit_module(ctx.m);
  ir_module_free(ctx.m);
  return 1;
}

int ir_build_each_and_emit(node_t **code, void (*emit_module)(ir_module_t *)) {
  if (!code) return 1;
  for (int i = 0; code[i]; i++) {
    if (!ir_build_emit_function(code[i], emit_module)) return 0;
  }
  return 1;
}
