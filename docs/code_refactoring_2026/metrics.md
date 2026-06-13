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


## Phase B 完了時 (2026-06-11, commit `b2c58d0`)

### 巨大関数 (実測)

| 関数 | 着手前 | Phase A 完了 | Phase B 完了 | 削減 |
|---|---:|---:|---:|---:|
| `build_expr` | 1055 | 1047 | **34** | -1021 (97%) |
| `psx_decl_parse_declaration_after_type_ex` | 364 | 349 | **207** | -157 (43%) |
| `parse_struct_initializer` | 217 | 217 | **166** | -51 (24%) |
| `parse_array_initializer` | 197 | 197 | **157** | -40 (20%) |
| `gen_global_vars` | 177 | 177 | **109** | -68 (38%) |

**目標達成**: 「最大関数行数 1055 → < 300」を build_expr で達成 (34 行)。

### ファイルサイズ (`wc -l`)

| ファイル | 着手前 | Phase B 完了 | 差分 |
|---|---:|---:|---:|
| src/parser/expr.c | 3300 | 3271 | -29 |
| src/parser/decl.c | 2735 | 2722 | -13 |
| src/parser/parser.c | 2353 | 2339 | -14 |
| src/ir/ir_builder.c | 2122 | 2179 | +57 (helper 追加分) |
| src/parser/semantic_ctx.c | 1046 | 1110 | +64 (新 API 追加分) |
| src/arch/arm64_apple.c | 443 | 443 | 0 |
| src/parser/struct_layout.c | 299 | 293 | -6 |

### 新設 helper 関数

Phase B で 24 個の static helper を新設 (内訳は phase_b_walkthrough.md
を参照)。最大の貢献は build_expr 分割の 18 helper。

### モジュール境界

(Phase C 着手前なので変化なし)

| 項目 | 値 |
|---|---|
| `src/ir/ir_builder.c` が include する parser ファイル | `../parser/ast.h`, `../parser/internal/semantic_ctx.h` |
| `src/arch/arm64_apple.c` が直接アクセスする parser 内部変数 | `global_vars` (グローバルリスト) |

### テスト状態

| 指標 | 値 |
|---|---|
| e2e | 820/820 |
| /tmp/probes | 246 件 diverged=0 |
| Phase B 最終 commit | `b2c58d0` |


## Phase C 完了時 (2026-06-11, commit `8c8166c`)

### ヘッダサイズ

| ファイル | 着手前 | Phase C 完了 | 差分 |
|---|---:|---:|---:|
| src/parser/ast.h | 308 | **237** | -71 (symtab 分離 + shim 撤去) |
| src/parser/symtab.h | — | **86** | 新規 (連結リスト型 symtab) |
| src/parser/parser_public.h | — | **50** | 新規 (parser → IR/arch ファサード) |
| src/parser/internal/decl.h | 109 | 132 | +23 (set_var/gvar_tag helper + symtab include) |
| src/parser/internal/semantic_ctx.h | 159 | 178 | +19 (統合 API 2 つ) |

### モジュール境界

| 項目 | 着手前 | Phase C 完了 |
|---|---|---|
| `grep "parser/internal" src/ir/` | あり (3 件) | **0 件** |
| `grep "parser/internal" src/arch/` | あり (1 件) | **0 件** |
| `arm64_apple.c` から `global_vars` 変数への直接参照 | 1 箇所 (`for` ループ) | **0 件** (visitor 経由) |
| ast.h と symtab の分離 | 混在 | 分離 (`ast.h`: AST、`symtab.h`: 連結リスト) |

### IR / arch が依存するヘッダ

| モジュール | 着手前 | Phase C 完了 |
|---|---|---|
| `src/ir/ir_builder.c` | `parser/ast.h`, `parser/internal/decl.h`, `parser/internal/semantic_ctx.h`, `parser/internal/node_utils.h` | **`parser/parser_public.h` のみ** |
| `src/arch/arm64_apple.c` | `parser/parser.h`, `parser/internal/semantic_ctx.h` | **`parser/symtab.h` + `parser/parser_public.h`** |

### ファイルサイズ全体 (Phase B 終 → Phase C 終)

| ファイル | Phase B 完了 | Phase C 完了 | 差分 |
|---|---:|---:|---:|
| src/parser/parser.c | 2339 | 2349 | +10 (codegen_iter_globals 実装) |
| src/arch/arm64_apple.c | 443 | 453 | +10 (visitor 関数追加) |
| 他主要ファイル | - | - | 不変 |

### テスト状態

| 指標 | 値 |
|---|---|
| e2e | 820/820 |
| /tmp/probes | 246 件 diverged=0 |
| Phase C 最終 commit | `8c8166c` |

---

## 達成度サマリ (Phase A/B/C 全完了、2026-06-11)

| 指標 | 着手前 | 完了後 | 達成度 |
|---|---:|---:|---|
| 最大関数行数 (`build_expr`) | 1055 | **34** | -1021 (97% 削減) 目標 < 300 ✓ |
| `psx_decl_parse_declaration_after_type_ex` | 364 | 207 | -157 (43%) 目標 < 150 △ |
| `parse_struct_initializer` | 217 | 166 | -51 (24%) 目標 < 200 ✓ |
| `parse_array_initializer` | 197 | 157 | -40 (20%) 目標 < 200 ✓ |
| `gen_global_vars` | 177 | **109** | -68 (38%) 目標 < 150 ✓ |
| tag_member 取得系 public API 数 | 5 | **3** (`_count` + `_get_info` + `_find_info`) | 統合完了 ✓ |
| `var->tag_kind = ` 4 行重複 | 9 | **0** | 全置換完了 ✓ |
| ir_builder.c fp_kind 判定 | 9 箇所散在 | **1** (ヘルパ内) | 集約完了 ✓ |
| `ast.h` | 308 行 (混在) | **237** (AST のみ) + symtab.h 86 (新規) | 分離完了 ✓ |
| IR の parser/internal include | あり (3 件) | **なし** (parser_public.h 経由) | 境界明確化 ✓ |
| arch の parser/internal include | あり (1 件) | **なし** (parser_public.h 経由) | 境界明確化 ✓ |
| arm64_apple.c から global_vars 直接参照 | あり | **なし** (codegen_iter_globals visitor) | 結合緩和 ✓ |

### 計画通り達成しなかった項目
- `psx_decl_parse_declaration_after_type_ex` の `< 150 行` 目標は 207 行で停止。
  残る分岐 (funcptr-array / paren-array-pointer / scalar) は個別の小ブロックの
  集合体で、これ以上の helper 化は call-site のシグネチャ膨張が savings を上回る判断。
- A3-3 `propagate_pointee_flags` 抽出: 実コードに「3 フィールド一括コピー」パターンが
  存在しないため、無理な抽出を避けてスキップ。詳細は `task.md` の A3-3 行を参照。

### 全 Phase の commit 統計

| Phase | refactor commit | docs commit | 合計 |
|---|---:|---:|---:|
| Phase A (保守性改善) | 9 | 10 (初期計画含む) | 19 |
| Phase B (巨大関数分割) | 11 | 12 | 23 |
| Phase C (モジュール境界) | 9 | 11 | 20 |
| **合計** | **29** | **33** | **62** |

全 commit で `make test` 820/820 と /tmp/probes diverged=0 を維持。
