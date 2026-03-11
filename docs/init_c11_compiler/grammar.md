# ag_c 文法規則

現在の ag_c コンパイラが対応している文法規則（BNF風）を記載します。
未実装の構文を追加する際は、このファイルを更新してください。

## 対応済みの文法

```
program    = funcdef*
funcdef    = type? ident "(" params? ")" "{" stmt* "}"
params     = type? ident ("," type? ident)*
stmt       = "{" stmt* "}"
           | "if" "(" expr ")" stmt ("else" stmt)?
           | "while" "(" expr ")" stmt
           | "for" "(" expr? ";" expr? ";" expr? ")" stmt
           | "return" expr ";"
           | type ident ("=" expr)? ";"
           | expr ";"
type       = "int" | "char" | "void" | "short" | "long" | "float" | "double"
expr       = assign
assign     = equality ("=" assign)?
equality   = relational ("==" relational | "!=" relational)*
relational = add ("<" add | "<=" add | ">" add | ">=" add)*
add        = mul ("+" mul | "-" mul)*
mul        = unary ("*" unary | "/" unary)*
unary      = ("*" | "&") unary | primary postfix*
postfix    = "[" expr "]"
primary    = ident "(" args? ")" | "(" expr ")" | ident | num | string | char_lit
args       = expr ("," expr)*
```

### トークン定義

| トークン種別 | 説明 | 例 |
|---|---|---|
| `TK_RESERVED` | 記号・演算子 | `+`, `-`, `*`, `/`, `(`, `)`, `<`, `>`, `<=`, `>=`, `==`, `!=`, `=`, `;`, `{`, `}`, `,`, `&`, `[`, `]` |
| `TK_IDENT` | 識別子（英字/`_`で始まり英数字/`_`が続く） | `a`, `foo`, `x1`, `my_var` |
| `TK_IF` | `if` キーワード | `if` |
| `TK_ELSE` | `else` キーワード | `else` |
| `TK_WHILE` | `while` キーワード | `while` |
| `TK_FOR` | `for` キーワード | `for` |
| `TK_RETURN` | `return` キーワード | `return` |
| `TK_INT` | `int` キーワード | `int` |
| `TK_CHAR` | `char` キーワード | `char` |
| `TK_VOID` | `void` キーワード | `void` |
| `TK_SHORT` | `short` キーワード | `short` |
| `TK_LONG` | `long` キーワード | `long` |
| `TK_FLOAT` | `float` キーワード | `float` |
| `TK_DOUBLE` | `double` キーワード | `double` |
| `TK_NUM` | 整数リテラル | `0`, `42`, `123` |
| `TK_STRING` | 文字列リテラル | `"hello"` |
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
| `ND_BLOCK` | ブロック文（`body[]`=文の配列） |
| `ND_FUNCDEF` | 関数定義（`funcname`, `args[]`=仮引数, `rhs`=本体BLOCK） |
| `ND_FUNCALL` | 関数呼び出し（`funcname`, `args[]`=実引数, `nargs`） |
| `ND_DEREF` | 間接参照 `*p`（`lhs`=アドレス式） |
| `ND_ADDR` | アドレス取得 `&x`（`lhs`=変数） |
| `ND_STRING` | 文字列リテラル（`string_label`=データラベル） |
| `ND_NUM` | 整数リテラル |

## 未実装（今後の拡張候補）

- ~~`if` / `else`~~ / ~~`while`~~ / ~~`for`~~ などの制御構文 → **実装済み**
- ~~`return` 文~~ → **実装済み**
- ~~関数定義・関数呼び出し~~ → **実装済み**
- ~~複数文字の変数名~~ → **実装済み**（英数字・アンダースコア対応）
- ~~型宣言（`int`）~~ → **実装済み**（`int`/`char`/`void`/`short`/`long`/`float`/`double`、サイズ・FPU命令対応済み）
- ~~ポインタ・配列~~ → **実装済み**（`*p`, `&x`, `int arr[N]`, `arr[i]`）
- ~~文字列リテラル~~ → **実装済み**（`char *s = "..."`、添字アクセス `s[i]` 対応済み）
- プリプロセッサ (`#include`, `#define`)
