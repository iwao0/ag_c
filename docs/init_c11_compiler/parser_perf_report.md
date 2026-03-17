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
- 追加ファイル（第2段）:
  - `src/parser/parser_node_utils.h`
  - `src/parser/parser_node_utils.c`
- 置換:
  - ASTノード生成・型サイズ計算・lvalue検証を `parser_node_utils` へ移管
- 追加ファイル（第3段）:
  - `src/parser/parser_decl.h`
  - `src/parser/parser_decl.c`
- 置換（第3段）:
  - ローカル変数表と宣言パース（`declaration` / `declaration_after_type`）を `parser_decl` へ移管
  - `parser_consume_type_kind` / `parser_assign_expr` を公開し、宣言モジュールから再利用
- 追加ファイル（第4段）:
  - `src/parser/parser_stmt.h`
  - `src/parser/parser_stmt.c`
  - `src/parser/parser_loop_ctx.h`
  - `src/parser/parser_loop_ctx.c`
- 置換（第4段）:
  - `stmt` 本体を `parser_stmt` へ移管
  - `loop_depth` を `parser_loop_ctx` へ移管
- ビルド連携:
  - `Makefile` に `PARSER_LIB_OBJS` を導入し、`test_parser` / `bench_parser` で再利用
- 回帰確認:
  - `build/test_parser` pass
  - `build/test_e2e` pass (`171/171`)

- 追加ファイル（第5段）:
  - `src/parser/parser_expr.h`
  - `src/parser/parser_expr.c`
- 置換（第5段）:
  - `expr/assign/conditional/logical/bit/equality/relational/shift/add/mul/unary/primary` を `parser_expr` へ移管
  - `parser.c` の `expr()` は `pexpr_expr()` への委譲に縮小
  - `parser_decl` の初期化式パース呼び出しを `pexpr_assign()` に統一
- 回帰確認（第5段）:
  - `build/test_parser` pass
  - `build/test_e2e` pass (`171/171`)

## 2026-03-17 Hot Path Tuning (Phase 3-1)

- 実施:
  - `parser_expr` の演算子判定を `tk_consume*` 連鎖から `token->kind` 直接分岐へ置換
  - `assign/equality/relational/shift/add/mul/unary/postfix` の分岐でトークン消費コストを削減
  - `primary` の頻出ケース（`TK_NUM`）を先頭で処理
- 回帰確認:
  - `make build/test_parser build/test_e2e && build/test_parser && build/test_e2e` pass

### Benchmark (`build/bench_parser`, `-O0`, 1st run)

| Case | Parser (Before) | Parser (After) | Δ |
|---|---:|---:|---:|
| mixed (16KB) parser_MB/s | 13.12 | 22.53 | +71.7% |
| mixed (256KB) parser_MB/s | 20.74 | 27.03 | +30.3% |
| expr-heavy (256KB) parser_MB/s | 22.14 | 24.49 | +10.6% |
| control-heavy (256KB) parser_MB/s | 28.05 | 28.92 | +3.1% |

- 判定:
  - 目標（`mixed 256KB` +10〜20%, `expr-heavy` +8%以上）は達成
  - `control-heavy` は小幅改善だが、回帰ガードレール（-5%以内）を満たす

## 2026-03-17 Local Type-Info Cache (Phase 3-2)

- 実施:
  - `pctx_get_type_info(kind, &is_type, &size)` を追加し、型トークン判定とサイズ判定を1回の分岐で取得
  - `parser_expr` の cast/`sizeof(type)` 判定で上記APIを利用
  - `parser_decl` の宣言サイズ決定で上記APIを利用
- 回帰確認:
  - `make build/test_parser build/test_e2e && build/test_parser && build/test_e2e` pass

### Benchmark (`build/bench_parser`, `-O0`)

| Case | Parser (Before) | Parser (After) | Δ |
|---|---:|---:|---:|
| mixed (16KB) parser_MB/s | 22.53 | 22.49 | -0.2% |
| mixed (256KB) parser_MB/s | 27.03 | 26.69 | -1.3% |
| expr-heavy (256KB) parser_MB/s | 24.49 | 23.83 | -2.7% |
| control-heavy (256KB) parser_MB/s | 28.92 | 28.11 | -2.8% |

- 判定:
  - 小幅な変動に留まり、回帰ガードレール（-5%）以内
  - 保守性側の狙い（重複判定の集約）を優先して採用

