# phase_b_walkthrough.md

Phase B (巨大関数分割) の commit ログと効果報告。

## 概要

| 項目 | 値 |
|---|---|
| 期間 | 2026-06-11 (1 日) |
| 起点 commit | `cc2e675` (Phase A closeout) |
| 完了 commit | `b2c58d0` (B4-1 docs) |
| refactor commits | 11 |
| docs commits | 11 |
| e2e 状態 | 全 commit で 820/820 維持 |
| /tmp/probes 状態 | 全 commit で diverged=0 維持 |

## Step 一覧

| Step | 内容 | commit | 行数変化 |
|---|---|---|---:|
| B1-1 | build_expr 8 lvalue 系 case helper 抽出 (STRING/GVAR/NUM/LVAR/ASSIGN/ADDR/DEREF/FUNCREF) | `5897581` | build_expr 1047→594 |
| B1-2 | build_expr 14 算術/比較系 case を build_node_binop に集約 | `4deb283` | build_expr 594→433 |
| B1-3 | build_expr 制御系 helper (FUNCALL/COMMA/LOGAND_OR/TERNARY) | `14e9b1b` | build_expr 433→197 |
| B1-4 | build_expr 残余 5 case helper (FP_TO_INT/INC_DEC/VA_ARG_AREA/PTR_CAST/VLA_ALLOC) | `d6cce8a` | build_expr 197→34 |
| B2-1 | struct brace init 冒頭の zero-fill ヘルパ化 | `c60840f` | parse_struct_initializer 217→189 |
| B2-2 | 末尾の未割当スカラ補完を helper 化 | `fb64010` | parse_struct_initializer 189→166 |
| B2-3 | `char a[] = {"hello"}` 特例パスを helper 化 | `82c3b58` | parse_array_initializer 197→157 |
| B3-1 | VLA 分岐を register_vla_lvar_and_append_alloc に抽出 | `ecbcc83` | psx_decl_parse_declaration_after_type_ex 349→302 |
| B3-2 | 多次元 `[N]` 配列分岐を register_multidim_array_lvar に抽出 | `57afc2d` | 同 302→238 |
| B3-3 | typedef-配列分岐を register_typedef_array_lvar に抽出 | `2257113` | 同 238→207 |
| B4-1 | gen_global_vars から struct/struct-array brace init を分離 | `11e5919` | gen_global_vars 177→109 |

## 巨大関数のサイズ変化

| 関数 | Phase A 完了時 | Phase B 完了時 | 削減 | 削減率 |
|---|---:|---:|---:|---:|
| `build_expr` | 1047 | **34** | -1013 | 97% |
| `psx_decl_parse_declaration_after_type_ex` | 349 | **207** | -142 | 41% |
| `parse_struct_initializer` | 217 | **166** | -51 | 24% |
| `parse_array_initializer` | 197 | **157** | -40 | 20% |
| `gen_global_vars` | 177 | **109** | -68 | 38% |

## 新設した helper 関数 (24 個)

### build_expr 分割 (18 個)

lvalue 系 (8): `build_node_string`, `build_node_gvar`, `build_node_num`, `build_node_lvar`, `build_node_assign`, `build_node_addr`, `build_node_deref`, `build_node_funcref`

算術系 (1): `build_node_binop` (14 case を統合)

制御系 (4): `build_node_funcall`, `build_node_comma`, `build_node_logand_or`, `build_node_ternary`

残余 (5): `build_node_fp_to_int`, `build_node_inc_dec`, `build_node_va_arg_area`, `build_node_ptr_cast`, `build_node_vla_alloc`

### brace init 分解 (3 個)
- `append_struct_zero_fill_chain`
- `append_unassigned_scalar_zero_fills`
- `try_parse_array_braced_string_initializer`

### declaration_after_type_ex 分解 (3 個)
- `register_vla_lvar_and_append_alloc`
- `register_multidim_array_lvar`
- `register_typedef_array_lvar`

### gen_global_vars 分解 (2 個)
- `emit_global_struct_init`
- `emit_global_struct_array_init`

## 設計判断 (計画との差異)

### B2-1: walker 構造体 → 自己完結ブロック抽出
計画では `brace_init_walker_t` 構造体で内部状態を集約する想定だったが、
parse_struct_initializer / parse_array_initializer に「複数関数で共有
する状態」がほぼ存在しないため、構造体抽出ではなく自己完結したロジック
ブロック (zero-fill、未割当スカラ補完など) を順次 helper 化する方針に
変更。完了条件「各関数 < 200 行」は両関数とも達成。

### B3-3: 完了条件「< 150 行」未達 (207 行)
計画では psx_decl_parse_declaration_after_type_ex を < 150 行に下げる
目標だったが、3 helper 抽出後 207 行で止めた。残る分岐は funcptr-array
(22 行)、paren-array-pointer (8 行)、scalar (15 行) などの個別小ブロック
の集合体で、これ以上の helper 化は call-site のシグネチャ膨張が savings
を上回ると判断。

### B1-1〜B1-4: 計画通りの 4 commit 分割
build_expr 1055 → 34 行は予想以上の削減。dispatch のみの薄い switch に
なり、各 case の処理は独立した `build_node_<kind>` ヘルパで保守できる
形に整理された。

## ファイルサイズ変化

| ファイル | Phase A 完了時 | Phase B 完了時 | 差分 |
|---|---:|---:|---:|
| src/ir/ir_builder.c | 2121 | 2179 | +58 (helper 追加で微増) |
| src/parser/decl.c | 2668 | 2722 | +54 (helper 追加) |
| src/parser/parser.c | 2339 | 2339 | 0 |
| src/parser/expr.c | 3271 | 3271 | 0 |
| src/arch/arm64_apple.c | 437 | 443 | +6 |

Phase B は「関数を分割して可読性向上」が目的で、行数削減そのものは
非目標。helper を追加すると signature/comment で行数は微増する。

## Phase B の効果

- **最大関数の劇的縮小**: build_expr が 1047 行のモンスター関数から
  switch dispatch のみの 34 行に。各 case 処理は単独で理解・修正可能
- **完了条件達成**: 「最大関数行数 < 300 行」を build_expr で達成、
  目標を大きく上回る
- **後続 Phase の前提**: B1 完了 (build_expr 分割) は将来の dispatch
  table 化 (`docs/ir_intermediate_representation/` Phase 8) の素地

## 機能変更ゼロの検証

各 commit で以下を確認:
- `make build/ag_c` — コンパイル通過
- `make test` — e2e 820/820
- `cd /tmp/probes && bash run_diff.sh` — diverged=0

最終確認 (Phase B 完了時):
- e2e 820/820
- /tmp/probes 246 件 diverged=0
- ag_c の出力 asm / exit code に変化なし
