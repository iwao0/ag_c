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

#include "../continuation_options.h"
#include "../ir/ir.h"
#include "../target_info.h"

struct node_t;

typedef struct {
  const ag_target_info_t *target;
  const ag_continuation_options_t *continuation;
} ir_build_options_t;

/* AST 列 (NULL 終端) を IR モジュールに変換する。
 * 変換不可なら NULL を返す。エラーメッセージは stderr に出す。
 * (全関数の IR を一括保持する。テスト・IR ダンプ用。) */
ir_module_t *ir_build_module(struct node_t **code);
ir_module_t *ir_build_module_for_target(
    struct node_t **code, const ag_target_info_t *target);
ir_module_t *ir_build_module_with_options(
    struct node_t **code, const ir_build_options_t *options);

/* 関数ごとストリーミング版: 各関数を「単一関数モジュールへ build → emit_module で
 * 最適化+codegen → 即解放」で 1 つずつ処理し、IR のピークメモリを最大 1 関数分に抑える。
 * 出力はバッチ版 (ir_build_module + emit) と一致する。成功 1 / エラー 0。 */
int ir_build_each_and_emit(struct node_t **code, void (*emit_module)(ir_module_t *));
int ir_build_each_and_emit_for_target(
    struct node_t **code, const ag_target_info_t *target,
    void (*emit_module)(ir_module_t *));
int ir_build_each_and_emit_with_options(
    struct node_t **code, const ir_build_options_t *options,
    void (*emit_module)(ir_module_t *));

/* 上記の 1 関数版。frontend item streamと組み合わせ、関数を 1 つ
 * パースするたびに呼んで AST も関数ごとに解放できるようにする。fn は ND_FUNCDEF。 */
int ir_build_emit_function(struct node_t *fn, void (*emit_module)(ir_module_t *));
int ir_build_emit_function_for_target(
    struct node_t *fn, const ag_target_info_t *target,
    void (*emit_module)(ir_module_t *));
int ir_build_emit_function_with_options(
    struct node_t *fn, const ir_build_options_t *options,
    void (*emit_module)(ir_module_t *));

/* 1 関数だけを IR モジュールへ変換して返す。呼び出し側が直接 codegen したい経路用。
 * 成功時は ir_module_free で解放する。変換不可なら NULL。 */
ir_module_t *ir_build_function_module(struct node_t *fn);
ir_module_t *ir_build_function_module_for_target(
    struct node_t *fn, const ag_target_info_t *target);
ir_module_t *ir_build_function_module_with_options(
    struct node_t *fn, const ir_build_options_t *options);

#endif /* AG_IR_BUILDER_H */