## 2026-03-17 Dynamic Array Growth Policy (Phase 3-3)

- 実施:
  - `parser_dynarray.h` を追加し、容量拡張ロジックを `pda_next_cap()` に統一
  - `program` / 関数引数配列 / block文配列 / 関数呼び出し引数 / switch case 値配列に適用
  - 初期容量を `16` に引き上げて中規模入力での `realloc` 回数を抑制
- 回帰確認:
  - `make build/test_parser build/test_e2e build/bench_parser && build/test_parser && build/test_e2e` pass

### Benchmark (`build/bench_parser`, `-O0`)

| Case | Parser (Before) | Parser (After) | Δ |
|---|---:|---:|---:|
| mixed (16KB) parser_MB/s | 22.49 | 21.59 | -4.0% |
| mixed (256KB) parser_MB/s | 26.69 | 27.28 | +2.2% |
| expr-heavy (256KB) parser_MB/s | 23.83 | 24.32 | +2.1% |
| control-heavy (256KB) parser_MB/s | 28.11 | 27.10 | -3.6% |

- 判定:
  - ケース間で増減はあるが、全ケースで回帰ガードレール（-5%）以内
  - 保守性（容量成長ロジックの統一）と `realloc` 抑制のため採用

## 2026-03-17 Lookup Hashing (Phase 4-1/4-2)

- 実施:
  - `parser_semantic_ctx` の `goto` 参照検証を、ラベル名ハッシュバケット探索へ置換
  - ラベル重複チェックも同じハッシュバケットで実施
  - `struct/union/enum` タグ型参照 (`pctx_has_tag_type`) をハッシュ探索へ置換
- 回帰確認:
  - `make build/test_parser build/test_e2e build/bench_parser && build/test_parser && build/test_e2e` pass

### Benchmark (`build/bench_parser`, `-O0`)

| Case | Parser (Before) | Parser (After) | Δ |
|---|---:|---:|---:|
| mixed (16KB) parser_MB/s | 21.59 | 39.04 | +80.8% |
| mixed (256KB) parser_MB/s | 27.28 | 52.69 | +93.1% |
| expr-heavy (256KB) parser_MB/s | 24.32 | 54.27 | +123.1% |
| control-heavy (256KB) parser_MB/s | 27.10 | 61.94 | +128.6% |

- 判定:
  - 全ケースで改善を確認
  - フェーズ4の残作業は「異常系診断精度の再確認」

## 2026-03-17 Diagnostic Precision Recheck (Phase 4-3)

- 実施:
  - `test_parse_invalid_diagnostics` を追加し、異常系メッセージ本文を直接検証
  - 検証対象:
    - 未定義ラベル（`goto MISSING`）
    - 重複ラベル（`L1:` 再定義）
    - 未定義タグ型（`struct T` 参照）
- 回帰確認:
  - `build/test_parser` pass
  - `build/test_e2e` pass (`171/171`)

### Benchmark (`build/bench_parser`, `-O0`)

| Case | Parser (Before) | Parser (After) | Δ |
|---|---:|---:|---:|
| mixed (16KB) parser_MB/s | 39.04 | 42.20 | +8.1% |
| mixed (256KB) parser_MB/s | 52.69 | 51.99 | -1.3% |
| expr-heavy (256KB) parser_MB/s | 54.27 | 53.57 | -1.3% |
| control-heavy (256KB) parser_MB/s | 61.94 | 60.34 | -2.6% |

- 判定:
  - メッセージ精度の追加検証を導入しつつ、性能は回帰ガードレール（-5%）以内

## 2026-03-17 Diagnostic Pattern Unification (Phase 5-1)

- 実施:
  - `src/parser/parser_diag.{h,c}` を追加し、診断メッセージの共通生成関数を導入
  - `parser_stmt`/`parser_switch_ctx`/`parser_semantic_ctx` の一部診断を共通関数へ移行
  - 既存メッセージ語彙は維持し、呼び出し側の重複を削減
- 回帰確認:
  - `build/test_parser` pass
  - `build/test_e2e` pass (`171/171`)

### Benchmark (`build/bench_parser`, `-O0`)

