# ag_c 文法規則

現在の ag_c コンパイラが対応している文法規則（BNF風）を記載します。
未実装の構文を追加する際は、このファイルを更新してください。

## 対応済みの文法

```
// ── 翻訳単位 ──────────────────────────────────────────
program        = external_decl*
external_decl  = func_def
               | declaration

// ── 宣言（C11 §6.7）─────────────────────────────────────
func_def       = decl_spec declarator compound_stmt
declaration    = decl_spec init_declarator_list? ";"
               | "_Static_assert" "(" const_expr "," string ")" ";"

decl_spec      = (storage_spec | type_spec | type_qual | func_spec | align_spec)+
storage_spec   = "static" | "extern" | "register" | "auto" | "_Thread_local"
type_spec      = "int" | "char" | "void" | "short" | "long" | "float" | "double"
               | "signed" | "unsigned" | "_Bool" | "_Complex" | "_Atomic"
               | struct_or_union_spec | enum_spec | typedef_name
struct_or_union_spec = ("struct" | "union") ident? "{" member_list "}"
                     | ("struct" | "union") ident
enum_spec      = "enum" ident? "{" enumerator_list ","? "}"
               | "enum" ident
type_qual      = "const" | "volatile" | "restrict" | "_Atomic"
func_spec      = "inline" | "_Noreturn"
align_spec     = "_Alignas" "(" (type_name | const_expr) ")"

init_declarator_list = init_declarator ("," init_declarator)*
init_declarator = declarator ("=" initializer)?

// ── 宣言子（C11 §6.7.6）─────────────────────────────────
declarator     = pointer? direct_declarator
direct_declarator = ident ("[" num? "]")*                           // variable / array
                  | ident "(" params? ")"                           // function
                  | "(" pointer? ident ")" "(" params? ")"          // function pointer: (*fp)(...)
pointer        = ("*" type_qual*)+

params         = param_decl ("," param_decl)* ("," "...")?
               | "void"
param_decl     = decl_spec (declarator | pointer?)

// ── 構造体・共用体メンバ（C11 §6.7.2.1）─────────────────
member_list    = member_decl+
member_decl    = decl_spec pointer? ident ("[" num? "]")? (":" const_expr)?
                 ("," pointer? ident ("[" num "]")? (":" const_expr)?)* ";"

// ── 列挙子（C11 §6.7.2.2）──────────────────────────────
enumerator_list = enumerator ("," enumerator)*
enumerator     = ident ("=" const_expr)?

// ── 型名（C11 §6.7.7）──────────────────────────────────
type_name      = decl_spec pointer?

// ── 初期化子（C11 §6.7.9）──────────────────────────────
initializer      = assign | "{" initializer_list? "}"
initializer_list = designation? initializer ("," designation? initializer)* ","?
designation      = ("." ident | "[" const_expr "]")+ "="

// ── 文（C11 §6.8）──────────────────────────────────────
compound_stmt  = "{" block_item* "}"
block_item     = declaration
               | "_Static_assert" "(" const_expr "," string ")" ";"
               | stmt
stmt           = compound_stmt
               | "if" "(" expr ")" stmt ("else" stmt)?
               | "while" "(" expr ")" stmt
               | "do" stmt "while" "(" expr ")" ";"
               | "for" "(" (declaration | expr? ";") expr? ";" expr? ")" stmt
               | "switch" "(" expr ")" stmt
               | "case" const_expr ":" stmt
               | "default" ":" stmt
               | "break" ";"
               | "continue" ";"
               | "goto" ident ";"
               | ident ":" stmt
               | "return" expr? ";"
               | expr ";"
               | ";"                                                // null statement

// ── 式（C11 §6.5）──────────────────────────────────────
expr           = assign ("," assign)*
assign         = conditional (("=" | "+=" | "-=" | "*=" | "/=" | "%="
               | "<<=" | ">>=" | "&=" | "^=" | "|=") assign)?
conditional    = logical_or ("?" expr ":" conditional)?
logical_or     = logical_and ("||" logical_and)*
logical_and    = bit_or ("&&" bit_or)*
bit_or         = bit_xor ("|" bit_xor)*
bit_xor        = bit_and ("^" bit_and)*
bit_and        = equality ("&" equality)*
equality       = relational ("==" relational | "!=" relational)*
relational     = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
shift          = add ("<<" add | ">>" add)*
add            = mul ("+" mul | "-" mul)*
mul            = cast ("*" cast | "/" cast | "%" cast)*
cast           = "(" type_name ")" cast | unary
unary          = "(" type_name ")" "{" initializer_list "}"         // compound literal
               | "sizeof" ("(" type_name ")" | unary)
               | "_Alignof" "(" type_name ")"
               | ("++" | "--" | "+" | "-" | "!" | "~" | "*" | "&") cast
               | postfix
postfix        = primary ("[" expr "]" | "(" args? ")" | "." ident | "->" ident | "++" | "--")*
primary        = "(" expr ")" | ident | num | string | char_lit
               | "_Generic" "(" assign "," generic_assoc_list ")"
args           = assign ("," assign)*
generic_assoc_list = generic_assoc ("," generic_assoc)*
generic_assoc  = (type_name | "default") ":" assign

// ── 定数式（C11 §6.6）──────────────────────────────────
// compile-time constant expression using enum_const_expr evaluator
const_expr     = integer literals, sizeof, !, ~, +, -, *, /, %, <<, >>, &, |, ^,
                 ==, !=, <, <=, >, >=, &&, ||, ?:, parenthesized sub-expressions
```

