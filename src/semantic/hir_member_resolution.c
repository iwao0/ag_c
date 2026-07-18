#include "hir_member_resolution.h"

#include <string.h>

#include "../parser/semantic_ctx.h"
#include "record_layout.h"

int psx_resolve_member_hir_node_spec_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t base_qual_type,
    const char *member_name,
    int member_name_len,
    int from_pointer,
    psx_hir_member_resolution_t *resolution) {
  if (!resolution) return 0;
  memset(resolution, 0, sizeof(*resolution));
  psx_resolve_member_access_qual_type_in(
      semantic_context, base_qual_type,
      member_name, member_name_len, from_pointer,
      &resolution->member);
  if (resolution->member.status != PSX_MEMBER_ACCESS_OK)
    return 0;

  const psx_record_layout_t *layout =
      psx_record_layout_table_lookup(
          ps_ctx_record_layout_table_in(semantic_context),
          resolution->member.record_id,
          ps_ctx_target_info(semantic_context));
  const psx_record_member_layout_t *member_layout =
      psx_record_layout_member(
          layout, resolution->member.member_index);
  if (!member_layout) return 0;

  resolution->node_spec = (psx_hir_node_spec_t){
      .kind = PSX_HIR_MEMBER_ACCESS,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .member_offset = member_layout->offset,
      .member_from_pointer = from_pointer ? 1 : 0,
      .bit_width = (unsigned char)(
          resolution->member.declaration.bit_width > 0
              ? resolution->member.declaration.bit_width : 0),
      .bit_offset = (unsigned char)(
          member_layout->bit_offset > 0
              ? member_layout->bit_offset : 0),
      .bit_is_signed =
          resolution->member.declaration.bit_is_signed ? 1 : 0,
  };
  return 1;
}
