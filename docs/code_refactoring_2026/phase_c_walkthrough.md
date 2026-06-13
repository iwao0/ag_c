# phase_c_walkthrough.md

Phase C (モジュール境界明確化) の commit ログと効果報告。

## 概要

| 項目 | 値 |
|---|---|
| 期間 | 2026-06-11 (1 日) |
| 起点 commit | `41328c0` (Phase B closeout) |
| 完了 commit | `8c8166c` (C4-1 docs) |
| refactor commits | 9 |
| docs commits | 11 |
| e2e 状態 | 全 commit で 820/820 維持 |
| /tmp/probes 状態 | 全 commit で diverged=0 維持 |

## Step 一覧

| Step | 内容 | commit |
|---|---|---|
| C1-1 | `symtab.h` 切り出し + ast.h shim 化 | `50715bd` |
| C1-2 | parser internal/ ヘッダ include 個別化 (core.h → token.h、decl.h += symtab.h) | `294dec9` |
| C1-3 | arm64_apple.c の parser.h → symtab.h | `1dec010` |
| C1-4 | shim 撤去 (ast.h から symtab.h include を削除) | `a538103` |
| C2-1 | `parser_public.h` 新設 (parser → IR/arch の唯一窓口) | `3ef039c` |
| C2-2 | ir_builder.c の internal/* 3 つ → parser_public.h 1 つ | `b6bf9bb` |
| C2-3 | arm64_apple.c の internal/semantic_ctx.h → parser_public.h | `1f26d4d` |
| C3-1 | `codegen_iter_globals` visitor API 新設 | `b790945` |
| C3-2 | gen_global_vars を visitor 経由に切替 | `788374c` |
| C4-1 | IR Phase 8 計画との接続を README.md に記録 | `b94b174` |

## モジュール境界の変化

### 着手前
- `ast.h` 308 行: AST node 定義 + 連結リスト型シンボルテーブル (global_var_t / string_lit_t / float_lit_t) が混在
- `src/ir/ir_builder.c` が `../parser/internal/decl.h` / `../parser/internal/semantic_ctx.h` / `../parser/internal/node_utils.h` を直接 include
- `src/arch/arm64_apple.c` が `../parser/internal/semantic_ctx.h` を直接 include
- arm64_apple.c の `gen_global_vars` が parser-owned `global_vars` 変数を直接 walk

### Phase C 完了時
- `ast.h` 237 行: AST node 定義のみ
- `symtab.h` 86 行 (新規): 連結リスト型シンボルテーブル専用
- `parser_public.h` 50 行 (新規): parser → IR/arch の唯一窓口
- IR / arch の parser 内部 (`internal/`) への直接 include は **0 件**
- arm64_apple.c の `gen_global_vars` は `codegen_iter_globals` visitor 経由 (parser-owned 変数への直接アクセスゼロ)

### grep による境界検証

```text
$ grep -rn "parser/internal" src/ --include="*.c" --include="*.h" | grep -v "^src/parser/"
(0 件)

$ grep -n "global_vars\b" src/arch/arm64_apple.c
9: *   - gen_string_literals / gen_float_literals / gen_global_vars: parser が登録
338:/* gen_global_vars の本体: 1 つの global_var_t を assembly directive に
451:void gen_global_vars(void) {
(変数参照は 0 件、関数名 gen_global_vars への言及のみ)
```

## 新設したヘッダ / API

| ヘッダ | 役割 | 行数 |
|---|---|---:|
| `src/parser/symtab.h` | 連結リスト型シンボルテーブル | 86 |
| `src/parser/parser_public.h` | parser → IR/arch の唯一窓口 (facade) | 50 |

`parser_public.h` の公開シンボル:
- `lvar_t` (型のみ; IR `find_owning_lvar` が offset/size/next_all を読む)
- `psx_node_is_pointer` / `psx_node_deref_size`
- `psx_ctx_get_function_is_variadic` / `_get_function_param_fp_kind`
- `tag_member_info_t` + `psx_ctx_get_tag_member_count` / `_get_tag_member_info`
- `codegen_iter_globals` / `global_var_visitor_t` (C3-1 で追加)

## 設計判断 (計画との差異)

### C2: parser_public.h は internal/ を transitive include する形に
完全な opaque 化 (lvar_t / tag_member_info_t をフィールド非公開にして
accessor 関数で操作) は: (1) lvar_t の 60+ フィールド分の accessor を
要する、(2) `find_owning_lvar` が next_all で linked list を walk する
パターンが accessor 経由だと冗長化、というコスト懸念から見送り。
代わりに parser_public.h が `internal/decl.h` / `internal/semantic_ctx.h`
を transitive include する形を取り、「IR/arch は内部実装に依存して
よいが、include 経路は parser_public.h 1 本」という契約で境界を担保。

### C3: visitor API は parser 側に置く (`codegen_backend.h` ではなく `parser_public.h`)
計画では `codegen_backend.h` に置く案だったが、`codegen_iter_globals` は
parser-owned データの走査 API であり、コンセプト上 parser モジュール
の公開機能。codegen_backend.h は arm64_apple.c 等の codegen 実装が
提供する側のヘッダなので、責務上 parser_public.h の方が適切と判断。

## ファイルサイズ変化 (Phase B 終 → Phase C 終)

| ファイル | Phase B 完了 | Phase C 完了 | 差分 | 備考 |
|---|---:|---:|---:|---|
| src/parser/ast.h | 308 | 237 | -71 | symtab 分離 + shim 撤去 |
| src/parser/symtab.h | — | 86 | +86 | 新規 |
| src/parser/parser_public.h | — | 50 | +50 | 新規 |
| src/parser/parser.c | 2339 | 2349 | +10 | codegen_iter_globals 実装 |
| src/arch/arm64_apple.c | 443 | 453 | +10 | visitor 関数追加分 |
| src/ir/ir_builder.c | 2179 | 2179 | 0 | include 数のみ変化 |

Phase C は「行数最適化」ではなく「ヘッダ依存の整理と境界の明確化」が
目的なので、行数の純増は新ヘッダ分が中心。

## Phase C の効果

- **ヘッダ依存の整理**: IR / arch から parser/internal への直接参照が
  0 件に。parser 内部の構造変更が IR/arch に波及しない壁ができた
- **概念的な分離**: AST node 定義 (ast.h) と シンボルテーブル (symtab.h) が
  別ファイルになり、各々の責務が明確
- **IR Phase 8 着手準備**: マルチターゲット対応 (x86_64 backend 追加) に
  必要な前提 (parser ↔ codegen の境界明確化、共通 visitor API) が揃った

## 機能変更ゼロの検証

各 commit で以下を確認:
- `make build/ag_c` — コンパイル通過
- `make test` — e2e 820/820
- `cd /tmp/probes && bash run_diff.sh` — diverged=0

最終確認 (Phase C 完了時):
- e2e 820/820
- /tmp/probes 246 件 diverged=0
- ag_c の出力 asm / exit code に変化なし
