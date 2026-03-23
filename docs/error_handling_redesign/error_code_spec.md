# エラーコード仕様（初版）

## コード形式
- 形式: `E####`（4桁固定）
- 例: `E0001`, `E1004`, `E2008`

## 番号帯
- `E0000` - `E0999`: プログラム不具合（内部エラー）
- `E1000` - `E1999`: Preprocess（プリプロセッサ）エラー
- `E2000` - `E2999`: Tokenizer エラー
- `E3000` - `E3999`: Parser エラー
- `E4000` - `E4999`: Codegen（コード生成）エラー
- `E5000` 以降: 追加カテゴリ（将来拡張）

## 追加ルール
- 1つの意味に対して1コードを割り当てる（使い回し禁止）。
- 同じエラー条件を複数場所で出す場合も同じコードを使う。
- 文言変更（翻訳差分含む）はコードを変えない。
- 意味変更・判定条件変更時は新コードを発行する。
- 新規カテゴリが必要になった場合は `E5000` 以降に新しい番号帯を割り当てる。
- 新規カテゴリ追加時は、カテゴリ名・開始番号・終了番号を本ドキュメントに追記する。
- `main` の使い方エラー（例: 引数不足）は採番対象外とする。

## 言語同梱ポリシー（ビルド時選択）
- ビルド変数 `DIAG_LANG` で同梱言語を選択する。
  - `DIAG_LANG=ja`: 日本語のみ同梱（推奨・最小サイズ）
  - `DIAG_LANG=en`: 英語のみ同梱（未実装キーは最小フォールバック）
  - `DIAG_LANG=all`: 複数言語同梱（開発/検証用）
- 単一言語ビルドでは、実行時切替APIは互換維持のため残すが、実質固定動作とする。

## メッセージ定義の推奨フォーマット
- `code`: `E1001`
- `key`: `preprocess.invalid_directive`
- `default_ja`: `不正なプリプロセッサディレクティブです`
- `default_en`: `Invalid preprocessor directive`
- `category`: `PREPROCESS`

## 初期採番案

### INTERNAL（0000番台）
| Code | Key | 既定メッセージ（ja） |
| --- | --- | --- |
| E0001 | internal.out_of_memory | メモリ確保に失敗しました |
| E0002 | internal.unreachable | 到達不能な状態です |
| E0003 | internal.invalid_state | 内部状態が不正です |

### PREPROCESS（1000番台）
| Code | Key | 既定メッセージ（ja） |
| --- | --- | --- |
| E1001 | preprocess.invalid_directive | 不正なプリプロセッサディレクティブです |
| E1002 | preprocess.invalid_include | include ファイル指定が不正です |
| E1003 | preprocess.cond_expr_error | #if 条件式が不正です |
| E1004 | preprocess.unterminated_conditional | 条件付きディレクティブが閉じられていません |
| E1005 | preprocess.macro_definition_error | マクロ定義が不正です |

### TOKENIZER（2000番台）
| Code | Key | 既定メッセージ（ja） |
| --- | --- | --- |
| E2001 | tokenizer.unexpected_char | 不正な文字です |
| E2002 | tokenizer.token_too_long | トークン長が上限を超えています |
| E2003 | tokenizer.unterminated_string | 文字列リテラルが閉じられていません |
| E2004 | tokenizer.invalid_number | 数値リテラルが不正です |
| E2005 | tokenizer.invalid_escape | エスケープシーケンスが不正です |

### PARSER（3000番台）
| Code | Key | 既定メッセージ（ja） |
| --- | --- | --- |
| E3001 | parser.expected_token | 必要なトークンがありません |
| E3002 | parser.unexpected_token | 予期しないトークンです |
| E3003 | parser.undefined_symbol | 未定義の識別子です |
| E3004 | parser.duplicate_symbol | 識別子が重複しています |
| E3005 | parser.invalid_context | この文脈では使用できません |

### CODEGEN（4000番台）
| Code | Key | 既定メッセージ（ja） |
| --- | --- | --- |
| E4001 | codegen.output_failed | コード生成出力に失敗しました |
| E4002 | codegen.invalid_lvalue | 代入の左辺値が不正です |
| E4003 | codegen.invalid_control_flow | 制御フローの使用位置が不正です |

## 出力例
- `main.c:12: E3001: ';' が必要です (actual: 'if')`
- `E0001: メモリ確保に失敗しました`

## 診断のパス表示ポリシー（情報漏えい抑制）
- 診断メッセージ内に絶対パスをそのまま出さない。
- CLI引数由来の入力パスは、絶対パスの場合はファイル名のみ表示する。
- `#include` 由来の表示は、ソースに記載された相対表記（表示用パス）を優先し、`realpath` 結果は表示しない。
- トークン位置のファイル名表示（`file:line`）も、外部入力由来の絶対パスを避ける方針とする。

## 警告コード仕様

### コード形式
- 形式: `W####`（4桁固定）
- 例: `W3001`, `W3002`

### 番号帯
- `W0000` - `W0999`: Internal
- `W1000` - `W1999`: Preprocess
- `W2000` - `W2999`: Tokenizer
- `W3000` - `W3999`: Parser
- `W4000` - `W4999`: Codegen

### 採番ルール（運用）
- 原則: カテゴリ内で小さい未使用番号を採用する（欠番は再利用しない）。
- 1つの意味に対して1コードを割り当てる（使い回し禁止）。
- 文言変更（翻訳差分含む）ではコードを変えない。
- 意味変更・判定条件変更時は新コードを発行する。
- `E####` と `W####` は独立運用する（同じ下4桁を強制しない）。
- 新規カテゴリが必要になった場合は `W5000` 以降に新しい番号帯を割り当てる。

### 追加手順（チェックリスト）
1. 追加先カテゴリを決める（Internal / Preprocess / Tokenizer / Parser / Codegen / 追加カテゴリ）。
2. 対応する番号帯の未使用 `W####` を採番する。
3. `src/diag/warning_catalog.h` に warning ID を追加する。
4. `src/diag/warning_catalog.c` に `code/key` を追加する。
5. `src/diag/messages_ja.c` / `src/diag/messages_en.c` に文言を追加する。
6. 呼び出し側を `diag_warn_tokf(...)`（または将来の warning API）で置換する。
7. テスト（既存 + 追加ケース）を更新し、`make test` で確認する。
8. 本ドキュメントの「初期採番案」または一覧に新コードを追記する。

### 初期採番案

#### PARSER（3000番台）
| Code | Key | 既定メッセージ（ja） |
| --- | --- | --- |
| W3001 | parser.implicit_int_return | 戻り値型が省略されています（暗黙の int） |
| W3002 | parser.unreachable_code | 到達不能なコードです |

## 将来拡張
- `E5000` 以降は、新しいエラーカテゴリが必要になった際に順次割り当てる。
- `W5000` 以降は、新しい警告カテゴリが必要になった際に順次割り当てる。
- 既存の番号帯を変更せず、帯域を追加する方式で運用する。
