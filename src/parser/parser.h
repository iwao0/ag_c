#ifndef PARSER_H
#define PARSER_H

// 抽象構文木 (AST) のノードの種類
typedef enum {
  ND_ADD, // +
  ND_SUB, // -
  ND_MUL, // *
  ND_DIV, // /
  ND_NUM, // 整数
} node_kind_t;

// 抽象構文木のノードの型
typedef struct node_t node_t;
struct node_t {
  node_kind_t kind; // ノードの型
  node_t *lhs;      // 左辺
  node_t *rhs;      // 右辺
  int val;          // kindがND_NUMの場合のみ使う
};

// パースを開始する関数（全体の式をパースしてASTのルートを返す）
node_t *expr(void);

#endif
