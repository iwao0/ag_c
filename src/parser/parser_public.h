#ifndef PARSER_PUBLIC_H
#define PARSER_PUBLIC_H

/* parser モジュールが IR / arch 等の外部に公開する API (Phase C2)。
 *
 * 役割: IR (src/ir/) や arch (src/arch/) は parser 内部 (internal/) を
 * 直接 include せず、必ず本ヘッダを経由する。これにより parser 内部の
 * 再構成 (関数名変更、ヘッダ統廃合) を IR 側に波及させずに済む。
 *
 * 現状の公開シンボル:
 *   - lvar_t (型のみ。フィールド直接アクセスは IR 内 find_owning_lvar
 *     等で行われる。将来的に opaque 化を検討するが Phase C2 では据置)
 *   - psx_node_is_pointer / psx_node_deref_size
 *   - psx_ctx_get_function_is_variadic
 *   - psx_ctx_get_function_param_fp_kind
 *
 * 非公開: tokenizer 内部、parser 自体の解析関数 (ps_program 等)、
 * decl 登録系 (psx_decl_register_lvar_*)、semantic_ctx の登録系 setter。
 */

#include "ast.h"        /* node_t, node_lvar_t 等 */
#include "internal/decl.h"  /* lvar_t — Phase C2 では内部含むが、IR からは
                              本ヘッダ越しにしか触らない契約とする */
#include <stdbool.h>

/* node_utils.h からの公開 */
int psx_node_is_pointer(node_t *n);
int psx_node_deref_size(node_t *n);

/* semantic_ctx.h からの公開 (関数呼出側 IR が必要とする最小限) */
bool psx_ctx_get_function_is_variadic(char *name, int len, int *out_nargs_fixed);
tk_float_kind_t psx_ctx_get_function_param_fp_kind(char *name, int len, int param_idx);

#endif