### トークン定義

| トークン種別 | 説明 | 例 |
|---|---|---|
| `TK_EOF` | 入力の終端 | — |
| `TK_IDENT` | 識別子（英字/`_`で始まり英数字/`_`が続く） | `a`, `foo`, `x1`, `my_var` |
| `TK_NUM` | 数値リテラル（整数/浮動小数） | `0`, `42`, `123`, `3.14`, `1.5f`, `0x2a`, `0b101`, `077`, `0x1.8p1` |
| `TK_STRING` | 文字列リテラル | `"hello"` |
| `TK_AUTO` | `auto` キーワード | `auto` |
| `TK_BREAK` | `break` キーワード | `break` |
| `TK_CASE` | `case` キーワード | `case` |
| `TK_CONST` | `const` キーワード | `const` |
| `TK_CONTINUE` | `continue` キーワード | `continue` |
| `TK_DEFAULT` | `default` キーワード | `default` |
| `TK_DO` | `do` キーワード | `do` |
| `TK_ENUM` | `enum` キーワード | `enum` |
| `TK_EXTERN` | `extern` キーワード | `extern` |
| `TK_GOTO` | `goto` キーワード | `goto` |
| `TK_IF` | `if` キーワード | `if` |
| `TK_ELSE` | `else` キーワード | `else` |
| `TK_WHILE` | `while` キーワード | `while` |
| `TK_FOR` | `for` キーワード | `for` |
| `TK_RETURN` | `return` キーワード | `return` |
| `TK_INLINE` | `inline` キーワード | `inline` |
| `TK_INT` | `int` キーワード | `int` |
| `TK_REGISTER` | `register` キーワード | `register` |
| `TK_RESTRICT` | `restrict` キーワード | `restrict` |
| `TK_SIGNED` | `signed` キーワード | `signed` |
| `TK_SIZEOF` | `sizeof` キーワード | `sizeof` |
| `TK_STATIC` | `static` キーワード | `static` |
| `TK_STRUCT` | `struct` キーワード | `struct` |
| `TK_SWITCH` | `switch` キーワード | `switch` |
| `TK_TYPEDEF` | `typedef` キーワード | `typedef` |
| `TK_UNION` | `union` キーワード | `union` |
| `TK_UNSIGNED` | `unsigned` キーワード | `unsigned` |
| `TK_VOLATILE` | `volatile` キーワード | `volatile` |
| `TK_CHAR` | `char` キーワード | `char` |
| `TK_VOID` | `void` キーワード | `void` |
| `TK_SHORT` | `short` キーワード | `short` |
| `TK_LONG` | `long` キーワード | `long` |
| `TK_FLOAT` | `float` キーワード | `float` |
| `TK_DOUBLE` | `double` キーワード | `double` |
| `TK_ALIGNAS` | `_Alignas` キーワード | `_Alignas` |
| `TK_ALIGNOF` | `_Alignof` キーワード | `_Alignof` |
| `TK_ATOMIC` | `_Atomic` キーワード | `_Atomic` |
| `TK_BOOL` | `_Bool` キーワード | `_Bool` |
| `TK_COMPLEX` | `_Complex` キーワード | `_Complex` |
| `TK_GENERIC` | `_Generic` キーワード | `_Generic` |
| `TK_IMAGINARY` | `_Imaginary` キーワード | `_Imaginary` |
| `TK_NORETURN` | `_Noreturn` キーワード | `_Noreturn` |
| `TK_STATIC_ASSERT` | `_Static_assert` キーワード | `_Static_assert` |
| `TK_THREAD_LOCAL` | `_Thread_local` キーワード | `_Thread_local` |
| `TK_LPAREN` | `(` | `(` |
| `TK_RPAREN` | `)` | `)` |
| `TK_LBRACE` | `{` | `{` |
| `TK_RBRACE` | `}` | `}` |
| `TK_LBRACKET` | `[` | `[` |
| `TK_RBRACKET` | `]` | `]` |
| `TK_COMMA` | `,` | `,` |
| `TK_SEMI` | `;` | `;` |
| `TK_ASSIGN` | `=` | `=` |
| `TK_PLUS` | `+` | `+` |
| `TK_MINUS` | `-` | `-` |
| `TK_MUL` | `*` | `*` |
| `TK_DIV` | `/` | `/` |
| `TK_MOD` | `%` | `%` |
| `TK_BANG` | `!` | `!` |
| `TK_TILDE` | `~` | `~` |
| `TK_LT` | `<` | `<` |
| `TK_LE` | `<=` | `<=` |
| `TK_GT` | `>` | `>` |
| `TK_GE` | `>=` | `>=` |
| `TK_EQEQ` | `==` | `==` |
| `TK_NEQ` | `!=` | `!=` |
| `TK_ANDAND` | `&&` | `&&` |
| `TK_OROR` | `||` | `||` |
| `TK_AMP` | `&` | `&` |
| `TK_PIPE` | `|` | `|` |
| `TK_CARET` | `^` | `^` |
| `TK_QUESTION` | `?` | `?` |
| `TK_COLON` | `:` | `:` |
| `TK_INC` | `++` | `++` |
| `TK_DEC` | `--` | `--` |
| `TK_SHL` | `<<` | `<<` |
| `TK_SHR` | `>>` | `>>` |
| `TK_ARROW` | `->` | `->` |
| `TK_PLUSEQ` | `+=` | `+=` |
| `TK_MINUSEQ` | `-=` | `-=` |
| `TK_MULEQ` | `*=` | `*=` |
| `TK_DIVEQ` | `/=` | `/=` |
| `TK_MODEQ` | `%=` | `%=` |
| `TK_SHLEQ` | `<<=` | `<<=` |
| `TK_SHREQ` | `>>=` | `>>=` |
| `TK_ANDEQ` | `&=` | `&=` |
| `TK_XOREQ` | `^=` | `^=` |
| `TK_OREQ` | `|=` | `|=` |
| `TK_ELLIPSIS` | `...` | `...` |
| `TK_HASH` | `#` | `#` |
| `TK_HASHHASH` | `##` | `##` |
| `TK_DOT` | `.` | `.` |

