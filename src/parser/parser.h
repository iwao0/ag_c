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
};

// プログラム全体をパースする（複数の文を返す）
extern node_t *code[MAX_STMTS];
void program(void);

// 単一の式をパースしてASTのルートを返す
node_t *expr(void);

#endif
