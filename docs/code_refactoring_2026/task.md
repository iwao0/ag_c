# task.md

各 Step は 1 commit。完了時に `[ ]` → `[x]` + commit hash + 日付を記録。

## Phase A: 保守性改善 (commit 数: 10, 6-9h)

### A1: tag_member API 統合 (commit 数: 4)
- [x] **A1-1** `tag_member_info_t` 構造体と `psx_ctx_get_tag_member_info` 新規追加 (2026-06-11)
- [x] **A1-2** `src/parser/decl.c` の呼び出し移行 (tag_find_member, tag_get_member_at) — f47982d (2026-06-11)
- [x] **A1-3** `src/parser/expr.c` と `src/ir/ir_builder.c` の呼び出し移行 — 2fbd3ce (2026-06-11) ※ir_builder.c は該当呼出なし
- [x] **A1-4** 旧 5 API のうち unused になったもの削除 — 2065e0c (2026-06-11)

### A2: lvar/var タグ設定ヘルパ化 (commit 数: 3)
- [x] **A2-1** `psx_decl_set_var_tag` / `psx_decl_set_gvar_tag` ヘルパ定義 — b383df5 (2026-06-11)
- [x] **A2-2** `src/parser/decl.c` の 5 箇所置換 — b18ae86 (2026-06-11)
- [x] **A2-3** `src/parser/parser.c` の 4 箇所置換 (+ gv 1 箇所) — 2987fe4 (2026-06-11)

### A3: 型/フラグ伝播ヘルパ化 (commit 数: 3)
- [x] **A3-1** `ir_type_from_node` 抽出 (ir_builder.c) — 3960179 (2026-06-11)
- [x] **A3-2** `build_expr` 内 9 箇所を `ir_type_from_node` に置換 — f2ca9e8 (2026-06-11)
- [~] **A3-3** `propagate_pointee_flags` 抽出 — **スキップ** (2026-06-11)
  - 理由: 実 expr.c には計画前提の「fp_kind / is_scalar_ptr_member /
    pointee_is_bool の 3 フィールド一括コピー」パターンが存在せず、各 site は
    個別 flag を異なる条件で立てている。`is_scalar_ptr_member =` の代入は
    expr.c:937 / 2581 の 2 箇所のみで、いずれも単独 `= 1`。`pointee_is_bool =`
    も expr.c:956 / 2573 の 2 箇所のみで、それぞれ別文脈。実態に合わない抽出は
    複雑化のリスクがあるためスキップ。`build_lvar_or_vla_node` の lvar→mem
    フィールド転写ヘルパ化は計画と別物なので別途検討。

**Phase A 完了時**:
- [x] `phase_a_walkthrough.md` 作成 (commit hash 一覧、削減指標) — 2026-06-11
- [x] `metrics.md` 更新 — 2026-06-11

---

## Phase B: 巨大関数分割 (commit 数: 11, 16-26h)

### B1: `build_expr` 分割 (commit 数: 4)
- [x] **B1-1** dispatch 骨格 + lvalue 系 (8 種) 抽出 — 5897581 (2026-06-11) build_expr 1047→594
- [x] **B1-2** 算術/比較系 (14 種) 抽出 — 4deb283 (2026-06-11) build_expr 594→433
- [ ] **B1-3** 制御系 (6 種) 抽出
- [ ] **B1-4** 残余抽出 + switch 削除

### B2: brace 初期化系の分解 (commit 数: 3)
- [ ] **B2-1** `brace_init_walker_t` 構造体抽出
- [ ] **B2-2** スカラ/集約分岐分離
- [ ] **B2-3** 指示子と文字列リテラル分離

### B3: `psx_decl_parse_declaration_after_type_ex` 分解 (commit 数: 3)
- [ ] **B3-1** function-declarator 抽出
- [ ] **B3-2** array/pointer 抽出
- [ ] **B3-3** init-declarator 抽出

### B4: `gen_global_vars` 分割 (commit 数: 1)
- [ ] **B4-1** `emit_global_aggregate_init` helper 抽出

**Phase B 完了時**:
- [ ] `phase_b_walkthrough.md` 作成
- [ ] `metrics.md` 更新

---

## Phase C: モジュール境界 (commit 数: 10, 11-17h)

### C1: `ast.h` 分割 (commit 数: 4)
- [ ] **C1-1** `symtab.h` 切り出し + ast.h shim 化
- [ ] **C1-2** parser 内 include 個別化
- [ ] **C1-3** ir/arch include 個別化
- [ ] **C1-4** shim 撤去

### C2: parser → IR public I/F 抽出 (commit 数: 3)
- [ ] **C2-1** `parser_public.h` 新設
- [ ] **C2-2** IR の include 切替 (`../parser/internal/` → `../parser/parser_public.h`)
- [ ] **C2-3** internal/ ヘッダの visibility 強化

### C3: codegen と global_vars の結合緩和 (commit 数: 2)
- [ ] **C3-1** `codegen_iter_globals` iterator API 新設
- [ ] **C3-2** arm64_apple.c 移行

### C4: IR Phase 8 との整合確認 (commit 数: 1)
- [ ] **C4-1** Phase 8 計画と本計画の整合性記録 (docs のみ)

**Phase C 完了時**:
- [ ] `phase_c_walkthrough.md` 作成
- [ ] `metrics.md` 最終更新

---

## 全 Phase 完了時

- [ ] 全 Phase 完了報告を README.md 末尾に追記
- [ ] `metrics.md` に達成度サマリ
- [ ] (任意) ag_c の git tag `refactor-2026-complete` を作成

## 各 Step の commit メッセージテンプレート

```
refactor(<area>): <要約>

<本文: なぜこの Step が必要か、何を変えたか、何を変えてないか>

Phase <X> Step <Y> in docs/code_refactoring_2026/task.md.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```
