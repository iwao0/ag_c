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

// 抽象構文木のノードの型
typedef struct node_t node_t;
struct node_t {
  node_kind_t kind; // ノードの型
  
  // ツリー構造用
  node_t *lhs;      // 左辺 / 条件式
  node_t *rhs;      // 右辺 / then節 / ループ本体
  
  // 関数/ブロック用
  node_t **body;    // ブロック内の文（NULL終端の動的配列）(ND_BLOCK)
  node_t **args;    // 引数/仮引数の動的配列 (ND_FUNCALL, ND_FUNCDEF)
  int nargs;        // 引数の数
  char *funcname;   // 関数名
  int funcname_len; // 関数名の長さ
  
  // 制御構造用
  node_t *els;      // else節（ND_IFのみ）
  node_t *init;     // 初期化式（ND_FORのみ）
  node_t *inc;      // インクリメント式（ND_FORのみ）
  
  // データ型/リテラル用
  int val;          // 整数値（ND_NUMのみ）
  int offset;       // フレームオフセット（ND_LVARのみ）
  int type_size;    // ロード/ストアサイズ（1=char, 8=int/pointer）
  int deref_size;   // ポインタが指す先の要素サイズ（*p で使用）
  int is_float;     // 0=整数, 1=float, 2=double
  double fval;      // 浮動小数点値
  int fval_id;      // 浮動小数点リテラルのID
  char *string_label; // 文字列リテラルのデータラベル（ND_STRINGのみ）
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
extern node_t **code;
void program(void);

// 単一の式をパースしてASTのルートを返す
node_t *expr(void);

#endif
