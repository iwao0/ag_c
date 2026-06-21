#ifndef PARSER_PUBLIC_H
#define PARSER_PUBLIC_H

/* parser モジュールが IR / arch 等の外部に公開する API (Phase C2)。
 *
 * 役割: IR (src/ir/) や arch (src/arch/) は parser 内部 (internal/) を
 * 直接 include せず、必ず本ヘッダを経由する。これにより parser 内部の
 * 再構成 (関数名変更、ヘッダ統廃合) を IR 側に波及させずに済む。
 *
 * 現状の公開シンボル:
 *   - lvar_t (型のみ。IR 側 find_owning_lvar が offset/size/next_all を
 *     直接読む。将来的に opaque 化を検討するが Phase C2 では据置)
 *   - ps_node_is_pointer / ps_node_deref_size (node_utils 由来)
 *   - psx_ctx_get_function_is_variadic / _param_fp_kind (semantic_ctx)
 *   - tag_member_info_t + psx_ctx_get_tag_member_count / _info
 *     (codegen が global struct/union を展開するのに必要)
 *
 * 非公開: tokenizer 内部、parser 自体の解析関数 (ps_program 等)、
 * decl 登録系 (psx_decl_register_lvar_*)、semantic_ctx の登録系 setter。
 * これら internal シンボルは外部から見えるが「契約上」非公開扱いとし、
 * IR / arch から直接 include しないことで境界を担保する。
 */

#include "ast.h"        /* node_t, node_lvar_t 等 */
#include "symtab.h"     /* global_var_t */
#include "decl.h"  /* lvar_t — Phase C2 では内部含むが、IR からは
                              本ヘッダ越しにしか触らない契約とする */
#include <stdbool.h>

/* node_utils.h からの公開 */
int ps_node_is_pointer(node_t *n);
int ps_node_deref_size(node_t *n);
int ps_node_is_unsigned(node_t *n);
int ps_node_type_size(node_t *n);

/* グローバル変数リスト走査 (Phase C3)。
 * codegen は global_vars リストを直接舐めず、本 visitor 経由で iterate する。
 * 走査順序は parser が登録した順 (FIFO ではなく LIFO: 後で登録した方が先)。 */
typedef void (*global_var_visitor_t)(global_var_t *gv, void *user);
void codegen_iter_globals(global_var_visitor_t fn, void *user);

/* 文字列リテラル / 浮動小数リテラルテーブル走査。global_vars 同様、
 * codegen は本 visitor 経由で iterate する。リストが空なら fn は呼ばれない。
 * 戻り値: リストが 1 つでもあれば true。preflight (section header を出すか
 * 判断する) で空判定に使う。 */
typedef void (*string_lit_visitor_t)(string_lit_t *lit, void *user);
typedef void (*float_lit_visitor_t)(float_lit_t *lit, void *user);
bool codegen_iter_string_literals(string_lit_visitor_t fn, void *user);
bool codegen_iter_float_literals(float_lit_visitor_t fn, void *user);
/* 空判定クエリ (リスト走査前に section header を出すかを判断するのに使う)。 */
bool codegen_has_string_literals(void);
bool codegen_has_float_literals(void);

/* semantic_ctx.h からの公開:
 * - 関数呼出側 IR が必要とする psx_ctx_get_function_is_variadic /
 *   _get_function_param_fp_kind
 * - codegen (arm64_apple.c) が global struct/union 初期化子を展開する
 *   ための tag_member_info_t と psx_ctx_get_tag_member_count /
 *   _get_tag_member_info (Phase A1 統合 API)
 * 重複定義回避のため internal/semantic_ctx.h を transitive include して
 * これらシンボルを取り込む。 */
#include "semantic_ctx.h"

#endif