### ASTノード種別

| ノード種別 | 説明 |
|---|---|
| `ND_ADD` | 加算 (`+`) |
| `ND_SUB` | 減算 (`-`) |
| `ND_MUL` | 乗算 (`*`) |
| `ND_DIV` | 除算 (`/`) |
| `ND_MOD` | 剰余 (`%`) |
| `ND_SHL` | 左シフト (`<<`) |
| `ND_SHR` | 右シフト (`>>`) |
| `ND_BITAND` | ビットAND (`&`) |
| `ND_BITXOR` | ビットXOR (`^`) |
| `ND_BITOR` | ビットOR (`\|`) |
| `ND_EQ` | 等値比較 (`==`) |
| `ND_NE` | 非等値比較 (`!=`) |
| `ND_LT` | 小なり (`<`, `>` は左右反転して `<` に変換) |
| `ND_LE` | 以下 (`<=`, `>=` は左右反転して `<=` に変換) |
| `ND_LOGAND` | 論理積 (`&&`, 短絡評価) |
| `ND_LOGOR` | 論理和 (`||`, 短絡評価) |
| `ND_TERNARY` | 条件演算子 (`?:`) |
| `ND_COMMA` | カンマ演算子 (`,`) |
| `ND_ASSIGN` | 代入 (`=`) |
| `ND_LVAR` | ローカル変数参照 |
| `ND_IF` | if文（`lhs`=条件, `rhs`=then節, `els`=else節） |
| `ND_WHILE` | while文（`lhs`=条件, `rhs`=ループ本体） |
| `ND_DO_WHILE` | do-while文（`rhs`=ループ本体, `lhs`=条件） |
| `ND_FOR` | for文（`init`=初期化, `lhs`=条件, `inc`=インクリメント, `rhs`=本体） |
| `ND_SWITCH` | switch文（`lhs`=制御式, `rhs`=本体） |
| `ND_CASE` | caseラベル（`val`=case値, `rhs`=case文） |
| `ND_DEFAULT` | defaultラベル（`rhs`=default文） |
| `ND_BREAK` | break文 |
| `ND_CONTINUE` | continue文 |
| `ND_GOTO` | goto文（`goto label;`） |
| `ND_LABEL` | ラベル文（`label: stmt`） |
| `ND_PRE_INC` | 前置インクリメント (`++x`) |
| `ND_PRE_DEC` | 前置デクリメント (`--x`) |
| `ND_POST_INC` | 後置インクリメント (`x++`) |
| `ND_POST_DEC` | 後置デクリメント (`x--`) |
| `ND_RETURN` | return文（`lhs`=戻り値の式） |
| `ND_BLOCK` | ブロック文（`body[]`=文の配列） |
| `ND_FUNCDEF` | 関数定義（`funcname`, `args[]`=仮引数, `rhs`=本体BLOCK） |
| `ND_FUNCALL` | 関数呼び出し（`funcname`, `args[]`=実引数, `nargs`） |
| `ND_DEREF` | 間接参照 `*p`（`lhs`=アドレス式） |
| `ND_ADDR` | アドレス取得 `&x`（`lhs`=変数） |
| `ND_STRING` | 文字列リテラル（`string_label`=データラベル） |
| `ND_NUM` | 整数リテラル |
| `ND_GVAR` | グローバル変数参照 |
| `ND_FUNCREF` | 関数シンボル参照（関数ポインタ値） |
| `ND_VLA_ALLOC` | VLA動的スタック確保（`lhs`=サイズ式） |

