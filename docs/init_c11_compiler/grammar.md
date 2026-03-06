# ag_c 文法規則

現在の ag_c コンパイラが対応している文法規則（BNF風）を記載します。
未実装の構文を追加する際は、このファイルを更新してください。

## 対応済みの文法

```
program    = stmt*
stmt       = "if" "(" expr ")" stmt ("else" stmt)?
           | "while" "(" expr ")" stmt
           | "for" "(" expr? ";" expr? ";" expr? ")" stmt
           | "return" expr ";"
           | expr ";"
expr       = assign
assign     = equality ("=" assign)?
equality   = relational ("==" relational | "!=" relational)*
relational = add ("<" add | "<=" add | ">" add | ">=" add)*
add        = mul ("+" mul | "-" mul)*
mul        = primary ("*" primary | "/" primary)*
primary    = "(" expr ")" | ident | num
```

### トークン定義

| トークン種別 | 説明 | 例 |
|---|---|---|
| `TK_RESERVED` | 記号・演算子 | `+`, `-`, `*`, `/`, `(`, `)`, `<`, `>`, `<=`, `>=`, `==`, `!=`, `=`, `;` |
| `TK_IDENT` | 識別子（現在は `a`〜`z` の1文字） | `a`, `b`, `z` |
| `TK_IF` | `if` キーワード | `if` |
| `TK_ELSE` | `else` キーワード | `else` |
| `TK_WHILE` | `while` キーワード | `while` |
| `TK_FOR` | `for` キーワード | `for` |
| `TK_RETURN` | `return` キーワード | `return` |
| `TK_NUM` | 整数リテラル | `0`, `42`, `123` |
| `TK_EOF` | 入力の終端 | — |

### ASTノード種別

| ノード種別 | 説明 |
|---|---|
| `ND_ADD` | 加算 (`+`) |
| `ND_SUB` | 減算 (`-`) |
| `ND_MUL` | 乗算 (`*`) |
| `ND_DIV` | 除算 (`/`) |
| `ND_EQ` | 等値比較 (`==`) |
| `ND_NE` | 非等値比較 (`!=`) |
| `ND_LT` | 小なり (`<`, `>` は左右反転して `<` に変換) |
| `ND_LE` | 以下 (`<=`, `>=` は左右反転して `<=` に変換) |
| `ND_ASSIGN` | 代入 (`=`) |
| `ND_LVAR` | ローカル変数参照 |
| `ND_IF` | if文（`lhs`=条件, `rhs`=then節, `els`=else節） |
| `ND_WHILE` | while文（`lhs`=条件, `rhs`=ループ本体） |
| `ND_FOR` | for文（`init`=初期化, `lhs`=条件, `inc`=インクリメント, `rhs`=本体） |
| `ND_RETURN` | return文（`lhs`=戻り値の式） |
| `ND_NUM` | 整数リテラル |

## 未実装（今後の拡張候補）

- ~~`if` / `else`~~ / ~~`while`~~ / ~~`for`~~ などの制御構文 → **実装済み**
- ~~`return` 文~~ → **実装済み**
- 関数定義・関数呼び出し
- 複数文字の変数名（現在は `a`〜`z` の1文字のみ）
- 型宣言（`int` など）
- ポインタ・配列
- 文字列リテラル
- プリプロセッサ (`#include`, `#define`)
