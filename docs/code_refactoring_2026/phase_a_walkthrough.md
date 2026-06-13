# phase_a_walkthrough.md

Phase A (保守性改善) の commit ログと効果報告。

## 概要

| 項目 | 値 |
|---|---|
| 期間 | 2026-06-11 (1 日) |
| 起点 commit | `48e2155` (計画 docs 追加) |
| 完了 commit | `176fcee` (A3-3 スキップ判断記録) |
| refactor commits | 9 |
| docs commits | 9 |
| e2e 状態 | 全 commit で 820/820 維持 |
| /tmp/probes 状態 | 全 commit で diverged=0 維持 |

## Step 一覧

| Step | 内容 | commit | 差分行数 |
|---|---|---|---:|
| A1-1 | `tag_member_info_t` 構造体 + `psx_ctx_get_tag_member_info` 新規追加 | `0e09c9b` | +44/-0 |
| A1-2 | `src/parser/decl.c` を統合 API に移行 + `psx_ctx_find_tag_member_info` 追加 | `f47982d` | +62/-83 |
| A1-3 | `src/parser/expr.c` を統合 API に移行 (ir_builder.c は該当呼出なし) | `2fbd3ce` | +31/-60 |
| A1-4 | 旧 5 getter を file-scope static 化、public API から削除 | `2065e0c` | +27/-48 |
| A2-1 | `psx_decl_set_var_tag` / `psx_decl_set_gvar_tag` ヘルパ定義 | `b383df5` | +20/-0 |
| A2-2 | decl.c 5 箇所の 4 行パターンを 1 行化 | `b18ae86` | +5/-20 |
| A2-3 | parser.c 4 lvar + 1 gv 箇所を 1 行化 | `2987fe4` | +6/-20 |
| A3-1 | `ir_type_from_node` ヘルパ定義 | `3960179` | +9/-0 |
| A3-2 | ir_builder.c の fp_kind 判定 9 箇所を helper 呼出に置換 | `f2ca9e8` | +34/-44 |
| A3-3 | `propagate_pointee_flags` 抽出 — **スキップ** (実コードに該当パターン無し) | (docs only) | — |

合計差分: **+238/-275** (refactor commits のみ、docs 除く)

## 主要メトリクス変化

| 指標 | 着手前 | Phase A 完了時 | 削減 |
|---|---:|---:|---:|
| `psx_ctx_get_tag_member_*` (public) | 5 | 2 (`_get_info` + `_find_info`) + `_count` 維持 | -3 |
| `var->tag_kind = ` 重複 (decl.c) | 5 | 0 | -5 |
| `var->tag_kind = ` 重複 (parser.c) | 4 | 0 | -4 |
| ir_builder.c の `fp_kind == TK_FLOAT_KIND_FLOAT` 判定 | 9 | 1 (ヘルパ内) | -8 |
| `src/parser/decl.c` | 2735 | 2668 | -67 |
| `src/parser/parser.c` | 2353 | 2339 | -14 |
| `src/parser/expr.c` | 3300 | 3271 | -29 |
| `src/ir/ir_builder.c` | 2122 | 2121 | -1 |
| `src/arch/arm64_apple.c` | 443 | 437 | -6 |
| `src/parser/struct_layout.c` | 299 | 293 | -6 |

## 設計判断

### A1-1 から外れた変更: `psx_ctx_find_tag_member_info` の追加
当初 A1-1 で index 検索版 `psx_ctx_get_tag_member_info` のみ追加していたが、
A1-2 で `tag_find_member` (name 検索) を統合 API に乗せ替えるため、name 検索版の
counterpart `psx_ctx_find_tag_member_info` を A1-2 で追加した。これにより
public API は `_count` + `_get_info` + `_find_info` の 3 つに集約。

### A3-3 のスキップ
計画前提の「fp_kind / is_scalar_ptr_member / pointee_is_bool の 3 フィールド
一括コピー」パターンが実 expr.c に存在しないことを確認 (各 site は個別 flag を
別条件で立てている)。実態に合わない抽出は複雑化のリスクがあるためスキップ。
詳細は `task.md` の A3-3 行を参照。

## Phase A の効果

- **API 削減**: 取得系 tag_member API 5 → 2 (+ count) で呼び出し側の冗長な
  3-tuple `(tag_kind, tag_name, tag_len)` 引数渡しを排除
- **重複削減**: 4 行の tag 設定パターン 9 箇所が 1 行ヘルパ呼出に集約
- **fp_kind 分岐の一元化**: ir_builder.c で 9 箇所散在していた `if FLOAT/DOUBLE`
  判定を `ir_type_from_node` 1 関数に集約
- **後続 Phase の土台**:
  - A1 完了は C2 (parser → IR public I/F 抽出) の前提
  - A3 完了は B1 (build_expr 分割) の前提 (fp_kind 落ち防止)

## 機能変更ゼロの検証

各 commit で以下を確認:
- `make build/ag_c` — コンパイル通過
- `make test` — e2e 820/820
- `cd /tmp/probes && bash run_diff.sh` — diverged=0

最終確認 (Phase A 完了時):
- e2e 820/820
- /tmp/probes 246 件 diverged=0
- ag_c の出力 asm / exit code に変化なし
