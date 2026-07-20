#include "record_decl.h"

#include "type_compatibility_view.h"
#include "type_identity.h"
#include <stddef.h>

const psx_type_t *psx_record_member_decl_type(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member) {
  if (!types || !member ||
      member->decl_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return NULL;
  return psx_type_compatibility_view_for(
      types, member->decl_qual_type);
}

static int record_member_leaf_shape(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member, psx_type_shape_t *shape) {
  if (!types || !member || !shape ||
      member->decl_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  psx_qual_type_t leaf = psx_semantic_type_table_array_leaf(
      types, member->decl_qual_type.type_id);
  return leaf.type_id != PSX_TYPE_ID_INVALID &&
         psx_semantic_type_table_describe(
             types, leaf.type_id, shape);
}

int psx_record_member_decl_is_tag_aggregate(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member) {
  psx_type_shape_t shape;
  return record_member_leaf_shape(types, member, &shape) &&
         (shape.kind == PSX_TYPE_STRUCT || shape.kind == PSX_TYPE_UNION);
}

int psx_record_member_decl_is_struct_aggregate(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member) {
  psx_type_shape_t shape;
  return record_member_leaf_shape(types, member, &shape) &&
         shape.kind == PSX_TYPE_STRUCT;
}

int psx_record_member_decl_is_union_aggregate(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member) {
  psx_type_shape_t shape;
  return record_member_leaf_shape(types, member, &shape) &&
         shape.kind == PSX_TYPE_UNION;
}

int psx_record_member_decl_is_unnamed_struct(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member) {
  return member && member->len <= 0 &&
         psx_record_member_decl_is_struct_aggregate(types, member);
}

int psx_record_member_decl_is_unnamed_union(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member) {
  return member && member->len <= 0 &&
         psx_record_member_decl_is_union_aggregate(types, member);
}

int psx_record_member_decl_is_unnamed_aggregate(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member) {
  return psx_record_member_decl_is_unnamed_struct(types, member) ||
         psx_record_member_decl_is_unnamed_union(types, member);
}
