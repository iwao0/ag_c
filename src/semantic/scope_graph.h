#ifndef SEMANTIC_SCOPE_GRAPH_H
#define SEMANTIC_SCOPE_GRAPH_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t psx_scope_id_t;
typedef uint32_t psx_decl_id_t;

#define PSX_SCOPE_ID_TRANSLATION_UNIT ((psx_scope_id_t)0)
#define PSX_SCOPE_ID_INVALID UINT32_MAX
#define PSX_DECL_ID_INVALID ((psx_decl_id_t)0)

typedef enum {
  PSX_SCOPE_TRANSLATION_UNIT = 0,
  PSX_SCOPE_FUNCTION,
  PSX_SCOPE_FUNCTION_PROTOTYPE,
  PSX_SCOPE_BLOCK,
  PSX_SCOPE_RECORD,
} psx_scope_kind_t;

typedef enum {
  PSX_NAMESPACE_ORDINARY = 0,
  PSX_NAMESPACE_TAG,
  PSX_NAMESPACE_LABEL,
  PSX_NAMESPACE_MEMBER,
  PSX_NAMESPACE_COUNT,
} psx_c_namespace_t;

typedef enum {
  PSX_DECL_UNKNOWN = 0,
  PSX_DECL_LOCAL_OBJECT,
  PSX_DECL_GLOBAL_OBJECT,
  PSX_DECL_FUNCTION,
  PSX_DECL_TYPEDEF,
  PSX_DECL_ENUM_CONSTANT,
  PSX_DECL_TAG,
  PSX_DECL_LABEL,
  PSX_DECL_MEMBER,
} psx_scope_decl_kind_t;

typedef struct psx_scope_graph_t psx_scope_graph_t;

typedef struct {
  psx_scope_id_t scope_id;
  uint32_t declaration_order;
} psx_scope_lookup_point_t;

typedef struct {
  psx_decl_id_t id;
  psx_scope_id_t scope_id;
  psx_c_namespace_t name_space;
  psx_scope_decl_kind_t kind;
  const char *name;
  int name_len;
  uint32_t declaration_order;
  void *payload;
} psx_scope_declaration_t;

typedef struct {
  size_t scope_count;
  size_t declaration_count;
  psx_scope_id_t current_scope;
  uint32_t *declaration_orders;
  size_t declaration_order_count;
  unsigned char active;
} psx_scope_graph_checkpoint_t;

psx_scope_graph_t *psx_scope_graph_create(void);
void psx_scope_graph_destroy(psx_scope_graph_t *graph);
void psx_scope_graph_reset(psx_scope_graph_t *graph);

psx_scope_id_t psx_scope_graph_current_scope(
    const psx_scope_graph_t *graph);
psx_scope_id_t psx_scope_graph_next_scope_id(
    const psx_scope_graph_t *graph);
psx_scope_kind_t psx_scope_graph_scope_kind(
    const psx_scope_graph_t *graph, psx_scope_id_t scope_id);
psx_scope_id_t psx_scope_graph_enter_scope(
    psx_scope_graph_t *graph, psx_scope_kind_t kind);
psx_scope_id_t psx_scope_graph_create_scope_at(
    psx_scope_graph_t *graph, psx_scope_id_t parent_scope,
    psx_scope_kind_t kind);
int psx_scope_graph_leave_scope(psx_scope_graph_t *graph);
int psx_scope_graph_scope_is_visible_from(
    const psx_scope_graph_t *graph, psx_scope_id_t declaration_scope,
    psx_scope_id_t reference_scope);
psx_scope_id_t psx_scope_graph_nearest_scope_of_kind(
    const psx_scope_graph_t *graph, psx_scope_id_t scope_id,
    psx_scope_kind_t kind);

uint32_t psx_scope_graph_reserve_declaration_order(
    psx_scope_graph_t *graph);
psx_scope_lookup_point_t psx_scope_graph_capture_lookup_point(
    const psx_scope_graph_t *graph);
psx_decl_id_t psx_scope_graph_declare(
    psx_scope_graph_t *graph, psx_c_namespace_t name_space,
    psx_scope_decl_kind_t kind, const char *name, int name_len,
    void *payload);
psx_decl_id_t psx_scope_graph_declare_at(
    psx_scope_graph_t *graph, psx_scope_id_t scope_id,
    psx_c_namespace_t name_space, psx_scope_decl_kind_t kind,
    const char *name, int name_len, void *payload);
psx_decl_id_t psx_scope_graph_declare_synthetic_at(
    psx_scope_graph_t *graph, psx_scope_id_t scope_id,
    psx_c_namespace_t name_space, psx_scope_decl_kind_t kind,
    const char *name, int name_len, void *payload);
psx_decl_id_t psx_scope_graph_lookup(
    const psx_scope_graph_t *graph, psx_c_namespace_t name_space,
    const char *name, int name_len, psx_scope_lookup_point_t point);
psx_decl_id_t psx_scope_graph_lookup_in_scope(
    const psx_scope_graph_t *graph, psx_scope_id_t scope_id,
    psx_c_namespace_t name_space, const char *name, int name_len);
const psx_scope_declaration_t *psx_scope_graph_lookup_declaration_in_scope(
    const psx_scope_graph_t *graph, psx_scope_id_t scope_id,
    psx_c_namespace_t name_space, const char *name, int name_len);
const psx_scope_declaration_t *psx_scope_graph_declaration(
    const psx_scope_graph_t *graph, psx_decl_id_t declaration_id);
size_t psx_scope_graph_declaration_count(
    const psx_scope_graph_t *graph);
const psx_scope_declaration_t *psx_scope_graph_declaration_at(
    const psx_scope_graph_t *graph, size_t index);
void psx_scope_graph_forget_declaration(
    psx_scope_graph_t *graph, psx_decl_id_t declaration_id);
int psx_scope_graph_rehome_declaration_at(
    psx_scope_graph_t *graph, psx_decl_id_t declaration_id,
    psx_scope_id_t scope_id);

int psx_scope_graph_checkpoint_begin(
    const psx_scope_graph_t *graph,
    psx_scope_graph_checkpoint_t *checkpoint);
void psx_scope_graph_checkpoint_commit(
    psx_scope_graph_checkpoint_t *checkpoint);
void psx_scope_graph_checkpoint_rollback(
    psx_scope_graph_t *graph, psx_scope_graph_checkpoint_t *checkpoint);

#endif
