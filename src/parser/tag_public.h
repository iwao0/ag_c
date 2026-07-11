#ifndef PARSER_TAG_PUBLIC_H
#define PARSER_TAG_PUBLIC_H

#include "tag_member_public.h"
#include "tag_flat_cover.h"

struct global_var_t;

int ps_tag_member_is_tag_aggregate(const tag_member_info_t *mi);
int ps_tag_member_is_struct_aggregate(const tag_member_info_t *mi);
int ps_tag_member_is_union_aggregate(const tag_member_info_t *mi);
int ps_tag_member_is_unnamed_struct(const tag_member_info_t *mi);
int ps_tag_member_is_unnamed_union(const tag_member_info_t *mi);
int ps_tag_member_is_unnamed_aggregate(const tag_member_info_t *mi);
int ps_tag_find_unnamed_union_covering_offset(token_kind_t tag_kind, char *tag_name,
                                               int tag_len, int base_off, int target_off,
                                               int *out_off, int *out_size);
int ps_tag_member_flat_slots(const tag_member_info_t *mi);
int ps_tag_member_elem_flat_slots(const tag_member_info_t *mi);
int ps_tag_member_subscript_stride_slots(const tag_member_info_t *mi);
int ps_tag_flat_slot_count(token_kind_t tag_kind, char *tag_name, int tag_len);
int ps_tag_member_at_flat_slot(token_kind_t tag_kind, char *tag_name, int tag_len,
                                int flat_slot, tag_member_info_t *out,
                                int *out_ordinal);
int ps_tag_next_named_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                              int *ordinal_inout, tag_member_info_t *out);
int ps_tag_first_named_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                               tag_member_info_t *out, int *out_ordinal);
int ps_tag_find_named_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                              char *member_name, int member_len,
                              tag_member_info_t *out, int *out_ordinal);
int ps_tag_select_union_member_for_init_slot(token_kind_t tag_kind, char *tag_name,
                                              int tag_len, const struct global_var_t *gv,
                                              int idx, tag_member_info_t *mi);
int ps_tag_union_init_member_for_slot(token_kind_t tag_kind, char *tag_name, int tag_len,
                                       const struct global_var_t *gv, int idx,
                                       tag_member_info_t *out);
int ps_tag_member_designator_slot(token_kind_t tag_kind, char *tag_name, int tag_len,
                                   char *member_name, int member_len, int *out_ordinal);

#endif
