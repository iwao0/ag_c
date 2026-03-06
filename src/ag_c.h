#ifndef AG_C_H
#define AG_C_H

struct node_t;

// コード生成関数 (arch/ 以下で実装)
void gen_main_prologue(void);
void gen_main_epilogue(void);
void gen(struct node_t *node);

#endif
