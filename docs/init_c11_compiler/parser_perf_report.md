# Parser Performance Report

## 2026-03-17 Baseline (`-O0`, Apple clang)

- Command: `build/bench_parser`
- Note: `tokenize` と `parser` を分離計測。スループット指標は `parser` 時間のみで算出。

| Case | Input Bytes | Funcs | Tokenize (s) | Parser (s) | Parser MB/s | Funcs/sec |
|---|---:|---:|---:|---:|---:|---:|
| mixed (16KB) | 16,416 | 288 | 0.001625 | 0.001193 | 13.12 | 241,408 |
| mixed (256KB) | 262,200 | 4,600 | 0.013408 | 0.012059 | 20.74 | 381,458 |
| expr-heavy (256KB) | 262,176 | 5,462 | 0.008209 | 0.011295 | 22.14 | 483,577 |
| control-heavy (256KB) | 262,185 | 3,405 | 0.007588 | 0.008913 | 28.05 | 382,026 |

## Interpretation

- 現時点では `parser` 単体で 13.12〜28.05 MB/s の範囲。
- `expr-heavy` / `control-heavy` で `funcs/sec` は 38万〜48万程度。
- 今後の最適化評価は、本レポートの `parser` 指標を基準値として比較する。

## 2026-03-17 Maintainability Baseline

- Command: `scripts/parser_maint_metrics.sh src/parser/parser.c`
- Scope: 関数長 / 分岐スコア / 3行ウィンドウ重複（ヒューリスティック）

### Summary

- functions: `22`
- max function length: `15` lines
- avg function length: `9.1` lines
- max branch score: `4`
- avg branch score: `0.7`

### Top Functions by Length

| Function | Start Line | Lines | Branch Score |
|---|---:|---:|---:|
| `parse_cast_type` | 353 | 15 | 4 |
| `program` | 397 | 15 | 0 |
| `register_switch_case` | 295 | 15 | 1 |
| `scalar_type_size` | 330 | 14 | 0 |
| `validate_goto_labels` | 247 | 14 | 1 |

### Top Duplicate 3-line Windows (Heuristic)

| Count | Snippet |
|---:|---|
| 10 | `} | return node; | }` |
| 5 | `else | return node; | }` |
| 5 | `return node; | } | }` |
| 4 | `tk_expect(';'); | return node; | }` |
| 4 | `token = token->next; | tk_expect('('); | node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));` |

## Optimization Targets & Guardrails

- Target window (Phase 1 -> Phase 3):
  - `mixed 256KB` の `parser_MB/s` を **+10%〜+20%** 改善
  - `expr-heavy 256KB` の `parser_MB/s` を **+8%以上** 改善
- Regression guardrail:
  - いずれかのケースで **-5%** を超える低下が出たら回帰候補として扱う
- Maintainability guardrail:
  - max function length を **180 行以下**（`check_function_size.sh` の方針）
  - 3行重複ウィンドウの上位件数は、根拠なしに **+20%** を超えて増やさない

## 2026-03-17 Refactor Progress (Phase 2)

- 実施: `parser_semantic_ctx` モジュールを追加し、以下を `parser.c` から分離
  - goto/label 参照管理
  - struct/union/enum タグ型管理
  - 型トークン判定・型サイズユーティリティ
- 追加ファイル:
  - `src/parser/parser_semantic_ctx.h`
  - `src/parser/parser_semantic_ctx.c`
- ビルド連携:
  - `Makefile` に `PARSER_LIB_OBJS` を導入し、`test_parser` / `bench_parser` で再利用
- 回帰確認:
  - `build/test_parser` pass
  - `build/test_e2e` pass (`171/171`)