## 未実装（今後の拡張候補）

- ~~`if` / `else`~~ / ~~`while`~~ / ~~`for`~~ などの制御構文 → **実装済み**
- ~~`return` 文~~ → **実装済み**
- ~~関数定義・関数呼び出し~~ → **実装済み**
- ~~複数文字の変数名~~ → **実装済み**（英数字・アンダースコア対応）
- ~~型宣言（`int`）~~ → **実装済み**（`int`/`char`/`void`/`short`/`long`/`float`/`double`/`signed`/`unsigned`/`_Bool`/`_Complex`/`_Atomic`）
- ~~ポインタ・配列~~ → **実装済み**（`*p`, `&x`, `int arr[N]`, `arr[i]`、フレキシブル配列メンバ `int data[];`）
- ~~文字列リテラル~~ → **実装済み**（`char *s = "..."`、添字アクセス `s[i]` 対応済み）
- ~~`_Static_assert`~~ → **実装済み**（`sizeof(type)` を含む定数式に対応）
- ~~`_Generic`~~ → **実装済み**（リテラル式の型選択に対応）
- ~~`_Thread_local`~~ → **実装済み**（macOS TLV descriptor 経由）
- ~~`_Complex`~~ → **実装済み**（実部/虚部セマンティクス）
- ~~`_Atomic`~~ → **実装済み**（load-acquire/store-release セマンティクス）
- ~~ブロックスコープ変数シャドウイング~~ → **実装済み**
- ~~匿名タグ宣言~~ → **実装済み**（`enum { A, B };` 等のタグ名省略）
- ~~空文~~ → **実装済み**（`;;` 等の null statement）
- ~~for文の変数宣言~~ → **実装済み**（`for (int i=0; ...)` のスコープ付き宣言）
- プリプロセッサ (`#include`, `#define`)

## 初期化子と診断の現行方針（2026-03時点）

- スカラ初期化子:
  - `int x=1;` と `int x={1};` を受理
  - `int x={1,2};` は診断（`スカラ初期化子の波括弧内は1要素のみ対応です`）
- 配列初期化子:
  - `int a[3]={1,2,3};` / `char s[4]="abc";` を受理
  - 非波括弧の配列初期化は診断（`配列初期化は現在 '{...}' または文字列リテラルのみ対応です`）
- 構造体初期化子:
  - `{...}`（designator含む）を受理
  - 単一式は同型オブジェクトのみ受理（`,` 演算子の最終値が同型 `lvar` の場合を含む）
- 共用体初期化子:
  - `{...}` は1要素のみ受理（designator含む）
  - 単一式は同型オブジェクトコピー、または先頭メンバへの初期化として処理
  - 先頭メンバが配列の場合、`union U u={1,2};` のような非波括弧初期化を段階受理（brace elision）
