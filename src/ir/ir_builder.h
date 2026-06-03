/*
 * AST → IR ビルダー (Phase 2)。
 *
 * 対応 AST node はまだ最小サブセット:
 *   ND_FUNCDEF (main 等), ND_BLOCK, ND_RETURN, ND_NUM, ND_LVAR,
 *   ND_ASSIGN, ND_ADD/SUB/MUL/MOD/DIV
 *
 * サポート外の node に当たると ir_build_module は NULL を返す。
 */

#ifndef AG_IR_BUILDER_H
#define AG_IR_BUILDER_H

#include "ir.h"

struct node_t;

/* AST 列 (NULL 終端) を IR モジュールに変換する。
 * 変換不可なら NULL を返す。エラーメッセージは stderr に出す。 */
ir_module_t *ir_build_module(struct node_t **code);

#endif /* AG_IR_BUILDER_H */
