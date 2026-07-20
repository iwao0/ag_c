#include "record_decl.h"

#include "type_identity.h"
#include <stddef.h>

const psx_type_t *psx_record_member_decl_type(
    const psx_record_member_decl_t *member) {
  if (!member || !member->decl_type_table ||
      member->decl_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return NULL;
  return psx_semantic_type_table_lookup_qual_type(
      member->decl_type_table, member->decl_qual_type);
}

static int record_member_leaf_shape(
    const psx_record_member_decl_t *member, psx_type_shape_t *shape) {
  if (!member || !member->decl_type_table || !shape ||
      member->decl_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  psx_qual_type_t leaf = psx_semantic_type_table_array_leaf(
      member->decl_type_table, member->decl_qual_type.type_id);
  return leaf.type_id != PSX_TYPE_ID_INVALID &&
         psx_semantic_type_table_describe(
             member->decl_type_table, leaf.type_id, shape);
}

int psx_record_member_decl_is_tag_aggregate(
    const psx_record_member_decl_t *member) {
  psx_type_shape_t shape;
  return record_member_leaf_shape(member, &shape) &&
         (shape.kind == PSX_TYPE_STRUCT || shape.kind == PSX_TYPE_UNION);
}

int psx_record_member_decl_is_unnamed_struct(
    const psx_record_member_decl_t *member) {
  psx_type_shape_t shape;
  return member && member->len <= 0 &&
         record_member_leaf_shape(member, &shape) &&
         shape.kind == PSX_TYPE_STRUCT;
}

int psx_record_member_decl_is_unnamed_union(
    const psx_record_member_decl_t *member) {
  psx_type_shape_t shape;
  return member && member->len <= 0 &&
         record_member_leaf_shape(member, &shape) &&
         shape.kind == PSX_TYPE_UNION;
}

int psx_record_member_decl_is_unnamed_aggregate(
    const psx_record_member_decl_t *member) {
  return psx_record_member_decl_is_unnamed_struct(member) ||
         psx_record_member_decl_is_unnamed_union(member);
}
