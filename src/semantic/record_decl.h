#ifndef SEMANTIC_RECORD_DECL_H
#define SEMANTIC_RECORD_DECL_H

#include "../type_system/type_ids.h"
#include "../type_system/type_shape.h"

typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;

typedef struct psx_record_member_decl_t {
  char *name;
  int len;
  int bit_width;
  int bit_is_signed;
  psx_qual_type_t decl_qual_type;
} psx_record_member_decl_t;

typedef struct psx_record_decl_t {
  psx_record_id_t record_id;
  psx_type_kind_t record_kind;
  char *tag_name;
  int tag_len;
  unsigned char is_complete;
  int member_count;
  const psx_record_member_decl_t *members;
} psx_record_decl_t;

int psx_record_member_decl_leaf_shape(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member, psx_type_shape_t *shape);
int psx_record_member_decl_is_tag_aggregate(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_struct_aggregate(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_union_aggregate(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_unnamed_struct(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_unnamed_union(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_unnamed_aggregate(
    const psx_semantic_type_table_t *types,
    const psx_record_member_decl_t *member);

#endif
