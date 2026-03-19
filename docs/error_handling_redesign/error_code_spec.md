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

## 将来拡張
- `E5000` 以降は、新しいカテゴリが必要になった際に順次割り当てる。
- 既存の番号帯を変更せず、帯域を追加する方式で運用する。
