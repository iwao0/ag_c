#ifndef SEMANTIC_EXPRESSION_IDENTITY_H
#define SEMANTIC_EXPRESSION_IDENTITY_H

typedef unsigned int psx_semantic_expr_id_t;
typedef struct node_t node_t;
typedef struct psx_semantic_expression_table_t
    psx_semantic_expression_table_t;

#define PSX_SEMANTIC_EXPR_ID_INVALID ((psx_semantic_expr_id_t)0)

psx_semantic_expression_table_t *psx_semantic_expression_table_create(void);
void psx_semantic_expression_table_destroy(
    psx_semantic_expression_table_t *table);
void psx_semantic_expression_table_reset(
    psx_semantic_expression_table_t *table);
psx_semantic_expr_id_t psx_semantic_expression_table_register(
    psx_semantic_expression_table_t *table, node_t *expression);
node_t *psx_semantic_expression_table_lookup(
    const psx_semantic_expression_table_t *table,
    psx_semantic_expr_id_t expression_id);

#endif
