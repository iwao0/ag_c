#ifndef PARSER_H
#define PARSER_H

// 抽象構文木 (AST) のノードの種類
typedef enum {
  ND_ADD,    // +
  ND_SUB,    // -
  ND_MUL,    // *
  ND_DIV,    // /
  ND_EQ,     // ==
  ND_NE,     // !=
  ND_LT,     // <
  ND_LE,     // <=
  ND_ASSIGN, // =
  ND_LVAR,   // ローカル変数
  ND_IF,     // if
  ND_WHILE,  // while
  ND_FOR,    // for
  ND_RETURN,  // return
  ND_BLOCK,   // { ... }
  ND_FUNCDEF, // 関数定義
  ND_FUNCALL, // 関数呼び出し
  ND_DEREF,   // 間接参照 (*p)
  ND_ADDR,    // アドレス取得 (&x)
  ND_STRING,  // 文字列リテラル
  ND_NUM,     // 整数
} node_kind_t;

#define MAX_ARGS 8

// 抽象構文木のノードの型
#define MAX_STMTS 100
typedef struct node_t node_t;
struct node_t {
  node_kind_t kind; // ノードの型
  node_t *lhs;      // 左辺 / 条件式
  node_t *rhs;      // 右辺 / then節 / ループ本体
  node_t *els;      // else節（ND_IFのみ）
  node_t *init;     // 初期化式（ND_FORのみ）
  node_t *inc;      // インクリメント式（ND_FORのみ）
  node_t *body[MAX_STMTS]; // ブロック内の文（ND_BLOCKのみ）
  char *funcname;           // 関数名（ND_FUNCDEF, ND_FUNCALL）
  int funcname_len;         // 関数名の長さ
  node_t *args[MAX_ARGS];   // 引数/仮引数（ND_FUNCALL, ND_FUNCDEF）
  int nargs;                // 引数の数
  int val;          // kindがND_NUMの場合のみ使う
  int offset;       // kindがND_LVARの場合のみ使う（フレームポインタからのオフセット）
  int type_size;    // ロード/ストアサイズ（1=char, 8=int/pointer）
  int deref_size;   // ポインタが指す先の要素サイズ（*p で使用）
  int is_float;     // 0=整数, 1=float, 2=double
  double fval;      // 浮動小数点値（is_float > 0 かつ文字列からパースした場合）
  int fval_id;      // 浮動小数点リテラルのID
  char *string_label; // kindがND_STRINGの場合のみ使う（データラベル）
};

// ローカル変数テーブル（連結リスト）
typedef struct lvar_t lvar_t;
struct lvar_t {
  lvar_t *next;
  char *name;
  int len;
  int offset;
  int size;      // サイズ（スカラー=8、配列=要素数*elem_size）
  int elem_size;   // 要素サイズ（1=char, 8=int/pointer）
  int is_array;  // 配列かどうか
  int is_float;  // 0=整数, 1=float, 2=double
};

// 文字列リテラルテーブル（連結リスト）
typedef struct string_lit_t string_lit_t;
struct string_lit_t {
  string_lit_t *next;
  char *label;
  char *str;
  int len;
};
extern string_lit_t *string_literals;

// 浮動小数点リテラルテーブル（連結リスト）
typedef struct float_lit_t float_lit_t;
struct float_lit_t {
  float_lit_t *next;
  int id;
  double fval;
  int is_float;
};
extern float_lit_t *float_literals;

// プログラム全体をパースする（複数の文を返す）
extern node_t *code[MAX_STMTS];
void program(void);

// 単一の式をパースしてASTのルートを返す
node_t *expr(void);

#endif