- cast 方針:
  - `struct/union` 値への cast は「同一タグ型どうしのみ no-op 受理」を基本とし、`config.toml` の `[parser].enable_size_compatible_nonscalar_cast = true` 時は「同種かつ同サイズ」も段階受理
  - `struct` へのスカラ/ポインタ cast（例: `(struct S)7`, `(struct S)p`）は段階受理（先頭メンバ初期化へ lowering）
  - `union` へのスカラ cast（例: `(union U)7`）は段階受理（先頭メンバ初期化へ lowering）
  - 非受理ケースは理由別に診断（型不整合: `[cast] ... 値へのキャストは未対応です（型不整合）` / 設定無効: `[cast] struct|union への scalar/pointer cast は設定で無効です`）
  - cast 型名は `const/volatile/restrict`・`_Atomic int`・`_Atomic(T)`・入れ子 `_Atomic(_Atomic(T))` を受理
  - cast 型名のストレージ指定子（例: `_Thread_local`）は診断（`[cast] cast 型名にストレージ指定子は使えません`）
  - C制約に合わせ、`struct/union` 値への cast で非スカラ源（`struct/union`）を受ける範囲は「同一タグ」または「同種同サイズ（拡張ON時）」に限定

## 2026-03 C11準拠強化（Tokenizer/Parser）

- 文字定数:
  - マルチ文字文字定数（例: `'ab'`）を受理
  - 接頭辞付き文字定数 `L'c'`, `u'c'`, `U'c'` を受理
  - 接頭辞付きマルチ文字定数（例: `L'AB'`）を実装定義として受理
- 文字列リテラル:
  - 接頭辞付き文字列 `L"..."`, `u"..."`, `U"..."`, `u8"..."` を受理
  - 隣接文字列リテラル連結（`"a" "b"`）を Parser 側で連結
  - 接頭辞情報を `token_string_t.str_prefix_kind` と `token_string_t.char_width` に保持し、AST (`node_string_t`) へ伝搬
- Universal Character Name:
  - `\uXXXX`, `\UXXXXXXXX` を識別子・文字列/文字定数のエスケープで受理
- トライグラフ:
  - `??=` などのトライグラフ置換を前処理として実行
- 数値リテラル:
  - `token_num_t.val` / `node_num_t.val` を `long long` 化し、`int` への早期切り詰めを回避
  - 浮動小数点サフィックス種別を `token_num_t.float_suffix_kind` へ保持（`0=none,1=f/F,2=l/L`）
  - `0b...` は拡張として維持し、`strict C11` モード時は拒否

> [!NOTE]
> 現在の実装では、字句解析の先頭でトライグラフ置換を行い、その後にトークナイズを行います（翻訳フェーズ順序との整合）。
> `0b...` はデフォルトで拡張として許可し、`strict C11 = true` または `enable_binary_literals = false` で拒否されます。
> これらの挙動は `config.toml` の `[tokenizer]` セクション（`strict_c11`, `enable_trigraphs`, `enable_binary_literals`）で切り替え可能です。
> `struct/union` 値 cast の段階受理（同種同サイズ）も `config.toml` の `[parser]` セクション（`enable_size_compatible_nonscalar_cast`）で切り替え可能です。
> `struct` のスカラ/ポインタ cast は `enable_struct_scalar_pointer_cast`、`union` のスカラ/ポインタ cast は `enable_union_scalar_pointer_cast` で切り替え可能です。
> cast 後の postfix 連鎖（例: `((union U)&x).p`）を受理します。
> 設定キーごとの代表挙動は README の `config.toml` 設定マトリクスを参照してください。
> 接頭辞付き文字列/文字定数の幅情報は Codegen まで伝搬され、`char_width=1/2/4` に応じて `.byte/.hword/.word` で出力します。
> 接頭辞付きマルチ文字定数は、現実装では 8-bit 単位で左シフトしながら畳み込む実装定義規則で値を形成します。
> `l/L` 付き浮動小数点は `is_float=3`（long double）として意味分類します。現時点のCodegenは double 経路へ lowering します。

## 2026-03 Tokenizer最適化メモ

- 実装整理:
  - `tokenizer.c` は制御フロー中心、文字種判定/リテラル処理/空白スキップ/キーワード判定/記号判定は分離モジュール化。
- 主要最適化:
  - UCNなし識別子はゼロコピー経路を使用。
  - 文字列中 escape は妥当性確認のみ先行し、値デコードは必要箇所で実施。
  - 記号は 3/4文字最長一致 → 2文字小テーブルの順で判定。
  - `tk_skip_ignored()` は ASCII 空白ホットパス + コメント/行継続フォールバックを採用。
- 運用:
  - ベンチは `mixed` / `ident-heavy` / `numeric-heavy` / `punct-heavy` を継続利用。
  - `scripts/bench_tokenizer_opt_levels.sh` で `-O0`/`-O2` 比較を定点化。
