#include "tag_declaration_resolution.h"

#include "../parser/semantic_ctx.h"

#include <string.h>

static int is_tag_kind(token_kind_t kind) {
  return kind == TK_STRUCT || kind == TK_UNION || kind == TK_ENUM;
}

void psx_resolve_tag_declaration(
    const psx_tag_declaration_resolution_request_t *request,
    psx_tag_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_TAG_DECLARATION_INVALID;
  if (!request || !is_tag_kind(request->kind) || !request->name ||
      request->name_len <= 0 || request->member_count < 0 ||
      request->size < 0 || request->alignment < 0) {
    return;
  }

  token_kind_t current_kind = TK_EOF;
  if (psx_ctx_find_tag_kind_at_current_scope(
          request->name, request->name_len, &current_kind) &&
      current_kind != request->kind) {
    resolution->status = PSX_TAG_DECLARATION_KIND_CONFLICT;
    return;
  }

  if (request->mode == PSX_TAG_DECLARATION_REFERENCE &&
      psx_ctx_has_tag_type(
          request->kind, request->name, request->name_len)) {
    resolution->status = PSX_TAG_DECLARATION_OK;
  } else {
    int is_complete =
        request->mode == PSX_TAG_DECLARATION_DEFINITION;
    if (!psx_ctx_register_tag_type(
            request->kind, request->name, request->name_len,
            is_complete, request->member_count, request->size,
            request->alignment)) {
      resolution->status = is_complete
                               ? PSX_TAG_DECLARATION_REDEFINITION
                               : PSX_TAG_DECLARATION_INVALID;
      return;
    }
    resolution->registered = 1;
    resolution->status = PSX_TAG_DECLARATION_OK;
  }
  resolution->scope_depth = ps_ctx_get_tag_scope_depth(
      request->kind, request->name, request->name_len);
  resolution->size = psx_ctx_get_tag_size(
      request->kind, request->name, request->name_len);
  resolution->alignment = psx_ctx_get_tag_align(
      request->kind, request->name, request->name_len);
}
