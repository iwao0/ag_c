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
 * 変換不可なら NULL を返す。エラーメッセージは stderr に出す。
 * (全関数の IR を一括保持する。テスト・IR ダンプ用。) */
ir_module_t *ir_build_module(struct node_t **code);

/* 関数ごとストリーミング版: 各関数を「単一関数モジュールへ build → emit_module で
 * 最適化+codegen → 即解放」で 1 つずつ処理し、IR のピークメモリを最大 1 関数分に抑える。
 * 出力はバッチ版 (ir_build_module + emit) と一致する。成功 1 / エラー 0。 */
int ir_build_each_and_emit(struct node_t **code, void (*emit_module)(ir_module_t *));

/* 上記の 1 関数版。ストリーミングパース (ps_next_function) と組み合わせ、関数を 1 つ
 * パースするたびに呼んで AST も関数ごとに解放できるようにする。fn は ND_FUNCDEF。 */
int ir_build_emit_function(struct node_t *fn, void (*emit_module)(ir_module_t *));

#endif /* AG_IR_BUILDER_H */
