# code_refactoring_2026

ag_c (C11 コンパイラ) の構造的リファクタリング計画。Phase A → B → C の 3 段階で実施。

## 達成目標

- **保守性向上**: 巨大関数の分割、コード重複の解消、API の整合化
- **モジュール境界明確化**: IR が parser 内部を覗かない、AST とシンボルテーブルの分離
- **行数削減 + 関数サイズ削減**: 最大関数 1055 → < 300 行を目指す
- **API 数削減**: tag_member 取得 API 5 → 1

## 非目標 (明確に対象外)

- **機能追加**: 新たな C 言語機能の対応はしない
- **IR 表現変更**: IR の命令種・型は据置
- **e2e 仕様変更**: テストの期待値は変えない
- **codegen ターゲット追加**: arm64 Apple のみ。マルチターゲット (x86_64 等) は別計画 (`docs/ir_intermediate_representation/` Phase 8)
- **パフォーマンス最適化**: 速度改善は本計画の主目的ではない

## 厳守事項

- **全 Phase で e2e 820/820 維持**: 各 commit 後に `make test` を必ず実行
- **/tmp/probes diverged=0 維持**: 各 Phase 完了時に probe diff sweep
- **機能変更ゼロ**: 入力 C ソースに対する ag_c の出力 (asm + exit code) は完全に同じ
- **1 Step = 1 commit**: 中途半端な状態を残さない (テスト失敗を含めない)

## 構成ファイル

| ファイル | 内容 |
|---|---|
| `README.md` | 本ファイル (達成目標、非目標、厳守事項) |
| `implementation_plan.md` | Phase A/B/C の詳細計画 |
| `task.md` | 階層チェックリスト (Phase → Step、commit hash で完了記録) |
| `metrics.md` | 着手前メトリクス + 各 Phase 完了時スナップショット |
| `phase_a_walkthrough.md` (Phase A 完了時) | Phase A の変更内容と効果報告 |
| `phase_b_walkthrough.md` (Phase B 完了時) | 同上 |
| `phase_c_walkthrough.md` (Phase C 完了時) | 同上 |

## 前提

- 開始時点: e2e 820/820 全パス、/tmp/probes 246 件 diverged=0、最終 commit `9a7349f`
- 開発環境: macOS Darwin 24.6.0, arm64 Apple
- ビルド: `make build/ag_c` / テスト: `make test` / probe sweep: `cd /tmp/probes && bash run_diff.sh`

## 関連 docs

- `docs/init_c11_compiler/` — C11 コンパイラ初期実装記録
- `docs/ast_refactoring/` — AST/Token 構造の整理 (完了)
- `docs/error_handling_redesign/` — エラーメッセージ一元管理 (本計画の docs フォーマット参考)
- `docs/ir_intermediate_representation/` — IR 導入計画 (Phase 8 が本計画 C2/C3 に依存)

## 完了時の状態

- Phase A 完了: 保守性改善 (重複削減、API 統合、ヘルパ抽出) で 820/820 維持
- Phase B 完了: 巨大関数分割で最大関数 < 300 行
- Phase C 完了: モジュール境界明確化で IR Phase 8 着手可能
