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

## Phase A 完了時 (2026-06-11, commit `176fcee`)

### ファイルサイズ (`wc -l`)

| ファイル | 着手前 | Phase A 完了 | 差分 |
|---|---:|---:|---:|
| src/parser/expr.c | 3300 | 3271 | -29 |
| src/parser/decl.c | 2735 | 2668 | -67 |
| src/parser/parser.c | 2353 | 2339 | -14 |
| src/ir/ir_builder.c | 2122 | 2121 | -1 |
| src/parser/semantic_ctx.c | 1046 | 1110 | +64 (新 API 2 つ追加) |
| src/arch/arm64_apple.c | 443 | 437 | -6 |
| src/parser/struct_layout.c | 299 | 293 | -6 |

### ヘッダサイズ

| ファイル | 着手前 | Phase A 完了 | 差分 |
|---|---:|---:|---:|
| src/parser/internal/semantic_ctx.h | 159 | 178 | +19 (新 API +2 / 旧 API -5) |
| src/parser/internal/decl.h | 109 | 129 | +20 (set helper +2) |

### 巨大関数

| 関数 | 着手前 | Phase A 完了 | 差分 |
|---|---:|---:|---:|
| `build_expr` | 1055 | 1047 | -8 |
| `psx_decl_parse_declaration_after_type_ex` | 364 | 349 | -15 |

(Phase B での本格分割は未着手)

### API 数

| API グループ | 着手前 | Phase A 完了 |
|---|---:|---:|
| `psx_ctx_get_tag_member_*` (public 取得系) | 5 | **3** (`_count`, `_get_info`, `_find_info`) |
| - 旧 `_at` / `_bf` / `_fp_kind` / `_is_bool` / `_find` | 5 (public) | 5 (file-scope static, 内部実装のみ) |

### 重複パターン

| パターン | 着手前 | Phase A 完了 |
|---|---:|---:|
| `var->tag_kind = ` 4 行ブロック (decl.c) | 5 | **0** |
| `var->tag_kind = ` 4 行ブロック (parser.c) | 4 | **0** |
| `fp_kind == TK_FLOAT_KIND_FLOAT` 判定 (ir_builder.c) | 9 | **1** (ヘルパ内のみ) |

### モジュール境界

(Phase C 着手前なので状態維持)

| 項目 | 値 |
|---|---|
| `src/ir/ir_builder.c` が include する parser ファイル | `../parser/ast.h`, `../parser/internal/semantic_ctx.h` |
| `src/arch/arm64_apple.c` が直接アクセスする parser 内部変数 | `global_vars` (グローバルリスト) |

### テスト状態

| 指標 | 値 |
|---|---|
| e2e | 820/820 |
| /tmp/probes | 246 件 diverged=0 |
| Phase A 最終 commit | `176fcee` |


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
