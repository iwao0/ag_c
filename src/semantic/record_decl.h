#ifndef SEMANTIC_RECORD_DECL_H
#define SEMANTIC_RECORD_DECL_H

#include "../type_system/type_ids.h"
#include "../type_system/type_shape.h"

typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;
typedef struct psx_type_t psx_type_t;

typedef struct psx_record_member_decl_t {
  char *name;
  int len;
  int bit_width;
  int bit_is_signed;
  const psx_semantic_type_table_t *decl_type_table;
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

const psx_type_t *psx_record_member_decl_type(
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_tag_aggregate(
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_unnamed_struct(
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_unnamed_union(
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_unnamed_aggregate(
    const psx_record_member_decl_t *member);

#endif
