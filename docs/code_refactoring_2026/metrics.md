# metrics.md

着手前と各 Phase 完了時のコードメトリクススナップショット。

## 着手前 (2026-06-11, commit 9a7349f)

### ファイルサイズ (`wc -l`)

| ファイル | 行数 |
|---|---:|
| src/parser/expr.c | 3300 |
| src/parser/decl.c | 2735 |
| src/parser/parser.c | 2353 |
| src/ir/ir_builder.c | 2122 |
| src/parser/semantic_ctx.c | 1046 |
| src/arch/arm64_apple_ir.c | 873 |
| src/parser/stmt.c | 534 |
| src/arch/arm64_apple.c | 443 |
| src/parser/node_utils.c | 434 |
| src/parser/struct_layout.c | 299 |
| src/ir/ir_regalloc.c | 268 |
| src/parser/enum_const.c | 254 |
| src/ir/ir_opt.c | 230 |
| src/ir/ir_print.c | 217 |
| **合計 (主要 14 ファイル)** | **15651** |

### ヘッダサイズ

| ファイル | 行数 |
|---|---:|
| src/parser/ast.h | 308 |
| src/ir/ir.h | 267 |
| src/parser/internal/semantic_ctx.h | 159 |
| src/parser/internal/decl.h | 109 |
| src/parser/internal/dynarray.h | 39 |
| src/parser/internal/node_utils.h | 26 |
| src/parser/internal/core.h | 23 |
| src/parser/parser.h | 22 |
| src/ir/ir_builder.h | 22 |

### 巨大関数 (実測)

| 関数 | ファイル:行 | 行数 |
|---|---|---:|
| `build_expr` | src/ir/ir_builder.c | **1055** |
| `psx_decl_parse_declaration_after_type_ex` | src/parser/decl.c:2138 | **364** |
| `build_stmt` | src/ir/ir_builder.c | 279 |
| `parse_param_decl` | src/parser/parser.c | 224 |
| `parse_struct_initializer` | src/parser/decl.c:1232 | 217 |
| `psx_decl_count_brace_init_elements` | src/parser/decl.c:400 | 51 |

### API 数

| API グループ | 数 |
|---|---:|
| `psx_ctx_get_tag_member_*` (取得系) | **5** |
| - `_at` (基本情報) | 1 |
| - `_bf` (bitfield) | 1 |
| - `_fp_kind` | 1 |
| - `_is_bool` | 1 |
| - `_count` | 1 |

### 重複パターン

| パターン | 出現数 |
|---|---:|
| `var->tag_kind = ` (decl.c) | 5 |
| `var->tag_kind = ` (parser.c) | 4 |
| `var->tag_kind = ` (合計) | **9** |

### モジュール境界

| 項目 | 値 |
|---|---|
| `src/ir/ir_builder.c` が include する parser ファイル | `../parser/ast.h`, `../parser/internal/semantic_ctx.h` |
| `src/arch/arm64_apple.c` が直接アクセスする parser 内部変数 | `global_vars` (グローバルリスト) |

### テスト状態

| 指標 | 値 |
|---|---|
| e2e | 820/820 |
| /tmp/probes | 246 件 diverged=0 |
| 最終 commit | `9a7349f` (parser: treat paren-grouped declarator with no [N] as plain pointer) |

---

## Phase A 完了時 (記入予定)

<!-- A1, A2, A3 完了時に追記 -->

## Phase B 完了時 (記入予定)

<!-- B1, B2, B3, B4 完了時に追記 -->

## Phase C 完了時 (記入予定)

<!-- C1, C2, C3, C4 完了時に追記 -->

---

## 達成度サマリ (全 Phase 完了時に追記)

| 指標 | 着手前 | 着手後 | 削減率 |
|---|---:|---:|---:|
| 最大関数行数 | 1055 | ? | ? |
| tag_member API 数 | 5 | ? | ? |
| `var->tag_kind = ` 重複 | 9 | ? | ? |
| ast.h | 308 | ? | ? |
| IR の parser/internal include | あり | ? | - |
