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
  if (!request || !request->semantic_context ||
      !is_tag_kind(request->kind) || !request->name ||
      request->name_len <= 0 || request->member_count < 0) {
    return;
  }
  psx_semantic_context_t *semantic_context = request->semantic_context;

  token_kind_t current_kind = TK_EOF;
  if (ps_ctx_find_tag_kind_at_current_scope_in(
          semantic_context, request->name, request->name_len,
          &current_kind) &&
      current_kind != request->kind) {
    resolution->status = PSX_TAG_DECLARATION_KIND_CONFLICT;
    return;
  }

  if (request->mode == PSX_TAG_DECLARATION_REFERENCE &&
      ps_ctx_has_tag_type_in(
          semantic_context, request->kind,
          request->name, request->name_len)) {
    resolution->status = PSX_TAG_DECLARATION_OK;
  } else {
    int is_complete =
        request->mode == PSX_TAG_DECLARATION_DEFINITION;
    if (!ps_ctx_register_tag_type_in(
            semantic_context, request->kind,
            request->name, request->name_len,
            is_complete, request->member_count)) {
      resolution->status = is_complete
                               ? PSX_TAG_DECLARATION_REDEFINITION
                               : PSX_TAG_DECLARATION_INVALID;
      return;
    }
    resolution->registered = 1;
    resolution->status = PSX_TAG_DECLARATION_OK;
  }
  resolution->scope_depth = ps_ctx_get_tag_scope_depth_in(
      semantic_context, request->kind,
      request->name, request->name_len);
}