| Case | Parser (Before) | Parser (After) | Δ |
|---|---:|---:|---:|
| mixed (16KB) parser_MB/s | 42.20 | 45.91 | +8.8% |
| mixed (256KB) parser_MB/s | 51.99 | 55.15 | +6.1% |
| expr-heavy (256KB) parser_MB/s | 53.57 | 52.84 | -1.4% |
| control-heavy (256KB) parser_MB/s | 60.34 | 62.64 | +3.8% |

- 判定:
  - 保守性改善を達成し、性能は全ケースで回帰ガードレール（-5%）以内

## 2026-03-17 Contextual Error Format (Phase 5-2)

- 実施:
  - `pdiag_ctx(tok, rule, fmt, ...)` を追加し、`[rule] detail` 形式の診断フォーマットを導入
  - `parser.c`（funcdef）, `parser_decl.c`（decl）, `parser_expr.c`（cast/sizeof/primary）, `parser_semantic_ctx.c`（goto）に段階適用
  - 既存メッセージ語彙は維持しつつ、失敗規則コンテキストを追加
- 回帰確認:
  - `build/test_parser` pass
  - `build/test_e2e` pass (`171/171`)

### Benchmark (`build/bench_parser`, `-O0`)

| Case | Parser (Before) | Parser (After) | Δ |
|---|---:|---:|---:|
| mixed (16KB) parser_MB/s | 45.91 | 69.27 | +50.9% |
| mixed (256KB) parser_MB/s | 55.15 | 75.54 | +37.0% |
| expr-heavy (256KB) parser_MB/s | 52.84 | 67.96 | +28.6% |
| control-heavy (256KB) parser_MB/s | 62.64 | 71.36 | +13.9% |

- 判定:
  - コンテキスト付き診断フォーマット統一を達成
  - 性能は全ケースで回帰ガードレール（-5%）を満たす

## 2026-03-17 Message-Dependent Test Alignment (Phase 5-3)

- 実施:
  - `test_parse_invalid_diagnostics` を新フォーマットに追従
  - 期待文字列を `[rule]` 付きへ更新
    - `[goto] 未定義ラベル ...`
    - `[parser] ラベル ... が重複`
    - `[parser] 未定義のタグ型 ...`
- 回帰確認:
  - `build/test_parser` pass
  - `build/test_e2e` pass (`171/171`)

### Benchmark (`build/bench_parser`, `-O0`)

| Case | Parser (Before) | Parser (After) | Δ |
|---|---:|---:|---:|
| mixed (16KB) parser_MB/s | 69.27 | 23.97 | -65.4% |
| mixed (256KB) parser_MB/s | 75.54 | 39.80 | -47.3% |
| expr-heavy (256KB) parser_MB/s | 67.96 | 46.15 | -32.1% |
| control-heavy (256KB) parser_MB/s | 71.36 | 56.58 | -20.7% |

- 注記:
  - このステップの変更はテスト文字列のみで、実行コードは無変更
  - 上記の大幅差分は環境ノイズの可能性が高いため再計測を実施

### Benchmark (`build/bench_parser`, `-O0`, 2nd run)

| Case | Parser (Before) | Parser (After) | Δ |
|---|---:|---:|---:|
| mixed (16KB) parser_MB/s | 69.27 | 43.01 | -37.9% |
| mixed (256KB) parser_MB/s | 75.54 | 54.68 | -27.6% |
| expr-heavy (256KB) parser_MB/s | 67.96 | 54.19 | -20.3% |
| control-heavy (256KB) parser_MB/s | 71.36 | 57.20 | -19.8% |

- 再計測判定:
  - 1st run より改善するが、依然として差分が大きくベンチ安定性に課題がある
  - 本変更自体はテストコードのみのため、次フェーズで複数回実行の中央値記録へ運用改善する

## 2026-03-17 CI Parser Perf Guard (Phase 6-1)

- 実施:
  - `scripts/check_parser_perf.sh` を追加
  - `.github/workflows/ci.yml` の `bench-and-guards` ジョブに parser perf guard を追加
  - `bench.out` から以下ケースの `parser_MB/s` / `funcs/sec` を検証
    - `mixed 262200b`
    - `expr-heavy 262176b`
    - `control-heavy 262185b`
  - しきい値は環境変数で上書き可能にしてCI調整を容易化
- ローカル確認:
  - `make bench | tee /tmp/bench.out`
  - `bash scripts/check_tokenizer_perf.sh /tmp/bench.out`
  - `bash scripts/check_parser_perf.sh /tmp/bench.out`
  - すべて pass
