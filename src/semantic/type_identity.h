#ifndef SEMANTIC_TYPE_IDENTITY_H
#define SEMANTIC_TYPE_IDENTITY_H

#include "../type_system/type_ids.h"
#include "../type_system/type_shape.h"

typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;
typedef struct psx_record_decl_table_t psx_record_decl_table_t;
typedef struct psx_type_t psx_type_t;

psx_semantic_type_table_t *psx_semantic_type_table_create(void);
void psx_semantic_type_table_destroy(psx_semantic_type_table_t *table);
void psx_semantic_type_table_reset(psx_semantic_type_table_t *table);
void psx_semantic_type_table_bind_record_decls(
    psx_semantic_type_table_t *table,
    const psx_record_decl_table_t *record_decls);
psx_qual_type_t psx_semantic_type_table_intern(
    psx_semantic_type_table_t *table, const psx_type_t *type);
psx_qual_type_t psx_semantic_type_table_find(
    const psx_semantic_type_table_t *table, const psx_type_t *type);
psx_qual_type_t psx_semantic_type_table_intern_pointer_to(
    psx_semantic_type_table_t *table, psx_qual_type_t pointee);
const psx_type_t *psx_semantic_type_table_lookup(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id);
int psx_semantic_type_table_describe(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    psx_type_shape_t *out);
/* Compatibility view for consumers still reading psx_type_t. The returned
 * immutable view restores qualifiers from QualType, including derived
 * relations, while TypeId remains the semantic identity. */
const psx_type_t *psx_semantic_type_table_lookup_qual_type(
    const psx_semantic_type_table_t *table, psx_qual_type_t type);
psx_qual_type_t psx_semantic_type_table_base(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id);
int psx_semantic_type_table_contains_vla_array(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id);
psx_qual_type_t psx_semantic_type_table_array_leaf(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id);
int psx_semantic_type_table_array_flat_element_count(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id);
psx_qual_type_t psx_semantic_type_table_pointee_value(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id);
psx_qual_type_t psx_semantic_type_table_callable_function(
    const psx_semantic_type_table_t *table, psx_qual_type_t type);
int psx_semantic_type_is_exact_int_void_function(
    const psx_semantic_type_table_t *table, psx_qual_type_t type);
psx_qual_type_t psx_semantic_type_table_aggregate_object(
    const psx_semantic_type_table_t *table, psx_qual_type_t type);
psx_qual_type_t psx_semantic_type_table_parameter(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    int parameter_index);
psx_qual_type_t psx_semantic_type_table_record_member(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    int member_index);

#endif
