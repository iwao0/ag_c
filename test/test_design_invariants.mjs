import { readFile, readdir } from "node:fs/promises";

async function sourceFilesUnder(directory) {
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = `${directory}/${entry.name}`;
    if (entry.isDirectory()) {
      files.push(...(await sourceFilesUnder(path)));
    } else if (entry.isFile() && /\.[ch]$/.test(entry.name)) {
      files.push(path);
    }
  }
  return files;
}

function callBodies(source, functionName) {
  const bodies = [];
  const needle = `${functionName}(`;
  let searchFrom = 0;
  while (true) {
    const start = source.indexOf(needle, searchFrom);
    if (start < 0) return bodies;
    let depth = 1;
    let quote = "";
    let lineComment = false;
    let blockComment = false;
    let escaped = false;
    let end = start + needle.length;
    for (; end < source.length && depth > 0; end++) {
      const ch = source[end];
      const next = source[end + 1];
      if (lineComment) {
        if (ch === "\n") lineComment = false;
        continue;
      }
      if (blockComment) {
        if (ch === "*" && next === "/") {
          blockComment = false;
          end++;
        }
        continue;
      }
      if (quote) {
        if (escaped) {
          escaped = false;
        } else if (ch === "\\") {
          escaped = true;
        } else if (ch === quote) {
          quote = "";
        }
        continue;
      }
      if (ch === "/" && next === "/") {
        lineComment = true;
        end++;
      } else if (ch === "/" && next === "*") {
        blockComment = true;
        end++;
      } else if (ch === '"' || ch === "'") {
        quote = ch;
      } else if (ch === "(") {
        depth++;
      } else if (ch === ")") {
        depth--;
      }
    }
    if (depth !== 0) {
      throw new Error(`unterminated ${functionName} call in design invariant source`);
    }
    bodies.push(source.slice(start, end));
    searchFrom = end;
  }
}

const allSourceFiles = (await sourceFilesUnder("src")).sort();
const removedMutableAstCompatibilityFiles = [
  "src/parser/node_vla_public.h",
  "src/semantic/resolution_state.h",
  "src/semantic/resolution_state_access.h",
  "src/semantic/resolution_store.c",
  "src/semantic/resolution_store.h",
  "src/semantic/resolved_node.c",
  "src/semantic/resolved_node.h",
  "src/semantic/resolved_node_kind.c",
  "src/semantic/resolved_node_kind.h",
  "src/semantic/resolved_node_type.c",
  "src/semantic/resolved_node_type.h",
  "src/semantic/resolved_object_ref.c",
  "src/semantic/resolved_object_ref.h",
  "src/semantic/type_compatibility_cache.c",
  "src/semantic/type_compatibility_cache_internal.h",
  "src/semantic/type_compatibility_cache_storage_internal.h",
];
const remainingMutableAstCompatibilityFiles =
  removedMutableAstCompatibilityFiles.filter(
    (path) => allSourceFiles.includes(path),
  );
if (remainingMutableAstCompatibilityFiles.length) {
  throw new Error(
    "mutable Syntax AST compatibility files must not return:\n" +
      remainingMutableAstCompatibilityFiles.join("\n"),
  );
}
const mutableAstCompatibilityCorpus = `${(
  await Promise.all(allSourceFiles.map((path) => readFile(path, "utf8")))
).join("\n")}\n${await readFile("test/test_parser.c", "utf8")}\n${await readFile("Makefile", "utf8")}`;
if (/\bpsx_(?:resolution_store_t|node_resolution_state_t|resolved_node_kind_t|resolved_object_ref_kind_t)\b|\bpsx_resolution_node[A-Za-z0-9_]*\b|\bps_node_resolution_state(?:_const)?\b|\bps_(?:ctx|lowering)_resolution_store\b/.test(
      mutableAstCompatibilityCorpus,
    )) {
  throw new Error(
    "mutable Syntax AST resolution stores, sidecars, and test-only compatibility APIs must not return",
  );
}
const astHeader = await readFile("src/parser/ast.h", "utf8");
const hirHeader = await readFile("src/hir/hir.h", "utf8");
const hirImplementation = await readFile("src/hir/hir.c", "utf8");
const hirInternalHeader = await readFile(
  "src/hir/hir_internal.h",
  "utf8",
);
const resolvedTreeHir = await readFile(
  "src/semantic/typed_hir_emission.c",
  "utf8",
);
const resolvedTreeHirHeader = await readFile(
  "src/semantic/typed_hir_materialization.h",
  "utf8",
);
const typedHirBuildStatusHeader = await readFile(
  "src/semantic/typed_hir_build_status.h",
  "utf8",
);
const resolvedHirNodeInternalHeader = await readFile(
  "src/semantic/semantic_node_internal.h",
  "utf8",
);
const semanticNodeBuilderHeader = await readFile(
  "src/semantic/semantic_node_builder.h",
  "utf8",
);
const semanticNodeBuilderSource = await readFile(
  "src/semantic/semantic_node_builder.c",
  "utf8",
);
const earlyAstSource = await readFile("src/parser/ast.h", "utf8");
const earlyNodeUtilsSource = await readFile(
  "src/parser/node_utils.c",
  "utf8",
);
if (/\b(?:usage_region|usage_lvar|records_lvar_usage|widen_zext_i64|is_decl_initializer|is_implicit_int_return)\b/.test(
      astHeader,
    ) ||
    /\b(?:bound_base_type|resolved_type)\s*;/.test(astHeader) ||
    /\bpsx_type_name_resolution_state_t\b/.test(
      mutableAstCompatibilityCorpus,
    )) {
  throw new Error(
    "type-name syntax must remain typeless and immutable while Typed HIR owns semantic types",
  );
}
const resolvedTreeHeader = await readFile(
  "src/semantic/typed_hir_tree.h",
  "utf8",
);
const resolvedTreeInternalHeader = await readFile(
  "src/semantic/typed_hir_tree_internal.h",
  "utf8",
);
const hirIrBuilder = `${await readFile(
  "src/lowering/hir_ir_builder.h",
  "utf8",
)}\n${await readFile("src/lowering/hir_ir_builder.c", "utf8")}\n${await readFile(
  "src/lowering/hir_ir_expression.c",
  "utf8",
)}\n${await readFile(
  "src/lowering/hir_ir_call.c",
  "utf8",
)}\n${await readFile(
  "src/lowering/hir_ir_aggregate.c",
  "utf8",
)}\n${await readFile(
  "src/lowering/hir_ir_statement.c",
  "utf8",
)}\n${await readFile(
  "src/lowering/hir_ir_vla.c",
  "utf8",
)}`;
const compilationSession = await readFile(
  "src/compilation_session.c",
  "utf8",
);
const compilationSessionInternal = await readFile(
  "src/compilation_session_internal.h",
  "utf8",
);
const scopeGraphHeader = await readFile(
  "src/semantic/scope_graph.h",
  "utf8",
);
const scopeGraphTypeIdsHeader = await readFile(
  "src/type_system/type_ids.h",
  "utf8",
);
const scopeGraphSource = await readFile(
  "src/semantic/scope_graph.c",
  "utf8",
);
const scopeGraphLocalRegistrySource = await readFile(
  "src/parser/local_registry.c",
  "utf8",
);
const scopeGraphLocalRegistryHeader = await readFile(
  "src/parser/local_registry.h",
  "utf8",
);
const prototypeParameterHeader = await readFile(
  "src/semantic/prototype_parameter.h",
  "utf8",
);
const prototypeParameterSource = await readFile(
  "src/semantic/prototype_parameter.c",
  "utf8",
);
const prototypeDeclarationApplicationSource = await readFile(
  "src/semantic/declaration_application.c",
  "utf8",
);
const prototypeSyntaxResolutionSource = await readFile(
  "src/semantic/syntax_typed_hir_resolution.c",
  "utf8",
);
const scopeGraphNameEnvironmentHeader = await readFile(
  "src/parser/name_environment.h",
  "utf8",
);
const scopeGraphNameEnvironmentSource = await readFile(
  "src/parser/name_environment.c",
  "utf8",
);
const scopeGraphFrontendTranslationUnitSource = await readFile(
  "src/frontend/translation_unit.c",
  "utf8",
);
const scopeGraphGlobalRegistrySource = await readFile(
  "src/parser/global_registry.c",
  "utf8",
);
const scopeGraphSemanticContextSource = await readFile(
  "src/parser/semantic_ctx.c",
  "utf8",
);
const scopeGraphSemanticContextHeader = await readFile(
  "src/parser/semantic_ctx.h",
  "utf8",
);
const scopeGraphIdentifierResolutionSource = await readFile(
  "src/semantic/identifier_resolution.c",
  "utf8",
);
const scopeGraphIdentifierResolutionHeader = await readFile(
  "src/semantic/identifier_resolution.h",
  "utf8",
);
const scopeGraphRegistrySources = [
  scopeGraphLocalRegistrySource,
  scopeGraphGlobalRegistrySource,
  scopeGraphSemanticContextSource,
].join("\n");
const identifierResolverBody =
  scopeGraphIdentifierResolutionSource.match(
    /void\s+psx_resolve_identifier\s*\([^]*?\n}\n\nstatic\s+psx_qual_type_t/,
  )?.[0] ?? "";
if (!/typedef\s+uint32_t\s+psx_scope_id_t\s*;/.test(scopeGraphHeader) ||
    !/typedef\s+uint32_t\s+psx_decl_id_t\s*;/.test(
      scopeGraphTypeIdsHeader,
    ) ||
    !/#include\s+"\.\.\/type_system\/type_ids\.h"/.test(
      scopeGraphHeader,
    ) ||
    /\bpsx_local_lookup_point_t\b/.test(
      scopeGraphLocalRegistryHeader + scopeGraphSemanticContextHeader,
    ) ||
    /ps_local_registry_(?:current_scope_seq|next_scope_seq|capture_lookup_point)_in\s*\(/.test(
      scopeGraphLocalRegistryHeader + scopeGraphLocalRegistrySource,
    ) ||
    !/unsigned\s+next_scope_id\s*;/.test(
      scopeGraphNameEnvironmentHeader,
    ) ||
    /next_scope_seq/.test(
      scopeGraphNameEnvironmentHeader + scopeGraphNameEnvironmentSource,
    ) ||
    !/current_scope_seq\s*=\s*environment->next_scope_id\+\+\s*;/.test(
      scopeGraphNameEnvironmentSource,
    ) ||
    !/psx_scope_graph_capture_lookup_point\s*\(scope_graph\)/.test(
      scopeGraphFrontendTranslationUnitSource,
    ) ||
    !/psx_scope_graph_next_scope_id\s*\(scope_graph\)/.test(
      scopeGraphFrontendTranslationUnitSource,
    ) ||
    !/PSX_NAMESPACE_ORDINARY/.test(scopeGraphHeader) ||
    !/PSX_NAMESPACE_TAG/.test(scopeGraphHeader) ||
    !/PSX_NAMESPACE_LABEL/.test(scopeGraphHeader) ||
    !/PSX_NAMESPACE_MEMBER/.test(scopeGraphHeader) ||
    !/psx_scope_graph_t\s*\*scope_graph\s*;/.test(
      compilationSessionInternal,
    ) ||
    !/session->scope_graph\s*=\s*psx_scope_graph_create\s*\(/.test(
      compilationSession,
    ) ||
    !/session->global_registry\s*=\s*ps_global_registry_create\s*\(\s*ps_ctx_semantic_type_table_in\s*\(\s*session->semantic_context\s*\)\s*,\s*session->scope_graph\s*\)/.test(
      compilationSession,
    ) ||
    !/session->local_registry\s*=\s*ps_local_registry_create\s*\(\s*session->diagnostic_context\s*,\s*ps_ctx_semantic_type_table_in\s*\(\s*session->semantic_context\s*\)\s*,\s*session->scope_graph\s*\)/.test(
      compilationSession,
    ) ||
    /ps_(?:global|local)_registry_bind_scope_graph\s*\(/.test(
      scopeGraphRegistrySources + compilationSession,
    ) ||
    !/context->scope_graph\s*=\s*scope_graph\s*;/.test(
      scopeGraphSemanticContextSource,
    ) ||
    /ps_ctx_bind_scope_graph\s*\(/.test(
      scopeGraphSemanticContextSource + compilationSession,
    ) ||
    /psx_scope_graph_create\s*\(|owns_scope_graph/.test(
      scopeGraphSemanticContextSource,
    ) ||
    /scope_graph\s*=\s*NULL/.test(scopeGraphSemanticContextSource) ||
    /psx_scope_graph_reset\s*\(/.test(scopeGraphGlobalRegistrySource) ||
    !/ag_compilation_session_reset_translation_unit\s*\([^]*?psx_scope_graph_reset\s*\(session->scope_graph\)/.test(
      compilationSession,
    ) ||
    !/ag_compilation_session_is_complete\s*\([^]*?ps_ctx_scope_graph\s*\([^]*?==\s*session->scope_graph[^]*?ps_global_registry_scope_graph\s*\([^]*?==\s*session->scope_graph[^]*?ps_local_registry_scope_graph\s*\([^]*?==\s*session->scope_graph/.test(
      compilationSession,
    ) ||
    /\b(?:gvars_by_bucket|lvars_by_bucket|tags_by_bucket|aggregate_members_by_bucket|enum_entries_by_bucket|typedef_entries_by_bucket|function_symbols_by_bucket|label_definitions_by_bucket|next_hash)\b/.test(
      scopeGraphRegistrySources,
    ) ||
    /\bfind_tag_type_at_scope_in\s*\(|\bps_ctx_(?:get|find)_tag_member_at_scope_in\s*\(|\bps_ctx_get_tag_member_count_at_scope_in\s*\(/.test(
      `${scopeGraphSemanticContextHeader}\n${scopeGraphSemanticContextSource}`,
    ) ||
    /\b(?:current_scope_seq|next_declaration_seq|scope_parent_by_seq)\s*;/.test(
      scopeGraphLocalRegistrySource,
    ) ||
    !/psx_scope_graph_lookup_declaration_in_scope\s*\(/.test(
      scopeGraphLocalRegistrySource,
    ) ||
    !/psx_scope_graph_declare\s*\([^]*?PSX_DECL_ENUM_CONSTANT/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/psx_scope_graph_declare\s*\([^]*?PSX_DECL_TYPEDEF/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/psx_scope_graph_declare_at\s*\([^]*?PSX_DECL_FUNCTION/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/psx_scope_graph_declare\s*\([^]*?PSX_NAMESPACE_TAG[^]*?PSX_DECL_TAG/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/ps_ctx_tag_qual_type_at_in\s*\([^]*?psx_scope_graph_lookup\s*\([^]*?PSX_NAMESPACE_TAG/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/ps_ctx_intern_enum_qual_type_in\s*\([^]*?psx_scope_graph_declaration\s*\([^]*?tag_type_from_declaration_in\s*\([^]*?tag->kind\s*!=\s*TK_ENUM[^]*?psx_semantic_type_table_intern_enum\s*\([^]*?declaration->id/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/ps_ctx_find_tag_kind_at_current_scope_in\s*\([^]*?psx_scope_graph_lookup_in_scope\s*\([^]*?PSX_NAMESPACE_TAG/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/ensure_tag_member_scope_in\s*\([^]*?psx_scope_graph_create_scope_at\s*\([^]*?PSX_SCOPE_RECORD/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/insert_tag_member_record_in\s*\([^]*?psx_scope_graph_declare_at\s*\([^]*?PSX_NAMESPACE_MEMBER[^]*?PSX_DECL_MEMBER/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/ps_ctx_find_record_member_in\s*\([^]*?psx_scope_graph_lookup_declaration_in_scope\s*\([^]*?PSX_NAMESPACE_MEMBER/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/ps_ctx_scope_graph\s*\(\s*request->semantic_context\s*\)[^]*?psx_scope_graph_lookup\s*\([^]*?switch\s*\(declaration->kind\)/.test(
      scopeGraphIdentifierResolutionSource,
    ) ||
    !identifierResolverBody ||
    /\bpsx_(?:global|local)_registry_t\b/.test(
      scopeGraphIdentifierResolutionHeader,
    ) ||
    /\b(?:ps_decl_find_lvar_in|ps_local_registry_find_visible_in|ps_ctx_find_enum_const_in|ps_ctx_find_enum_const_at_in(?:_contexts)?|ps_ctx_find_function_symbol_in|psx_resolve_global_object_symbol_in)\s*\(/.test(
      identifierResolverBody,
    )) {
  throw new Error(
    "CompilationSession must own one ScopeId/DeclId graph with all C namespaces",
  );
}
const prototypeParameterStruct = prototypeParameterSource.match(
  /struct\s+psx_prototype_parameter_t\s*\{([^{}]*)\};/,
);
if (!/PSX_DECL_PARAMETER/.test(scopeGraphHeader) ||
    !prototypeParameterStruct ||
    !/psx_qual_type_t\s+declaration_qual_type\s*;/.test(
      prototypeParameterStruct[1],
    ) ||
    /\blvar_t\b|storage|offset/.test(prototypeParameterStruct[1]) ||
    !/psx_scope_graph_declare\s*\([^]*?PSX_DECL_PARAMETER/.test(
      prototypeParameterSource,
    ) ||
    !/PSX_SCOPE_FUNCTION_PROTOTYPE/.test(prototypeParameterSource) ||
    !/case\s+PSX_DECL_PARAMETER:[^]*?PSX_IDENTIFIER_PARAMETER/.test(
      scopeGraphIdentifierResolutionSource,
    ) ||
    !/case\s+PSX_IDENTIFIER_PARAMETER:[^]*?psx_prototype_parameter_qual_type/.test(
      scopeGraphIdentifierResolutionSource,
    ) ||
    !/psx_scope_graph_enter_scope\s*\([^]*?PSX_SCOPE_FUNCTION_PROTOTYPE/.test(
      prototypeDeclarationApplicationSource,
    ) ||
    !/psx_declare_prototype_parameter_in\s*\(/.test(
      prototypeDeclarationApplicationSource,
    ) ||
    /ps_local_registry_(?:create_type_binding|enter_prototype_scope)_in\s*\(/.test(
      scopeGraphLocalRegistryHeader + scopeGraphLocalRegistrySource +
        prototypeDeclarationApplicationSource,
    ) ||
    !/PSX_HIR_PROTOTYPE_PARAMETER_REF/.test(hirHeader) ||
    !/case\s+PSX_HIR_PROTOTYPE_PARAMETER_REF:/.test(hirImplementation) ||
    !/PSX_IDENTIFIER_PARAMETER[^]*?PSX_HIR_PROTOTYPE_PARAMETER_REF/.test(
      prototypeSyntaxResolutionSource,
    ) ||
    /PSX_HIR_PROTOTYPE_PARAMETER_REF/.test(hirIrBuilder)) {
  throw new Error(
    "prototype parameters must be QualType-only scope declarations and must not masquerade as local storage",
  );
}
const enumConstantPayload =
  scopeGraphSemanticContextSource.match(
    /struct\s+enum_const_t\s*\{([^}]*)\};/,
  )?.[1] ?? "";
const typedefPayload =
  scopeGraphSemanticContextSource.match(
    /struct\s+typedef_name_t\s*\{([^}]*)\};/,
  )?.[1] ?? "";
if (!enumConstantPayload || !typedefPayload ||
    !/\blong\s+long\s+value\s*;/.test(enumConstantPayload) ||
    !/\bpsx_qual_type_t\s+decl_qual_type\s*;/.test(typedefPayload) ||
    /\b(?:name|len|declaration_id|next_all|scope_depth|scope_seq|declaration_seq)\b/.test(
      `${enumConstantPayload}\n${typedefPayload}`,
    ) ||
    /\b(?:enum_entries_all|typedef_entries_all)\b/.test(
      scopeGraphSemanticContextSource,
    )) {
  throw new Error(
    "typedef and enum payloads must not duplicate scope graph identity or visibility state",
  );
}
const aggregateMemberPayload =
  scopeGraphSemanticContextSource.match(
    /struct\s+tag_member_t\s*\{([^}]*)\};/,
  )?.[1] ?? "";
if (!aggregateMemberPayload ||
    !/\btag_member_decl_t\s+declaration\s*;/.test(
      aggregateMemberPayload,
    ) ||
    /\b(?:next_all|owner|declaration_id|tag_kind|tag_name|tag_len|decl_order|scope_depth)\b/.test(
      aggregateMemberPayload,
    ) ||
    /\b(?:aggregate_members_all|aggregate_member_decl_order)\b/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/is_unnamed_member\s*=\s*[^;]*PSX_NAMESPACE_MEMBER[^;]*PSX_DECL_MEMBER[^;]*!name[^;]*name_len\s*==\s*0/.test(
      scopeGraphSource,
    ) ||
    !/is_invalid_member_scope\s*=\s*[^;]*PSX_NAMESPACE_MEMBER[^;]*PSX_DECL_MEMBER[^;]*PSX_SCOPE_RECORD/.test(
      scopeGraphSource,
    ) ||
    !/collect_tag_member_declarations_in\s*\([^]*?psx_scope_graph_declaration_at\s*\([^]*?declaration->scope_id\s*!=\s*tag->member_scope_id/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/insert_tag_member_record_in\s*\([^]*?psx_scope_graph_declare_at\s*\([^]*?declaration->len\s*>\s*0\s*\?\s*declaration->name\s*:\s*NULL/.test(
      scopeGraphSemanticContextSource,
    )) {
  throw new Error(
    "record scopes must own named and unnamed member identity and declaration order",
  );
}
const tagPayload =
  scopeGraphSemanticContextSource.match(
    /struct\s+tag_type_t\s*\{([^}]*)\};/,
  )?.[1] ?? "";
if (!tagPayload ||
    /\b(?:next_all|scope_depth|scope_seq|declaration_seq|declaration_id)\b/.test(
      tagPayload,
    ) ||
    /\btags_all\b/.test(scopeGraphSemanticContextSource) ||
    /\bint\s+scope_depth\s*;/.test(scopeGraphSemanticContextSource) ||
    /\bps_ctx_(?:reset_function_scope|enter_block_scope|leave_block_scope)_in\s*\(/.test(
      `${scopeGraphSemanticContextSource}\n${scopeGraphSemanticContextHeader}`,
    ) ||
    !/tag_declaration_for_payload_in\s*\([^]*?psx_scope_graph_declaration_at\s*\([^]*?declaration->payload\s*==\s*tag/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/ps_ctx_current_tag_scope_depth_in\s*\([^]*?psx_scope_graph_scope_depth\s*\(/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/find_tag_type_by_record_id_in\s*\([^]*?psx_scope_graph_declaration_at\s*\(/.test(
      scopeGraphSemanticContextSource,
    )) {
  throw new Error(
    "tag identity and lexical depth must come exclusively from scope graph declarations",
  );
}
const functionSymbolPayload =
  scopeGraphSemanticContextSource.match(
    /struct\s+psx_function_symbol_t\s*\{([\s\S]*?)\n\};/,
  )?.[1] ?? "";
if (!functionSymbolPayload ||
    /\b(?:next_all|name|len|declaration_id)\b/.test(
      functionSymbolPayload,
    ) ||
    /\bfunction_symbols_all\b/.test(scopeGraphSemanticContextSource) ||
    !/ps_ctx_reset_function_names_in\s*\([^]*?psx_scope_graph_declaration_at\s*\([^]*?psx_scope_graph_forget_declaration\s*\(/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/ps_ctx_reset_function_diag_state_in\s*\([^]*?psx_scope_graph_declaration_at\s*\(/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/ps_ctx_rollback_function_registration_in\s*\([^]*?psx_scope_graph_forget_declaration\s*\([^]*?ctx_release_in\s*\(/.test(
      scopeGraphSemanticContextSource,
    ) ||
    /f->(?:next_all|name|len|declaration_id)\b/.test(
      scopeGraphSemanticContextSource,
    )) {
  throw new Error(
    "function symbol identity and enumeration must come exclusively from scope graph declarations",
  );
}
const semanticIntegerConstructionSource = (
  await Promise.all(
    allSourceFiles
      .filter((path) =>
        path !== "src/parser/type.c" &&
        path !== "src/parser/type_builder.h")
      .map((path) => readFile(path, "utf8")),
  )
).join("\n");
if (/\bps_type_new_integer_in\s*\(/.test(
      semanticIntegerConstructionSource,
    )) {
  throw new Error(
    "semantic integer construction must use psx_integer_kind_t instead of parser token kinds",
  );
}
const semanticFloatingConstructionSource = (
  await Promise.all(
    allSourceFiles
      .filter((path) =>
        path !== "src/parser/expr.c" &&
        path !== "src/parser/type.c" &&
        path !== "src/parser/type_builder.h")
      .map((path) => readFile(path, "utf8")),
  )
).join("\n");
if (/\bps_type_new_float_in\s*\(/.test(
      semanticFloatingConstructionSource,
    )) {
  throw new Error(
    "semantic floating construction must use psx_floating_kind_t instead of tokenizer literal kinds",
  );
}
const frontendSourceFiles = allSourceFiles.filter(
  (path) => path.startsWith("src/frontend/"),
);
const frontendLayerSource = (
  await Promise.all(frontendSourceFiles.map((path) => readFile(path, "utf8")))
).join("\n");
if (/\b(?:ps_ctx_active|ps_global_registry_active|ps_local_registry_active)\s*\(/.test(
      frontendLayerSource,
    )) {
  throw new Error(
    "frontend APIs must receive CompilationSession or explicit registries",
  );
}
const removedContextFreeFrontendApis = [
  "psx_frontend_analyze_function",
  "psx_frontend_analyze_expression",
  "psx_frontend_analyze_initializer_syntax",
  "psx_frontend_analyze_program",
  "psx_apply_function_definition_header",
  "psx_apply_function_definition",
  "psx_frontend_local_declaration_callbacks",
  "psx_apply_toplevel_declaration",
  "psx_frontend_toplevel_declaration_callbacks",
];
for (const name of removedContextFreeFrontendApis) {
  if (new RegExp(`\\b${name}\\s*\\(`).test(frontendLayerSource)) {
    throw new Error(`context-free frontend API ${name} must not return`);
  }
}
const explicitSemanticSourceFiles = allSourceFiles.filter(
  (path) => path.startsWith("src/semantic/"),
);
const explicitSemanticLayerSource = (
  await Promise.all(
    explicitSemanticSourceFiles.map((path) => readFile(path, "utf8")),
  )
).join("\n");
if (/\b(?:ps_ctx_active|ps_global_registry_active|ps_local_registry_active)\s*\(/.test(
      explicitSemanticLayerSource,
    )) {
  throw new Error(
    "semantic APIs must receive explicit contexts",
  );
}
if (/\btag_member_info_t\b/.test(explicitSemanticLayerSource) ||
    /\bps_ctx_(?:get|find)_tag_member_info/.test(explicitSemanticLayerSource)) {
  throw new Error(
    "semantic passes must resolve aggregate members through RecordDecl",
  );
}
const nonParserTypeConsumerSource = (
  await Promise.all(
    allSourceFiles
      .filter((path) =>
        path.startsWith("src/semantic/") ||
        path.startsWith("src/lowering/") ||
        path.startsWith("src/frontend/"))
      .map((path) => readFile(path, "utf8")),
  )
).join("\n");
if (/#include\s+"\.\.\/parser\/(?:tag_public|tag_member_public)\.h"/.test(
      nonParserTypeConsumerSource,
    )) {
  throw new Error(
    "semantic, lowering, and frontend layers must not depend on parser member compatibility headers",
  );
}
if (/\b(?:node|source|src|value|literal|selection|query|callee)->(?:type|qual_type|type_state)\b|->base\.(?:type|qual_type|type_state)\b/.test(
      nonParserTypeConsumerSource,
    )) {
  throw new Error(
    "semantic, lowering, and frontend layers must access node type state through node APIs",
  );
}
const removedContextFreeSemanticApis = [
  "psx_build_decl_specifier_type",
  "psx_resolve_decl_specifier_syntax",
  "psx_resolve_prepared_enum_const_expr",
  "psx_resolve_declarator_syntax",
  "psx_resolve_generic_selection",
  "psx_resolve_global_object_symbol",
  "psx_collect_lvar_usage_events",
  "psx_analyze_function_lvar_usage",
  "psx_semantic_resolve_tree",
  "psx_semantic_resolve_initializer_tree",
  "psx_bind_type_name_ref",
  "psx_resolve_bound_type_name_ref",
  "psx_resolve_sizeof_query",
  "psx_resolve_alignof_query",
  "psx_apply_parsed_enum_body",
  "psx_apply_parsed_aggregate_body_layout",
  "psx_apply_parsed_type_name",
  "psx_apply_parsed_declarator_type",
  "psx_apply_runtime_declarator_type",
  "psx_apply_declaration_phase",
  "psx_apply_parsed_decl_specifier",
  "psx_apply_parsed_standalone_tag",
  "psx_apply_parsed_declarator",
  "psx_apply_runtime_parsed_declarator",
  "psx_apply_runtime_parsed_declarator_ex",
  "psx_apply_parsed_function_parameters",
];
for (const name of removedContextFreeSemanticApis) {
  if (new RegExp(`\\b${name}\\s*\\(`).test(explicitSemanticLayerSource)) {
    throw new Error(`context-free semantic API ${name} must not return`);
  }
}
const loweringSourceFiles = allSourceFiles.filter(
  (path) => path.startsWith("src/lowering/"),
);
const loweringLayerSource = (
  await Promise.all(loweringSourceFiles.map((path) => readFile(path, "utf8")))
).join("\n");
if (/\bps_lvar_(?:storage_size|decl_sizeof|elem_size)\s*\(/.test(
      loweringLayerSource,
    )) {
  throw new Error(
    "lowering must separate frame storage from TypeId target layout",
  );
}
if (/\baggregate_definition\b/.test(loweringLayerSource)) {
  throw new Error(
    "lowering must resolve aggregate identity through RecordId and RecordDeclTable",
  );
}
if (/#include\s+["<][^">]*parser\/(?:ast|node_utils|decl)\.h[">]/.test(
      loweringLayerSource,
    ) ||
    /\bnode_t\b|\bnode_kind_t\b/.test(loweringLayerSource) ||
    /\blower_(?:global_object|static_local)_declaration\s*\(/.test(
      loweringLayerSource,
    )) {
  throw new Error(
    "lowering must consume semantic IDs and resolved plans without Syntax AST compatibility entry points",
  );
}
const explicitArenaDeclarationPipelineSource = await readFile(
  "src/declaration_pipeline.c",
  "utf8",
);
const parserExpressionSource = await readFile("src/parser/expr.c", "utf8");
const expressionSyntaxContextSource = await readFile(
  "src/parser/expression_syntax_context.h",
  "utf8",
);
const expressionSyntaxAdapterSource = await readFile(
  "src/parser/expression_syntax_adapter.c",
  "utf8",
);
const statementSyntaxContextSource = await readFile(
  "src/parser/statement_syntax_context.h",
  "utf8",
);
const statementSyntaxAdapterSource = await readFile(
  "src/parser/statement_syntax_adapter.c",
  "utf8",
);
const statementSyntaxAdapterHeader = await readFile(
  "src/parser/statement_syntax_adapter.h",
  "utf8",
);
const parserStatementSource = await readFile("src/parser/stmt.c", "utf8");
const parserLocalDeclarationSource = await readFile(
  "src/parser/local_declaration_syntax.c",
  "utf8",
);
const explicitPhaseArenaSource = [
  frontendLayerSource,
  explicitSemanticLayerSource,
  loweringLayerSource,
  explicitArenaDeclarationPipelineSource,
  await readFile("src/parser/declaration_syntax.c", "utf8"),
  await readFile("src/parser/declarator_syntax.c", "utf8"),
  parserExpressionSource,
  parserStatementSource,
  await readFile("src/parser/expr.c", "utf8"),
  parserLocalDeclarationSource,
  await readFile("src/main.c", "utf8"),
].join("\n");
if (/\barena_(?:alloc|total_reserved_bytes)\s*\(/.test(
      explicitPhaseArenaSource,
    )) {
  throw new Error(
    "frontend, semantic, declaration, lowering, declarator syntax, and driver code must use an explicit CompilationSession-owned arena",
  );
}
const compilationSessionArenaSource = await readFile(
  "src/compilation_session.c",
  "utf8",
);
if (/\barena_context_(?:activate|active)\s*\(/.test(
      compilationSessionArenaSource,
    )) {
  throw new Error(
    "CompilationSession activation must not mutate a process-global arena",
  );
}
const parserRuntimeOwnershipHeader = await readFile(
  "src/parser/runtime_context.h",
  "utf8",
);
const implicitTokenizerCursorApiRe = /\b(?:tk_get_current_token|tk_set_current_token|tk_consume|tk_consume_str|tk_consume_ident|tk_expect|tk_expect_number|tk_at_eof|tk_ensure_lookahead)\s*\(/;
const explicitParserTokenizerSources = [
  parserStatementSource,
  await readFile("src/parser/parser.c", "utf8"),
  await readFile("src/parser/decl.c", "utf8"),
  await readFile("src/parser/local_registry.c", "utf8"),
  await readFile("src/parser/node_utils.c", "utf8"),
  await readFile("src/parser/initializer_syntax.c", "utf8"),
  await readFile("src/parser/static_assert_declaration.c", "utf8"),
  await readFile("src/parser/declarator_syntax.c", "utf8"),
  await readFile("src/parser/declaration_syntax.c", "utf8"),
  await readFile("src/parser/function_parameter_syntax.c", "utf8"),
  await readFile("src/parser/aggregate_member_syntax.c", "utf8"),
  await readFile("src/parser/local_declaration_syntax.c", "utf8"),
  await readFile("src/parser/toplevel_declaration_syntax.c", "utf8"),
  await readFile("src/parser/enum_const.c", "utf8"),
];
const parserCoreHeader = await readFile("src/parser/core.h", "utf8");
if (!/tokenizer_context_t\s*\*tokenizer_context\s*;/.test(
      parserRuntimeOwnershipHeader,
    ) ||
    !/ps_parser_runtime_context_create\s*\(\s*session->arena_context\s*,\s*&session->tokenizer\s*,\s*session->diagnostic_context\s*\)/.test(
      compilationSessionArenaSource,
    ) ||
    !/ag_diagnostic_context_t\s*\*diagnostic_context\s*;/.test(
      parserRuntimeOwnershipHeader,
    ) ||
    !/ps_parser_runtime_tokenizer\s*\(\s*runtime_context\s*\)/.test(
      parserStatementSource,
    ) ||
    /\bpsx_consume_type_kind_ex\s*\(/.test(parserCoreHeader) ||
    !/tokenizer_context_t\s*\*tokenizer_context\s*;/.test(
      parserCoreHeader,
    ) ||
    explicitParserTokenizerSources.some((source) =>
      implicitTokenizerCursorApiRe.test(source)
    )) {
  throw new Error(
    "migrated parser syntax must use the CompilationSession-owned tokenizer through parser runtime",
  );
}
if (/\bps_type_(?:new(?:_integer|_enum|_float|_pointer|_function|_array|_tag)?|clone|apply_declarator_shape|adjust_parameter_type|binary_result|conditional_result|address_result|decay_array|subscript_result|generic_control)\s*\(/.test(
      explicitPhaseArenaSource,
    )) {
  throw new Error(
    "frontend, semantic, declaration, lowering, parser expression, and driver code must allocate types in an explicit CompilationSession-owned arena",
  );
}
const explicitFrontendSemanticAndExpressionNodeSource = [
  frontendLayerSource,
  explicitSemanticLayerSource,
  loweringLayerSource,
  explicitArenaDeclarationPipelineSource,
  parserExpressionSource,
  parserStatementSource,
  parserLocalDeclarationSource,
].join("\n");
const implicitArenaNodeConstructorRe = new RegExp(
  "\\b(?:" +
    [
      "ps_node_new_binary",
      "psx_node_new_raw_binary",
      "ps_node_new_num",
      "ps_node_new_shift_trunc_extend",
      "ps_node_new_fp_to_int_cast",
      "ps_node_new_int_to_fp_cast",
      "ps_node_new_integer_cast_result",
      "ps_node_new_integer_cast_result_ex",
      "ps_node_new_i64_to_i32_trunc_cast",
      "ps_node_new_pointer_cast_result",
      "ps_node_new_aggregate_cast_result",
      "ps_node_new_void_cast_result",
      "psx_node_new_lvar",
      "ps_node_new_lvar_typed",
      "ps_node_new_lvar_typed_at_for",
      "ps_node_new_lvar_type_at_for",
      "psx_node_new_lvar_scalar_slot_at",
      "psx_node_new_lvar_fp_slot_at",
      "ps_node_new_lvar_fp_slot_for",
      "ps_node_new_param_placeholder",
      "ps_node_new_unsigned_lvar_typed",
      "psx_node_new_lvar_for",
      "psx_node_new_lvar_object_ref_for",
      "ps_node_new_lvar_expr_ref_for",
      "psx_node_new_lvar_identifier_ref_for",
      "psx_node_new_vla_decay_ref_for",
      "ps_node_new_param_lvar_for",
      "ps_node_new_array_elem_lvar_for",
      "ps_node_new_tag_member_lvar_ref_for",
      "ps_node_new_gvar_for",
      "psx_node_new_gvar_array_base_for",
      "psx_node_new_static_local_gvar_for",
      "psx_node_new_source_cast",
      "ps_node_new_gvar_array_addr_for",
      "psx_node_new_static_local_array_addr_for",
      "ps_node_new_lvar_array_addr_for",
      "ps_node_new_addr_value_for",
      "ps_node_new_explicit_addr_value_for",
      "ps_node_new_unary_addr_for",
      "ps_node_new_tag_member_deref_for",
      "ps_node_new_unary_deref_for",
      "psx_node_new_unary_deref_syntax_for",
      "psx_node_new_subscript_syntax_for",
      "ps_node_new_subscript_deref_for",
      "ps_node_clone_lvalue_with_lhs",
      "ps_node_new_vla_runtime",
      "ps_node_new_assign",
      "psx_node_new_raw_assign",
      "psx_node_new_raw_decl_initializer",
      "psx_node_new_compound_literal",
      "psx_node_new_raw_decl_initializer_list",
      "psx_node_new_initializer_list",
    ].join("|") +
    ")\\s*\\(",
);
if (implicitArenaNodeConstructorRe.test(
      explicitFrontendSemanticAndExpressionNodeSource,
    )) {
  throw new Error(
    "frontend, semantic, declaration, lowering, and parser syntax code must allocate constructed AST nodes in an explicit CompilationSession-owned arena",
  );
}
if (/\b(?:ps_ctx_active|ps_global_registry_active|ps_local_registry_active)\s*\(/.test(
      loweringLayerSource,
    )) {
  throw new Error(
    "lowering APIs must receive semantic and registry contexts explicitly",
  );
}
if (/\bps_find_string_lit_by_label(?:_in)?\s*\(/.test(loweringLayerSource)) {
  throw new Error(
    "lowering must consume resolved string literal contents from the AST",
  );
}
const removedContextFreeLoweringApis = [
  "psx_lower_semantic_tree",
  "psx_lower_semantic_initializer_syntax",
  "lower_compound_literal_expression",
  "lower_member_access_expression",
];
for (const name of removedContextFreeLoweringApis) {
  if (new RegExp(`\\b${name}\\s*\\(`).test(loweringLayerSource)) {
    throw new Error(`context-free lowering API ${name} must not return`);
  }
}
const parserSourceFiles = allSourceFiles.filter(
  (path) => path.startsWith("src/parser/"),
);
const parserLayerSource = (
  await Promise.all(parserSourceFiles.map((path) => readFile(path, "utf8")))
).join("\n");
if (/\bps_node_(?:type|storage_type|deref|aggregate_value)_size\s*\(/.test(
      explicitSemanticLayerSource,
    )) {
  throw new Error(
    "semantic passes must classify types or query target layout instead of reading parser node sizes",
  );
}
const gvarPublicHeaderSource = await readFile(
  "src/parser/gvar_public.h",
  "utf8",
);
const translationUnitDataLoweringSourceForMemberLayout = await readFile(
  "src/lowering/translation_unit_data_lowering.c",
  "utf8",
);
const irSymbolLoweringSourceForMemberLayout = await readFile(
  "src/lowering/ir_symbol_lowering.c",
  "utf8",
);
if (/\bps_tag_member_decl_(?:storage|value)_size\s*\(/.test(
      `${parserLayerSource}\n${explicitSemanticLayerSource}\n${loweringLayerSource}`,
    ) ||
    !/ps_gvar_init_member_value\s*\([^;]*\bint\s+member_size\s*\)/s.test(
      gvarPublicHeaderSource,
    ) ||
    !/ps_gvar_init_member_value\s*\([^;]*type_size_id\s*\(/s.test(
      translationUnitDataLoweringSourceForMemberLayout,
    ) ||
    !/int\s+value_size\s*=\s*psx_type_layout_sizeof\s*\(/.test(
      irSymbolLoweringSourceForMemberLayout,
    ) ||
    !/ps_gvar_init_member_value\s*\(\s*ctx->semantic_types\s*,\s*ctx->global\s*,\s*slot\s*,\s*member\s*,\s*value_size\s*\)/.test(
      irSymbolLoweringSourceForMemberLayout,
    ) ||
    /\btag_member_info_t\b/.test(gvarPublicHeaderSource) ||
    /\btag_member_info_t\b/.test(
      translationUnitDataLoweringSourceForMemberLayout,
    ) ||
    /\btag_member_info_t\b/.test(
      irSymbolLoweringSourceForMemberLayout,
    ) ||
    !/bitfield_member\)\s*\(\s*void\s*\*[^,]*,\s*const\s+psx_record_member_decl_t\s*\*[^,]*,\s*const\s+psx_record_member_layout_t\s*\*/s.test(
      gvarPublicHeaderSource,
    )) {
  throw new Error(
    "member descriptors must remain layout-free and lowering must pass member size from TypeId and TargetSpec",
  );
}
const parserContextLifecycleFiles = new Set([
  "src/parser/semantic_ctx.c",
  "src/parser/semantic_ctx.h",
  "src/parser/global_registry.c",
  "src/parser/global_registry.h",
  "src/parser/local_registry.c",
  "src/parser/local_registry.h",
]);
const ordinaryParserLayerSource = (
  await Promise.all(
    parserSourceFiles
      .filter((path) => !parserContextLifecycleFiles.has(path))
      .map((path) => readFile(path, "utf8")),
  )
).join("\n");
if (/\b(?:ps_ctx_active|ps_global_registry_active|ps_local_registry_active)\s*\(/.test(
      ordinaryParserLayerSource,
    )) {
  throw new Error(
    "ordinary parser APIs must receive explicit CompilationSession-owned contexts",
  );
}
const parserRuntimeLifecycleFiles = new Set([
  "src/parser/runtime_context.c",
  "src/parser/runtime_context.h",
]);
const parserSyntaxSource = (
  await Promise.all(
    parserSourceFiles
      .filter((path) => !parserContextLifecycleFiles.has(path))
      .filter((path) => !parserRuntimeLifecycleFiles.has(path))
      .map((path) => readFile(path, "utf8")),
  )
).join("\n");
if (/\bps_parser_runtime_context_active\s*\(/.test(parserSyntaxSource)) {
  throw new Error(
    "ordinary parser syntax must receive explicit CompilationSession-owned runtime context",
  );
}
const removedContextFreeParserApis = [
  "ps_type_sizeof",
  "ps_type_deref_size",
  "ps_type_pointee_value_size",
  "ps_type_array_scalar_element_size",
  "ps_type_array_subscript_stride_bytes",
  "ps_type_subscript_static_stride",
  "ps_type_pointer_view_structural_base_deref_size",
  "ps_type_pointer_view_structural_ptr_array_pointee_bytes",
  "ps_type_integer_promotion_is_unsigned",
  "ps_type_usual_arithmetic_result_is_unsigned",
  "ps_node_integer_promotion_is_unsigned",
  "ps_node_usual_arith_operands_is_unsigned",
  "ps_node_usual_arith_is_unsigned",
  "ps_type_usual_arithmetic_result_in",
  "ps_type_binary_result_in",
  "ps_type_conditional_result_in",
  "ps_type_format_canonical_signature",
  "psx_eval_parsed_alignas_value",
  "psx_parse_case_const_expr",
  "psx_eval_parsed_enum_const_expr",
  "psx_parse_initializer_syntax_value",
  "psx_parse_initializer_syntax_list",
  "psx_parse_static_assert_syntax",
  "psx_parse_statement_expression",
  "psx_decl_parse_initializer_for_var",
  "psx_expr_expr",
  "psx_expr_assign",
  "psx_stmt_stmt",
  "ps_parser_stream_begin",
  "ps_expr",
  "ps_expr_from",
  "ps_expr_ctx",
  "psx_parse_function_parameters_syntax",
  "psx_parse_function_parameters_syntax_ex",
  "psx_parse_function_parameters_syntax_with_typedef_lookup",
  "psx_parse_aggregate_body",
  "psx_parse_toplevel_declaration_syntax",
  "psx_parse_toplevel_declaration_head_syntax",
  "psx_finish_toplevel_declaration_syntax",
  "ps_decl_reset_locals",
  "ps_decl_reset_translation_unit_state",
  "ps_decl_set_current_funcname",
  "ps_decl_get_current_funcname",
  "psx_parse_decl_specifier_syntax",
  "psx_parse_declarator_syntax_tree",
  "psx_parse_declarator_syntax_tree_into",
  "psx_parse_declarator_syntax_tree_into_with_typedef_lookup",
  "psx_parse_toplevel_declarator_syntax_tree",
  "psx_try_parse_toplevel_declarator_syntax_tree",
  "psx_try_parse_toplevel_declarator_syntax_tree_with_typedef_lookup",
  "psx_parse_abstract_declarator_syntax_tree",
  "psx_parse_parameter_declarator_syntax_tree",
  "ps_parse_runtime_declarator_expressions",
  "ps_prepare_constant_declarator_expressions",
  "ps_prepare_decl_specifier_alignments",
  "psx_parse_alignas_value",
  "psx_parse_enum_const_expr",
  "ps_gvar_symbol_ref_named_function",
  "ps_gvar_init_value_named_function",
  "ps_gvar_walk_aggregate_initializer",
  "ps_tag_flat_cover_state_note",
  "ps_tag_find_unnamed_union_covering_offset",
  "ps_tag_member_flat_slots",
  "ps_tag_member_elem_flat_slots",
  "ps_tag_member_subscript_stride_slots",
  "ps_tag_flat_slot_count",
  "ps_tag_member_at_flat_slot",
  "ps_tag_next_named_member",
  "ps_tag_first_named_member",
  "ps_tag_find_named_member",
  "ps_tag_select_union_member_for_init_slot",
  "ps_tag_union_init_member_for_slot",
  "ps_tag_member_designator_slot",
  "psx_register_string_lit",
  "psx_register_float_lit",
  "ps_find_string_lit_by_label",
  "psx_make_anonymous_tag_name",
  "ps_anon_tag_reset_translation_unit_state",
  "psx_try_consume_pragma_pack_marker",
  "ps_parser_mark_recoverable_syntax_error",
  "ps_parser_has_recoverable_syntax_error",
  "ps_parser_enter_recovery_block",
  "ps_parser_leave_recovery_block",
  "pragma_pack_push",
  "pragma_pack_pop",
  "pragma_pack_set",
  "pragma_pack_reset",
  "pragma_pack_current_alignment",
];
for (const name of removedContextFreeParserApis) {
  if (new RegExp(`\\b${name}\\s*\\(`).test(parserLayerSource)) {
    throw new Error(`context-free parser API ${name} must not return`);
  }
}
for (const path of [
  "src/parser/array_suffixes.c",
  "src/parser/array_suffixes.h",
]) {
  if (allSourceFiles.includes(path)) {
    throw new Error(`unused legacy parser file ${path} must not return`);
  }
}
const forbiddenCompatibilityFiles = [
  "src/parser/config_runtime.c",
  "src/parser/config_runtime.h",
  "src/compilation_session_compat.h",
  "src/frontend/translation_unit_compat.c",
  "src/frontend/translation_unit_compat.h",
  "src/lowering/ir_builder_compat.c",
  "src/lowering/ir_builder_compat.h",
  "src/lowering/translation_unit_data_lowering_compat.c",
  "src/lowering/translation_unit_data_lowering_compat.h",
  "src/preprocess/preprocess_compat.c",
  "src/preprocess/preprocess_compat.h",
];
const remainingCompatibilityFiles = forbiddenCompatibilityFiles.filter(
  (path) => allSourceFiles.includes(path),
);
if (remainingCompatibilityFiles.length) {
  throw new Error(
    "context-free CompilationSession compatibility files must not return:\n" +
      remainingCompatibilityFiles.join("\n"),
  );
}

const irFiles = (await sourceFilesUnder("src/ir")).sort();

const archEntries = await readdir("src/arch", { withFileTypes: true });
const ungroupedArchFiles = archEntries
  .filter((entry) => entry.isFile() && /\.[ch]$/.test(entry.name))
  .map((entry) => `src/arch/${entry.name}`)
  .sort();
if (ungroupedArchFiles.length) {
  throw new Error(
    "architecture sources must live in target-specific directories:\n" +
      ungroupedArchFiles.join("\n"),
  );
}

const archFiles = (await sourceFilesUnder("src/arch")).sort();

const backendFiles = [
  ...archFiles,
  ...irFiles,
];

const semanticContextOwnershipSource = await readFile(
  "src/parser/semantic_ctx.c",
  "utf8",
);
const semanticContextOwnershipHeader = await readFile(
  "src/parser/semantic_ctx.h",
  "utf8",
);
if (!/struct\s+psx_semantic_context_t\s*\{/.test(semanticContextOwnershipSource) ||
    !/psx_semantic_context_t\s*\*ps_ctx_create\s*\([^)]*arena_context_t\s*\*arena_context[^)]*ag_diagnostic_context_t\s*\*diagnostic_context[^)]*psx_scope_graph_t\s*\*scope_graph[^)]*const\s+ag_target_info_t\s*\*target\s*\)/s.test(semanticContextOwnershipSource) ||
    !/ag_target_info_is_valid\s*\(target\)/.test(
      semanticContextOwnershipSource,
    ) ||
    !/const\s+ag_target_info_t\s*\*target\s*;/.test(
      semanticContextOwnershipSource,
    ) ||
    !/context->target\s*=\s*target\s*;/.test(
      semanticContextOwnershipSource,
    ) ||
    /context->target\s*=\s*\*target\s*;/.test(
      semanticContextOwnershipSource,
    ) ||
    !/context->diagnostic_context\s*=\s*diagnostic_context\s*;/.test(
      semanticContextOwnershipSource,
    ) ||
    !/context->scope_graph\s*=\s*scope_graph\s*;/.test(
      semanticContextOwnershipSource,
    ) ||
    /ps_ctx_bind_(?:diagnostic_context|resolution_store|scope_graph|target_info)\s*\(/.test(
      semanticContextOwnershipSource + semanticContextOwnershipHeader,
    ) ||
    /psx_scope_graph_create\s*\(|owns_scope_graph/.test(
      semanticContextOwnershipSource,
    ) ||
    /ag_target_info_host\s*\(/.test(semanticContextOwnershipSource) ||
    !/void\s+ps_ctx_destroy\s*\(/.test(semanticContextOwnershipSource) ||
    /default_semantic_context|active_semantic_context/.test(
      semanticContextOwnershipSource,
    ) ||
    /ps_ctx_(?:active|activate)\s*\(/.test(semanticContextOwnershipSource)) {
  throw new Error("semantic state must be owned by an explicit context lifecycle");
}
const legacySemanticGlobals =
  /^static\s+.*\b(?:goto_refs_all|label_defs_by_bucket|deferred_parser_diagnostics_all|tag_types_by_bucket|all_tag_types|tag_members_by_bucket|enum_consts_by_bucket|all_enum_consts|typedefs_by_bucket|all_typedefs|func_names_by_bucket|tag_scope_depth|tag_member_decl_order)\b/gm;
if (legacySemanticGlobals.test(semanticContextOwnershipSource)) {
  throw new Error("semantic registries must not return to file-scope global ownership");
}
const contextFreeSemanticRegistryApis =
  /\b(?:ps_ctx_reset_function_names|ps_ctx_reset_translation_unit_scope|ps_ctx_reset_function_diag_state|ps_ctx_reset_tag_diag_state|ps_ctx_reset_function_scope|ps_ctx_enter_block_scope|ps_ctx_leave_block_scope|ps_ctx_has_tag_type|ps_ctx_register_tag_type|ps_ctx_ensure_tag_record_decl|ps_ctx_get_tag_size|ps_ctx_get_tag_align|ps_ctx_register_tag_members|ps_ctx_find_enum_const|ps_ctx_find_typedef_name|ps_ctx_find_typedef_decl_type|ps_ctx_find_function_symbol|ps_ctx_get_function_type)\s*\(/;
if (contextFreeSemanticRegistryApis.test(semanticContextOwnershipSource)) {
  throw new Error("semantic registry operations must require an explicit context");
}
const splitSemanticLocalContextApis =
  /\bps_ctx_(?:register_tag_type|register_enum_const|register_typedef_name)_in_contexts\s*\(/;
if (splitSemanticLocalContextApis.test(
      semanticContextOwnershipSource + semanticContextOwnershipHeader,
    )) {
  throw new Error(
    "semantic namespace declarations must be owned by the semantic context scope graph",
  );
}
const compilationSessionHeader = await readFile(
  "src/compilation_session.h",
  "utf8",
);
const compilationSessionSource = await readFile(
  "src/compilation_session.c",
  "utf8",
);
const sourceManagerHeader = await readFile("src/source_manager.h", "utf8");
const sourceManagerSource = await readFile("src/source_manager.c", "utf8");
const diagnosticContextHeader = await readFile("src/diag/diag.h", "utf8");
const diagnosticContextSource = await readFile("src/diag/diag.c", "utf8");
if (!/ps_ctx_create\s*\(\s*session->arena_context\s*,\s*session->diagnostic_context\s*,\s*session->scope_graph\s*,\s*&session->target\s*\)/s.test(
      compilationSessionSource,
    )) {
  throw new Error(
    "CompilationSession must construct semantic state with its immutable target",
  );
}
const compilerMainSource = await readFile("src/main.c", "utf8");
const codegenEmitSource = await readFile("src/codegen_emit.c", "utf8");
const codegenEmitHeader = await readFile("src/codegen_emit.h", "utf8");
const arm64DataEmitSource = await readFile(
  "src/arch/arm64_apple/arm64_apple.c",
  "utf8",
);
const arm64IrEmitSource = await readFile(
  "src/arch/arm64_apple/arm64_apple_ir.c",
  "utf8",
);
const wasmBackendContextSource = await readFile(
  "src/arch/wasm32/backend_context.c",
  "utf8",
);
const wasmBackendContextHeader = await readFile(
  "src/arch/wasm32/backend_context.h",
  "utf8",
);
const wasmIrHeader = await readFile(
  "src/arch/wasm32/wasm32_ir.h",
  "utf8",
);
const wasmObjHeader = await readFile(
  "src/arch/wasm32/wasm32_obj.h",
  "utf8",
);
const wasmIrSource = await readFile("src/arch/wasm32/wasm32_ir.c", "utf8");
const wasmWatRuntimeContextSource = await readFile(
  "src/arch/wasm32/wasm32_wat_runtime.c",
  "utf8",
);
const wasmRuntimeWideSource = await readFile(
  "tools/wasm_obj_linker/runtime/parts/wide.c",
  "utf8",
);
const tokenizerHeader = await readFile("src/tokenizer/tokenizer.h", "utf8");
const tokenizerSource = await readFile("src/tokenizer/tokenizer.c", "utf8");
const tokenizerNumberSource = await readFile(
  "src/tokenizer/number.c",
  "utf8",
);
const tokenizerLiteralsSource = await readFile(
  "src/tokenizer/literals.c",
  "utf8",
);
const tokenizerCursorSource = await readFile(
  "src/tokenizer/cursor.c",
  "utf8",
);
const tokenizerDiagnosticHelperSource = await readFile(
  "src/tokenizer/diag_helper.h",
  "utf8",
);
const tokenizerConfigRuntimeSource = await readFile(
  "src/tokenizer/config_runtime.c",
  "utf8",
);
const tokenizerAllocatorSource = await readFile(
  "src/tokenizer/allocator.c",
  "utf8",
);
const tokenizerAllocatorHeader = await readFile(
  "src/tokenizer/allocator.h",
  "utf8",
);
const tokenizerFilenameSource = await readFile(
  "src/tokenizer/filename_table.c",
  "utf8",
);
const preprocessSource = await readFile("src/preprocess/preprocess.c", "utf8");
const preprocessHeader = await readFile("src/preprocess/preprocess.h", "utf8");
if (!/struct\s+ag_preprocessor_context_t\s*\{[^]*?char\s+project_root\s*\[\s*PATH_MAX\s*\]\s*;[^]*?char\s+include_root\s*\[\s*PATH_MAX\s*\]\s*;/.test(
      preprocessSource,
    ) ||
    !/include_path_is_allowed\s*\(\s*const\s+ag_preprocessor_context_t\s*\*\s*context\s*,/.test(
      preprocessSource,
    ) ||
    !/include_path_is_allowed\s*\(\s*context\s*,\s*resolved\s*\)/.test(
      preprocessSource,
    ) ||
    /\broots_initialized\b/.test(preprocessSource) ||
    !/\bpp_context_project_root\s*\(/.test(preprocessHeader) ||
    !/\bpp_context_include_root\s*\(/.test(preprocessHeader)) {
  throw new Error(
    "preprocessor include roots must be immutable per-context session state",
  );
}
const parserRuntimeSource = await readFile(
  "src/parser/runtime_context.c",
  "utf8",
);
const parserRuntimeHeader = await readFile(
  "src/parser/runtime_context.h",
  "utf8",
);
const compilationOptionsHeader = await readFile(
  "src/compilation_options.h",
  "utf8",
);
const configHeader = await readFile("src/config/config.h", "utf8");
const configSource = await readFile("src/config/config.c", "utf8");
const parserRuntimeConsumerSource = await readFile(
  "src/parser/parser.c",
  "utf8",
);
const parserArenaSource = await readFile("src/parser/arena.c", "utf8");
const parserExprSource = await readFile("src/parser/expr.c", "utf8");
const parserPragmaPackSource = await readFile(
  "src/parser/pragma_pack.c",
  "utf8",
);
const parserDeclSource = await readFile("src/parser/decl.c", "utf8");
const parserDeclHeader = await readFile("src/parser/decl.h", "utf8");
const loweringRuntimeHeader = await readFile(
  "src/lowering/runtime_context.h",
  "utf8",
);
const loweringRuntimeSource = await readFile(
  "src/lowering/runtime_context.c",
  "utf8",
);
const translationUnitDataLoweringSource = await readFile(
  "src/lowering/translation_unit_data_lowering.c",
  "utf8",
);
const translationUnitDataLoweringHeader = await readFile(
  "src/lowering/translation_unit_data_lowering.h",
  "utf8",
);
const irBuildOptionsHeader = await readFile(
  "src/lowering/ir_build_options.h",
  "utf8",
);
const irAllocationStatsHeader = await readFile(
  "src/ir/ir_allocation_stats.h",
  "utf8",
);
const irAllocationSource = await readFile("src/ir/ir_alloc.c", "utf8");
const irOptimizationSource = await readFile("src/ir/ir_opt.c", "utf8");
if (/\bps_node_atomic_pointer_info\s*\(/.test(
      `${parserLayerSource}\n${loweringLayerSource}`,
    )) {
  throw new Error(
    "atomic IR width must come from TypeId target layout instead of a parser node size helper",
  );
}
const compilationSessionInternalHeader = await readFile(
  "src/compilation_session_internal.h",
  "utf8",
);
const targetInfoSource = await readFile("src/target_info.c", "utf8");
const targetInfoHeader = await readFile("src/target_info.h", "utf8");
if (!/ag_target_info_is_valid\s*\(/.test(targetInfoHeader) ||
    !/typedef\s+struct\s+ag_data_layout_t\s*\{[^]*?pointer_size[^]*?pointer_alignment[^]*?atomic_promoted_max_size[^]*?atomic_max_alignment[^]*?scalar\s*\[\s*AG_TARGET_SCALAR_COUNT\s*\][^]*?\}\s*ag_data_layout_t\s*;/.test(
      targetInfoHeader,
    ) ||
    !/ag_data_layout_t\s+data_layout\s*;/.test(targetInfoHeader) ||
    !/ag_target_info_data_layout\s*\(/.test(targetInfoHeader) ||
    !/ag_data_layout_atomic_promoted_max_size\s*\(/.test(
      targetInfoHeader,
    ) ||
    !/ag_data_layout_atomic_max_alignment\s*\(/.test(targetInfoHeader) ||
    !/ag_target_info_is_valid\s*\(target\)/.test(
      compilationSessionSource,
    ) ||
    /target\s*\?\s*\*target\s*:\s*ag_target_info_host\s*\(\)/.test(
      compilationSessionSource,
    ) ||
    /\bag_target_info_host\s*\(\)/.test(compilationSessionSource) ||
    (targetInfoSource.match(/\bstandard_target\b/g) ?? []).length !== 3 ||
    /\bag_target_info_(?:pointer_size|pointer_alignment|scalar_size|scalar_alignment)\s*\(/.test(
      `${targetInfoHeader}\n${targetInfoSource}`,
    )) {
  throw new Error(
    "target layout queries must reject incomplete inputs instead of falling back to the host ABI",
  );
}
const sessionContextAccessorNames = [
  "semantic_context",
  "global_registry",
  "local_registry",
  "preprocessor_context",
  "arena_context",
  "diagnostic_context",
  "source_manager",
  "codegen_emit_context",
  "token_allocator_context",
  "parser_runtime_context",
  "lowering_context",
];
if (sessionContextAccessorNames.some((name) =>
      !new RegExp(`ag_compilation_session_${name}\\s*\\(`).test(
        compilationSessionHeader,
      ) ||
      !new RegExp(`ag_compilation_session_${name}\\s*\\(`).test(
        compilationSessionSource,
      )
    ) ||
    !/ps_global_registry_create\s*\(\s*ps_ctx_semantic_type_table_in\s*\(\s*session->semantic_context\s*\)\s*,\s*session->scope_graph\s*\)/.test(
      compilationSessionSource,
    ) ||
    !/ps_global_registry_destroy\s*\(/.test(compilationSessionSource) ||
    /ps_global_registry_activate\s*\(/.test(compilationSessionSource) ||
    /previous_global_registry/.test(compilationSessionSource) ||
    /ps_ctx_activate\s*\(/.test(compilationSessionSource) ||
    /previous_semantic_context/.test(compilationSessionSource) ||
    !/ps_local_registry_create\s*\(\s*session->diagnostic_context\s*,\s*ps_ctx_semantic_type_table_in\s*\(\s*session->semantic_context\s*\)\s*,\s*session->scope_graph\s*\)/.test(
      compilationSessionSource,
    ) ||
    !/ps_local_registry_destroy\s*\(/.test(compilationSessionSource) ||
    /ps_local_registry_activate\s*\(/.test(compilationSessionSource) ||
    /previous_local_registry/.test(compilationSessionSource) ||
    !/pp_context_create\s*\(/.test(compilationSessionSource) ||
    !/pp_context_destroy\s*\(/.test(compilationSessionSource) ||
    /pp_context_activate\s*\(/.test(compilationSessionSource) ||
    /previous_preprocessor_context/.test(compilationSessionSource) ||
    !/arena_context_create\s*\(/.test(compilationSessionSource) ||
    !/arena_context_destroy\s*\(/.test(compilationSessionSource) ||
    !/diag_context_create\s*\(/.test(compilationSessionSource) ||
    /diag_context_activate\s*\(/.test(compilationSessionSource) ||
    /previous_diagnostic_context/.test(compilationSessionInternalHeader) ||
    !/diag_context_destroy\s*\(/.test(compilationSessionSource) ||
    !/ag_source_manager_create\s*\(/.test(compilationSessionSource) ||
    !/ag_source_manager_destroy\s*\(/.test(compilationSessionSource) ||
    /tk_context_activate\s*\(/.test(compilationSessionSource) ||
    /previous_tokenizer_context/.test(compilationSessionSource) ||
    !/tk_context_dispose\s*\(&session->tokenizer\)/.test(
      compilationSessionSource,
    ) ||
    !/tk_allocator_context_create\s*\(/.test(compilationSessionSource) ||
    !/tk_allocator_context_destroy\s*\(/.test(compilationSessionSource) ||
    /tk_allocator_context_activate\s*\(/.test(compilationSessionSource) ||
    /previous_token_allocator_context/.test(compilationSessionSource) ||
    !/ps_parser_runtime_context_create\s*\(/.test(compilationSessionSource) ||
    !/ps_parser_runtime_context_destroy\s*\(/.test(compilationSessionSource) ||
    /ps_parser_runtime_context_activate\s*\(/.test(
      compilationSessionSource,
    ) ||
    /previous_parser_runtime_context/.test(compilationSessionSource) ||
    !/ps_lowering_context_create\s*\(/.test(compilationSessionSource) ||
    !/ps_lowering_context_destroy\s*\(/.test(compilationSessionSource) ||
    /ps_lowering_context_activate\s*\(/.test(compilationSessionSource) ||
    /previous_lowering_context/.test(compilationSessionSource) ||
    !/cg_context_create\s*\(/.test(compilationSessionSource) ||
    /cg_context_activate\s*\(/.test(compilationSessionSource) ||
    /previous_codegen_emit_context/.test(compilationSessionSource) ||
    !/cg_context_destroy\s*\(/.test(compilationSessionSource) ||
    !/ag_compilation_session_set_backend_context\s*\(/.test(
      compilationSessionSource,
    ) ||
    !/ag_compilation_session_backend_context\s*\(/.test(
      compilationSessionSource,
    ) ||
    /backend_(?:activate|deactivate)|previous_session|is_active/.test(
      `${compilationSessionSource}\n${compilationSessionInternalHeader}`,
    ) ||
    !/session->backend_destroy\s*\(session->backend_context\)/.test(
      compilationSessionSource,
    ) ||
    /wasm32_(?:ir|obj|backend)_context/.test(compilationSessionSource) ||
    !/ag_compilation_session_is_complete\s*\(/.test(compilationSessionSource) ||
    !/ag_compilation_session_reset_translation_unit\s*\(/.test(
      compilerMainSource,
    ) ||
    !/ag_compilation_session_create\s*\(/.test(compilerMainSource) ||
    /ag_compilation_session_(?:activate|deactivate|is_active)\s*\(/.test(
      compilerMainSource,
    ) ||
    !/ag_compilation_session_destroy\s*\(/.test(compilerMainSource) ||
    !/ag_compilation_session_tokenizer\s*\(/.test(compilerMainSource) ||
    /tk_get_default_context\s*\(/.test(compilerMainSource) ||
    /ag_target_set_pointer_size\s*\(/.test(compilerMainSource) ||
    !/pp_stream_open_in\s*\(/.test(compilerMainSource) ||
    /pp_stream_open_for_target\s*\(/.test(compilerMainSource) ||
    /diag_context_publish\s*\(/.test(compilerMainSource) ||
    /static\s+ag_compilation_session_t\s*\*wasm_adapter_session\s*;/.test(
      compilerMainSource,
    ) ||
    !/typedef\s+struct\s*\{[^]*?ag_compilation_session_t\s*\*session;[^]*?\}\s*agc_wasm_adapter_t\s*;/.test(
      compilerMainSource,
    ) ||
    !/wasm_adapter_retain_session\s*\(/.test(compilerMainSource) ||
    !/wasm_adapter_language_session\s*\(/.test(compilerMainSource) ||
    !/wasm_adapter_discard_session\s*\(/.test(compilerMainSource) ||
    !/agc_wasm_adapter_session_generation\s*\(/.test(
      compilerMainSource,
    ) ||
    /agc_wasm_adapter_analyze_source_virtual\s*\([^]*?ag_compilation_session_destroy\s*\(adapter->session\)[^]*?agc_wasm_adapter_analysis_error_code_ptr/.test(
      compilerMainSource,
    ) ||
    !/agc_wasm_adapter_create\s*\(/.test(compilerMainSource) ||
    !/agc_wasm_adapter_destroy\s*\(/.test(compilerMainSource) ||
    /\bagc_wasm_compile_(?:wat|object)/.test(compilerMainSource) ||
    !/ir_build_function_module_from_hir\s*\(/.test(
      compilerMainSource,
    ) ||
    !/psx_frontend_free_processed_ast_in_session\s*\(/.test(
      compilerMainSource,
    ) ||
    !/lower_ir_translation_unit_data_in_session\s*\(/.test(
      compilerMainSource,
    ) ||
    /\bpsx_frontend_free_processed_ast\s*\(\s*\)/.test(
      compilerMainSource,
    ) ||
    /\blower_ir_translation_unit_data\s*\(\s*\)/.test(
      compilerMainSource,
    )) {
  throw new Error(
    "compilation entry points must own registries, tokenizer, and target through CompilationSession",
  );
}

if (!/typedef\s+struct\s+ag_compilation_options_t\s*\{/.test(
      compilationOptionsHeader,
    ) ||
    !/ag_compilation_options_t\s+options\s*;/.test(
      compilationSessionInternalHeader,
    ) ||
    !/ag_compilation_session_options\s*\(/.test(compilationSessionHeader) ||
    !/ag_compilation_session_options_view\s*\(/.test(
      compilationSessionHeader,
    ) ||
    !/load_config_toml_in_session\s*\(/.test(configHeader) ||
    /\bload_config_toml\s*\(/.test(configHeader) ||
    !/ag_compilation_session_options\s*\(session\)/.test(configSource) ||
    /parser\/config_runtime\.h/.test(
      `${configSource}\n${loweringLayerSource}`,
    ) ||
    /enable_(?:size_compatible_nonscalar_cast|struct_scalar_pointer_cast|union_scalar_pointer_cast|union_array_member_nonbrace_init)/.test(
      parserRuntimeHeader,
    )) {
  throw new Error(
    "compilation compatibility options must be owned by CompilationSession and passed explicitly to lowering",
  );
}

if (!/typedef\s+struct\s+ag_compilation_session_t\s+ag_compilation_session_t\s*;/.test(
      compilationSessionHeader,
    ) ||
    /struct\s+ag_compilation_session_t\s*\{/.test(
      compilationSessionHeader,
    ) ||
    !/struct\s+ag_compilation_session_t\s*\{/.test(
      compilationSessionInternalHeader,
    ) ||
    !/#include\s+"compilation_session_internal\.h"/.test(
      compilationSessionSource,
    ) ||
    !/ag_compilation_session_t\s*\*ag_compilation_session_create\s*\(/.test(
      compilationSessionSource,
    ) ||
    !/int\s+ag_compilation_session_destroy\s*\(/.test(
      compilationSessionSource,
    ) ||
    /ag_compilation_session_(?:init|dispose)\s*\(/.test(
      compilationSessionHeader,
    ) ||
    !/ag_compilation_session_init\s*\(/.test(
      compilationSessionInternalHeader,
    ) ||
    !/ag_compilation_session_dispose\s*\(/.test(
      compilationSessionInternalHeader,
    ) ||
    /ag_compilation_session_(?:active_compat|effective_target_compat)\s*\(/.test(
      `${compilationSessionHeader}\n${compilationSessionSource}`,
    ) ||
    /ag_compilation_session_(?:activate|deactivate|is_active)\s*\(/.test(
      `${compilationSessionHeader}\n${compilationSessionSource}`,
    ) ||
    /ag_compiler_context_/.test(compilationSessionSource) ||
    /#include\s+"tokenizer\/tokenizer\.h"/.test(
      compilationSessionHeader,
    ) ||
    /ag_compilation_session_t\s+\w+\s*;/.test(compilerMainSource)) {
  throw new Error(
    "CompilationSession representation must remain private behind create/destroy lifecycle APIs",
  );
}

if (!/typedef\s+struct\s*\{[\s\S]*?const\s+ag_target_info_t\s*\*target\s*;[\s\S]*?const\s+psx_record_decl_table_t\s*\*record_decls\s*;[\s\S]*?const\s+ag_continuation_options_t\s*\*continuation\s*;[\s\S]*?ag_diagnostic_context_t\s*\*diagnostic_context\s*;[\s\S]*?\}\s*ir_build_options_t\s*;/.test(
      irBuildOptionsHeader,
    ) ||
    !/\.target\s*=\s*ag_compilation_session_target\s*\(/.test(
      compilerMainSource,
    ) ||
    !/\.continuation\s*=\s*ag_compilation_session_continuation\s*\(/.test(
      compilerMainSource,
    ) ||
    !/\.diagnostic_context\s*=\s*\n?\s*ag_compilation_session_diagnostic_context\s*\(/.test(
      compilerMainSource,
    ) ||
    /ir_build_(?:function_module|emit_function)_for_target\s*\(/.test(
      compilerMainSource,
    )) {
  throw new Error(
    "explicit IR build paths must receive target and continuation options without reading the active CompilationSession",
  );
}

if (!/int\s+ag_compilation_session_dispose\s*\(/.test(
      compilationSessionInternalHeader,
    ) ||
    !/ag_compilation_session_backend_context\s*\(/.test(
      compilationSessionHeader,
    ) ||
    /active_compilation_session|previous_session|owns_session_activation/.test(
      `${compilationSessionSource}\n${compilationSessionInternalHeader}\n${compilationSessionHeader}`,
    )) {
  throw new Error(
    "CompilationSession lifecycle must use explicit ownership without an active-session stack",
  );
}

if (!/ir_allocation_stats_t\s+ir_allocation_stats\s*;/.test(
      compilationSessionInternalHeader,
    ) ||
    !/ag_compilation_session_ir_allocation_stats\s*\(/.test(
      compilationSessionHeader,
    ) ||
    !/ir_allocation_stats_t\s*\*allocation_stats\s*;/.test(
      irBuildOptionsHeader,
    ) ||
    !/ir_module_new_with_allocation_stats\s*\(\s*options->allocation_stats\s*\)/.test(
      hirIrBuilder,
    ) ||
    !/\.allocation_stats\s*=\s*\n?\s*ag_compilation_session_ir_allocation_stats\s*\(session\)/.test(
      compilerMainSource,
    ) ||
    !/typedef\s+struct\s+ir_allocation_stats_t\s*\{/.test(
      irAllocationStatsHeader,
    ) ||
    /^static\s+size_t\s+ir_(?:inst|block)_(?:live|peak)\b/m.test(
      irAllocationSource,
    ) ||
    /\bir_(?:inst|block)_total_count\s*\(/.test(
      `${irAllocationSource}\n${compilerMainSource}`,
    )) {
  throw new Error(
    "IR allocation accounting must be owned by CompilationSession and passed explicitly to IR modules",
  );
}

if (/ag_compilation_session_(?:active_compat|effective_target_compat)\s*\(/.test(
      `${compilationSessionSource}\n${preprocessSource}\n${hirIrBuilder}`,
    ) ||
    /\bag_target_pointer_size\s*\(/.test(preprocessSource)) {
  throw new Error(
    "preprocess and IR core APIs must receive their target explicitly",
  );
}

const frontendTranslationUnitSessionSource = await readFile(
  "src/frontend/translation_unit.c",
  "utf8",
);

if (!/arena_alloc_in\s*\(/.test(parserArenaSource) ||
    !/arena_register_cleanup_in\s*\(/.test(parserArenaSource) ||
    !/arena_run_cleanups_until\s*\(/.test(parserArenaSource) ||
    !/arena_checkpoint_in\s*\(/.test(parserArenaSource) ||
    !/arena_rollback_in\s*\(/.test(parserArenaSource) ||
    !/arena_free_all_in\s*\(/.test(parserArenaSource) ||
    /\barena_(?:checkpoint|rollback|free_all)\s*\(/.test(
      frontendTranslationUnitSessionSource,
    )) {
  throw new Error(
    "frontend arena lifecycle must operate on the CompilationSession arena",
  );
}

if (!/lower_ir_translation_unit_data_in_session\s*\(/.test(
      translationUnitDataLoweringSource,
    ) ||
    !/ps_iter_string_literals_in\s*\(/.test(
      translationUnitDataLoweringSource,
    ) ||
    !/ps_iter_float_literals_in\s*\(/.test(
      translationUnitDataLoweringSource,
    ) ||
    !/ps_iter_globals_in\s*\(/.test(translationUnitDataLoweringSource) ||
    !/ps_gvar_symbol_ref_named_function_in\s*\(/.test(
      translationUnitDataLoweringSource,
    ) ||
    !/ps_gvar_walk_resolved_aggregate_initializer\s*\(/.test(
      translationUnitDataLoweringSource,
    ) ||
    /\bps_gvar_symbol_ref_named_function\s*\(/.test(
      translationUnitDataLoweringSource,
    ) ||
    /\bps_gvar_walk_aggregate_initializer_in\s*\(/.test(
      translationUnitDataLoweringSource,
    )) {
  throw new Error(
    "translation-unit data lowering must enumerate the requested CompilationSession registry",
  );
}

if (/active_session_view\s*\(/.test(frontendTranslationUnitSessionSource) ||
    /ag_compilation_session_active_compat\s*\(\)/.test(
      frontendTranslationUnitSessionSource,
    )) {
  throw new Error(
    "frontend core APIs must receive CompilationSession explicitly",
  );
}

if (/^static\s+(?:gen_output_line_fn|void\s*\*|char\s+|int\s+)\b(?:gen_output|cg_format_stack_buf|gen_simple_formatter)/m.test(
      codegenEmitSource,
    ) ||
    !/struct\s+ag_codegen_emit_context_t\s*\{/.test(codegenEmitSource) ||
    !/ag_diagnostic_context_t\s*\*diagnostic_context\s*;/.test(
      codegenEmitSource,
    ) ||
    !/cg_context_create\s*\(\s*session->diagnostic_context\s*\)/.test(
      compilationSessionSource,
    ) ||
    /\bdiag_(?:emit_internalf|message_for)\s*\(/.test(codegenEmitSource)) {
  throw new Error(
    "backend output, formatting, and diagnostics must be context-owned",
  );
}

if (!/ag_compilation_session_codegen_emit_context\s*\(/.test(
      compilerMainSource,
    ) ||
    !/gen_set_output_callback_in\s*\(/.test(compilerMainSource) ||
    !/gen_set_simple_formatter_in\s*\(/.test(compilerMainSource) ||
    /\bgen_set_output_callback\s*\(/.test(compilerMainSource) ||
    /\bgen_set_simple_formatter\s*\(/.test(compilerMainSource)) {
  throw new Error(
    "compiler entry output routing must use its CompilationSession emit context",
  );
}

if (!/wasm32_backend_context_create\s*\(\s*ag_codegen_emit_context_t\s*\*emit_context\s*\)/.test(
      wasmBackendContextSource,
    ) ||
    !/wasm32_ir_context_create\s*\(emit_context\)/.test(
      wasmBackendContextSource,
    ) ||
    !/ag_codegen_emit_context_t\s*\*emit_context\s*;/.test(wasmIrSource) ||
    !/cg_emitf_in\s*\(wasm32_ir_emit_context\s*\(context\)/.test(
      wasmIrSource,
    ) ||
    /\b_Thread_local\b/.test(
      wasmIrSource + wasmWatRuntimeContextSource,
    ) ||
    /wasm32_ir_context_(?:activate|active)\s*\(/.test(
      wasmIrSource + wasmWatRuntimeContextSource + wasmIrHeader,
    ) ||
    /\bcg_context_active\s*\(/.test(wasmIrSource) ||
    /\bcg_emitf\s*\(/.test(wasmIrSource)) {
  throw new Error(
    "Wasm text backend must emit through its injected CompilationSession context",
  );
}

if (/\(i64\.load\s+\(local\.get\s+\$srcp\)\)/.test(wasmIrSource) ||
    /\(i64\.store\s+\(local\.get\s+\$srcp\)/.test(wasmIrSource) ||
    /intern_data_symbol\("__ag_(?:mbstowcs|wcstombs)_srcp"[^;]*,\s*8\s*,\s*8\s*\)/.test(
      wasmIrSource,
    ) ||
    /long\s*\*\s*srcp\s*=/.test(wasmRuntimeWideSource) ||
    !/char\s*\*\s*\*srcp\s*=\s*\(char\s*\*\s*\*\)ag_rt_ptr\(srcp_addr\)/.test(
      wasmRuntimeWideSource,
    ) ||
    !/int\s*\*\s*\*srcp\s*=\s*\(int\s*\*\s*\*\)ag_rt_ptr\(srcp_addr\)/.test(
      wasmRuntimeWideSource,
    )) {
  throw new Error(
    "Wasm multibyte conversion pointer-to-pointer storage must use the target pointer width",
  );
}

const explicitCodegenEmitSources = [
  compilationSessionSource,
  codegenEmitSource,
  codegenEmitHeader,
  arm64DataEmitSource,
  arm64IrEmitSource,
  wasmIrSource,
];
if (explicitCodegenEmitSources.some((source) =>
      /\bcg_context_(?:active|activate)\s*\(/.test(source) ||
      /\bcg_emitf\s*\(/.test(source) ||
      /\bdiag_(?:emit_internalf|message_for)\s*\(/.test(source)
    ) ||
    !/gen_ir_module_in\s*\(/.test(arm64IrEmitSource) ||
    (!/ir_build_emit_function_with_options_in\s*\(/.test(
      compilerMainSource,
    ) &&
      (!/ir_build_function_module_from_hir\s*\(/.test(
        compilerMainSource,
      ) ||
        !/gen_ir_module_in\s*\(/.test(compilerMainSource))) ||
    !/gen_(?:string_literals|float_literals|global_vars)_in\s*\(/.test(
      compilerMainSource,
    )) {
  throw new Error(
    "CompilationSession codegen must use explicit emit contexts end to end",
  );
}

if (/\b(?:user_input|current_filename|filename_table|filename_table_count)\b/.test(
      tokenizerHeader,
    ) ||
    /\btk_filename_(?:intern|lookup|reset_translation_unit)\s*\(/.test(
      `${tokenizerFilenameSource}\n${tokenizerSource}\n${preprocessSource}`,
    ) ||
    !/ag_source_manager_intern_name\s*\(/.test(tokenizerFilenameSource) ||
    !/ag_source_manager_name\s*\(/.test(tokenizerFilenameSource) ||
    !/ag_source_manager_reset_translation_unit\s*\(/.test(
      tokenizerFilenameSource,
    ) ||
    !/uint16_t\s+ag_source_manager_intern_name\s*\(/.test(
      sourceManagerHeader + sourceManagerSource,
    ) ||
    /char\s*\*\s*names\s*\[[^\]]+\]/.test(sourceManagerSource) ||
    !/realloc\s*\(\s*manager->names/.test(sourceManagerSource) ||
    !/if\s*\(\s*!manager\s*\|\|\s*!name\s*\|\|\s*!name\[0\]\s*\)\s*return\s+0/.test(
      sourceManagerSource,
    ) ||
    !/tk_filename_intern_ctx\s*\(\s*ctx\s*,/.test(tokenizerSource) ||
    !/tk_filename_reset_translation_unit_ctx\s*\(/.test(
      tokenizerFilenameSource,
    )) {
  throw new Error(
    "token source identity must be owned and reset by SourceManager",
  );
}

const tokenizerProductionContextSources = [
  tokenizerHeader,
  tokenizerSource,
  tokenizerConfigRuntimeSource,
  tokenizerAllocatorSource,
  tokenizerAllocatorHeader,
].join("\n");
if (!/pp_context_create\s*\(\s*ag_diagnostic_context_t\s*\*diagnostic_context\s*,\s*tokenizer_context_t\s*\*tokenizer_context\s*,\s*const\s+ag_target_info_t\s*\*target\s*\)/s.test(
      preprocessHeader + preprocessSource,
    ) ||
    !/context->tokenizer\s*=\s*tokenizer_context\s*;/.test(
      preprocessSource,
    ) ||
    !/context->target\s*=\s*target\s*;/.test(preprocessSource) ||
    /(?:preprocess_tokenizer|preprocess_target)\s*=/.test(
      preprocessSource,
    ) ||
    /struct\s+pp_stream\s*\{[^}]*\b(?:tk_ctx|ag_target_info_t\s+target)\b/s.test(
      preprocessSource,
    ) ||
    !/static\s+token_t\s*\*preprocess_tokens\s*\(\s*ag_preprocessor_context_t\s*\*context\s*,\s*token_t\s*\*tok\s*\)/s.test(
      preprocessSource,
    ) ||
    /preprocess_tokens\s*\(/.test(preprocessHeader) ||
    /tk_filename_reset_translation_unit_ctx\s*\(/.test(preprocessSource) ||
    !/pp_stream_open_in\s*\(\s*ag_preprocessor_context_t\s*\*context\s*,\s*pp_stream_t\s*\*\*out_s\s*,\s*const\s+char\s*\*src\s*\)/s.test(
      preprocessHeader + preprocessSource,
    ) ||
    !/context->active_stream/.test(
      preprocessSource.match(
        /token_t\s*\*pp_stream_open_in\s*\([^]*?\n\}/,
      )?.[0] ?? "",
    )) {
  throw new Error(
    "preprocessor tokenizer and target dependencies must be immutable context state",
  );
}
if (/owns_(?:allocator|diagnostic)_context/.test(tokenizerHeader) ||
    /tk_context_(?:bind_diagnostic_context|set_allocator)\s*\(/.test(
      tokenizerHeader + tokenizerConfigRuntimeSource,
    ) ||
    /tk_allocator_bind_diagnostic_context_in\s*\(/.test(
      tokenizerAllocatorHeader + tokenizerAllocatorSource,
    ) ||
    /(?:diag_context_create|tk_allocator_context_create)\s*\(/.test(
      tokenizerConfigRuntimeSource,
    ) ||
    !/int\s+tk_context_init\s*\(\s*tokenizer_context_t\s*\*ctx\s*,\s*ag_diagnostic_context_t\s*\*diagnostic_context\s*,\s*tk_allocator_context_t\s*\*allocator_context\s*,\s*ag_source_manager_t\s*\*source_manager\s*\)/s.test(
      tokenizerHeader + tokenizerConfigRuntimeSource,
    ) ||
    !/tk_allocator_diagnostics\s*\(\s*allocator_context\s*\)\s*!=\s*diagnostic_context/.test(
      tokenizerConfigRuntimeSource,
    ) ||
    !/tk_context_init\s*\(\s*&session->tokenizer\s*,\s*session->diagnostic_context\s*,\s*session->token_allocator_context\s*,\s*session->source_manager\s*\)/s.test(
      compilationSessionSource,
    ) ||
    /diag_context_bind_tokenizer\s*\(/.test(
      diagnosticContextSource + diagnosticContextHeader +
        compilationSessionSource,
    ) ||
    /tokenizer_context_t\s*\*tokenizer_context\s*;/.test(
      diagnosticContextSource,
    ) ||
    !/diag_context_create\s*\(\s*ag_source_manager_t\s*\*source_manager\s*\)/s.test(
      diagnosticContextSource + diagnosticContextHeader,
    ) ||
    !/context->source_manager\s*=\s*source_manager\s*;/.test(
      diagnosticContextSource,
    ) ||
    !/diag_context_source_manager\s*\(\s*diagnostic_context\s*\)\s*!=\s*source_manager/.test(
      tokenizerConfigRuntimeSource,
    )) {
  throw new Error(
    "tokenizer dependencies must be session-owned, immutable, and supplied at initialization",
  );
}
if (/\btk_(?:get_default_context|context_active|context_activate|runtime_ctx|allocator_default_context)\s*\(/.test(
      tokenizerProductionContextSources,
    ) ||
    /\b(?:active_ctx|default_ctx|default_allocator_context|default_diagnostic_context)\b/.test(
      tokenizerProductionContextSources,
    ) ||
    !/s\.ctx\s*=\s*ctx\s*;/.test(tokenizerSource) ||
    !/tokenizer_context_t\s*\*tk_effective_ctx\s*\([^)]*\)\s*\{\s*return\s+ctx\s*;\s*\}/s.test(
      tokenizerSource,
    )) {
  throw new Error(
    "production tokenization must require an explicit tokenizer context",
  );
}

if (/tk_tokenize_ctx_active/.test(tokenizerSource) ||
    !/tk_parse_number_literal_ctx\s*\(\s*ctx\s*,/.test(
      tokenizerSource,
    ) ||
    !/tk_skip_escape_in_literal_ctx\s*\(\s*ctx\s*,/.test(
      tokenizerSource,
    ) ||
    !/tk_skip_ignored_ctx\s*\(\s*ctx\s*,/.test(tokenizerSource) ||
    !/tk_tolerate_longjmp_if_active_ctx\s*\(\s*tk_diag_ctx__\s*\)/.test(
      tokenizerDiagnosticHelperSource,
    )) {
  throw new Error(
    "explicit tokenizer streams must carry their context through scanning and diagnostics",
  );
}

if (!/wasm32_ir_context_create\s*\(/.test(wasmBackendContextSource) ||
    !/wasm32_obj_context_create\s*\(/.test(wasmBackendContextSource) ||
    !/wasm32_ir_context_destroy\s*\(/.test(wasmBackendContextSource) ||
    !/wasm32_obj_context_destroy\s*\(/.test(wasmBackendContextSource) ||
    !/wasm32_gen_machine_module_in\s*\(ctx->ir,/.test(
      wasmBackendContextSource,
    ) ||
    !/wasm32_obj_gen_machine_module_in\s*\(ctx->obj,/.test(
      wasmBackendContextSource,
    ) ||
    /wasm32_(?:ir|obj)_context_(?:activate|active)\s*\(/.test(
      wasmBackendContextSource + wasmBackendContextHeader +
        wasmIrHeader + wasmObjHeader,
    ) ||
    /wasm32_backend_context_(?:activate|deactivate)\s*\(/.test(
      wasmBackendContextSource + wasmBackendContextHeader + compilerMainSource,
    ) ||
    !/ag_compilation_session_backend_context\s*\(session\)/.test(
      compilerMainSource,
    ) ||
    /\bwasm32_(?:module_begin|module_end|gen_ir_module|emit_data_segments|obj_(?:set_output_file|capture_output|set_capture_limit|capture_limit_exceeded|take_output|begin|gen_ir_module|emit_data_segments|end))\s*\(/.test(
      compilerMainSource,
    ) ||
    !/attach_wasm_backend_context\s*\(/.test(compilerMainSource)) {
  throw new Error(
    "Wasm entry points must use an explicit session-owned backend context without public active/default APIs",
  );
}
const loweringStateSources = await Promise.all([
  readFile("src/lowering/local_storage.c", "utf8"),
  readFile("src/lowering/static_local_lowering.c", "utf8"),
  readFile("src/semantic/syntax_typed_hir_resolution.c", "utf8"),
  readFile("src/lowering/cast_lowering.c", "utf8"),
]);
if (!/typedef\s+struct\s+psx_lowering_context_t\s*\{/.test(
      loweringRuntimeHeader,
    ) ||
    !/ag_diagnostic_context_t\s*\*diagnostic_context\s*;/.test(
      loweringRuntimeHeader,
    ) ||
    !/ps_lowering_diagnostics\s*\(/.test(loweringRuntimeHeader) ||
    !/typedef\s+struct\s*\{[^]*?arena_context_t\s*\*arena_context\s*;[^]*?ag_diagnostic_context_t\s*\*diagnostic_context\s*;[^]*?const\s+ag_target_info_t\s*\*target\s*;[^]*?const\s+psx_semantic_type_table_t\s*\*semantic_types\s*;[^]*?const\s+psx_record_decl_table_t\s*\*record_decls\s*;[^]*?const\s+psx_record_layout_table_t\s*\*record_layouts\s*;[^]*?\}\s*psx_lowering_context_dependencies_t\s*;/.test(
      loweringRuntimeHeader,
    ) ||
    !/ps_lowering_context_create\s*\(\s*&lowering_dependencies\s*\)/.test(
      compilationSessionSource,
    ) ||
    !/psx_lowering_context_t\s*\*ps_lowering_context_create\s*\(\s*const\s+psx_lowering_context_dependencies_t\s*\*dependencies\s*\)/s.test(
      loweringRuntimeSource,
    ) ||
    !/ag_target_info_is_valid\s*\(dependencies->target\)/.test(loweringRuntimeSource) ||
    !/const\s+ag_target_info_t\s*\*target\s*;/.test(
      loweringRuntimeHeader,
    ) ||
    !/ctx->target\s*=\s*dependencies->target\s*;/.test(loweringRuntimeSource) ||
    /ctx->target\s*=\s*\*dependencies->target\s*;/.test(loweringRuntimeSource) ||
    /ps_lowering_context_bind_(?:target|semantic_types|record_decls|record_layouts)\s*\(/.test(
      loweringRuntimeHeader + loweringRuntimeSource,
    ) ||
    !/ctx->semantic_types\s*=\s*dependencies->semantic_types\s*;/.test(
      loweringRuntimeSource,
    ) ||
    !/ctx->record_decls\s*=\s*dependencies->record_decls\s*;/.test(
      loweringRuntimeSource,
    ) ||
    !/ctx->record_layouts\s*=\s*dependencies->record_layouts\s*;/.test(
      loweringRuntimeSource,
    ) ||
    !/ps_lowering_semantic_types\s*\(\s*session->lowering_context\s*\)\s*==\s*ps_ctx_semantic_type_table_in\s*\(\s*session->semantic_context\s*\)/.test(
      compilationSessionSource,
    ) ||
    !/ps_lowering_record_decls\s*\(\s*session->lowering_context\s*\)\s*==\s*ps_ctx_record_decl_table_in\s*\(\s*session->semantic_context\s*\)/.test(
      compilationSessionSource,
    ) ||
    !/ps_lowering_record_layouts\s*\(\s*session->lowering_context\s*\)\s*==\s*ps_ctx_record_layout_table_in\s*\(\s*session->semantic_context\s*\)/.test(
      compilationSessionSource,
    ) ||
    /ag_target_info_host\s*\(/.test(loweringRuntimeSource) ||
    /default_lowering_context|active_lowering_context/.test(
      loweringRuntimeSource,
    ) ||
    /ps_lowering_context_(?:active|activate)\s*\(/.test(
      loweringRuntimeHeader,
    ) ||
    loweringStateSources.some((source) =>
      /ps_lowering_context_(?:active|activate)\s*\(/.test(source)
    ) ||
    loweringStateSources.some((source) =>
      /static\s+(?:frame_layout_t\s+current_layout|int\s+(?:object_sequences|file_scope_compound_sequence|local_compound_sequence|aggregate_temp_seq|compound_assignment_temp_seq|member_rvalue_sequence))/.test(
        source,
      )
    ) ||
    !/ps_lowering_context_reset_translation_unit\s*\(\s*session->lowering_context\s*\)/.test(
      compilationSessionSource,
    )) {
  throw new Error(
    "lowering temporary naming and frame state must use an explicit lowering context",
  );
}
if (!/struct\s+psx_parser_runtime_context_t\s*\{/.test(
      await readFile("src/parser/runtime_context.h", "utf8"),
    ) ||
    /default_parser_runtime_context|active_parser_runtime_context/.test(
      parserRuntimeSource,
    ) ||
    /ps_parser_runtime_context_(?:active|activate)\s*\(/.test(
      await readFile("src/parser/runtime_context.h", "utf8"),
    ) ||
    /static\s+int\s+g_(?:recoverable_syntax_error|function_block_depth|recovery_block_depth)/.test(
      parserRuntimeConsumerSource,
    ) ||
    /static\s+int\s+(?:string_label_count|float_label_count)/.test(
      parserExprSource,
    ) ||
    /static\s+int\s+(?:pragma_pack_current|pack_stack_depth)/.test(
      parserPragmaPackSource,
    ) ||
    /static\s+(?:char\s*\*|int\s+)current_funcname/.test(
      parserDeclSource,
    ) ||
    !/ps_parser_runtime_context_reset_translation_unit\s*\(\s*session->parser_runtime_context\s*\)/.test(
      compilationSessionSource,
    )) {
  throw new Error(
    "parser translation-unit state must be owned by parser runtime context",
  );
}
if (!/struct\s+tk_allocator_context_t\s*\{/.test(
      tokenizerAllocatorSource,
    ) ||
    /static\s+arena_chunk_t\s*\*arena_head\s*;/.test(
      tokenizerAllocatorSource,
    ) ||
    /static\s+arena_chunk_t\s*\*recyc_oldest\s*;/.test(
      tokenizerAllocatorSource,
    ) ||
    /static\s+size_t\s+total_reserved_bytes\s*;/.test(
      tokenizerAllocatorSource,
    ) ||
    /active_allocator_context|tk_allocator_context_(?:active|activate)\s*\(/.test(
      tokenizerAllocatorSource + tokenizerAllocatorHeader,
    ) ||
    /default_allocator_context|tk_allocator_default_context\s*\(/.test(
      tokenizerAllocatorSource + tokenizerAllocatorHeader,
    ) ||
    !/tk_allocator_calloc_in\s*\(\s*tk_allocator_context_t\s*\*ctx/.test(
      tokenizerAllocatorSource,
    ) ||
    !/tk_allocator_context_t\s*\*allocator_context\s*;/.test(
      tokenizerHeader,
    )) {
  throw new Error(
    "token allocator storage and operations must require an explicit context",
  );
}
if (!/tk_cursor_hook_t\s+cursor_hook\s*;/.test(
      tokenizerHeader,
    ) ||
    !/void\s*\*cursor_hook_user_data\s*;/.test(tokenizerHeader) ||
    !/tk_ensure_lookahead_hook_t\s+ensure_lookahead_hook\s*;/.test(
      tokenizerHeader,
    ) ||
    !/void\s*\*ensure_lookahead_hook_user_data\s*;/.test(
      tokenizerHeader,
    ) ||
    !/bool\s+tolerate_untokenizable\s*;/.test(tokenizerHeader) ||
    !/void\s*\*tolerate_jump_target\s*;/.test(tokenizerHeader) ||
    /static\s+void\s*\(\*tk_cursor_hook\)/.test(tokenizerSource) ||
    /static\s+void\s*\(\*g_ensure_lookahead_hook\)/.test(tokenizerSource) ||
    /static\s+bool\s+g_tolerate_untokenizable/.test(tokenizerSource) ||
    !/tk_set_cursor_hook_ctx\s*\(s->context->tokenizer/.test(
      preprocessSource,
    ) ||
    !/tk_set_ensure_lookahead_hook_ctx\s*\(s->context->tokenizer/.test(
      preprocessSource,
    ) ||
    !/tk_set_tolerate_untokenizable_ctx\s*\(s->context->tokenizer/.test(
      preprocessSource,
    )) {
  throw new Error(
    "tokenizer callbacks and tolerant scanning state must be owned by tokenizer context",
  );
}
if (/default_preprocessor_context|active_preprocessor_context/.test(
      preprocessSource,
    ) ||
    /pp_context_(?:active|activate)\s*\(/.test(preprocessSource) ||
    !/pp_stream_open_in\s*\(ag_preprocessor_context_t\s*\*context/.test(
      preprocessSource,
    ) ||
    !/preprocess_tokens\s*\(\s*ag_preprocessor_context_t\s*\*context/.test(
      preprocessSource,
    ) ||
    /tk_ctx\s*\?\s*tk_ctx\s*:\s*tk_get_default_context\s*\(/.test(
      preprocessSource,
    ) ||
    /target\s*\?\s*\*target\s*:\s*ag_target_info_host\s*\(/.test(
      preprocessSource,
    )) {
  throw new Error(
    "preprocessor state and stream callbacks must require an explicit context",
  );
}
const contextFreePreprocessorDiagnosticApi =
  /\bdiag_(?:emit_atf|emit_tokf|report_atf|report_tokf|warn_tokf|emit_internalf|report_internalf|message_for|warn_message_for|text_for|has_error_records|limit_kind)\s*\(/;
if (!/ag_diagnostic_context_t\s*\*diagnostic_context\s*;/.test(
      preprocessSource,
    ) ||
    !/pp_context_create\s*\(\s*ag_diagnostic_context_t\s*\*diagnostic_context/.test(
      preprocessSource,
    ) ||
    !/pp_context_create\s*\(\s*session->diagnostic_context\s*,\s*&session->tokenizer\s*,\s*&session->target\s*\)/s.test(
      compilationSessionSource,
    ) ||
    contextFreePreprocessorDiagnosticApi.test(preprocessSource)) {
  throw new Error(
    "preprocessor diagnostics must use the CompilationSession-owned diagnostic context",
  );
}
const globalRegistrySource = await readFile(
  "src/parser/global_registry.c",
  "utf8",
);
const globalRegistryHeader = await readFile(
  "src/parser/global_registry.h",
  "utf8",
);
if (!/struct\s+psx_global_registry_t\s*\{/.test(globalRegistrySource) ||
    !/psx_global_registry_t\s*\*ps_global_registry_create\s*\(/.test(
      globalRegistrySource,
    ) ||
    !/registry->semantic_types\s*=\s*semantic_types\s*;/.test(
      globalRegistrySource,
    ) ||
    !/registry->scope_graph\s*=\s*scope_graph\s*;/.test(
      globalRegistrySource,
    ) ||
    !/ps_global_registry_semantic_types\s*\(/.test(
      globalRegistryHeader + globalRegistrySource,
    ) ||
    /ps_global_registry_bind_(?:semantic_types|scope_graph)\s*\(/.test(
      globalRegistryHeader + globalRegistrySource,
    ) ||
    !/void\s+ps_global_registry_destroy\s*\(/.test(
      globalRegistrySource,
    ) ||
    /default_global_registry|active_global_registry/.test(
      globalRegistrySource,
    ) ||
    /ps_global_registry_(?:active|activate)\s*\(/.test(
      globalRegistrySource,
    ) ||
    /\b(?:ps_register_global_var|ps_find_global_var|ps_iter_globals|ps_iter_string_literals|ps_iter_float_literals|ps_has_string_literals|ps_has_float_literals|ps_global_registry_reset_translation_unit|ps_global_registry_reset_diag_state)\s*\([^_]/.test(
      globalRegistrySource,
    ) ||
    /^static\s+(?:string_lit_t|float_lit_t|global_var_t)\s*\*(?:string_literals|float_literals|global_vars)\s*;/m.test(
      globalRegistrySource,
    ) ||
    /^static\s+global_var_t\s*\*gvars_by_bucket\s*\[/m.test(
      globalRegistrySource,
    )) {
  throw new Error(
    "global symbols and literals must be owned by an explicit registry",
  );
}
const localRegistrySource = await readFile(
  "src/parser/local_registry.c",
  "utf8",
);
const localRegistryHeader = await readFile(
  "src/parser/local_registry.h",
  "utf8",
);
const legacyLocalRegistryGlobals =
  /^static\s+.*\b(?:locals|all_locals|all_bindings|lvar_scope_stack|lvar_scope_seq_stack|lvar_scope_depth|next_scope_seq|current_scope_seq|next_declaration_seq|scope_parent_by_seq|scope_parent_capacity|lvars_by_bucket|lvars_by_offset|usage_events_head|usage_events_tail|current_usage_region)\b/gm;
const legacyActiveLocalRegistryMacros =
  /^#define\s+(?:locals|all_locals|all_bindings|lvar_scope_stack|lvar_scope_seq_stack|lvar_scope_depth|next_scope_seq|current_scope_seq|next_declaration_seq|scope_parent_by_seq|scope_parent_capacity|lvars_by_bucket|lvars_by_offset|usage_events_head|usage_events_tail|current_usage_region)\b/gm;
if (!/struct\s+psx_local_registry_t\s*\{/.test(localRegistrySource) ||
    !/ag_diagnostic_context_t\s*\*diagnostic_context\s*;/.test(
      localRegistrySource,
    ) ||
    !/psx_local_registry_t\s*\*ps_local_registry_create\s*\(/.test(
      localRegistrySource,
    ) ||
    !/registry->semantic_types\s*=\s*semantic_types\s*;/.test(
      localRegistrySource,
    ) ||
    !/registry->scope_graph\s*=\s*scope_graph\s*;/.test(
      localRegistrySource,
    ) ||
    !/ps_local_registry_semantic_types\s*\(/.test(
      localRegistryHeader + localRegistrySource,
    ) ||
    /owns_scope_graph/.test(localRegistrySource) ||
    /ps_local_registry_bind_(?:semantic_types|scope_graph)\s*\(/.test(
      localRegistryHeader + localRegistrySource,
    ) ||
    !/void\s+ps_local_registry_destroy\s*\(/.test(
      localRegistrySource,
    ) ||
    /ps_local_registry_find_visible_in\s*\(/.test(localRegistrySource) ||
    !/ps_decl_record_lvar_usage_in_region_in\s*\(/.test(
      localRegistrySource,
    ) ||
    legacyLocalRegistryGlobals.test(localRegistrySource) ||
    legacyActiveLocalRegistryMacros.test(localRegistrySource) ||
    /default_local_registry|active_local_registry/.test(localRegistrySource) ||
    /ps_local_registry_(?:active|activate)\s*\(/.test(
      localRegistrySource,
    )) {
  throw new Error(
    "function-local symbols, scopes, and usage events must be owned by an explicit registry",
  );
}
const sessionDiagnosticIdentityChecks = [
  "tk_context_diagnostics\\s*\\(\\s*&session->tokenizer\\s*\\)",
  "tk_allocator_diagnostics\\s*\\(\\s*session->token_allocator_context\\s*\\)",
  "ps_ctx_diagnostics\\s*\\(\\s*session->semantic_context\\s*\\)",
  "ps_local_registry_diagnostics\\s*\\(\\s*session->local_registry\\s*\\)",
  "pp_context_diagnostics\\s*\\(\\s*session->preprocessor_context\\s*\\)",
  "ps_parser_runtime_diagnostics\\s*\\(\\s*session->parser_runtime_context\\s*\\)",
  "ps_lowering_diagnostics\\s*\\(\\s*session->lowering_context\\s*\\)",
  "cg_context_diagnostics\\s*\\(\\s*session->codegen_emit_context\\s*\\)",
];
if (!/ps_local_registry_diagnostics\s*\(/.test(
      localRegistryHeader + localRegistrySource,
    ) ||
    !/tk_allocator_diagnostics\s*\(/.test(
      tokenizerAllocatorHeader + tokenizerAllocatorSource,
    ) ||
    sessionDiagnosticIdentityChecks.some((check) =>
      !new RegExp(`${check}\\s*==\\s*session->diagnostic_context`).test(
        compilationSessionSource,
      )
    ) ||
    !/tk_context_allocator\s*\(\s*&session->tokenizer\s*\)\s*==\s*session->token_allocator_context/.test(
      compilationSessionSource,
    ) ||
    !/tk_context_source_manager\s*\(\s*&session->tokenizer\s*\)\s*==\s*session->source_manager/.test(
      compilationSessionSource,
    ) ||
    !/diag_context_source_manager\s*\(\s*session->diagnostic_context\s*\)\s*==\s*session->source_manager/.test(
      compilationSessionSource,
    ) ||
    !/pp_context_tokenizer\s*\(\s*session->preprocessor_context\s*\)\s*==\s*&session->tokenizer/.test(
      compilationSessionSource,
    ) ||
    !/pp_context_target\s*\(\s*session->preprocessor_context\s*\)\s*==\s*&session->target/.test(
      compilationSessionSource,
    ) ||
    !/ps_ctx_arena\s*\(\s*session->semantic_context\s*\)\s*==\s*session->arena_context/.test(
      compilationSessionSource,
    ) ||
    !/ps_parser_runtime_arena\s*\(\s*session->parser_runtime_context\s*\)\s*==\s*session->arena_context/.test(
      compilationSessionSource,
    ) ||
    !/ps_parser_runtime_tokenizer\s*\(\s*session->parser_runtime_context\s*\)\s*==\s*&session->tokenizer/.test(
      compilationSessionSource,
    ) ||
    !/ps_lowering_arena\s*\(\s*session->lowering_context\s*\)\s*==\s*session->arena_context/.test(
      compilationSessionSource,
    ) ||
    !/ps_global_registry_semantic_types\s*\(\s*session->global_registry\s*\)\s*==\s*ps_ctx_semantic_type_table_in\s*\(\s*session->semantic_context\s*\)/.test(
      compilationSessionSource,
    ) ||
    !/ps_local_registry_semantic_types\s*\(\s*session->local_registry\s*\)\s*==\s*ps_ctx_semantic_type_table_in\s*\(\s*session->semantic_context\s*\)/.test(
      compilationSessionSource,
    )) {
  throw new Error(
    "CompilationSession completeness must reject split phase-owned dependencies",
  );
}
const contextFreeLocalRegistryApis =
  /\b(?:ps_local_registry_capture_lookup_point|ps_local_registry_find_visible|ps_local_registry_reset|psx_local_registry_add|ps_decl_enter_scope|ps_decl_leave_scope|ps_decl_get_locals|ps_decl_find_lvar|psx_decl_find_lvar_by_offset|ps_decl_replay_lvar_usage_events)\s*\(/;
if (contextFreeLocalRegistryApis.test(localRegistrySource)) {
  throw new Error("local registry operations must require an explicit registry");
}
const nodeUtilsSourceForRegistry = await readFile(
  "src/parser/node_utils.c",
  "utf8",
);
if (/psx_decl_find_lvar_by_offset\s*\(/.test(nodeUtilsSourceForRegistry)) {
  throw new Error(
    "local variable nodes must retain their symbol instead of resolving through an implicit registry",
  );
}
if (!/ps_ctx_register_tag_type_in\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_ctx_tag_qual_type_at_in\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_ctx_register_enum_const_in\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_ctx_find_enum_const_at_in\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_ctx_register_typedef_name_in\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_ctx_find_typedef_name_at_in\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    /ps_ctx_(?:clone_tag_type_at|find_enum_const_at|find_typedef_(?:decl_type|name)_at)_in_contexts\s*\(/.test(
      semanticContextOwnershipSource + semanticContextOwnershipHeader,
    ) ||
    /ps_ctx_(?:register_tag_type|register_enum_const|register_typedef_name|clone_tag_type_at|find_enum_const_at|find_typedef_(?:decl_type|name)_at)_in\s*\([^)]*psx_local_registry_t/.test(
      semanticContextOwnershipSource + semanticContextOwnershipHeader,
    ) ||
    !/psx_scope_graph_lookup\s*\(/.test(
      semanticContextOwnershipSource,
    )) {
  throw new Error(
    "semantic namespace operations must use the semantic context scope graph directly",
  );
}
const frontendTranslationUnitHeader = await readFile(
  "src/frontend/translation_unit.h",
  "utf8",
);
const frontendTranslationUnitSource = await readFile(
  "src/frontend/translation_unit.c",
  "utf8",
);
if (!/ag_source_manager_reset_translation_unit\s*\(\s*session->source_manager\s*\)/.test(
      compilationSessionSource,
    ) ||
    !/arena_free_all_in\s*\(\s*session->arena_context\s*\)/.test(
      compilationSessionSource,
    ) ||
    /\b(?:ag_source_manager_reset_translation_unit|psx_scope_graph_reset|ps_global_registry_reset_translation_unit_in|ps_local_registry_reset_translation_unit_in|ps_ctx_reset_translation_unit_scope_in|ps_parser_runtime_context_reset_translation_unit|ps_lowering_context_reset_translation_unit)\s*\(/.test(
      frontendTranslationUnitSource,
    )) {
  throw new Error(
    "CompilationSession must exclusively coordinate translation-unit state reset",
  );
}
const frontendTranslationUnitResolverHeader = await readFile(
  "src/frontend/translation_unit_resolver.h",
  "utf8",
);
if (!/typedef\s+struct\s*\{\s*psx_hir_node_id_t\s+hir_root\s*;\s*\}\s*psx_frontend_function_t\s*;/.test(
      frontendTranslationUnitHeader,
    ) ||
    !/int\s+psx_frontend_next_function\s*\(\s*psx_frontend_stream_t\s*\*stream\s*,\s*psx_frontend_function_t\s*\*function\s*\)/.test(
      frontendTranslationUnitHeader,
    ) ||
    /node_t\s*\*\s*psx_frontend_next_function/.test(
      frontendTranslationUnitHeader,
    ) ||
    /compatibility_function|legacy_ast/.test(
      frontendTranslationUnitHeader,
    ) ||
    /node_t\s*\*\*\s*psx_frontend_/.test(
      frontendTranslationUnitHeader,
    ) ||
    !/psx_frontend_next_function_with_resolver\s*\(/.test(
      frontendTranslationUnitResolverHeader,
    ) ||
    /\bpsx_resolution_work_tree_t\b|compatibility_root/.test(
      `${frontendTranslationUnitSource}\n${frontendTranslationUnitHeader}\n${frontendTranslationUnitResolverHeader}`,
    ) ||
    /\bnode_t\b/.test(frontendTranslationUnitResolverHeader) ||
    allSourceFiles.includes("test/support/parser_compatibility_test_hook.c") ||
    allSourceFiles.includes("test/support/parser_compatibility_test_hook.h") ||
    /psx_frontend_legacy_(?:program_ast|analyze_expression_ast)_in_session/.test(
      frontendTranslationUnitSource,
    ) ||
    allSourceFiles.includes("src/frontend/legacy_ast_api.h") ||
    allSourceFiles.includes("src/frontend/translation_unit_internal.h") ||
    /legacy_ast_api\.h/.test(compilerMainSource)) {
  throw new Error(
    "production frontend APIs must expose only Typed HIR roots",
  );
}
const parserStreamHeader = await readFile("src/parser/parser.h", "utf8");
const parserStreamSource = await readFile("src/parser/parser.c", "utf8");
const parserStreamDefinition = parserStreamHeader.match(
  /typedef\s+struct\s*\{([^]*?)\}\s*psx_parser_stream_t\s*;/,
)?.[1] ?? "";
const semanticPipelineSource = await readFile(
  "src/frontend/semantic_pipeline.c",
  "utf8",
);
const semanticPipelineHeader = await readFile(
  "src/frontend/semantic_pipeline.h",
  "utf8",
);
const semanticPipelineInternalHeader = await readFile(
  "src/frontend/semantic_pipeline_internal.h",
  "utf8",
);
const semanticTreeResolutionSource = await readFile(
  "src/semantic/semantic_tree_resolution.c",
  "utf8",
);
const semanticTreeResolutionHeader = await readFile(
  "src/semantic/semantic_tree_resolution.h",
  "utf8",
);
const typedHirDiagnosticsSource = await readFile(
  "src/semantic/typed_hir_diagnostics.c",
  "utf8",
);
const typedWarningHirInternalHeader = await readFile(
  "src/hir/hir_internal.h",
  "utf8",
);
const typedWarningSyntaxTypedHirResolutionSource = await readFile(
  "src/semantic/syntax_typed_hir_resolution.c",
  "utf8",
);
if (/\bnode_t\b|\bND_[A-Z0-9_]+\b|parser\/ast\.h/.test(
      typedHirDiagnosticsSource,
    ) ||
    /\bpsx_type_t\b|\bps_ctx_type_by_id_in\s*\(|\bps_type_(?:integer_promotion|usual_arithmetic|is_unsigned)\w*\s*\(/.test(
      typedHirDiagnosticsSource,
    ) ||
    /\bps_ctx_type_(?:size|align)of_in\s*\(/.test(
      typedHirDiagnosticsSource,
    ) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      typedHirDiagnosticsSource,
    ) ||
    !/\bpsx_integer_promotion_for_data_layout\s*\(/.test(
      typedHirDiagnosticsSource,
    ) ||
    !/\bpsx_usual_integer_conversion_for_data_layout\s*\(/.test(
      typedHirDiagnosticsSource,
    ) ||
    !/\bpsx_type_layout_sizeof\s*\(/.test(
      typedHirDiagnosticsSource,
    ) ||
    !/unsigned\s+char\s+is_source_assignment\s*;/.test(
      typedWarningHirInternalHeader,
    ) ||
    !/unsigned\s+char\s+is_declaration_initializer\s*;/.test(
      typedWarningHirInternalHeader,
    ) ||
    !/\.is_source_assignment\s*=\s*syntax->kind\s*==\s*ND_ASSIGN\s*\?\s*1\s*:\s*0/.test(
      typedWarningSyntaxTypedHirResolutionSource,
    ) ||
    !/\.is_declaration_initializer\s*=\s*1/.test(
      typedWarningSyntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "Typed HIR warnings must consume semantic provenance without depending on Syntax AST kinds",
  );
}
if (/\bis_source_assignment\b/.test(earlyAstSource)) {
  throw new Error(
    "Source assignment provenance must live only in Typed HIR, not Syntax sidecars",
  );
}
const parserUnitTestSource = await readFile(
  "test/test_parser.c",
  "utf8",
);
const tokenizerUnitTestSource = await readFile(
  "test/test_tokenizer.c",
  "utf8",
);
const wasmMachineIrUnitTestSource = await readFile(
  "test/test_wasm32_machine_ir.c",
  "utf8",
);
const unitTestSourcePaths = (await readdir(
  "test", { withFileTypes: true},
))
  .filter((entry) => entry.isFile() && /^test_.*\.[ch]$/.test(entry.name))
  .map((entry) => `test/${entry.name}`)
  .sort();
const unitTestCompatibilityCorpus = (
  await Promise.all(unitTestSourcePaths.map((file) => readFile(file, "utf8")))
).join("\n");
const declarationRegistrationBoundarySource = await readFile(
  "src/semantic/declaration_registration.c",
  "utf8",
);
const mutableCompatibilityCallers = [];
const mutableCompatibilityCallPattern =
  /\bpsx_(?:bind_identifier_(?:tree|initializer_tree)|semantic_resolve_(?:tree|initializer_tree)|lower_semantic_(?:tree|initializer_syntax)|resolve_local_declaration_syntax_tree)_in_contexts\s*\(/;
for (const file of allSourceFiles) {
  if (!file.endsWith(".c")) continue;
  const source = await readFile(file, "utf8");
  if (mutableCompatibilityCallPattern.test(source))
    mutableCompatibilityCallers.push(file);
}
if (mutableCompatibilityCallers.length > 0 ||
    !/psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts\s*\(/.test(
      declarationRegistrationBoundarySource,
    ) ||
    /psx_bind_identifier_tree|psx_semantic_resolve_tree/.test(
      declarationRegistrationBoundarySource,
    )) {
  throw new Error(
    "mutable compatibility resolution APIs must be absent; callers: " +
      (mutableCompatibilityCallers.join(", ") || "none"),
  );
}
if (!/static\s+psx_frontend_expression_hir_t\s+resolve_test_expression_hir\s*\([^]*?psx_frontend_resolve_expression_to_hir_in_contexts\s*\(/.test(
      parserUnitTestSource,
    )) {
  throw new Error(
    "parser expression boundary tests must resolve through the production frontend HIR API",
  );
}
for (const testName of [
  "test_syntax_literal_type_boundary",
  "test_additive_typed_hir_boundary",
  "test_subscript_typed_hir_boundary",
  "test_unary_deref_typed_hir_boundary",
  "test_unary_operator_typed_hir_boundary",
  "test_generic_selection_typed_hir_boundary",
  "test_sizeof_typed_hir_boundary",
  "test_expression_typed_hir_type_boundary",
  "test_function_call_typed_hir_boundary",
  "test_cast_typed_hir_boundary",
  "test_compound_assignment_typed_hir_boundary",
  "test_builtin_expect_typed_hir_boundary",
  "test_member_access_typed_hir_boundary",
  "test_expr_compound_literal_typed_hir_boundary",
  "test_expr_compound_literal_array_subscript",
  "test_expr_inc_dec_typed_hir_boundary",
  "test_expr_deref_address_typed_hir_boundary",
  "test_bool_assignment_typed_hir_boundary",
  "test_expr_number",
  "test_expr_float",
  "test_expr_long_double_suffix_metadata",
  "test_expr_add_sub",
  "test_expr_mul_div",
  "test_expr_mod",
  "test_expr_precedence",
  "test_expr_parentheses",
  "test_expr_eq_neq",
  "test_expr_relational",
  "test_expr_logical_and_or",
  "test_expr_bitwise",
  "test_expr_shift",
  "test_expr_ternary",
  "test_expr_unary_ops",
  "test_expr_sizeof",
]) {
  const body = parserUnitTestSource.match(
    new RegExp(
      `static\\s+void\\s+${testName}\\s*\\(\\s*` +
      `ag_compilation_session_t\\s*\\*\\s*test_suite_session\\s*` +
      `\\)\\s*\\{([^]*?)\\n\\}`,
    ),
  );
  if (!body || /\banalyze_test_expression\s*\(/.test(body[1]) ||
      /\bparse_expr_input\s*\(/.test(body[1]) ||
      (testName !== "test_syntax_literal_type_boundary" &&
       !/\b(?:resolve_test_expression(?:_input)?_hir|resolve_program_input_hir)\s*\(/.test(body[1]))) {
    throw new Error(
      `${testName} must validate immutable Syntax or production Typed HIR without the compatibility analyzer`,
    );
  }
}
if (/\b(?:analyze_test_expression|parse_expr_input)\s*\(/.test(
      parserUnitTestSource,
    )) {
  throw new Error(
    "parser tests must resolve standalone expressions as immutable Syntax plus Typed HIR",
  );
}
const directProgramHirHelper = parserUnitTestSource.match(
  /static\s+int\s+resolve_test_program_hir_from_in_session\s*\([^)]*\)\s*\{([^]*?)\n\}/,
);
const directProgramHirWrapper = parserUnitTestSource.match(
  /static\s+int\s+resolve_test_program_hir_from\s*\([^)]*\)\s*\{([^]*?)\n\}/,
);
const directProgramHirTests = [
  "test_typed_hir_ownership_and_type_boundary",
  ...[
    ...parserUnitTestSource.matchAll(
      /static\s+void\s+(test_typed_hir_[A-Za-z0-9_]+_without_ast)\s*\(/g,
    ),
  ].map((match) => match[1]),
];
if (!directProgramHirHelper ||
    !/\bpsx_frontend_next_function\s*\(/.test(directProgramHirHelper[1]) ||
    !directProgramHirWrapper ||
    !/\bresolve_test_program_hir_from_in_session\s*\(/.test(
      directProgramHirWrapper[1],
    ) ||
    /compatibility|psx_test_frontend_next_function/.test(
      directProgramHirHelper[1],
    ) ||
    directProgramHirTests.length !== 32) {
  throw new Error(
    "Typed HIR program tests must enter through the production frontend",
  );
}
for (const testName of directProgramHirTests) {
  const body = parserUnitTestSource.match(
    new RegExp(
      `static\\s+void\\s+${testName}\\s*\\(\\s*` +
      `ag_compilation_session_t\\s*\\*\\s*test_suite_session\\s*` +
      `\\)\\s*\\{([^]*?)\\n\\}`,
    ),
  );
  if (!body || !/\bresolve_program_input_hir\s*\(/.test(body[1]) ||
      /\bparse_program_input\s*\(/.test(body[1])) {
    throw new Error(
      `${testName} must resolve programs directly to Typed HIR`,
    );
  }
}
for (const helperName of ["expect_parse_fail", "expect_parse_ok"]) {
  const body = parserUnitTestSource.match(
    new RegExp(`static\\s+void\\s+${helperName}\\s*\\([^)]*\\)\\s*\\{([^]*?)\\n\\}`),
  );
  if (!body ||
      !/\bresolve_test_program_hir_from\s*\(/.test(body[1]) ||
      !/!resolved\s*\|\|\s*diag_has_error_records_in/.test(body[1]) ||
      /\bparse_test_program_from\s*\(/.test(body[1])) {
    throw new Error(
      `${helperName} must validate program acceptance through the production Typed HIR frontend`,
    );
  }
}
const typedHirBoundaryTests = [
  ...parserUnitTestSource.matchAll(
    /^static\s+void\s+(test_[A-Za-z0-9_]*typed_hir[A-Za-z0-9_]*)\s*\(/gm,
  ),
].map((match) => match[1]);
if (typedHirBoundaryTests.length === 0) {
  throw new Error("Typed HIR boundary tests must remain discoverable");
}
for (const testName of typedHirBoundaryTests) {
  const body = parserUnitTestSource.match(
    new RegExp(
      `static\\s+void\\s+${testName}\\s*\\(\\s*` +
      `ag_compilation_session_t\\s*\\*\\s*test_suite_session\\s*` +
      `\\)\\s*\\{([^]*?)\\n\\}`,
    ),
  );
  if (!body || /\bparse_program_input\s*\(/.test(body[1])) {
    throw new Error(
      `${testName} must not recover Typed HIR assertions from a compatibility program AST`,
    );
  }
}
for (const testName of [
  "test_parser_name_environment_boundary",
  "test_direct_literal_typed_hir_resolution_boundary",
  "test_expr_compound_literal_typed_hir_boundary",
  "test_subscript_typed_hir_boundary",
  "test_unary_deref_typed_hir_boundary",
  "test_unary_operator_typed_hir_boundary",
  "test_generic_selection_typed_hir_boundary",
  "test_compound_assignment_typed_hir_boundary",
  "test_toplevel_point_of_declaration_boundary",
  "test_parameter_declaration_storage_plan_boundary",
  "test_toplevel_declarator_phase_boundary",
  "test_member_access_typed_hir_boundary",
  "test_sizeof_typed_hir_boundary",
  "test_parse_evil_edge_cases",
]) {
  const body = parserUnitTestSource.match(
    new RegExp(
      `static\\s+void\\s+${testName}\\s*\\(\\s*` +
      `ag_compilation_session_t\\s*\\*\\s*test_suite_session\\s*` +
      `\\)\\s*\\{([^]*?)\\n\\}`,
    ),
  );
  if (!body || !/\bresolve_program_input_hir\s*\(/.test(body[1]) ||
      /\bparse_program_input\s*\(/.test(body[1])) {
    throw new Error(
      `${testName} must use production Typed HIR for program fixtures`,
    );
  }
}
if (/\(void\)\s*parsed_code\s*;/.test(parserUnitTestSource)) {
  throw new Error(
    "parser tests that discard compatibility ASTs must use the production Typed HIR frontend",
  );
}
if (/node_t\s*\*psx_frontend_/.test(semanticPipelineHeader) ||
    !/psx_frontend_resolve_parsed_function_to_hir_in_session/.test(
      semanticPipelineHeader,
    ) ||
    !/psx_resolve_parsed_function_hir_from_syntax_in_contexts/.test(
      semanticPipelineSource,
    ) ||
    /\bpsx_typed_hir_tree_t\b|psx_typed_hir_tree_emit|typed_hir_materialization/.test(
      `${semanticPipelineSource}\n${semanticPipelineHeader}\n${semanticPipelineInternalHeader}`,
    ) ||
    /\bpsx_resolution_work_tree_t\b|psx_resolution_work_tree_/.test(
      `${semanticPipelineSource}\n${semanticPipelineHeader}\n${semanticPipelineInternalHeader}`,
    ) ||
    /psx_frontend_resolve_function_(?:to_hir|work_tree)_in_session/.test(
      `${semanticPipelineSource}\n${semanticPipelineHeader}\n${semanticPipelineInternalHeader}`,
    ) ||
    /psx_frontend_legacy_|node_t\s*\*\s*psx_frontend_/.test(
      `${semanticPipelineSource}\n${semanticPipelineInternalHeader}`,
    ) ||
    /semantic_pipeline_internal\.h/.test(compilerMainSource)) {
  throw new Error(
    "frontend semantic pipeline APIs must consume resolved HIR without exposing intermediate tree state",
  );
}
const assignmentResolutionHeader = await readFile(
  "src/semantic/assignment_resolution.h",
  "utf8",
);
const assignmentResolutionSource = await readFile(
  "src/semantic/assignment_resolution.c",
  "utf8",
);
const typeCompletenessHeader = await readFile(
  "src/semantic/type_completeness.h",
  "utf8",
);
const typeCompletenessSource = await readFile(
  "src/semantic/type_completeness.c",
  "utf8",
);
const callResolutionHeader = await readFile(
  "src/semantic/call_resolution.h",
  "utf8",
);
const callResolutionSource = await readFile(
  "src/semantic/call_resolution.c",
  "utf8",
);
const localDeclarationHeader = await readFile(
  "src/parser/local_declaration_syntax.h",
  "utf8",
);
const localDeclarationFrontendSource = await readFile(
  "src/frontend/local_declaration.c",
  "utf8",
);
const localDeclarationFrontendHeader = await readFile(
  "src/frontend/local_declaration.h",
  "utf8",
);
const toplevelDeclarationHeader = await readFile(
  "src/parser/toplevel_declaration_syntax.h",
  "utf8",
);
const toplevelDeclarationFrontendSource = await readFile(
  "src/frontend/toplevel_declaration.c",
  "utf8",
);
const frontendFunctionDefinitionSource = await readFile(
  "src/semantic/function_definition_resolution.c",
  "utf8",
);
const toplevelDeclarationSyntaxSource = await readFile(
  "src/parser/toplevel_declaration_syntax.c",
  "utf8",
);
const functionDefinitionSyntaxHeader = await readFile(
  "src/parser/function_definition_syntax.h",
  "utf8",
);
const declarationBindingEventsSource = await readFile(
  "src/parser/declaration_binding_events.c",
  "utf8",
);
const localObjectLoweringSource = await readFile(
  "src/lowering/local_object_lowering.c",
  "utf8",
);
const localObjectLoweringHeader = await readFile(
  "src/lowering/local_object_lowering.h",
  "utf8",
);
const parameterLoweringSource = await readFile(
  "src/lowering/parameter_lowering.c",
  "utf8",
);
const parameterLoweringHeader = await readFile(
  "src/lowering/parameter_lowering.h",
  "utf8",
);
const vlaLoweringSource = await readFile(
  "src/lowering/vla_lowering.c",
  "utf8",
);
const vlaLoweringHeader = await readFile(
  "src/lowering/vla_lowering.h",
  "utf8",
);
const vlaRuntimeHeaderSource = await readFile(
  "src/parser/vla_runtime.h",
  "utf8",
);
const vlaRuntimePlanHeaderSource = await readFile(
  "src/semantic/vla_runtime_plan.h",
  "utf8",
);
const frameLayoutSource = await readFile(
  "src/lowering/frame_layout.c",
  "utf8",
);
const staticLocalLoweringSource = await readFile(
  "src/lowering/static_local_lowering.c",
  "utf8",
);
const staticLocalLoweringHeader = await readFile(
  "src/lowering/static_local_lowering.h",
  "utf8",
);
const globalObjectLoweringHeader = await readFile(
  "src/lowering/global_object_lowering.h",
  "utf8",
);
const localDeclarationPipelineSource = await readFile(
  "src/declaration_pipeline.c",
  "utf8",
);
const staticHirInitializerSource = await readFile(
  "src/lowering/static_hir_initializer.c",
  "utf8",
);
if (/\bps_(?:ctx_active|global_registry_active|local_registry_active)\s*\(/.test(
      localDeclarationPipelineSource,
    )) {
  throw new Error(
    "declaration pipelines must use only their explicit compiler contexts",
  );
}
const ambiguousFrontendContextApis =
  /psx_(?:frontend_analyze_(?:function|expression|initializer_syntax|program)|apply_function_definition(?:_header)?|apply_toplevel_declaration|frontend_init_(?:toplevel|local)_declaration_callbacks)_in_context\s*\(/;
if (ambiguousFrontendContextApis.test(
      `${semanticPipelineSource}\n${frontendFunctionDefinitionSource}\n${toplevelDeclarationFrontendSource}\n${localDeclarationFrontendSource}`,
    )) {
  throw new Error(
    "frontend APIs must not combine one passed semantic context with active registries",
  );
}
if (!/ag_compilation_session_t\s*\*session\s*;/.test(
      frontendTranslationUnitHeader,
    ) ||
    /owns_session_activation/.test(
      frontendTranslationUnitHeader,
    ) ||
    !/int\s+psx_frontend_stream_begin\s*\(/.test(
      frontendTranslationUnitHeader,
    ) ||
    !/psx_frontend_stream_begin\s*\([\s\S]*?ag_compilation_session_t\s*\*session/.test(
      frontendTranslationUnitHeader,
    ) ||
    !/ps_global_registry_reset_diag_state_in\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    /psx_(?:semantic_context|global_registry|local_registry)_t\s*\*/.test(
      parserStreamDefinition,
    ) ||
    /\bpsx_(?:semantic_context|global_registry|local_registry)_t\b/.test(
      parserStreamHeader,
    ) ||
    /\bps_expr_in_contexts\s*\(/.test(parserStreamHeader) ||
    !/psx_parser_syntax_services_t\s+syntax\s*;/.test(
      parserStreamDefinition,
    ) ||
    !/psx_parser_runtime_context_t\s*\*runtime_context\s*;/.test(
      parserStreamDefinition,
    ) ||
    !/ps_parser_stream_begin_with_syntax\s*\(/.test(
      parserStreamSource,
    ) ||
    /stream->(?:semantic_context|global_registry|local_registry)/.test(
      parserStreamSource,
    ) ||
    !/psx_frontend_resolve_parsed_function_to_hir_in_session\s*\(/.test(
      frontendTranslationUnitSource,
    )) {
  throw new Error("frontend stream must receive the compilation-unit context explicitly");
}
const frontendStreamCore = frontendTranslationUnitSource.slice(
  frontendTranslationUnitSource.indexOf("int psx_frontend_stream_begin("),
  frontendTranslationUnitSource.indexOf(
    "int psx_frontend_free_processed_ast_in_session(",
  ),
);
if (/\bps_(?:ctx_active|global_registry_active|local_registry_active)\s*\(/.test(
      frontendStreamCore,
    ) ||
    !/frontend_session_is_complete\s*\(stream->session\)/.test(
      frontendStreamCore,
    ) ||
    /ag_compilation_session_(?:activate|deactivate|is_active|active_compat)\s*\(/.test(
      frontendStreamCore,
    ) ||
    /owns_session_activation/.test(frontendStreamCore)) {
  throw new Error(
    "frontend stream core must use only the explicitly supplied CompilationSession",
  );
}
if (!/frontend_session_is_complete\s*\([^)]*\)\s*\{\s*return\s+ag_compilation_session_is_complete\s*\(session\)\s*;\s*\}/.test(
      frontendTranslationUnitSource,
    ) ||
    !/ag_compilation_session_is_complete\s*\(session\)/.test(
      semanticPipelineSource,
    ) ||
    !/ag_compilation_session_is_complete\s*\(session\)/.test(
      translationUnitDataLoweringSource,
    ) ||
    /!session\s*\|\|\s*!session->/.test(
      semanticPipelineSource,
    )) {
  throw new Error(
    "explicit frontend and lowering APIs must use CompilationSession completeness as their validity source",
  );
}
const directSessionContextField =
  /(?:\bsession|stream->session)->(?:semantic_context|global_registry|local_registry|arena_context|diagnostic_context|parser_runtime_context|lowering_context)\b/;
const productionSessionConsumers = [
  compilerMainSource,
  frontendTranslationUnitSessionSource,
  semanticPipelineSource,
  translationUnitDataLoweringSource,
  hirIrBuilder,
  preprocessSource,
];
if (directSessionContextField.test(productionSessionConsumers.join("\n")) ||
    productionSessionConsumers.some((source) =>
      /compilation_session_(?:internal|compat)\.h/.test(source)
    )) {
  throw new Error(
    "CompilationSession consumers must use ownership accessors instead of public struct fields",
  );
}

const contextFreeApiDeclarations = [
  {
    core: frontendTranslationUnitHeader,
    source: frontendTranslationUnitSource,
    names: [
      "psx_frontend_reset_translation_unit_state",
      "psx_frontend_free_processed_ast",
      "psx_frontend_program",
      "psx_frontend_program_from",
      "psx_frontend_program_ctx",
    ],
  },
  {
    core: preprocessHeader,
    source: preprocessSource,
    names: ["preprocess", "preprocess_ctx", "pp_stream_open"],
  },
  {
    core: translationUnitDataLoweringHeader,
    source: translationUnitDataLoweringSource,
    names: ["lower_ir_translation_unit_data"],
  },
];
for (const { core, source, names } of contextFreeApiDeclarations) {
  for (const name of names) {
    const declaration = new RegExp(`\\b${name}\\s*\\(`);
    if (declaration.test(core) || declaration.test(source)) {
      throw new Error(
        `context-free API ${name} must not return`,
      );
    }
  }
}
const toplevelSyntaxContextDefinition =
  toplevelDeclarationHeader.match(
    /typedef\s+struct\s*\{([^]*?)\}\s*psx_toplevel_declaration_syntax_context_t\s*;/,
  )?.[1] ?? "";
if (/\bpsx_(?:global_registry|local_registry|lowering_context)_t\s*\*/.test(
      toplevelSyntaxContextDefinition,
    ) ||
    /\bag_compilation_options_t\s*\*/.test(
      toplevelSyntaxContextDefinition,
    ) ||
    /\b(?:begin_declaration|begin_declarator|finish_declarator|finish_declaration|abort_declaration)\b/.test(
      toplevelSyntaxContextDefinition,
    ) ||
    /\bapplied_during_parse\b/.test(
      `${toplevelDeclarationHeader}\n${toplevelDeclarationSyntaxSource}`,
    ) ||
    /\bpsx_apply_(?:toplevel_declaration|parsed_decl_specifier|parsed_declarator)[a-z_]*\s*\(/.test(
      toplevelDeclarationSyntaxSource,
    )) {
  throw new Error(
    "top-level syntax parsing must not own semantic application state",
  );
}
if (!/psx_name_classifier_t\s+name_classifier\s*;/.test(
      toplevelSyntaxContextDefinition,
    ) ||
    !/psx_parser_runtime_context_t\s*\*runtime_context\s*;/.test(
      toplevelSyntaxContextDefinition,
    ) ||
    !/parse_assignment_expression/.test(
      toplevelSyntaxContextDefinition,
    ) ||
    !/ps_name_classifier_declare\s*\(\s*&context->name_classifier/.test(
      toplevelDeclarationSyntaxSource,
    ) ||
    !/psx_apply_parsed_decl_specifier_qual_type_in_contexts\s*\(/.test(
      toplevelDeclarationFrontendSource,
    ) ||
    !/psx_apply_parsed_declarator_qual_type_in_contexts\s*\(/.test(
      toplevelDeclarationFrontendSource,
    ) ||
    /psx_frontend_init_toplevel_declaration_callbacks_in_contexts\s*\(/.test(
      `${toplevelDeclarationFrontendSource}\n${frontendTranslationUnitSource}`,
    ) ||
    !/psx_apply_toplevel_declaration_in_contexts\s*\(/.test(
      frontendTranslationUnitSource,
    )) {
  throw new Error(
    "top-level declarations must update syntax names during parse and apply semantics afterward",
  );
}
const explicitLocalDeclarationLowering = [
  localObjectLoweringSource,
  parameterLoweringSource,
  vlaLoweringSource,
  staticLocalLoweringSource,
].join("\n");
if (/\bpsx_(?:semantic_context|global_registry|local_registry)_t\s*\*/.test(
      localDeclarationHeader,
    ) ||
    !/psx_name_classifier_t\s+name_classifier\s*;/.test(
      localDeclarationHeader,
    ) ||
    !/psx_parser_runtime_context_t\s*\*runtime_context\s*;/.test(
      localDeclarationHeader,
    ) ||
    !/parse_decl_specifier/.test(localDeclarationHeader) ||
    !/parse_declarator/.test(localDeclarationHeader) ||
    !/parse_initializer/.test(localDeclarationHeader) ||
    /\b(?:begin_declaration|begin_declarator|finish_declarator|finish_declaration|abort_declaration)\s*\)\s*\(/.test(
      localDeclarationHeader,
    ) ||
    /\bpsx_lowering_context_t\s*\*lowering_context\s*;|\bag_compilation_options_t\s*\*options\s*;/.test(
      localDeclarationHeader,
    ) ||
    /callbacks->(?:context|lowering_context|options|begin_declaration|begin_declarator|finish_declarator|finish_declaration|abort_declaration)/.test(
      localDeclarationFrontendSource,
    ) ||
    /callbacks->(?:semantic_context|global_registry|local_registry)/.test(
      localDeclarationFrontendSource,
    ) ||
    !/psx_frontend_local_declaration_syntax_adapter_t/.test(
      localDeclarationFrontendSource,
    ) ||
    /\bps_local_registry_create_storage_object\s*\(/.test(
      explicitLocalDeclarationLowering,
    ) ||
    /\bps_local_registry_create_static_alias\s*\(/.test(
      staticLocalLoweringSource,
    ) ||
    /\bps_register_global_var\s*\(/.test(staticLocalLoweringSource) ||
    /\bps_decl_find_lvar\s*\(/.test(vlaLoweringSource) ||
    !/ps_local_registry_create_storage_object_qual_type_in\s*\(/.test(
      localObjectLoweringSource,
    ) ||
    !/ps_local_registry_create_storage_object_qual_type_in\s*\(/.test(
      parameterLoweringSource,
    ) ||
    !/ps_local_registry_create_storage_object_qual_type_in\s*\(/.test(
      vlaLoweringSource,
    ) ||
    !/ps_local_registry_create_static_alias_qual_type_in\s*\(/.test(
      staticLocalLoweringSource,
    ) ||
    !/ps_register_global_var_in\s*\(/.test(staticLocalLoweringSource)) {
  throw new Error(
    "local declaration syntax must receive only runtime, NameClassifier, and syntax services while semantic application and lowering use frontend-owned registries",
  );
}
const resolvedGlobalObjectRequest = globalObjectLoweringHeader.match(
  /typedef\s+struct\s*\{([^{}]*)\}\s*psx_resolved_global_object_request_t\s*;/,
);
if (/\bpsx_type_t\b/.test(localObjectLoweringHeader) ||
    /\bpsx_type_t\b/.test(staticLocalLoweringHeader) ||
    /\bps_lowering_type_id\s*\(|\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      `${localObjectLoweringSource}\n${staticLocalLoweringSource}`,
    ) ||
    /\bps_local_registry_(?:create_storage_object_in|create_internal_storage_object_in|create_static_alias_in|complete_array_type)\s*\(/.test(
      `${localObjectLoweringSource}\n${staticLocalLoweringSource}`,
    ) ||
    !/\bps_local_registry_create_storage_object_qual_type_in\s*\(/.test(
      localObjectLoweringSource,
    ) ||
    !/\bps_local_registry_create_internal_storage_object_qual_type_in\s*\(/.test(
      localObjectLoweringSource,
    ) ||
    !/\bps_local_registry_create_static_alias_qual_type_in\s*\(/.test(
      staticLocalLoweringSource,
    ) ||
    !/\bps_local_registry_complete_array_qual_type\s*\(/.test(
      `${localObjectLoweringSource}\n${staticLocalLoweringSource}`,
    ) ||
    !resolvedGlobalObjectRequest ||
    /\bpsx_type_t\b/.test(resolvedGlobalObjectRequest[1]) ||
    !/\bpsx_global_declaration_resolution_t\s*\*\s*resolution\b/.test(
      resolvedGlobalObjectRequest[1],
    )) {
  throw new Error(
    "resolved object storage lowering must consume canonical QualType identities without compatibility type views",
  );
}
const canonicalLocalArrayCompletion = localRegistrySource.match(
  /int\s+ps_local_registry_complete_array_qual_type\s*\([^]*?\n\}/,
);
if (!/\bps_local_registry_create_storage_object_qual_type_in\s*\(/.test(
      localRegistryHeader,
    ) ||
    !/\bps_local_registry_create_static_alias_qual_type_in\s*\(/.test(
      localRegistryHeader,
    ) ||
    !canonicalLocalArrayCompletion ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      canonicalLocalArrayCompletion[0],
    ) ||
    /\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      canonicalLocalArrayCompletion[0],
    )) {
  throw new Error(
    "local registry canonical storage APIs must validate TypeShape without restoring compatibility type views",
  );
}
if (!/psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts\s*\([\s\S]*?request->local_registry/.test(
      localDeclarationPipelineSource,
    ) ||
    !/psx_frontend_resolve_expression_to_hir_in_contexts\s*\([\s\S]*?request->local_registry/.test(
      localDeclarationPipelineSource,
    ) ||
    /psx_frontend_analyze_(?:expression|initializer_syntax)_in_contexts\s*\(/.test(
      localDeclarationPipelineSource,
    )) {
  throw new Error(
    "static local initializers must resolve to Typed HIR or an aggregate data plan with the declaration registry",
  );
}
const staticInitializerMaterializationSource = await readFile(
  "src/semantic/static_initializer_materialization.c",
  "utf8",
);
const staticAggregateFrontendBoundary =
  semanticPipelineSource.match(
    /int\s+psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts\s*\([^)]*\)\s*\{([^]*?)\n\}/,
  )?.[1] ?? "";
if (!/psx_resolve_initializer_hir_from_syntax_in_contexts\s*\(/.test(
      staticAggregateFrontendBoundary,
    ) ||
    !/psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts\s*\([^)]*\bpsx_qual_type_t\s+type\b/.test(
      semanticPipelineSource,
    ) ||
    /\bps_lowering_type_id\s*\(/.test(
      staticAggregateFrontendBoundary,
    ) ||
    !/psx_build_static_aggregate_hir_initializer_plan\s*\(/.test(
      staticAggregateFrontendBoundary,
    ) ||
    /\bpsx_typed_hir_tree_t\b|psx_typed_hir_tree_emit|psx_materialize_static_aggregate_initializer_plan/.test(
      staticAggregateFrontendBoundary,
    ) ||
    /psx_resolution_work_tree_export_compatibility_ast\s*\(/.test(
      staticAggregateFrontendBoundary,
    ) ||
    !/const\s+psx_typed_hir_tree_t\s*\*typed_tree/.test(
      staticInitializerMaterializationSource,
    ) ||
    !/\bpsx_qual_type_t\s+type\b/.test(
      staticInitializerMaterializationSource,
    ) ||
    /\bps_lowering_type_id\s*\(/.test(
      staticInitializerMaterializationSource,
    ) ||
    !/psx_typed_hir_tree_emit\s*\(/.test(
      staticInitializerMaterializationSource,
    ) ||
    !/psx_build_static_aggregate_hir_initializer_plan\s*\(/.test(
      staticInitializerMaterializationSource,
    ) ||
    /\bpsx_resolution_work_tree_t\b|psx_resolution_work_tree_/.test(
      staticInitializerMaterializationSource,
    ) ||
    /#include\s+"\.\.\/frontend\//.test(
      staticInitializerMaterializationSource,
    )) {
  throw new Error(
    "static aggregate initializer production paths must emit and lower Typed HIR without exporting or reading the compatibility AST",
  );
}
if (/\bnode_t\b|\bND_[A-Z0-9_]+\b|parser\/ast\.h|resolution_state/.test(
      staticHirInitializerSource,
    ) ||
    !/psx_hir_module_lookup\s*\(/.test(staticHirInitializerSource) ||
    !/psx_hir_node_qual_type\s*\(/.test(staticHirInitializerSource) ||
    !/PSX_HIR_INITIALIZER_LIST/.test(staticHirInitializerSource) ||
    !/PSX_HIR_EDGE_INITIALIZER_VALUE/.test(staticHirInitializerSource) ||
    !/psx_build_static_aggregate_hir_initializer_plan\s*\(/.test(
      staticHirInitializerSource,
    ) ||
    !/initializer_hir/.test(localDeclarationPipelineSource) ||
    !/psx_frontend_expression_hir_dispose\s*\(/.test(
      localDeclarationPipelineSource,
    )) {
  throw new Error(
    "scalar and aggregate static initializer lowering must consume owned Typed HIR without reading semantic AST nodes",
  );
}
const identifierResolutionSource = await readFile(
  "src/semantic/identifier_resolution.c",
  "utf8",
);
const identifierResolutionHeader = await readFile(
  "src/semantic/identifier_resolution.h",
  "utf8",
);
const functionDeclarationResolutionSource = await readFile(
  "src/semantic/function_declaration_resolution.c",
  "utf8",
);
const functionDeclarationResolutionHeader = await readFile(
  "src/semantic/function_declaration_resolution.h",
  "utf8",
);
if (!/case\s+PSX_DECL_FUNCTION:[^]*?resolution->function\s*=\s*declaration->payload/.test(
      identifierResolutionSource,
    ) ||
    /ps_ctx_find_function_symbol\s*\(/.test(identifierResolutionSource) ||
    !/ps_ctx_register_function_qual_type_in\s*\(/.test(
      functionDeclarationResolutionSource,
    ) ||
    /\bpsx_type_t\b/.test(functionDeclarationResolutionSource) ||
    !/psx_semantic_type_table_describe\s*\(/.test(
      functionDeclarationResolutionSource,
    ) ||
    !/ps_ctx_track_function_defined_in\s*\(/.test(
      functionDeclarationResolutionSource,
    )) {
  throw new Error(
    "semantic function-symbol resolution must use the passed semantic context",
  );
}
const tagDeclarationResolutionSource = await readFile(
  "src/semantic/tag_declaration_resolution.c",
  "utf8",
);
const tagDeclarationResolutionHeader = await readFile(
  "src/semantic/tag_declaration_resolution.h",
  "utf8",
);
const aggregateMemberResolutionSource = await readFile(
  "src/semantic/aggregate_member_resolution.c",
  "utf8",
);
const aggregateMemberResolutionHeader = await readFile(
  "src/semantic/aggregate_member_resolution.h",
  "utf8",
);
const aggregateRegistryHeader = await readFile(
  "src/parser/semantic_ctx.h",
  "utf8",
);
const aggregateMemberResolutionType = aggregateMemberResolutionHeader.match(
  /typedef struct\s*\{((?:(?!typedef struct)[\s\S])*?)\}\s*psx_aggregate_member_declaration_resolution_t\s*;/,
);
const aggregateLayoutStateType = aggregateMemberResolutionHeader.match(
  /typedef struct\s*\{((?:(?!typedef struct)[\s\S])*?)\}\s*psx_aggregate_layout_state_t\s*;/,
);
const aggregateMemberRequestType = aggregateMemberResolutionHeader.match(
  /typedef struct\s*\{((?:(?!typedef struct)[\s\S])*?)\}\s*psx_aggregate_member_declaration_request_t\s*;/,
);
if (!aggregateMemberResolutionType ||
    !/\bpsx_type_id_t\s+type_id\s*;/.test(
      aggregateMemberResolutionType[1],
    ) ||
    /\bpsx_type_t\b/.test(aggregateMemberResolutionType[1]) ||
    !/\bpsx_resolve_decl_qual_type\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    /\bpsx_type_t\b|\bpsx_build_decl_type\s*\(|\bpsx_resolve_decl_type\s*\(|\bps_ctx_intern_qual_type_in\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    /\bps_type_(?:size|align)of_id_for_target\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bpsx_qual_type_layout_sizeof\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bpsx_qual_type_layout_alignof\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bps_ctx_record_layout_table_in\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    /\baggregate_definition\b/.test(aggregateMemberResolutionSource) ||
    !/\bpsx_semantic_type_table_array_leaf\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bps_ctx_get_record_decl_in\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/record->is_complete/.test(aggregateMemberResolutionSource) ||
    !aggregateLayoutStateType ||
    !/\bpsx_record_id_t\s+record_id\s*;/.test(
      aggregateLayoutStateType[1],
    ) ||
    !/\bpsx_type_kind_t\s+record_kind\s*;/.test(
      aggregateLayoutStateType[1],
    ) ||
    /\btoken_kind_t\s+kind\s*;/.test(aggregateLayoutStateType[1]) ||
    !aggregateMemberRequestType ||
    !/\bpsx_qual_type_t\s+base_qual_type\s*;/.test(
      aggregateMemberRequestType[1],
    ) ||
    /\bpsx_type_t\b/.test(aggregateMemberRequestType[1]) ||
    /\btarget_tag_(?:kind|name|name_len)\b/.test(
      aggregateMemberRequestType[1],
    ) ||
    !/\bps_ctx_register_record_members_in\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    /\bps_ctx_register_tag_members_in\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    /\bps_ctx_resolve_tag_record_id_in\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bpsx_aggregate_layout_init\s*\([^;]*const\s+psx_record_decl_t\s*\*\s*record\s*\)/s.test(
      aggregateMemberResolutionHeader,
    ) ||
    /\btag_member_info_t\b/.test(aggregateMemberResolutionSource) ||
    /\bps_ctx_(?:get|find)_tag_member_info/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bpsx_record_layout_member\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/ps_ctx_register_record_members_in\s*\([^;]*const\s+psx_record_member_decl_t\s*\*\s*declarations\s*,[^;]*const\s+psx_record_member_layout_t\s*\*\s*layouts/s.test(
      aggregateRegistryHeader,
    ) ||
    /\bps_ctx_register_tag_members_in\s*\(/.test(aggregateRegistryHeader) ||
    /\bps_type_(?:size|align)of_for_target\s*\(/.test(
      aggregateMemberResolutionSource,
    )) {
  throw new Error(
    "aggregate member resolution must separate RecordDecl completeness from QualType target layout",
  );
}
const declarationApplicationSource = await readFile(
  "src/semantic/declaration_application.c",
  "utf8",
);
const declarationSpecifierResolutionSource = await readFile(
  "src/semantic/declaration_specifier_resolution.c",
  "utf8",
);
const declarationSpecifierResolutionHeader = await readFile(
  "src/semantic/declaration_specifier_resolution.h",
  "utf8",
);
const declarationSpecifierValueResolutionStruct =
  declarationSpecifierResolutionHeader.match(
    /typedef struct\s*\{((?:(?!typedef struct)[\s\S])*?)\}\s*psx_decl_specifier_value_resolution_t\s*;/,
  );
if (!declarationSpecifierValueResolutionStruct ||
    !/\bpsx_qual_type_t\s+base_qual_type\s*;/.test(
      declarationSpecifierValueResolutionStruct[1],
    ) ||
    /\bpsx_type_t\b|\bbase_type\b/.test(
      declarationSpecifierValueResolutionStruct[1],
    ) ||
    !/resolution->base_qual_type\s*=\s*[^;]*psx_resolve_decl_specifier_qual_type_in_context\s*\(/s.test(
      declarationSpecifierResolutionSource,
    )) {
  throw new Error(
    "declaration specifier resolution must publish canonical QualType instead of owning a compatibility type view",
  );
}
const declarationSpecifierDirectHirSource = await readFile(
  "src/semantic/syntax_typed_hir_resolution.c",
  "utf8",
);
const directLocalDeclarationStart =
  declarationSpecifierDirectHirSource.indexOf(
    "static int preflight_direct_local_declaration(",
  );
const directLocalDeclarationEnd = declarationSpecifierDirectHirSource.indexOf(
  "static int preflight_direct_statement(",
  directLocalDeclarationStart,
);
const directLocalDeclaration =
  directLocalDeclarationStart >= 0 && directLocalDeclarationEnd > 0
    ? declarationSpecifierDirectHirSource.slice(
        directLocalDeclarationStart,
        directLocalDeclarationEnd,
      )
    : "";
if (!directLocalDeclaration ||
    /specifier_resolution\.base_type\b/.test(directLocalDeclaration) ||
    /\bconst\s+psx_type_t\s*\*\s*base_type\b/.test(
      directLocalDeclaration,
    ) ||
    /\bpsx_apply_runtime_declarator_type_in_context\s*\(/.test(
      directLocalDeclaration,
    ) ||
    !/specifier_resolution\.base_qual_type\b/.test(
      directLocalDeclaration,
    ) ||
    !/\bpsx_apply_runtime_declarator_qual_type_in_context\s*\(/.test(
      directLocalDeclaration,
    )) {
  throw new Error(
    "direct local declarations must apply declarators to canonical QualType identities",
  );
}
if (!/PSX_PARSED_TAG_DEFINITION[^]*?PSX_TAG_DECLARATION_FORWARD/.test(
      declarationApplicationSource,
    ) ||
    !/PSX_PARSED_TAG_DEFINITION[^]*?PSX_TAG_DECLARATION_FORWARD/.test(
      declarationSpecifierResolutionSource,
    )) {
  throw new Error(
    "tag definitions must bind an incomplete RecordId before resolving members",
  );
}
const declaratorBoundResolutionSource = await readFile(
  "src/semantic/declarator_bound_resolution.c",
  "utf8",
);
const splitDeclarationApplicationApi =
  /psx_apply_(?:parsed_(?:enum_body|aggregate_body_layout|type_name|declarator_type|decl_specifier|standalone_tag|declarator|function_parameters)|runtime_parsed_declarator(?:_ex)?)_in_context\s*\(/;
if (splitDeclarationApplicationApi.test(declarationApplicationSource)) {
  throw new Error(
    "declaration application APIs must not combine one context with active registries",
  );
}
const splitSemanticTypeResolutionApi =
  /psx_(?:bind_type_name_ref|resolve_bound_type_name_ref|resolve_sizeof_query|resolve_alignof_query|resolve_generic_selection)_in_context\s*\(/;
for (const sourcePath of [
  "src/semantic/type_name_resolution.c",
  "src/semantic/generic_selection_resolution.c",
]) {
  const source = await readFile(sourcePath, "utf8");
  if (splitSemanticTypeResolutionApi.test(source)) {
    throw new Error(
      `${sourcePath} must not combine one context with active registries`,
    );
  }
}
const splitParserContextApi =
  /(?:psx_(?:expr_(?:expr|assign)|parse_statement_expression|stmt_stmt|parse_initializer_syntax_(?:value|list)|finish_toplevel_declaration_syntax)|ps_parse_runtime_declarator_expressions|ps_parser_stream_begin)_in_context\s*\(/;
for (const sourcePath of [
  "src/parser/expr.c",
  "src/parser/declaration_syntax.c",
  "src/parser/initializer_syntax.c",
  "src/parser/stmt.c",
  "src/parser/parser.c",
  "src/parser/toplevel_declaration_syntax.c",
]) {
  const source = await readFile(sourcePath, "utf8");
  if (splitParserContextApi.test(source)) {
    throw new Error(
      `${sourcePath} must not combine a semantic context with the active local registry`,
    );
  }
}
const frontendDeclarationSources = [
  await readFile("src/frontend/toplevel_declaration.c", "utf8"),
  await readFile("src/semantic/function_definition_resolution.c", "utf8"),
].join("\n");
const contextFreeTagRegistryCall =
  /\bps_ctx_(?:has_tag_type|register_tag_type|get_tag_size|get_tag_align|ensure_tag_record_decl|get_tag_member_count|register_tag_members|find_tag_member_info)\s*\(/;
if (contextFreeTagRegistryCall.test(tagDeclarationResolutionSource) ||
    contextFreeTagRegistryCall.test(aggregateMemberResolutionSource) ||
    !/psx_apply_parsed_tag_declaration_in\s*\(/.test(
      declarationApplicationSource,
    ) ||
    !/psx_apply_parsed_decl_specifier_qual_type_in_contexts\s*\(/.test(
      frontendDeclarationSources,
    ) ||
    !/psx_apply_parsed_standalone_tag_in_contexts\s*\(/.test(
      frontendDeclarationSources,
    )) {
  throw new Error(
    "tag registration and layout resolution must use the passed semantic context",
  );
}
const memberResolutionSource = await readFile(
  "src/semantic/member_resolution.c",
  "utf8",
);
const memberResolutionHeader = await readFile(
  "src/semantic/member_resolution.h",
  "utf8",
);
const hirMemberResolutionHeader = await readFile(
  "src/semantic/hir_member_resolution.h",
  "utf8",
);
const hirMemberResolutionSource = await readFile(
  "src/semantic/hir_member_resolution.c",
  "utf8",
);
const memberAccessAstHeader = await readFile(
  "src/parser/ast.h",
  "utf8",
);
const memberNodeUtilsSource = await readFile(
  "src/parser/node_utils.c",
  "utf8",
);
const memberNodeUtilsHeader = await readFile(
  "src/parser/node_utils.h",
  "utf8",
);
const memberTypeIdentitySource = await readFile(
  "src/semantic/type_identity.c",
  "utf8",
);
const memberAccessResolutionType = memberResolutionHeader.match(
  /typedef struct\s*\{((?:(?!typedef struct)[\s\S])*?)\}\s*psx_member_access_resolution_t\s*;/,
);
if (/\bps_node_new_tag_member_(?:deref|lvar_ref)_for_in\s*\(/.test(
      memberNodeUtilsSource,
    ) ||
    /\bps_node_new_tag_member_(?:deref|lvar_ref)_for_in\s*\(/.test(
      memberNodeUtilsHeader,
    )) {
  throw new Error(
    "member node constructors must require explicit declaration and layout inputs",
  );
}
if (/\bpsx_record_layout_(?:table_lookup|member)\s*\(/.test(
      memberResolutionSource,
    ) ||
    /\baggregate_definition\b/.test(memberResolutionSource) ||
    /\bps_type_find_aggregate_member\s*\(/.test(
      memberResolutionSource,
    ) ||
    !/\bps_ctx_find_record_member_in\s*\(/.test(
      memberResolutionSource,
    ) ||
    /\baggregate_member_(?:index|named)\s*\(|\bmemcmp\s*\(/.test(
      memberResolutionSource,
    ) ||
    !/ps_ctx_find_record_member_in\s*\([^]*?psx_scope_graph_lookup_declaration_in_scope\s*\([^]*?PSX_NAMESPACE_MEMBER/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/resolution->record_id\s*=/.test(memberResolutionSource) ||
    !/\bpsx_resolve_member_access_qual_type_in\s*\(/.test(
      memberResolutionSource,
    ) ||
    !/\bpsx_record_member_decl_t\s+declaration\s*;/.test(
      memberResolutionHeader,
    ) ||
    /\btag_member_info_t\s+member\s*;/.test(
      memberResolutionHeader,
    ) ||
    /\bpsx_record_member_decl_t\s*\*\s*resolved_member\s*;/.test(
      memberAccessAstHeader,
    ) ||
    /\btag_member_info_t\s*\*\s*resolved_member\s*;/.test(
      memberAccessAstHeader,
    ) ||
    /resolution->declaration\.(?:offset|bit_offset)\b/.test(
      memberResolutionSource,
    ) ||
    !/\bpsx_resolve_member_access_qual_type_in\s*\(/.test(
      hirMemberResolutionSource,
    ) ||
    !/\bpsx_record_layout_table_lookup\s*\(/.test(
      hirMemberResolutionSource,
    ) ||
    !/\bpsx_record_layout_member\s*\(/.test(
      hirMemberResolutionSource,
    ) ||
    !/\bPSX_HIR_MEMBER_ACCESS\b/.test(
      hirMemberResolutionSource,
    ) ||
    /member_access_resolution\.h/.test(
      `${hirMemberResolutionHeader}\n${hirMemberResolutionSource}`,
    )) {
  throw new Error(
    "member access semantics must retain RecordId and ordinal while each Typed HIR materializer resolves target offsets",
  );
}
const memberQualTypeCore = memberResolutionSource.match(
  /void\s+psx_resolve_member_access_qual_type_in\s*\([^]*?\n\}/,
);
if (!/\bpsx_qual_type_t\s+base_object_qual_type\s*;/.test(
      memberResolutionHeader,
    ) ||
    !/\bpsx_qual_type_t\s+member_qual_type\s*;/.test(
      memberResolutionHeader,
    ) ||
    !memberQualTypeCore ||
    !memberAccessResolutionType ||
    /\bpsx_type_t\b/.test(memberAccessResolutionType[1]) ||
    /\bnode_t\b|\bps_node_|\bPSX_HIR_/.test(memberQualTypeCore[0]) ||
    /parser\/ast\.h|member_access_resolution\.h/.test(
      `${memberResolutionHeader}\n${memberResolutionSource}`,
    ) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      memberQualTypeCore[0],
    ) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      memberQualTypeCore[0],
    ) ||
    /\bpsx_type_compatibility_canonical_view_for\s*\(/.test(
      memberQualTypeCore[0],
    ) ||
    !/\bpsx_semantic_type_table_record_member\s*\(/.test(
      memberQualTypeCore[0],
    ) ||
    /\bpsx_record_layout_(?:table_lookup|member)\s*\(/.test(
      memberQualTypeCore[0],
    ) ||
    !/object_qual_type\.qualifiers/.test(
      memberResolutionSource,
    ) ||
    /\bps_type_find_aggregate_object_type\s*\(/.test(
      memberResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      memberTypeIdentitySource.match(
        /psx_qual_type_t\s+psx_semantic_type_table_aggregate_object\s*\([^]*?\n\}/,
      )?.[0] || "",
    )) {
  throw new Error(
    "member access owner qualifiers must be resolved through TypeId QualType relations",
  );
}
const typeNameResolutionSource = await readFile(
  "src/semantic/type_name_resolution.c",
  "utf8",
);
const typeNameResolutionHeader = await readFile(
  "src/semantic/type_name_resolution.h",
  "utf8",
);
const typeQuerySemanticsHeader = await readFile(
  "src/semantic/type_query_semantics.h",
  "utf8",
);
const typeQuerySemanticsSource = await readFile(
  "src/semantic/type_query_semantics.c",
  "utf8",
);
const compoundLiteralSemanticsHeader = await readFile(
  "src/semantic/compound_literal_semantics.h",
  "utf8",
);
const compoundLiteralSemanticsSource = await readFile(
  "src/semantic/compound_literal_semantics.c",
  "utf8",
);
if (/\bnode_t\b|\bnode_[A-Za-z0-9_]+_t\b|\bps_node_|\bND_[A-Z0-9_]+\b|parser\/ast\.h/.test(
      `${typeQuerySemanticsHeader}\n${typeQuerySemanticsSource}`,
    ) ||
    /\bps_ctx_type_(?:size|align)of_in\s*\(/.test(
      typeQuerySemanticsSource,
    ) ||
    !/\bpsx_qual_type_layout_sizeof\s*\(/.test(
      typeQuerySemanticsSource,
    ) ||
    !/\bpsx_qual_type_layout_alignof\s*\(/.test(
      typeQuerySemanticsSource,
    ) ||
    !/psx_type_query_plan_kind_t/.test(typeQuerySemanticsHeader) ||
    !/PSX_TYPE_QUERY_PLAN_RUNTIME_PRODUCT/.test(
      typeQuerySemanticsHeader,
    ) ||
    !/PSX_TYPE_QUERY_PLAN_RUNTIME_SLOT/.test(
      typeQuerySemanticsHeader,
    ) ||
    !/psx_resolve_sizeof_qual_type_plan_in\s*\(/.test(
      typeQuerySemanticsSource,
    ) ||
    !/psx_resolve_alignof_qual_type_plan_in\s*\(/.test(
      typeQuerySemanticsSource,
    )) {
  throw new Error(
    "type query semantics must resolve canonical QualType plans without Syntax AST dependencies",
  );
}
if (/\bnode_t\b|\bnode_[A-Za-z0-9_]+_t\b|\bps_node_|\bND_[A-Z0-9_]+\b|\bpsx_type_t\b|parser\/(?:ast|type)\.h|resolution_state/.test(
      `${compoundLiteralSemanticsHeader}\n${compoundLiteralSemanticsSource}`,
    ) ||
    !/psx_compound_literal_plan_t/.test(
      compoundLiteralSemanticsHeader,
    ) ||
    !/psx_resolve_compound_literal_qual_type_plan_in\s*\(/.test(
      compoundLiteralSemanticsSource,
    ) ||
    !/psx_semantic_type_table_describe\s*\(/.test(
      compoundLiteralSemanticsSource,
    ) ||
    !/psx_semantic_type_table_contains_vla_array\s*\(/.test(
      compoundLiteralSemanticsSource,
    ) ||
    !/PSX_COMPOUND_LITERAL_STORAGE_AUTOMATIC/.test(
      compoundLiteralSemanticsHeader,
    ) ||
    !/PSX_COMPOUND_LITERAL_STORAGE_STATIC/.test(
      compoundLiteralSemanticsHeader,
    ) ||
    !/psx_compound_literal_storage_duration_in_scope_graph\s*\(/.test(
      compoundLiteralSemanticsSource,
    ) ||
    !/psx_scope_graph_nearest_scope_of_kind\s*\([^]*?PSX_SCOPE_FUNCTION/.test(
      compoundLiteralSemanticsSource,
    ) ||
    !/inside_function_body\s*\|\|[^]*?function_scope\s*!=\s*PSX_SCOPE_ID_INVALID/.test(
      compoundLiteralSemanticsSource,
    ) ||
    /has_file_scope_storage|requires_address_result|result_qual_type|yields_address/.test(
      `${compoundLiteralSemanticsHeader}\n${compoundLiteralSemanticsSource}`,
    )) {
  throw new Error(
    "compound literal semantics must resolve storage duration and canonical object QualType without Syntax AST dependencies",
  );
}
const declarationResolutionSource = await readFile(
  "src/semantic/declaration_resolution.c",
  "utf8",
);
if (/\bpsx_type_t\b|\bps_type_(?:new|clone|apply|add|set|is_tag|record_id|character_code_unit_width)\b|type_builder\.h/.test(
      declarationResolutionSource,
    ) ||
    /\bps_type_sizeof\s*\(/.test(declarationResolutionSource) ||
    /->\s*aggregate_definition\b/.test(declarationResolutionSource) ||
    /\bmember->offset\b/.test(declarationResolutionSource) ||
    !/\bps_ctx_resolve_tag_record_id_in\s*\(/.test(
      declarationResolutionSource,
    ) ||
    !/\bps_ctx_intern_record_qual_type_in\s*\(/.test(
      declarationResolutionSource,
    ) ||
    !/\bps_ctx_tag_qual_type_at_in\s*\(/.test(
      declarationResolutionSource,
    ) ||
    !/\bpsx_type_layout_character_code_unit_width\s*\(/.test(
      declarationResolutionSource,
    ) ||
    /\btype->(?:record_id|is_plain_char|floating_kind)\s*=/.test(
      declarationResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_record_member\s*\(/.test(
      declarationResolutionSource,
    ) ||
    !/\bpsx_record_layout_member\s*\(/.test(
      declarationResolutionSource,
    )) {
  throw new Error(
    "declaration resolution must use canonical type identity and explicit record layout",
  );
}
const genericSelectionResolutionSource = await readFile(
  "src/semantic/generic_selection_resolution.c",
  "utf8",
);
const staticInitializerResolutionSource = await readFile(
  "src/semantic/static_initializer_resolution.c",
  "utf8",
);
const staticInitializerResolutionHeader = await readFile(
  "src/semantic/static_initializer_resolution.h",
  "utf8",
);
const staticInitializerClassificationHeader = await readFile(
  "src/semantic/static_initializer_classification.h",
  "utf8",
);
const frontendSemanticPipelineSource = await readFile(
  "src/frontend/semantic_pipeline.c",
  "utf8",
);
const semanticTraversalCallers = [
  frontendSemanticPipelineSource,
  semanticTreeResolutionSource,
  declarationApplicationSource,
  await readFile("src/semantic/declaration_registration.c", "utf8"),
].join("\n");
const contextFreeSemanticTraversalCall =
  /\bpsx_semantic_resolve_(?:tree|initializer_tree)\s*\(/;
if (contextFreeSemanticTraversalCall.test(semanticTraversalCallers) ||
    !/ps_ctx_find_typedef_(?:decl_type|name)_at_in\s*\(/.test(
      typeNameResolutionSource,
    ) ||
    !/ps_ctx_tag_qual_type_at_in\s*\(/.test(
      typeNameResolutionSource,
    ) ||
    !/psx_semantic_type_table_describe\s*\(/.test(
      staticInitializerResolutionSource,
    )) {
  throw new Error(
    "semantic traversal and delayed type queries must use the passed semantic context",
  );
}
const enumConstantResolutionSource = await readFile(
  "src/semantic/enum_constant_resolution.c",
  "utf8",
);
const enumConstantResolutionHeader = await readFile(
  "src/semantic/enum_constant_resolution.h",
  "utf8",
);
const typedefDeclarationResolutionSource = await readFile(
  "src/semantic/typedef_declaration_resolution.c",
  "utf8",
);
const typedefDeclarationResolutionHeader = await readFile(
  "src/semantic/typedef_declaration_resolution.h",
  "utf8",
);
const ordinarySemanticContextHeaderSource = await readFile(
  "src/parser/semantic_ctx.h",
  "utf8",
);
const functionPublicHeaderSource = await readFile(
  "src/parser/function_public.h",
  "utf8",
);
const ordinaryNodeUtilsSource = await readFile(
  "src/parser/node_utils.c",
  "utf8",
);
const obsoleteOrdinaryNameApi =
  /\b(?:psx_resolve_global_object_symbol_in|ps_ctx_has_function_name_in|ps_ctx_has_typedef_in_current_scope_in|ps_ctx_has_enum_const_in_current_scope_in|ps_find_global_var_in|ps_decl_find_lvar_in|ps_local_registry_find_visible_in)\s*\(/;
if (obsoleteOrdinaryNameApi.test([
      identifierResolutionHeader,
      identifierResolutionSource,
      semanticContextOwnershipSource,
      ordinarySemanticContextHeaderSource,
      functionPublicHeaderSource,
      ordinaryNodeUtilsSource,
      globalRegistrySource,
      globalRegistryHeader,
      localRegistrySource,
      localRegistryHeader,
    ].join("\n")) ||
    !/ps_ctx_find_function_symbol_in\s*\(/.test(ordinaryNodeUtilsSource)) {
  throw new Error(
    "obsolete registry-specific ordinary-name APIs must not survive the scope graph migration",
  );
}
const globalDeclarationResolutionSource = await readFile(
  "src/semantic/global_declaration_resolution.c",
  "utf8",
);
const globalDeclarationResolutionHeader = await readFile(
  "src/semantic/global_declaration_resolution.h",
  "utf8",
);
if (!/\bps_ctx_get_record_decl_in\s*\(/.test(
      globalDeclarationResolutionSource,
    ) ||
    !/record->is_complete/.test(globalDeclarationResolutionSource) ||
    /\bps_type_sizeof\s*\(/.test(globalDeclarationResolutionSource)) {
  throw new Error(
    "global object completeness must come from recursive type meaning and RecordDecl state",
  );
}
const declarationRegistrationSource = await readFile(
  "src/semantic/declaration_registration.c",
  "utf8",
);
const declarationRegistrationHeader = await readFile(
  "src/semantic/declaration_registration.h",
  "utf8",
);
if (/\bps_(?:ctx_active|global_registry_active|local_registry_active)\s*\(/.test(
      declarationRegistrationSource,
    )) {
  throw new Error(
    "declaration registration must not fall back to active contexts",
  );
}
if (!/psx_apply_parsed_typedef_declaration_in\s*\(/.test(
      declarationRegistrationSource,
    ) ||
    !/psx_apply_parsed_enum_constant_in\s*\(/.test(
      declarationRegistrationSource,
    ) ||
    !/psx_apply_parsed_tag_declaration_in\s*\(/.test(
      declarationRegistrationSource,
    ) ||
    /psx_apply_parsed_(?:typedef_declaration|enum_constant|tag_declaration)_in_contexts\s*\(/.test(
      declarationRegistrationSource + declarationRegistrationHeader,
    ) ||
    /psx_apply_parsed_(?:typedef_declaration|enum_constant|tag_declaration)_in\s*\([^)]*psx_(?:global|local)_registry_t/.test(
      declarationRegistrationSource + declarationRegistrationHeader,
    )) {
  throw new Error(
    "semantic namespace registration adapters must use semantic context only",
  );
}
const ordinaryNamespaceResolutionSources = [
  identifierResolutionSource,
  enumConstantResolutionSource,
  typedefDeclarationResolutionSource,
  globalDeclarationResolutionSource,
].join("\n");
const ordinaryDeclarationConflictSources = [
  enumConstantResolutionSource,
  typedefDeclarationResolutionSource,
];
const strictResolverRequestSources = [
  [identifierResolutionSource, ["semantic_context"]],
  [enumConstantResolutionSource, ["semantic_context"]],
  [typedefDeclarationResolutionSource, ["semantic_context"]],
  [functionDeclarationResolutionSource, ["semantic_context"]],
  [globalDeclarationResolutionSource, ["semantic_context"]],
  [tagDeclarationResolutionSource, ["semantic_context"]],
  [declarationResolutionSource, ["semantic_context"]],
  [aggregateMemberResolutionSource, ["semantic_context"]],
  [staticInitializerResolutionSource, ["semantic_context"]],
];
for (const [source, fields] of strictResolverRequestSources) {
  for (const field of fields) {
    if (!new RegExp(`!request->${field}\\b`).test(source)) {
      throw new Error(
        `semantic resolver request must require an explicit ${field}`,
      );
    }
  }
}
const namespaceDeclarationRequestStructs = [
  [enumConstantResolutionHeader, "enum_constant"],
  [typedefDeclarationResolutionHeader, "typedef_declaration"],
  [tagDeclarationResolutionHeader, "tag_declaration"],
  [functionDeclarationResolutionHeader, "function_declaration"],
  [globalDeclarationResolutionHeader, "global_declaration"],
];
for (const [header, name] of namespaceDeclarationRequestStructs) {
  const requestStruct = header.match(
    new RegExp(`typedef\\s+struct\\s*\\{([^]*?)\\}\\s*psx_${name}_resolution_request_t\\s*;`),
  );
  if (!requestStruct ||
      /\b(?:global_registry|local_registry)\b/.test(requestStruct[1])) {
    throw new Error(
      "semantic namespace resolver requests must be owned by semantic context",
    );
  }
}
const implicitResolverContextFallback =
  /request->(?:semantic_context|global_registry|local_registry)\s*\?[\s\S]{0,120}:\s*ps_(?:ctx_active|global_registry_active|local_registry_active)\s*\(\)/;
if (strictResolverRequestSources.some(([source]) =>
      implicitResolverContextFallback.test(source))) {
  throw new Error(
    "semantic resolver request APIs must not fall back to active contexts",
  );
}
const contextFreeOrdinaryNamespaceCall =
  /\bps_ctx_(?:has_function_name|find_typedef_name|find_enum_const|find_enum_const_at|has_typedef_in_current_scope|has_enum_const_in_current_scope|register_typedef_name|register_enum_const|current_tag_scope_depth)\s*\(/;
if (contextFreeOrdinaryNamespaceCall.test(
      ordinaryNamespaceResolutionSources,
    ) ||
    ordinaryDeclarationConflictSources.some((source) =>
      !/psx_scope_graph_lookup_declaration_in_scope\s*\(/.test(source) ||
      /\b(?:ps_find_global_var_in|ps_ctx_has_function_name_in|ps_decl_find_lvar_in|ps_ctx_has_typedef_in_current_scope_in|ps_ctx_has_enum_const_in_current_scope_in)\s*\(/.test(
        source,
      )
    ) ||
    !/ps_ctx_register_enum_const_in\s*\(/.test(
      enumConstantResolutionSource,
    ) ||
    !/ps_ctx_register_typedef_name_in\s*\(/.test(
      typedefDeclarationResolutionSource,
    ) ||
    !/psx_scope_graph_lookup\s*\([^]*?PSX_NAMESPACE_ORDINARY/.test(
      identifierResolutionSource,
    ) ||
    !/ps_ctx_enum_const_value_by_declaration_id_in\s*\(/.test(
      identifierResolutionSource,
    ) ||
    !/psx_apply_parsed_typedef_declaration_in\s*\(/.test(
      frontendDeclarationSources,
    ) ||
    !/\.semantic_context\s*=\s*semantic_context/.test(
      declarationRegistrationSource,
    )) {
  throw new Error(
    "ordinary namespace resolution must use the passed semantic context",
  );
}
const globalObjectLoweringSource = await readFile(
  "src/lowering/global_object_lowering.c",
  "utf8",
);
if (!/psx_scope_graph_lookup_declaration_in_scope\s*\(/.test(
      globalDeclarationResolutionSource,
    ) ||
    !/psx_scope_graph_lookup_declaration_in_scope\s*\(/.test(
      functionDeclarationResolutionSource,
    ) ||
    /\b(?:ps_find_global_var_in|ps_ctx_has_function_name_in|ps_ctx_find_typedef_name_in|ps_ctx_find_enum_const_in)\s*\(/.test(
      globalDeclarationResolutionSource,
    ) ||
    /\bps_find_global_var_in\s*\(/.test(
      functionDeclarationResolutionSource,
    ) ||
    /\b(?:request->global_registry|ps_global_registry_scope_graph)\b/.test(
      globalDeclarationResolutionSource + functionDeclarationResolutionSource,
    ) ||
    !/ps_register_global_var_in\s*\(/.test(
      globalObjectLoweringSource,
    ) ||
    /\bps_register_global_var\s*\(/.test(globalObjectLoweringSource) ||
    /\bps_global_registry_active\s*\(/.test(globalObjectLoweringSource) ||
    !/lower_resolved_global_object_declaration\s*\(/.test(
      globalObjectLoweringSource,
    ) ||
    !/!request->global_registry\b/.test(globalObjectLoweringSource) ||
    !/request->resolution->status\s*!=\s*PSX_GLOBAL_DECLARATION_OK/.test(
      globalObjectLoweringSource,
    ) ||
    /\b(?:semantic_context|psx_resolve_global_declaration)\b/.test(
      globalObjectLoweringSource,
    )) {
  throw new Error(
    "global declarations must classify names through the scope graph and lowering must use explicit registries",
  );
}
const parserSource = await readFile("src/parser/parser.c", "utf8");
const statementParserSource = await readFile("src/parser/stmt.c", "utf8");
const expressionParserSource = await readFile("src/parser/expr.c", "utf8");
const initializerSyntaxSource = await readFile(
  "src/parser/initializer_syntax.c",
  "utf8",
);
const initializerSyntaxHeader = await readFile(
  "src/parser/initializer_syntax.h",
  "utf8",
);
if (/\bpsx_(?:semantic_context|global_registry|local_registry)_t\s*\*/.test(
      `${initializerSyntaxHeader}\n${initializerSyntaxSource}`,
    ) ||
    /#include\s+"(?:semantic_ctx|global_registry|local_registry)\.h"/.test(
      initializerSyntaxSource,
    ) ||
    !/const\s+node_t\s*\*value\s*;/.test(initializerSyntaxHeader) ||
    !/psx_initializer_syntax_context_t/.test(initializerSyntaxHeader) ||
    !/parse_assignment_expression/.test(initializerSyntaxHeader)) {
  throw new Error(
    "initializer syntax parsing must depend only on parser runtime and explicit syntax-expression services",
  );
}
if (!/\bpsx_initializer_designator_t\s*\*\s*designators\s*;/.test(
      astHeader,
    ) ||
    !/\bint\s+designator_count\s*;/.test(astHeader) ||
    !/grow_initializer_syntax_array\s*\([^]*?sizeof\(\*designators\)/.test(
      initializerSyntaxSource,
    ) ||
    /designators\s*\[\s*8\s*\]|designator_count\s*>=\s*8/.test(
      `${astHeader}\n${initializerSyntaxSource}`,
    )) {
  throw new Error(
    "standard C initializer designator paths must use parser-arena dynamic storage rather than an eight-component cap",
  );
}
const localDeclarationSyntaxSource = await readFile(
  "src/parser/local_declaration_syntax.c",
  "utf8",
);
const enumConstSource = await readFile("src/parser/enum_const.c", "utf8");
const enumConstHeader = await readFile("src/parser/enum_const.h", "utf8");
const enumBodySyntaxBoundary = enumConstSource.match(
  /void\s+psx_parse_enum_body_syntax\s*\([^]*?\n\}/,
)?.[0] ?? "";
if (!/node_t\s*\*initializer\s*;/.test(enumConstHeader) ||
    /psx_parsed_enum_expr_t|PSX_ENUM_EXPR_/.test(
      `${enumConstHeader}\n${enumBodySyntaxBoundary}`,
    ) ||
    /psx_semantic_context_t|ps_ctx_|psx_(?:parse|eval)_.*const_expr/.test(
      `${enumConstHeader}\n${enumConstSource}`,
    ) ||
    !/parse_assignment_expression\s*\(/.test(enumBodySyntaxBoundary) ||
    /psx_semantic_context_t|ps_ctx_|psx_resolve_/.test(
      enumBodySyntaxBoundary,
    ) ||
    !/psx_resolve_enum_initializer_syntax_in_contexts\s*\([^]*?psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts\s*\(/.test(
      enumConstantResolutionSource,
    )) {
  throw new Error(
    "enum initializers must remain immutable Syntax AST until direct semantic resolution",
  );
}
const functionParameterResolutionSource = await readFile(
  "src/semantic/function_parameter_resolution.c",
  "utf8",
);
const lifecycleDeclarationPipelineSource = await readFile(
  "src/declaration_pipeline.c",
  "utf8",
);
const explicitLifecycleCallers = [
  compilerMainSource,
  frontendTranslationUnitSource,
  frontendFunctionDefinitionSource,
  parserSource,
  statementParserSource,
  declarationApplicationSource,
  functionParameterResolutionSource,
  lifecycleDeclarationPipelineSource,
].join("\n");
const contextFreeLifecycleCall =
  /\bps_ctx_(?:reset_translation_unit_scope|reset_function_diag_state|reset_tag_diag_state|reset_function_scope|enter_block_scope|leave_block_scope|record_unsupported_gnu_extension|emit_deferred_parser_diagnostics|promote_tag_to_file_scope)\s*\(/;
const contextFreeJumpRegistryCall =
  /\bpsx_ctx_(?:register_goto_ref|register_label_def|validate_goto_refs)\s*\(/;
const implicitActiveContextFallback =
  /semantic_context\s*\?\s*semantic_context\s*:\s*ps_ctx_active\s*\(\)|if\s*\(\s*!semantic_context\s*\)\s*semantic_context\s*=\s*ps_ctx_active\s*\(\)/;
const explicitParserContextSources = [
  expressionParserSource,
  expressionSyntaxAdapterSource,
  statementParserSource,
  initializerSyntaxSource,
  enumConstSource,
  declarationResolutionSource,
  typeNameResolutionSource,
  declarationApplicationSource,
].join("\n");
if (contextFreeLifecycleCall.test(explicitLifecycleCallers) ||
    contextFreeJumpRegistryCall.test(
      `${parserSource}\n${statementParserSource}`,
    ) ||
    implicitActiveContextFallback.test(explicitParserContextSources) ||
    /stream\s*&&\s*stream->semantic_context\s*\?[^;]*ps_ctx_active\s*\(\)/s.test(
      parserSource,
    ) ||
    !/ps_parser_stream_begin_with_syntax\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    /psx_stmt_stmt_in_contexts\s*\(/.test(parserSource) ||
    !/psx_stmt_stmt_syntax\s*\(\s*statement_syntax\s*\)/.test(
      parserSource,
    ) ||
    !/ps_name_classifier_is_typedef_name\s*\(/.test(
      statementParserSource,
    ) ||
    /active_local_declarations/.test(statementParserSource) ||
    /psx_expr_expr_in_contexts\s*\(/.test(statementParserSource) ||
    !/syntax\.parse_expression/.test(statementParserSource) ||
    !/psx_statement_syntax_adapter_init\s*\(/.test(
      expressionSyntaxAdapterSource,
    ) ||
    /psx_expr_(?:expr|assign|conditional)_in_contexts\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    /psx_legacy_statement_syntax_adapter_init\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    !/psx_frontend_init_local_declaration_syntax_adapter\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    !/psx_parse_initializer_syntax_list_with_context\s*\(/.test(
      expressionSyntaxAdapterSource,
    ) ||
    /ps_ctx_record_unsupported_gnu_extension_in\s*\(/.test(
      initializerSyntaxSource,
    ) ||
    !/context->diagnose_unsupported_gnu_extension/.test(
      initializerSyntaxSource,
    ) ||
    /psx_parse_declarator_syntax_tree_into_with_typedef_lookup_in_contexts\s*\(/.test(
      localDeclarationSyntaxSource,
    ) ||
    !/callbacks->parse_decl_specifier\s*\(/.test(
      localDeclarationSyntaxSource,
    ) ||
    !/callbacks->parse_declarator\s*\(/.test(
      localDeclarationSyntaxSource,
    ) ||
    !/callbacks->parse_initializer\s*\(/.test(
      localDeclarationSyntaxSource,
    ) ||
    !/psx_try_parse_toplevel_declarator_syntax_tree_with_typedef_lookup_in_contexts\s*\(/.test(
      toplevelDeclarationSyntaxSource,
    ) ||
    !/ag_compilation_session_reset_translation_unit\s*\(/.test(
      compilerMainSource,
    ) ||
    !/ps_ctx_emit_deferred_parser_diagnostics_in\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    !/psx_apply_parsed_function_parameters_in_contexts\s*\(/.test(
      declarationApplicationSource,
    ) ||
    !/ps_ctx_record_unsupported_gnu_extension_in\s*\(/.test(
      functionParameterResolutionSource,
    ) ||
    !/psx_apply_parsed_declarator_qual_type_in_contexts\s*\(/.test(
      typeNameResolutionSource,
    )) {
  throw new Error(
    "semantic lifecycle and prototype type resolution must use the passed context",
  );
}

for (const file of await sourceFilesUnder("src")) {
  const source = await readFile(file, "utf8");
  if (/#[ \t]*include[^\n]*parser_public\.h/.test(source)) {
    throw new Error(`${file} must include narrow parser API headers`);
  }
}

const actual = new Map();
const contextBridgeRe =
  /\b(?:psx?_ctx_[A-Za-z0-9_]+|ir_abi_classify_function_param)\b/g;
const irSymbolTypeRe = /\bps_(?:lvar|gvar)_get_decl_type\b/g;
const parserLiteralRegistryRe =
  /\b(?:ps_iter_string_literals|ps_iter_float_literals|ps_string_lit_view|ps_float_lit_view|string_lit_t|float_lit_t)\b/g;
for (const file of backendFiles) {
  const source = await readFile(file, "utf8");
  if (/#[ \t]*include[^\n]*parser\/semantic_ctx\.h/.test(source)) {
    throw new Error(`${file} directly includes parser/semantic_ctx.h`);
  }
  const parserDependencySource = file.startsWith("src/ir/")
    ? source.replace(/\bpsx_(?:type_id|qual_type)_t\b/g, "")
    : source;
  if ((file.startsWith("src/arch/") || file.startsWith("src/ir/")) &&
      (/#[ \t]*include[^\n]*parser\//.test(parserDependencySource) ||
       /\bpsx?_[A-Za-z0-9_]+\b/.test(parserDependencySource) ||
       /\b(?:global_var_t|lvar_t|node_t|tag_member_info_t|string_lit_t|float_lit_t)\b/.test(
         parserDependencySource,
       ))) {
    throw new Error(`${file} IR/backend layer directly depends on parser types or APIs`);
  }
  const forbidden = file.startsWith("src/ir/")
    ? [contextBridgeRe, irSymbolTypeRe]
    : [contextBridgeRe, parserLiteralRegistryRe];
  for (const pattern of forbidden) {
    for (const match of source.matchAll(pattern)) {
      const key = `${file}:${match[0]}`;
      actual.set(key, (actual.get(key) || 0) + 1);
    }
  }
}

if (actual.size) {
  const violations = [...actual.entries()]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([key, count]) => `${key}: ${count}`);
  throw new Error(
    "backend must not read parser context; IR must not recover types from symbols or invoke parser-backed ABI " +
    `classification:\n${violations.join("\n")}`,
  );
}

const wasmFunctionCodegenStart = wasmIrSource.indexOf(
  "static void set_vreg_global_ref",
);
const wasmFunctionCodegenEnd = wasmIrSource.indexOf(
  "static void emit_string_literal_data",
);
if (wasmFunctionCodegenStart < 0 ||
    wasmFunctionCodegenEnd <= wasmFunctionCodegenStart) {
  throw new Error("Wasm function codegen boundary markers are missing");
}
const wasmFunctionCodegen = wasmIrSource.slice(
  wasmFunctionCodegenStart,
  wasmFunctionCodegenEnd,
);
const parserGlobalReads = [
  /\bps_find_global_var\b/g,
  /\bps_iter_globals\b/g,
  /\bps_iter_string_literals\b/g,
  /\bps_gvar_[A-Za-z0-9_]+\b/g,
  /\bglobal_var_t\b/g,
  /\bstring_lit_t\b/g,
];
const wasmFunctionCodegenViolations = [];
for (const pattern of parserGlobalReads) {
  for (const match of wasmFunctionCodegen.matchAll(pattern)) {
    wasmFunctionCodegenViolations.push(match[0]);
  }
}
if (wasmFunctionCodegenViolations.length ||
    /\bir_module_find_symbol\s*\(/.test(wasmFunctionCodegen) ||
    /\bir_symbol_find_func_ref\s*\(/.test(wasmFunctionCodegen) ||
    !/\bwasm32_machine_symbol_find_func_ref\s*\(/.test(
      wasmFunctionCodegen,
    ) ||
    !/i->resolved_symbol/.test(wasmFunctionCodegen)) {
  throw new Error(
    "Wasm function codegen must consume resolved Machine symbols instead of source IR or parser registries" +
      (wasmFunctionCodegenViolations.length
        ? `:\n${wasmFunctionCodegenViolations.sort().join("\n")}`
        : ""),
  );
}

const wasmObjSource = await readFile("src/arch/wasm32/wasm32_obj.c", "utf8");
const wasmObjInternalSource = await readFile(
  "src/arch/wasm32/wasm32_obj_internal.h",
  "utf8",
);
const wasmObjStateSources = `${wasmObjSource}\n${wasmObjInternalSource}`;
if (/^static\s+obj_ctx_t\s+g_obj\s*;/m.test(wasmObjStateSources) ||
    /^static\s+wb_t\s+g_obj_capture\s*;/m.test(wasmObjStateSources) ||
    /\b_Thread_local\b/.test(wasmObjStateSources) ||
    /wasm32_obj_context_(?:activate|active)\s*\(/.test(wasmObjStateSources) ||
    /^static\s+(?:ir_type_t|unsigned char|int)\s*\*?g_emit_local_/m.test(
      wasmObjStateSources,
    ) ||
    !/struct\s+wasm32_obj_context_t\s*\{/.test(wasmObjInternalSource) ||
    !/ag_diagnostic_context_t\s*\*diagnostic_context\s*;/.test(
      wasmObjInternalSource,
    ) ||
    /\bdiag_(?:emit_internalf|message_for)\s*\(/.test(wasmObjSource) ||
    !/wasm32_obj_clear_module\s*\(&g_obj\)/.test(wasmObjSource)) {
  throw new Error(
    "Wasm object module, capture, and emit state must be context-owned and reset with cleanup",
  );
}
const wasmObjFunctionCodegenStart = wasmObjSource.indexOf(
  "static void gen_func_body",
);
const wasmObjFunctionCodegenEnd = wasmObjSource.indexOf(
  "static void emit_obj_string_literal",
);
if (wasmObjFunctionCodegenStart < 0 ||
    wasmObjFunctionCodegenEnd <= wasmObjFunctionCodegenStart) {
  throw new Error("Wasm object function codegen boundary markers are missing");
}
const wasmObjFunctionCodegen = wasmObjSource.slice(
  wasmObjFunctionCodegenStart,
  wasmObjFunctionCodegenEnd,
);
const wasmObjParserReads = [
  /\bpsx?_[A-Za-z0-9_]+\b/g,
  /\bglobal_var_t\b/g,
  /\bstring_lit_t\b/g,
  /\btag_member_info_t\b/g,
];
const wasmObjFunctionCodegenViolations = [];
for (const pattern of wasmObjParserReads) {
  for (const match of wasmObjFunctionCodegen.matchAll(pattern)) {
    wasmObjFunctionCodegenViolations.push(match[0]);
  }
}
if (wasmObjFunctionCodegenViolations.length ||
    !/\bdata_for_machine_inst\s*\(\s*context\s*,\s*i\s*,/.test(
      wasmObjFunctionCodegen,
    ) ||
    /\bir_module_find_symbol\s*\(/.test(wasmObjSource) ||
    !/static obj_data_t \*data_for_machine_inst[\s\S]*?inst->resolved_symbol/.test(
      wasmObjSource,
    )) {
  throw new Error(
    "Wasm object function codegen must consume resolved Machine symbols instead of source IR or parser registries" +
      (wasmObjFunctionCodegenViolations.length
        ? `:\n${wasmObjFunctionCodegenViolations.sort().join("\n")}`
        : ""),
  );
}

const irHeaderSource = await readFile("src/ir/ir.h", "utf8");
const irDataHeaderSource = await readFile("src/ir/ir_data.h", "utf8");
const integerConstantEvaluationSource = await readFile(
  "src/semantic/integer_constant_evaluation.c",
  "utf8",
);
const irSymbolLoweringSource = await readFile(
  "src/lowering/ir_symbol_lowering.c",
  "utf8",
);
const abiLoweringSource = await readFile(
  "src/lowering/abi_lowering.c",
  "utf8",
);
const abiLoweringHeader = await readFile(
  "src/lowering/abi_lowering.h",
  "utf8",
);
const explicitIrLayoutCorpus =
  `${irHeaderSource}\n${irAllocationSource}\n${irOptimizationSource}\n` +
  `${abiLoweringSource}\n${hirIrBuilder}\n${arm64IrEmitSource}\n` +
  compilerMainSource;
if (!/ir_type_fixed_size\s*\(\s*ir_type_t\s+type\s*\)/.test(
      irAllocationSource,
    ) ||
    !/ir_type_size_for_layout\s*\([^]*?const\s+ag_data_layout_t\s*\*data_layout/.test(
      irAllocationSource,
    ) ||
    /case\s+IR_TY_PTR\s*:\s*return\s+\d+/.test(irAllocationSource) ||
    /\bir_type_size\s*\(/.test(explicitIrLayoutCorpus) ||
    !/ir_opt_const_fold\s*\([^]*?const\s+ag_data_layout_t\s*\*data_layout/.test(
      irOptimizationSource,
    ) ||
    (compilerMainSource.match(
      /\bir_opt_const_fold\s*\([^;]*ag_target_info_data_layout\s*\(/g,
    ) ?? []).length !== 2 ||
    !/gen_ir_module_in\s*\([^;]*const\s+ag_data_layout_t\s*\*data_layout/.test(
      arm64IrEmitSource,
    )) {
  throw new Error(
    "MIR pointer width and optimization/codegen size queries must receive the selected DataLayout explicitly",
  );
}
const abiTargetPolicySource = await readFile(
  "src/lowering/abi_target_policy.c",
  "utf8",
);
const abiTargetPolicyHeader = await readFile(
  "src/lowering/abi_target_policy.h",
  "utf8",
);
const abiTargetPolicyInternalHeader = await readFile(
  "src/lowering/abi_target_policy_internal.h",
  "utf8",
);
const arm64AppleAbiPolicySource = await readFile(
  "src/arch/arm64_apple/arm64_apple_abi_policy.c",
  "utf8",
);
const wasm32AbiPolicySource = await readFile(
  "src/arch/wasm32/wasm32_abi_policy.c",
  "utf8",
);
const wasmMachineFunctionPlanSource = await readFile(
  "src/arch/wasm32/wasm32_machine_function.c",
  "utf8",
);
const wasmMachineModulePlanSource = await readFile(
  "src/arch/wasm32/wasm32_machine_module.c",
  "utf8",
);
if (!/typedef struct ir_symbol_t\s*\{/.test(irHeaderSource) ||
    !/\bir_symbol_t\s*\*symbols\s*;/.test(irHeaderSource) ||
    !/\blower_ir_global_symbol\s*\(/.test(irSymbolLoweringSource) ||
    !/\bobject_size\b/.test(hirIrBuilder)) {
  throw new Error(
    "IR lowering must materialize global layout and string size before backend codegen",
  );
}
if (/\bps_find_global_var\s*\(/.test(irSymbolLoweringSource)) {
  throw new Error(
    "Typed HIR global references must retain symbol identity without active-registry lookup",
  );
}
if (/\b(?:semantic_context|ps_ctx_|ps_gvar_symbol_ref_named_function_in)\b/.test(
      irSymbolLoweringSource,
    ) ||
    /abi_lowering\.h/.test(irSymbolLoweringSource) ||
    !/ps_gvar_walk_resolved_aggregate_initializer\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    /\bir_abi_(?:classify|callable)[A-Za-z0-9_]*\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    !/ir_function_type_from_type_id\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    !/ir_symbol_add_func_ref\s*\([^]*?&function_type\s*\)/.test(
      irSymbolLoweringSource,
    ) ||
    /\bps_type_sizeof\s*\(/.test(abiLoweringSource) ||
    !/\bpsx_type_layout_sizeof\s*\(/.test(abiLoweringSource)) {
  throw new Error(
    "generic IR must retain resolved function TypeId while ABI lowering owns target classification",
  );
}
if (/abi_lowering\.h|\bir_abi_(?:classify|lower)_[A-Za-z0-9_]*\s*\(/.test(
      hirIrBuilder,
    ) ||
    !/mir_type_lowering\.h/.test(hirIrBuilder) ||
    !/\bir_mir_classify_type_id\s*\(/.test(hirIrBuilder)) {
  throw new Error(
    "HIR to MIR lowering must use logical MIR value types and leave ABI classification to the post-MIR pass",
  );
}

const forbiddenGenericIrAbiMetadata = [
  "ir_callable_sig_t",
  "callable_sig",
  "has_callable_sig",
  "ret_struct_size",
  "ret_complex_half",
  "arg_abi_types",
  "param_abi_types",
  "nargs_fixed",
  "is_variadic_call",
];
for (const name of forbiddenGenericIrAbiMetadata) {
  const pattern = new RegExp(`\\b${name}\\b`);
  if (pattern.test(irHeaderSource) || pattern.test(irDataHeaderSource)) {
    throw new Error(`generic IR must not own target ABI metadata: ${name}`);
  }
}
if (/\bx8\b|\bret_type\b|\bTLV\b|\bGOT\b|\bis_got_funcref\b|IR_LOAD_TLV_ADDR/.test(irHeaderSource) ||
    !/\bIR_LOAD_TLS_SYM\b/.test(irHeaderSource) ||
    !/\bis_external_symbol\b/.test(irHeaderSource) ||
    /\bIR_(?:PARAM|RESULT_AREA)\b/.test(irHeaderSource) ||
    /\bresult_area_vreg\b/.test(abiLoweringHeader) ||
    !/\bir_abi_reference_signature\s*\(/.test(
      wasmMachineFunctionPlanSource,
    ) ||
    !/\bir_abi_data_relocation_signature\s*\(/.test(
      wasmMachineModulePlanSource,
    ) ||
    /\bir_abi_data_relocation_signature\s*\(/.test(wasmIrSource) ||
    /\bir_abi_data_relocation_signature\s*\(/.test(wasmObjSource)) {
  throw new Error(
    "aggregate result and function-reference ABI must be represented by target-neutral MIR plus sidecars",
  );
}
if (/(?:unsigned\s+char|int)\s+(?:result_is_indirect|result_complex_half|result_size)\s*;/.test(
      abiLoweringHeader,
    ) ||
    /\bir_abi_(?:param_info_t|classify_type_id)\b/.test(
      abiLoweringHeader,
    ) ||
    !/ir_abi_piece_t\s*\*result_pieces\s*;/.test(abiLoweringHeader) ||
    !/size_t\s+result_count\s*;/.test(abiLoweringHeader) ||
    !/\blower_result_pieces\s*\(/.test(abiLoweringSource) ||
    !/\bir_abi_target_policy_for\s*\(/.test(abiLoweringSource) ||
    /\bag_target_info_call_abi\s*\(/.test(abiLoweringSource) ||
    !/\bag_target_info_call_abi\s*\(/.test(abiTargetPolicySource) ||
    /size_t\s+complex_result_piece_count\s*;/.test(
      abiTargetPolicyHeader,
    ) ||
    !/size_t\s+complex_result_piece_count\s*;/.test(
      abiTargetPolicyInternalHeader,
    ) ||
    !/int\s+parameter_aggregate_direct_size_limit\s*;/.test(
      abiTargetPolicyInternalHeader,
    ) ||
    !/\bir_abi_policy_parameter_aggregate_is_indirect\s*\(/.test(
      abiTargetPolicySource,
    ) ||
    !/\bir_abi_policy_direct_aggregate_type\s*\(/.test(
      abiLoweringSource,
    ) ||
    !/\bir_abi_policy_variadic_aggregate_piece_count\s*\(/.test(
      abiLoweringSource,
    ) ||
    !/\bir_abi_policy_variadic_aggregate_piece\s*\(/.test(
      abiLoweringSource,
    ) ||
    /source_size\s*\+\s*7|piece_index\s*\*\s*8/.test(
      abiLoweringSource,
    ) ||
    !/\.complex_result_piece_count\s*=\s*2/.test(
      arm64AppleAbiPolicySource,
    ) ||
    !/\.complex_result_piece_count\s*=\s*1/.test(
      wasm32AbiPolicySource,
    ) ||
    !/\.parameter_aggregate_direct_size_limit\s*=\s*16/.test(
      arm64AppleAbiPolicySource,
    ) ||
    !/\.parameter_aggregate_direct_size_limit\s*=\s*16/.test(
      wasm32AbiPolicySource,
    ) ||
    !/\.variadic_aggregate_piece_size\s*=\s*8/.test(
      arm64AppleAbiPolicySource,
    ) ||
    !/\.variadic_aggregate_piece_size\s*=\s*8/.test(
      wasm32AbiPolicySource,
    ) ||
    /\[[ \t]*(?:16|32)[ \t]*\]/.test(
      `${irHeaderSource}\n${abiLoweringHeader}\n${abiLoweringSource}\n${abiTargetPolicyHeader}\n${abiTargetPolicyInternalHeader}\n${abiTargetPolicySource}\n${arm64AppleAbiPolicySource}\n${wasm32AbiPolicySource}`,
    )) {
  throw new Error(
    "AbiLowering must consume an explicit target ABI policy and own dynamic parameter and result piece sequences without legacy result flags or fixed callable caps",
  );
}

if (/->(?:args|nargs)\b/.test(arm64IrEmitSource) ||
    !/\bir_abi_call_arguments\s*\(/.test(arm64IrEmitSource)) {
  throw new Error(
    "Apple ARM64 backend must consume call argument pieces from AbiLowering sidecars",
  );
}
for (const [name, source] of [
  ["Wasm text", wasmIrSource],
  ["Wasm object", wasmObjSource],
]) {
  if (/->(?:args|nargs)\b/.test(source) ||
      /\bir_abi_call_arguments\s*\(/.test(source) ||
      !/WASM32_MACHINE_INST_CALL/.test(source)) {
    throw new Error(
      `${name} backend must consume preplanned Machine call arguments`,
    );
  }
}
const hirIrCallAbiSource = await readFile(
  "src/lowering/hir_ir_call.c",
  "utf8",
);
const hirIrOrchestratorSource = await readFile(
  "src/lowering/hir_ir_builder.c",
  "utf8",
);
const parameterBindingLowering = hirIrCallAbiSource.match(
  /int hir_ir_setup_parameter_bindings\s*\([^]*?(?=\nstatic psx_qual_type_t)/,
);
if (!parameterBindingLowering ||
    !/ir_inst_new\s*\(\s*IR_PARAM_BIND\s*\)/.test(
      parameterBindingLowering[0],
    ) ||
    /ir_inst_new\s*\(\s*IR_PARAM\s*\)|ag_target_info_call_abi|integer_index|float_index/.test(
      parameterBindingLowering[0],
    ) ||
    /\b(?:static\s+)?int\s+(?:setup_parameter_bindings|hir_ir_setup_parameter_bindings)\s*\([^;]*\)\s*\{/.test(
      hirIrOrchestratorSource,
    )) {
  throw new Error(
    "typed HIR must bind one source parameter to storage without assigning physical ABI indices",
  );
}
if (!/\bir_abi_signature_parameter_pieces\s*\(/.test(
      arm64IrEmitSource,
    )) {
  throw new Error(
    "Apple ARM64 backend must expand logical parameter bindings from AbiLowering sidecars",
  );
}
for (const [name, source] of [
  ["Wasm text", wasmIrSource],
  ["Wasm object", wasmObjSource],
]) {
  if (/\bir_abi_signature_parameter_pieces\s*\(/.test(source) ||
      !/WASM32_MACHINE_INST_PARAMETER_BIND/.test(source)) {
    throw new Error(
      `${name} backend must serialize preplanned Machine parameter bindings`,
    );
  }
}
if (!/ir_opt_const_fold\s*\(\s*m\s*,[^;]*\);[\s\S]*?ir_opt_dce\s*\(m\)[\s\S]*?lower_module_abi\s*\(m/.test(
      compilerMainSource,
    ) ||
    /\bir_opt_(?:const_fold|dce)\s*\(/.test(arm64IrEmitSource) ||
    /\bir_opt_(?:const_fold|dce)\s*\(/.test(wasmIrSource) ||
    /\bir_opt_(?:const_fold|dce)\s*\(/.test(wasmObjSource)) {
  throw new Error(
    "target-independent MIR optimization must finish before AbiLowering and backend emission",
  );
}

const sourceFiles = (await sourceFilesUnder("src")).sort();
const legacyTypeMutationRe =
  /\b(?:psx_ctx_add_tag_member|ps_ctx_typedef_set_decl_type|ps_tag_member_set_decl_type|ps_tag_member_decl_type_mut|tag_member_record_set_decl_type|typedef_record_set_decl_type)\b/g;
const legacyTypeMutations = [];
const legacyFunctionNodeReferences = [];
const legacyRecursiveTypeMetadata = [];
const mutableProcessState = [];
const mutableStaticDataRe =
  /^\s*static\s+(?!const\b|inline\b)(?:[A-Za-z_][A-Za-z0-9_]*\s+)*(?:\*+\s*)?[A-Za-z_][A-Za-z0-9_]*\s*(?:\[[^\n;]*\]\s*)?(?:=|;)/gm;
const mutableStaticConstPointerRe =
  /^\s*static\s+const\s+[^;\n]*\*\s*(?!const\b)[A-Za-z_][A-Za-z0-9_]*\s*(?:\[[^\n;]*\]\s*)?(?:=|;)/gm;
for (const file of sourceFiles) {
  const source = await readFile(file, "utf8");
  for (const match of source.matchAll(legacyTypeMutationRe)) {
    legacyTypeMutations.push(`${file}:${match[0]}`);
  }
  if (/\bnode_func_t\b/.test(source)) {
    legacyFunctionNodeReferences.push(file);
  }
  if (/\b(?:funcptr_ret_|ret_funcptr_|base_funcptr_ret_|ret_pointee_array_|funcptr_nargs_fixed|is_variadic_funcptr)\w*/.test(
        source,
      )) {
    legacyRecursiveTypeMetadata.push(file);
  }
  if (file.endsWith(".c") &&
      (mutableStaticDataRe.test(source) ||
       mutableStaticConstPointerRe.test(source))) {
    mutableProcessState.push(file);
  }
  mutableStaticDataRe.lastIndex = 0;
  mutableStaticConstPointerRe.lastIndex = 0;
}
if (mutableProcessState.length) {
  throw new Error(
    "production mutable static state must be owned by CompilationSession contexts:\n" +
      mutableProcessState.sort().join("\n"),
  );
}
if (legacyTypeMutations.length) {
  throw new Error(
    "canonical typedef and tag-member records must not expose generic type mutation APIs:\n" +
      legacyTypeMutations.sort().join("\n"),
  );
}
if (legacyFunctionNodeReferences.length) {
  throw new Error(
    "function definitions and calls must not share the legacy node_func_t record:\n" +
      legacyFunctionNodeReferences.sort().join("\n"),
  );
}
if (legacyRecursiveTypeMetadata.length) {
  throw new Error(
    "function pointer and array derivations must come from recursive canonical types:\n" +
      legacyRecursiveTypeMetadata.sort().join("\n"),
  );
}

const astSource = await readFile("src/parser/ast.h", "utf8");
const syntaxNodeKindHeader = await readFile(
  "src/parser/syntax_node_kind.h",
  "utf8",
);
const functionCallResolutionHeader = await readFile(
  "src/semantic/function_call_resolution.h",
  "utf8",
);
const functionCallResolutionSource = await readFile(
  "src/semantic/function_call_resolution.c",
  "utf8",
);
const literalResolutionHeader = await readFile(
  "src/semantic/literal_resolution.h",
  "utf8",
);
const literalResolutionSource = await readFile(
  "src/semantic/literal_resolution.c",
  "utf8",
);
const syntaxTypedHirResolutionHeader = await readFile(
  "src/semantic/syntax_typed_hir_resolution.h",
  "utf8",
);
const syntaxTypedHirResolutionSource = await readFile(
  "src/semantic/syntax_typed_hir_resolution.c",
  "utf8",
);
if (/\bpsx_type_t\b|\bps_ctx_type_by_id_in\s*\(|\bps_type_[A-Za-z0-9_]*\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /\(\s*node_t\s*\*\s*\)\s*syntax_initializer/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "Syntax-to-Typed-HIR resolution must follow canonical QualType edges without materializing parser type views",
  );
}
const legacySemanticLabelApi =
  /\b(?:psx_ctx_register_goto_ref_in|psx_ctx_register_label_def_in|psx_ctx_validate_goto_refs_in)\s*\(/;
if (!/psx_scope_graph_declare_synthetic_at\s*\([^]*?PSX_NAMESPACE_LABEL[^]*?PSX_DECL_LABEL/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_scope_graph_lookup_in_scope\s*\([^]*?PSX_NAMESPACE_LABEL/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /\bdirect_label_binding_t\b|context->labels\b/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/forget_direct_label_declarations\s*\([^]*?label_declaration_start[^]*?psx_scope_graph_declare_synthetic_at\s*\([^]*?PSX_NAMESPACE_LABEL/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/label_declaration_start\s*=\s*psx_scope_graph_declaration_count\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /direct_jump_name_equal\s*\(/.test(syntaxTypedHirResolutionSource) ||
    legacySemanticLabelApi.test(semanticContextOwnershipSource) ||
    legacySemanticLabelApi.test(ordinarySemanticContextHeaderSource)) {
  throw new Error(
    "function labels must use the shared scope graph instead of resolver-local or semantic-context symbol tables",
  );
}
const directFunctionRejections = [
  ...typedHirBuildStatusHeader.matchAll(
    /\b(PSX_SYNTAX_TYPED_HIR_REJECTION_[A-Z0-9_]+)\b/g,
  ),
].map((match) => match[1]).filter(
  (name) => name !== "PSX_SYNTAX_TYPED_HIR_REJECTION_NONE",
);
if (directFunctionRejections.length === 0 ||
    directFunctionRejections.some(
      (name) => !syntaxTypedHirResolutionSource.includes(name) ||
                !semanticTreeResolutionSource.includes(name),
    ) ||
    !/resolve_parsed_function_typed_hir_from_syntax_in_contexts\s*\([^]*?diagnose_direct_syntax_rejection\s*\(/.test(
      semanticTreeResolutionSource,
    ) ||
    /psx_legacy_syntax_diagnostics_accept_/.test(
      semanticTreeResolutionSource,
    )) {
  throw new Error(
    "every direct Syntax rejection must diagnose without a mutable compatibility tree",
  );
}
const hirSymbolResolutionSource = await readFile(
  "src/semantic/hir_symbol_resolution.c",
  "utf8",
);
const hirLocalResolutionSource = await readFile(
  "src/semantic/hir_local_resolution.c",
  "utf8",
);
const semanticParserTypeBoundaryViolations = [];
for (const path of allSourceFiles) {
  if (!path.startsWith("src/semantic/")) continue;
  const source = await readFile(path, "utf8");
  if (/\bpsx_type_t\b|parser\/type\.h|type_compatibility_view\.h/.test(
        source,
      )) {
    semanticParserTypeBoundaryViolations.push(path);
  }
}
if (semanticParserTypeBoundaryViolations.length > 0) {
  throw new Error(
    `semantic modules outside the explicit compatibility boundary must use canonical types: ${semanticParserTypeBoundaryViolations.join(", ")}`,
  );
}
const nodeStruct = astSource.match(/struct node_t\s*\{([\s\S]*?)\n\};/);
if (!nodeStruct ||
    !/\bpsx_syntax_node_kind_t\s+kind\s*;/.test(nodeStruct[1]) ||
    /\bpsx_(?:work|resolution)_node_kind_t\s+kind\s*;/.test(
      nodeStruct[1],
    ) ||
    /\bresolution_state\b/.test(nodeStruct[1]) ||
    /\bconst\s+psx_type_t\s*\*\s*type\s*;/.test(nodeStruct[1]) ||
    /\bpsx_qual_type_t\s+qual_type\s*;/.test(nodeStruct[1]) ||
    /\b(?:is_resolution_work_node|has_external_resolution_state)\b/.test(
      nodeStruct[1],
    ) ||
    /\blvar_usage_unevaluated\b/.test(nodeStruct[1]) ||
    /(?:type_system\/type_ids|parser\/type|["<]type\.h[">])/.test(
      astSource,
    ) ||
    /\b(?:unsigned_override|has_unsigned_override)\b/.test(nodeStruct[1])) {
  throw new Error(
    "syntax node_t must be typeless and must not own semantic resolution state",
  );
}
if (/\bis_source_cast\b/.test(astSource) ||
    !/\bND_SOURCE_CAST\b/.test(syntaxNodeKindHeader) ||
    /\bND_CAST\b/.test(syntaxNodeKindHeader) ||
    !/cast->base\.kind\s*=\s*ND_SOURCE_CAST/.test(
      earlyNodeUtilsSource,
    ) ||
    !/static\s+int\s+resolve_direct_source_cast\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/case\s+ND_SOURCE_CAST\s*:/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "source casts must use Syntax ND_SOURCE_CAST and resolve directly into Typed HIR casts",
  );
}
if (/\b(?:unevaluated_operand_depth|in_unevaluated_operand)\b/.test(
      parserExpressionSource,
    ) ||
    !/DIRECT_IDENTIFIER_USAGE_UNEVALUATED/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/preflight_direct_unevaluated_expression\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_LVAR_USAGE_UNEVALUATED/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "unevaluated local-usage state must be derived by semantic binding and stored outside Syntax AST",
  );
}
if (/psx_resolution_node_set_kind\s*\(/.test(
      frontendFunctionDefinitionSource,
    ) ||
    /node->base\.kind\s*=\s*ND_FUNCDEF/.test(
      frontendFunctionDefinitionSource,
    )) {
  throw new Error(
    "Syntax node kinds must stay in Syntax AST and function resolution must not synthesize compatibility nodes",
  );
}
const directResolvedKindFieldAccess =
  /(?:->|\.)kind\s*(?:==|!=|=)\s*ND_(?:FUNCDEF|LVAR|FUNCREF|DEREF|GVAR|VLA_ALLOC|FP_TO_INT|INT_TO_FP|VA_ARG_AREA)\b/;
const directResolvedKindFieldFiles = [];
for (const path of allSourceFiles) {
  if (directResolvedKindFieldAccess.test(await readFile(path, "utf8")))
    directResolvedKindFieldFiles.push(path);
}
if (directResolvedKindFieldFiles.length) {
  throw new Error(
    "resolved node kinds must not be read from or written to Syntax node_t.kind:\n" +
      directResolvedKindFieldFiles.join("\n"),
  );
}
const semanticMetadataAccessSource = (
  await Promise.all(
    allSourceFiles.map((path) => readFile(path, "utf8")),
  )
).join("\n");
if (/\b(?:\.|->)resolution_state\b/.test(
      semanticMetadataAccessSource,
    )) {
  throw new Error(
    "semantic resolution state must not be stored or accessed through Syntax AST fields",
  );
}
if (/\bnode_(?:lvar|funcref|gvar|vla_alloc)_t\b/.test(astSource)) {
  throw new Error(
    "resolved references and VLA work nodes must not use specialized Syntax AST payload structs",
  );
}
const resolvedOnlyNodeKinds = [
  "ND_FUNCDEF",
  "ND_LVAR",
  "ND_FUNCREF",
  "ND_DEREF",
  "ND_GVAR",
  "ND_VLA_ALLOC",
  "ND_FP_TO_INT",
  "ND_INT_TO_FP",
  "ND_VA_ARG_AREA",
];
const resolvedOnlyNodeKindPattern = new RegExp(
  `\\b(?:${resolvedOnlyNodeKinds.join("|")})\\b`,
);
const parserSyntaxSources = (
  await Promise.all(
    allSourceFiles
      .filter((path) =>
        path.startsWith("src/parser/") &&
        path.endsWith(".c") &&
        path !== "src/parser/node_utils.c" &&
        path !== "src/parser/node_utils_legacy.c")
      .map((path) => readFile(path, "utf8")),
  )
).join("\n");
if (!/typedef\s+enum\s*\{[^]*?\}\s*psx_syntax_node_kind_t\s*;/.test(
      syntaxNodeKindHeader,
    ) ||
    resolvedOnlyNodeKindPattern.test(syntaxNodeKindHeader) ||
    /resolved_node_kind\.h/.test(astSource) ||
    /typedef\s+enum\s*\{[^]*?\}\s*node_kind_t\s*;/.test(astSource) ||
    resolvedOnlyNodeKindPattern.test(parserSyntaxSources) ||
    /#include\s+"node_utils\.h"/.test(parserExpressionSource) ||
    !/#include\s+"syntax_node\.h"/.test(parserExpressionSource) ||
    /#include\s+"node_utils\.h"/.test(parserStatementSource) ||
    !/#include\s+"syntax_node\.h"/.test(parserStatementSource)) {
  throw new Error(
    "semantic working node kinds must not return, and syntax parsing must depend only on syntax node construction",
  );
}
if (/__va_arg_area/.test(parserExpressionSource) ||
    !/memcmp\s*\(\s*request->name\s*,\s*"__va_arg_area"/.test(
      identifierResolutionSource,
    ) ||
    !/resolution->kind\s*=\s*PSX_IDENTIFIER_BUILTIN_VA_ARG_AREA/.test(
      identifierResolutionSource,
    ) ||
    !/PSX_IDENTIFIER_BUILTIN_VA_ARG_AREA/.test(
      identifierResolutionHeader,
    ) ||
    !/resolution\.symbol\.kind\s*==\s*PSX_IDENTIFIER_BUILTIN_VA_ARG_AREA[^]*?\.kind\s*=\s*PSX_HIR_VARARG_CURSOR/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "__va_arg_area must remain identifier syntax and resolve directly into a Typed HIR vararg cursor",
  );
}
if (!/resolution\.symbol\.kind\s*==\s*PSX_IDENTIFIER_FUNCTION[^]*?spec\.kind\s*=\s*PSX_HIR_FUNCTION_REF/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/resolution\.symbol\.kind\s*==\s*PSX_IDENTIFIER_LOCAL[^]*?psx_resolve_local_hir_node_spec_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /identifier->base\.kind\s*=/.test(syntaxTypedHirResolutionSource)) {
  throw new Error(
    "direct identifier resolution must preserve Syntax identifiers and emit Typed HIR references",
  );
}
if (!/\bND_STATIC_ASSERT\b/.test(syntaxNodeKindHeader) ||
    !/callbacks->parse_static_assert\s*\(/.test(
      localDeclarationSyntaxSource,
    ) ||
    !/psx_node_new_static_assert_syntax_in\s*\(/.test(
      localDeclarationFrontendSource,
    ) ||
    /\bapply_static_assert\b/.test(localDeclarationSyntaxSource) ||
    /\bapply_static_assert\b/.test(localDeclarationFrontendSource) ||
    !/case\s+ND_STATIC_ASSERT\s*:\s*\{[^]*?\.kind\s*=\s*PSX_HIR_NOP/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/case\s+PSX_HIR_NOP\s*:\s*return\s+1\s*;/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "block static assertions must remain immutable Syntax AST until resolver processing and materialize as Typed HIR no-ops",
  );
}
if (!/\bND_NULL_STMT\b/.test(syntaxNodeKindHeader) ||
    /\bhas_empty_body\b/.test(astSource) ||
    !/psx_node_new_null_statement_syntax_in\s*\(/.test(
      parserStatementSource,
    ) ||
    !/case\s+ND_NULL_STMT\s*:\s*return\s+1\s*;/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/case\s+ND_NULL_STMT\s*:\s*\{[^]*?\.kind\s*=\s*PSX_HIR_NOP/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "null statements must remain explicit Syntax AST and materialize as Typed HIR no-ops",
  );
}
const expressionParseContext = parserExpressionSource.match(
  /typedef\s+struct\s*\{([^]*?)\}\s*expr_parse_ctx_t\s*;/,
)?.[1] ?? "";
if (!/psx_expression_syntax_context_t\s+syntax\s*;/.test(
      expressionParseContext,
    ) ||
    /psx_(?:semantic_context|global_registry|local_registry)_t/.test(
      `${expressionParseContext}\n${expressionSyntaxContextSource}`,
    ) ||
    /#include\s+"(?:semantic_ctx|global_registry|local_registry|decl|initializer_syntax|stmt)\.h"/.test(
      parserExpressionSource,
    ) ||
    /\bpsx_expr_(?:expr|assign)_in_contexts\s*\(/.test(
      parserExpressionSource,
    ) ||
    !/\bpsx_expr_expr_syntax\s*\(/.test(parserExpressionSource) ||
    !/\bpsx_expr_assign_syntax\s*\(/.test(parserExpressionSource) ||
    !/\bpsx_expr_conditional_syntax\s*\(/.test(parserExpressionSource) ||
    !/psx_name_classifier_t\s+name_classifier\s*;/.test(
      expressionSyntaxContextSource,
    ) ||
    !/parse_with_syntax_services\s*\(/.test(
      expressionSyntaxAdapterSource,
    ) ||
    !/psx_expr_expr_syntax/.test(
      expressionSyntaxAdapterSource,
    ) ||
    !/psx_expr_assign_syntax/.test(
      expressionSyntaxAdapterSource,
    ) ||
    !/psx_expr_conditional_syntax/.test(
      expressionSyntaxAdapterSource,
    )) {
  throw new Error(
    "expression parser core must receive a syntax-only context with NameClassifier while semantic registries remain isolated in the compatibility adapter",
  );
}
const statementParseContext = statementParserSource.match(
  /typedef\s+struct\s*\{([^]*?)\}\s*psx_statement_parse_context_t\s*;/,
)?.[1] ?? "";
if (!/psx_statement_syntax_context_t\s+syntax\s*;/.test(
      statementParseContext,
    ) ||
    /psx_(?:semantic_context|global_registry|local_registry)_t/.test(
      `${statementParseContext}\n${statementSyntaxContextSource}`,
    ) ||
    /#include\s+"(?:semantic_ctx|global_registry|local_registry|decl|enum_const|expr)\.h"/.test(
      statementParserSource,
    ) ||
    /\bpsx_(?:stmt_stmt|parse_statement_expression)_in_contexts\s*\(/.test(
      statementParserSource,
    ) ||
    !/\bpsx_stmt_stmt_syntax\s*\(/.test(statementParserSource) ||
    !/\bpsx_parse_statement_expression_syntax\s*\(/.test(
      statementParserSource,
    ) ||
    !/psx_name_classifier_t\s+name_classifier\s*;/.test(
      statementSyntaxContextSource,
    ) ||
    /\b(?:begin_usage_region|end_usage_region)\b/.test(
      `${statementSyntaxContextSource}\n${statementParserSource}\n${statementSyntaxAdapterSource}`,
    ) ||
    /\bpsx_decl_(?:begin|end)_lvar_usage_region_in\s*\(/.test(
      `${statementParserSource}\n${parserSource}`,
    ) ||
    !/syntax\.parse_expression/.test(statementParserSource) ||
    !/syntax\.parse_local_declaration/.test(statementParserSource) ||
    !/syntax\.parse_case_expression/.test(statementParserSource) ||
    /syntax\.parse_case_constant/.test(statementParserSource) ||
    /\bpsx_parse_case_const_expr_in_contexts\s*\(/.test(
      statementSyntaxAdapterSource,
    ) ||
    /#include\s+"enum_const\.h"/.test(statementSyntaxAdapterSource) ||
    /syntax\.register_goto/.test(statementParserSource) ||
    /syntax\.register_label/.test(statementParserSource) ||
    /\bpsx_ctx_(?:register_goto_ref|register_label_def)\s*\(/.test(
      statementSyntaxAdapterSource,
    )) {
  throw new Error(
    "statement parser core must receive a syntax-only context with NameClassifier while semantic registries remain isolated in the compatibility adapter",
  );
}
if (/\b(?:legacy|psx_local_registry_t|psx_global_registry_t|psx_semantic_context_t)\b|#include\s+"(?:local_registry|semantic_ctx|stmt_legacy)\.h"/.test(
      `${expressionSyntaxAdapterSource}\n${statementSyntaxAdapterSource}\n${statementSyntaxAdapterHeader}\n${localDeclarationFrontendSource}\n${localDeclarationFrontendHeader}`,
    )) {
  throw new Error(
    "production syntax adapters must depend only on NameClassifier",
  );
}
if (/psx_(?:semantic_context|global_registry|local_registry)_t/.test(
      statementSyntaxAdapterHeader,
    ) ||
    /ps_ctx_name_classifier\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    /psx_expr_(?:expr|assign|conditional)_in_contexts\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    /psx_legacy_statement_syntax_adapter_init\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    !/psx_name_classifier_t\s+empty_classifier\s*=\s*\{0\}/.test(
      frontendTranslationUnitSource,
    ) ||
    !/ps_parser_name_environment_reset_at\s*\([^]*?stream->parser\.syntax\.name_classifier/.test(
      frontendTranslationUnitSource,
    ) ||
    !/psx_statement_syntax_adapter_init\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    !/psx_frontend_init_local_declaration_syntax_adapter\s*\(/.test(
      frontendTranslationUnitSource,
    )) {
  throw new Error(
    "production parsing must own typedef classification in parser NameEnvironment and must not receive semantic registries",
  );
}
if (/\bps_ctx_(?:enter|leave)_block_scope_in\s*\(/.test(parserSource) ||
    /\bpsx_ctx_validate_goto_refs_in\s*\(/.test(parserSource) ||
    !/\bvalidate_direct_function_jumps\s*\(/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "function-body parsing must not mutate semantic scopes or jump registries",
  );
}
const functionBodyParseIndex = frontendTranslationUnitSource.indexOf(
  "ps_parse_function_definition_body(",
);
const functionSemanticResolveIndex = frontendTranslationUnitSource.indexOf(
  "if (!resolver(",
);
const parameterBindingSeedIndex = frontendTranslationUnitSource.indexOf(
  "psx_record_function_definition_declarator_binding_events(",
);
const internalStorageRegistration = localRegistrySource.match(
  /lvar_t\s*\*ps_local_registry_create_internal_storage_object_qual_type_in\s*\([^]*?\n\}/,
)?.[0] ?? "";
const declaratorExpressionSyntaxSource = await readFile(
  "src/parser/declaration_syntax.c",
  "utf8",
);
if (!/node_t\s*\*body\s*;/.test(functionDefinitionSyntaxHeader) ||
    !/int\s+ps_parse_function_definition_body\s*\(\s*psx_parser_stream_t\s*\*stream\s*,\s*psx_parsed_function_definition_t\s*\*definition\s*,/.test(
      parserStreamHeader,
    ) ||
    /ps_parse_function_definition_body\s*\([^;]*node_function_definition_t/.test(
      parserStreamHeader,
    ) ||
    !/definition->body\s*=\s*\(node_t\s*\*\)parse_funcdef_body_block/.test(
      parserStreamSource,
    ) ||
    !/!syntax_function->body/.test(syntaxTypedHirResolutionSource) ||
    !/const\s+psx_parsed_function_definition_t\s*\*definition/.test(
      frontendFunctionDefinitionSource,
    ) ||
    !/psx_resolve_function_definition_header_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /ps_parse_runtime_declarator_expressions_with_options\s*\(/.test(
      frontendFunctionDefinitionSource,
    ) ||
    !/psx_materialize_declarator_expression_syntax\s*\(\s*declarator\s*,\s*options\s*\)/.test(
      declaratorExpressionSyntaxSource,
    ) ||
    /\bbound->node\s*=/.test(
      `${localDeclarationPipelineSource}\n${declarationApplicationSource}`,
    ) ||
    !/psx_resolve_declarator_bound_in_contexts\s*\(/.test(
      declarationApplicationSource,
    ) ||
    !/psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts\s*\(/.test(
      declaratorBoundResolutionSource,
    ) ||
    /psx_resolution_work_tree_|psx_bind_identifier_tree|psx_semantic_resolve_tree|psx_typed_hir_tree_materialize/.test(
      declaratorBoundResolutionSource,
    ) ||
    parameterBindingSeedIndex < 0 ||
    functionBodyParseIndex < 0 ||
    functionSemanticResolveIndex < 0 ||
    parameterBindingSeedIndex > functionBodyParseIndex ||
    functionBodyParseIndex > functionSemanticResolveIndex ||
    /psx_prepare_function_definition_resolution_in_contexts\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    !/ps_name_classifier_declare\s*\(/.test(
      declarationBindingEventsSource,
    ) ||
    !/psx_declarator_outermost_function_suffix\s*\(/.test(
      declarationBindingEventsSource,
    ) ||
    !/suffix\s*==\s*primary/.test(
      declarationBindingEventsSource,
    ) ||
    !/ps_name_classifier_reserve_scope\s*\(/.test(
      declarationBindingEventsSource,
    ) ||
    /function_suffixes\s*\[\s*0\s*\]/.test(
      `${frontendTranslationUnitSource}\n${frontendFunctionDefinitionSource}\n${localDeclarationPipelineSource}`,
    ) ||
    !/psx_record_decl_specifier_binding_events\s*\(/.test(
      localDeclarationSyntaxSource,
    ) ||
    !/psx_record_declarator_binding_events\s*\(/.test(
      localDeclarationSyntaxSource,
    ) ||
    !/ps_local_registry_create_internal_storage_object_qual_type_in\s*\(/.test(
      vlaLoweringSource,
    ) ||
    !/registry->storage_objects\s*=\s*var/.test(
      internalStorageRegistration,
    ) ||
    !/registry->lvars_by_offset\[bucket\]\s*=\s*var/.test(
      internalStorageRegistration,
    ) ||
    /register_binding_event|all_bindings|lvars_by_bucket|registry->locals\s*=/.test(
      internalStorageRegistration,
    )) {
  throw new Error(
    "function definitions must be completed as syntax before semantic function construction",
  );
}
const caseNodeStruct = astSource.match(
  /struct node_case_t\s*\{([^{}]*)\};/,
);
if (!/case\s+ND_CASE\s*:\s*\{[^]*?direct_integer_constant\s*\([^]*?bind_direct_case_value\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/kind\s*==\s*PSX_HIR_CASE[^]*?direct_case_value\s*\([^]*?&spec\.integer_value/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !caseNodeStruct ||
    /\b(?:val|has_resolved_value|label_id)\b/.test(caseNodeStruct[1]) ||
    /\blabel_id\b/.test(astSource)) {
  throw new Error(
    "case label expressions must remain Syntax while resolved values live in semantic state and Typed HIR",
  );
}
const numberNodeStruct = astSource.match(
  /struct node_num_t\s*\{([\s\S]*?)\n\};/,
);
if (!numberNodeStruct ||
    /\b(?:int_is_long|int_is_long_long|int_width|int_is_plain_char)\b/.test(
      numberNodeStruct[1],
    )) {
  throw new Error(
    "node_num_t must derive integer identity and width from its canonical type",
  );
}
const functionCallStruct = astSource.match(
  /struct node_function_call_t\s*\{([^{}]*)\};/,
);
if (/\bnode_func_t\b/.test(astSource) ||
    /\bnode_function_definition_t\b/.test(astSource) ||
    /\bND_FUNCDEF\b/.test(syntaxNodeKindHeader) ||
    /\bnode_function_definition_t\b/.test(
      frontendFunctionDefinitionSource,
    ) ||
    !functionCallStruct ||
    !/\bnode_t\s*\*\*\s*arguments\s*;/.test(functionCallStruct[1]) ||
    !/\bnode_t\s*\*\s*callee\s*;/.test(functionCallStruct[1]) ||
    /\b(?:callee_type|callee_qual_type|direct_name|is_implicit_declaration|parameters|signature|lvars|is_static)\b/.test(
      functionCallStruct[1],
    ) ||
    /\bis_implicit_func_decl\b/.test(astSource) ||
    /\bpsx_function_call_(?:prepare_resolution_in|bind_direct_name|direct_name|direct_name_length|bind_qual_type|qual_type|set_implicit_declaration|is_implicit_declaration)\s*\(/.test(
      functionCallResolutionHeader + functionCallResolutionSource,
    ) ||
    !/\bpsx_qual_type_t\s+psx_resolve_function_reference_qual_type\s*\(/.test(
      functionCallResolutionHeader,
    ) ||
    !/ps_ctx_intern_pointer_to_qual_type_in\s*\(/.test(
      functionCallResolutionSource,
    ) ||
    /\bps_node_resolution_state(?:_const)?\s*\(/.test(
      functionCallResolutionSource,
    )) {
  throw new Error(
    "function definitions and calls must resolve directly into Typed HIR without Syntax sidecars",
  );
}
const logicalCallLowering = hirIrBuilder.match(
  /ir_val_t\s+hir_ir_build_call\s*\([^;]*?\)\s*\{[^]*?\n\}/,
);
if (!logicalCallLowering ||
    !/psx_semantic_type_table_parameter\s*\(/.test(logicalCallLowering[0]) ||
    !/ir_function_type_from_type_id\s*\(/.test(logicalCallLowering[0]) ||
    /ir_abi_(?:source_callable|callable_type)/.test(logicalCallLowering[0])) {
  throw new Error(
    "IR calls must retain finalized function TypeId without embedding ABI projections",
  );
}
const typeNameRef = astSource.match(
  /typedef struct\s*\{([^{}]*)\}\s*psx_type_name_ref_t\s*;/,
);
const compoundLiteralNode = astSource.match(
  /typedef struct\s*\{([^{}]*)\}\s*node_compound_literal_t\s*;/,
);
const genericAssociation = astSource.match(
  /typedef struct\s*\{([^{}]*)\}\s*psx_generic_association_t\s*;/,
);
const sizeofQueryNode = astSource.match(
  /typedef struct\s*\{([^{}]*)\}\s*node_sizeof_query_t\s*;/,
);
const alignofQueryNode = astSource.match(
  /typedef struct\s*\{([^{}]*)\}\s*node_alignof_query_t\s*;/,
);
if (!typeNameRef ||
    /\bconst\s+psx_type_t\s*\*\s*bound_base_type\s*;/.test(
      typeNameRef[1],
    ) ||
    /\bconst\s+psx_type_t\s*\*\s*resolved_type\s*;/.test(
      typeNameRef[1],
    ) ||
    !/\bpsx_type_name_base_resolution_t\b/.test(
      typeNameResolutionHeader,
    ) ||
    !/\bpsx_qual_type_t\s+base_qual_type\s*;/.test(
      typeNameResolutionHeader,
    ) ||
    !/\bconst\s+psx_runtime_declarator_application_t\s*\*\s*runtime_application\s*;/.test(
      typeNameResolutionHeader,
    ) ||
    !/\bpsx_resolve_type_name_base_in_contexts\s*\(/.test(
      typeNameResolutionHeader,
    ) ||
    /\bpsx_(?:bind_type_name_ref|resolve_bound_type_name|type_name_bind_resolved|type_name_bound_|type_name_resolved_)/.test(
      `${typeNameResolutionHeader}\n${typeNameResolutionSource}`,
    ) ||
    /type_compatibility_view\.h|\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      typeNameResolutionSource,
    ) ||
    !/static\s+psx_qual_type_t\s+bind_base_qual_type\s*\(/.test(
      typeNameResolutionSource,
    ) ||
    !/ps_ctx_tag_qual_type_at_in\s*\(/.test(
      typeNameResolutionSource,
    ) ||
    /ps_type_(?:clone_in|add_qualifiers|set_decl_spec_qualifiers)\s*\(/.test(
      typeNameResolutionSource,
    ) ||
    !compoundLiteralNode ||
    !/\bpsx_type_name_ref_t\s+type_name\s*;/.test(
      compoundLiteralNode[1],
    ) ||
    /\bobject_type\b|\brequires_addressable_object\b|\bhas_file_scope_storage\b/.test(
      compoundLiteralNode[1],
    ) ||
    !genericAssociation ||
    !/\bpsx_type_name_ref_t\s+type_name\s*;/.test(
      genericAssociation[1],
    ) ||
    /\bpsx_type_t\s*\*\s*type\s*;/.test(genericAssociation[1]) ||
    !sizeofQueryNode ||
    !/\bpsx_type_name_ref_t\s+type_name\s*;/.test(sizeofQueryNode[1]) ||
    /\bqueried_type\b/.test(sizeofQueryNode[1]) ||
    !alignofQueryNode ||
    !/\bpsx_type_name_ref_t\s+type_name\s*;/.test(alignofQueryNode[1]) ||
    /\bresolved_alignment\b/.test(alignofQueryNode[1])) {
  throw new Error(
    "type-name expressions must resolve directly into canonical semantic types without Syntax state",
  );
}

const nodeUtilsSource = await readFile("src/parser/node_utils.c", "utf8");
const nodeUtilsHeaderSource = await readFile(
  "src/parser/node_utils.h",
  "utf8",
);
if (allSourceFiles.includes("src/parser/node_type_public.h") ||
    allSourceFiles.includes("src/parser/node_resolution_state.h") ||
    /^(?:const\s+psx_type_t\s*\*|psx_qual_type_t|void|int)\s+(?:ps_node_get_type|ps_node_qual_type|ps_node_bind_type|ps_node_bind_qual_type|ps_node_clear_type|ps_node_prepare_resolution_state_in|ps_node_copy_resolution_state_in)\s*\(/m.test(
      nodeUtilsSource,
    )) {
  throw new Error(
    "resolved node type compatibility APIs must be absent",
  );
}
if (/\bpsx_type_t\b|parser_type_compatibility\.h|\bps_node_(?:get_type|bind_type)\s*\(/.test(
      `${nodeUtilsHeaderSource}\n${nodeUtilsSource}`,
    ) ||
    allSourceFiles.includes("src/parser/node_utils_legacy.c") ||
    allSourceFiles.includes("src/parser/node_utils_legacy.h")) {
  throw new Error(
    "legacy parser-type node builders must be absent",
  );
}
if (/\bps_ctx_(?:get|find)_tag_member(?:_info)?(?:_at_scope)?_in\s*\(/.test(
      nodeUtilsSource,
    )) {
  throw new Error(
    "parser node utilities must traverse canonical RecordDecl and RecordLayout data without tag lookup compatibility APIs",
  );
}
const removedContextBoundSlotHelpers = [
  /static\s+int\s+gvar_record_find_unnamed_union_covering_offset\s*\([^]*?\n\}/,
  /static\s+void\s+gvar_record_flat_cover_state_note\s*\([^]*?\n\}/,
  /static\s+int\s+gvar_member_flat_slot_count\s*\([^]*?\n\}/,
  /static\s+int\s+gvar_record_flat_slot_count\s*\([^]*?\n\}/,
].map((pattern) => nodeUtilsSource.match(pattern)?.[0]);
const canonicalInitializerSlotSource = await readFile(
  "src/semantic/initializer_resolution.c",
  "utf8",
);
const canonicalSlotCounter = canonicalInitializerSlotSource.match(
  /int\s+psx_initializer_flat_slot_count_with_records\s*\([^]*?\n\}/,
)?.[0];
if (removedContextBoundSlotHelpers.some((source) => source) ||
    !/\bpsx_initializer_flat_slot_count_with_records\s*\(/.test(
      nodeUtilsSource,
    ) ||
    !canonicalSlotCounter ||
    !/\bpsx_semantic_type_table_t\b/.test(canonicalSlotCounter) ||
    !/\bpsx_record_decl_table_t\b/.test(canonicalSlotCounter) ||
    !/\bpsx_record_layout_table_t\b/.test(canonicalSlotCounter) ||
    !/\bag_data_layout_t\b/.test(canonicalSlotCounter) ||
    /\bag_target_info_t\b/.test(canonicalSlotCounter)) {
  throw new Error(
    "resolved aggregate slot traversal must use canonical TypeId and DataLayout inputs",
  );
}
const removedSplitTagTraversalFunctions = [
  "ps_tag_flat_cover_state_note_in",
  "ps_tag_find_unnamed_union_covering_offset_in",
  "ps_record_member_decl_flat_slots_in",
  "ps_record_member_decl_elem_flat_slots_in",
  "ps_record_member_decl_subscript_stride_slots_in",
  "ps_tag_flat_slot_count_in",
  "ps_tag_member_at_flat_slot_in",
  "ps_tag_next_named_member_in",
  "ps_tag_first_named_member_in",
  "ps_tag_find_named_member_in",
  "ps_tag_select_union_member_for_init_slot_in",
  "ps_tag_union_init_member_for_slot_in",
  "ps_tag_member_designator_slot_in",
];
const remainingSplitTagTraversalFunctions =
  removedSplitTagTraversalFunctions.filter((name) =>
    new RegExp(`\\b${name}\\s*\\(`).test(
      `${nodeUtilsSource}\n${nodeUtilsHeaderSource}`,
    ));
if (remainingSplitTagTraversalFunctions.length) {
  throw new Error(
    "unused parser tag traversal compatibility APIs must not return:\n" +
      remainingSplitTagTraversalFunctions.join("\n"),
  );
}
for (const removedApi of [
  "ps_node_type_size",
  "ps_node_storage_type_size",
  "ps_node_deref_size",
  "ps_node_aggregate_value_size",
  "ps_node_cast_i64_extension_info",
  "ps_node_i64_widen_source_is_unsigned",
  "ps_node_row_decay_pointer_arith_type_in",
  "ps_node_new_shift_trunc_extend_in",
  "ps_node_compound_literal_array_size",
  "ps_node_new_binary_in",
]) {
  if (new RegExp(`\\b${removedApi}\\s*\\(`).test(
        `${parserLayerSource}\n${loweringLayerSource}`,
      )) {
    throw new Error(
      `context-free parser node layout API ${removedApi} must not return`,
    );
  }
}
for (const removedApi of [
  "ps_gvar_decl_sizeof",
  "ps_gvar_storage_size",
  "ps_gvar_array_element_size",
  "ps_gvar_initializer_element_size",
  "ps_gvar_initializer_element_count",
  "ps_gvar_visit_initializer",
]) {
  if (new RegExp(`\\b${removedApi}\\s*\\(`).test(
        `${parserLayerSource}\n${loweringLayerSource}`,
      )) {
    throw new Error(
      `context-free global layout API ${removedApi} must not return`,
    );
  }
}
for (const removedApi of [
  "ps_lvar_decl_sizeof",
  "ps_lvar_storage_size",
  "ps_lvar_elem_size",
  "ps_lvar_array_scalar_element_size",
  "ps_node_new_lvar_typed_at_for_in",
  "ps_node_new_array_elem_lvar_for_in",
]) {
  if (new RegExp(`\\b${removedApi}\\s*\\(`).test(
        `${parserLayerSource}\n${loweringLayerSource}`,
      )) {
    throw new Error(
      `context-free local layout API ${removedApi} must not return`,
    );
  }
}
if (!/static\s+psx_semantic_node_t\s*\*build_direct_identifier\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_identifier_expression_resolution_t\s+resolution/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/resolution\.expression_qual_type/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/resolution\.decays_array_to_address/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\.kind\s*=\s*PSX_HIR_ADDRESS/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "direct identifier resolution must consume resolver-owned QualTypes for array decay",
  );
}
const identifierExpressionResolver = identifierResolutionSource.match(
  /void\s+psx_resolve_identifier_expression\s*\([^]*?\n\}/,
);
if (!identifierExpressionResolver ||
    !/psx_semantic_type_table_describe\s*\(/.test(
      identifierExpressionResolver[0],
    ) ||
    !/psx_semantic_type_table_contains_vla_array\s*\(/.test(
      identifierExpressionResolver[0],
    ) ||
    /ps_ctx_type_by_id_in\s*\(|declared_type->kind|ps_lvar_is_vla\s*\(/.test(
      identifierExpressionResolver[0],
    ) ||
    !/resolved\.local_has_static_storage/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/resolution\.local_is_vla_object/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "direct identifier resolution must consume resolver classification instead of re-reading parser symbol types",
  );
}
const classifiedInitializerVisitor = nodeUtilsSource.match(
  /int\s+ps_gvar_visit_initializer_classified\s*\([^]*?\n\}/,
);
if (!classifiedInitializerVisitor ||
    /\bps_type_(?:size|align)of\s*\(/.test(
      classifiedInitializerVisitor[0],
    ) ||
    !/int\s+scalar_size\s*,\s*int\s+slot_element_size\s*,\s*int\s+slot_element_count/.test(
      gvarPublicHeaderSource,
    ) ||
    !/scalar_element_type_id\s*\(/.test(
      translationUnitDataLoweringSource,
    ) ||
    !/ps_gvar_visit_initializer_classified\s*\([^;]*slot_element_size\s*,\s*slot_element_count/s.test(
      translationUnitDataLoweringSource,
    )) {
  throw new Error(
    "global initializer visitors must consume layout values resolved from TypeId and TargetSpec",
  );
}
if (!/try_build_pointer_arithmetic\s*\(/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "array decay result typing must follow semantic type shape without querying target layout",
  );
}
const explicitDiagnosticCastLoweringSource = await readFile(
  "src/lowering/cast_lowering.c",
  "utf8",
);
const explicitHirCastCoercion = hirIrBuilder.match(
  /static\s+ir_val_t\s+coerce_explicit_cast_value\s*\([^]*?\n\}/,
);
if (!explicitHirCastCoercion ||
    /\bps_type_(?:size|align)of\s*\(/.test(explicitHirCastCoercion[0]) ||
    !/\btarget\.source_size\b/.test(explicitHirCastCoercion[0])) {
  throw new Error(
    "shift truncation must consume a width already resolved against the active target",
  );
}
const explicitDiagnosticInitializerResolutionSource = await readFile(
  "src/semantic/initializer_resolution.c",
  "utf8",
);
const initializerResolutionHeader = await readFile(
  "src/semantic/initializer_resolution.h",
  "utf8",
);
const initializerTargetType = initializerResolutionHeader.match(
  /typedef struct\s*\{([^}]*)\}\s*psx_initializer_target_t\s*;/,
);
const initializerLeafType = initializerResolutionHeader.match(
  /typedef struct\s*\{([^}]*)\}\s*psx_initializer_scalar_leaf_t\s*;/,
);
const initializerLayoutApis = [
  "psx_resolve_flat_local_initializer_plan",
  "psx_collect_initializer_scalar_leaves_with_records",
  "psx_initializer_flat_slot_count_with_records",
];
if (!initializerTargetType ||
    !/\bpsx_type_id_t\s+type_id\s*;/.test(initializerTargetType[1]) ||
    /\bpsx_type_t\b/.test(initializerTargetType[1]) ||
    !initializerLeafType ||
    !/\bpsx_qual_type_t\s+qual_type\s*;/.test(initializerLeafType[1]) ||
    /\bpsx_type_id_t\s+type_id\s*;/.test(initializerLeafType[1]) ||
    /\bpsx_type_t\b/.test(initializerLeafType[1]) ||
    !/\bpsx_type_id_t\s+string_array_type_id\s*;/.test(
      initializerLeafType[1],
    ) ||
    /\bpsx_resolve_initializer_designator_path_with_records\s*\(/.test(
      initializerResolutionHeader,
    ) ||
    !/\bpsx_collect_initializer_scalar_leaves_with_records\s*\([\s\S]*?\bpsx_qual_type_t\s+qual_type\b/.test(
      initializerResolutionHeader,
    ) ||
    /\bpsx_resolve_initializer_designator_path\s*\(/.test(
      initializerResolutionHeader,
    ) ||
    /\bpsx_collect_initializer_scalar_leaves\s*\(/.test(
      initializerResolutionHeader,
    ) ||
    /\bpsx_initializer_leaf_cursor_after_target\s*\(/.test(
      initializerResolutionHeader,
    ) ||
    /\bpsx_type_compatibility_canonical_view_for\s*\(/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_array_flat_element_count\s*\(/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    /#include\s+"\.\.\/parser\/type\.h"/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    /\bps_type_(?:is_tag_aggregate|record_id|array_flat_element_count|character_code_unit_width)\s*\(/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    !/\bpsx_type_layout_sizeof\s*\(/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    /\bag_target_info_t\b/.test(
      `${initializerResolutionHeader}\n${explicitDiagnosticInitializerResolutionSource}`,
    ) ||
    /\bag_target_info_data_layout\s*\(/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    initializerLayoutApis.some(
      (name) => !new RegExp(
        `\\b${name}\\s*\\([^]*?const\\s+ag_data_layout_t\\s*\\*data_layout`,
      ).test(initializerResolutionHeader),
    ) ||
    /\bpsx_type_layout_sizeof_for_target\s*\(/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    !/\bpsx_record_layout_table_lookup\s*\(/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    /member_layout\s*\?\s*member_layout->offset\s*:\s*member->offset/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    /\bps_type_sizeof_for_target\s*\(|\bps_tag_member_decl_storage_size\s*\(/.test(
      explicitDiagnosticInitializerResolutionSource,
    )) {
  throw new Error(
    "initializer resolution must carry TypeId values and derive target layout through the semantic type table",
  );
}
const explicitDiagnosticStaticDataInitializerSource = await readFile(
  "src/lowering/static_data_initializer.c",
  "utf8",
);
const declarationPipelineSource = await readFile(
  "src/declaration_pipeline.c",
  "utf8",
);
const declarationPipelineHeader = await readFile(
  "src/declaration_pipeline.h",
  "utf8",
);
const functionDeclarationPipelineRequest = declarationPipelineHeader.match(
  /typedef\s+struct\s*\{([^{}]*)\}\s*psx_function_declaration_pipeline_request_t\s*;/,
);
const functionDeclarationPipeline = declarationPipelineSource.match(
  /int\s+psx_apply_function_declaration_pipeline\s*\([^]*?\n\}/,
);
if (!functionDeclarationPipelineRequest ||
    !functionDeclarationPipeline ||
    /\bglobal_registry\b/.test(functionDeclarationPipelineRequest[1]) ||
    /request->global_registry\b/.test(functionDeclarationPipeline[0])) {
  throw new Error(
    "function declaration pipeline must resolve names from semantic context only",
  );
}
const parameterDeclarationResolutionSource = await readFile(
  "src/semantic/parameter_declaration_resolution.c",
  "utf8",
);
const localDeclarationPlanHeader = await readFile(
  "src/semantic/local_declaration_plan.h",
  "utf8",
);
const localDeclarationPlanSource = await readFile(
  "src/semantic/local_declaration_plan.c",
  "utf8",
);
const parameterDeclarationPlanHeader = await readFile(
  "src/lowering/parameter_storage_plan.h",
  "utf8",
);
const parameterDeclarationPlanSource = await readFile(
  "src/lowering/parameter_storage_plan.c",
  "utf8",
);
const semanticDiagnosticsSource = `${typedHirDiagnosticsSource}\n${await readFile(
  "src/semantic/local_usage_diagnostics.c",
  "utf8",
)}`;
const semanticWarningCalls = callBodies(
  semanticDiagnosticsSource, "diag_warn_tokf_in",
);
const irWarningCalls = callBodies(hirIrBuilder, "diag_warn_tokf_in");
const wasmObjectOutputSource = wasmObjSource.slice(
  wasmObjSource.indexOf("static void wasm32_obj_end_context("),
);
const wasmObjectDiagnosticCalls = callBodies(
  wasmObjectOutputSource, "diag_emit_internalf_in",
);
const diagnosticLocaleAdapterSource = await readFile(
  "tools/wasm_js_api/agc-wasm.js",
  "utf8",
);
const diagnosticLocaleToolchainSource = await readFile(
  "tools/wasm_js_api/agc-toolchain.js",
  "utf8",
);
const selfHostBuildSource = await readFile(
  "scripts/build_wasm_selfhost_api.sh",
  "utf8",
);
const diagnosticLocaleConfigSource = await readFile(
  "src/diag/locale_config.h",
  "utf8",
);
const diagnosticCoreSource = await readFile(
  "src/diag/diag.c",
  "utf8",
);
const diagnosticUiTextsSource = await readFile(
  "src/diag/ui_texts.c",
  "utf8",
);
const makefileSource = await readFile("Makefile", "utf8");
if (/alignas_value(?:\.o|\.h)/.test(
      `${makefileSource}\n${parserUnitTestSource}`,
    )) {
  throw new Error(
    "the removed token-semantic alignas compatibility layer must not return",
  );
}
if (semanticWarningCalls.length === 0 ||
    semanticWarningCalls.some((body) =>
      !body.includes("diag_warn_message_for_in(")) ||
    !irWarningCalls.some((body) =>
      body.includes("DIAG_WARN_PARSER_MISSING_RETURN") &&
      body.includes("diag_warn_message_for_in(")) ||
    wasmObjectDiagnosticCalls.length !== 3 ||
    wasmObjectDiagnosticCalls.some((body) =>
      !body.includes("diag_message_for_in(")) ||
    !/DIAG_ERR_CODEGEN_WASM_OBJECT_OPEN_FAILED[\s\S]*?diag_emit_internalf_in\s*\([\s\S]*?diag_message_for_in\s*\(/.test(
      compilerMainSource,
    ) ||
    /failed to (?:open|write) Wasm object output|missing Wasm object output sink/.test(
      `${compilerMainSource}\n${wasmObjSource}`,
    ) ||
    !/agc_wasm_adapter_set_diagnostic_locale\s*\(/.test(compilerMainSource) ||
    !/agc_wasm_adapter_set_diagnostic_locale/.test(diagnosticLocaleAdapterSource) ||
    !/\{ diagnosticLocale \}/.test(diagnosticLocaleToolchainSource) ||
    !/--export=agc_wasm_adapter_set_diagnostic_locale/.test(selfHostBuildSource) ||
    !/--export=agc_wasm_adapter_create/.test(selfHostBuildSource) ||
    !/--export=agc_wasm_adapter_destroy/.test(selfHostBuildSource) ||
    !/--export=agc_wasm_adapter_session_generation/.test(
      selfHostBuildSource,
    ) ||
    /--export=agc_wasm_(?:compile|set_|diagnostic_(?:set|count|bytes|limit|severity|code|message|source|start|end))/.test(
      selfHostBuildSource,
    ) ||
    !/requireAdapterHandle\s*\(\)/.test(diagnosticLocaleAdapterSource) ||
    !/src\/diag\/messages_ja\.c\|src\/diag\/messages_en\.c/.test(
      selfHostBuildSource,
    ) ||
    !/AGC_DIAG_LOCALE_ALL/.test(diagnosticLocaleConfigSource) ||
    !/defined\(AGC_DIAG_LOCALE_ALL\)/.test(diagnosticCoreSource) ||
    !/defined\(AGC_DIAG_LOCALE_ALL\)/.test(diagnosticUiTextsSource) ||
    /defined\(DIAG_LANG_ALL\)/.test(diagnosticUiTextsSource) ||
    !/^DIAG_LANG\?=all$/m.test(makefileSource)) {
  throw new Error(
    "user-facing diagnostics must come from per-context locale catalogs across native and Wasm paths",
  );
}
const expressionOperandResolutionSource = await readFile(
  "src/semantic/expression_operand_resolution.c",
  "utf8",
);
const integerConversionHeader = await readFile(
  "src/type_system/integer_conversion.h",
  "utf8",
);
const integerConversionSource = await readFile(
  "src/type_system/integer_conversion.c",
  "utf8",
);
const expressionOperandResolutionHeader = await readFile(
  "src/semantic/expression_operand_resolution.h",
  "utf8",
);
const typeOperatorsHeader = await readFile(
  "src/type_system/type_operators.h",
  "utf8",
);
if (
  /parser\/ast\.h|resolved_node_kind\.h|\bnode_t\b|\bND_[A-Z0-9_]+\b|\bps_node_/.test(
    `${expressionOperandResolutionHeader}\n${expressionOperandResolutionSource}`,
  ) ||
  !/\bpsx_resolve_binary_result_qual_type_in\s*\(/.test(
    expressionOperandResolutionHeader,
  ) ||
  !/\bpsx_resolve_binary_qual_types_in\s*\(/.test(
    expressionOperandResolutionHeader,
  ) ||
  !/\bpsx_resolve_binary_qual_types_in\s*\(/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/PSX_BINARY_OPERANDS_INCOMPATIBLE/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/\bpsx_resolve_binary_result_qual_type_in\s*\(/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/\bpsx_resolve_subscript_qual_types_in\s*\(/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  allSourceFiles.some((file) =>
    file.endsWith("expression_operand_compatibility.c") ||
    file.endsWith("expression_operand_compatibility.h")
  ) ||
  /\bnode_t\b|\bND_[A-Z0-9_]+\b|parser\//.test(
    typeOperatorsHeader,
  )
) {
  throw new Error(
    "expression QualType rules and operators must be AST-independent while direct HIR resolution adapts syntax",
  );
}
if (/\bpsx_type_t\b|\bps_type_[A-Za-z0-9_]*\s*\(|parser\/type(?:_builder)?\.h/.test(
      expressionOperandResolutionSource,
    ) ||
    !/\bpsx_type_shape_t\b/.test(expressionOperandResolutionSource) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/\bpsx_integer_conversion_from_shape\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/\bpsx_integer_promotion_for_data_layout\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/\bpsx_usual_integer_conversion_for_data_layout\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    /\b(?:integer_rank|integer_rank_size|promoted_integer_conversion|usual_integer_conversion)\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/\bps_ctx_data_layout\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    /\bps_type_sizeof\s*\(/.test(expressionOperandResolutionSource) ||
    /\bps_type_sizeof\s*\(/.test(semanticDiagnosticsSource) ||
    /\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      semanticDiagnosticsSource,
    ) ||
    /\bps_node_get_type\s*\(|\bps_type_(?:integer_promotion|usual_arithmetic)\w*\s*\(/.test(
      semanticDiagnosticsSource,
    ) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      semanticDiagnosticsSource,
    ) ||
    !/\bpsx_integer_promotion_for_data_layout\s*\(/.test(
      semanticDiagnosticsSource,
    ) ||
    !/\bpsx_usual_integer_conversion_for_data_layout\s*\(/.test(
      semanticDiagnosticsSource,
    )) {
  throw new Error(
    "semantic rank and category checks must use TypeShape while target-sensitive conversions receive explicit DataLayout",
  );
}
const functionParameterSyntaxSource = await readFile(
  "src/parser/function_parameter_syntax.c",
  "utf8",
);
if (/\bPS_MAX_DECLARATOR_COUNT\b|function parameter limit exceeded/.test(
      functionParameterSyntaxSource,
    ) ||
    !/\bpda_next_cap_in\s*\(/.test(functionParameterSyntaxSource) ||
    !/\bpda_xreallocarray_in\s*\(/.test(functionParameterSyntaxSource)) {
  throw new Error(
    "function parameter syntax must use dynamic storage without a fixed argument-count cap",
  );
}
const parserDeclarationSyntaxSource = await readFile(
  "src/parser/declaration_syntax.c",
  "utf8",
);
const declaratorSyntaxSource = await readFile(
  "src/parser/declarator_syntax.c",
  "utf8",
);
const aggregateMemberSyntaxSource = await readFile(
  "src/parser/aggregate_member_syntax.c",
  "utf8",
);
const staticAssertDeclarationSource = await readFile(
  "src/parser/static_assert_declaration.c",
  "utf8",
);
const staticAssertDeclarationHeader = await readFile(
  "src/parser/static_assert_declaration.h",
  "utf8",
);
if (/\bpsx_(?:semantic_context|global_registry|local_registry)_t\s*\*/.test(
      `${staticAssertDeclarationHeader}\n${staticAssertDeclarationSource}`,
    ) ||
    /#include\s+"(?:semantic_ctx|global_registry|local_registry)\.h"/.test(
      staticAssertDeclarationSource,
    ) ||
    !/psx_static_assert_syntax_context_t/.test(
      staticAssertDeclarationHeader,
    ) ||
    !/parse_assignment_expression/.test(
      staticAssertDeclarationHeader,
    )) {
  throw new Error(
    "static assert syntax parsing must depend only on parser runtime and an explicit assignment-expression service",
  );
}
const parserSemanticContextImplementation = await readFile(
  "src/parser/semantic_ctx.c",
  "utf8",
);
const diagnosticImplementationSource = await readFile(
  "src/diag/diag.c",
  "utf8",
);
const diagnosticPublicHeaderSource = await readFile(
  "src/diag/diag.h",
  "utf8",
);
const contextFreeParserDiagnosticApi =
  /\b(?:diag_(?:emit_atf|emit_tokf|report_atf|report_tokf|warn_tokf|emit_internalf|report_internalf|message_for|warn_message_for|text_for|has_error_records|active_limit_kind|limit_kind|reset_records)|ps_diag_ctx|ps_diag_missing|psx_diag_undefined_with_name|ps_diag_duplicate_with_name|ps_diag_only_in|pda_next_cap|pda_xreallocarray)\s*\(/;
const compilerOwnedDiagnosticSource = (
  await Promise.all(
    allSourceFiles
      .filter((path) => path !== "src/diag/diag.c" && path !== "src/diag/diag.h")
      .map((path) => readFile(path, "utf8")),
  )
).join("\n");
if (contextFreeParserDiagnosticApi.test(compilerOwnedDiagnosticSource) ||
    contextFreeParserDiagnosticApi.test(
      `${diagnosticImplementationSource}\n${diagnosticPublicHeaderSource}`,
    ) ||
    /compatibility_diagnostic_context|diag_compatibility_context/.test(
      `${diagnosticImplementationSource}\n${diagnosticPublicHeaderSource}`,
    ) ||
    /\b(?:diag_context_(?:active|activate)|active_diagnostic_context|diag_current_context)\b/.test(
      `${diagnosticImplementationSource}\n${diagnosticPublicHeaderSource}`,
    ) ||
    /diag_published_context|published_diagnostic_context|diag_context_publish/.test(
      `${diagnosticImplementationSource}\n${diagnosticPublicHeaderSource}`,
    ) ||
    !/diag_context_record_count\s*\(/.test(diagnosticImplementationSource) ||
    !/ag_diagnostic_context_t\s*\*diagnostic_context\s*=\s*context->diagnostic_context\s*;/.test(
      parserSemanticContextImplementation,
    ) ||
    !/context->diagnostic_context\s*=\s*diagnostic_context\s*;/.test(
      parserSemanticContextImplementation,
    )) {
  throw new Error(
    "migrated parser and semantic phases must preserve and use explicit diagnostic contexts",
  );
}
if (!/\bhir_ir_coerce_direct_value_to_qual_type\s*\(/.test(
      hirIrBuilder,
    ) ||
    !/semantic_target\.kind\s*==\s*PSX_TYPE_BOOL/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "function semantic trees must preserve operands while HIR-to-IR owns implicit conversion and Bool normalization",
  );
}
if (/\bps_function_definition_(?:return_type|signature_qual_type)\s*\(/.test(
      nodeUtilsSource,
    ) ||
    !/\bpsx_semantic_type_table_base\s*\([^]*?header\.signature_qual_type\.type_id/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /\bnode_function_definition_t\b/.test(
      frontendFunctionDefinitionSource,
    ) ||
    /\bfunction_node\b/.test(
      declarationPipelineHeader + declarationPipelineSource,
    )) {
  throw new Error(
    "function definition signatures and return types must be owned only by canonical QualType relations",
  );
}
const castLoweringSource = await readFile(
  "src/lowering/cast_lowering.c",
  "utf8",
);
const castLoweringHeader = await readFile(
  "src/lowering/cast_lowering.h",
  "utf8",
);
const aggregateCastResolutionHeader = await readFile(
  "src/semantic/aggregate_cast_resolution.h",
  "utf8",
);
const aggregateCastResolutionSource = await readFile(
  "src/semantic/aggregate_cast_resolution.c",
  "utf8",
);
const sourceCastTypeResolutionHeader = await readFile(
  "src/semantic/source_cast_type_resolution.h",
  "utf8",
);
const sourceCastTypeResolutionSource = await readFile(
  "src/semantic/source_cast_type_resolution.c",
  "utf8",
);
const characterArrayInitializerHeader = await readFile(
  "src/semantic/character_array_initializer.h",
  "utf8",
);
const characterArrayInitializerSource = await readFile(
  "src/semantic/character_array_initializer.c",
  "utf8",
);
const canonicalNodeTypeConsumers = new Map();
for (const [name, source] of canonicalNodeTypeConsumers) {
  if (/\bps_node_get_type\s*\(|\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
        source,
      )) {
    throw new Error(
      `${name} must consume resolved node QualType and TypeShape without compatibility views`,
    );
  }
}
if (/\bpsx_type_t\b|parser\/type\.h/.test(
      integerConstantEvaluationSource,
    ) ||
    !/\bpsx_type_shape_t\b/.test(integerConstantEvaluationSource)) {
  throw new Error(
    "integer constant normalization must consume canonical TypeShape without parser type views",
  );
}
const aggregateQualTypeCastPlan = castLoweringSource.match(
  /int\s+psx_plan_aggregate_source_cast_qual_types\s*\([^]*?\n\}/,
)?.[0] ?? "";
if (!/arena_alloc_in\s*\(\s*arena_context\s*,\s*sizeof\s*\(\s*node_source_cast_t\s*\)\s*\)/.test(
      nodeUtilsSource,
    ) ||
    /node_source_cast_t\s*\*[^;=]*=\s*arena_alloc(?:_in)?\s*\([^;]*sizeof\s*\(\s*node_num_t\s*\)/.test(
      nodeUtilsSource,
    )) {
  throw new Error("source casts must use their own arena allocation size");
}
if (!/\bpsx_aggregate_cast_resolution_t\b/.test(
      aggregateCastResolutionHeader,
    ) ||
    !/\bpsx_resolve_aggregate_cast_qual_types\s*\(/.test(
      aggregateCastResolutionHeader,
    ) ||
    /\b(?:node_t|node_source_cast_t|psx_hir_[a-zA-Z0-9_]+)\b/.test(
      `${aggregateCastResolutionHeader}\n${aggregateCastResolutionSource}`,
    ) ||
    /(?:temporary|local_storage|lowering_context)/.test(
      aggregateCastResolutionSource,
    ) ||
    !/psx_record_decl_table_lookup\s*\(/.test(
      aggregateCastResolutionSource,
    ) ||
    !/psx_type_kind_t\s+target_type_kind\s*;/.test(
      aggregateCastResolutionHeader,
    ) ||
    !/psx_record_id_t\s+target_record_id\s*;/.test(
      aggregateCastResolutionHeader,
    ) ||
    /target_tag_kind/.test(aggregateCastResolutionHeader) ||
    !/psx_semantic_type_table_describe\s*\(/.test(
      aggregateCastResolutionSource,
    ) ||
    /psx_type_compatibility_canonical_view_for\s*\(|ps_type_(?:is_tag_aggregate|tag_identity_matches|tag_token_kind|record_id)\s*\(/.test(
      aggregateCastResolutionSource,
    ) ||
    /#include\s+"\.\.\/parser\/type\.h"/.test(
      aggregateCastResolutionSource,
    ) ||
    !/psx_type_layout_sizeof\s*\(/.test(
      aggregateCastResolutionSource,
    ) ||
    /\bag_target_info_t\b/.test(
      `${aggregateCastResolutionHeader}\n${aggregateCastResolutionSource}`,
    ) ||
    !/const\s+ag_data_layout_t\s*\*data_layout/.test(
      aggregateCastResolutionHeader,
    )) {
  throw new Error(
    "aggregate cast semantics must resolve independently from Syntax AST, HIR, and temporary storage planning",
  );
}
if (!/\bpsx_source_cast_types_resolution_t\b/.test(
      sourceCastTypeResolutionHeader,
    ) ||
    !/\bpsx_resolve_source_cast_qual_types\s*\(/.test(
      sourceCastTypeResolutionHeader,
    ) ||
    /\b(?:node_t|node_source_cast_t|psx_hir_[a-zA-Z0-9_]+)\b/.test(
      `${sourceCastTypeResolutionHeader}\n${sourceCastTypeResolutionSource}`,
    ) ||
    /(?:lowering_context|ps_node_new_)/.test(
      sourceCastTypeResolutionSource,
    ) ||
    !/target_type\.kind\s*==\s*PSX_TYPE_VOID/.test(
      sourceCastTypeResolutionSource,
    ) ||
    !/psx_type_kind_t\s+target_type_kind\s*;/.test(
      sourceCastTypeResolutionHeader,
    ) ||
    /target_tag_kind/.test(sourceCastTypeResolutionHeader) ||
    !/psx_semantic_type_table_describe\s*\(/.test(
      sourceCastTypeResolutionSource,
    ) ||
    /psx_type_compatibility_canonical_view_for\s*\(|ps_type_(?:is_tag_aggregate|tag_token_kind|is_scalar)\s*\(/.test(
      sourceCastTypeResolutionSource,
    ) ||
    /#include\s+"\.\.\/parser\/type\.h"/.test(
      sourceCastTypeResolutionSource,
    ) ||
    !/PSX_SOURCE_CAST_TARGET_NOT_VOID_OR_SCALAR/.test(
      sourceCastTypeResolutionSource,
    ) ||
    !/PSX_SOURCE_CAST_OPERAND_NOT_SCALAR/.test(
      sourceCastTypeResolutionSource,
    ) ||
    !/psx_resolve_aggregate_cast_qual_types\s*\(/.test(
      sourceCastTypeResolutionSource,
    ) ||
    /\bag_target_info_t\b/.test(
      `${sourceCastTypeResolutionHeader}\n${sourceCastTypeResolutionSource}`,
    ) ||
    !/const\s+ag_data_layout_t\s*\*data_layout/.test(
      sourceCastTypeResolutionHeader,
    )) {
  throw new Error(
    "all source-cast constraints must resolve from canonical QualTypes before HIR emission or storage planning",
  );
}
if (!/\bpsx_character_array_initializer_plan_t\b/.test(
      characterArrayInitializerHeader,
    ) ||
    !/\bpsx_plan_character_array_string_initializer\s*\(/.test(
      characterArrayInitializerHeader,
    ) ||
    /\b(?:node_t|node_string_t|psx_hir_[a-zA-Z0-9_]+)\b/.test(
      `${characterArrayInitializerHeader}\n${characterArrayInitializerSource}`,
    ) ||
    /(?:lowering_context|ps_node_new_)/.test(
      characterArrayInitializerSource,
    ) ||
    /\bpsx_type_t\b/.test(characterArrayInitializerHeader) ||
    /\bpsx_type_compatibility_canonical_view_for\s*\(|\bps_type_character_code_unit_width\s*\(/.test(
      characterArrayInitializerSource,
    ) ||
    !/psx_semantic_type_table_describe\s*\(/.test(
      characterArrayInitializerSource,
    ) ||
    !/psx_type_layout_character_code_unit_width\s*\(/.test(
      characterArrayInitializerSource,
    ) ||
    !/const\s+ag_data_layout_t\s*\*data_layout/.test(
      characterArrayInitializerHeader,
    ) ||
    !/psx_resolve_character_array_string_shape\s*\(\s*int\s+array_capacity\s*,\s*int\s+element_width/.test(
      characterArrayInitializerHeader,
    ) ||
    !/tk_emit_string_code_units\s*\(/.test(
      characterArrayInitializerSource,
    ) ||
    !/psx_plan_character_array_string_initializer\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/static\s+psx_semantic_node_t\s+\*build_direct_character_array_initializer\s*\([\s\S]{0,4000}?PSX_HIR_NUMBER[\s\S]{0,1000}?plan->units\[unit\]/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    (syntaxTypedHirResolutionSource.match(
      /\bbuild_direct_character_array_initializer\s*\(/g,
    ) ?? []).length < 3) {
  throw new Error(
    "character array string initialization must use an AST-independent semantic plan and emit Typed HIR directly",
  );
}
if (/\bint\s+psx_plan_aggregate_source_cast\s*\(/.test(
      castLoweringHeader,
    ) ||
    !/\bint\s+psx_plan_aggregate_source_cast_qual_types\s*\(/.test(
      castLoweringHeader,
    ) ||
    !aggregateQualTypeCastPlan ||
    /\bnode_t\b|\bnode_source_cast_t\b|\bND_[A-Z0-9_]+\b/.test(
      aggregateQualTypeCastPlan,
    ) ||
    !/psx_resolve_source_cast_qual_types\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_SOURCE_CAST_TARGET_NOT_VOID_OR_SCALAR[\s\S]{0,1400}PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_TARGET_NOT_VOID_OR_SCALAR/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_SOURCE_CAST_OPERAND_NOT_SCALAR[\s\S]{0,1400}PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_OPERAND_NOT_SCALAR/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_SOURCE_CAST_AGGREGATE_TYPE_MISMATCH[\s\S]{0,1400}PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_AGGREGATE_TYPE_MISMATCH/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_type_compatibility_canonical_view_for\s*\(|ps_lowering_type_(?:size|alignment)\s*\(/.test(
      castLoweringSource,
    ) ||
    !/ps_lowering_type_id_size\s*\(/.test(castLoweringSource) ||
    !/ps_lowering_type_id_alignment\s*\(/.test(castLoweringSource) ||
    !/resolution->target_record_id/.test(castLoweringSource) ||
    !/ps_local_registry_create_internal_storage_object_qual_type_in\s*\(/.test(
      castLoweringSource,
    ) ||
    /#include\s+"\.\.\/parser\/type\.h"/.test(castLoweringSource) ||
    !/ps_local_registry_create_internal_storage_object_qual_type_in\s*\(/.test(
      localRegistrySource,
    ) ||
    !/context->unevaluated_depth\s*==\s*0[\s\S]*?psx_plan_aggregate_source_cast_resolution\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /if\s*\(context->unevaluated_depth\s*!=\s*0\)\s*return\s+0\s*;[\s\S]{0,300}ps_type_is_tag_aggregate/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/binding->aggregate_plan\.temporary/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_HIR_ASSIGN/.test(syntaxTypedHirResolutionSource) ||
    /\blower_source_cast_expression\s*\(/.test(
      castLoweringSource,
    ) ||
    /\bps_node_new_/.test(castLoweringSource) ||
    /\*\s*\(\s*node_num_t\s*\*\s*\)\s*node\s*=/.test(
      castLoweringSource,
    )) {
  throw new Error(
    "source casts must materialize directly from structured resolution metadata without generated semantic AST",
  );
}
if (!/initializer->kind\s*==\s*PSX_DECL_INIT_LIST[\s\S]{0,300}preflight_direct_flat_initializer\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /!is_complete_fixed_array\s*&&\s*!is_complete_aggregate[\s\S]{0,150}initializer->kind\s*!=\s*PSX_DECL_INIT_EXPR/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "direct local resolution must route scalar brace initializers through the structural flat initializer plan",
  );
}
if (/declaration->is_typedef[\s\S]{0,1200}declarator->function_suffix_count\s*>\s*0/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "direct local typedef resolution must accept recursive function declarators",
  );
}
const directSizeofTypeName = syntaxTypedHirResolutionSource.match(
  /static int resolve_direct_sizeof_type_name\s*\([\s\S]*?\n\}/,
);
const directTypeBeforeApplication = syntaxTypedHirResolutionSource.match(
  /static psx_qual_type_t direct_type_before_application\s*\([\s\S]*?\n\}/,
);
if (!directSizeofTypeName ||
    !directTypeBeforeApplication ||
    /\bps_ctx_type_sizeof_in\s*\(/.test(directSizeofTypeName[0]) ||
    /\bpsx_type_name_bound_base_type\s*\(|\bpsx_apply_runtime_declarator_type_in_context\s*\(|\bps_ctx_intern_qual_type_in\s*\(/.test(
      directSizeofTypeName[0],
    ) ||
    !/\bpsx_resolve_type_name_base_in_contexts\s*\(/.test(
      directSizeofTypeName[0],
    ) ||
    !/base_resolution\.base_qual_type/.test(
      directSizeofTypeName[0],
    ) ||
    !/\bpsx_apply_runtime_declarator_qual_type_in_context\s*\(/.test(
      directSizeofTypeName[0],
    ) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      directTypeBeforeApplication[0],
    ) ||
    /\bpsx_type_t\b|->(?:kind|base)\b/.test(
      directTypeBeforeApplication[0],
    ) ||
    !/\bpsx_qual_type_layout_sizeof\s*\(/.test(
      directSizeofTypeName[0],
    ) ||
    /direct_type_before_application\(\s*base_type\s*,\s*runtime_application\s*\);[\s\S]{0,150}if\s*\(factor\s*<=\s*0\)\s*return\s+0/.test(
      directSizeofTypeName[0],
    ) ||
    !/op->is_incomplete_array\s*\|\|\s*op->array_len\s*<\s*0\s*\|\|\s*factor\s*<\s*0/.test(
      directSizeofTypeName[0],
    ) ||
    !/if\s*\(op->array_len\s*==\s*0\)\s*\{[^]*?factor\s*=\s*0\s*;/.test(
      directSizeofTypeName[0],
    ) ||
    !/if\s*\(factor\s*==\s*0\)\s*\{[^]*?psx_resolve_sizeof_qual_type_plan_in\s*\([^]*?queried_qual_type\s*,\s*1\s*,\s*0\s*,/.test(
      directSizeofTypeName[0],
    )) {
  throw new Error(
    "direct sizeof(type-name) must stay on canonical QualType edges and apply pointer declarators before rejecting an unsized function base",
  );
}
if (!/capture_direct_vla_typedef_bounds\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/__vla_typedef_bound_%d/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/bound->expression_id\s*=\s*reference_id/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\.runtime_application\s*=\s*[^]*?psx_semantic_type_table_contains_vla_array\s*\([^]*?decl_qual_type\.type_id[^]*?\?\s*&effective_application/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /if\s*\(\s*!type\s*\|\|\s*ps_type_contains_vla_array\(type\)\s*\)\s*return\s+0/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_expr_id_t\s*\*runtime_factor_ids\s*;/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /const\s+node_t\s*\*\*runtime_factors\s*;/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/base_resolution\.runtime_application/.test(
      directSizeofTypeName[0],
    ) ||
    !/ps_ctx_semantic_expression_in\s*\([^]*?runtime_factor_ids\[i\]/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/ps_ctx_find_typedef_name_at_in\s*\(/.test(
      typeNameResolutionSource,
    )) {
  throw new Error(
    "VLA typedef bounds must be captured once into Typed HIR and referenced by semantic expression ID outside canonical types",
  );
}
if (/\blower_aggregate_address_expression\s*\(/.test(
      castLoweringSource,
    ) ||
    /\blower_aggregate_address_expression\s*\(/.test(
      castLoweringHeader,
    )) {
  throw new Error(
    "aggregate address representation must be lowered while materializing Typed HIR, without rewriting the semantic node tree",
  );
}
if (!/\bbuild_direct_addressable_compound_literal\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/operand_syntax->kind\s*==\s*ND_COMPOUND_LITERAL/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /\blower_compound_literal_expression_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "compound literals must materialize directly into Typed HIR without AST replacement",
  );
}

const inPlaceLoweringOverwrites = [];
for (const file of sourceFiles.filter(
  (path) => path.startsWith("src/lowering/") && path.endsWith(".c"),
)) {
  const source = await readFile(file, "utf8");
  if (/\*\s*node\s*=\s*\*/.test(source)) {
    inPlaceLoweringOverwrites.push(file);
  }
}
if (inPlaceLoweringOverwrites.length) {
  throw new Error(
    "semantic lowering must return replacement roots instead of overwriting parse AST nodes:\n" +
      inPlaceLoweringOverwrites.sort().join("\n"),
  );
}

const recordDeclHeaderSource = await readFile(
  "src/semantic/record_decl.h",
  "utf8",
);
const recordDeclImplementationSource = await readFile(
  "src/semantic/record_decl.c",
  "utf8",
);
const removedMutableTypeApiNames = [
  "ps_type_new_in",
  "ps_type_new_integer_kind_in",
  "ps_type_new_integer_in",
  "ps_type_new_enum_in",
  "ps_type_new_record_in",
  "ps_type_new_floating_in",
  "ps_type_new_float_in",
  "ps_type_new_pointer_in",
  "ps_type_new_function_in",
  "ps_type_new_array_in",
  "ps_type_clone_in",
  "ps_type_clone_persistent",
  "ps_type_new_tag_in",
  "ps_type_normalize_scalar_identity",
  "ps_type_set_function_params_in",
  "ps_type_wrap_array_dims_in",
  "ps_type_apply_declarator_shape_in",
  "ps_type_apply_resolved_declarator_shape_in",
  "ps_type_adjust_parameter_type_in",
  "ps_type_complete_array",
  "ps_type_set_decl_spec_qualifiers",
  "ps_type_add_qualifiers",
  "ps_type_remove_qualifiers",
  "ps_type_remove_all_qualifiers_recursive",
  "ps_type_usual_arithmetic_result_for_target_in",
  "ps_type_integer_promotion_is_unsigned_for_target",
  "ps_type_usual_arithmetic_result_is_unsigned_for_target",
  "ps_type_binary_result_for_target_in",
  "ps_type_conditional_result_for_target_in",
];
const removedMutableTypeApiRe = new RegExp(
  `\\b(?:${removedMutableTypeApiNames.join("|")})\\b`,
);
const mutableTypeCompatibilityViolations = [];
for (const file of [...sourceFiles, "test/test_parser.c"]) {
  const source = await readFile(file, "utf8");
  if (/\bpsx_type_t\b|type_compatibility_view|parser_type_compatibility|parser\/type(?:_builder)?\.h/.test(source) ||
      removedMutableTypeApiRe.test(source))
    mutableTypeCompatibilityViolations.push(file);
}
if (allSourceFiles.some((file) =>
      /src\/parser\/(?:type|type_builder|type_fwd|type_owned_internal)\.[ch]$/.test(file)
    ) || mutableTypeCompatibilityViolations.length) {
  throw new Error(
    "mutable parser type compatibility must be absent:\n" +
      mutableTypeCompatibilityViolations.sort().join("\n"),
  );
}

const declaratorShapeBuilderSource = await readFile(
  "src/parser/declarator_shape_builder.h",
  "utf8",
);
const declaratorShapeSource = await readFile(
  "src/parser/declarator_shape.h",
  "utf8",
);
if (!/\bpsx_declarator_shape_t\b/.test(declaratorShapeSource) ||
    !/type_system\/type_ids\.h/.test(declaratorShapeSource) ||
    !/psx_qual_type_t\s*\*\s*function_param_qual_types\s*;/.test(
      declaratorShapeSource,
    ) ||
    /\bpsx_type_t\b|function_param_types/.test(declaratorShapeSource) ||
    /#[ \t]*include[^\n]*type\.h/.test(declaratorShapeSource)) {
  throw new Error(
    "declarator shape state must retain canonical parameter QualTypes without semantic type views",
  );
}
const declaratorShapeBuilderApiNames = [
  "ps_declarator_shape_init",
  "ps_declarator_shape_copy_in",
  "ps_declarator_shape_append_pointer_in",
  "ps_declarator_shape_append_array_in",
  "ps_declarator_shape_append_array_ex_in",
  "ps_declarator_shape_append_vla_array_in",
  "ps_declarator_shape_append_function_in",
  "ps_declarator_op_set_function_param_qual_types_in",
  "ps_declarator_shape_set_array_bound",
  "ps_declarator_op_set_variadic",
  "ps_declarator_shape_count_ops",
];
for (const functionName of declaratorShapeBuilderApiNames) {
  const name = new RegExp(`\\b${functionName}\\b`);
  if (!name.test(declaratorShapeBuilderSource)) {
    throw new Error(
      `${functionName} must be declared by declarator_shape_builder.h`,
    );
  }
}

const declaratorShapeBuilderApiRe = new RegExp(
  `\\b(?:${declaratorShapeBuilderApiNames.join("|")})\\b`,
);
const declaratorShapeBuilderUsers = new Set([
  "src/parser/declarator_shape_builder.h",
  "src/parser/declarator_shape_builder.c",
  "src/parser/declaration_syntax.c",
  "src/semantic/type_name_resolution.c",
  "src/semantic/function_parameter_resolution.c",
  "src/semantic/declaration_application.c",
  "src/semantic/parameter_declaration_resolution.c",
  "src/declaration_pipeline.c",
  "test/test_parser.c",
]);
const declaratorShapeBuilderViolations = [];
for (const file of [...sourceFiles, "test/test_parser.c"]) {
  const source = await readFile(file, "utf8");
  if (!declaratorShapeBuilderUsers.has(file) &&
      (declaratorShapeBuilderApiRe.test(source) ||
       /#[ \t]*include[^\n]*(?:[\/"]declarator_shape_builder\.h")/.test(
         source,
       ))) {
    declaratorShapeBuilderViolations.push(file);
  }
  if (/\b(?:psx_declarator_shape_append_array_dims|ps_declarator_shape_append_shape)\b/.test(
    source,
  )) {
    declaratorShapeBuilderViolations.push(
      `${file}:removed declarator shape API`,
    );
  }
  if (file !== "src/parser/declarator_shape_builder.c" &&
      /->(?:array_len|is_incomplete_array|is_vla_array|function_is_variadic)\s*=(?!=)/.test(
        source,
      )) {
    declaratorShapeBuilderViolations.push(
      `${file}:direct declarator shape state mutation`,
    );
  }
}
if (declaratorShapeBuilderViolations.length) {
  throw new Error(
    "declarator shape builders must stay inside syntax and semantic owners:\n" +
      declaratorShapeBuilderViolations.sort().join("\n"),
  );
}

const tagContextSource = await readFile(
  "src/parser/semantic_ctx.c",
  "utf8",
);
const semanticContextHeaderSource = await readFile(
  "src/parser/semantic_ctx.h",
  "utf8",
);
const recordLayoutHeaderSource = await readFile(
  "src/semantic/record_layout.h",
  "utf8",
);
if (/\bps_ctx_(?:get|find)_tag_member_info(?:_at_scope)?_in\s*\(/.test(
      tagContextSource,
    )) {
  throw new Error(
    "semantic context must not expose combined tag member declaration and layout queries",
  );
}
const compatibilityTagMemberViolations = [];
for (const file of parserSourceFiles) {
  const source = await readFile(file, "utf8");
  if (/\btag_member_info_t\b|tag_member_public\.h/.test(source)) {
    compatibilityTagMemberViolations.push(file);
  }
}
if (allSourceFiles.includes("src/parser/tag_member_public.h") ||
    compatibilityTagMemberViolations.length) {
  throw new Error(
    "combined compatibility member views must not exist in the parser:\n" +
      compatibilityTagMemberViolations.join("\n"),
  );
}
if (allSourceFiles.includes("src/parser/tag_public.h") ||
    allSourceFiles.includes("src/parser/tag_flat_cover.h") ||
    /#include\s+"(?:tag_public|tag_flat_cover)\.h"/.test(
      `${nodeUtilsHeaderSource}\n${nodeUtilsSource}`,
    )) {
  throw new Error(
    "unused public tag traversal compatibility headers must not return",
  );
}
if (/#include\s+"tag_member_public\.h"/.test(
      semanticContextHeaderSource,
    ) ||
    /\btag_member_info_t\b/.test(nodeUtilsHeaderSource)) {
  throw new Error(
    "compatibility tag member views must stay out of production parser headers and type implementation",
  );
}
const tagTypeStruct = tagContextSource.match(
  /struct tag_type_t\s*\{([\s\S]*?)\n\};/,
);
const tagMemberStruct = tagContextSource.match(
  /struct tag_member_t\s*\{([\s\S]*?)\n\};/,
);
const tagMemberDeclStruct = tagContextSource.match(
  /typedef struct tag_member_decl_t\s*\{([\s\S]*?)\}\s*tag_member_decl_t\s*;/,
);
const tagMemberLayoutDraftStruct = tagContextSource.match(
  /struct tag_member_layout_draft_t\s*\{([\s\S]*?)\n\};/,
);
const tagSizeLookupFunction = tagContextSource.match(
  /int\s+ps_ctx_get_tag_size_in\s*\([^]*?\n\}/,
);
const tagAlignLookupFunction = tagContextSource.match(
  /int\s+ps_ctx_get_tag_align_in\s*\([^]*?\n\}/,
);
const publishRecordLayoutFunction = tagContextSource.match(
  /int\s+ps_ctx_publish_record_layout_in\s*\([^]*?\n\}/,
);
const registerTagTypeFunction = tagContextSource.match(
  /int\s+ps_ctx_register_tag_type_in\s*\([^]*?\n\}/,
);
const tagDeclarationRequestStruct =
  tagDeclarationResolutionHeader.match(
    /typedef\s+struct\s*\{([^]*?)\}\s*psx_tag_declaration_resolution_request_t\s*;/,
  );
const refreshRecordDeclFunction = tagContextSource.match(
  /static\s+void\s+refresh_cached_record_decl\s*\([^]*?\n\}/,
);
const findRecordMemberFunction = tagContextSource.match(
  /bool\s+ps_ctx_find_record_member_in\s*\([^]*?\n\}/,
);
const registerTagMembersFunction = tagContextSource.match(
  /static\s+int\s+register_tag_members_for_owner_in\s*\([^]*?\n\}/,
);
const recordDeclStruct = recordDeclHeaderSource.match(
  /typedef struct psx_record_decl_t\s*\{([\s\S]*?)\}\s*psx_record_decl_t\s*;/,
);
const recordMemberDeclStruct = recordDeclHeaderSource.match(
  /typedef struct psx_record_member_decl_t\s*\{([\s\S]*?)\}\s*psx_record_member_decl_t\s*;/,
);
const recordMemberLayoutStruct = recordLayoutHeaderSource.match(
  /typedef struct\s*\{([\s\S]*?)\}\s*psx_record_member_layout_t\s*;/,
);
if (!publishRecordLayoutFunction ||
    !/\bcollect_tag_member_declarations_in\s*\(/.test(
      publishRecordLayoutFunction[0],
    ) ||
    !/\bfind_tag_member_layout_draft\s*\(/.test(
      publishRecordLayoutFunction[0],
    ) ||
    /\b(?:tag_member_info_t|get_tag_member_info_impl_in)\b/.test(
      publishRecordLayoutFunction[0],
    ) ||
    /record->members\s*\[[^\]]+\]\s*\.(?:offset|bit_offset|bit_width)/.test(
      publishRecordLayoutFunction[0],
    )) {
  throw new Error(
    "RecordLayout publication must pair declaration-order members with target layout drafts",
  );
}
if (!registerTagTypeFunction ||
    !tagDeclarationRequestStruct ||
    /\b(?:size|alignment)\s*;/.test(tagDeclarationRequestStruct[1]) ||
    /\bps_ctx_publish_record_layout_in\s*\(/.test(
      registerTagTypeFunction[0],
    ) ||
    /\bps_ctx_publish_record_layout_in\s*\(/.test(
      tagDeclarationResolutionSource,
    ) ||
    !/\bps_ctx_publish_record_layout_in\s*\(/.test(
      declarationApplicationSource,
    )) {
  throw new Error(
    "tag declaration must own RecordDecl state while aggregate layout is published explicitly",
  );
}
if (!findRecordMemberFunction ||
    !/\bpsx_scope_graph_lookup_declaration_in_scope\s*\(/.test(
      findRecordMemberFunction[0],
    ) ||
    !/\bPSX_NAMESPACE_MEMBER\b/.test(findRecordMemberFunction[0]) ||
    !/\bps_ctx_find_record_member_in\s*\([^;]*psx_record_id_t\s+record_id[^;]*int\s*\*\s*out_member_index[^;]*psx_record_member_decl_t\s*\*\s*out_declaration/s.test(
      aggregateRegistryHeader,
    ) ||
    /\bps_ctx_(?:get|find)_tag_member_in\s*\(|\bps_ctx_get_tag_member_count_in\s*\(|\bps_ctx_register_tag_members_in\s*\(/.test(
      `${aggregateRegistryHeader}\n${tagContextSource}`,
    )) {
  throw new Error(
    "record member name lookup must use the scope graph and legacy tag-name member query APIs must stay removed",
  );
}
if (!registerTagMembersFunction ||
    !/\bpsx_scope_graph_checkpoint_begin\s*\(/.test(
      registerTagMembersFunction[0],
    ) ||
    !/\bpsx_scope_graph_lookup_in_scope\s*\(/.test(
      registerTagMembersFunction[0],
    ) ||
    !/\bpsx_scope_graph_checkpoint_rollback\s*\(/.test(
      registerTagMembersFunction[0],
    ) ||
    !/\bpsx_scope_graph_checkpoint_commit\s*\(/.test(
      registerTagMembersFunction[0],
    ) ||
    /\b(?:strncmp|memcmp)\s*\(/.test(registerTagMembersFunction[0])) {
  throw new Error(
    "record member registration must use the scope graph as its sole duplicate-name authority and roll back atomically",
  );
}
if (/#include\s+"\.\.\/parser\//.test(
      `${recordDeclHeaderSource}\n${recordDeclImplementationSource}`,
    ) ||
    !recordMemberDeclStruct ||
    /\bdecl_type_table\b/.test(
      recordMemberDeclStruct[1],
    ) ||
    !/\bpsx_qual_type_t\s+decl_qual_type\s*;/.test(
      recordMemberDeclStruct[1],
    ) ||
    /\bconst\s+psx_type_t\s*\*/.test(recordMemberDeclStruct[1]) ||
    !/\bint\s+bit_width\s*;/.test(recordMemberDeclStruct[1]) ||
    /\b(?:offset|bit_offset)\s*;/.test(recordMemberDeclStruct[1]) ||
    !recordMemberLayoutStruct ||
    !/\bint\s+offset\s*;/.test(recordMemberLayoutStruct[1]) ||
    !/\bint\s+bit_offset\s*;/.test(recordMemberLayoutStruct[1]) ||
    /\bbit_width\s*;/.test(recordMemberLayoutStruct[1]) ||
    !recordDeclStruct ||
    !/\bconst\s+psx_record_member_decl_t\s*\*\s*members\s*;/.test(
      recordDeclStruct[1],
    ) ||
    !/\bunsigned\s+char\s+is_anonymous\s*;/.test(
      recordDeclStruct[1],
    ) ||
    /__anon_tag_/.test(declarationPipelineSource) ||
    !/leaf_record\s*&&\s*leaf_record->is_anonymous/.test(
      declarationPipelineSource,
    ) ||
    !refreshRecordDeclFunction ||
    !/members\s*\[\s*i\s*\]\s*=\s*\(psx_record_member_decl_t\)/.test(
      refreshRecordDeclFunction[0],
    )) {
  throw new Error(
    "semantic RecordDecl must own member declarations independently from parser types and target placement",
  );
}
if (!tagMemberStruct ||
    !/\btag_member_decl_t\s+declaration\s*;/.test(tagMemberStruct[1]) ||
    /\b(?:psx_record_member_layout_t|offset|bit_offset|decl_type)\b/.test(
      tagMemberStruct[1],
    ) ||
    !tagMemberDeclStruct ||
    !/\bconst\s+psx_semantic_type_table_t\s*\*\s*type_table\s*;/.test(
      tagMemberDeclStruct[1],
    ) ||
    !/\bpsx_qual_type_t\s+qual_type\s*;/.test(tagMemberDeclStruct[1]) ||
    /\bpsx_type_t\s*\*/.test(tagMemberDeclStruct[1]) ||
    /\b(?:offset|bit_offset)\s*;/.test(tagMemberDeclStruct[1]) ||
    !tagMemberLayoutDraftStruct ||
    !/\bconst\s+tag_member_t\s*\*\s*member\s*;/.test(
      tagMemberLayoutDraftStruct[1],
    ) ||
    !/\bag_data_layout_t\s+data_layout\s*;/.test(
      tagMemberLayoutDraftStruct[1],
    ) ||
    !/\bpsx_record_member_layout_t\s+placement\s*;/.test(
      tagMemberLayoutDraftStruct[1],
    ) ||
    !/\btag_member_layout_draft_t\s*\*\s*aggregate_member_layout_drafts\s*;/.test(
      tagContextSource,
    )) {
  throw new Error(
    "parser-owned record members must keep declarations separate from target layout",
  );
}
if (!recordDeclStruct ||
    !/\bpsx_record_id_t\s+record_id\s*;/.test(recordDeclStruct[1]) ||
    !/\bpsx_type_kind_t\s+record_kind\s*;/.test(recordDeclStruct[1]) ||
    /\btoken_kind_t\s+tag_kind\s*;/.test(recordDeclStruct[1]) ||
    !/\bunsigned\s+char\s+is_complete\s*;/.test(recordDeclStruct[1]) ||
    /\b(?:size|align)\s*;/.test(recordDeclStruct[1]) ||
    !/\bconst\s+psx_record_member_decl_t\s*\*\s*members\s*;/.test(
      recordDeclStruct[1],
    )) {
  throw new Error(
    "record declarations must have stable identity and explicit completeness",
  );
}

if (!tagTypeStruct ||
    /\b(?:size|align)\s*;/.test(tagTypeStruct[1]) ||
    /\b(?:int\s+)?member_count\s*;/.test(tagTypeStruct[1]) ||
    /\b(?:int|unsigned\s+char)\s+is_complete\s*;/.test(
      tagTypeStruct[1],
    ) ||
    !/\bunsigned\s+char\s+enum_is_complete\s*;/.test(tagTypeStruct[1]) ||
    !/\bpsx_record_decl_t\s*\*\s*record_decl\s*;/.test(tagTypeStruct[1]) ||
    !tagSizeLookupFunction ||
    !tagAlignLookupFunction ||
    !/\bpsx_record_layout_table_lookup\s*\(/.test(
      tagSizeLookupFunction[0],
    ) ||
    !/\bpsx_record_layout_table_lookup\s*\(/.test(
      tagAlignLookupFunction[0],
    ) ||
    !/ps_ctx_intern_record_qual_type_in\s*\([^]*?tag->record_decl->record_id\s*\)/.test(
      tagContextSource,
    )) {
  throw new Error(
    "RecordDecl must solely own struct/union identity, completeness, and member count while target layout stays in RecordLayoutTable",
  );
}

const typeLayoutSource = await readFile("src/type_layout.c", "utf8");
const typeLayoutHeader = await readFile("src/type_layout.h", "utf8");
const staticHirInitializerHeader = await readFile(
  "src/lowering/static_hir_initializer.h",
  "utf8",
);
const typeShapeLoweringSources = (await Promise.all([
  "src/lowering/hir_ir_builder.c",
  "src/lowering/mir_type_lowering.c",
  "src/lowering/function_type_lowering.c",
  "src/lowering/abi_lowering.c",
  "src/lowering/hir_ir_expression.c",
  "src/lowering/hir_ir_call.c",
  "src/lowering/hir_ir_aggregate.c",
  "src/lowering/ir_symbol_lowering.c",
  "src/lowering/static_hir_initializer.c",
  "src/lowering/translation_unit_data_lowering.c",
].map((path) => readFile(path, "utf8")))).join("\n");
const mirTypeLoweringHeader = await readFile(
  "src/lowering/mir_type_lowering.h",
  "utf8",
);
const mirTypeLoweringSource = await readFile(
  "src/lowering/mir_type_lowering.c",
  "utf8",
);
const typeIdLayoutFunction = typeLayoutSource.match(
  /int\s+psx_type_layout_of\s*\([^]*?\n\}/,
);
if (!/\bpsx_type_layout_of\s*\(/.test(typeLayoutSource) ||
    !/\bpsx_type_layout_sizeof\s*\(/.test(typeLayoutHeader) ||
    !/\bpsx_type_layout_alignof\s*\(/.test(typeLayoutHeader) ||
    !/\bpsx_qual_type_layout_of\s*\(/.test(typeLayoutHeader) ||
    !/\bpsx_qual_type_layout_sizeof\s*\(/.test(typeLayoutHeader) ||
    !/\bpsx_qual_type_layout_alignof\s*\(/.test(typeLayoutHeader) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(typeLayoutSource) ||
    /\bpsx_type_compatibility_canonical_view_for\s*\(/.test(typeLayoutSource) ||
    /parser\/type\.h/.test(typeLayoutSource) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(typeLayoutSource) ||
    /\baggregate_definition\b/.test(typeLayoutSource) ||
    /\bps_type_(?:layout_of|sizeof_for_target|alignof_for_target)\s*\(/.test(
      `${typeLayoutHeader}\n${typeLayoutSource}`,
    ) ||
    /\btype\s*->\s*(?:size|align)\b/.test(typeLayoutSource) ||
    !typeIdLayoutFunction ||
    !/\bconst\s+psx_record_layout_table_t\s*\*/.test(
      typeIdLayoutFunction[0],
    ) ||
    !/\bpsx_qual_type_layout_of\s*\(/.test(typeIdLayoutFunction[0]) ||
    !/\blayout_of_qual_type_recursive\s*\(/.test(typeLayoutSource) ||
    !/\bapply_atomic_layout\s*\([^]*?ag_data_layout_atomic_promoted_max_size\s*\([^]*?ag_data_layout_atomic_max_alignment\s*\(/.test(
      typeLayoutSource,
    ) ||
    /\bps_type_layout_of\s*\(/.test(typeIdLayoutFunction[0]) ||
    /\bps_type_(?:layout_of_id_with_records|(?:size|align)of_id_with_records|(?:size|align)of_id_for_target)\s*\(/.test(
      `${typeLayoutHeader}\n${typeLayoutSource}`,
    ) ||
    /out->is_complete\s*=\s*type->aggregate_definition->align/.test(
      typeLayoutSource,
    ) ||
    /aggregate_definition\s*->\s*(?:size|align)/.test(
      typeLayoutSource,
    )) {
  throw new Error(
    "layout must resolve QualType with an explicit target and get record completeness from RecordLayoutTable",
  );
}
if (/alignment\s*>\s*8\)\s*alignment\s*=\s*8/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/psx_qual_type_layout_sizeof\s*\([^]*?psx_qual_type_layout_alignof\s*\(/.test(
      aggregateMemberResolutionSource,
    )) {
  throw new Error(
    "aggregate member placement must retain QualType layout and target-provided natural alignment",
  );
}
if (!/\bpsx_record_layout_table_lookup\s*\(/.test(typeLayoutSource) ||
    /aggregate_definition\s*->\s*(?:size|align)/.test(
      typeLayoutSource.match(
        /static\s+int\s+layout_non_array_with_records\s*\([^]*?\n\}/,
      )?.[0] ?? "",
    )) {
  throw new Error(
    "record ABI layout must be an explicit RecordLayoutTable input separate from TypeId",
  );
}
for (const path of allSourceFiles) {
  const source = await readFile(path, "utf8");
  if (/\bps_type_(?:layout_of_id|sizeof_id|alignof_id)\s*\(/.test(source) ||
      /\bps_type_(?:layout_of_id_with_records|(?:size|align)of_id_with_records|(?:size|align)of_id_for_target)\s*\(/.test(source)) {
    throw new Error(
      `${path} must use the canonical psx_type_layout API`,
    );
  }
}
if (/parser\/type\.h/.test(typeShapeLoweringSources) ||
    /\bpsx_type_compatibility_canonical_view_for\s*\(/.test(typeShapeLoweringSources) ||
    /\bpsx_type_t\b/.test(typeShapeLoweringSources) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      typeShapeLoweringSources,
    ) ||
    !/\bir_mir_integer_promotion_is_unsigned\s*\(/.test(
      typeShapeLoweringSources,
    ) ||
    !/\bir_mir_usual_arithmetic_result_is_unsigned\s*\(/.test(
      typeShapeLoweringSources,
    ) ||
    !/\bpsx_integer_conversion_from_shape\s*\(/.test(
      mirTypeLoweringSource,
    ) ||
    !/\bpsx_integer_promotion_for_data_layout\s*\(/.test(
      mirTypeLoweringSource,
    ) ||
    !/\bpsx_usual_integer_conversion_for_data_layout\s*\(/.test(
      mirTypeLoweringSource,
    ) ||
    /\b(?:integer_rank|integer_size_for_rank|scalar_kind_for_rank|promoted_integer)\s*\(/.test(
      mirTypeLoweringSource,
    ) ||
    !/const\s+struct\s+ag_data_layout_t\s*\*data_layout\s*;/.test(
      mirTypeLoweringHeader,
    ) ||
    /\bag_target_info_t\b|\bag_target_info_(?:pointer_size|pointer_alignment|scalar_size|scalar_alignment)\s*\(/.test(
      `${mirTypeLoweringHeader}\n${mirTypeLoweringSource}`,
    )) {
  throw new Error(
    "type and ABI lowering must consume TypeId shape and target layout without parser compatibility types",
  );
}
if (/parser\/type\.h/.test(
      `${staticHirInitializerHeader}\n${staticHirInitializerSource}`,
    ) ||
    /\bpsx_type_t\b/.test(
      `${staticHirInitializerHeader}\n${staticHirInitializerSource}`,
    ) ||
    !/\bpsx_type_id_t\s+type_id\b/.test(staticHirInitializerHeader) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      staticHirInitializerSource,
    )) {
  throw new Error(
    "static HIR initializer lowering must consume canonical TypeId shapes without parser compatibility types",
  );
}
if (/\bpsx_ctx_get_type_info\s*\(/.test(tagContextSource) ||
    /\bps_ctx_scalar_type_size\s*\(/.test(tagContextSource) ||
    !/\bpsx_ctx_get_type_token_layout_in\s*\(/.test(tagContextSource) ||
    !/\bag_data_layout_scalar_size\s*\(/.test(tagContextSource) ||
    !/\bag_data_layout_scalar_alignment\s*\(/.test(tagContextSource) ||
    !/\bpsx_ctx_find_typedef_layout_in\s*\(/.test(tagContextSource) ||
    !/resolve_parsed_alignas_type_name\s*\([^]*?psx_qual_type_layout_alignof\s*\([^]*?ps_ctx_data_layout\s*\(/.test(
      declarationApplicationSource,
    ) ||
    !/resolve_parsed_alignas_expression\s*\([^]*?psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts\s*\(/.test(
      declarationApplicationSource,
    )) {
  throw new Error(
    "declaration alignment must resolve through Typed HIR and explicit DataLayout rather than parser-side constant evaluation",
  );
}

const semanticTypeTraversalSource = await readFile(
  "src/semantic/type_identity.c",
  "utf8",
);
const localDeclarationResolutionTraversalSource = await readFile(
  "src/semantic/local_declaration_resolution.c",
  "utf8",
);
for (const functionName of [
  "psx_semantic_type_table_contains_vla_array",
  "psx_semantic_type_table_array_leaf",
  "psx_semantic_type_table_pointee_value",
]) {
  if (!new RegExp(`\\b${functionName}\\s*\\(`).test(
        semanticTypeTraversalSource,
      )) {
    throw new Error(
      "derived type traversal must be owned by the semantic TypeId graph",
    );
  }
}
const semanticTypeIdTraversalImplementation =
  semanticTypeTraversalSource.slice(
    semanticTypeTraversalSource.indexOf("static psx_qual_type_t related_type"),
  );
if (/\bpsx_type_compatibility_canonical_view_for\s*\(/.test(
      semanticTypeIdTraversalImplementation,
    )) {
  throw new Error(
    "semantic TypeId graph traversal must inspect immutable TypeShape relations without materializing parser type views",
  );
}
for (const source of [
  localDeclarationPlanSource,
  localDeclarationResolutionTraversalSource,
]) {
  if (/\bpsx_semantic_type_table_find\s*\(/.test(source)) {
    throw new Error(
      "local declaration planning and resolution must not reverse-map type pointers",
    );
  }
}

for (const [name, header, source, functionName] of [
  ["local", localDeclarationPlanHeader, localDeclarationPlanSource,
   "psx_plan_local_storage_for_type_id"],
  ["parameter", parameterDeclarationPlanHeader,
   parameterDeclarationPlanSource,
   "psx_plan_parameter_storage_for_type_id"],
]) {
  const signature = new RegExp(
    `\\b${functionName}\\s*\\(\\s*const\\s+psx_semantic_type_table_t\\s*\\*[^,]*,\\s*const\\s+psx_record_layout_table_t\\s*\\*[^,]*,\\s*psx_type_id_t\\s+type_id`,
  );
  if (!signature.test(header) || !signature.test(source) ||
      !/\bpsx_(?:qual_type|type)_layout_sizeof\s*\(/.test(source) ||
      !/\bpsx_semantic_type_table_describe\s*\(/.test(source) ||
      /\bpsx_type_compatibility_canonical_view_for\s*\(/.test(source) ||
      /#include\s+"\.\.\/parser\/type\.h"/.test(source) ||
      (name === "local" &&
       (!/\bpsx_semantic_type_table_contains_vla_array\s*\(/.test(source) ||
        !/\bpsx_plan_local_storage_for_qual_type\s*\(/.test(header) ||
        !/\bpsx_qual_type_layout_sizeof\s*\(/.test(source) ||
        !/\bpsx_qual_type_layout_alignof\s*\(/.test(source))) ||
      /\bps_type_(?:size|align)of_id_for_target\s*\(/.test(source) ||
      /\bps_type_(?:size|align)of_for_target\s*\(/.test(source) ||
      /\bag_target_info_t\b/.test(`${header}\n${source}`) ||
      !/const\s+ag_data_layout_t\s*\*data_layout/.test(header) ||
      (name === "local" && /storage_size\s*>=/.test(source)) ||
      (name === "parameter" &&
       (/\bir_abi_target_policy_t\b|\bir_abi_policy_/.test(
          `${header}\n${source}`,
        ) ||
        /#include\s+"abi_target_policy\.h"/.test(source) ||
        /PSX_PARAMETER_STORAGE_AGGREGATE_BYREF|\bis_byref\b/.test(
          `${header}\n${source}`,
        ))) ||
      /\bpsx_plan_(?:local|parameter)_storage_for_target\s*\(/.test(
        header,
      )) {
    throw new Error(
      `${name} storage planning must derive source-object layout from TypeId without target ABI policy`,
    );
  }
}

for (const [name, source, requiredLayoutCall] of [
  ["pointer arithmetic", hirIrBuilder, /\bpsx_qual_type_layout_sizeof\s*\(/],
  ["subscript", hirIrBuilder, /\bpsx_qual_type_layout_sizeof\s*\(/],
  ["direct Typed HIR initializer", syntaxTypedHirResolutionSource, /\bpsx_qual_type_layout_sizeof\s*\(/],
  ["VLA", vlaLoweringSource, /\bpsx_qual_type_layout_sizeof\s*\(/],
  ["static HIR initializer", staticHirInitializerSource, /\bpsx_qual_type_layout_sizeof\s*\(/],
  ["translation unit data", translationUnitDataLoweringSource, /\bpsx_qual_type_layout_sizeof\s*\(/],
]) {
  if (!requiredLayoutCall.test(source) ||
      /\bps_type_(?:size|align)of_for_target\s*\(/.test(source)) {
    throw new Error(
      `${name} lowering must retain QualType while obtaining target layout`,
    );
  }
}
const automaticLocalPipeline = declarationPipelineSource.match(
  /int\s+psx_begin_automatic_local_declaration_hir_pipeline\s*\([^]*?\n\}/,
);
if (!automaticLocalPipeline ||
    !/\bpsx_qual_type_t\s+declaration_identity\s*=\s*request->type\s*;/.test(
      automaticLocalPipeline[0],
    ) ||
    /\bps_ctx_intern_qual_type_in\s*\(/.test(
      automaticLocalPipeline[0],
    ) ||
    !/\bpsx_resolve_decl_qual_type\s*\([^]*?\bpsx_type_layout_sizeof\s*\(/.test(
      parameterDeclarationResolutionSource,
    ) ||
    /\bpsx_plan_parameter_storage_for_type_id\s*\(/.test(
      parameterDeclarationResolutionSource,
    ) ||
    !/\bpsx_plan_parameter_storage_for_type_id\s*\(/.test(
      parameterLoweringSource,
    ) ||
    /\bir_abi_target_policy_for\s*\(|#include\s+"abi_target_policy\.h"/.test(
      parameterLoweringSource,
    )) {
  throw new Error(
    "parameter source-object storage planning must occur after TypeId resolution without pre-MIR ABI classification",
  );
}
if (/\bpsx_decl_parse_initializer_for_var_in_contexts\s*\(/.test(
      `${parserDeclHeader}\n${parserDeclSource}`,
    ) ||
    /\bps_decl_bind_initializer_for_var_in\s*\(/.test(
      `${parserDeclHeader}\n${parserDeclSource}`,
    ) ||
    /\bpsx_node_new_raw_decl_initializer_in\s*\(/.test(
      parserDeclSource,
    ) ||
    /\bpsx_bind_local_initializer_target_in\s*\(/.test(
      declarationPipelineSource,
    ) ||
    !/build_direct_flat_initializer\s*\(/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "local initializer binding must lower directly into Typed HIR without compatibility AST targets",
  );
}
if (!/\bconst\s+psx_semantic_type_table_t\s*\*\s*semantic_types\s*;/.test(
      loweringRuntimeHeader,
    ) ||
    !/\bconst\s+psx_record_layout_table_t\s*\*\s*record_layouts\s*;/.test(
      loweringRuntimeHeader,
    ) ||
    /\bpsx_semantic_context_t\b/.test(loweringRuntimeHeader) ||
    /#include\s+"\.\.\/parser\/semantic_ctx\.h"/.test(
      loweringRuntimeHeader,
    )) {
  throw new Error(
    "lowering context must receive the semantic type table without owning parser semantic state",
  );
}
const targetSensitiveLoweringSources = [
  loweringRuntimeSource,
  explicitDiagnosticCastLoweringSource,
  declarationPipelineSource,
  hirIrBuilder,
].join("\n");
if (!/\bps_lowering_type_id_size\s*\(/.test(loweringRuntimeHeader) ||
    !/\bps_lowering_type_id_alignment\s*\(/.test(loweringRuntimeHeader) ||
    /\bps_lowering_type_(?:id|size|deref_size|alignment)\s*\(/.test(
      `${loweringRuntimeHeader}\n${loweringRuntimeSource}`,
    ) ||
    !/\bpsx_type_layout_sizeof\s*\(/.test(
      loweringRuntimeSource,
    ) ||
    !/\bps_lowering_type_id_size\s*\(/.test(
      declarationPipelineSource,
    ) ||
    /\bps_lowering_type_(?:id|size|deref_size|alignment)\s*\(/.test(
      declarationPipelineSource,
    ) ||
    !/\bpsx_type_layout_alignof\s*\(/.test(
      loweringRuntimeSource,
    ) ||
    !/\bpsx_type_layout_sizeof\s*\(/.test(hirIrBuilder) ||
    /\bps_type_sizeof\s*\(/.test(targetSensitiveLoweringSources)) {
  throw new Error(
    "target-sensitive lowering must resolve C type layout through TypeId, RecordLayoutTable, and TargetSpec",
  );
}
const targetInfoHeaderSource = await readFile("src/target_info.h", "utf8");
const targetInfoImplementationSource = await readFile(
  "src/target_info.c",
  "utf8",
);
const recordLayoutImplementationSource = await readFile(
  "src/semantic/record_layout.c",
  "utf8",
);
const layoutBoundarySource = [
  typeLayoutHeader,
  typeLayoutSource,
  recordLayoutHeaderSource,
  recordLayoutImplementationSource,
].join("\n");
const typeIdentityImplementationSource = await readFile(
  "src/semantic/type_identity.c",
  "utf8",
);
if (!/typedef\s+struct\s+ag_data_layout_t\s*\{[^]*?\bscalar\s*\[\s*AG_TARGET_SCALAR_COUNT\s*\][^]*?\}\s*ag_data_layout_t\s*;/.test(targetInfoHeaderSource) ||
    !/\bag_data_layout_t\s+data_layout\s*;/.test(targetInfoHeaderSource) ||
    !/\bpointer_alignment\s*;/.test(targetInfoHeaderSource) ||
    !/\bag_data_layout_equal\s*\(/.test(targetInfoHeaderSource) ||
    !/\bag_target_info_data_layout\s*\(/.test(targetInfoHeaderSource) ||
    !/\bag_data_layout_scalar_size\s*\(/.test(targetInfoHeaderSource) ||
    !/\bag_data_layout_scalar_alignment\s*\(/.test(targetInfoHeaderSource) ||
    /\bag_target_info_(?:pointer_size|pointer_alignment|scalar_size|scalar_alignment)\s*\(/.test(
      targetInfoHeaderSource + targetInfoImplementationSource,
    ) ||
    !/\bag_target_info_equal\s*\(/.test(targetInfoHeaderSource) ||
    !/layout\s*&&\s*layout->pointer_size\s*>\s*0/.test(
      targetInfoImplementationSource,
    ) ||
    /target->pointer_size\s*==\s*4\s*\?\s*4\s*:\s*8/.test(
      targetInfoImplementationSource,
    ) ||
    /\bag_target_(?:pointer_size|set_pointer_size)\s*\(/.test(
      targetInfoHeaderSource + targetInfoImplementationSource,
    ) ||
    /\bdefault_target\b/.test(targetInfoImplementationSource) ||
    !/\blayout_scalar\s*\(/.test(await readFile("src/type_layout.c", "utf8")) ||
    !/\bAG_TARGET_SCALAR_FLOAT_COMPLEX\b/.test(targetInfoImplementationSource) ||
    !/\bag_data_layout_equal\s*\(/.test(recordLayoutImplementationSource) ||
    /\bag_target_info_t\b/.test(layoutBoundarySource) ||
    /\bag_target_info_(?:pointer|scalar)_(?:size|alignment)\s*\(/.test(
      typeLayoutSource,
    ) ||
    !/psx_type_layout_of\s*\([^]*?const\s+ag_data_layout_t\s*\*data_layout/.test(
      typeLayoutHeader,
    ) ||
    !/psx_record_layout_table_(?:define|lookup)\s*\([^]*?const\s+ag_data_layout_t\s*\*data_layout/.test(
      recordLayoutHeaderSource,
    ) ||
    /\bag_target_info_t\s+target\s*;/.test(recordLayoutHeaderSource) ||
    !/\bag_data_layout_t\s+data_layout\s*;/.test(recordLayoutHeaderSource) ||
    /\bps_type_clear_(?:cached_layout|record_layout_cache)\s*\(/.test(
      typeIdentityImplementationSource +
        await readFile("src/semantic/declaration_resolution.c", "utf8"),
    )) {
  throw new Error(
    "TypeLayout and RecordLayout must consume DataLayout without ABI policy or semantic type caches",
  );
}
for (const [name, source] of [
  ["cast", explicitDiagnosticCastLoweringSource],
  ["HIR IR builder", hirIrBuilder],
]) {
  if (/\bps_node_(?:type|storage_type|deref)_size\s*\(/.test(source)) {
    throw new Error(
      `${name} lowering must not read target layout cached on parser nodes`,
    );
  }
}

const typeIdCanonicalSignatureSource = await readFile(
  "src/type_signature.c",
  "utf8",
);
if (!/\bpsx_type_shape_t\b/.test(integerConversionHeader) ||
    !/\bconst\s+ag_data_layout_t\s*\*data_layout\b/.test(
      integerConversionHeader,
    ) ||
    /parser\/|\bpsx_type_t\b|\bps_ctx_|\bps_type_[A-Za-z0-9_]*\s*\(|->(?:size|align)\b/.test(
      `${integerConversionHeader}\n${integerConversionSource}`,
    ) ||
    !/\bag_data_layout_scalar_size\s*\(/.test(
      integerConversionSource,
    ) ||
    /\bag_target_info_t\b|\bag_target_info_(?:scalar_size|data_layout)\s*\(/.test(
      integerConversionSource,
    ) ||
    !/\bpsx_integer_conversion_from_shape\s*\(/.test(
      integerConversionSource,
    ) ||
    !/\bpsx_integer_promotion_for_data_layout\s*\(/.test(
      integerConversionSource,
    ) ||
    !/\bpsx_integer_conversion_size_for_data_layout\s*\(/.test(
      integerConversionSource,
    ) ||
    !/\bpsx_usual_integer_conversion_for_data_layout\s*\(/.test(
      integerConversionSource,
    ) ||
    !/\bpsx_usual_integer_conversion_for_data_layout\s*\([^]*?ps_ctx_data_layout\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/\bpsx_usual_integer_conversion_for_data_layout\s*\(/.test(
      mirTypeLoweringSource,
    ) ||
    !/\bpsx_integer_conversion_from_shape\s*\(/.test(
      typeIdentityImplementationSource,
    ) ||
    /\bsemantic_integer_rank\s*\(/.test(typeIdentityImplementationSource) ||
    !/\bpsx_integer_conversion_from_shape\s*\(/.test(
      typeIdCanonicalSignatureSource,
    ) ||
    !/\bpsx_integer_conversion_size_for_data_layout\s*\(/.test(
      typeIdCanonicalSignatureSource,
    ) ||
    /\b(?:integer_rank|integer_target_kind)\s*\(/.test(
      typeIdCanonicalSignatureSource,
    ) ||
    !/^TYPE_SYSTEM_SRCS=\$\(wildcard src\/type_system\/\*\.c\)$/m.test(
      makefileSource,
    ) ||
    !/\$\(TYPE_SYSTEM_SRCS\)/.test(makefileSource) ||
    !/\bpsx_resolve_binary_result_qual_type_in\s*\([^]*?usual_arithmetic_result\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/\bpsx_resolve_conditional_result_qual_type_in\s*\([^]*?usual_arithmetic_result\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/\bpsx_resolve_conditional_result_qual_type_in\s*\([^]*?psx_resolve_value_decay_qual_type_in\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/\bpsx_resolve_conditional_result_qual_type_in\s*\([^]*?pointer_types_are_compatible\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/\bpsx_resolve_conditional_result_qual_type_in\s*\([^]*?composite_array_type\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/left_atomic\s*!=\s*right_atomic/.test(
      expressionOperandResolutionSource,
    ) ||
    !/allow_array_element_qualifier_difference/.test(
      expressionOperandResolutionSource,
    ) ||
    !/pointer_types_are_compatible\s*\([^;]*?1\s*,\s*1\s*,\s*0\s*,\s*0\s*\)/.test(
      expressionOperandResolutionSource,
    ) ||
    !/pointer_types_are_compatible\s*\([^;]*?1\s*,\s*1\s*,\s*0\s*,\s*1\s*\)/.test(
      expressionOperandResolutionSource,
    ) ||
    !/common_base\.qualifiers\s*&=\s*~PSX_TYPE_QUALIFIER_ATOMIC/.test(
      expressionOperandResolutionSource,
    )) {
  throw new Error(
    "integer promotion and usual arithmetic conversion must have one TypeShape and DataLayout source of truth",
  );
}
if (/parser\/type\.h|\bpsx_type_compatibility_canonical_view_for\s*\(/.test(
      typeIdCanonicalSignatureSource,
    ) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      typeIdCanonicalSignatureSource,
    ) ||
    !/\bag_data_layout_scalar_size\s*\(/.test(
      typeIdCanonicalSignatureSource,
    ) ||
    /\bag_target_info_t\b|\bag_target_info_scalar_size\s*\(/.test(
      typeIdCanonicalSignatureSource,
    ) ||
    !/\bpsx_format_canonical_type_signature\s*\([^]*?ag_target_info_data_layout\s*\(\s*options->target\s*\)/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "canonical C signatures must derive widths from explicit DataLayout without depending on TargetInfo",
  );
}
const semanticTypeIdentityHeader = await readFile(
  "src/semantic/type_identity.h",
  "utf8",
);
const typeIdsHeader = await readFile(
  "src/type_system/type_ids.h",
  "utf8",
);
const semanticTypeIdentitySource = await readFile(
  "src/semantic/type_identity.c",
  "utf8",
);
const semanticTypeIdentityInternalHeader = await readFile(
  "src/semantic/type_identity_internal.h",
  "utf8",
);
const semanticTypeShapeHeader = await readFile(
  "src/type_system/type_shape.h",
  "utf8",
);
const continuationSyntaxValidationSource = await readFile(
  "src/semantic/continuation_syntax_validation.c",
  "utf8",
);
const qualTypeStruct = typeIdsHeader.match(
  /typedef\s+struct\s*\{([^]*?)\}\s*psx_qual_type_t\s*;/,
);
if (!qualTypeStruct ||
    !/\bpsx_type_id_t\s+type_id\s*;/.test(qualTypeStruct[1]) ||
    !/\bpsx_type_qualifiers_t\s+qualifiers\s*;/.test(qualTypeStruct[1]) ||
    !/#include\s+"\.\.\/type_system\/type_ids\.h"/.test(
      semanticTypeIdentityHeader,
    ) ||
    /parser\/type\.h/.test(semanticTypeIdentityHeader) ||
    /\bpsx_type_t\b|\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      semanticTypeIdentityHeader,
    ) ||
    /#include\s+"\.\.\/semantic\/type_identity\.h"/.test(astSource) ||
    /parser\/type\.h|\bps_type_[A-Za-z0-9_]*\s*\(/.test(
      semanticTypeIdentitySource,
    ) ||
    /\bpsx_semantic_type_table_(?:intern|find)\s*\(/.test(
      semanticTypeIdentityHeader,
    ) ||
    /#include\s+"\.\.\/(?:target_info|type_layout)\.h"/.test(
      semanticTypeIdentitySource,
    ) ||
    /->(?:size|align)\b/.test(semanticTypeIdentitySource)) {
  throw new Error(
    "QualType must pair an interned TypeId with qualifiers, independent of target layout",
  );
}
if (!/PSX_TYPE_QUALIFIER_RESTRICT\s*=\s*1u\s*<<\s*3/.test(
      typeIdsHeader,
    ) ||
    !/is_restrict_qualified\s*:\s*1/.test(declaratorShapeSource) ||
    !/TK_RESTRICT[^]*?\.is_restrict\s*=\s*1/.test(
      declaratorSyntaxSource,
    ) ||
    !/op->is_restrict_qualified[^]*?PSX_TYPE_QUALIFIER_RESTRICT/.test(
      declarationResolutionSource,
    ) ||
    !/PSX_TYPE_QUALIFIER_RESTRICT/.test(semanticTypeIdentitySource) ||
    !/type\.qualifiers\s*&\s*PSX_TYPE_QUALIFIER_RESTRICT[^]*?write_literal\(writer,\s*"R"\)/.test(
      typeIdCanonicalSignatureSource,
    )) {
  throw new Error(
    "restrict must survive declarator parsing as a canonical QualType qualifier",
  );
}
const semanticTypeEntry = semanticTypeIdentitySource.match(
  /typedef\s+struct\s*\{([^]*?)\}\s*psx_semantic_type_entry_t\s*;/,
);
if (!semanticTypeEntry ||
    !/psx_type_shape_t\s+shape\s*;/.test(semanticTypeEntry[1]) ||
    !/psx_qual_type_t\s+base_type\s*;/.test(semanticTypeEntry[1]) ||
    !/psx_qual_type_t\s*\*\s*parameter_types\s*;/.test(
      semanticTypeEntry[1],
    ) ||
    /\bpsx_type_t\b|qualified_views|materializ/.test(semanticTypeEntry[1]) ||
    /type_builder\.h|\bps_type_(?:clone|normalize_scalar_identity|remove_all_qualifiers_recursive)\b/.test(
      semanticTypeIdentitySource,
    ) ||
    !/table->entries\[id\]\.shape\s*=\s*owned_shape\s*;/.test(
      semanticTypeIdentitySource,
    ) ||
    !/table->entries\[id\]\.base_type\s*=\s*base_type\s*;/.test(
      semanticTypeIdentitySource,
    ) ||
    !/table->entries\[id\]\.parameter_types\s*=\s*owned_parameters\s*;/.test(
      semanticTypeIdentitySource,
    ) ||
    /\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      semanticTypeIdentitySource,
    ) ||
    !/psx_semantic_type_table_record_member\s*\([^]*?psx_record_decl_table_lookup\s*\([^]*?record->members\[member_index\]\.decl_qual_type/.test(
      semanticTypeIdentitySource,
    ) ||
    !/\bpsx_semantic_type_table_find_shape\s*\(/.test(
      semanticTypeIdentitySource,
    ) ||
    !/\bpsx_semantic_type_table_intern_shape\s*\(/.test(
      semanticTypeIdentitySource,
    ) ||
    !/\bpsx_semantic_type_table_find_shape\s*\(/.test(
      semanticTypeIdentityInternalHeader,
    ) ||
    !/\bpsx_semantic_type_table_intern_shape\s*\(/.test(
      semanticTypeIdentityInternalHeader,
    ) ||
    /\bps_type_clone_for_identity_in\s*\(/.test(
      semanticTypeIdentitySource,
    ) ||
    !/\bpsx_semantic_type_table_bind_record_decls\s*\(/.test(
      semanticTypeIdentityHeader,
    ) ||
    !/\bpsx_semantic_type_table_bind_record_decls\s*\(/.test(
      parserSemanticContextImplementation,
    ) ||
    !/psx_type_shape_t\s+shape\s*;/.test(semanticTypeIdentitySource) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      semanticTypeIdentityHeader,
    ) ||
    !/\bpsx_semantic_type_table_intern_array_of\s*\(/.test(
      semanticTypeIdentityHeader,
    ) ||
    !/\bpsx_semantic_type_table_qual_type_is_valid\s*\(/.test(
      semanticTypeIdentityHeader,
    ) ||
    !/psx_semantic_type_table_qual_type_is_valid\s*\([^]*?type\.qualifiers\s*&\s*~supported[^]*?semantic_type_id_is_valid\s*\(/.test(
      semanticTypeIdentitySource,
    ) ||
    !/typedef\s+struct\s*\{[^]*?\}\s*psx_type_shape_t\s*;/.test(
      semanticTypeShapeHeader,
    ) ||
    /\b(?:qualifiers|size|alignment)\s*;/.test(semanticTypeShapeHeader) ||
    !/\brecord_tag_name\s*;/.test(semanticTypeShapeHeader) ||
    !/\benum_tag_name\s*;/.test(semanticTypeShapeHeader) ||
    !/\benum_decl_id\s*;/.test(
      semanticTypeShapeHeader,
    ) ||
    /\benum_tag_scope_depth_p1\s*;/.test(
      semanticTypeShapeHeader,
    ) ||
    !/canonical->enum_decl_id\s*!=\s*PSX_DECL_ID_INVALID[^]*?canonical->enum_decl_id\s*==\s*candidate->enum_decl_id/.test(
      semanticTypeIdentitySource,
    )) {
  throw new Error(
    "semantic TypeId shape must own target-independent identity, use scope DeclId for enum identity, and resolve record relations through RecordDeclTable",
  );
}
for (const [contextInterner, tableInterner] of [
  ["ps_ctx_intern_integer_qual_type_in",
   "psx_semantic_type_table_intern_integer"],
  ["ps_ctx_intern_floating_qual_type_in",
   "psx_semantic_type_table_intern_floating"],
  ["ps_ctx_intern_void_qual_type_in",
   "psx_semantic_type_table_intern_void"],
  ["ps_ctx_intern_enum_qual_type_in",
   "psx_semantic_type_table_intern_enum"],
  ["ps_ctx_intern_record_qual_type_in",
   "psx_semantic_type_table_intern_record"],
  ["ps_ctx_intern_pointer_to_qual_type_in",
   "psx_semantic_type_table_intern_pointer_to"],
  ["ps_ctx_intern_array_of_qual_type_in",
   "psx_semantic_type_table_intern_array_of"],
  ["ps_ctx_intern_function_qual_type_in",
   "psx_semantic_type_table_intern_function"],
]) {
  const implementation = parserSemanticContextImplementation.match(
    new RegExp(
      `psx_qual_type_t\\s+${contextInterner}\\s*\\([^]*?\\n\\}`,
    ),
  );
  if (!implementation ||
      !new RegExp(`\\b${tableInterner}\\s*\\(`).test(implementation[0]) ||
      /\bps_type_new_[A-Za-z0-9_]*\s*\(|\bps_ctx_intern_qual_type_in\s*\(/.test(
        implementation[0],
      ) ||
      !new RegExp(`\\b${tableInterner}\\s*\\(`).test(
        semanticTypeIdentityHeader,
      ) ||
      !new RegExp(`\\b${tableInterner}\\s*\\(`).test(
        semanticTypeIdentitySource,
      )) {
    throw new Error(
      `${contextInterner} must intern an immutable TypeShape without constructing a parser type`,
    );
  }
}
const directShapeInterner = semanticTypeIdentitySource.match(
  /psx_qual_type_t\s+psx_semantic_type_table_intern_shape\s*\([^]*?\n\}/,
);
if (!directShapeInterner ||
    !/table->entries\[id\]\.shape\s*=\s*owned_shape\s*;/.test(
      directShapeInterner[0],
    ) ||
    !/table->entries\[id\]\.base_type\s*=\s*base_type\s*;/.test(
      directShapeInterner[0],
    ) ||
    !/table->entries\[id\]\.parameter_types\s*=\s*owned_parameters\s*;/.test(
      directShapeInterner[0],
    ) ||
    /\bps_type_new_[A-Za-z0-9_]*\s*\(|\bpsx_semantic_type_table_intern\s*\(/.test(
      directShapeInterner[0],
    )) {
  throw new Error(
    "semantic TypeIds must be interned directly from TypeShape relations while parser types remain compatibility views",
  );
}
for (const derivedInterner of [
  "psx_semantic_type_table_intern_pointer_to",
  "psx_semantic_type_table_intern_array_of",
  "psx_semantic_type_table_intern_function",
]) {
  const implementation = semanticTypeIdentitySource.match(
    new RegExp(
      `psx_qual_type_t\\s+${derivedInterner}\\s*\\([^]*?\\n\\}`,
    ),
  );
  if (!implementation ||
      !/\bpsx_semantic_type_table_intern_shape\s*\(/.test(
        implementation[0],
      ) ||
      /\bps_type_new_[A-Za-z0-9_]*\s*\(|\bpsx_type_compatibility_canonical_view_for\s*\(/.test(
        implementation[0],
      )) {
    throw new Error(
      `${derivedInterner} must derive TypeId directly from recursive QualType relations`,
    );
  }
}
if (/parser\/type_builder\.h|\bps_type_new_[A-Za-z0-9_]*\s*\(/.test(
      literalResolutionSource,
    ) ||
    /parser\/type_builder\.h|\bps_type_new_[A-Za-z0-9_]*\s*\(/.test(
      parameterDeclarationResolutionSource,
    ) ||
    !/case\s+PSX_IDENTIFIER_BUILTIN_VA_ARG_AREA:[^]*?ps_ctx_intern_pointer_to_qual_type_in\s*\([^]*?ps_ctx_intern_void_qual_type_in\s*\(/.test(
      identifierResolutionSource,
    )) {
  throw new Error(
    "semantic synthesized pointer and array types must use canonical TypeId constructors",
  );
}
const exactIntVoidTypePredicate = semanticTypeIdentitySource.match(
  /int\s+psx_semantic_type_is_exact_int_void_function\s*\([^]*?\n\}/,
);
if (!exactIntVoidTypePredicate ||
    !/psx_semantic_type_table_callable_function\s*\(/.test(
      exactIntVoidTypePredicate[0],
    ) ||
    !/psx_semantic_type_table_describe\s*\(/.test(
      exactIntVoidTypePredicate[0],
    ) ||
    /psx_type_compatibility_canonical_view_for\s*\(/.test(
      exactIntVoidTypePredicate[0],
    ) ||
    /static\s+int\s+exact_int_void_function\s*\(/.test(
      continuationSyntaxValidationSource + hirIrBuilder,
    ) ||
    !/psx_semantic_type_is_exact_int_void_function\s*\(/.test(
      continuationSyntaxValidationSource,
    ) ||
    !/psx_semantic_type_is_exact_int_void_function\s*\(/.test(
      hirIrBuilder,
    ) ||
    /#include\s+"\.\.\/parser\/type\.h"/.test(
      continuationSyntaxValidationSource,
    )) {
  throw new Error(
    "continuation function type validation must consume canonical TypeId shape in every phase",
  );
}
if (!/canonical->record_id\s*!=\s*PSX_RECORD_ID_INVALID/.test(
      semanticTypeIdentitySource,
    )) {
  throw new Error(
    "aggregate TypeId identity must require a resolved RecordId throughout the recursive type",
  );
}
if (!/\bpsx_qual_type_t\s+base_type\s*;/.test(
      semanticTypeIdentitySource,
    ) ||
    !/\bpsx_qual_type_t\s*\*\s*parameter_types\s*;/.test(
      semanticTypeIdentitySource,
    ) ||
    /\bpsx_qual_type_t\s*\*\s*record_member_types\s*;/.test(
      semanticTypeIdentitySource,
    ) ||
    !/psx_semantic_type_table_record_member\s*\([^]*?psx_record_decl_table_lookup\s*\([^]*?record->members\[member_index\]\.decl_qual_type/.test(
      semanticTypeIdentitySource,
    ) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      semanticTypeIdentityHeader,
    ) ||
    !/\bpsx_semantic_type_table_parameter\s*\(/.test(
      semanticTypeIdentityHeader,
    ) ||
    !/\bpsx_semantic_type_table_record_member\s*\(/.test(
      semanticTypeIdentityHeader,
    )) {
  throw new Error(
    "TypeIds must retain recursive base and parameter relations while RecordDecl remains the sole member QualType owner",
  );
}

const semanticContextSource = await readFile(
  "src/parser/semantic_ctx.h",
  "utf8",
);
if (!/\bconst\s+ag_data_layout_t\s*\*\s*ps_ctx_data_layout\s*\(/.test(
      semanticContextSource,
    ) ||
    !/\bps_ctx_data_layout\s*\([^]*?ag_target_info_data_layout\s*\(\s*ps_ctx_target_info\s*\(/.test(
      parserSemanticContextImplementation,
    ) ||
    !/\bconst\s+psx_record_decl_t\s*\*\s*ps_ctx_ensure_tag_record_decl_in\s*\(/.test(
      semanticContextSource,
    ) ||
    !/\bconst\s+psx_record_decl_t\s*\*\s*ps_ctx_get_record_decl_in\s*\(/.test(
      semanticContextSource,
    ) ||
    !/\bps_ctx_register_record_members_in\s*\(/.test(
      semanticContextSource,
    ) ||
    /\bpsx_type_t\b|type_compatibility_view\.h|\bps_ctx_(?:intern_qual_type_in|intern_declaration_qual_type_in|find_interned_qual_type_in|type_by_id_in|clone_tag_type_at_in|bind_record_ids_in)\s*\(/.test(
      `${semanticContextSource}\n${parserSemanticContextImplementation}`,
    ) ||
    /\bps_ctx_type_(?:size|align)of_in\s*\(/.test(
      `${semanticContextSource}\n${parserSemanticContextImplementation}`,
    ) ||
    !/\bpsx_qual_type_layout_sizeof\s*\(/.test(
      parserSemanticContextImplementation,
    ) ||
    !/\bpsx_qual_type_layout_alignof\s*\(/.test(
      parserSemanticContextImplementation,
    ) ||
    /\bps_ctx_refresh_type_completeness_in\s*\(/.test(
      `${semanticContextSource}\n${parserSemanticContextImplementation}`,
    ) ||
    /\bps_type_sizeof\s*\(/.test(parserSemanticContextImplementation)) {
  throw new Error(
    "semantic context must expose immutable type identity and target layout queries",
  );
}
for (const path of allSourceFiles) {
  const source = await readFile(path, "utf8");
  if (/\bps_ctx_type_(?:size|align)of_in\s*\(/.test(source)) {
    throw new Error(
      `${path} must query target layout by canonical TypeId instead of a raw parser type`,
    );
  }
  if (path.startsWith("src/semantic/") &&
      /\bag_target_info_data_layout\s*\(\s*ps_ctx_target_info\s*\(/.test(
        source,
      )) {
    throw new Error(
      `${path} must obtain semantic layout through ps_ctx_data_layout`,
    );
  }
}
const sourcesWithLegacyRecordOwnership = [];
for (const path of allSourceFiles) {
  const source = await readFile(path, "utf8");
  if (/\b(?:ps_ctx_attach_aggregate_definitions_in|ps_ctx_get_tag_definition_in|psx_aggregate_definition_t)\b/.test(
        source,
      )) {
    sourcesWithLegacyRecordOwnership.push(path);
  }
}
if (sourcesWithLegacyRecordOwnership.length > 0) {
  throw new Error(
    "semantic aggregate ownership must use RecordId without legacy aggregate definitions",
  );
}
if (!/\bfind_tag_type_by_record_id_in\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/\bps_ctx_ensure_tag_record_decl_in\s*\([\s\S]*?record->record_id[\s\S]*?psx_aggregate_layout_init\s*\([\s\S]*?record->record_id/.test(
      declarationApplicationSource,
    )) {
  throw new Error(
    "aggregate body layout must bind member registration to stable RecordId identity",
  );
}

const declaratorApplicationTypesHeader = await readFile(
  "src/semantic/declarator_application_types.h",
  "utf8",
);
const runtimeArrayBoundStruct = declaratorApplicationTypesHeader.match(
  /typedef\s+struct\s*\{([^]*?)\}\s*psx_runtime_array_bound_t\s*;/,
);
const runtimeDeclaratorApplicationStruct =
  declaratorApplicationTypesHeader.match(
    /typedef\s+struct\s+psx_runtime_declarator_application_t\s*\{([^]*?)\}\s*psx_runtime_declarator_application_t\s*;/,
  );
const typedefInfoStruct = semanticContextHeaderSource.match(
  /typedef\s+struct\s*\{((?:(?!typedef\s+struct)[^])*?)\}\s*psx_typedef_info_t\s*;/,
);
const localDeclarationResolutionSource = await readFile(
  "src/semantic/local_declaration_resolution.h",
  "utf8",
);
const localDeclarationResolutionImplementation = await readFile(
  "src/semantic/local_declaration_resolution.c",
  "utf8",
);
const localVlaDimensionStruct = localDeclarationResolutionSource.match(
  /typedef\s+struct\s*\{([^]*?)\}\s*psx_local_vla_dimension_t\s*;/,
);
const parameterDeclarationResolutionHeader = await readFile(
  "src/semantic/parameter_declaration_resolution.h",
  "utf8",
);
const parameterVlaDimensionStruct =
  parameterDeclarationResolutionHeader.match(
    /typedef\s+struct\s*\{([^]*?)\}\s*psx_parameter_dimension_t\s*;/,
  );
const parameterDeclarationResolutionStruct =
  parameterDeclarationResolutionHeader.match(
    /typedef\s+struct\s*\{((?:(?!typedef struct)[\s\S])*?)\}\s*psx_parameter_declaration_resolution_t\s*;/,
  );
const expressionIdentityHeaderSource = await readFile(
  "src/semantic/expression_identity.h",
  "utf8",
);
const expressionIdentitySource = await readFile(
  "src/semantic/expression_identity.c",
  "utf8",
);
if (!runtimeArrayBoundStruct ||
    !/\bpsx_semantic_expr_id_t\s+expression_id\s*;/.test(
      runtimeArrayBoundStruct[1],
    ) ||
    /\bnode_t\s*\*/.test(runtimeArrayBoundStruct[1]) ||
    !runtimeDeclaratorApplicationStruct ||
    !/psx_declarator_shape_t\s+shape\s*;/.test(
      runtimeDeclaratorApplicationStruct[1],
    ) ||
    !/psx_runtime_array_bound_t\s*\*array_bounds\s*;/.test(
      runtimeDeclaratorApplicationStruct[1],
    ) ||
    !typedefInfoStruct ||
    !/const\s+psx_runtime_declarator_application_t\s*\*runtime_application\s*;/.test(
      typedefInfoStruct[1],
    ) ||
    !/clone_typedef_runtime_application\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/out->runtime_application\s*=/.test(
      semanticContextOwnershipSource,
    ) ||
    !/psx_compose_runtime_declarator_applications_in\s*\(/.test(
      declarationApplicationSource,
    ) ||
    !/bound\.declarator_op_index\s*\+=\s*op_offset/.test(
      declarationApplicationSource,
    ) ||
    !/specifier_resolution\.typedef_runtime_application/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !localVlaDimensionStruct ||
    !/\bpsx_semantic_expr_id_t\s+expression_id\s*;/.test(
      localVlaDimensionStruct[1],
    ) ||
    /\bnode_t\s*\*/.test(localVlaDimensionStruct[1]) ||
    !parameterVlaDimensionStruct ||
    !/\bpsx_semantic_expr_id_t\s+expression_id\s*;/.test(
      parameterVlaDimensionStruct[1],
    ) ||
    /\b(?:node_t\s*\*|source_name)\b/.test(
      parameterVlaDimensionStruct[1],
    ) ||
    !parameterDeclarationResolutionStruct ||
    !/\bpsx_qual_type_t\s+declaration_qual_type\s*;/.test(
      parameterDeclarationResolutionStruct[1],
    ) ||
    !/\bpsx_qual_type_t\s+function_qual_type\s*;/.test(
      parameterDeclarationResolutionStruct[1],
    ) ||
    /\bpsx_parameter_storage_plan_t\b/.test(
      parameterDeclarationResolutionStruct[1],
    ) ||
    /\bpsx_type_t\b/.test(parameterDeclarationResolutionStruct[1]) ||
    !/\bpsx_semantic_expr_id_t\s+pointer_row_dimension_id\s*;/.test(
      localDeclarationResolutionSource,
    ) ||
    /\bnode_t\s*\*\s*pointer_row_dimension\s*;/.test(
      localDeclarationResolutionSource,
    ) ||
    !/const\s+psx_typed_hir_tree_t\s*\*expression/.test(
      expressionIdentityHeaderSource,
    ) ||
    /\bnode_t\b/.test(expressionIdentityHeaderSource) ||
    !/const\s+psx_typed_hir_tree_t\s*\*\*expressions/.test(
      expressionIdentitySource,
    ) ||
    !/const\s+psx_typed_hir_tree_t\s*\*expression/.test(
      semanticContextHeaderSource,
    ) ||
    /ps_ctx_(?:register_)?semantic_expression_in\s*\([^;]*\bnode_t\b/.test(
      semanticContextHeaderSource,
    ) ||
    !/psx_resolve_declarator_bound_in_contexts\s*\(/.test(
      declarationApplicationSource,
    ) ||
    /psx_resolution_work_tree_|psx_bind_identifier_tree_|psx_semantic_resolve_tree_in_contexts/.test(
      declarationApplicationSource,
    ) ||
    !/psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts\s*\(/.test(
      declaratorBoundResolutionSource,
    ) ||
    /psx_typed_hir_tree_materialize|psx_resolution_work_tree_|psx_bind_identifier_tree_|psx_semantic_resolve_tree_in_contexts/.test(
      declaratorBoundResolutionSource,
    ) ||
    !/ps_ctx_register_semantic_expression_in\s*\([^]*?bound_resolution\.typed_expression/.test(
      declarationApplicationSource,
    )) {
  throw new Error(
    "VLA runtime bounds must use semantic expression IDs backed by Typed HIR outside canonical types",
  );
}
const parameterVlaLoweringFunction = vlaLoweringSource.match(
  /psx_parameter_vla_lowering_result_t\s+lower_parameter_vla_declaration\s*\([^]*?\n\}/,
);
if (!/dimension->expression_id\s*=/.test(localDeclarationPipelineSource) ||
    !/ps_ctx_semantic_expression_in\s*\([^]*?resolution\.inner_dimensions\[i\]\.expression_id/.test(
      localDeclarationPipelineSource,
    ) ||
    !parameterVlaLoweringFunction ||
    !/parameter_storage_size\s*=\s*type_size\s*\([^]*?request->type\.type_id/.test(
      parameterVlaLoweringFunction[0],
    ) ||
    !/parameter_alignment\s*=\s*type_alignment\s*\([^]*?request->type\.type_id/.test(
      parameterVlaLoweringFunction[0],
    ) ||
    !/psx_qual_type_t\s+type\s*;/.test(vlaLoweringHeader) ||
    !/psx_qual_type_t\s+stride_storage_type\s*;/.test(
      vlaLoweringHeader,
    ) ||
    !/psx_semantic_expr_id_t\s+row_dimension_id\s*;/.test(
      vlaLoweringHeader,
    ) ||
    !/psx_semantic_expr_id_t\s+expression_id\s*;/.test(
      vlaLoweringHeader,
    ) ||
    !/psx_semantic_expression_table_t\s*\*\s*semantic_expressions\s*;/.test(
      `${vlaLoweringHeader}\n${parameterLoweringHeader}`,
    ) ||
    /psx_typed_hir_tree_t/.test(
      `${vlaLoweringHeader}\n${parameterLoweringHeader}`,
    ) ||
    !/psx_semantic_expression_table_lookup\s*\([^]*?dimension->expression_id/.test(
      vlaLoweringSource,
    ) ||
    /\bpsx_type_t\b/.test(vlaLoweringHeader) ||
    /\bps_lowering_type_(?:id|alignment)\s*\(/.test(vlaLoweringSource) ||
    /ps_local_registry_create_(?:internal_)?storage_object_in\s*\(/.test(
      vlaLoweringSource,
    ) ||
    /\bpsx_parameter_lowering_request_t\b|\blower_parameter_declaration\s*\(/.test(
      `${parameterLoweringHeader}\n${parameterLoweringSource}`,
    ) ||
    /\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      parameterLoweringSource,
    ) ||
    /request->name_len\s*,\s*8\b/.test(parameterVlaLoweringFunction[0]) ||
    /ps_decl_find_lvar_in\s*\(/.test(parameterVlaLoweringFunction[0])) {
  throw new Error(
    "parameter VLA bounds must cross semantic/lowering by expression identity and use target layout",
  );
}
const vlaGeneratedSemanticNodeRe =
  /\bps_(?:node_new_binary_for_target_in|node_new_num_in|node_new_assign_in|node_new_lvar_typed_in)\s*\(/;
if (!/typedef\s+struct\s*\{[^]*?psx_semantic_expr_id_t\s+expression_id\s*;[^]*?long\s+long\s+constant_value\s*;[^]*?is_constant\s*;[^]*?\}\s*psx_vla_runtime_dimension_t\s*;/.test(
      vlaRuntimePlanHeaderSource,
    ) ||
    !/typedef\s+struct\s+psx_vla_runtime_plan_t\s*\{[^]*?psx_vla_runtime_dimension_t\s*\*\s*dimensions\s*;[^]*?psx_qual_type_t\s+constant_qual_type\s*;[^]*?\bstride_store_offsets\s*;[^]*?\bstride_start_dimensions\s*;[^]*?\bperforms_allocation\s*;[^]*?\}\s*psx_vla_runtime_plan_t\s*;/.test(
      vlaRuntimePlanHeaderSource,
    ) ||
    vlaGeneratedSemanticNodeRe.test(vlaLoweringSource) ||
    /\bps_node_new_vla_runtime_in\s*\(/.test(vlaLoweringSource) ||
    !/PSX_HIR_EDGE_VLA_DIMENSION/.test(hirHeader) ||
    !/vla_runtime_store_offsets/.test(hirInternalHeader) ||
    !/psx_semantic_node_builder_vla_runtime\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_node_builder_vla_runtime\s*\([^]*?PSX_HIR_EDGE_VLA_DIMENSION[^]*?vla_runtime_store_offsets/.test(
      semanticNodeBuilderSource,
    ) ||
    !/dimension->is_constant[^]*?PSX_HIR_NUMBER[^]*?ps_ctx_semantic_expression_in\s*\([^]*?dimension->expression_id[^]*?expression->root/.test(
      semanticNodeBuilderSource,
    ) ||
    /psx_typed_hir_tree_t\s*\*\s*expression\s*;/.test(
      vlaRuntimePlanHeaderSource,
    ) ||
    !/child_count_for_edge\s*\([^]*?PSX_HIR_EDGE_VLA_DIMENSION/.test(
      hirIrBuilder,
    ) ||
    !/psx_hir_node_vla_runtime_store_dimension\s*\(/.test(
      hirIrBuilder,
    ) ||
    !/for\s*\(size_t\s+i\s*=\s*0;\s*i\s*<\s*dimension_count;\s*i\+\+\)[^]*?build_expr\s*\(/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "VLA declarations must lower through an immutable runtime plan and Typed HIR without generated semantic arithmetic or assignment AST",
  );
}
if (!/\bpsx_resolve_parameter_declaration\s*\(/.test(
      declarationApplicationSource,
    ) ||
    !/\bpsx_resolve_parameter_declaration\s*\(/.test(
      declarationPipelineSource,
    ) ||
    !/resolution->declaration_qual_type\s*=\s*identity\s*;/.test(
      parameterDeclarationResolutionSource,
    ) ||
    !/resolution->function_qual_type\s*=\s*identity\s*;[^]*?resolution->function_qual_type\.qualifiers\s*=\s*PSX_TYPE_QUALIFIER_NONE\s*;/.test(
      parameterDeclarationResolutionSource,
    ) ||
    /\bps_type_adjust_parameter_type_in\s*\(/.test(
      declarationApplicationSource,
    )) {
  throw new Error(
    "prototype and definition parameter types must share canonical adjustment while preserving definition-object qualifiers",
  );
}
if (!/PSX_VLA_RUNTIME_SLOT_SIZE\s*=\s*8\b/.test(
      vlaRuntimeHeaderSource,
    ) ||
    !/PSX_VLA_RUNTIME_DESCRIPTOR_HEADER_SIZE/.test(frameLayoutSource) ||
    !/PSX_VLA_RUNTIME_SLOT_SIZE/.test(frameLayoutSource) ||
    !/PSX_VLA_RUNTIME_SLOT_SIZE/.test(vlaLoweringSource) ||
    !/pointer_stride_value\s*\([^]*?vla_stride_slot_size/.test(
      hirIrBuilder,
    ) ||
    !/syntax->kind\s*==\s*ND_SUBSCRIPT[^]*?PSX_VLA_RUNTIME_SLOT_SIZE/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/AG_TARGET_SCALAR_LONG_LONG[^]*?PSX_VLA_RUNTIME_SLOT_SIZE/.test(
      parameterDeclarationResolutionSource,
    ) ||
    !/ps_ctx_intern_integer_qual_type_in\s*\([^]*?PSX_INTEGER_KIND_LONG_LONG/.test(
      parameterDeclarationResolutionSource,
    ) ||
    !/ps_ctx_intern_array_of_qual_type_in\s*\(/.test(
      parameterDeclarationResolutionSource,
    ) ||
    /\b8\s*\*\s*(?:count|level|stride_count|subscript_depth)/.test(
      frameLayoutSource + vlaLoweringSource + hirIrBuilder,
    )) {
  throw new Error(
    "VLA runtime descriptor ABI must be explicit and independent from C target layout",
  );
}
if (!/psx_qual_type_t\s+psx_resolve_address_result_qual_type_in\s*\([^]*?ps_ctx_intern_pointer_to_qual_type_in\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/semantic_type_relations_match\s*\([^]*?entry->base_type\.type_id\s*!=\s*base_type\.type_id[^]*?entry->base_type\.qualifiers\s*!=\s*base_type\.qualifiers/.test(
      typeIdentityImplementationSource,
    ) ||
    !/psx_semantic_type_table_intern_pointer_to\s*\([^]*?semantic_type_table_intern_shape\s*\(/.test(
      typeIdentityImplementationSource,
    ) ||
    !/psx_semantic_type_table_callable_function\s*\(/.test(
      typeIdentityImplementationSource,
    ) ||
    !/psx_semantic_type_table_callable_function\s*\(/.test(
      callResolutionSource,
    ) ||
    !/psx_semantic_type_table_base\s*\(/.test(
      callResolutionSource,
    )) {
  throw new Error(
    "address decay and function-call results must preserve recursive QualType relations",
  );
}
if (!/\bconst\s+psx_semantic_type_table_t\s*\*\s*semantic_types\s*;/.test(
      localDeclarationResolutionSource,
    ) ||
    !/\bconst\s+psx_record_layout_table_t\s*\*\s*record_layouts\s*;/.test(
      localDeclarationResolutionSource,
    ) ||
    !/\bconst\s+ag_data_layout_t\s*\*\s*data_layout\s*;/.test(
      localDeclarationResolutionSource,
    ) ||
    /\bag_target_info_t\b/.test(localDeclarationResolutionSource) ||
    !/\bpsx_type_id_t\s+type_id\s*;/.test(
      localDeclarationResolutionSource,
    ) ||
    /\bconst\s+psx_type_t\s*\*\s*type\s*;/.test(
      localDeclarationResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      localDeclarationResolutionImplementation,
    ) ||
    /\bpsx_type_compatibility_canonical_view_for\s*\(/.test(
      localDeclarationResolutionImplementation,
    ) ||
    !/\bpsx_type_layout_sizeof\s*\(/.test(
      localDeclarationResolutionImplementation,
    ) ||
    /\bag_target_info_data_layout\s*\(/.test(
      localDeclarationResolutionImplementation,
    ) ||
    /\bps_type_(?:size|align)of_id_for_target\s*\(/.test(
      localDeclarationResolutionImplementation,
    ) ||
    /\bps_type_(?:size|align)of_for_target\s*\(/.test(
      localDeclarationResolutionImplementation,
    )) {
  throw new Error(
    "local declaration resolution must derive layout from TypeId, record layouts, and explicit DataLayout",
  );
}

const lvarInternalSource = await readFile("src/parser/lvar_internal.h", "utf8");
const lvarStruct = lvarInternalSource.match(/struct lvar_t\s*\{([\s\S]*?)\n\};/);
const symtabSource = await readFile("src/parser/symtab.h", "utf8");
const gvarStruct = symtabSource.match(
  /struct global_var_t\s*\{([\s\S]*?)\n\};/,
);
const lvarPublicSource = await readFile("src/parser/lvar_public.h", "utf8");
const gvarPublicSource = await readFile("src/parser/gvar_public.h", "utf8");
if (!lvarStruct ||
    /\bpsx_type_t\s*\*\s*decl_type\s*;/.test(lvarStruct[1]) ||
    /\bis_byref_param\b/.test(lvarStruct[1]) ||
    !gvarStruct ||
    /\bpsx_type_t\s*\*\s*decl_type\s*;/.test(gvarStruct[1]) ||
    /\bps_lvar_get_decl_type\b|\bpsx_type_t\b/.test(
      lvarPublicSource,
    ) ||
    /\bps_gvar_get_decl_type\b/.test(
      gvarPublicSource,
    )) {
  throw new Error(
    "symbols must own only QualType identity without publishing compatibility type views",
  );
}
if (/\b(?:scope_seq|declaration_seq|declaration_id)\b/.test(
      lvarStruct[1],
    ) ||
    /\b(?:next_all|next_binding)\b/.test(lvarStruct[1]) ||
    !/\blvar_t\s*\*next_storage\s*;/.test(lvarStruct[1]) ||
    /\blvar_t\s*\*next\s*;/.test(lvarStruct[1]) ||
    /\bglobal_var_t\s*\*next\s*;/.test(gvarStruct[1]) ||
    /\bdeclaration_id\b/.test(gvarStruct[1]) ||
    /\b(?:scope_seq|declaration_id)\b/.test(lvarPublicSource) ||
    !/has_local_object_in_current_scope\s*\([^]*?psx_scope_graph_lookup_declaration_in_scope\s*\([^]*?PSX_NAMESPACE_ORDINARY/.test(
      localRegistrySource,
    ) ||
    /previous->scope_seq|var->(?:scope_seq|declaration_seq|declaration_id)|gv->declaration_id/.test(
      `${localRegistrySource}\n${globalRegistrySource}`,
    ) ||
    /\bglobal_var_t\s*\*global_vars\s*;/.test(globalRegistrySource) ||
    /\b(?:all_locals|all_bindings)\b/.test(localRegistrySource) ||
    !/\blvar_t\s*\*storage_objects\s*;/.test(localRegistrySource) ||
    !/ps_iter_globals_in\s*\([^]*?psx_scope_graph_declaration_at\s*\([^]*?PSX_DECL_GLOBAL_OBJECT/.test(
      globalRegistrySource,
    ) ||
    !/transaction_contains_original_global\s*\([^]*?scope_graph_checkpoint\.declaration_count[^]*?psx_scope_graph_declaration_at\s*\(/.test(
      globalRegistrySource,
    ) ||
    /\b(?:LVAR_SCOPE_STACK_MAX|lvar_scope_stack|lvar_scope_depth)\b|\blvar_t\s*\*locals\s*;/.test(
      localRegistrySource,
    ) ||
    !/ps_decl_leave_scope_in\s*\([^]*?PSX_SCOPE_BLOCK[^]*?PSX_SCOPE_FUNCTION_PROTOTYPE[^]*?psx_scope_graph_leave_scope\s*\(/.test(
      localRegistrySource,
    )) {
  throw new Error(
    "local and global object payloads must not duplicate scope graph declaration identity",
  );
}
if (!/\bpsx_qual_type_t\s+decl_qual_type\s*;/.test(lvarStruct[1]) ||
    /\bpsx_type_id_t\s+decl_type_id\s*;/.test(lvarStruct[1]) ||
    !/\bpsx_qual_type_t\s+ps_lvar_decl_qual_type\s*\(/.test(
      lvarPublicSource,
    ) ||
    !/\bpsx_type_id_t\s+ps_lvar_decl_type_id\s*\(/.test(
      lvarPublicSource,
    ) ||
    !/\bconst\s+psx_semantic_type_table_t\s*\*\s*semantic_types\s*;/.test(
      localRegistrySource,
    ) ||
    /\bpsx_type_t\b|type_compatibility_view\.h|resolve_local_decl_type|ps_local_registry_(?:create_storage_object_in|create_internal_storage_object_in|create_static_alias_in|complete_array_type)\s*\(/.test(
      `${localRegistryHeader}\n${localRegistrySource}`,
    ) ||
    /\bps_type_clone_persistent\s*\(/.test(localRegistrySource) ||
    !/ps_local_registry_create\s*\([^]*?ps_ctx_semantic_type_table_in\s*\(\s*session->semantic_context\s*\)/.test(
      compilationSessionSource,
    )) {
  throw new Error(
    "local symbols must retain their declaration QualType from the compilation unit semantic type table",
  );
}
if (!/\bconst\s+psx_semantic_type_table_t\s*\*\s*decl_type_table\s*;/.test(
      lvarStruct[1],
    ) ||
    !/\bconst\s+psx_semantic_type_table_t\s*\*\s*decl_type_table\s*;/.test(
      gvarStruct[1],
    ) ||
    /\bpsx_type_compatibility_view_for\s*\(|\bps_lvar_get_decl_type\s*\(/.test(
      parserDeclSource,
    ) ||
    /\bpsx_type_compatibility_view_for\s*\(|\bps_gvar_get_decl_type\s*\(/.test(
      nodeUtilsSource,
    ) ||
    !/\bint\s+ps_gvar_decl_type_shape\s*\(\s*const\s+global_var_t\s*\*/.test(
      gvarPublicSource,
    ) ||
    !/\bint\s+ps_gvar_decl_type_shape\s*\([^]*?psx_semantic_type_table_describe\s*\([^]*?decl_type_table[^]*?decl_qual_type\.type_id/.test(
      nodeUtilsSource,
    ) ||
    !/decl_type_table\s*=\s*registry->semantic_types\s*;/.test(
      localRegistrySource,
    ) ||
    /\bvar->decl_type\s*=/.test(localRegistrySource) ||
    /\bcanonical_type\b/.test(localRegistrySource) ||
    !/decl_type_table\s*=\s*registry->semantic_types\s*;/.test(
      globalRegistrySource,
    ) ||
    /\bglobal->decl_type\s*=/.test(globalRegistrySource) ||
    /\bcanonical_type\b/.test(globalRegistrySource) ||
    /\bglobal->decl_type\b/.test(staticLocalLoweringSource) ||
    /\bps_gvar_get_decl_type\s*\(/.test(staticLocalLoweringSource) ||
    !/\bps_gvar_decl_qual_type\s*\(/.test(staticLocalLoweringSource)) {
  throw new Error(
    "production symbol type queries must consume declaration QualType identity without compatibility views",
  );
}
const lvarSemanticTypeQueries = parserDeclSource.match(
  /int\s+ps_lvar_array_flat_element_count\s*\([^]*?int\s+ps_lvar_vla_row_stride_frame_off\s*\(/,
);
const gvarSemanticTypeQueries = nodeUtilsSource.match(
  /int\s+ps_gvar_decl_type_shape\s*\([^]*?int\s+ps_gvar_has_aggregate_initializer\s*\(/,
);
if (!/\bpsx_semantic_type_table_array_subscript_stride_elements\s*\(/.test(
      semanticTypeIdentityHeader,
    ) ||
    !/psx_semantic_type_table_array_subscript_stride_elements\s*\([^]*?psx_semantic_type_table_base\s*\([^]*?psx_semantic_type_table_array_flat_element_count\s*\(/.test(
      semanticTypeIdentitySource,
    ) ||
    !lvarSemanticTypeQueries ||
    /\bps_lvar_get_decl_type\s*\(|\bps_type_[a-z_]+\s*\(/.test(
      lvarSemanticTypeQueries[0],
    ) ||
    !/\bpsx_semantic_type_table_(?:describe|array_leaf|array_flat_element_count|array_subscript_stride_elements|contains_vla_array)\s*\(/.test(
      lvarSemanticTypeQueries[0],
    ) ||
    !gvarSemanticTypeQueries ||
    /\bgvar_decl_type_view\s*\(|\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(|\bps_type_[a-z_]+\s*\(/.test(
      gvarSemanticTypeQueries[0],
    ) ||
    !/\bpsx_semantic_type_table_(?:describe|array_leaf)\s*\(/.test(
      gvarSemanticTypeQueries[0],
    )) {
  throw new Error(
    "local and global symbol type queries must consume QualType and TypeShape without compatibility views",
  );
}
if (!recordMemberDeclStruct ||
    /decl_type_table/.test(recordMemberDeclStruct[1]) ||
    !/decl_qual_type/.test(recordMemberDeclStruct[1]) ||
    /\bpsx_type_t\b/.test(recordMemberDeclStruct[1]) ||
    /\bpsx_record_member_decl_type\s*\(/.test(
      `${recordDeclHeaderSource}\n${recordDeclImplementationSource}`,
    ) ||
    /type_compatibility_view\.h|\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      recordDeclImplementationSource,
    ) ||
    /member->decl_type_table/.test(recordDeclImplementationSource) ||
    /return\s+member->decl_type\s*;/.test(recordDeclImplementationSource) ||
    !/psx_qual_type_t\s+identity\s*=\s*declaration->decl_qual_type\s*;[^]*?psx_semantic_type_table_qual_type_is_valid\s*\([^]*?m->declaration\.qual_type\s*=\s*identity/s.test(
      parserSemanticContextImplementation,
    )) {
  throw new Error(
    "record members must store QualType only without publishing parser type views",
  );
}
if (!/\bpsx_record_member_decl_leaf_shape\s*\(/.test(
      recordDeclHeaderSource,
    ) ||
    !/psx_record_member_decl_leaf_shape\s*\([^]*?psx_semantic_type_table_array_leaf\s*\([^]*?psx_semantic_type_table_describe\s*\(/.test(
      recordDeclImplementationSource,
    )) {
  throw new Error(
    "record member leaf meaning must be exposed as TypeShape without materializing parser type views",
  );
}
const staticDataInitializerBoundary = parserUnitTestSource.match(
  /static\s+void\s+test_static_data_initializer_boundary\s*\(\s*ag_compilation_session_t\s*\*\s*test_suite_session\s*\)\s*\{([^]*?)\n\}/,
);
if (!/\bpsx_qual_type_t\s+decl_qual_type\s*;/.test(gvarStruct[1]) ||
    /\bpsx_type_id_t\s+decl_type_id\s*;/.test(gvarStruct[1]) ||
    !/\bpsx_qual_type_t\s+ps_gvar_decl_qual_type\s*\(/.test(
      gvarPublicSource,
    ) ||
    !/\bpsx_type_id_t\s+ps_gvar_decl_type_id\s*\(/.test(
      gvarPublicSource,
    ) ||
    !/\bconst\s+psx_semantic_type_table_t\s*\*\s*semantic_types\s*;/.test(
      globalRegistrySource,
    ) ||
    /\bpsx_type_t\b|type_compatibility_view\.h|resolve_global_decl_type|ps_global_registry_(?:bind_decl_type|complete_array_type)\s*\(/.test(
      `${globalRegistryHeader}\n${globalRegistrySource}`,
    ) ||
    !/\bpsx_semantic_type_table_qual_type_is_valid\s*\(/.test(
      globalRegistrySource,
    ) ||
    /\bps_type_clone_persistent\s*\(/.test(globalRegistrySource) ||
    !/\bps_global_registry_bind_decl_qual_type\s*\(/.test(
      globalRegistrySource,
    ) ||
    !staticDataInitializerBoundary ||
    !/\bps_gvar_decl_qual_type\s*\(/.test(
      staticDataInitializerBoundary[1],
    ) ||
    !/PSX_DECL_GLOBAL_OBJECT/.test(
      staticDataInitializerBoundary[1],
    ) ||
    !/\bps_global_registry_complete_array_qual_type\s*\(/.test(
      globalObjectLoweringSource,
    ) ||
    !/ps_global_registry_create\s*\([^]*?ps_ctx_semantic_type_table_in\s*\(\s*session->semantic_context\s*\)/.test(
      compilationSessionSource,
    )) {
  throw new Error(
    "global symbols must retain their declaration QualType from the compilation unit semantic type table",
  );
}

if (!/\bpsx_qual_type_t\s+declaration_qual_type\s*;/.test(
      globalDeclarationResolutionHeader,
    ) ||
    !/\bpsx_qual_type_t\s+type\s*;/.test(
      globalDeclarationResolutionHeader,
    ) ||
    /\bpsx_type_t\b/.test(globalDeclarationResolutionSource) ||
    /\bps_ctx_intern_qual_type_in\s*\(/.test(
      globalDeclarationResolutionSource,
    ) ||
    !/\bps_global_registry_bind_decl_qual_type\s*\(/.test(
      await readFile("src/lowering/global_object_lowering.c", "utf8"),
    )) {
  throw new Error(
    "global declaration resolution must consume and preserve canonical QualType identity",
  );
}

const staticInitializerSource = await readFile(
  "src/semantic/static_initializer_resolution.h",
  "utf8",
);
const staticInitializerRequest = staticInitializerSource.match(
  /typedef struct\s*\{([\s\S]*?)\}\s*psx_static_initializer_resolution_request_t\s*;/,
);
const staticInitializerResolution = staticInitializerClassificationHeader.match(
  /typedef struct\s*\{((?:(?!typedef struct)[\s\S])*?)\}\s*psx_static_initializer_resolution_t\s*;/,
);
if (!staticInitializerRequest ||
    !/\bpsx_qual_type_t\s+type\s*;/.test(
      staticInitializerRequest[1],
    ) ||
    !staticInitializerResolution ||
    !/\bpsx_qual_type_t\s+object_qual_type\s*;/.test(
      staticInitializerResolution[1],
    ) ||
    /\bpsx_type_t\b/.test(staticInitializerResolution[1])) {
  throw new Error(
    "static initializer resolution must consume and return canonical QualType identity",
  );
}

const readonlyTypeSources = new Map();
const canonicalLoweringTypeFields = [
  ["src/semantic/declaration_application.h", "psx_declaration_phase_t", "base_qual_type"],
  ["src/semantic/declaration_resolution.h", "psx_decl_type_request_t", "base_qual_type"],
  ["src/semantic/aggregate_member_resolution.h", "psx_aggregate_member_declaration_request_t", "base_qual_type"],
  ["src/semantic/global_declaration_resolution.h", "psx_global_declaration_resolution_request_t", "type"],
  ["src/semantic/static_initializer_resolution.h", "psx_static_initializer_resolution_request_t", "type"],
  ["src/semantic/function_declaration_resolution.h", "psx_function_declaration_resolution_request_t", "function_qual_type"],
  ["src/semantic/typedef_declaration_resolution.h", "psx_typedef_declaration_resolution_request_t", "decl_qual_type"],
  ["src/declaration_pipeline.h", "psx_global_declaration_pipeline_request_t", "type"],
  ["src/declaration_pipeline.h", "psx_function_declaration_pipeline_request_t", "function_qual_type"],
  ["src/declaration_pipeline.h", "psx_function_definition_pipeline_request_t", "base_qual_type"],
  ["src/declaration_pipeline.h", "psx_function_definition_pipeline_state_t", "base_qual_type"],
  ["src/declaration_pipeline.h", "psx_static_local_declaration_pipeline_request_t", "type"],
  ["src/declaration_pipeline.h", "psx_automatic_local_declaration_pipeline_request_t", "type"],
  ["src/declaration_pipeline.h", "psx_block_extern_declaration_pipeline_request_t", "type"],
  ["src/declaration_pipeline.h", "psx_temporary_local_declaration_pipeline_request_t", "type"],
  ["src/lowering/local_object_lowering.h", "psx_local_object_request_t", "type"],
  ["src/lowering/static_local_lowering.h", "psx_static_local_declaration_request_t", "type"],
];
for (const [file, typeName, fieldName] of canonicalLoweringTypeFields) {
  let source = readonlyTypeSources.get(file);
  if (!source) {
    source = await readFile(file, "utf8");
    readonlyTypeSources.set(file, source);
  }
  const body = source.match(
    new RegExp(
      `typedef struct\\s*\\{((?:(?!typedef struct)[\\s\\S])*?)\\}\\s*${typeName}\\s*;`,
    ),
  );
  const field = new RegExp(
    `\\bpsx_qual_type_t\\s+${fieldName}\\s*;`,
  );
  if (!body || !field.test(body[1]) || /\bpsx_type_t\b/.test(body[1])) {
    throw new Error(`${typeName}.${fieldName} must be a canonical QualType`);
  }
}

const canonicalDeclarationApplicationStates = [
  [
    "psx_toplevel_declaration_application_t",
    toplevelDeclarationFrontendSource,
  ],
];
for (const [typeName, source] of canonicalDeclarationApplicationStates) {
  const body = source.match(
    new RegExp(
      `typedef struct\\s*\\{((?:(?!typedef struct)[\\s\\S])*?)\\}\\s*${typeName}\\s*;`,
    ),
  );
  if (!body ||
      !/\bpsx_qual_type_t\s+base_qual_type\s*;/.test(body[1]) ||
      !/\bpsx_qual_type_t\s+current_qual_type\s*;/.test(body[1]) ||
      /\bconst\s+psx_type_t\s*\*\s*(?:base_type|current_type)\s*;/.test(
        body[1],
      )) {
    throw new Error(
      `${typeName} must retain declaration base and current types as canonical QualType`,
    );
  }
}

for (const [typeName, source] of canonicalDeclarationApplicationStates) {
  if (!/\bpsx_semantic_type_table_describe\s*\(/.test(source) ||
      /\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(source)) {
    throw new Error(
      `${typeName} must classify declaration types directly from canonical TypeShape`,
    );
  }
}

const declarationApplicationHeader = await readFile(
  "src/semantic/declaration_application.h",
  "utf8",
);
const declarationPhaseStruct = declarationApplicationHeader.match(
  /typedef struct\s*\{((?:(?!typedef struct)[\s\S])*?)\}\s*psx_declaration_phase_t\s*;/,
);
if (!/\bpsx_qual_type_t\s+psx_apply_parsed_type_name_qual_type_in_contexts\s*\(/.test(
      declarationApplicationHeader,
    ) ||
    !/\bpsx_qual_type_t\s+psx_apply_parsed_declarator_qual_type_in_contexts\s*\(/.test(
      declarationApplicationHeader,
    ) ||
    !/\bpsx_qual_type_t\s+psx_apply_runtime_declarator_qual_type_in_context\s*\(/.test(
      declarationApplicationHeader,
    ) ||
    !declarationPhaseStruct ||
    !/\bpsx_qual_type_t\s+base_qual_type\s*;/.test(
      declarationPhaseStruct[1],
    ) ||
    /\btype_table\b|\bpsx_type_t\b/.test(declarationPhaseStruct[1]) ||
    /\bpsx_(?:declaration_phase_base_type|apply_parsed_type_name_in_contexts|apply_parsed_declarator_type_in_contexts|apply_runtime_declarator_type_in_context)\s*\(/.test(
      `${declarationApplicationHeader}\n${declarationApplicationSource}`,
    ) ||
    /type_compatibility_view\.h|\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      declarationApplicationSource,
    )) {
  throw new Error(
    "type-name and declarator application must expose only canonical QualType results",
  );
}

const declarationResolutionHeader = await readFile(
  "src/semantic/declaration_resolution.h",
  "utf8",
);
const declarationTypeBuilderUsers = new Set();
const declarationTypeBuilderViolations = [];
for (const file of sourceFiles) {
  const source = await readFile(file, "utf8");
  if (!declarationTypeBuilderUsers.has(file) &&
      /\bpsx_build_decl_(?:type|specifier_type)\b/.test(source)) {
    declarationTypeBuilderViolations.push(file);
  }
}
if (declarationTypeBuilderViolations.length) {
  throw new Error(
    "mutable declaration type builders must stay inside semantic construction owners:\n" +
      declarationTypeBuilderViolations.sort().join("\n"),
  );
}
if (/\bpsx_build_decl_type\b|\bbuild_decl_type_value\b|\bps_type_apply_resolved_declarator_shape_in\b/.test(
      `${declarationResolutionHeader}\n${declarationResolutionSource}`,
    ) ||
    !/static\s+psx_qual_type_t\s+apply_declarator_shape\s*\(/.test(
      declarationResolutionSource,
    ) ||
    !/ps_ctx_intern_pointer_to_qual_type_in\s*\(/.test(
      declarationResolutionSource,
    ) ||
    !/ps_ctx_intern_array_of_qual_type_in\s*\(/.test(
      declarationResolutionSource,
    ) ||
    !/ps_ctx_intern_function_qual_type_in\s*\(/.test(
      declarationResolutionSource,
    )) {
  throw new Error(
    "declarator type resolution must build recursive canonical QualType relations directly",
  );
}
if (/\bpsx_build_decl_specifier_type_in_context\b|\bbuild_decl_specifier_type_value\b/.test(
      `${declarationResolutionHeader}\n${declarationResolutionSource}`,
    ) ||
    !/static\s+psx_qual_type_t\s+resolve_decl_specifier_qual_type\s*\(/.test(
      declarationResolutionSource,
    ) ||
    !/psx_qual_type_t\s+psx_resolve_decl_specifier_qual_type_in_context\s*\(/.test(
      declarationResolutionHeader,
    )) {
  throw new Error(
    "declaration specifier resolution must produce canonical QualType without mutable type builders",
  );
}

if (!/psx_resolve_completed_incomplete_array_qual_type_in\s*\(/.test(
      declarationResolutionHeader,
    ) ||
    !/ps_ctx_intern_array_of_qual_type_in\s*\(/.test(
      declarationResolutionSource,
    ) ||
    /psx_resolve_incomplete_array_type\s*\(|psx_resolve_completed_incomplete_array_type\s*\(|psx_resolve_incomplete_array_initializer\s*\(/.test(
      `${declarationResolutionHeader}\n${declarationResolutionSource}`,
    )) {
  throw new Error(
    "incomplete array completion must create a canonical QualType without mutable compatibility APIs",
  );
}

if (!/psx_qual_type_t\s+psx_apply_parsed_decl_specifier_qual_type_in_contexts\s*\(/.test(
      declarationApplicationHeader,
    ) ||
    /\bpsx_apply_parsed_decl_specifier_in_contexts\s*\(/.test(
      `${declarationApplicationHeader}\n${declarationApplicationSource}`,
    )) {
  throw new Error(
    "parsed declaration specifier application must publish canonical QualType without a compatibility type-view API",
  );
}

const canonicalDeclarationTypeConsumers = [
  ["aggregate member resolution", aggregateMemberResolutionSource],
  ["parameter declaration resolution", parameterDeclarationResolutionSource],
  ["declaration application", declarationApplicationSource],
  ["function definition pipeline", declarationPipelineSource],
];
for (const [name, source] of canonicalDeclarationTypeConsumers) {
  if (!/\bpsx_resolve_decl_qual_type\s*\(/.test(source) ||
      /\bpsx_build_decl_type\s*\(|\bpsx_resolve_decl_type\s*\(/.test(
        source,
      )) {
    throw new Error(
      `${name} must consume declaration types through canonical QualType`,
    );
  }
}
if (/\bps_type_derived_leaf_type\s*\(|\bps_type_is_tag_aggregate\s*\(/.test(
      parameterDeclarationResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      parameterDeclarationResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      parameterDeclarationResolutionSource,
    )) {
  throw new Error(
    "parameter declaration adjustment must traverse canonical TypeId relations",
  );
}

const declarationQualTypeCore = declarationResolutionSource.match(
  /psx_qual_type_t\s+psx_resolve_decl_qual_type\s*\([^]*?\n\}/,
);
if (!/psx_qual_type_t\s+psx_resolve_decl_qual_type\s*\(/.test(
      declarationResolutionHeader,
    ) ||
    !declarationQualTypeCore ||
    !/return\s+apply_declarator_shape\s*\(\s*request\s*\)\s*;/.test(
      declarationQualTypeCore[0],
    ) ||
    /ps_ctx_intern_declaration_qual_type_in\s*\(/.test(
      declarationResolutionSource,
    ) ||
    /\bpsx_resolve_decl_type\s*\(/.test(
      `${declarationResolutionHeader}\n${declarationResolutionSource}`,
    )) {
  throw new Error(
    "declaration type resolution must publish canonical QualType without a compatibility type-view API",
  );
}

const staticDataInitializerHeader = await readFile(
  "src/lowering/static_data_initializer.h",
  "utf8",
);
const staticDataInitializerSource = await readFile(
  "src/lowering/static_data_initializer.c",
  "utf8",
);
const staticInitializerResolutionResult =
  staticInitializerClassificationHeader.match(
    /typedef\s+struct\s*\{((?:(?!typedef\s+struct)[\s\S])*?)\}\s*psx_static_initializer_resolution_t\s*;/,
  );
const staticInitializerLoweringInput = staticDataInitializerHeader.match(
  /typedef\s+struct\s+psx_static_initializer_lowering_input_t\s*\{([^]*?)\}\s*psx_static_initializer_lowering_input_t\s*;/,
);
const resolvedStaticInitializerLowering = staticDataInitializerSource.match(
  /int\s+lower_resolved_static_initializer\s*\([^]*?\n\}/,
);
if (!staticInitializerResolutionResult ||
    /parser\/ast\.h|\bnode_t\b|\btoken_t\b/.test(
      staticInitializerClassificationHeader,
    ) ||
    !/#include\s+"static_initializer_classification\.h"/.test(
      staticInitializerResolutionHeader,
    ) ||
    /parser\/ast\.h|static_initializer_resolution\.h|\bnode_t\b|\bnode_init_list_t\b|\btoken_t\b/.test(
      staticDataInitializerHeader,
    ) ||
    /\bnode_t\b|\bpsx_hir_|aggregate_plan|\binitializer\s*;/.test(
      staticInitializerResolutionResult[1],
    ) ||
    !/\bpsx_qual_type_t\s+object_qual_type\s*;/.test(
      staticInitializerResolutionResult[1],
    ) ||
    !/\bint\s+scalar_list_value_selected\s*;/.test(
      staticInitializerResolutionResult[1],
    ) ||
    !staticInitializerLoweringInput ||
    !/const\s+psx_static_initializer_resolution_t\s*\*resolution\s*;/.test(
      staticInitializerLoweringInput[1],
    ) ||
    !/const\s+psx_static_aggregate_initializer_plan_t\s*\*aggregate_plan\s*;/.test(
      staticInitializerLoweringInput[1],
    ) ||
    !/const\s+psx_hir_module_t\s*\*initializer_hir\s*;/.test(
      staticInitializerLoweringInput[1],
    ) ||
    !resolvedStaticInitializerLowering ||
    /\bnode_t\b|\bND_[A-Z0-9_]+\b|lower_static_object_initializer\s*\(|lower_static_scalar_expression\s*\(/.test(
      resolvedStaticInitializerLowering[0],
    ) ||
    !/initializer->aggregate_plan/.test(
      resolvedStaticInitializerLowering[0],
    ) ||
    !/initializer->initializer_hir/.test(
      resolvedStaticInitializerLowering[0],
    )) {
  throw new Error(
    "static initializer semantics must return AST-free classification while lowering consumes only aggregate plans or HIR",
  );
}
if (/AGC_STATIC_INITIALIZER_COMPAT|static_data_initializer_compat|\bnode_t\b|\bND_[A-Z0-9_]+\b|lower_static_(?:object|scalar_array)_initializer|psx_build_static_aggregate_initializer_plan/.test(
      `${staticDataInitializerSource}\n${staticDataInitializerHeader}\n${makefileSource}`,
    )) {
  throw new Error(
    "legacy AST static initializer lowering must not remain in source or build contracts",
  );
}
const initializerResolutionSource = await readFile(
  "src/semantic/initializer_resolution.c",
  "utf8",
);
if (!/\}\s*psx_initializer_designator_range_t\s*;/.test(
      initializerResolutionSource,
    ) ||
    !/\(size_t\)entry->designator_count\s*\*\s*sizeof\(\*ranges\)/.test(
      initializerResolutionSource,
    ) ||
    /range_(?:overrides|begins|ends|indices)\s*\[\s*8\s*\]/.test(
      initializerResolutionSource,
    )) {
  throw new Error(
    "semantic initializer range state must scale with the complete designator path",
  );
}
const initializerMemberRef = initializerResolutionHeader.match(
  /typedef\s+struct\s*\{([\s\S]*?)\}\s*psx_initializer_member_ref_t\s*;/,
);
if (!initializerMemberRef ||
    !/\bpsx_record_id_t\s+record_id\s*;/.test(initializerMemberRef[1]) ||
    !/\bint\s+member_index\s*;/.test(initializerMemberRef[1]) ||
    !/\bpsx_record_member_layout_t\s+layout\s*;/.test(
      initializerMemberRef[1],
    ) ||
    /\bdirect_member\b/.test(initializerResolutionHeader) ||
    /\bdirect_member\b/.test(initializerResolutionSource) ||
    /\bdirect_member\b/.test(staticDataInitializerSource) ||
    /\bdirect_member\b/.test(syntaxTypedHirResolutionSource) ||
    /\bps_node_new_tag_member_lvar_ref_for_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "initializer member identity and placement must remain explicit and separate",
  );
}
if (/\bmember->offset\b/.test(staticHirInitializerSource) ||
    /\baggregate_definition\b/.test(staticHirInitializerSource) ||
    !/\bpsx_record_decl_table_lookup\s*\(/.test(
      staticHirInitializerSource,
    ) ||
    !/\bpsx_hir_node_is_resolved_initializer_entry\s*\(/.test(
      staticHirInitializerSource,
    ) ||
    !/\bpsx_hir_node_object_offset\s*\(/.test(
      staticHirInitializerSource,
    ) ||
    /\baggregate_member_layout\s*\(|\bps_lowering_lookup_record_member\s*\(|\bpsx_resolve_initializer_member_target_with_records\s*\(/.test(
      staticHirInitializerSource,
    )) {
  throw new Error(
    "initializer lowering must consume semantic TypeId and offset metadata without re-resolving members",
  );
}
if (/\brecord_member_lookup(?:_context)?\b|\bps_lowering_lookup_record_member\s*\(/.test(
      `${loweringRuntimeSource}\n${loweringRuntimeHeader}\n${compilationSession}`,
    ) ||
    /\bpsx_resolve_initializer_member_target_with_records\s*\(/.test(
      `${initializerResolutionHeader}\n${explicitDiagnosticInitializerResolutionSource}`,
    )) {
  throw new Error(
    "record member names must be resolved during semantic initializer planning, not through lowering callbacks",
  );
}
if (/\baggregate_definition\b/.test(initializerResolutionSource) ||
    !/\bpsx_record_decl_table_lookup\s*\(/.test(
      initializerResolutionSource,
    ) ||
    !/member_shape\.kind\s*==\s*PSX_TYPE_ARRAY\s*\|\|\s*member_shape\.kind\s*==\s*PSX_TYPE_COMPLEX\s*\|\|\s*psx_type_kind_is_aggregate\(member_shape\.kind\)/.test(
      initializerResolutionSource,
    )) {
  throw new Error(
    "initializer semantics must resolve aggregate declarations through RecordDeclTable and flatten complex members into both components",
  );
}
if (!/\bunsigned\s+char\s+bit_width\s*;/.test(
      initializerResolutionHeader,
    ) ||
    !/record->members\[child_index\]\.bit_width\s*>\s*0[^]*?leaf->member_ref\.record_id\s*!=\s*parent_shape\.record_id[^]*?leaf->member_ref\.member_index\s*!=\s*child_index/.test(
      initializerResolutionSource,
    ) ||
    !/target\.bit_width\s*=\s*\(unsigned char\)bit_width/.test(
      staticHirInitializerSource,
    ) ||
    !/target->bit_width\s*>\s*0[^]*?unit_mask[^]*?packed\s*=\s*\(packed\s*&\s*~unit_mask\)/.test(
      staticHirInitializerSource,
    ) ||
    !/gvar_init_cursor_advance_at_offset\s*\([^,]*,\s*base_offset\s*\+\s*unit_off\s*\)/.test(
      nodeUtilsSource,
    )) {
  throw new Error(
    "bitfield initializer leaves sharing one offset must retain member identity and one packed storage unit",
  );
}
if (!/\}\s*psx_initializer_union_activation_t\s*;/.test(
      initializerResolutionSource,
    ) ||
    !/activation\s*&&\s*activation->member_index\s*==\s*member_index/.test(
      initializerResolutionSource,
    ) ||
    !/flat_initializer_clear_nested_union_activations\s*\(\s*context\s*,\s*parent\s*\)/.test(
      initializerResolutionSource,
    ) ||
    !/flat_initializer_reset_aggregate_target\s*\(\s*context\s*,\s*target\s*\)/.test(
      initializerResolutionSource,
    ) ||
    !/replacement\.count\s*!=\s*target->leaf_end\s*-\s*target->leaf_begin/.test(
      initializerResolutionSource,
    ) ||
    !/\bunsigned\s+char\s+is_whole_object_value\s*;/.test(
      initializerResolutionHeader,
    ) ||
    !/flat_initializer_relocate_whole_object_value\s*\(\s*context\s*,\s*target->leaf_begin\s*\)/.test(
      initializerResolutionSource,
    ) ||
    !/item->is_active\s*=\s*1\s*;[^]*?item->is_object_copy\s*=\s*0\s*;[^]*?item->is_whole_object_value\s*=\s*0\s*;/.test(
      initializerResolutionSource,
    ) ||
    !/for\s*\(\s*int\s+whole_pass\s*=\s*1\s*;\s*whole_pass\s*>=\s*0\s*;\s*whole_pass--\s*\)/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "repeated designators must rebuild whole subobjects and emit retained whole-object values before scalar overrides",
  );
}

const recordDeclTableHeader = await readFile(
  "src/semantic/record_decl_table.h",
  "utf8",
);
const recordDeclTableSource = await readFile(
  "src/semantic/record_decl_table.c",
  "utf8",
);
if (!/\bpsx_record_decl_table_define\s*\(/.test(recordDeclTableHeader) ||
    !/\bpsx_record_decl_table_lookup\s*\(/.test(recordDeclTableHeader) ||
    !/records\s*\[\s*record->record_id\s*\]\s*=\s*record/.test(
      recordDeclTableSource,
    ) ||
    !/\bps_lowering_record_decls\s*\(/.test(loweringRuntimeSource) ||
    !/\.record_decls\s*=\s*ps_ctx_record_decl_table_in\s*\(\s*session->semantic_context\s*\)/.test(
      compilationSessionSource,
    )) {
  throw new Error(
    "RecordDeclTable must be an explicit semantic-to-lowering phase input",
  );
}
if (/\bpsx_type_t\b|\bnode_t\b|\bND_[A-Z0-9_]+\b|parser\/ast\.h/.test(
      `${staticDataInitializerSource}\n${staticDataInitializerHeader}`,
    ) ||
    /\bps_lowering_type_id\s*\(|\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(|\bps_node_get_type\s*\(/.test(
      staticDataInitializerSource,
    ) ||
    !/\bps_gvar_decl_type_id\s*\(/.test(staticDataInitializerSource) ||
    !/\bps_global_registry_complete_array_qual_type\s*\(/.test(
      staticDataInitializerSource,
    ) ||
    !/\bpsx_lower_static_scalar_hir_initializer\s*\(/.test(
      staticDataInitializerSource,
    )) {
  throw new Error(
    "static initializer phase bridge must consume canonical classification and HIR without compatibility types or AST",
  );
}
if (!/\bpsx_qual_type_t\s+object_qual_type\b/.test(
      staticInitializerClassificationHeader,
    ) ||
    !/psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts\s*\([^)]*\bpsx_qual_type_t\s+type\b/.test(
      semanticPipelineSource,
    ) ||
    /psx_build_static_aggregate_initializer_plan\s*\(/.test(
      declarationPipelineSource,
    )) {
  throw new Error(
    "static aggregate initializer plans must preserve canonical QualType identity",
  );
}
const staticInitializerPlanSource = await readFile(
  "src/lowering/static_initializer_plan.h",
  "utf8",
);
if (!/\bint\s*\*\s*init_offsets\s*;/.test(gvarStruct[1]) ||
    !/\bps_gvar_init_slot_set_offset\s*\(/.test(gvarPublicSource) ||
    !/\bint\s*\*\s*offsets\s*;/.test(staticInitializerPlanSource) ||
    !/\bps_gvar_init_slot_set_offset\s*\([^]*?aggregate\.leaves\.items\[i\]\.relative_offset/.test(
      staticHirInitializerSource,
    ) ||
    !/\.offsets\s*=\s*temporary\.init_offsets/.test(
      staticHirInitializerSource,
    ) ||
    !/global->init_offsets\s*=\s*plan->offsets/.test(
      staticDataInitializerSource,
    ) ||
    !/gv->init_offsets\s*=\s*malloc\s*\(/.test(nodeUtilsSource) ||
    !/gv->init_offsets\s*=\s*realloc\s*\(/.test(nodeUtilsSource) ||
    !/gv->init_offsets\s*\)\s*gv->init_offsets\[idx\]\s*=\s*-1/.test(
      nodeUtilsSource,
    ) ||
    !/\bgvar_init_cursor_advance_at_offset\s*\(/.test(nodeUtilsSource)) {
  throw new Error(
    "static aggregate initializer slots must preserve resolved byte offsets through recursive union walking",
  );
}
const canonicalArrayCompletion = globalRegistrySource.match(
  /int\s+ps_global_registry_complete_array_qual_type\s*\([^]*?\n\}/,
);
if (!canonicalArrayCompletion ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      canonicalArrayCompletion[0],
    ) ||
    /\bpsx_type_compatibility_canonical_view_for\s*\(/.test(
      canonicalArrayCompletion[0],
    )) {
  throw new Error(
    "canonical global array completion must validate TypeShape without restoring compatibility type views",
  );
}
if (!/\bpsx_qual_type_t\s+qual_type\s*=\s*ps_gvar_decl_qual_type\s*\(\s*global\s*\)\s*;[^]*?\bqual_type_size\s*\(\s*lowering\s*,\s*qual_type\s*\)/.test(
      translationUnitDataLoweringSource,
    ) ||
    !/\bpsx_collect_initializer_scalar_leaves_with_records\s*\([^]*?\bps_gvar_decl_qual_type\s*\(\s*ctx->global\s*\)\s*,\s*0\s*,/.test(
      translationUnitDataLoweringSource,
    ) ||
    !/\bps_global_registry_bind_decl_qual_type\s*\([^]*?\(psx_qual_type_t\)\{type_id,\s*PSX_TYPE_QUALIFIER_NONE\}[^]*?\bpsx_collect_initializer_scalar_leaves_with_records\s*\([^]*?\bps_gvar_decl_qual_type\s*\(\s*&temporary\s*\)\s*,\s*0\s*,/.test(
      staticHirInitializerSource,
    )) {
  throw new Error(
    "global data and static HIR initializer root layout must consume canonical declaration type identity",
  );
}
if (/\bstorage_alignment\s*\(/.test(translationUnitDataLoweringSource) ||
    /storage_size\s*>=/.test(translationUnitDataLoweringSource) ||
    /\bpsx_semantic_type_table_find\s*\(/.test(
      translationUnitDataLoweringSource,
    ) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      translationUnitDataLoweringSource,
    )) {
  throw new Error(
    "global object storage alignment and incomplete-array element layout must come from TypeId target layout",
  );
}
const globalAggregateScalarLowering =
  translationUnitDataLoweringSource.match(
    /static\s+void\s+lower_aggregate_scalar\s*\([^]*?\n\}/,
  );
if (!/void\s*\(\s*\*scalar\s*\)\s*\([^;]*\bpsx_type_id_t\s+value_type_id\b/.test(
      gvarPublicSource,
    ) ||
    !globalAggregateScalarLowering ||
    !/\bvalue_type_id\b/.test(globalAggregateScalarLowering[0]) ||
    /\bpsx_semantic_type_table_find\s*\(/.test(
      globalAggregateScalarLowering[0],
    ) ||
    !/\bpsx_semantic_type_table_record_member\s*\(/.test(
      nodeUtilsSource,
    ) ||
    !/\bpsx_semantic_type_table_array_leaf\s*\(/.test(nodeUtilsSource)) {
  throw new Error(
    "aggregate initializer scalar events must carry member value TypeIds through recursive traversal",
  );
}
const aggregateWalkerLayoutSection = nodeUtilsSource.match(
  /static\s+int\s+gvar_member_value_size_for_target\s*\([^]*?psx_gvar_init_slot_t\s+ps_gvar_init_slot_view\s*\(/,
);
const recordIdentityAggregateWalker = nodeUtilsSource.match(
  /static\s+int\s+gvar_walk_aggregate_initializer\s*\([^]*?\n\}/,
);
const recordUnionInitializerSelector = nodeUtilsSource.match(
  /static\s+int\s+record_union_init_member_for_slot\s*\([^;]*?\)\s*\{[^]*?\n\}/,
);
if (!aggregateWalkerLayoutSection ||
    /->\s*aggregate_definition\b/.test(nodeUtilsSource) ||
    !/\bpsx_record_decl_table_lookup\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !/\bpsx_qual_type_layout_sizeof\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !/\bgvar_member_value_size_for_target\s*\([^)]*\bpsx_qual_type_t\s+value_qual_type\b/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !/\bgvar_member_storage_size_for_target\s*\([^)]*\bpsx_qual_type_t\s+member_qual_type\b/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !/\bgvar_get_record_member_layout\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    /\btag_member_info_t\b/.test(aggregateWalkerLayoutSection[0]) ||
    !/\bgvar_resolved_record_find_unnamed_union_covering_offset\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !/\bgvar_aggregate_member_iter_note_cover\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !/\bpsx_record_layout_table_lookup\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    /\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    /\bpsx_record_member_decl_type\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !/\bpsx_record_member_decl_leaf_shape\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !/\bpsx_semantic_type_table_array_flat_element_count\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    /\b(?:ctx_get_tag_member_scoped|gvar_tag_identity|tag_scope_depth_p1)\b/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    /\bps_ctx_(?:get|find)_tag_member/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !recordIdentityAggregateWalker ||
    /\bpsx_semantic_context_t\b|\btag_(?:kind|name|len|scope_depth_p1)\b|\bps_ctx_(?:get|find)_tag_member/.test(
      recordIdentityAggregateWalker[0],
    ) ||
    !recordUnionInitializerSelector ||
    !/\brecord_union_init_member_for_slot\s*\(\s*const\s+psx_semantic_type_table_t\s*\*/.test(
      recordUnionInitializerSelector[0],
    ) ||
    /\bpsx_semantic_context_t\b|\bps_ctx_(?:get|find)_tag_member/.test(
      recordUnionInitializerSelector[0],
    ) ||
    !/if\s*\(\s*!layout\.record_decl\s*\)\s*return\s+0\s*;/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    /\bps_type_sizeof_for_target\s*\(|\bps_tag_member_decl_value_size\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !/\bps_gvar_walk_resolved_aggregate_initializer\s*\(\s*const\s+psx_semantic_type_table_t\s*\*[^,]*,\s*const\s+psx_record_decl_table_t\s*\*[^,]*,\s*const\s+psx_record_layout_table_t\s*\*[^,]*,\s*const\s+ag_target_info_t\s*\*[^,]*,\s*psx_qual_type_t\s+root_qual_type/.test(
      gvarPublicSource,
    ) ||
    /\bps_gvar_storage_size\s*\(|\bsymbol_alignment\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    !/\bpsx_qual_type_layout_(?:size|align)of\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    /\bps_type_(?:size|align)of_id_for_target\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    /\bpsx_semantic_type_table_(?:create|intern)\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    !/\bconst\s+psx_semantic_type_table_t\s*\*semantic_types\s*;/.test(
      irBuildOptionsHeader,
    ) ||
    !/\bconst\s+psx_record_decl_table_t\s*\*record_decls\s*;/.test(
      irBuildOptionsHeader,
    ) ||
    !/\.semantic_types\s*=\s*ps_ctx_semantic_type_table_in\s*\(/.test(
      compilerMainSource,
    ) ||
    !/\.record_decls\s*=\s*ps_ctx_record_decl_table_in\s*\(/.test(
      compilerMainSource,
    ) ||
    !/\.record_layouts\s*=\s*ps_ctx_record_layout_table_in\s*\(/.test(
      compilerMainSource,
    )) {
  throw new Error(
    "aggregate walking and IR symbol layout must preserve QualType and consume explicit TargetSpec",
  );
}
const nameClassifierHeader = await readFile(
  "src/parser/name_classifier.h",
  "utf8",
);
const declarationSyntaxHeader = await readFile(
  "src/parser/declaration_syntax.h",
  "utf8",
);
const declarationSyntaxOptionsDefinition =
  declarationSyntaxHeader.match(
    /typedef\s+struct\s*\{([^]*?)\}\s*psx_decl_specifier_syntax_options_t\s*;/,
  )?.[1] ?? "";
const parsedConstExpressionDefinition =
  declarationSyntaxHeader.match(
    /typedef\s+struct\s*\{([^]*?)\}\s*psx_parsed_const_expr_t\s*;/,
  )?.[1] ?? "";
const parsedAlignasDefinition =
  declarationSyntaxHeader.match(
    /typedef\s+struct\s*\{([^]*?)\}\s*psx_parsed_alignas_t\s*;/,
  )?.[1] ?? "";
const typeSpecifierSyntaxDefinition =
  parserCoreHeader.match(
    /typedef\s+struct\s*\{([^]*?)\}\s*psx_type_spec_syntax_t\s*;/,
  )?.[1] ?? "";
const declarationParserSyntaxOnlySource = [
  parserDeclarationSyntaxSource,
  functionParameterSyntaxSource,
  declaratorSyntaxSource,
  aggregateMemberSyntaxSource,
  localDeclarationSyntaxSource,
  toplevelDeclarationSyntaxSource,
].join("\n");
if (/\bpsx_(?:semantic_context|global_registry|local_registry)_t\b/.test(
      `${declarationSyntaxOptionsDefinition}\n${toplevelSyntaxContextDefinition}\n${typeSpecifierSyntaxDefinition}\n${declarationParserSyntaxOnlySource}`,
    ) ||
    /#include\s+"(?:semantic_ctx|global_registry|local_registry)\.h"/.test(
      declarationParserSyntaxOnlySource,
    ) ||
    /#include\s+"alignas_value\.h"/.test(
      parserDeclarationSyntaxSource,
    ) ||
    !/const\s+psx_name_classifier_t\s*\*name_classifier\s*;/.test(
      declarationSyntaxOptionsDefinition,
    ) ||
    !/ag_diagnostic_context_t\s*\*diagnostics\s*;/.test(
      typeSpecifierSyntaxDefinition,
    ) ||
    !/const\s+psx_name_classifier_t\s*\*name_classifier\s*;/.test(
      typeSpecifierSyntaxDefinition,
    ) ||
    !/node_t\s*\*node\s*;/.test(parsedConstExpressionDefinition) ||
    /\b(?:constant_value|has_constant_value)\b/.test(
      parsedConstExpressionDefinition,
    ) ||
    !/node_t\s*\*expression\s*;/.test(parsedAlignasDefinition) ||
    !/psx_parsed_type_name_t\s*\*type_name\s*;/.test(
      parsedAlignasDefinition,
    ) ||
    !/psx_parsed_alignas_t\s+alignas_specifiers\s*\[8\]\s*;/.test(
      declarationSyntaxHeader,
    ) ||
    /alignas_expressions|alignas_expression_count/.test(
      declarationSyntaxHeader,
    ) ||
    !/psx_token_starts_type_name_syntax\s*\(/.test(
      parserDeclarationSyntaxSource,
    ) ||
    !/psx_parse_type_name_syntax_at\s*\(/.test(
      parserDeclarationSyntaxSource,
    ) ||
    !/parse_assignment_expression\s*\(options->expression_context\)/.test(
      parserDeclarationSyntaxSource,
    ) ||
    !/alignas->type_name[^]*?psx_dispose_type_name_syntax\s*\(alignas->type_name\)[^]*?free\s*\(alignas->type_name\)/.test(
      parserDeclarationSyntaxSource,
    ) ||
    !/token_kind_t\s+psx_consume_type_kind_with_syntax_ex\s*\(\s*psx_type_spec_result_t\s*\*out\s*,\s*const\s+psx_type_spec_syntax_t\s*\*syntax\s*\)/.test(
      parserCoreHeader,
    ) ||
    !/void\s+psx_materialize_declarator_expression_syntax\s*\(/.test(
      declarationSyntaxHeader,
    )) {
  throw new Error(
    "declaration parser cores must build typeless Syntax with NameClassifier and syntax services only",
  );
}
const nameClassifierUsers = [
  "src/parser/declaration_syntax.c",
  "src/parser/function_parameter_syntax.c",
  "src/parser/aggregate_member_syntax.c",
  "src/parser/local_declaration_syntax.c",
  "src/parser/toplevel_declaration_syntax.c",
  "src/parser/expr.c",
  "src/parser/stmt.c",
  "src/parser/enum_const.c",
];
const directTypedefLookupUsers = [];
const implicitNameClassifierUsers = [];
for (const file of nameClassifierUsers) {
  const source = await readFile(file, "utf8");
  if (/\bpsx_ctx_is_typedef_name_token_in\s*\(/.test(source)) {
    directTypedefLookupUsers.push(file);
  }
  if (/\bps_ctx_name_classifier\s*\(/.test(source)) {
    implicitNameClassifierUsers.push(file);
  }
}
if (!/typedef\s+struct\s*\{[^]*?void\s*\*context\s*;[^]*?psx_typedef_name_classifier_fn\s+is_typedef_name\s*;[^]*?\}\s*psx_name_classifier_t\s*;/.test(
      nameClassifierHeader,
    ) ||
    !/ps_name_classifier_is_typedef_name\s*\(\s*const\s+psx_name_classifier_t\s*\*classifier\s*,\s*const\s+token_t\s*\*token\s*\)/.test(
      nameClassifierHeader,
    ) ||
    !/\bconst\s+psx_name_classifier_t\s*\*name_classifier\s*;/.test(
      declarationSyntaxHeader,
    ) ||
    /\bpsx_decl_typedef_name_predicate_t\b/.test(
      `${declarationSyntaxHeader}\n${nameClassifierHeader}`,
    ) ||
    directTypedefLookupUsers.length > 0 ||
    implicitNameClassifierUsers.length > 0) {
  throw new Error(
    "parser typedef ambiguity must be isolated behind the NameClassifier interface" +
      (directTypedefLookupUsers.length
        ? `:\n${directTypedefLookupUsers.join("\n")}`
        : implicitNameClassifierUsers.length
          ? `:\n${implicitNameClassifierUsers.join("\n")}`
          : ""),
  );
}
if (/#include\s+"semantic_ctx\.h"|\bpsx_semantic_context_t\b|\bps_ctx_[A-Za-z0-9_]+\s*\(/.test(
      `${parserSource}\n${parserCoreHeader}`,
    ) ||
    allSourceFiles.includes("src/parser/parser_legacy.c") ||
    allSourceFiles.includes("src/parser/parser_legacy.h")) {
  throw new Error(
    "parser core must depend on NameClassifier and syntax services without semantic-context wrappers",
  );
}

const parsedNumberLiteral = parserExpressionSource.match(
  /static\s+node_t\s*\*parse_num_literal\s*\([^]*?\n\}\n\n\/\/ 内容文字列/,
);
const parsedStringLiteral = parserExpressionSource.match(
  /static\s+node_string_t\s*\*make_string_lit_node\s*\([^]*?\n\}\n\n\/\/ 連続する/,
);
const stringNodeStruct = astSource.match(
  /struct node_string_t\s*\{([^{}]*)\};/,
);
const syntaxIntConstructor = nodeUtilsSource.match(
  /node_t\s*\*psx_node_new_syntax_int_in\s*\([^]*?\n\}/,
);
const syntaxLiteralParserSource = [
  parserExpressionSource,
  parserStatementSource,
  parserLocalDeclarationSource,
].join("\n");
if (!parsedNumberLiteral ||
    !parsedStringLiteral ||
    !syntaxIntConstructor ||
    /\bps_(?:type_new|node_bind_type)|->base\.type\s*=/.test(
      `${parsedNumberLiteral?.[0] ?? ""}\n${parsedStringLiteral?.[0] ?? ""}`,
    ) ||
    /\bps_(?:type_new|node_bind_type)/.test(
      syntaxIntConstructor?.[0] ?? "",
    ) ||
    /\bps_node_new_num_in\s*\(/.test(syntaxLiteralParserSource) ||
    !/\bbuild_direct_literal\s*\(/.test(syntaxTypedHirResolutionSource) ||
    !/psx_resolve_number_literal_semantics_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_string_literal_semantics_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /\bfval_id\b/.test(numberNodeStruct?.[1] ?? "") ||
    !stringNodeStruct ||
    /\bstring_label\b/.test(stringNodeStruct[1]) ||
    /\bpsx_string_literal_(?:bind_label|label)\s*\(/.test(
      literalResolutionHeader + literalResolutionSource,
    ) ||
    /\bps_node_resolution_state(?:_const)?\s*\(/.test(
      literalResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_qual_type_is_valid\s*\(/.test(
      literalResolutionSource,
    ) ||
    /\bps_ctx_type_by_id_in\s*\(/.test(literalResolutionSource)) {
  throw new Error(
    "parser literals must remain typeless syntax while Typed HIR owns resolved literal metadata",
  );
}
if (/\bps_type_new_[a-z0-9_]*\s*\(|\bps_node_bind_(?:type|qual_type)\s*\(|(?:->|\.)base\.type\s*=/.test(
      parserExpressionSource,
    ) ||
    /\bps_global_registry_next_(?:string|float)_literal_id\s*\(|\bpsx_register_(?:string|float)_lit_in\s*\(/.test(
      parserExpressionSource,
    ) ||
    !/\bpsx_resolve_number_literal_semantics_in_contexts\s*\([^]*?\bpsx_register_float_lit_in\s*\(/.test(
      literalResolutionSource,
    ) ||
    !/\bpsx_resolve_string_literal_value_in_contexts\s*\([^]*?\bpsx_register_string_lit_in\s*\(/.test(
      literalResolutionSource,
    )) {
  throw new Error(
    "expression parser must build typeless Syntax AST nodes without canonical type or literal-pool registration",
  );
}

const identifierSyntaxParser = parserExpressionSource.match(
  /static\s+node_t\s*\*parse_identifier_syntax\s*\([^]*?\n\}/,
);
const predefinedFunctionNameSyntaxAdapters = [
  expressionSyntaxContextSource,
  expressionSyntaxAdapterSource,
  statementSyntaxAdapterSource,
  statementSyntaxAdapterHeader,
  localDeclarationFrontendSource,
  localDeclarationFrontendHeader,
].join("\n");
if (!identifierSyntaxParser ||
    /__func__|make_func_name_string_node/.test(
      identifierSyntaxParser?.[0] ?? "",
    ) ||
    /current_function_name/.test(predefinedFunctionNameSyntaxAdapters) ||
    !/psx_string_literal_value_t/.test(literalResolutionHeader) ||
    !/direct_is_predefined_function_name\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/build_direct_string_value\s*\([^]*?context->function_name/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "predefined function names must remain identifier Syntax and resolve from semantic function context",
  );
}

const builtinExpectDirectUses =
  syntaxTypedHirResolutionSource.match(
    /psx_builtin_expect_value_operand\s*\(/g,
  ) ?? [];
if (/__builtin_expect|try_parse_builtin_expect/.test(
      parserExpressionSource,
    ) ||
    !/PSX_BUILTIN_CALL_EXPECT/.test(functionCallResolutionHeader) ||
    !/psx_function_call_builtin_kind\s*\(/.test(
      functionCallResolutionSource,
    ) ||
    !/__builtin_expect/.test(functionCallResolutionSource) ||
    builtinExpectDirectUses.length < 3) {
  throw new Error(
    "builtin calls must remain ordinary call Syntax and fold only through shared semantic classification",
  );
}

if (!/psx_resolve_number_literal_semantics_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_string_literal_semantics_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_node_builder_leaf_expression\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/preflight_direct_expression\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_identifier_expression\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /\bpsx_resolve_identifier\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_global_hir_symbol_spec_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_source_cast_qual_types\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_HIR_GLOBAL/.test(syntaxTypedHirResolutionSource) ||
    !/PSX_HIR_FUNCTION_REF/.test(syntaxTypedHirResolutionSource) ||
    !/PSX_HIR_ADDRESS/.test(syntaxTypedHirResolutionSource) ||
    !/PSX_HIR_DEREF/.test(syntaxTypedHirResolutionSource) ||
    !/psx_resolve_deref_operand_qual_type_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_indirection_result_qual_type_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_address_result_qual_type_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_node_expression_qual_type\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_arithmetic_unary_result_qual_type_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_binary_result_qual_type_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_conditional_qual_types_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_CONDITIONAL_CONDITION_NOT_SCALAR/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/then_is_null_pointer_constant/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/else_is_null_pointer_constant/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /ps_type_(?:binary|conditional)_result_for_target_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_node_builder_expression\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_SYNTAX_TYPED_HIR_REJECTED/.test(
      syntaxTypedHirResolutionHeader,
    ) ||
    /psx_resolution_work_tree_|\bpsx_resolution_work_tree_t\b|ps_node_bind_|ps_node_resolution_state|ps_node_get_type|ps_node_qual_type/.test(
      `${syntaxTypedHirResolutionHeader}\n${syntaxTypedHirResolutionSource}`,
    ) ||
    /\bsyntax(?:_expression)?->(?:kind|lhs|rhs|tok)\s*=(?!=)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_node_resolution_state|ps_node_bind_/.test(
      literalResolutionSource.match(
        /int\s+psx_resolve_number_literal_semantics_in_contexts\s*\([^]*?\n\}/,
      )?.[0] ?? "",
    ) ||
    /psx_node_resolution_state|ps_node_bind_/.test(
      literalResolutionSource.match(
        /int\s+psx_resolve_string_literal_semantics_in_contexts\s*\([^]*?\n\}/,
      )?.[0] ?? "",
    )) {
  throw new Error(
    "literal semantics must be a Syntax-preserving value result consumed by direct Typed HIR resolution",
  );
}

if (!/\bpsx_qual_type_t\s+declaration_qual_type\s*;/.test(
      identifierResolutionHeader,
    ) ||
    !/\bpsx_qual_type_t\s+expression_qual_type\s*;/.test(
      identifierResolutionHeader,
    ) ||
    !/\bint\s+decays_array_to_address\s*;/.test(
      identifierResolutionHeader,
    ) ||
    !/\bint\s+decays_function_to_pointer\s*;/.test(
      identifierResolutionHeader,
    ) ||
    !/\bglobal_var_t\s*\*\s*static_storage_global\s*;/.test(
      identifierResolutionHeader,
    ) ||
    !/\bint\s+local_has_static_storage\s*;/.test(
      identifierResolutionHeader,
    ) ||
    !/\bint\s+local_is_vla\s*;/.test(
      identifierResolutionHeader,
    ) ||
    !/ps_ctx_intern_pointer_to_qual_type_in\s*\(/.test(
      identifierResolutionSource,
    ) ||
    !/psx_resolve_global_hir_symbol_spec_in\s*\(/.test(
      hirSymbolResolutionSource,
    ) ||
    /ps_type_(?:sizeof|alignof)_id\s*\(/.test(
      hirSymbolResolutionSource,
    ) ||
    !/psx_qual_type_layout_sizeof\s*\(/.test(
      hirSymbolResolutionSource,
    ) ||
    !/psx_qual_type_layout_alignof\s*\(/.test(
      hirSymbolResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_qual_type_is_valid\s*\(/.test(
      hirSymbolResolutionSource,
    ) ||
    /\bps_ctx_type_by_id_in\s*\(/.test(hirSymbolResolutionSource)) {
  throw new Error(
    "identifier decay and global HIR layout must be canonical semantic values consumed by direct materialization",
  );
}

const directLocalHirSpecCalls =
  syntaxTypedHirResolutionSource.match(
    /\bpsx_resolve_local_hir_node_spec_in\s*\(/g,
  ) ?? [];
const directTreeWrapIndex = syntaxTypedHirResolutionSource.indexOf(
  "wrap_typed_root(",
  syntaxTypedHirResolutionSource.indexOf(
    "psx_resolve_syntax_expression_direct_to_typed_hir_in_contexts",
  ),
);
const directUsageRecordIndex = syntaxTypedHirResolutionSource.indexOf(
  "record_direct_identifier_usage(&context)",
);
if (!/ps_lvar_static_storage_global\s*\(/.test(
      identifierResolutionSource,
    ) ||
    /->static_(?:global|global_name)\b/.test(
      `${identifierResolutionSource}\n${syntaxTypedHirResolutionSource}`,
    ) ||
    !/psx_resolve_local_hir_node_spec_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    directLocalHirSpecCalls.length < 2 ||
    !/\bPSX_HIR_LOCAL\b/.test(hirLocalResolutionSource) ||
    !/ps_lvar_frame_storage_size\s*\(/.test(
      hirLocalResolutionSource,
    ) ||
    !/ps_lvar_vla_row_stride_frame_off\s*\(/.test(
      hirLocalResolutionSource,
    ) ||
    !/ps_lvar_vla_param_inner_dim_count\s*\(/.test(
      hirLocalResolutionSource,
    ) ||
    !/ps_decl_record_lvar_usage_in_region_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    directTreeWrapIndex < 0 || directUsageRecordIndex < directTreeWrapIndex) {
  throw new Error(
    "local, VLA, and static-local identifier resolution must share canonical HIR storage metadata and record usage only after direct resolution succeeds",
  );
}

if (/\bnode_t\b|\bND_[A-Z0-9_]+\b|PSX_HIR_|parser\/(?:ast|type)\.h|\bpsx_type_t\b|\bps_ctx_type_by_id_in\s*\(|\bps_type_[A-Za-z0-9_]*\s*\(/.test(
      `${assignmentResolutionHeader}\n${assignmentResolutionSource}`,
    ) ||
    /\bnode_t\b|\bND_[A-Z0-9_]+\b|PSX_HIR_|parser\/(?:ast|type)\.h|\bpsx_type_t\b|\bps_ctx_type_by_id_in\s*\(|\bps_type_[A-Za-z0-9_]*\s*\(/.test(
      `${typeCompletenessHeader}\n${typeCompletenessSource}`,
    ) ||
    !/\bpsx_type_shape_t\b/.test(assignmentResolutionSource) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      assignmentResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_unqualified_types_match\s*\(/.test(
      assignmentResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_function_types_compatible\s*\(/.test(
      assignmentResolutionSource,
    ) ||
    !/target_base\.qualifiers\s*\^\s*value_base\.qualifiers[^]*?PSX_TYPE_QUALIFIER_ATOMIC/.test(
      assignmentResolutionSource,
    ) ||
    !/psx_semantic_pointer_points_to_complete_object_in\s*\(/.test(
      assignmentResolutionSource,
    ) ||
    !/psx_semantic_pointer_points_to_complete_object_in\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/psx_semantic_type_is_complete_object_in\s*\([^]*?PSX_TYPE_VOID[^]*?PSX_TYPE_FUNCTION[^]*?record->is_complete[^]*?type\.array_len\s*>\s*0\s*\|\|\s*type\.is_vla/.test(
      typeCompletenessSource,
    ) ||
    !/\$\(OBJROOT\)\/semantic\/type_completeness\.o/.test(
      makefileSource,
    ) ||
    !/static\s+int\s+semantic_qual_types_match\s*\([^]*?case\s+PSX_TYPE_POINTER:[^]*?case\s+PSX_TYPE_ARRAY:[^]*?case\s+PSX_TYPE_FUNCTION:/.test(
      typeIdentityImplementationSource,
    ) ||
    !/psx_resolve_assignment_qual_types_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_compound_assignment_qual_types_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bPSX_HIR_ASSIGN\b/.test(syntaxTypedHirResolutionSource) ||
    !/\bPSX_HIR_COMPOUND_ASSIGN\b/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/DIRECT_IDENTIFIER_USAGE_INITIALIZED/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "assignment typing must be an AST-independent canonical QualType rule consumed directly by Typed HIR",
  );
}

const sharedCallTypeRuleUses = [
  syntaxTypedHirResolutionSource,
].filter((source) =>
  /\bpsx_resolve_call_qual_types_in\s*\(/.test(source)
).length;
const callArgumentQualTypeRule = callResolutionSource.match(
  /void\s+psx_resolve_call_argument_qual_types_in\s*\([^]*?\n\}/,
);
const sharedCallArgumentTypeRuleUses = [
  syntaxTypedHirResolutionSource,
].filter((source) =>
  /\bpsx_resolve_call_argument_qual_types_in\s*\(/.test(source)
).length;
if (/\bnode_t\b|\bND_[A-Z0-9_]+\b|PSX_HIR_|parser\/ast\.h/.test(
      `${callResolutionHeader}\n${callResolutionSource}`,
    ) ||
    !callArgumentQualTypeRule ||
    /\bnode_t\b|\bND_[A-Z0-9_]+\b|\bPSX_HIR_/.test(
      callArgumentQualTypeRule?.[0] ?? "",
    ) ||
    !/canonical->has_function_prototype\s*==\s*[^;]*candidate->has_function_prototype/.test(
      typeIdentityImplementationSource,
    ) ||
    !/function\.has_function_prototype/.test(callResolutionSource) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      callResolutionSource,
    ) ||
    /\bpsx_type_compatibility_canonical_view_for\s*\(/.test(
      callResolutionSource,
    ) ||
    !/callable_semantic_type\.has_function_prototype/.test(
      hirIrBuilder,
    ) ||
    /enforce_empty_parameter_count/.test(
      `${callResolutionHeader}\n${callResolutionSource}\n${syntaxTypedHirResolutionSource}`,
    ) ||
    !/parameters->count\s*>\s*0\s*\|\|\s*parameters->is_variadic/.test(
      declarationApplicationSource,
    ) ||
    !/\bpsx_semantic_type_table_callable_function\s*\(/.test(
      callResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      callResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_parameter\s*\(/.test(
      callArgumentQualTypeRule?.[0] ?? "",
    ) ||
    !/\bpsx_resolve_assignment_qual_types_in\s*\(/.test(
      callArgumentQualTypeRule?.[0] ?? "",
    ) ||
    /\bpsx_resolve_function_call_type\b|\bpsx_function_call_resolution_t\b|\bPSX_FUNCTION_CALL_RESOLUTION_/.test(
      `${functionCallResolutionHeader}\n${functionCallResolutionSource}`,
    ) ||
    sharedCallTypeRuleUses !== 1 ||
    sharedCallArgumentTypeRuleUses !== 1 ||
    !/PSX_CALL_ARGUMENT_TYPES_INCOMPATIBLE/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_CALL_ARGUMENT_TYPES_DISCARDS_QUALIFIERS/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bPSX_HIR_CALL\b/.test(syntaxTypedHirResolutionSource) ||
    !/\bPSX_HIR_EDGE_CALLEE\b/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bPSX_HIR_EDGE_ARGUMENT\b/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/resolution\.function_qual_type/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "function call typing and argument conversion must use AST-independent QualType rules in direct Typed HIR resolution",
  );
}

const controlExpressionQualTypeRule =
  expressionOperandResolutionSource.match(
    /void\s+psx_resolve_control_expression_qual_type_in\s*\([^]*?\n\}/,
  );
const sharedControlExpressionRuleUses = [
  syntaxTypedHirResolutionSource,
].filter((source) =>
  /\bpsx_resolve_control_expression_qual_type_in\s*\(/.test(source)
).length;
if (
  !controlExpressionQualTypeRule ||
  /\bnode_t\b|\bND_[A-Z0-9_]+\b|\bPSX_HIR_/.test(
    controlExpressionQualTypeRule[0],
  ) ||
  sharedControlExpressionRuleUses !== 1 ||
  !/PSX_CONTROL_EXPRESSION_NOT_SCALAR/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/PSX_CONTROL_EXPRESSION_NOT_INTEGER/.test(
    syntaxTypedHirResolutionSource,
  )
) {
  throw new Error(
    "control-expression typing must be an AST-independent QualType rule in direct Typed HIR resolution",
  );
}

const returnQualTypeRule = assignmentResolutionSource.match(
  /void\s+psx_resolve_return_qual_types_in\s*\([^]*?\n\}/,
);
const sharedReturnTypeRuleUses = [
  syntaxTypedHirResolutionSource,
].filter((source) =>
  /\bpsx_resolve_return_qual_types_in\s*\(/.test(source)
).length;
if (
  !returnQualTypeRule ||
  /\bnode_t\b|\bND_[A-Z0-9_]+\b|\bPSX_HIR_/.test(
    returnQualTypeRule[0],
  ) ||
  sharedReturnTypeRuleUses !== 1 ||
  !/PSX_RETURN_TYPES_INCOMPATIBLE/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/PSX_RETURN_TYPES_DISCARDS_QUALIFIERS/.test(
    syntaxTypedHirResolutionSource,
  )
) {
  throw new Error(
    "return conversion must be an AST-independent QualType rule in direct Typed HIR resolution",
  );
}

const addressOperandQualTypeRule =
  expressionOperandResolutionSource.match(
    /void\s+psx_resolve_address_operand_qual_type_in\s*\([^]*?\n\}/,
  );
const sharedAddressOperandRuleUses = [
  syntaxTypedHirResolutionSource,
].filter((source) =>
  /\bpsx_resolve_address_operand_qual_type_in\s*\(/.test(source)
).length;
const directAddressPreflight = syntaxTypedHirResolutionSource.match(
  /if\s*\(syntax->kind\s*==\s*ND_ADDRESS_OF\s*\)\s*\{[^]*?\n\s*if\s*\(syntax->kind\s*==\s*ND_UNARY_PLUS/,
);
if (
  !addressOperandQualTypeRule ||
  !/\bND_ADDRESS_OF\b/.test(syntaxNodeKindHeader) ||
  /\bND_ADDR\b/.test(syntaxNodeKindHeader) ||
  /\bis_explicit_addr_expr\b/.test(astSource) ||
  /\bbuild_unary_addr_node\b/.test(parserExpressionSource) ||
  !/PSX_HIR_ADDRESS/.test(syntaxTypedHirResolutionSource) ||
  /\bnode_t\b|\bND_[A-Z0-9_]+\b|\bPSX_HIR_/.test(
    addressOperandQualTypeRule[0],
  ) ||
  sharedAddressOperandRuleUses !== 1 ||
  !/PSX_ADDRESS_OPERAND_REQUIRES_ADDRESSABLE_VALUE/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/PSX_ADDRESS_OPERAND_IS_BITFIELD/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/static\s+const\s+node_t\s*\*direct_selected_expression\s*\(/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/if\s*\(syntax->kind\s*==\s*ND_GENERIC_SELECTION\)\s*\{[^]*?preflight_direct_lvalue\s*\(\s*context,\s*selected,/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !directAddressPreflight ||
  !/direct_selected_expression\s*\(\s*context,\s*syntax->lhs\)/.test(
    directAddressPreflight[0],
  ) ||
  /syntax->lhs->kind\s*==\s*ND_GENERIC_SELECTION/.test(
    directAddressPreflight[0],
  )
) {
  throw new Error(
    "explicit address operands must use one AST-independent value-category and QualType rule in direct resolution",
  );
}

const subscriptQualTypeCore = expressionOperandResolutionSource.match(
  /void\s+psx_resolve_subscript_qual_types_in\s*\([^]*?\n\}/,
);
if (!/\bpsx_subscript_qual_types_resolution_t\b/.test(
      expressionOperandResolutionHeader,
    ) ||
    !subscriptQualTypeCore ||
    /\bnode_t\b|\bps_node_|\bND_[A-Z0-9_]+\b|\bPSX_HIR_/.test(
      subscriptQualTypeCore[0],
    ) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      subscriptQualTypeCore[0],
    ) ||
    !/\bpsx_type_shape_t\b/.test(subscriptQualTypeCore[0]) ||
    !/\bdescribe_type\s*\(/.test(subscriptQualTypeCore[0]) ||
    !/\bpsx_resolve_subscript_qual_types_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bPSX_HIR_SUBSCRIPT\b/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/resolution\.swapped\s*\?\s*right\s*:\s*left/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/direct_vla_runtime_view_t/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/direct_vla_runtime_view\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/apply_direct_vla_runtime_view\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/view\.stride_frame_offset\s*\+=\s*PSX_VLA_RUNTIME_SLOT_SIZE/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_type_table_contains_vla_array\s*\(\s*direct_semantic_types\s*\(context\)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /direct_subscript_binding_t|resolve_direct_vla_subscript_binding/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "subscript typing must use one AST-independent QualType rule, normalize child order, and structurally propagate VLA runtime views into direct Typed HIR",
  );
}

if (!/\bpsx_resolve_member_hir_node_spec_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/resolution\.member\.member_qual_type/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /member_access_resolution\.h/.test(
      `${hirMemberResolutionHeader}\n${hirMemberResolutionSource}`,
    ) ||
    !/\bpsx_resolve_member_access_qual_type_in\s*\(/.test(
      hirMemberResolutionSource,
    )) {
  throw new Error(
    "direct member Typed HIR must share AST-independent meaning resolution and add target layout only at HIR materialization",
  );
}

if (!/#include\s+"static_assert_resolution\.h"/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/case\s+ND_STATIC_ASSERT\s*:\s*\{[^]*?psx_resolve_static_assert\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/case\s+ND_STATIC_ASSERT\s*:\s*\{[^]*?\.kind\s*=\s*PSX_HIR_NOP/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "block static assertions must resolve through the shared constant rule and become direct HIR NOP statements without mutating Syntax",
  );
}

if (!/control->init->kind\s*==\s*ND_LOCAL_DECLARATION/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/declaration_scope[^]*?ps_decl_enter_scope_in\s*\([^]*?preflight_direct_statement\s*\(\s*context,\s*control->init\s*\)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/ps_decl_leave_scope_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /ps_ctx_(?:enter|leave)_block_scope_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/children\[count\]\s*=\s*build_direct_statement\s*\(\s*context,\s*control->init\s*\)/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "for declaration initializers must resolve in a dedicated lexical scope and remain declaration blocks on the direct HIR init edge",
  );
}

const typeNameQualTypeValueAdapter = typeNameResolutionSource.match(
  /int\s+psx_resolve_type_name_qual_type_in_contexts\s*\([^]*?\n\}/,
);
const typeNameBaseQualTypeCore = typeNameResolutionSource.match(
  /int\s+psx_resolve_type_name_base_in_contexts\s*\([^]*?\n\}/,
);
if (!/\bpsx_resolve_type_name_qual_type_in_contexts\s*\(/.test(
      typeNameResolutionHeader,
    ) ||
    !/\bpsx_resolve_type_name_base_in_contexts\s*\(/.test(
      typeNameResolutionHeader,
    ) ||
    !typeNameQualTypeValueAdapter ||
    !/psx_type_name_base_resolution_t\s+base\s*=\s*\{0\}/.test(
      typeNameQualTypeValueAdapter[0],
    ) ||
    !/psx_resolve_type_name_base_in_contexts\s*\(/.test(
      typeNameQualTypeValueAdapter[0],
    ) ||
    !typeNameBaseQualTypeCore ||
    /#include[^\n]*parser\/(?:global_registry|local_registry|type_builder)\.h/.test(
      typeNameResolutionSource,
    ) ||
    !/psx_apply_parsed_declarator_qual_type_in_contexts\s*\(/.test(
      typeNameQualTypeValueAdapter[0],
    ) ||
    /psx_resolve_bound_type_name_ref_in_contexts\s*\(|psx_build_decl_type\s*\(|ps_type_clone_in\s*\(/.test(
      typeNameQualTypeValueAdapter[0],
    ) ||
    /\bpsx_(?:bind_type_name_ref|resolve_bound_type_name|type_name_bind_resolved|type_name_bound_|type_name_resolved_)|\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      `${typeNameResolutionHeader}\n${typeNameResolutionSource}`,
    ) ||
    !/psx_resolve_type_name_qual_type_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bPSX_HIR_CAST\b/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_node_type_name_state/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "type names and generic selection must resolve exclusively through canonical QualType identities",
  );
}

const obsoleteOperandTypeViewCores = [
  "resolve_arithmetic_unary_result_type_value",
  "resolve_binary_result_type_value",
  "resolve_conditional_result_type_value",
];
for (const core of obsoleteOperandTypeViewCores) {
  if (new RegExp(`\\b${core}\\s*\\(`).test(expressionOperandResolutionSource)) {
    throw new Error(
      `${core} must not rebuild a parser type view inside the QualType core`,
    );
  }
}
const sharedOperandQualTypeRuleCores = new Map([
  ["promoted_integer_result", 4],
  ["usual_arithmetic_result", 3],
  ["decay_pointer_like", 6],
]);
for (const [core, expectedCalls] of sharedOperandQualTypeRuleCores) {
  const calls = expressionOperandResolutionSource.match(
    new RegExp(`\\b${core}\\s*\\(`, "g"),
  ) ?? [];
  if (calls.length !== expectedCalls ||
      !new RegExp(`static\\s+psx_qual_type_t\\s+${core}\\s*\\(`).test(
        expressionOperandResolutionSource,
      )) {
    throw new Error(
      `${core} must remain private to the AST-independent QualType core`,
    );
  }
}
const indirectionQualTypeAdapter = expressionOperandResolutionSource.match(
  /psx_qual_type_t\s+psx_resolve_indirection_result_qual_type_in\s*\([^]*?\n\}/,
);
const addressQualTypeAdapter = expressionOperandResolutionSource.match(
  /psx_qual_type_t\s+psx_resolve_address_result_qual_type_in\s*\([^]*?\n\}/,
);
if (!indirectionQualTypeAdapter ||
    !/psx_semantic_type_table_base\s*\(/.test(
      indirectionQualTypeAdapter[0],
    ) ||
    /resolve_indirection_result_type_value\s*\(/.test(
      indirectionQualTypeAdapter[0],
    ) ||
    !addressQualTypeAdapter ||
    !/ps_ctx_intern_pointer_to_qual_type_in\s*\(/.test(
      addressQualTypeAdapter[0],
    ) ||
    /resolve_address_result_type_value\s*\(/.test(
      addressQualTypeAdapter[0],
    )) {
  throw new Error(
    "indirection and address QualType rules must preserve recursive qualifiers through TypeId relations instead of canonical type-value clones",
  );
}
const binaryTargetRuleCalls = expressionOperandResolutionSource.match(
  /\bps_type_binary_result_for_data_layout_in\s*\(/g,
) ?? [];
const conditionalTargetRuleCalls = expressionOperandResolutionSource.match(
  /\bps_type_conditional_result_for_data_layout_in\s*\(/g,
) ?? [];
if (binaryTargetRuleCalls.length !== 0 ||
    conditionalTargetRuleCalls.length !== 0) {
  throw new Error(
    "operand result rules must stay in the QualType core without delegating to parser type views",
  );
}

if (/parser\/ast\.h|\bnode_t\b|\bnode_kind_t\b|\bpsx_type_t\b/.test(
      hirHeader,
    )) {
  throw new Error(
    "public Typed HIR must not expose parser AST or mutable type objects",
  );
}
const hirCommonNodeSpec = hirInternalHeader.match(
  /typedef\s+struct\s*\{([^]*?)\}\s*psx_hir_node_spec_t\s*;/,
)?.[1] ?? "";
const storedHirNode = hirImplementation.match(
  /struct\s+psx_hir_node_t\s*\{([^]*?)\};/,
)?.[1] ?? "";
const resolvedHirNode = resolvedHirNodeInternalHeader.match(
  /struct\s+psx_semantic_node_t\s*\{([^]*?)\};/,
)?.[1] ?? "";
if (!/psx_qual_type_t\s+psx_hir_node_qual_type/.test(hirHeader) ||
    !/typedef\s+struct\s*\{\s*psx_hir_node_spec_t\s+node\s*;\s*psx_qual_type_t\s+qual_type\s*;\s*\}\s*psx_hir_expression_spec_t\s*;/.test(
      hirInternalHeader,
    ) ||
    !/typedef\s+struct\s*\{\s*psx_hir_node_spec_t\s+node\s*;\s*\}\s*psx_hir_statement_spec_t\s*;/.test(
      hirInternalHeader,
    ) ||
    /psx_hir_node_role_t\s+role\s*;|psx_qual_type_t\s+qual_type\s*;/.test(
      hirCommonNodeSpec,
    ) ||
    /psx_hir_node_role_t\s+role\s*;|psx_qual_type_t\s+qual_type\s*;/.test(
      storedHirNode,
    ) ||
    !/typedef\s+struct\s*\{\s*psx_hir_node_t\s+node\s*;\s*psx_qual_type_t\s+qual_type\s*;\s*\}\s*psx_hir_expression_node_t\s*;/.test(
      hirImplementation,
    ) ||
    /psx_hir_node_role_t\s+role\s*;|psx_qual_type_t\s+expression_type\s*;/.test(
      resolvedHirNode,
    ) ||
    !/typedef\s+struct\s*\{\s*psx_semantic_node_t\s+node\s*;\s*psx_qual_type_t\s+qual_type\s*;\s*\}\s*psx_semantic_expression_t\s*;/.test(
      resolvedHirNodeInternalHeader,
    ) ||
    !/typedef\s+struct\s*\{\s*psx_semantic_node_t\s+node\s*;\s*\}\s*psx_semantic_statement_t\s*;/.test(
      resolvedHirNodeInternalHeader,
    ) ||
    /\bpsx_hir_module_add_node\b/.test(hirInternalHeader) ||
    !/\bpsx_hir_module_add_expression\b/.test(hirInternalHeader) ||
    !/\bpsx_hir_module_add_statement\b/.test(hirInternalHeader) ||
    !/spec->qual_type\.type_id\s*==\s*PSX_TYPE_ID_INVALID/.test(
      hirImplementation,
    ) ||
    !/psx_semantic_node_builder_expression\s*\(/.test(
      semanticNodeBuilderHeader,
    ) ||
    !/psx_semantic_node_builder_has_canonical_type\s*\([^]*?qual_type/.test(
      semanticNodeBuilderSource,
    ) ||
    !/\bpsx_semantic_type_table_qual_type_is_valid\s*\(/.test(
      semanticNodeBuilderSource,
    ) ||
    /\bps_ctx_type_by_id_in\s*\(/.test(semanticNodeBuilderSource) ||
    !/PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE/.test(
      semanticNodeBuilderSource,
    ) ||
    !/expression->qual_type\s*=\s*qual_type/.test(
      semanticNodeBuilderSource,
    ) ||
    !/psx_semantic_node_builder_expression\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_node_builder_statement\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_node_builder_leaf_expression\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/static\s+psx_semantic_node_t\s*\*build_direct_literal\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/static\s+psx_semantic_node_t\s*\*build_direct_identifier\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_semantic_expression_t\s*\*|->qual_type\s*=/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "Typed HIR construction must structurally require canonical QualType for expressions",
  );
}
if (!/PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS/.test(
      semanticNodeBuilderSource,
    )) {
  throw new Error(
    "resolved-tree HIR emission must reject unresolved syntax node kinds",
  );
}
if (!/\.kind\s*=\s*PSX_HIR_DEREF/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/unary_deref_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "typed unary dereference must materialize directly into Typed HIR without parser-shaped lowering",
  );
}
if (!/\.kind\s*=\s*PSX_HIR_SUBSCRIPT/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bPSX_HIR_SUBSCRIPT\b/.test(hirHeader) ||
    !/kind\s*==\s*PSX_HIR_SUBSCRIPT/.test(hirIrBuilder) ||
    !/\bsubscript_address\s*\(/.test(hirIrBuilder) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/subscript_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "typed subscript must materialize directly into Typed HIR without parser-shaped lowering",
  );
}
if (!/try_build_pointer_arithmetic\s*\(/.test(hirIrBuilder) ||
    !/pointer_stride_value\s*\(/.test(hirIrBuilder) ||
    !/psx_hir_node_vla_stride_frame_offset\s*\(/.test(hirIrBuilder) ||
    !/psx_type_layout_sizeof\s*\(/.test(hirIrBuilder) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/expr_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "pointer arithmetic must preserve source operators through Typed HIR and apply target stride in HIR lowering",
  );
}
if (!/psx_resolve_member_hir_node_spec_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bPSX_HIR_MEMBER_ACCESS\b/.test(hirHeader) ||
    !/\bmember_address\s*\(/.test(hirIrBuilder) ||
    !/kind\s*==\s*PSX_HIR_MEMBER_ACCESS/.test(hirIrBuilder) ||
    !/psx_hir_node_member_offset\s*\(/.test(hirIrBuilder) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/member_access_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "typed member access must materialize directly into Typed HIR without parser-shaped lowering",
  );
}
if (!/\bND_COMPOUND_ASSIGN\b/.test(syntaxNodeKindHeader) ||
    !/\bPSX_HIR_COMPOUND_ASSIGN\b/.test(hirHeader) ||
    /\bis_source_compound_assignment\b/.test(astSource) ||
    !/psx_node_new_raw_binary_in\s*\([^]*?ND_COMPOUND_ASSIGN,[^]*?node,\s*assign_ctx\s*\(\s*ctx\s*\)/.test(
      parserExpressionSource,
    ) ||
    !/syntax->kind\s*==\s*ND_COMPOUND_ASSIGN[^]*?PSX_HIR_COMPOUND_ASSIGN/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bbuild_compound_assignment\s*\(/.test(hirIrBuilder) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/assignment_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "compound assignment must preserve one lvalue evaluation directly through Typed HIR",
  );
}
if (!/\bND_GT\b/.test(syntaxNodeKindHeader) ||
    !/\bND_GE\b/.test(syntaxNodeKindHeader) ||
    !/\bPSX_HIR_GT\b/.test(hirHeader) ||
    !/\bPSX_HIR_GE\b/.test(hirHeader) ||
    !/new_binary_with_source_op\s*\(\s*ctx,\s*ND_GT,\s*node,\s*rhs,\s*TK_GT\s*\)/.test(
      parserExpressionSource,
    ) ||
    !/new_binary_with_source_op\s*\(\s*ctx,\s*ND_GE,\s*node,\s*rhs,\s*TK_GE\s*\)/.test(
      parserExpressionSource,
    ) ||
    /new_binary_with_source_op\s*\(\s*ctx,\s*ND_(?:LT|LE),\s*rhs,\s*node,\s*TK_(?:GT|GE)\s*\)/.test(
      parserExpressionSource,
    ) ||
    !/MAP\s*\(\s*ND_GT,\s*PSX_HIR_GT,\s*PSX_TYPE_BINARY_RELATIONAL\s*\)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/MAP\s*\(\s*ND_GE,\s*PSX_HIR_GE,\s*PSX_TYPE_BINARY_RELATIONAL\s*\)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/kind\s*==\s*PSX_HIR_GT\s*\|\|\s*kind\s*==\s*PSX_HIR_GE/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "greater-than syntax and Typed HIR must preserve source operators and operand order until IR lowering",
  );
}
const unaryPlusParser = parserExpressionSource.match(
  /if\s*\(\s*k\s*==\s*TK_PLUS\s*\)\s*\{[^]*?\n\s*\}/,
)?.[0] ?? "";
if (!/\bND_UNARY_PLUS\b/.test(syntaxNodeKindHeader) ||
    !/\bPSX_HIR_UNARY_PLUS\b/.test(hirHeader) ||
    !/unary_plus->kind\s*=\s*ND_UNARY_PLUS/.test(unaryPlusParser) ||
    /return\s+cast_ctx\s*\(/.test(unaryPlusParser) ||
    !/syntax->kind\s*==\s*ND_UNARY_PLUS[^]*?PSX_HIR_UNARY_PLUS/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_arithmetic_unary_result_qual_type_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_HIR_UNARY_PLUS[^]*?build_unary_plus\s*\(/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "unary plus must preserve arithmetic promotion through Typed HIR",
  );
}
if (/node->kind\s*==\s*ND_COMMA[^]*?node->rhs\s*=\s*apply_postfix\s*\(/.test(
      parserExpressionSource,
    )) {
  throw new Error(
    "postfix parsing must wrap the complete source expression instead of rewriting a comma-expression RHS",
  );
}
if (!/syntax->kind\s*==\s*ND_UNARY_NEGATE[^]*?PSX_HIR_NEGATE/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bPSX_HIR_NEGATE\b/.test(hirHeader) ||
    !/kind\s*==\s*PSX_HIR_NEGATE[^]*?build_complex_negate\s*\(/.test(
      hirIrBuilder,
    ) ||
    !/psx_hir_node_kind\s*\(\s*node\s*\)\s*==\s*PSX_HIR_NEGATE[^]*?build_scalar_negate\s*\(/.test(
      hirIrBuilder,
    ) ||
    allSourceFiles.some(
      (path) =>
        /src\/lowering\/unary_operator_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "typed unary negate must remain distinct from syntax lowering and materialize as PSX_HIR_NEGATE",
  );
}
const logicalNotParser = parserExpressionSource.match(
  /if\s*\(\s*k\s*==\s*TK_BANG\s*\)\s*\{[^]*?\n\s*\}/,
)?.[0] ?? "";
if (!/\bND_LOGICAL_NOT\b/.test(syntaxNodeKindHeader) ||
    !/\bPSX_HIR_LOGICAL_NOT\b/.test(hirHeader) ||
    !/logical_not->kind\s*=\s*ND_LOGICAL_NOT/.test(logicalNotParser) ||
    /\bND_EQ\b|from_logical_not/.test(logicalNotParser) ||
    /\bfrom_logical_not\b/.test(astHeader) ||
    !/syntax->kind\s*==\s*ND_LOGICAL_NOT[^]*?PSX_HIR_LOGICAL_NOT/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_logical_not_result_qual_type_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_HIR_LOGICAL_NOT[^]*?build_logical_not\s*\(/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "logical not must preserve its source operator through Typed HIR",
  );
}
const bitwiseNotParser = parserExpressionSource.match(
  /if\s*\(\s*k\s*==\s*TK_TILDE\s*\)\s*\{[^]*?\n\s*\}/,
)?.[0] ?? "";
if (!/\bND_BITWISE_NOT\b/.test(syntaxNodeKindHeader) ||
    !/\bPSX_HIR_BITWISE_NOT\b/.test(hirHeader) ||
    !/bitwise_not->kind\s*=\s*ND_BITWISE_NOT/.test(bitwiseNotParser) ||
    /\bND_SUB\b|psx_node_new_syntax_int_in/.test(bitwiseNotParser) ||
    !/syntax->kind\s*==\s*ND_BITWISE_NOT[^]*?PSX_HIR_BITWISE_NOT/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_bitwise_not_result_qual_type_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_HIR_BITWISE_NOT[^]*?build_bitwise_not\s*\(/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "bitwise not must preserve its source operator through Typed HIR",
  );
}
const directComplexComponentKindChecks =
  syntaxTypedHirResolutionSource.match(
    /syntax->kind\s*==\s*ND_CREAL\s*\|\|\s*\n\s*syntax->kind\s*==\s*ND_CIMAG/g,
  ) ?? [];
if (!/syntax->kind\s*==\s*ND_CREAL[^]*?PSX_HIR_CREAL/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/syntax->kind\s*==\s*ND_CIMAG[^]*?PSX_HIR_CIMAG/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/build_complex_component\s*\([^]*?!hir_ir_is_float_value_type\s*\(\s*result_type\s*\)[^]*?ir_val_imm\s*\(\s*result_type\.type\s*,\s*0\s*\)/.test(
      hirIrBuilder,
    ) ||
    directComplexComponentKindChecks.length !== 2 ||
    !/direct_arithmetic_unary_operator\s*\(\s*syntax->kind,\s*&type_operator\)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/context->semantic_context,\s*type_operator,\s*\n\s*operand_type/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/syntax->kind\s*==\s*ND_CREAL[^]*?PSX_HIR_CREAL[^]*?syntax->kind\s*==\s*ND_CIMAG[^]*?PSX_HIR_CIMAG/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/complex_part_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "real/imag operators must resolve from canonical QualType directly into Typed HIR without parser-shaped lowering",
  );
}
const genericQualTypeResolution = genericSelectionResolutionSource.match(
  /void\s+psx_resolve_generic_selection_qual_types_in\s*\([^]*?\n\}/,
)?.[0] ?? "";
if (!genericQualTypeResolution ||
    /\bnode_t\b|\bps_node_|node_generic_selection_t/.test(
      genericQualTypeResolution,
    ) ||
    /\bpsx_(?:generic_selection_resolution_state_t|resolution_store_t)\b|\bps_node_resolution_state/.test(
      genericSelectionResolutionSource,
    ) ||
    !/psx_resolve_generic_selection_qual_types_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/direct_generic_binding_t/.test(syntaxTypedHirResolutionSource) ||
    !/preflight_direct_unevaluated_expression\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_generic_selection_(?:selected|type_name_state)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/binding->selected_index[^]*?selection->associations\[binding->selected_index\]\.expression/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /typedef\s+struct\s*\{[^}]*\bselected_index\b[^}]*\}\s*node_generic_selection_t\s*;/.test(
      astHeader,
    ) ||
    allSourceFiles.some(
      (path) =>
        /src\/lowering\/generic_selection_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "generic selection resolution must stay outside Syntax AST and materialize its selected expression directly into Typed HIR",
  );
}
if (!/syntax->kind\s*==\s*ND_ALIGNOF_QUERY[^]*?resolve_direct_alignof_type_name/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/static\s+int\s+resolve_direct_alignof_type_name\s*\([^]*?psx_resolve_alignof_qual_type_plan_in/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\.kind\s*=\s*PSX_HIR_NUMBER/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/alignof_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "resolved alignof queries must materialize directly as Typed HIR numbers",
  );
}
if (!/PSX_TYPE_QUERY_PLAN_RUNTIME_PRODUCT/.test(
      typeQuerySemanticsHeader,
    ) ||
    !/PSX_TYPE_QUERY_PLAN_RUNTIME_SLOT/.test(
      typeQuerySemanticsHeader,
    ) ||
    !/psx_semantic_expr_id_t\s*\*runtime_factor_ids/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/ps_ctx_register_semantic_expression_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/ps_ctx_semantic_expression_in\s*\([^]*?binding->runtime_factor_ids\[i\]/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\.kind\s*=\s*PSX_HIR_MUL/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\.kind\s*=\s*PSX_HIR_LOCAL[^]*?plan->runtime_size_slot/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /runtime_size_expr|resolved_size|runtime_size_slot|evaluates_vla_operand/.test(
      sizeofQueryNode?.[1] ?? "",
    ) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/sizeof_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "sizeof resolution must stay outside Syntax AST and materialize its runtime plan directly into Typed HIR",
  );
}
if (!/direct_type_query_binding_t/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_sizeof_qual_type_plan_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_sizeof_runtime_product_plan_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_sizeof_runtime_slot_plan_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_alignof_qual_type_plan_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/static\s+psx_semantic_node_t\s*\*build_direct_type_query\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_(?:sizeof|alignof)_query_(?:resolution_state|resolved_|runtime_plan)/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "direct sizeof and alignof resolution must use Syntax-external QualType plans instead of node resolution state",
  );
}
if (!/direct_compound_literal_binding_t/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_compound_literal_qual_type_plan_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/static\s+psx_semantic_node_t\s*\*build_direct_compound_literal\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_HIR_STMT_EXPR/.test(syntaxTypedHirResolutionSource) ||
    !/lower_complete_internal_local_object\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_apply_global_declaration_pipeline\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/build_direct_global_reference\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/operand_syntax->kind\s*==\s*ND_COMPOUND_LITERAL/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/build_direct_addressable_compound_literal\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/__compound_lit_%d/.test(syntaxTypedHirResolutionSource) ||
    /psx_compound_literal_(?:resolution|direct_initializer)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_node_type_name_state|ps_node_resolution_state/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/lower_complete_internal_local_object\s*\(/.test(
      localObjectLoweringSource,
    ) ||
    !/ps_local_registry_create_internal_storage_object_qual_type_in\s*\(/.test(
      localObjectLoweringSource,
    ) ||
    !/ps_local_registry_create_internal_storage_object_qual_type_in\s*\(/.test(
      castLoweringSource,
    )) {
  throw new Error(
    "direct compound literals must use Syntax-external QualType plans, internal automatic storage, and registered static global storage without perturbing source name binding",
  );
}
if (/requires_addressable_object/.test(parserExpressionSource) ||
    !/psx_node_new_unary_addr_syntax_for_in\s*\(/.test(
      parserExpressionSource,
    ) ||
    !/operand_syntax->kind\s*==\s*ND_COMPOUND_LITERAL[^]*?build_direct_addressable_compound_literal\s*\(/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "compound literal address syntax must remain explicit and lower directly without compatibility resolution state",
  );
}
const compoundLiteralParser = parserExpressionSource.match(
  /static\s+node_t\s*\*parse_compound_literal_from_type\s*\([^]*?\n\}/,
);
if (!compoundLiteralParser ||
    /current_function_name\s*\(/.test(compoundLiteralParser[0]) ||
    /has_file_scope_storage/.test(parserExpressionSource) ||
    !/compound->type_name\.scope_seq/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/context->function_name\s*!=\s*NULL/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_compound_literal_storage_duration_in_scope_graph\s*\(/.test(
      compoundLiteralSemanticsSource,
    ) ||
    !/psx_scope_graph_nearest_scope_of_kind\s*\(/.test(
      compoundLiteralSemanticsSource,
    ) ||
    !/compound->type_name\.scope_seq/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "compound literal storage duration must derive from its lexical scope during semantic resolution, not from a parser-owned flag",
  );
}
if (!/session->hir_module\s*=\s*psx_hir_module_create\(\)/.test(
      compilationSession,
    )) {
  throw new Error("CompilationSession must own the Typed HIR module");
}
const parsedFunctionResolutionBoundary = semanticPipelineSource.match(
  /int\s+psx_frontend_resolve_parsed_function_to_hir_in_session\s*\([^)]*\)\s*\{([^]*?)\n\}/,
);
const expressionResolutionBoundary = semanticPipelineSource.match(
  /int\s+psx_frontend_resolve_expression_to_hir_in_contexts\s*\([^)]*\)\s*\{([^]*?)\n\}/,
);
const initializerResolutionBoundary = semanticPipelineSource.match(
  /int\s+psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts\s*\([^)]*\)\s*\{([^]*?)\n\}/,
);
const nonfunctionTypedResolutionStart =
  semanticTreeResolutionSource.indexOf(
    "resolve_nonfunction_typed_hir_from_syntax_in_contexts",
  );
const directSyntaxTypedHirDispatch =
  semanticTreeResolutionSource.indexOf(
    "psx_resolve_syntax_expression_direct_to_typed_hir_with_lowering_in_contexts",
    nonfunctionTypedResolutionStart,
  );
const directInitializerTypedHirDispatch =
  semanticTreeResolutionSource.indexOf(
    "psx_resolve_syntax_initializer_for_object_direct_to_typed_hir_in_contexts",
    nonfunctionTypedResolutionStart,
  );
const directDiagnosticDispatch =
  semanticTreeResolutionSource.indexOf(
    "diagnose_direct_syntax_rejection",
    nonfunctionTypedResolutionStart,
  );
if (nonfunctionTypedResolutionStart < 0 ||
    directSyntaxTypedHirDispatch < nonfunctionTypedResolutionStart ||
    directDiagnosticDispatch < 0 ||
    directSyntaxTypedHirDispatch > directDiagnosticDispatch) {
  throw new Error(
    "expression resolution must produce Typed HIR or a structured direct diagnostic",
  );
}
const incdecQualTypeResolution = expressionOperandResolutionSource.match(
  /void\s+psx_resolve_incdec_operand_qual_type_in\s*\([^]*?\n\}/,
)?.[0] ?? "";
if (!incdecQualTypeResolution ||
    /\bnode_t\b|\bps_node_/.test(incdecQualTypeResolution) ||
    !/PSX_TYPE_QUALIFIER_CONST/.test(incdecQualTypeResolution) ||
    !/PSX_INCDEC_OPERAND_CONST/.test(incdecQualTypeResolution) ||
    !/PSX_INCDEC_OPERAND_INVALID_TYPE/.test(incdecQualTypeResolution) ||
    !/psx_resolve_incdec_operand_qual_type_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/MAP\s*\(\s*ND_PRE_INC\s*,\s*PSX_HIR_PRE_INC\s*\)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/MAP\s*\(\s*ND_PRE_DEC\s*,\s*PSX_HIR_PRE_DEC\s*\)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/MAP\s*\(\s*ND_POST_INC\s*,\s*PSX_HIR_POST_INC\s*\)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/MAP\s*\(\s*ND_POST_DEC\s*,\s*PSX_HIR_POST_DEC\s*\)/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "increment and decrement must resolve from canonical QualType directly into Typed HIR",
  );
}
if (!/MAP\s*\(\s*ND_COMMA\s*,\s*PSX_HIR_COMMA\s*,\s*PSX_TYPE_BINARY_COMMA\s*\)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_hir_node_kind\(node\)\s*==\s*PSX_HIR_COMMA\s*&&\s*is_void/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "comma expressions must resolve directly into Typed HIR, including void-valued evaluation",
  );
}
if (directInitializerTypedHirDispatch < nonfunctionTypedResolutionStart ||
    directInitializerTypedHirDispatch > directDiagnosticDispatch ||
    !/psx_resolve_syntax_initializer_for_object_direct_to_typed_hir_in_contexts\s*\([^]*?psx_qual_type_t\s+object_qual_type/.test(
      syntaxTypedHirResolutionHeader,
    ) ||
    !/PSX_HIR_INITIALIZER_LIST/.test(syntaxTypedHirResolutionSource) ||
    !/PSX_HIR_INITIALIZER_ENTRY/.test(syntaxTypedHirResolutionSource) ||
    !/\.is_resolved_initializer_entry\s*=\s*1/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\.attached_qual_type\s*=\s*item->target_qual_type/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /PSX_HIR_(?:MEMBER|INDEX)_DESIGNATOR/.test(
      `${syntaxTypedHirResolutionSource}\n${hirHeader}`,
    ) ||
    /PSX_HIR_EDGE_DESIGNATOR/.test(
      `${syntaxTypedHirResolutionSource}\n${hirHeader}`,
    )) {
  throw new Error(
    "initializer resolution must emit target-typed resolved entries without carrying raw designators into HIR",
  );
}
if (!/static\s+int\s+flat_initializer_entry_ranges\s*\(/.test(
      initializerResolutionSource,
    ) ||
    !/static\s+int\s+flat_initializer_apply_designated_ranges\s*\(/.test(
      initializerResolutionSource,
    ) ||
    !/designator_index\s*\+\s*1/.test(initializerResolutionSource) ||
    /aggregate_entry_range|range_override|PSX_HIR_EDGE_DESIGNATOR/.test(
      staticHirInitializerSource,
    ) ||
    !/item->evaluation_group\s*=\s*group/.test(
      initializerResolutionSource,
    ) ||
    !/flat_initializer_activate_union_member\s*\(/.test(
      initializerResolutionSource,
    ) ||
    !/\.is_active\s*=\s*0/.test(initializerResolutionSource) ||
    !/\.is_active\s*=\s*1/.test(initializerResolutionSource) ||
    /flat_initializer_type_contains_union/.test(
      initializerResolutionSource,
    ) ||
    !/if\s*\(\s*!item->is_active\s*\)\s*continue/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/evaluation_group_count/.test(initializerResolutionSource) ||
    !/__initializer_value_%d/.test(syntaxTypedHirResolutionSource) ||
    !/lower_complete_internal_local_object\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/plan->evaluation_group_count\s*\+[^;]*active_item_count/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/for\s*\(int i\s*=\s*0;\s*i\s*<\s*plan->evaluation_group_count;\s*i\+\+\)[^]*?build_direct_expression\s*\(\s*context,\s*value_syntax\s*\)/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "automatic aggregate plans must preserve union member activity and lower range values through one explicit internal temporary evaluation group",
  );
}
if (!/psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionHeader,
    ) ||
    !/preflight_direct_statement\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/build_direct_statement\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_node_builder_statement\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/direct_case_value\s*\(/.test(syntaxTypedHirResolutionSource) ||
    !/psx_normalize_integer_constant_cast\s*\(/.test(
      integerConstantEvaluationSource,
    ) ||
    !/psx_apply_integer_constant_binary\s*\(/.test(
      integerConstantEvaluationSource,
    ) ||
    !/psx_apply_integer_constant_binary\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_case_label_(?:bind_value|is_resolved|value)\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_resolution_work_tree_|typed_hir_tree_materialization/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "direct statement resolution must build Typed HIR without a mutable compatibility tree",
  );
}
const directFunctionDispatchStart = semanticTreeResolutionSource.indexOf(
  "resolve_parsed_function_typed_hir_from_syntax_in_contexts",
);
const directFunctionDispatch = semanticTreeResolutionSource.indexOf(
  "psx_resolve_syntax_function_direct_to_typed_hir_in_contexts",
  directFunctionDispatchStart,
);
const directFunctionDiagnosticDispatch = semanticTreeResolutionSource.indexOf(
  "diagnose_direct_syntax_rejection",
  directFunctionDispatchStart,
);
if (!/psx_resolve_syntax_function_direct_to_typed_hir_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionHeader,
    ) ||
    !/psx_resolve_function_definition_header_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_HIR_EDGE_PARAMETER/.test(syntaxTypedHirResolutionSource) ||
    !/PSX_HIR_EDGE_FUNCTION_BODY/.test(syntaxTypedHirResolutionSource) ||
    !/\.kind\s*=\s*PSX_HIR_FUNCTION/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/preflight_direct_local_declaration\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_begin_automatic_local_declaration_hir_pipeline\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_begin_automatic_local_declaration_pipeline\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_apply_resolved_runtime_parsed_declarator_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/int\s+psx_apply_resolved_runtime_parsed_declarator_in_contexts\s*\(/.test(
      declarationApplicationSource,
    ) ||
    /psx_apply_runtime_parsed_declarator_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/build_direct_local_declaration\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/preflight_direct_flat_initializer\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_flat_local_initializer_plan\s*\(/.test(
      initializerResolutionSource,
    ) ||
    !/psx_resolve_incomplete_array_initializer_shape_in\s*\(/.test(
      declarationResolutionSource,
    ) ||
    !/resolve_direct_completed_array_qual_type\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_flat_local_initializer_plan\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/build_direct_flat_initializer\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_typedef_declaration\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/is_semantic_only/.test(syntaxTypedHirResolutionSource) ||
    !/psx_resolve_decl_specifier_value_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/void\s+psx_resolve_decl_specifier_value_in_contexts\s*\(/.test(
      declarationSpecifierResolutionSource,
    ) ||
    !/psx_resolve_declarator_bound_in_contexts\s*\(/.test(
      declarationSpecifierResolutionSource,
    ) ||
    /psx_eval_parsed_enum_const_expr_in_context\s*\(/.test(
      declarationSpecifierResolutionSource,
    ) ||
    !/psx_resolve_parsed_decl_alignment_in_contexts\s*\(/.test(
      declarationSpecifierResolutionSource,
    ) ||
    /psx_(?:eval_parsed_alignas_value|parse_alignas_value)_in_context/.test(
      `${declarationApplicationSource}\n${declarationSpecifierResolutionSource}`,
    ) ||
    !/psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts\s*\(/.test(
      declarationApplicationSource,
    ) ||
    !/psx_resolve_type_name_qual_type_in_contexts\s*\(/.test(
      declarationApplicationSource,
    ) ||
    !/psx_apply_resolved_runtime_parsed_declarator_in_contexts\s*\(/.test(
      declarationSpecifierResolutionSource,
    ) ||
    !/specifier_resolution\.requested_alignment/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /ps_prepare_constant_declarator_expressions_in_context\s*\(|ps_prepare_decl_specifier_alignments_in_context\s*\(/.test(
      declarationSpecifierResolutionSource,
    ) ||
    /ps_prepare_constant_declarator_expressions_in_context\s*\(|ps_prepare_decl_specifier_alignments_in_context\s*\(|psx_apply_parsed_decl_specifier_qual_type_in_contexts\s*\(|psx_apply_parsed_standalone_tag_in_contexts\s*\(|psx_apply_local_declaration_syntax_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_node_builder_vla_runtime\s*\(/.test(
      semanticNodeBuilderSource,
    ) ||
    !/psx_semantic_node_builder_vla_runtime\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/vla_runtime_plan/.test(explicitArenaDeclarationPipelineSource) ||
    /psx_finish_automatic_local_declaration_pipeline\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_bind_local_initializer_target_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/ps_ctx_rollback_function_registration_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    directFunctionDispatchStart < 0 || directFunctionDispatch < 0 ||
    directFunctionDiagnosticDispatch < 0 ||
    directFunctionDispatch > directFunctionDiagnosticDispatch ||
    /psx_legacy_syntax_diagnostics_accept_/.test(
      semanticTreeResolutionSource,
    )) {
  throw new Error(
    "function resolution must build direct Typed HIR from a canonical header without compatibility fallback",
  );
}
if (!/active_transaction/.test(globalRegistrySource) ||
    !/psx_global_registry_checkpoint_begin\s*\(/.test(
      globalRegistrySource,
    ) ||
    !/psx_global_registry_note_global_mutation\s*\(/.test(
      globalRegistrySource,
    ) ||
    !/psx_global_registry_checkpoint_rollback\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_global_registry_checkpoint_commit\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_global_registry_checkpoint_is_active\s*\(/.test(
      globalRegistrySource,
    ) ||
    !/!psx_global_registry_checkpoint_is_active\s*\(\s*global_registry\s*\)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_local_registry_checkpoint_begin\s*\(/.test(
      localRegistrySource,
    ) ||
    !/psx_local_registry_checkpoint_is_active\s*\(/.test(
      localRegistrySource,
    ) ||
    !/local_transaction_active\s*&&\s*!global_transaction_active/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_local_registry_checkpoint_rollback\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_lowering_context_checkpoint\s*\(/.test(
      loweringRuntimeSource,
    ) ||
    !/psx_lowering_context_rollback\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_apply_block_extern_declaration_pipeline\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_begin_static_local_declaration_pipeline\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/function_declarations/.test(syntaxTypedHirResolutionSource) ||
    !/ps_ctx_rollback_function_registration_in\s*\([^]*?declaration->name/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_global_registry_note_global_mutation\s*\(/.test(
      globalObjectLoweringSource,
    ) ||
    !/psx_global_registry_note_global_mutation\s*\(/.test(
      staticDataInitializerSource,
    ) ||
    !/const\s+ag_compilation_options_t\s*\*options/.test(
      syntaxTypedHirResolutionHeader,
    ) ||
    !/lowering_context,\s*options,\s*syntax_function/.test(
      semanticTreeResolutionSource,
    ) ||
    !/psx_resolve_syntax_expression_direct_to_typed_hir_with_lowering_in_contexts\s*\(/.test(
      semanticTreeResolutionSource,
    ) ||
    !/psx_resolve_syntax_initializer_for_object_direct_to_typed_hir_in_contexts\s*\(/.test(
      semanticTreeResolutionSource,
    ) ||
    !/\.options\s*=\s*options/.test(syntaxTypedHirResolutionSource)) {
  throw new Error(
    "direct Syntax resolution must transactionally isolate global state, join nested transactions, and receive lowering options before compatibility fallback",
  );
}
if (!/int\s+psx_finish_static_local_declaration_typed_hir_pipeline\s*\(/.test(
      declarationPipelineSource,
    ) ||
    !/psx_materialize_static_aggregate_initializer_plan\s*\(/.test(
      declarationPipelineSource,
    ) ||
    !/psx_resolve_syntax_expression_direct_to_typed_hir_in_contexts\s*\([^]*?psx_finish_static_local_declaration_typed_hir_pipeline\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /psx_finish_static_local_declaration_pipeline\s*\(/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "direct static local scalar initializers must lower from Typed HIR without invoking the compatibility declaration pipeline",
  );
}
if (/\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    /type_compatibility_view\.h/.test(syntaxTypedHirResolutionSource) ||
    /node_num_t\s+[A-Za-z_][A-Za-z0-9_]*\s*=|\.base\.kind\s*=\s*ND_/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/resolve_direct_completed_array_qual_type\s*\([^]*?psx_type_layout_character_code_unit_width\s*\(/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "direct Typed HIR resolution must classify canonical types through TypeShape and must not reconstruct mutable Syntax AST compatibility nodes",
  );
}
const functionDefinitionResolutionHeader = await readFile(
  "src/semantic/function_definition_resolution.h",
  "utf8",
);
const functionDefinitionResolutionSource = await readFile(
  "src/semantic/function_definition_resolution.c",
  "utf8",
);
const functionDefinitionHeaderResolutionStruct =
  functionDefinitionResolutionHeader.match(
    /typedef struct\s*\{((?:(?!typedef struct)[\s\S])*?)\}\s*psx_function_definition_header_resolution_t\s*;/,
  );
const functionSymbolStruct = semanticContextOwnershipSource.match(
  /struct\s+psx_function_symbol_t\s*\{([\s\S]*?)\n\};/,
);
const functionDefinitionPipelineResultStruct = declarationPipelineHeader.match(
  /typedef struct\s*\{((?:(?!typedef struct)[\s\S])*?)\}\s*psx_function_definition_pipeline_result_t\s*;/,
);
const typedefSymbolStruct = semanticContextOwnershipSource.match(
  /struct\s+typedef_name_t\s*\{([\s\S]*?)\n\};/,
);
if (!/psx_function_definition_header_resolution_t\s*;/.test(
      functionDefinitionResolutionHeader,
    ) ||
    !/psx_resolve_function_definition_header_in_contexts\s*\(/.test(
      functionDefinitionResolutionHeader,
    ) ||
    !/psx_qual_type_t\s+signature_qual_type\s*;/.test(
      functionDefinitionResolutionHeader,
    ) ||
    !functionDefinitionHeaderResolutionStruct ||
    /\bpsx_type_t\b/.test(functionDefinitionHeaderResolutionStruct[1]) ||
    !functionDefinitionPipelineResultStruct ||
    !/psx_qual_type_t\s+function_qual_type\s*;/.test(
      functionDefinitionPipelineResultStruct[1],
    ) ||
    !/psx_qual_type_t\s*\*\s*parameter_qual_types\s*;/.test(
      functionDefinitionPipelineResultStruct[1],
    ) ||
    /\bpsx_type_t\b/.test(
      functionDefinitionPipelineResultStruct[1],
    ) ||
    !/lvar_t\s*\*\*\s*parameters\s*;/.test(
      functionDefinitionResolutionHeader,
    ) ||
    !/function_parameter_qual_types\s*,\s*result->nargs/.test(
      explicitArenaDeclarationPipelineSource,
    ) ||
    !/function_parameter_qual_types\[i\]\.qualifiers\s*=\s*PSX_TYPE_QUALIFIER_NONE\s*;/.test(
      explicitArenaDeclarationPipelineSource,
    ) ||
    /parameter_types\s*\[[^\]]+\]\s*=\s*ps_node_get_type\s*\(/.test(
      explicitArenaDeclarationPipelineSource,
    ) ||
    !/ps_ctx_get_function_qual_type_in\s*\(/.test(
      functionDefinitionResolutionSource,
    ) ||
    /ps_ctx_get_function_type_in\s*\(|ps_ctx_intern_qual_type_in\s*\(/.test(
      functionDefinitionResolutionSource,
    ) ||
    !/psx_semantic_type_table_describe\s*\(/.test(
      functionDefinitionResolutionSource,
    ) ||
    /node->signature_qual_type\s*=\s*resolution\.signature_qual_type\s*;/.test(
      functionDefinitionResolutionSource,
    ) ||
    /node->signature\b/.test(functionDefinitionResolutionSource) ||
    /resolution\.function_type\b/.test(
      functionDefinitionResolutionSource,
    ) ||
    !functionSymbolStruct ||
    !/psx_qual_type_t\s+function_qual_type\s*;/.test(
      functionSymbolStruct[1],
    ) ||
    /\bpsx_type_t\b/.test(functionSymbolStruct[1]) ||
    !/psx_qual_type_t\s+function_qual_type\s*;/.test(
      semanticContextSource,
    ) ||
    /f->function_type\b|checkpoint->function_type\b/.test(
      semanticContextOwnershipSource,
    ) ||
    /\b(?:ps_ctx_register_function_type_in|psx_ctx_track_function_type_in|ps_ctx_get_function_type_in|psx_ctx_get_function_ret_type_in)\s*\(/.test(
      `${functionPublicHeaderSource}\n${semanticContextHeaderSource}\n${semanticContextOwnershipSource}`,
    ) ||
    /#include\s+"(?:core|type)\.h"/.test(functionPublicHeaderSource) ||
    !/ps_ctx_format_function_signature_in\s*\([^]*?psx_format_canonical_type_signature\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_function_symbol_qual_type\s*\(/.test(
      identifierResolutionSource,
    ) ||
    /ps_function_symbol_type\s*\(|ps_ctx_intern_qual_type_in\s*\([^]*?resolution->symbol\.function/.test(
      identifierResolutionSource,
    ) ||
    !typedefSymbolStruct ||
    !/psx_qual_type_t\s+decl_qual_type\s*;/.test(
      typedefSymbolStruct[1],
    ) ||
    /\bpsx_type_t\b/.test(typedefSymbolStruct[1]) ||
    !typedefInfoStruct ||
    !/const\s+psx_semantic_type_table_t\s*\*\s*decl_type_table\s*;/.test(
      typedefInfoStruct[1],
    ) ||
    !/psx_qual_type_t\s+decl_qual_type\s*;/.test(
      typedefInfoStruct[1],
    ) ||
    /\bpsx_type_t\b/.test(typedefInfoStruct[1]) ||
    /\bps_ctx_typedef_decl_type\s*\(|\bps_ctx_find_typedef_decl_type(?:_at)?_in\s*\(/.test(
      `${semanticContextHeaderSource}\n${semanticContextOwnershipSource}`,
    ) ||
    /\bt->decl_type\b/.test(semanticContextOwnershipSource) ||
    /\btypedef_record_decl_type\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/resolve_typedef_decl_qual_type\s*\([^]*?info->decl_type_table\s*!=\s*context->semantic_types/.test(
      semanticContextOwnershipSource,
    ) ||
    /ps_ctx_intern_declaration_qual_type_in\s*\(/.test(
      typedefDeclarationResolutionSource,
    ) ||
    /request->type\b/.test(typedefDeclarationResolutionSource) ||
    !/psx_semantic_type_table_qual_type_is_valid\s*\([^]*?request->decl_qual_type/.test(
      typedefDeclarationResolutionSource,
    ) ||
    /psx_prepare_function_definition_resolution_in_contexts\s*\(/.test(
      functionDefinitionResolutionSource,
    ) ||
    !/psx_resolve_function_definition_header_in_contexts\s*\(/.test(
      functionDefinitionResolutionSource,
    )) {
  throw new Error(
    "function and typedef symbols must own canonical QualType values without projecting mutable compatibility nodes",
  );
}
const removedMutableCompatibilitySourcePattern =
  /\/(?:legacy_syntax_diagnostics|resolution_work_tree(?:_internal)?|typed_hir_tree_materialization|identifier_binding|local_declaration_tree_resolution|semantic_pass|semantic_lowering_pass|initializer_lowering|compound_literal_lowering|lowered_tree_validation|control_flow_validation|semantic_diagnostics|semantic_invariants|type_identity_pass|tree_walk|lvar_usage_analysis|parser_legacy|stmt_legacy|name_classifier_legacy|semantic_ctx_legacy|node_utils_legacy|local_declaration_legacy|parser_type_compatibility|type_compatibility_view|type(?:_builder|_fwd|_owned_internal)?|semantic_tree_resolution_test_support)\.[ch]$/;
const remainingMutableCompatibilitySources = allSourceFiles.filter(
  (path) => removedMutableCompatibilitySourcePattern.test(path),
);
if (remainingMutableCompatibilitySources.length > 0 ||
    /^(?:TEST_ONLY_SRCS|TEST_ONLY_OBJS)=/m.test(makefileSource) ||
    /\$\(TEST_ONLY_(?:SRCS|OBJS)\)/.test(makefileSource) ||
    /psx_resolution_work_tree_|\bpsx_resolution_work_tree_t\b|compatibility_root|legacy_syntax_diagnostics/.test(
      `${semanticTreeResolutionSource}\n${semanticPipelineSource}\n${semanticPipelineHeader}\n${semanticPipelineInternalHeader}`,
    ) ||
    !/psx_resolve_parsed_function_hir_from_syntax_in_contexts\s*\(/.test(
      semanticTreeResolutionHeader,
    ) ||
    !/psx_resolve_expression_hir_from_syntax_in_contexts\s*\(/.test(
      semanticTreeResolutionHeader,
    ) ||
    !/psx_resolve_initializer_hir_from_syntax_in_contexts\s*\(/.test(
      semanticTreeResolutionHeader,
    ) ||
    !/diagnose_direct_syntax_rejection\s*\(/.test(
      semanticTreeResolutionSource,
    ) ||
    /parser\/|\bnode_t\b|\bnode_kind_t\b/.test(resolvedTreeHeader) ||
    /parser\/|\bnode_t\b|\bnode_kind_t\b/.test(
      resolvedHirNodeInternalHeader,
    ) ||
    /parser\/|\bnode_t\b|\bnode_kind_t\b|\bpsx_semantic_context_t\b/.test(
      resolvedTreeHir,
    ) ||
    /ps_node_|ND_[A-Z0-9_]+/.test(resolvedTreeHir) ||
    !/static\s+int\s+emit_resolved_typed_hir\s*\(/.test(
      semanticTreeResolutionSource,
    ) ||
    !/psx_typed_hir_tree_emit\s*\(/.test(
      semanticTreeResolutionSource,
    )) {
  throw new Error(
    "mutable AST compatibility sources must be absent and frontend resolution must use direct Typed HIR exclusively: " +
      (remainingMutableCompatibilitySources.join(", ") || "none"),
  );
}

if (!/int\s+psx_frontend_resolve_parsed_function_to_hir_in_session\s*\([^)]*psx_hir_node_id_t\s*\*hir_root/.test(
      semanticPipelineSource,
    ) ||
    /node_t\s*\*psx_frontend_analyze_function_in_session/.test(
      semanticPipelineSource,
    )) {
  throw new Error(
    "public function resolution must return a Typed HIR root instead of a semantic node tree",
  );
}
if (/\bnode_t\b|\bND_[A-Z0-9_]+\b|parser\/ast\.h|#include\s+"ir_builder\.h"/.test(hirIrBuilder) ||
    !/ir_build_function_module_from_hir\s*\(/.test(hirIrBuilder)) {
  throw new Error(
    "direct Typed HIR IR lowering must not depend on parser AST nodes",
  );
}
if (!/ir_build_function_module_from_hir\s*\(/.test(compilerMainSource) ||
    /ir_build_function_module_with_options\s*\(|#include\s+"lowering\/ir_builder\.h"|AG_(?:DISABLE|REQUIRE)_TYPED_HIR|translation_unit_legacy_ast/.test(
      compilerMainSource,
    ) ||
    /COMPAT_AST_IR_SRCS|src\/lowering\/ir_builder\.c/.test(
      makefileSource,
    ) ||
    !/LOWERING_SRCS\s*=\s*\$\(wildcard\s+src\/lowering\/\*\.c\)/.test(
      makefileSource,
    )) {
  throw new Error(
    "compiler function lowering must consume Typed HIR without an AST fallback",
  );
}

const astTypeMutationRe =
  /(?:->|\.)type->[A-Za-z_][A-Za-z0-9_]*\s*=(?!=)/g;
const mutableNodeTypeReadRe =
  /^[ \t]*psx_type_t\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*=\s*ps_node_get_type\s*\(/gm;
const castedNodeTypeReadRe =
  /\(\s*psx_type_t\s*\*\s*\)\s*ps_node_get_type\s*\(/g;
const mutableSymbolTypeReadRe =
  /^[ \t]*psx_type_t\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*=\s*ps_(?:lvar|gvar)_get_decl_type\s*\(/gm;
const castedSymbolTypeReadRe =
  /\(\s*psx_type_t\s*\*\s*\)\s*ps_(?:lvar|gvar)_get_decl_type\s*\(/g;
const symbolTypeBodyMutationRe =
  /(?:->|\.)decl_type->[A-Za-z_][A-Za-z0-9_]*\s*=(?!=)/g;
const functionTypeBodyMutationRe =
  /(?:->|\.)function_type->[A-Za-z_][A-Za-z0-9_]*\s*=(?!=)/g;
const requestTypeBodyMutationRe =
  /\brequest->type->[A-Za-z_][A-Za-z0-9_]*\s*=(?!=)/g;
const resolutionTypeBodyMutationRe =
  /\bresolution->type->[A-Za-z_][A-Za-z0-9_]*\s*=(?!=)/g;
const astAuxiliaryTypeMutationRe =
  /(?:->|\.)(?:bound_base_type|resolved_type|object_type)->[A-Za-z_][A-Za-z0-9_]*\s*=(?!=)/g;
const symbolTypeRootMutationRe =
  /\b(?:var|global|gv)->decl_type\s*=(?!=)/g;
const escapedCanonicalTypeConstRe = /\(\s*psx_type_t\s*\*\s*\)/g;
const astTypeOwnershipViolations = [];
const symbolTypeRootOwners = new Set([
  "src/parser/global_registry.c",
  "src/parser/local_registry.c",
]);
const ownedChildMutationUsers = new Set([
  "src/parser/semantic_ctx.c",
  "src/parser/semantic_ctx_legacy.c",
  "src/parser/type.c",
  "src/parser/type_owned_internal.h",
]);
const ownedChildMutationRe =
  /\bpsx_type_owned_(?:base|param)_mut\s*\(/g;
for (const file of sourceFiles) {
  const source = await readFile(file, "utf8");
  for (const pattern of [
    astTypeMutationRe,
    mutableNodeTypeReadRe,
    castedNodeTypeReadRe,
    mutableSymbolTypeReadRe,
    castedSymbolTypeReadRe,
    symbolTypeBodyMutationRe,
    functionTypeBodyMutationRe,
    requestTypeBodyMutationRe,
    resolutionTypeBodyMutationRe,
    astAuxiliaryTypeMutationRe,
  ]) {
    pattern.lastIndex = 0;
    for (const match of source.matchAll(pattern)) {
      astTypeOwnershipViolations.push(`${file}:${match[0]}`);
    }
  }
  if (!symbolTypeRootOwners.has(file)) {
    symbolTypeRootMutationRe.lastIndex = 0;
    for (const match of source.matchAll(symbolTypeRootMutationRe)) {
      astTypeOwnershipViolations.push(`${file}:${match[0]}`);
    }
  }
  if (file !== "src/parser/type.c") {
    escapedCanonicalTypeConstRe.lastIndex = 0;
    for (const match of source.matchAll(escapedCanonicalTypeConstRe)) {
      astTypeOwnershipViolations.push(`${file}:${match[0]}`);
    }
  }
  if (!ownedChildMutationUsers.has(file)) {
    ownedChildMutationRe.lastIndex = 0;
    for (const match of source.matchAll(ownedChildMutationRe)) {
      astTypeOwnershipViolations.push(`${file}:${match[0]}`);
    }
  }
}
if (astTypeOwnershipViolations.length) {
  throw new Error(
    "AST-bound canonical types must not be mutated or recovered as mutable views:\n" +
      astTypeOwnershipViolations.sort().join("\n"),
  );
}

const errorCatalogHeader = await readFile(
  "src/diag/error_catalog.h",
  "utf8",
);
const warningCatalogHeader = await readFile(
  "src/diag/warning_catalog.h",
  "utf8",
);
if (!/DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION\s*=\s*3096/.test(
      errorCatalogHeader,
    ) ||
    /DIAG_WARN_PARSER_UNSUPPORTED_GNU_EXTENSION/.test(
      warningCatalogHeader,
    ) ||
    !/psx_parse_statement_expression_syntax[^]*?DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION/.test(
      parserStatementSource,
    ) ||
    !/diagnose_unsupported_gnu_extension[^]*?array range designator/.test(
      initializerSyntaxSource,
    ) ||
    !/diagnose_unsupported_gnu_extension_token[^]*?DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION/.test(
      preprocessSource,
    ) ||
    /\bND_STMT_EXPR\b/.test(syntaxTypedHirResolutionSource)) {
  throw new Error(
    "GNU extensions must be rejected as E3096 before direct Typed HIR resolution",
  );
}

const hirIrBuilderSource = await readFile(
  "src/lowering/hir_ir_builder.c",
  "utf8",
);
const hirIrCfgSource = await readFile(
  "src/lowering/hir_ir_cfg.c",
  "utf8",
);
const hirIrStatementSource = await readFile(
  "src/lowering/hir_ir_statement.c",
  "utf8",
);
const hirIrBuilderInternalHeader = await readFile(
  "src/lowering/hir_ir_builder_internal.h",
  "utf8",
);
const hirIrExpressionSource = await readFile(
  "src/lowering/hir_ir_expression.c",
  "utf8",
);
const complexCompoundAssignmentLowering = hirIrExpressionSource.match(
  /static\s+ir_val_t\s+build_complex_compound_assignment\s*\([^]*?\n\}/,
);
if (!complexCompoundAssignmentLowering ||
    (complexCompoundAssignmentLowering[0].match(
      /\bhir_ir_lvalue_address\s*\(/g,
    ) ?? []).length !== 1 ||
    !/\bemit_complex_binary_values\s*\(/.test(
      complexCompoundAssignmentLowering[0],
    ) ||
    !/\bconvert_complex_pointer\s*\(/.test(
      complexCompoundAssignmentLowering[0],
    ) ||
    !/\bcomplex_pointer_truth_value\s*\(/.test(
      complexCompoundAssignmentLowering[0],
    ) ||
    !/\bIR_MEMCPY\b/.test(complexCompoundAssignmentLowering[0]) ||
    !/hir_ir_is_complex_type\(target_type\)\s*\|\|[^]*?hir_ir_is_complex_type\(rhs_type\)[^]*?build_complex_compound_assignment\s*\(/.test(
      hirIrExpressionSource,
    ) ||
    !/kind\s*==\s*PSX_HIR_COMPOUND_ASSIGN[^]*?build_compound_assignment\s*\(/.test(
      hirIrExpressionSource,
    )) {
  throw new Error(
    "complex compound assignment must preserve one lvalue evaluation and lower through shared complex arithmetic",
  );
}
const atomicCompoundAssignmentLowering = hirIrExpressionSource.match(
  /static\s+ir_val_t\s+build_direct_atomic_compound_assignment\s*\([^]*?\n\}\n\nstatic\s+ir_val_t\s+build_compound_assignment/,
);
const directAtomicUpdateFinish = hirIrExpressionSource.match(
  /static\s+ir_val_t\s+finish_direct_atomic_update\s*\([^]*?\n\}/,
);
if (!atomicCompoundAssignmentLowering ||
    (atomicCompoundAssignmentLowering[0].match(
      /\bhir_ir_lvalue_address\s*\(/g,
    ) ?? []).length !== 1 ||
    (atomicCompoundAssignmentLowering[0].match(
      /\bhir_ir_build_expr\s*\(/g,
    ) ?? []).length !== 1 ||
    !/\bbegin_direct_atomic_update\s*\(/.test(
      atomicCompoundAssignmentLowering[0],
    ) ||
    !/\bfinish_direct_atomic_update\s*\(/.test(
      atomicCompoundAssignmentLowering[0],
    ) ||
    !directAtomicUpdateFinish ||
    !/atomic_kind\s*=\s*IR_ATOMIC_CAS/.test(
      directAtomicUpdateFinish[0],
    ) ||
    !/\bhir_ir_emit_conditional_branch\s*\([^]*?success_block[^]*?retry_block/.test(
      directAtomicUpdateFinish[0],
    ) ||
    !/PSX_TYPE_QUALIFIER_ATOMIC[^]*?build_direct_atomic_compound_assignment\s*\(/.test(
      hirIrExpressionSource,
    ) ||
    !/node_has_atomic_qualifier\(target\)[^]*?build_atomic_inc_dec\s*\(/.test(
      hirIrExpressionSource,
    ) ||
    !/node_has_atomic_qualifier\(node\)[^]*?load_direct_atomic_value\s*\(/.test(
      hirIrExpressionSource,
    ) ||
    (hirIrExpressionSource.match(
      /\bload_direct_lvalue_value\s*\(/g,
    ) ?? []).length < 6 ||
    !/node_has_atomic_qualifier\(target\)[^]*?store_direct_atomic_value\s*\(/.test(
      hirIrExpressionSource,
    ) ||
    !/target\.kind\s*==\s*PSX_TYPE_POINTER[^]*?target_type\.qualifiers\s*&[^]*?PSX_TYPE_QUALIFIER_ATOMIC[^]*?==\s*0/.test(
      assignmentResolutionSource,
    )) {
  throw new Error(
    "direct atomic loads, stores, inc/dec, and compound assignment must use atomic IR while compound pointer arithmetic remains rejected",
  );
}
const hirIrCallSource = await readFile(
  "src/lowering/hir_ir_call.c",
  "utf8",
);
const atomicPointerDeltaLowering = hirIrCallSource.match(
  /static\s+ir_val_t\s+scale_atomic_pointer_delta\s*\([^]*?\n\}/,
);
if (!/PSX_BUILTIN_CALL_ATOMIC_LOAD/.test(functionCallResolutionHeader) ||
    !/__ag_atomic_fetch_add[^]*?PSX_BUILTIN_CALL_ATOMIC_FETCH_ADD/.test(
      functionCallResolutionSource,
    ) ||
    !/psx_resolve_atomic_builtin_call\s*\([^]*?resolution\.return_qual_type/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/atomic_builtin_returns_object_value[^]*?result_type->qualifiers\s*=\s*PSX_TYPE_QUALIFIER_NONE/.test(
      functionCallResolutionSource,
    ) ||
    !/PSX_TYPE_POINTER[^]*?PSX_BUILTIN_CALL_ATOMIC_FETCH_ADD[^]*?PSX_BUILTIN_CALL_ATOMIC_FETCH_SUB[^]*?psx_semantic_pointer_points_to_complete_object_in/.test(
      functionCallResolutionSource,
    ) ||
    !atomicPointerDeltaLowering ||
    !/psx_semantic_type_table_base\s*\(/.test(
      atomicPointerDeltaLowering[0],
    ) ||
    !/psx_qual_type_layout_sizeof\s*\(/.test(
      atomicPointerDeltaLowering[0],
    ) ||
    !/IR_MUL/.test(atomicPointerDeltaLowering[0]) ||
    !/object_shape\.kind\s*==\s*PSX_TYPE_POINTER[^]*?IR_TY_PTR/.test(
      hirIrCallSource,
    ) ||
    !/IR_ARMW_ADD\s*\|\|\s*rmw_operation\s*==\s*IR_ARMW_SUB[^]*?scale_atomic_pointer_delta/.test(
      hirIrCallSource,
    )) {
  throw new Error(
    "stdatomic pointer operations must preserve pointer result types and scale fetch_add/sub by the pointed object layout",
  );
}
const hirIrAggregateSource = await readFile(
  "src/lowering/hir_ir_aggregate.c",
  "utf8",
);
const hirIrVlaSource = await readFile(
  "src/lowering/hir_ir_vla.c",
  "utf8",
);
if (/\bir_val_t\s+hir_ir_build_expr\s*\([^;]*\)\s*\{/.test(
      hirIrBuilderSource,
    ) ||
    !/\bir_val_t\s+hir_ir_build_expr\s*\([^;]*\)\s*\{/.test(
      hirIrExpressionSource,
    ) ||
    /\bir_val_t\s+hir_ir_build_call\s*\([^;]*\)\s*\{/.test(
      hirIrExpressionSource,
    ) ||
    !/\bir_val_t\s+hir_ir_build_call\s*\([^;]*\)\s*\{/.test(
      hirIrCallSource,
    ) ||
    /\bir_val_t\s+hir_ir_aggregate_value_address\s*\([^;]*\)\s*\{/.test(
      hirIrExpressionSource,
    ) ||
    !/\bir_val_t\s+hir_ir_aggregate_value_address\s*\([^;]*\)\s*\{/.test(
      hirIrAggregateSource,
    ) ||
    /\bint\s+hir_ir_build_statement\s*\([^;]*\)\s*\{/.test(
      hirIrBuilderSource,
    ) ||
    !/\bint\s+hir_ir_build_statement\s*\([^;]*\)\s*\{/.test(
      hirIrStatementSource,
    ) ||
    !/\bhir_ir_cfg_new_block\s*\([^;]*\)\s*\{/.test(hirIrCfgSource) ||
    /\bhir_ir_cfg_new_block\s*\([^;]*\)\s*\{/.test(hirIrBuilderSource) ||
    /\bir_val_t\s+hir_ir_build_vla_allocation\s*\([^;]*\)\s*\{/.test(
      hirIrBuilderSource,
    ) ||
    /\bint\s+hir_ir_emit_vla_parameter_strides\s*\([^;]*\)\s*\{/.test(
      hirIrBuilderSource,
    ) ||
    !/\bir_val_t\s+hir_ir_build_vla_allocation\s*\([^;]*\)\s*\{/.test(
      hirIrVlaSource,
    ) ||
    !/\bint\s+hir_ir_emit_vla_parameter_strides\s*\([^;]*\)\s*\{/.test(
      hirIrVlaSource,
    ) ||
    !/\bint\s+psx_lower_static_scalar_hir_initializer\s*\([^;]*\)\s*\{/.test(
      staticHirInitializerSource,
    ) ||
    !/\bint\s+psx_build_static_aggregate_hir_initializer_plan\s*\([^;]*\)\s*\{/.test(
      staticHirInitializerSource,
    )) {
  throw new Error(
    "HIR-to-IR expression, call, aggregate, statement, CFG, VLA, and initializer lowering must remain separate modules",
  );
}

if (/hir_case_target_t\s+cases\s*\[/.test(
      hirIrBuilderInternalHeader,
    ) ||
    !/hir_case_target_t\s*\*cases\s*;/.test(
      hirIrBuilderInternalHeader,
    ) ||
    !/size_t\s+case_capacity\s*;/.test(
      hirIrBuilderInternalHeader,
    ) ||
    !/reserve_switch_cases\s*\(/.test(hirIrStatementSource) ||
    !/dispose_switch_target\s*\(/.test(hirIrStatementSource)) {
  throw new Error(
    "HIR-to-IR switch lowering must grow case targets dynamically",
  );
}

const wasmMachineIrSource = await readFile(
  "src/arch/wasm32/wasm32_machine_ir.c",
  "utf8",
);
const wasmMachineIrHeader = await readFile(
  "src/arch/wasm32/wasm32_machine_ir.h",
  "utf8",
);
const wasmMachineAbiSource = await readFile(
  "src/arch/wasm32/wasm32_machine_abi.c",
  "utf8",
);
const wasmMachineFunctionSource = await readFile(
  "src/arch/wasm32/wasm32_machine_function.c",
  "utf8",
);
const wasmMachineFunctionHeader = await readFile(
  "src/arch/wasm32/wasm32_machine_function.h",
  "utf8",
);
const wasmMachineModuleSource = await readFile(
  "src/arch/wasm32/wasm32_machine_module.c",
  "utf8",
);
const wasmMachineModuleHeader = await readFile(
  "src/arch/wasm32/wasm32_machine_module.h",
  "utf8",
);
const wasmWatWriterSource = await readFile(
  "src/arch/wasm32/wasm32_ir.c",
  "utf8",
);
const wasmObjectWriterSource = await readFile(
  "src/arch/wasm32/wasm32_obj.c",
  "utf8",
);
const wasmObjectBufferSource = await readFile(
  "src/arch/wasm32/wasm32_obj_buffer.c",
  "utf8",
);
const wasmObjectSectionsSource = await readFile(
  "src/arch/wasm32/wasm32_obj_sections.c",
  "utf8",
);
const wasmWatRuntimeSource = await readFile(
  "src/arch/wasm32/wasm32_wat_runtime.c",
  "utf8",
);
if (!/wasm32_machine_select_binary\s*\(/.test(wasmMachineIrSource) ||
    !/wasm32_machine_select_binary\s*\(/.test(
      wasmMachineFunctionSource,
    ) ||
    /wasm32_machine_function_instruction\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    /wasm32_machine_function_instruction\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    !/machine\.instructions\s*\[/.test(wasmWatWriterSource) ||
    !/machine_function\.instructions\s*\[/.test(
      wasmObjectWriterSource,
    ) ||
    /wasm32_machine_select_binary\s*\(/.test(wasmWatWriterSource) ||
    /wasm32_machine_select_binary\s*\(/.test(wasmObjectWriterSource) ||
    /wasm32_machine_opcode_is_[A-Za-z0-9_]+\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    /wasm32_machine_opcode_is_[A-Za-z0-9_]+\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    !/guard_zero_divisor/.test(wasmWatWriterSource) ||
    !/guard_zero_divisor/.test(wasmObjectWriterSource) ||
    /static\s+const\s+char\s*\*\s*wasm_(?:fp_)?binop\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    /static\s+unsigned\s+(?:int|fp)_binop_opcode\s*\(/.test(
      wasmObjectWriterSource,
    )) {
  throw new Error(
    "Wasm WAT and object writers must serialize preselected Machine IR binary instructions",
  );
}

for (const [name, source] of [
  ["Wasm text", wasmWatWriterSource],
  ["Wasm object", wasmObjectWriterSource],
]) {
  if (!/WASM32_MACHINE_INST_CONTROL/.test(source) ||
      !/control\.target_block_id/.test(source) ||
      !/control\.else_block_id/.test(source) ||
      !/control\.value/.test(source) ||
      /(?:i|instruction)->(?:label_id|else_label_id)/.test(source)) {
    throw new Error(
      `${name} backend must serialize preplanned Machine control flow`,
    );
  }
}

if (!/wasm32_machine_select_load\s*\(/.test(wasmMachineIrSource) ||
    !/wasm32_machine_select_store\s*\(/.test(wasmMachineIrSource) ||
    !/wasm32_machine_select_load\s*\(/.test(wasmMachineFunctionSource) ||
    !/wasm32_machine_select_store\s*\(/.test(
      wasmMachineFunctionSource,
    ) ||
    !/static\s+void\s+emit_load\s*\([^]*?WASM32_MACHINE_INST_LOAD/.test(
      wasmWatWriterSource,
    ) ||
    !/static\s+void\s+emit_store\s*\([^]*?WASM32_MACHINE_INST_STORE/.test(
      wasmWatWriterSource,
    ) ||
    !/case\s+WASM32_MACHINE_INST_LOAD\s*:\s*\{/.test(
      wasmObjectWriterSource,
    ) ||
    !/case\s+WASM32_MACHINE_INST_STORE\s*:\s*\{/.test(
      wasmObjectWriterSource,
    ) ||
    /switch\s*\(\s*effective_load_type/.test(wasmWatWriterSource) ||
    /switch\s*\(\s*store_ty\s*\)/.test(wasmWatWriterSource) ||
    /\b0x(?:28|29|2a|2b|2c|2d|2e|2f|36|37|38|39|3a|3b)\b/.test(
      wasmObjectWriterSource,
    )) {
  throw new Error(
    "Wasm WAT and object writers must serialize preselected Machine IR memory instructions",
  );
}

if (!/wasm32_machine_copy_plan_build\s*\(/.test(wasmMachineIrSource) ||
    !/wasm32_machine_copy_plan_build\s*\(/.test(
      wasmMachineFunctionSource,
    ) ||
    !/result_copy/.test(wasmMachineFunctionSource) ||
    !/direct_result_load/.test(wasmMachineFunctionSource) ||
    !/direct_result_store/.test(wasmMachineFunctionSource) ||
    !/copy\.chunks/.test(wasmWatWriterSource) ||
    !/copy\.chunks/.test(wasmObjectWriterSource) ||
    !/copy_plans/.test(wasmWatWriterSource) ||
    !/copy_plans/.test(wasmObjectWriterSource) ||
    /wasm_copy_chunks/.test(wasmWatWriterSource) ||
    /for\s*\([^)]*(?:off|offset)\s*\+\s*(?:8|4|2)\s*<=/.test(
      wasmWatWriterSource,
    ) ||
    /for\s*\([^)]*(?:off|offset)\s*\+\s*(?:8|4|2)\s*<=/.test(
      wasmObjectWriterSource,
    )) {
  throw new Error(
    "Wasm writers must serialize preplanned Machine copy chunks and aggregate memory operations",
  );
}

if (!/wasm32_machine_alignment_plan_build\s*\(/.test(
      wasmMachineIrSource,
    ) ||
    !/case\s+IR_ALIGN_PTR\s*:[^]*?wasm32_machine_alignment_plan_build\s*\(\s*instruction->alloca_align,\s*16,\s*&selected->alignment\s*\)/.test(
      wasmMachineFunctionSource,
    ) ||
    !/case\s+IR_VLA_ALLOC\s*:[^]*?wasm32_machine_alignment_plan_build\s*\(\s*0,\s*16,\s*&selected->alignment\s*\)/.test(
      wasmMachineFunctionSource,
    ) ||
    !/alignment\.addend/.test(wasmWatWriterSource) ||
    !/alignment\.mask/.test(wasmWatWriterSource) ||
    !/alignment\.addend/.test(wasmObjectWriterSource) ||
    !/alignment\.mask/.test(wasmObjectWriterSource) ||
    /\balloca_align\b/.test(wasmWatWriterSource) ||
    /\balloca_align\b/.test(wasmObjectWriterSource) ||
    /int\s+alloca_(?:size|align)\s*;/.test(wasmMachineFunctionHeader)) {
  throw new Error(
    "Wasm serializers must encode preplanned pointer and dynamic-allocation alignment",
  );
}

if (!/wasm32_machine_stack_plan_build\s*\(/.test(
      wasmMachineIrSource,
    ) ||
    !/wasm32_machine_stack_plan_t\s+stack\s*;/.test(
      wasmMachineFunctionHeader,
    ) ||
    !/wasm32_machine_stack_plan_build\s*\([^]*?&function->stack/.test(
      wasmMachineFunctionSource,
    ) ||
    !/stack\.saves_stack_pointer/.test(wasmWatWriterSource) ||
    !/stack\.restores_stack_pointer/.test(wasmWatWriterSource) ||
    !/stack\.fixed_frame_size/.test(wasmWatWriterSource) ||
    !/stack\.restores_stack_pointer/.test(wasmObjectWriterSource) ||
    !/stack\.uses_stack_pointer/.test(wasmObjectWriterSource) ||
    /machine_function\.(?:frame_size|has_vla_alloc|has_variadic_varargs)/.test(
      wasmObjectWriterSource,
    ) ||
    /ctx\.machine\.frame_size/.test(wasmWatWriterSource) ||
    /int\s+frame_size\s*;|has_vla_alloc|has_variadic_varargs/.test(
      wasmMachineFunctionHeader,
    )) {
  throw new Error(
    "Wasm serializers must consume one Machine function stack plan",
  );
}

for (const [name, source] of [
  ["Wasm text", wasmWatWriterSource],
  ["Wasm object", wasmObjectWriterSource],
]) {
  if (/switch\s*\(\s*i->op\s*\)/.test(source) ||
      !/switch\s*\(\s*planned->kind\s*\)/.test(source)) {
    throw new Error(
      `${name} backend must dispatch selected Machine instructions instead of source IR opcodes`,
    );
  }
  if (/(?:planned|selected|instruction|i)->source\b/.test(source) ||
      /machine_function\.instructions\[[^\]]+\]\.source/.test(source) ||
      /ir_abi_reference_signature\s*\(/.test(source)) {
    throw new Error(
      `${name} backend must serialize Machine instruction operands without dereferencing source IR`,
    );
  }
}

if (!/wasm32_machine_select_unary\s*\(/.test(wasmMachineIrSource) ||
    !/wasm32_machine_select_atomic_rmw\s*\(/.test(wasmMachineIrSource) ||
    !/wasm32_machine_select_unary\s*\(/.test(
      wasmMachineFunctionSource,
    ) ||
    !/wasm32_machine_select_atomic_rmw\s*\(/.test(
      wasmMachineFunctionSource,
    ) ||
    /wasm32_machine_select_(?:unary|atomic_rmw)\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    /wasm32_machine_select_(?:unary|atomic_rmw)\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    !/WASM32_MACHINE_INST_UNARY/.test(wasmWatWriterSource) ||
    !/WASM32_MACHINE_INST_ATOMIC/.test(wasmWatWriterSource) ||
    !/WASM32_MACHINE_INST_UNARY/.test(wasmObjectWriterSource) ||
    !/WASM32_MACHINE_INST_ATOMIC/.test(wasmObjectWriterSource) ||
    !/WASM32_MACHINE_ATOMIC_COMPARE_EXCHANGE/.test(
      wasmMachineFunctionSource,
    ) ||
    /\bIR_(?:ATOMIC|ARMW)_[A-Z0-9_]+\b/.test(wasmWatWriterSource) ||
    /\bIR_(?:ATOMIC|ARMW)_[A-Z0-9_]+\b/.test(
      wasmObjectWriterSource,
    ) ||
    /switch\s*\(\s*i->atomic_rmw_op\s*\)/.test(wasmWatWriterSource) ||
    /switch\s*\(\s*i->atomic_rmw_op\s*\)/.test(wasmObjectWriterSource)) {
  throw new Error(
    "Wasm WAT and object writers must serialize preselected Machine IR unary and atomic instructions",
  );
}

if (!/WASM32_MACHINE_INST_CONVERSION/.test(wasmMachineFunctionSource) ||
    !/WASM32_MACHINE_INST_CONVERSION/.test(wasmWatWriterSource) ||
    !/WASM32_MACHINE_INST_CONVERSION/.test(wasmObjectWriterSource) ||
    !/wasm32_machine_primitive_plan_build\s*\(/.test(
      wasmMachineIrSource,
    ) ||
    !/wasm32_machine_primitive_plan_t\s+primitives\s*;/.test(
      wasmMachineModuleHeader,
    ) ||
    !/unsigned\s+char\s+has_primitive_plan\s*;/.test(
      wasmMachineModuleHeader,
    ) ||
    !/wasm32_machine_primitive_plan_build\s*\(\s*&module->primitives\s*\)/.test(
      wasmMachineModuleSource,
    ) ||
    !/wasm32_machine_module_primitives\s*\(/.test(
      wasmMachineModuleSource,
    ) ||
    !/wasm32_machine_module_primitives\s*\(\s*machine_module\s*\)/.test(
      wasmWatWriterSource,
    ) ||
    !/wasm32_machine_module_primitives\s*\(\s*machine_module\s*\)/.test(
      wasmObjectWriterSource,
    ) ||
    /wasm32_machine_primitive_plan_build\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    /wasm32_machine_primitive_plan_build\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    /wasm32_machine_primitive_plan_t\s+primitives\s*;/.test(
      wasmWatWriterSource,
    ) ||
    /wasm32_machine_primitive_plan_t\s+primitives\s*;/.test(
      wasmObjectWriterSource,
    ) ||
    !/wasm32_machine_binary_t\s+i32_add\s*;/.test(
      wasmMachineIrHeader,
    ) ||
    !/wasm32_machine_zero_test_t\s+i64_zero_test\s*;/.test(
      wasmMachineIrHeader,
    ) ||
    !/wasm32_machine_select_zero_test\s*\(/.test(
      wasmMachineIrSource,
    ) ||
    /i(?:32|64)\.(?:add|sub|and|eq|eqz|ne)/.test(
      wasmWatWriterSource,
    ) ||
    /\b0x(?:45|46|47|50|6a|6b|71)\b/.test(
      wasmObjectWriterSource,
    ) ||
    !/wasm32_machine_planned_conversion\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    !/wasm32_machine_planned_conversion\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    /wasm32_machine_select_(?:conversion|load|store|binary|unary|atomic_rmw)\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    /wasm32_machine_select_(?:conversion|load|store|binary|unary|atomic_rmw)\s*\(/.test(
      wasmObjectWriterSource,
    )) {
  throw new Error(
    "Wasm WAT and object writers must serialize the preplanned Machine primitive catalog",
  );
}

if (!/wasm32_machine_signature_from_abi\s*\(/.test(
      wasmMachineAbiSource,
    ) ||
    !/wasm32_machine_call_signature\s*\(/.test(wasmMachineAbiSource) ||
    !/wasm32_machine_signature_from_abi\s*\(/.test(
      wasmMachineModuleSource,
    ) ||
    /wasm32_machine_signature_from_abi\s*\(/.test(wasmWatWriterSource) ||
    /wasm32_machine_signature_from_abi\s*\(/.test(wasmObjectWriterSource) ||
    !/wasm32_machine_call_signature\s*\(/.test(
      wasmMachineFunctionSource,
    ) ||
    !/WASM32_MACHINE_INST_CALL/.test(wasmWatWriterSource) ||
    !/WASM32_MACHINE_INST_CALL/.test(wasmObjectWriterSource) ||
    /(?:ir_abi_call_(?:signature|arguments)|wasm32_machine_call_signature)\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    /(?:ir_abi_call_(?:signature|arguments)|wasm32_machine_call_signature)\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    /\bir_abi_(?:argument|piece)_t\b|\bIR_ABI_(?:ARGUMENT|PIECE)_[A-Z0-9_]+\b/.test(
      wasmWatWriterSource,
    ) ||
    /\bir_abi_(?:argument|piece)_t\b|\bIR_ABI_(?:ARGUMENT|PIECE)_[A-Z0-9_]+\b/.test(
      wasmObjectWriterSource,
    ) ||
    !/\bwasm32_machine_argument_t\b/.test(wasmWatWriterSource) ||
    !/\bwasm32_machine_argument_t\b/.test(wasmObjectWriterSource) ||
    !/\bwasm32_machine_call_visit_variadic_prepare\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    !/\bwasm32_machine_call_visit_variadic_restore\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    !/\bwasm32_machine_call_visit_variadic_prepare\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    !/\bwasm32_machine_call_visit_variadic_restore\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    !/\bvariadic_area_size\b/.test(wasmMachineFunctionSource) ||
    /\bnargs_var\b/.test(wasmWatWriterSource) ||
    /\bnargs_var\b/.test(wasmObjectWriterSource) ||
    !/\bwasm32_machine_parameter_piece_t\b/.test(
      wasmMachineFunctionSource,
    )) {
  throw new Error(
    "Wasm WAT and object writers must serialize the preplanned Machine call ABI",
  );
}

if (!/compute_instruction_liveness\s*\(/.test(
      wasmMachineFunctionSource,
    ) ||
    !/dst_used_after/.test(wasmMachineFunctionHeader) ||
    /vreg_used_after\s*\(|inst_uses_vreg\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    /vreg_used_after\s*\(|inst_uses_vreg\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    /for\s*\([^)]*variadic_argument_count/.test(
      wasmWatWriterSource,
    ) ||
    /for\s*\([^)]*variadic_argument_count/.test(
      wasmObjectWriterSource,
    )) {
  throw new Error(
    "Wasm writers must consume Machine liveness and variadic action plans without replanning them",
  );
}

if (!/wasm32_obj_buffer_reserve\s*\(/.test(
      wasmObjectBufferSource,
    ) ||
    !/wasm32_obj_buffer_(?:uleb|sleb|section)\s*\(/.test(
      wasmObjectBufferSource,
    ) ||
    /static\s+(?:int|void|uint32_t)\s+wb_(?:reserve|u8|bytes|uleb|sleb)\s*\(/.test(
      wasmObjectWriterSource,
    )) {
  throw new Error(
    "Wasm object serialization must keep byte and section encoding in its serializer buffer module",
  );
}

if (!/wasm32_obj_serialize_sections\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    !/void\s+wasm32_obj_serialize_sections\s*\(/.test(
      wasmObjectSectionsSource,
    ) ||
    !/emit_(?:type|import|function|code|data|linking|reloc)_section\s*\(/.test(
      wasmObjectSectionsSource,
    ) ||
    /static\s+void\s+emit_(?:type|import|function|code|data|linking|reloc)_section\s*\(/.test(
      wasmObjectWriterSource,
    )) {
  throw new Error(
    "Wasm object module assembly must delegate section and relocation serialization to its section writer",
  );
}

if (!/wasm32_machine_function_build\s*\(/.test(
      wasmMachineFunctionSource,
    ) ||
    !/wasm32_machine_function_build\s*\(/.test(
      wasmMachineModuleSource,
    ) ||
    !/wasm32_machine_module_build\s*\(/.test(
      wasmBackendContextSource,
    ) ||
    /wasm32_machine_function_build\s*\(/.test(wasmWatWriterSource) ||
    /wasm32_machine_function_build\s*\(/.test(wasmObjectWriterSource) ||
    !/wasm32_machine_module_function\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    !/wasm32_machine_module_function\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    /\bir_abi_function_signature\s*\(/.test(wasmWatWriterSource) ||
    /\bir_abi_function_signature\s*\(/.test(wasmObjectWriterSource) ||
    /const\s+ir_abi_module_t\s*\*\s*abi\s*;/.test(
      wasmWatWriterSource,
    ) ||
    /const\s+ir_abi_module_t\s*\*\s*abi\s*;/.test(
      wasmObjectWriterSource,
    ) ||
    /static\s+void\s+collect_local_types\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    /static\s+void\s+collect_inst_vregs\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    /static\s+int\s+collect_frame_size\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    /static\s+void\s+add_alloca_slot\s*\(/.test(wasmWatWriterSource)) {
  throw new Error(
    "Wasm backend orchestration must build one owned Machine module while WAT and object writers only consume preplanned functions",
  );
}

if (!/char\s+\*name\s*;/.test(wasmMachineFunctionHeader) ||
    !/char\s+\*c_signature\s*;/.test(wasmMachineFunctionHeader) ||
    !/char\s+\*continuation_entry_name\s*;/.test(
      wasmMachineFunctionHeader,
    ) ||
    !/copy_function_metadata\s*\(/.test(wasmMachineFunctionSource) ||
    !/free\(function->continuation_result_export\)/.test(
      wasmMachineFunctionSource,
    ) ||
    /const\s+ir_func_t\s*\*source\s*;/.test(
      wasmMachineFunctionHeader,
    ) ||
    /ir_(?:inst|block)_t\s*\*source\s*;/.test(
      wasmMachineFunctionHeader,
    ) ||
    /\bir_op_t\s+op\s*;/.test(wasmMachineFunctionHeader) ||
    !/wasm32_machine_inst_kind_name\s*\(/.test(
      wasmMachineFunctionHeader,
    ) ||
    !/wasm32_machine_inst_kind_name\s*\(/.test(
      wasmMachineFunctionSource,
    ) ||
    /wasm32_machine_function_instruction\s*\(/.test(
      wasmMachineFunctionHeader,
    ) ||
    /wasm32_machine_function_instruction\s*\(/.test(
      wasmMachineFunctionSource,
    ) ||
    !/copy_string\s*\(\s*&machine_instruction->sym/.test(
      wasmMachineFunctionSource,
    ) ||
    !/free\(function->instructions\[i\]\.sym\)/.test(
      wasmMachineFunctionSource,
    ) ||
    /\bir_func_t\b/.test(wasmWatWriterSource) ||
    /\bir_func_t\b/.test(wasmObjectWriterSource) ||
    /(?:i|instruction|selected)->op\b/.test(wasmWatWriterSource) ||
    /(?:i|instruction|selected)->op\b/.test(wasmObjectWriterSource) ||
    /\bir_op_name\s*\(/.test(wasmWatWriterSource) ||
    /\bir_op_name\s*\(/.test(wasmObjectWriterSource) ||
    /\bir_module_t\b/.test(wasmWatWriterSource) ||
    /\bir_module_t\b/.test(wasmObjectWriterSource) ||
    /module->funcs/.test(wasmWatWriterSource) ||
    /module->funcs/.test(wasmObjectWriterSource) ||
    /const\s+ir_module_t\s*\*source\s*;/.test(
      wasmMachineModuleHeader,
    ) ||
    !/wasm32_machine_symbol_t\s*\*symbols\s*;/.test(
      wasmMachineModuleHeader,
    ) ||
    !/instruction->resolved_symbol\s*=\s*wasm32_machine_module_symbol/.test(
      wasmMachineModuleSource,
    ) ||
    !/index\s*<\s*machine_module->function_count/.test(
      wasmWatWriterSource,
    ) ||
    !/function_index\s*<\s*machine_module->function_count/.test(
      wasmObjectWriterSource,
    )) {
  throw new Error(
    "Wasm serializers must use Machine function identity, linkage, and continuation metadata",
  );
}

if (!/wasm32_machine_data_object_t\s*\*data_objects\s*;/.test(
      wasmMachineModuleHeader,
    ) ||
    !/wasm32_machine_module_build_data\s*\(/.test(
      wasmMachineModuleSource,
    ) ||
    !/copy_data_object\s*\(/.test(wasmMachineModuleSource) ||
    !/ir_abi_data_relocation_signature\s*\(/.test(
      wasmMachineModuleSource,
    ) ||
    !/wasm32_emit_machine_data_segments_in\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    !/wasm32_obj_emit_machine_data_segments_in\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    /\bir_data_(?:module|object|reloc)_t\b/.test(wasmWatWriterSource) ||
    /\bir_data_(?:module|object|reloc)_t\b/.test(wasmObjectWriterSource) ||
    /\bir_abi_data_module_t\b|ir_abi_data_relocation_signature\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    /\bir_abi_data_module_t\b|ir_abi_data_relocation_signature\s*\(/.test(
      wasmObjectWriterSource,
    ) ||
    !/wasm32_machine_module_build_data\s*\(/.test(
      wasmBackendContextSource,
    )) {
  throw new Error(
    "Wasm serializers must consume owned Machine data objects and relocations",
  );
}

if (!/void\s+wasm32_wat_emit_minimal_libc_stubs\s*\(\s*wasm32_ir_context_t\s*\*context\s*\)\s*\{/.test(
      wasmWatRuntimeSource,
    ) ||
    /void\s+wasm32_wat_emit_minimal_libc_stubs\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    !/wasm32_wat_emit_minimal_libc_stubs\s*\(\s*context\s*\)/.test(
      wasmWatWriterSource,
    )) {
  throw new Error(
    "Wasm WAT writer must keep the minimal libc runtime in its dedicated emitter",
  );
}

const wasmWatCallSerializer = wasmWatWriterSource.match(
  /static\s+void\s+emit_call\s*\([^]*?\)\s*\{([^]*?)\n\}/,
);
if (!wasmWatCallSerializer ||
    !/int\s+wasm32_wat_runtime_module_plan_build\s*\(/.test(
      wasmWatRuntimeSource,
    ) ||
    !/wasm32_wat_runtime_module_plan_call\s*\(/.test(
      wasmWatRuntimeSource,
    ) ||
    !/wasm32_wat_runtime_module_plan_dispose\s*\(/.test(
      wasmWatRuntimeSource,
    ) ||
    !/wasm32_wat_runtime_module_plan_build\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    !/wasm32_wat_runtime_module_plan_dispose\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    !/wasm32_wat_runtime_module_plan_call\s*\(/.test(
      wasmWatCallSerializer[1],
    ) ||
    /wasm32_wat_runtime_(?:plan_call|call_plan_dispose)\s*\(/.test(
      wasmWatCallSerializer[1],
    ) ||
    /\b(?:calloc|malloc|realloc|free)\s*\(/.test(
      wasmWatCallSerializer[1],
    ) ||
    /"(?:printf|fprintf|snprintf|swprintf|sscanf|swscanf|scalbln|scalblnf|scalblnl)"/.test(
      wasmWatCallSerializer[1],
    )) {
  throw new Error(
    "Wasm WAT writer must consume prebuilt runtime bridge call plans without planning or allocation",
  );
}

if (!/result_source_size/.test(wasmMachineFunctionSource) ||
    !/direct_result_type/.test(wasmMachineFunctionSource) ||
    !/machine\.result_copy/.test(wasmWatWriterSource) ||
    !/machine\.direct_result_load/.test(wasmWatWriterSource) ||
    !/machine_function\.result_copy/.test(wasmObjectWriterSource) ||
    !/machine_function\.direct_result_load/.test(
      wasmObjectWriterSource,
    ) ||
    /ir_abi_signature_(?:result_is_indirect|result_is_direct_aggregate|direct_result_type|result_source_size)\s*\(/.test(
      wasmWatWriterSource,
    ) ||
    /ir_abi_signature_(?:result_is_indirect|result_is_direct_aggregate|direct_result_type|result_source_size)\s*\(/.test(
      wasmObjectWriterSource,
    )) {
  throw new Error(
    "Wasm writers must serialize the Machine function result plan without reinterpreting ABI sidecars",
  );
}

const runtimeSymbolManifest = JSON.parse(await readFile(
  "tools/wasm_obj_linker/runtime/symbol-manifest.json",
  "utf8",
));
const runtimeSymbolGenerator = await readFile(
  "tools/wasm_obj_linker/generate_runtime_symbol_manifest.mjs",
  "utf8",
);
const runtimeLinkerSource = await readFile(
  "tools/wasm_obj_linker/ag_wasm_link.c",
  "utf8",
);
const runtimeImportsSource = await readFile(
  "tools/wasm_js_api/agc-runtime-imports.js",
  "utf8",
);
const wasmLinkerWrapperSource = await readFile(
  "tools/wasm_obj_linker/ag-wasm-link.js",
  "utf8",
);
const wasmToolchainSource = await readFile(
  "tools/wasm_js_api/agc-toolchain.js",
  "utf8",
);
const runtimeCatalog = await readFile(
  "tools/wasm_obj_linker/runtime/generated/runtime-symbols.md",
  "utf8",
);
const runtimeManifestFields = [
  "cSymbol",
  "runtimeSymbol",
  "importNamespace",
  "signature",
  "memory",
  "availability",
  "bridge",
];
const strlenRuntimeEntry = runtimeSymbolManifest.functions.find(
  (entry) => entry.cSymbol === "strlen",
);
if (runtimeSymbolManifest.version !== 2 ||
    !Array.isArray(runtimeSymbolManifest.functions) ||
    runtimeSymbolManifest.functions.length === 0 ||
    runtimeSymbolManifest.functions.some((entry) =>
      runtimeManifestFields.some((field) => !(field in entry)) ||
      typeof entry.memory?.read !== "boolean" ||
      typeof entry.memory?.write !== "boolean") ||
    !strlenRuntimeEntry?.memory.read || strlenRuntimeEntry.memory.write ||
    /params\.length\s*>\s*16|while\s*\(params\.length\s*<\s*16\)/.test(
      runtimeSymbolGenerator,
    ) ||
    !/const\s+unsigned\s+char\s*\*param_types\s*;/.test(
      runtimeLinkerSource,
    ) ||
    !/uint32_t\s+param_count\s*;/.test(runtimeLinkerSource) ||
    /param_types\s*\[\s*16\s*\]/.test(runtimeLinkerSource) ||
    !/generateC\(manifest\)/.test(runtimeSymbolGenerator) ||
    !/generateJs\(manifest\)/.test(runtimeSymbolGenerator) ||
    !/generateDocs\(manifest\)/.test(runtimeSymbolGenerator) ||
    !/runtime_manifest_signature_matches\(entry, target_type, 0\)/.test(
      runtimeLinkerSource,
    ) ||
    !/runtime_manifest_signature_matches\(entry, caller_type, 1\)/.test(
      runtimeLinkerSource,
    ) ||
    !/materializeAgcRuntimeImportGroup\("env", "math", env\)/.test(
      runtimeImportsSource,
    ) ||
    !/materializeAgcRuntimeImportGroup\("env", "stdio", implementations\)/.test(
      runtimeImportsSource,
    ) ||
    !/This file is generated from `tools\/wasm_obj_linker\/runtime\/symbol-manifest\.json`/.test(
      runtimeCatalog,
    )) {
  throw new Error(
    "runtime manifest must own signatures, effects, availability, linker routing, JS imports, and generated docs",
  );
}

if (/duplicate symbol definition|frame condition call outside configured continuation loop/.test(
      runtimeLinkerSource,
    ) ||
    !/AGC_LINK_DUPLICATE_CONTINUATION_ENTRY/.test(runtimeLinkerSource) ||
    !/AGC_LINK_DUPLICATE_SYMBOL/.test(runtimeLinkerSource) ||
    !/AGC_LINK_FRAME_CONDITION_OUTSIDE_LOOP/.test(runtimeLinkerSource) ||
    !/parseLinkDiagnostic\s*\(/.test(wasmLinkerWrapperSource) ||
    !/localizeLinkError\s*\(/.test(wasmToolchainSource) ||
    !/normalizedSources/.test(wasmToolchainSource) ||
    !/diagnosticLocale/.test(wasmToolchainSource)) {
  throw new Error(
    "continuation linker failures must preserve structured codes and source identity without exposing raw internal diagnostics",
  );
}

if (!/PSX_HIR_VARARG_CURSOR/.test(hirHeader) ||
    /PSX_HIR_VA_ARG_AREA/.test(hirHeader) ||
    !/IR_VARARG_CURSOR/.test(irHeaderSource) ||
    /IR_VA_ARG_AREA/.test(irHeaderSource) ||
    /Apple ARM64 ABI variadic argument-area builtin/.test(irHeaderSource)) {
  throw new Error(
    "generic HIR and IR must model a target-independent variadic cursor",
  );
}

const contextlessTokenizerTestApi =
  /\btk_(?:get_default_context|context_activate|context_active|test_context_shutdown|get_current_token|set_current_token|consume|consume_str|consume_ident|expect|expect_number|at_eof|tokenize|set_strict_c11_mode|get_strict_c11_mode|set_enable_trigraphs|get_enable_trigraphs|set_enable_binary_literals|get_enable_binary_literals|set_enable_c11_audit_extensions|get_enable_c11_audit_extensions|reset_tokenizer_stats|get_tokenizer_stats|set_max_token_len_for_test)\s*\(/;
if (contextlessTokenizerTestApi.test(parserUnitTestSource) ||
    contextlessTokenizerTestApi.test(tokenizerUnitTestSource) ||
    /tokenizer_test(?:\.h|_hook\.c)/.test(
      `${parserUnitTestSource}\n${tokenizerUnitTestSource}\n${makefileSource}`,
    )) {
  throw new Error(
    "tokenizer tests must use explicit tokenizer contexts without active/default compatibility APIs",
  );
}
if (/static\s+node_t\s*\*\*\s*parsed_code\s*;/.test(
      parserUnitTestSource,
    ) ||
    /^\s*static\s+(?:ag_compilation_session_t|psx_semantic_context_t|psx_scope_graph_t|psx_global_registry_t|psx_local_registry_t|psx_lowering_context_t|tokenizer_context_t)\s*\*+\s*[A-Za-z_][A-Za-z0-9_]*\s*(?:=|;)/m.test(
      parserUnitTestSource,
    ) ||
    /static\s+psx_record_id_t\s+next_record_id\s*=/.test(
      parserUnitTestSource,
    )) {
  throw new Error(
    "parser tests must not retain mutable process-global compatibility state",
  );
}
if (/^\s*static\s+[^;\n]*\bfixture_(?:function|call|reference|data_relocation)\b/m.test(
      wasmMachineIrUnitTestSource,
    )) {
  throw new Error(
    "Wasm Machine IR tests must carry ABI fixtures through explicit module arguments without process-global test doubles",
  );
}
if (/\bpsx_type_t\b|type_compatibility_view|parser_type_compatibility|semantic_ctx_legacy|node_utils_legacy|parser\/type(?:_builder)?\.h|\bpsx_type_compatibility_(?:canonical_)?view_for\s*\(|\bps_ctx_intern_qual_type_in\s*\(|\bps_node_(?:get|bind)_type\s*\(|\bps_type_[A-Za-z0-9_]*\s*\(/.test(
      unitTestCompatibilityCorpus,
    )) {
  throw new Error(
    "unit tests must use canonical QualType APIs without mutable parser type compatibility paths",
  );
}

const typedHirBuildStatusSource = await readFile(
  "src/semantic/typed_hir_build_status.c",
  "utf8",
);
const arenaSource = await readFile("src/parser/arena.c", "utf8");
const selfhostBuildScript = await readFile(
  "scripts/build_wasm_selfhost_api.sh",
  "utf8",
);
if (!/PSX_RESOLVED_HIR_BUILD_INTERNAL_FAILURE/.test(
      typedHirBuildStatusHeader,
    ) ||
    !/failure->status\s*==\s*PSX_RESOLVED_HIR_BUILD_OK/.test(
      typedHirBuildStatusSource,
    ) ||
    !/failure->source_node_kind\s*<\s*0/.test(
      typedHirBuildStatusSource,
    ) ||
    !/if\s*\(!failure->source_token\)/.test(
      typedHirBuildStatusSource,
    ) ||
    !/PSX_RESOLVED_HIR_BUILD_INTERNAL_FAILURE,\s*syntax/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/diag_emit_tokf_in\s*\([\s\S]*?direct function Syntax to Typed HIR resolution failed[\s\S]*?function '%\.\*s'/.test(
      semanticTreeResolutionSource,
    ) ||
    !/allocation_failure_enabled/.test(arenaSource) ||
    !/test-wasm-selfhost-source:/.test(makefileSource) ||
    !/test-wasm-selfhost-source:[^]*?src\/semantic\/syntax_typed_hir_resolution\.c/.test(
      makefileSource,
    ) ||
    /src\/semantic\/legacy_syntax_diagnostics\.c/.test(makefileSource) ||
    !/self-host compile failed: %s/.test(selfhostBuildScript)) {
  throw new Error(
    "direct Typed HIR failures must preserve the first status, node, token, and self-host source identity",
  );
}

console.log(
  "design invariants: ok (IR/backend isolation, canonical type ownership, and record identity verified)",
);
