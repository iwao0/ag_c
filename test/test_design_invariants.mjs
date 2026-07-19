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
const resolvedTreeMaterialization = await readFile(
  "src/semantic/typed_hir_tree_materialization.c",
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
const resolvedObjectRefHeader = await readFile(
  "src/semantic/resolved_object_ref.h",
  "utf8",
);
const resolvedObjectRefSource = await readFile(
  "src/semantic/resolved_object_ref.c",
  "utf8",
);
const resolutionWorkTree = await readFile(
  "src/semantic/resolution_work_tree.c",
  "utf8",
);
const resolutionWorkTreeHeader = await readFile(
  "src/semantic/resolution_work_tree.h",
  "utf8",
);
const resolutionWorkTreeInternalHeader = await readFile(
  "src/semantic/resolution_work_tree_internal.h",
  "utf8",
);
const earlyNodeResolutionState = await readFile(
  "src/semantic/resolution_state.h",
  "utf8",
);
const earlyTypeQueryResolutionSource = await readFile(
  "src/semantic/type_query_resolution.c",
  "utf8",
);
const syntaxWorkTreeFactory = resolutionWorkTree.match(
  /psx_resolution_work_tree_t\s*\*psx_resolution_work_tree_create_from_syntax\s*\([^]*?\n\}/,
);
if (!/\bclone_type_name_ref\s*\(/.test(resolutionWorkTree) ||
    !/\bclone_type_name_syntax\s*\(/.test(resolutionWorkTree) ||
    !/\bclone_parsed_declarator\s*\(/.test(resolutionWorkTree) ||
    !syntaxWorkTreeFactory ||
    /\bps_node_copy_resolution_state_in\s*\(/.test(
      syntaxWorkTreeFactory[0],
    ) ||
    /psx_resolution_work_tree_create_from_function_seed/.test(
      `${resolutionWorkTree}\n${resolutionWorkTreeInternalHeader}`,
    ) ||
    /psx_resolution_work_tree_export_compatibility_ast/.test(
      `${resolutionWorkTree}\n${resolutionWorkTreeInternalHeader}`,
    ) ||
    /\b(?:usage_region|usage_lvar|records_lvar_usage|widen_zext_i64|is_decl_initializer|is_implicit_int_return)\b/.test(
      astHeader,
    ) ||
    !/psx_lvar_usage_resolution_state_t\s+lvar_usage\s*;/.test(
      earlyNodeResolutionState,
    ) ||
    !/psx_node_semantic_flags_t\s+flags\s*;/.test(
      earlyNodeResolutionState,
    ) ||
    /\b(?:bound_base_type|resolved_type)\s*;/.test(astHeader) ||
    !/\bpsx_type_name_resolution_state_t\s+type_name\s*;/.test(
      earlyNodeResolutionState,
    ) ||
    !/\bps_declarator_shape_copy_in\s*\(/.test(
      earlyTypeQueryResolutionSource,
    ) ||
    !/case\s+ND_COMPOUND_LITERAL:[^]*?clone_type_name_ref/.test(
      resolutionWorkTree,
    ) ||
    !/case\s+ND_CAST:[^]*?clone_type_name_ref/.test(
      resolutionWorkTree,
    ) ||
    !/case\s+ND_SIZEOF_QUERY:[^]*?clone_type_name_ref/.test(
      resolutionWorkTree,
    ) ||
    !/case\s+ND_ALIGNOF_QUERY:[^]*?clone_type_name_ref/.test(
      resolutionWorkTree,
    )) {
  throw new Error(
    "type-name syntax must remain typeless and immutable while resolution state owns semantic types",
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
const scopeGraphLocalRegistrySource = await readFile(
  "src/parser/local_registry.c",
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
const scopeGraphIdentifierResolutionSource = await readFile(
  "src/semantic/identifier_resolution.c",
  "utf8",
);
const scopeGraphTranslationUnitSource = await readFile(
  "src/frontend/translation_unit.c",
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
const tagScopeLookupBody =
  scopeGraphSemanticContextSource.match(
    /static\s+tag_type_t\s*\*find_tag_type_at_scope_in\s*\([^]*?\n}\n\nstatic\s+tag_type_t\s*\*find_tag_type_by_record_id_in/,
  )?.[0] ?? "";
if (!/typedef\s+uint32_t\s+psx_scope_id_t\s*;/.test(scopeGraphHeader) ||
    !/typedef\s+uint32_t\s+psx_decl_id_t\s*;/.test(scopeGraphHeader) ||
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
    !/ps_local_registry_bind_scope_graph\s*\(/.test(compilationSession) ||
    !/ps_global_registry_bind_scope_graph\s*\(/.test(compilationSession) ||
    !/ps_ctx_bind_scope_graph\s*\(/.test(compilationSession) ||
    !/context->scope_graph\s*=\s*psx_scope_graph_create\s*\(/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/context->owns_scope_graph\s*=\s*context->scope_graph\s*\?\s*1\s*:\s*0/.test(
      scopeGraphSemanticContextSource,
    ) ||
    /scope_graph\s*=\s*NULL/.test(scopeGraphSemanticContextSource) ||
    /psx_scope_graph_reset\s*\(/.test(scopeGraphGlobalRegistrySource) ||
    !/reset_translation_unit_state\s*\([^]*?psx_scope_graph_reset\s*\(ps_ctx_scope_graph\s*\(semantic_context\)\)/.test(
      scopeGraphTranslationUnitSource,
    ) ||
    !/ag_compilation_session_is_complete\s*\([^]*?ps_ctx_scope_graph\s*\([^]*?==\s*session->scope_graph[^]*?ps_global_registry_scope_graph\s*\([^]*?==\s*session->scope_graph[^]*?ps_local_registry_scope_graph\s*\([^]*?==\s*session->scope_graph/.test(
      compilationSession,
    ) ||
    /\b(?:gvars_by_bucket|lvars_by_bucket|tags_by_bucket|aggregate_members_by_bucket|enum_entries_by_bucket|typedef_entries_by_bucket|function_symbols_by_bucket|label_definitions_by_bucket|next_hash)\b/.test(
      scopeGraphRegistrySources,
    ) ||
    !tagScopeLookupBody ||
    !/psx_scope_graph_declaration_at\s*\(/.test(tagScopeLookupBody) ||
    /context->tags_all/.test(tagScopeLookupBody) ||
    /\b(?:current_scope_seq|next_declaration_seq|scope_parent_by_seq)\s*;/.test(
      scopeGraphLocalRegistrySource,
    ) ||
    !/psx_scope_graph_lookup\s*\(/.test(scopeGraphLocalRegistrySource) ||
    !/psx_scope_graph_lookup_in_scope\s*\(/.test(
      scopeGraphGlobalRegistrySource,
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
    !/ps_ctx_clone_tag_type_at_in_contexts\s*\([^]*?psx_scope_graph_lookup\s*\([^]*?PSX_NAMESPACE_TAG/.test(
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
    !/find_tag_member_impl_in\s*\([^]*?psx_scope_graph_lookup_in_scope\s*\([^]*?PSX_NAMESPACE_MEMBER/.test(
      scopeGraphSemanticContextSource,
    ) ||
    !/shared_identifier_scope_graph\s*\([^]*?psx_scope_graph_lookup\s*\([^]*?switch\s*\(declaration->kind\)/.test(
      scopeGraphIdentifierResolutionSource,
    ) ||
    !identifierResolverBody ||
    !/if\s*\(!shared_identifier_scope_graph\s*\(request\)\)\s*return\s*;/.test(
      identifierResolverBody,
    ) ||
    /\b(?:ps_decl_find_lvar_in|ps_local_registry_find_visible_in|ps_ctx_find_enum_const_in|ps_ctx_find_enum_const_at_in_contexts|ps_ctx_find_function_symbol_in|psx_resolve_global_object_symbol_in)\s*\(/.test(
      identifierResolverBody,
    )) {
  throw new Error(
    "CompilationSession must own one ScopeId/DeclId graph with all C namespaces",
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
const statementLegacySource = await readFile(
  "src/parser/stmt_legacy.c",
  "utf8",
);
const legacyNameClassifierSource = await readFile(
  "src/parser/name_classifier_legacy.c",
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
  await readFile("src/parser/alignas_value.c", "utf8"),
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
    !/int\s+value_size\s*=\s*ps_type_sizeof_id\s*\(/.test(
      irSymbolLoweringSourceForMemberLayout,
    ) ||
    !/ps_gvar_init_member_value\s*\(\s*ctx->global\s*,\s*slot\s*,\s*member\s*,\s*value_size\s*\)/.test(
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
if (!/struct\s+psx_semantic_context_t\s*\{/.test(semanticContextOwnershipSource) ||
    !/psx_semantic_context_t\s*\*ps_ctx_create\s*\(/.test(semanticContextOwnershipSource) ||
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
  /\bps_ctx_(?:clone_tag_type_at|register_tag_type|register_enum_const|find_enum_const_at|register_typedef_name|find_typedef_decl_type_at)_in\s*\(/;
if (splitSemanticLocalContextApis.test(semanticContextOwnershipSource)) {
  throw new Error(
    "semantic namespace operations must receive semantic and local contexts together",
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
const localInitializerBindingHeader = await readFile(
  "src/semantic/local_initializer_binding.h",
  "utf8",
);
const localInitializerBindingSource = await readFile(
  "src/semantic/local_initializer_binding.c",
  "utf8",
);
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
const sessionContextAccessorNames = [
  "semantic_context",
  "global_registry",
  "local_registry",
  "preprocessor_context",
  "arena_context",
  "diagnostic_context",
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
    !/ps_global_registry_create\s*\(/.test(compilationSessionSource) ||
    !/ps_global_registry_destroy\s*\(/.test(compilationSessionSource) ||
    /ps_global_registry_activate\s*\(/.test(compilationSessionSource) ||
    /previous_global_registry/.test(compilationSessionSource) ||
    /ps_ctx_activate\s*\(/.test(compilationSessionSource) ||
    /previous_semantic_context/.test(compilationSessionSource) ||
    !/ps_local_registry_create\s*\(\s*session->diagnostic_context\s*\)/.test(
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
    !/psx_frontend_reset_translation_unit_state_in_session\s*\(/.test(
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
    !/pp_stream_open_for_target\s*\(/.test(compilerMainSource) ||
    /diag_context_publish\s*\(/.test(compilerMainSource) ||
    /static\s+ag_compilation_session_t\s*\*wasm_adapter_session\s*;/.test(
      compilerMainSource,
    ) ||
    !/typedef\s+struct\s*\{[^]*?ag_compilation_session_t\s*\*session;[^]*?\}\s*agc_wasm_adapter_t\s*;/.test(
      compilerMainSource,
    ) ||
    !/wasm_adapter_retain_session\s*\(/.test(compilerMainSource) ||
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

if (/^static\s+.*\bfilename_table(?:_count)?\b/gm.test(
      tokenizerFilenameSource,
    ) ||
    !/ctx->filename_table_count/.test(tokenizerFilenameSource) ||
    /\btk_filename_(?:intern|lookup|reset_translation_unit)\s*\(/.test(
      `${tokenizerFilenameSource}\n${tokenizerSource}\n${preprocessSource}`,
    ) ||
    !/tk_filename_intern_ctx\s*\(\s*ctx\s*,/.test(tokenizerSource) ||
    !/tk_filename_reset_translation_unit_ctx\s*\(/.test(
      tokenizerFilenameSource,
    )) {
  throw new Error(
    "token filename interning must be owned and reset by tokenizer context",
  );
}

const tokenizerProductionContextSources = [
  tokenizerHeader,
  tokenizerSource,
  tokenizerConfigRuntimeSource,
  tokenizerAllocatorSource,
  tokenizerAllocatorHeader,
].join("\n");
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
    !/wasm32_gen_ir_module_in\s*\(ctx->ir,/.test(
      wasmBackendContextSource,
    ) ||
    !/wasm32_obj_gen_ir_module_in\s*\(ctx->obj,/.test(
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
  readFile("src/lowering/compound_literal_lowering.c", "utf8"),
  readFile("src/lowering/cast_lowering.c", "utf8"),
]);
if (!/typedef\s+struct\s+psx_lowering_context_t\s*\{/.test(
      loweringRuntimeHeader,
    ) ||
    !/ag_diagnostic_context_t\s*\*diagnostic_context\s*;/.test(
      loweringRuntimeHeader,
    ) ||
    !/ps_lowering_diagnostics\s*\(/.test(loweringRuntimeHeader) ||
    !/ps_lowering_context_create\s*\(\s*session->arena_context\s*,\s*session->diagnostic_context\s*\)/.test(
      compilationSessionSource,
    ) ||
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
    !/psx_declaration_pipeline_reset_translation_unit_state_in\s*\(\s*lowering_context\s*\)/.test(
      await readFile("src/frontend/translation_unit.c", "utf8"),
    ) ||
    !/\.lowering_context\s*=\s*lowering_context/.test(
      await readFile("src/lowering/semantic_lowering_pass.c", "utf8"),
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
    !/ps_parser_runtime_context_reset_translation_unit\s*\(runtime_context\)/.test(
      await readFile("src/frontend/translation_unit.c", "utf8"),
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
    !/tk_set_cursor_hook_ctx\s*\(s->tk_ctx/.test(preprocessSource) ||
    !/tk_set_ensure_lookahead_hook_ctx\s*\(s->tk_ctx/.test(
      preprocessSource,
    ) ||
    !/tk_set_tolerate_untokenizable_ctx\s*\(s->tk_ctx/.test(
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
    !/pp_stream_open_for_target\s*\(ag_preprocessor_context_t\s*\*context/.test(
      preprocessSource,
    ) ||
    !/preprocess_for_target_ctx\s*\(ag_preprocessor_context_t\s*\*context/.test(
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
    !/pp_context_create\s*\(\s*session->diagnostic_context\s*\)/.test(
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
if (!/struct\s+psx_global_registry_t\s*\{/.test(globalRegistrySource) ||
    !/psx_global_registry_t\s*\*ps_global_registry_create\s*\(/.test(
      globalRegistrySource,
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
    !/void\s+ps_local_registry_destroy\s*\(/.test(
      localRegistrySource,
    ) ||
    !/ps_local_registry_find_visible_in\s*\(/.test(localRegistrySource) ||
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
if (!/ps_ctx_register_tag_type_in_contexts\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_ctx_clone_tag_type_at_in_contexts\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_ctx_register_enum_const_in_contexts\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_ctx_find_enum_const_at_in_contexts\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_ctx_register_typedef_name_in_contexts\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_ctx_find_typedef_decl_type_at_in_contexts\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/context->scope_graph\s*!=\s*ps_local_registry_scope_graph\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/psx_scope_graph_lookup\s*\(/.test(
      semanticContextOwnershipSource,
    )) {
  throw new Error(
    "semantic namespaces must receive local scope ownership explicitly",
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
const frontendTranslationUnitResolverHeader = await readFile(
  "src/frontend/translation_unit_resolver.h",
  "utf8",
);
const parserCompatibilityTestHook = await readFile(
  "test/support/parser_compatibility_test_hook.c",
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
    !/psx_resolve_parsed_function_compatibility_for_test_in_contexts\s*\(/.test(
      parserCompatibilityTestHook,
    ) ||
    !/resolution\.compatibility_root/.test(
      parserCompatibilityTestHook,
    ) ||
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
const parserLegacyHeader = await readFile(
  "src/parser/parser_legacy.h",
  "utf8",
);
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
    /\bps_ctx_type_(?:size|align)of_in\s*\(/.test(
      typedHirDiagnosticsSource,
    ) ||
    !/\bps_type_sizeof_id\s*\(/.test(
      typedHirDiagnosticsSource,
    ) ||
    !/unsigned\s+char\s+is_source_assignment\s*;/.test(
      typedWarningHirInternalHeader,
    ) ||
    !/unsigned\s+char\s+is_declaration_initializer\s*;/.test(
      typedWarningHirInternalHeader,
    ) ||
    !/\.is_source_assignment\s*=\s*syntax->is_source_assignment/.test(
      typedWarningSyntaxTypedHirResolutionSource,
    ) ||
    !/\.is_declaration_initializer\s*=\s*1/.test(
      typedWarningSyntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "Typed HIR warnings must consume semantic provenance without depending on Syntax AST kinds",
  );
}
const legacySyntaxDiagnosticsSource = await readFile(
  "src/semantic/legacy_syntax_diagnostics.c",
  "utf8",
);
const semanticTreeResolutionTestSupportHeader = await readFile(
  "src/semantic/semantic_tree_resolution_test_support.h",
  "utf8",
);
const declarationRegistrationBoundarySource = await readFile(
  "src/semantic/declaration_registration.c",
  "utf8",
);
const mutableCompatibilityImplementationFiles = new Set([
  "src/semantic/identifier_binding.c",
  "src/semantic/local_declaration_tree_resolution.c",
  "src/semantic/semantic_pass.c",
  "src/lowering/semantic_lowering_pass.c",
  "src/semantic/legacy_syntax_diagnostics.c",
]);
const mutableCompatibilityCallers = [];
const mutableCompatibilityCallPattern =
  /\bpsx_(?:bind_identifier_(?:tree|initializer_tree)|semantic_resolve_(?:tree|initializer_tree)|lower_semantic_(?:tree|initializer_syntax)|resolve_local_declaration_syntax_tree)_in_contexts\s*\(/;
for (const file of allSourceFiles) {
  if (mutableCompatibilityImplementationFiles.has(file) ||
      !file.endsWith(".c"))
    continue;
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
    "mutable compatibility resolution must remain diagnostic-only; callers: " +
      (mutableCompatibilityCallers.join(", ") || "none"),
  );
}
if (/node_t\s*\*psx_frontend_/.test(semanticPipelineHeader) ||
    !/psx_frontend_resolve_parsed_function_to_hir_in_session/.test(
      semanticPipelineHeader,
    ) ||
    !/psx_resolve_parsed_function_typed_hir_from_syntax_in_contexts/.test(
      semanticPipelineSource,
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
    "frontend semantic pipeline APIs must consume Typed HIR without exposing work-tree state",
  );
}
const semanticPassSource = await readFile(
  "src/semantic/semantic_pass.c",
  "utf8",
);
const loweredTreeValidationSource = await readFile(
  "src/semantic/lowered_tree_validation.c",
  "utf8",
);
const assignmentValidationSource = await readFile(
  "src/semantic/assignment_validation.c",
  "utf8",
);
const assignmentResolutionHeader = await readFile(
  "src/semantic/assignment_resolution.h",
  "utf8",
);
const assignmentResolutionSource = await readFile(
  "src/semantic/assignment_resolution.c",
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
const identifierBindingSource = await readFile(
  "src/semantic/identifier_binding.c",
  "utf8",
);
const identifierBindingHeader = await readFile(
  "src/semantic/identifier_binding.h",
  "utf8",
);
if (/\bpsx_bind_identifier_(?:tree|initializer_tree)\s*\(/.test(
      `${identifierBindingHeader}\n${identifierBindingSource}`,
    )) {
  throw new Error(
    "identifier binding must receive CompilationSession or explicit registries",
  );
}
const lvarUsageAnalysisSource = await readFile(
  "src/semantic/lvar_usage_analysis.c",
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
const localDeclarationLegacySource = await readFile(
  "src/frontend/local_declaration_legacy.c",
  "utf8",
);
const localDeclarationTreeResolutionSource = await readFile(
  "src/semantic/local_declaration_tree_resolution.c",
  "utf8",
);
if (/#include\s+"\.\.\/frontend\//.test(
      `${semanticTreeResolutionSource}\n${legacySyntaxDiagnosticsSource}\n${localDeclarationTreeResolutionSource}`,
    ) ||
    /\bpsx_(?:apply|resolve)_local_declaration_syntax/.test(
      localDeclarationFrontendSource,
    ) ||
    !/\bpsx_apply_local_declaration_syntax_in_contexts\s*\(/.test(
      localDeclarationTreeResolutionSource,
    ) ||
    !/\bpsx_resolve_local_declaration_syntax_tree_in_contexts\s*\(/.test(
      localDeclarationTreeResolutionSource,
    )) {
  throw new Error(
    "local declaration syntax adapters must stay in frontend while semantic tree resolution owns declaration application",
  );
}
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
const parameterLoweringSource = await readFile(
  "src/lowering/parameter_lowering.c",
  "utf8",
);
const vlaLoweringSource = await readFile(
  "src/lowering/vla_lowering.c",
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
if (/psx_semantic_resolve_(?:initializer_)?tree_in_context\s*\(/.test(
      semanticPassSource,
    ) ||
    /psx_bind_identifier_(?:initializer_)?tree_in\s*\(/.test(
      identifierBindingSource,
    )) {
  throw new Error(
    "semantic traversal APIs must receive one complete registry set",
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
    !/\bps_expr_in_contexts\s*\(/.test(parserLegacyHeader) ||
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
    ) ||
    !/psx_bind_identifier_tree_in_contexts\s*\(/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    !/\.local_registry\s*=\s*local_registry/.test(
      identifierBindingSource,
    ) ||
    !/psx_analyze_function_lvar_usage_in\s*\(/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    !/psx_lower_semantic_tree_in_contexts\s*\(/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    !/ps_decl_replay_lvar_usage_events_in\s*\(/.test(
      lvarUsageAnalysisSource,
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
      identifierBindingSource,
    ) ||
    !/ag_compilation_session_is_complete\s*\(session\)/.test(
      translationUnitDataLoweringSource,
    ) ||
    /!session\s*\|\|\s*!session->/.test(
      `${semanticPipelineSource}\n${identifierBindingSource}`,
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
  identifierBindingSource,
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
    !/psx_apply_parsed_decl_specifier_in_contexts\s*\(/.test(
      toplevelDeclarationFrontendSource,
    ) ||
    !/psx_apply_parsed_declarator_type_in_contexts\s*\(/.test(
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
    !/psx_local_declaration_application_context_t/.test(
      localDeclarationTreeResolutionSource,
    ) ||
    !/psx_apply_parsed_decl_specifier_in_contexts\s*\(/.test(
      localDeclarationTreeResolutionSource,
    ) ||
    !/psx_apply_parsed_standalone_tag_in_contexts\s*\(/.test(
      localDeclarationTreeResolutionSource,
    ) ||
    /\bps_local_registry_create_storage_object\s*\(/.test(
      explicitLocalDeclarationLowering,
    ) ||
    /\bps_local_registry_create_static_alias\s*\(/.test(
      staticLocalLoweringSource,
    ) ||
    /\bps_register_global_var\s*\(/.test(staticLocalLoweringSource) ||
    /\bps_decl_find_lvar\s*\(/.test(vlaLoweringSource) ||
    !/ps_local_registry_create_storage_object_in\s*\(/.test(
      localObjectLoweringSource,
    ) ||
    !/ps_local_registry_create_storage_object_in\s*\(/.test(
      parameterLoweringSource,
    ) ||
    !/ps_local_registry_create_storage_object_in\s*\(/.test(
      vlaLoweringSource,
    ) ||
    !/ps_local_registry_create_static_alias_in\s*\(/.test(
      staticLocalLoweringSource,
    ) ||
    !/ps_register_global_var_in\s*\(/.test(staticLocalLoweringSource)) {
  throw new Error(
    "local declaration syntax must receive only runtime, NameClassifier, and syntax services while semantic application and lowering use frontend-owned registries",
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
if (!/psx_materialize_static_aggregate_initializer_plan\s*\(/.test(
      staticAggregateFrontendBoundary,
    ) ||
    /psx_resolution_work_tree_export_compatibility_ast\s*\(/.test(
      staticAggregateFrontendBoundary,
    ) ||
    !/const\s+psx_typed_hir_tree_t\s*\*typed_tree/.test(
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
if (!/case\s+PSX_DECL_FUNCTION:[^]*?resolution->function\s*=\s*declaration->payload/.test(
      identifierResolutionSource,
    ) ||
    /ps_ctx_find_function_symbol\s*\(/.test(identifierResolutionSource) ||
    !/ps_ctx_register_function_type_in\s*\(/.test(
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
    !/\bps_ctx_intern_qual_type_in\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    /\bps_type_(?:size|align)of_id_for_target\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bps_type_sizeof_id\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bps_type_alignof_id\s*\(/.test(
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
    !/ps_ctx_register_tag_members_in\s*\([^;]*const\s+psx_record_member_decl_t\s*\*\s*declarations\s*,[^;]*const\s+psx_record_member_layout_t\s*\*\s*layouts/s.test(
      aggregateRegistryHeader,
    ) ||
    /\bps_type_(?:size|align)of_for_target\s*\(/.test(
      aggregateMemberResolutionSource,
    )) {
  throw new Error(
    "aggregate member resolution must separate RecordDecl completeness from TypeId target layout",
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
  "src/semantic/type_query_resolution.c",
  "src/semantic/generic_selection_resolution.c",
]) {
  const source = await readFile(sourcePath, "utf8");
  if (splitSemanticTypeResolutionApi.test(source)) {
    throw new Error(
      `${sourcePath} must not combine one context with active registries`,
    );
  }
}
const splitSemanticLoweringApi =
  /(?:psx_lower_semantic_(?:tree|initializer_syntax)|lower_compound_literal_expression)_in\s*\(/;
for (const sourcePath of [
  "src/lowering/semantic_lowering_pass.c",
  "src/lowering/compound_literal_lowering.c",
]) {
  const source = await readFile(sourcePath, "utf8");
  if (splitSemanticLoweringApi.test(source)) {
    throw new Error(
      `${sourcePath} must not combine a local registry with active contexts`,
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
  localDeclarationTreeResolutionSource,
  await readFile("src/semantic/function_definition_resolution.c", "utf8"),
].join("\n");
const contextFreeTagRegistryCall =
  /\bps_ctx_(?:has_tag_type|register_tag_type|get_tag_size|get_tag_align|ensure_tag_record_decl|get_tag_member_count|register_tag_members|find_tag_member_info)\s*\(/;
if (contextFreeTagRegistryCall.test(tagDeclarationResolutionSource) ||
    contextFreeTagRegistryCall.test(aggregateMemberResolutionSource) ||
    !/psx_apply_parsed_tag_declaration_in_contexts\s*\(/.test(
      declarationApplicationSource,
    ) ||
    !/psx_apply_parsed_decl_specifier_in_contexts\s*\(/.test(
      frontendDeclarationSources,
    ) ||
    !/psx_apply_parsed_standalone_tag_in_contexts\s*\(/.test(
      frontendDeclarationSources,
    )) {
  throw new Error(
    "tag registration and layout resolution must use the passed semantic context",
  );
}
const memberAccessResolutionSource = await readFile(
  "src/semantic/member_access_resolution.c",
  "utf8",
);
const memberAccessResolutionHeader = await readFile(
  "src/semantic/member_access_resolution.h",
  "utf8",
);
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
const memberAccessStateHeader = await readFile(
  "src/semantic/resolution_state.h",
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
      `${memberAccessResolutionSource}\n${memberResolutionSource}`,
    ) ||
    /\baggregate_definition\b/.test(memberResolutionSource) ||
    /\bps_type_find_aggregate_member\s*\(/.test(
      memberResolutionSource,
    ) ||
    !/\bps_ctx_get_record_decl_in\s*\(/.test(
      memberResolutionSource,
    ) ||
    !/resolution->member_index\s*=\s*aggregate_member_index/.test(
      memberResolutionSource,
    ) ||
    !/resolution->record_id\s*=/.test(memberResolutionSource) ||
    !/\bpsx_resolve_member_access_qual_type_in\s*\(/.test(
      memberAccessResolutionSource,
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
    !/\bpsx_record_member_decl_t\s+declaration\s*;/.test(
      memberAccessStateHeader,
    ) ||
    !/\bpsx_member_access_state_t\s+member_access\s*;/.test(
      memberAccessStateHeader,
    ) ||
    /resolution->declaration\.(?:offset|bit_offset)\b/.test(
      memberResolutionSource,
    ) ||
    !/\bpsx_record_layout_table_lookup\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    !/\bpsx_record_layout_member\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    /member->(?:offset|bit_offset|bit_width)\s*=/.test(
      resolvedTreeMaterialization,
    ) ||
    /\btag_member_info_t\b/.test(
      resolvedTreeMaterialization,
    ) ||
    !/spec->member_offset\s*=\s*member->offset/.test(
      resolvedTreeMaterialization,
    ) ||
    !/spec->bit_offset\s*=\s*\(unsigned char\)member->bit_offset/.test(
      resolvedTreeMaterialization,
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
    /\bpsx_semantic_type_table_lookup\s*\(/.test(
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
    ) ||
    !/resolution\.member_qual_type/.test(
      semanticPassSource,
    ) ||
    /resolution\.base_object_qual_type\.qualifiers/.test(
      semanticPassSource,
    ) ||
    !/\bpsx_resolve_member_access_qual_type_in\s*\(/.test(
      memberAccessResolutionSource.match(
        /void\s+psx_resolve_member_access\s*\([^]*?\n\}/,
      )?.[0] || "",
    ) ||
    /ps_type_has_qualifier\s*\(\s*object_type/.test(
      semanticPassSource,
    )) {
  throw new Error(
    "member access owner qualifiers must be resolved through TypeId QualType relations",
  );
}
const semanticQualifierDiagnosticSection = memberNodeUtilsSource.match(
  /static\s+psx_qual_type_t\s+node_semantic_qual_type\s*\([^]*?void\s+ps_node_expect_lvalue_at_in\s*\(/,
);
const pointeeQualTypeRelation = memberTypeIdentitySource.match(
  /psx_qual_type_t\s+psx_semantic_type_table_pointee_value\s*\([^]*?\n\}/,
);
if (!semanticQualifierDiagnosticSection ||
    !/\bps_node_qual_type\s*\(/.test(
      semanticQualifierDiagnosticSection[0],
    ) ||
    !/\bps_ctx_intern_qual_type_in\s*\(/.test(
      semanticQualifierDiagnosticSection[0],
    ) ||
    !/\bpsx_semantic_type_table_pointee_value\s*\(/.test(
      semanticQualifierDiagnosticSection[0],
    ) ||
    !pointeeQualTypeRelation ||
    !/semantic_type_table_array_leaf_from\s*\(\s*table\s*,\s*base\s*\)/.test(
      pointeeQualTypeRelation[0],
    ) ||
    /psx_semantic_type_table_array_leaf\s*\(\s*table\s*,\s*base\.type_id\s*\)/.test(
      pointeeQualTypeRelation[0],
    ) ||
    /\bnode_(?:self|pointee)_is_(?:const|volatile)_qualified\s*\(/.test(
      semanticQualifierDiagnosticSection[0],
    ) ||
    !/ps_node_reject_const_assign_at_in\s*\([^;]*psx_semantic_context_t\s*\*/s.test(
      memberNodeUtilsHeader,
    ) ||
    !/ps_node_reject_const_qual_discard_at_in\s*\([^;]*psx_semantic_context_t\s*\*/s.test(
      memberNodeUtilsHeader,
    ) ||
    !/ps_node_reject_const_assign_at_in\s*\(\s*semantic_context\s*,/.test(
      assignmentValidationSource,
    ) ||
    !/ps_node_reject_const_qual_discard_at_in\s*\(\s*semantic_context\s*,/.test(
      assignmentValidationSource,
    )) {
  throw new Error(
    "semantic qualifier diagnostics must read self and pointee qualifiers through QualType relations",
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
const typeQueryResolutionSource = await readFile(
  "src/semantic/type_query_resolution.c",
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
if (/\bps_ctx_type_(?:size|align)of_in\s*\(/.test(
      typeQueryResolutionSource,
    ) ||
    !/\bps_type_sizeof_id\s*\(/.test(
      typeQueryResolutionSource,
    ) ||
    !/\bps_type_alignof_id\s*\(/.test(
      typeQueryResolutionSource,
    ) ||
    /\bps_type_(?:size|align)of_for_target\s*\(/.test(
      typeQueryResolutionSource,
    )) {
  throw new Error(
    "sizeof and alignof resolution must intern semantic types and query target layout by TypeId",
  );
}
if (/\bnode_t\b|\bnode_[A-Za-z0-9_]+_t\b|\bps_node_|\bND_[A-Z0-9_]+\b|parser\/ast\.h/.test(
      `${typeQuerySemanticsHeader}\n${typeQuerySemanticsSource}`,
    ) ||
    /\bps_ctx_type_(?:size|align)of_in\s*\(/.test(
      typeQuerySemanticsSource,
    ) ||
    !/\bps_type_sizeof_id\s*\(/.test(
      typeQuerySemanticsSource,
    ) ||
    !/\bps_type_alignof_id\s*\(/.test(
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
if (/\bnode_t\b|\bnode_[A-Za-z0-9_]+_t\b|\bps_node_|\bND_[A-Z0-9_]+\b|parser\/ast\.h|resolution_state/.test(
      `${compoundLiteralSemanticsHeader}\n${compoundLiteralSemanticsSource}`,
    ) ||
    !/psx_compound_literal_plan_t/.test(
      compoundLiteralSemanticsHeader,
    ) ||
    !/psx_resolve_compound_literal_qual_type_plan_in\s*\(/.test(
      compoundLiteralSemanticsSource,
    ) ||
    !/PSX_COMPOUND_LITERAL_STORAGE_AUTOMATIC/.test(
      compoundLiteralSemanticsHeader,
    ) ||
    !/PSX_COMPOUND_LITERAL_STORAGE_STATIC/.test(
      compoundLiteralSemanticsHeader,
    )) {
  throw new Error(
    "compound literal semantics must resolve storage duration and canonical result QualType without Syntax AST dependencies",
  );
}
const declarationResolutionSource = await readFile(
  "src/semantic/declaration_resolution.c",
  "utf8",
);
if (!/\bps_type_character_code_unit_width\s*\(/.test(
      declarationResolutionSource,
    ) ||
    /\bps_type_sizeof\s*\(/.test(declarationResolutionSource) ||
    /->\s*aggregate_definition\b/.test(declarationResolutionSource) ||
    /\bmember->offset\b/.test(declarationResolutionSource) ||
    !/\bps_ctx_resolve_tag_record_id_in\s*\(/.test(
      declarationResolutionSource,
    ) ||
    !/\bps_type_new_record_in\s*\(/.test(
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
const frontendSemanticPipelineSource = await readFile(
  "src/frontend/semantic_pipeline.c",
  "utf8",
);
const semanticTraversalCallers = [
  frontendSemanticPipelineSource,
  semanticTreeResolutionSource,
  legacySyntaxDiagnosticsSource,
  declarationApplicationSource,
  await readFile("src/semantic/declaration_registration.c", "utf8"),
].join("\n");
const contextFreeSemanticTraversalCall =
  /\bpsx_semantic_resolve_(?:tree|initializer_tree)\s*\(/;
if (!/psx_semantic_resolve_tree_in_contexts\s*\(/.test(
      semanticPassSource,
    ) ||
    !/psx_semantic_resolve_initializer_tree_in_contexts\s*\(/.test(
      semanticPassSource,
    ) ||
    !/\.semantic_context\s*=\s*semantic_context/.test(
      semanticPassSource,
    ) ||
    !/\.local_registry\s*=\s*local_registry/.test(
      semanticPassSource,
    ) ||
    contextFreeSemanticTraversalCall.test(semanticTraversalCallers) ||
    !/psx_semantic_resolve_tree_in_contexts\s*\(/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    !/ps_ctx_find_typedef_(?:decl_type|name)_at_in_contexts\s*\(/.test(
      typeNameResolutionSource,
    ) ||
    !/ps_ctx_clone_tag_type_at_in_contexts\s*\(/.test(
      typeNameResolutionSource,
    ) ||
    !/psx_resolve_bound_type_name_ref_in_contexts\s*\(/.test(
      genericSelectionResolutionSource,
    ) ||
    !/psx_bind_type_name_ref_in_contexts\s*\(/.test(
      typeQueryResolutionSource,
    ) ||
    !/ps_ctx_bind_record_ids_in\s*\(/.test(
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
const typedefDeclarationResolutionSource = await readFile(
  "src/semantic/typedef_declaration_resolution.c",
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
  /\b(?:psx_resolve_global_object_symbol_in|ps_ctx_has_function_name_in|ps_ctx_has_typedef_in_current_scope_in|ps_ctx_has_enum_const_in_current_scope_in)\s*\(/;
if (obsoleteOrdinaryNameApi.test([
      identifierResolutionHeader,
      identifierResolutionSource,
      semanticContextOwnershipSource,
      ordinarySemanticContextHeaderSource,
      functionPublicHeaderSource,
      ordinaryNodeUtilsSource,
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
if (/\bps_(?:ctx_active|global_registry_active|local_registry_active)\s*\(/.test(
      declarationRegistrationSource,
    )) {
  throw new Error(
    "declaration registration must receive all required registries explicitly",
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
  [identifierResolutionSource, ["semantic_context", "global_registry", "local_registry"]],
  [enumConstantResolutionSource, ["semantic_context", "global_registry", "local_registry"]],
  [typedefDeclarationResolutionSource, ["semantic_context", "global_registry", "local_registry"]],
  [functionDeclarationResolutionSource, ["semantic_context", "global_registry"]],
  [globalDeclarationResolutionSource, ["semantic_context", "global_registry"]],
  [tagDeclarationResolutionSource, ["semantic_context", "local_registry"]],
  [declarationResolutionSource, ["semantic_context"]],
  [aggregateMemberResolutionSource, ["semantic_context"]],
  [memberAccessResolutionSource, ["semantic_context"]],
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
    !/ps_ctx_register_enum_const_in_contexts\s*\(/.test(
      enumConstantResolutionSource,
    ) ||
    !/ps_ctx_register_typedef_name_in_contexts\s*\(/.test(
      typedefDeclarationResolutionSource,
    ) ||
    !/psx_scope_graph_lookup\s*\([^]*?PSX_NAMESPACE_ORDINARY/.test(
      identifierResolutionSource,
    ) ||
    !/ps_ctx_enum_const_value_by_declaration_id_in\s*\(/.test(
      identifierResolutionSource,
    ) ||
    !/psx_apply_parsed_typedef_declaration_in_contexts\s*\(/.test(
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
const semanticLoweringPassSource = await readFile(
  "src/lowering/semantic_lowering_pass.c",
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
    !/ps_register_global_var_in\s*\(/.test(
      globalObjectLoweringSource,
    ) ||
    /\bps_register_global_var\s*\(/.test(globalObjectLoweringSource) ||
    /\bps_global_registry_active\s*\(/.test(globalObjectLoweringSource) ||
    !/!request->semantic_context\b/.test(globalObjectLoweringSource) ||
    !/!request->global_registry\b/.test(globalObjectLoweringSource) ||
    !/psx_lower_semantic_tree_in_contexts\s*\(/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    !/psx_lower_semantic_initializer_syntax_in_contexts\s*\(/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    !/psx_plan_compound_literal_storage_in_contexts\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
    !/\.global_registry\s*=\s*global_registry/.test(
      semanticLoweringPassSource,
    )) {
  throw new Error(
    "global declarations must classify names through the scope graph and lowering must use explicit registries",
  );
}
const parserSource = await readFile("src/parser/parser.c", "utf8");
const parserLegacySource = await readFile(
  "src/parser/parser_legacy.c",
  "utf8",
);
const statementParserSource = await readFile("src/parser/stmt.c", "utf8");
const statementControlFlowValidationSource = await readFile(
  "src/semantic/control_flow_validation.c",
  "utf8",
);
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
    !/psx_initializer_syntax_context_t/.test(initializerSyntaxHeader) ||
    !/parse_assignment_expression/.test(initializerSyntaxHeader)) {
  throw new Error(
    "initializer syntax parsing must depend only on parser runtime and explicit syntax-expression services",
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
const alignasValueSource = await readFile(
  "src/parser/alignas_value.c",
  "utf8",
);
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
  semanticPassSource,
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
  typeQueryResolutionSource,
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
    !/ps_ctx_find_enum_const_in\s*\(/.test(enumConstSource) ||
    !/psx_frontend_reset_translation_unit_state_in_session\s*\(/.test(
      compilerMainSource,
    ) ||
    !/ps_ctx_reset_function_scope_in\s*\(/.test(
      frontendFunctionDefinitionSource,
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
    !/psx_apply_parsed_declarator_in_contexts\s*\(/.test(
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
    !/\bir_module_find_symbol\s*\(/.test(wasmFunctionCodegen) ||
    !/\bir_symbol_find_func_ref\s*\(/.test(wasmFunctionCodegen)) {
  throw new Error(
    "Wasm function codegen must consume resolved IR symbols instead of parser registries" +
      (wasmFunctionCodegenViolations.length
        ? `:\n${wasmFunctionCodegenViolations.sort().join("\n")}`
        : ""),
  );
}

const wasmObjSource = await readFile("src/arch/wasm32/wasm32_obj.c", "utf8");
if (/^static\s+obj_ctx_t\s+g_obj\s*;/m.test(wasmObjSource) ||
    /^static\s+wb_t\s+g_obj_capture\s*;/m.test(wasmObjSource) ||
    /\b_Thread_local\b/.test(wasmObjSource) ||
    /wasm32_obj_context_(?:activate|active)\s*\(/.test(wasmObjSource) ||
    /^static\s+(?:ir_type_t|unsigned char|int)\s*\*?g_emit_local_/m.test(
      wasmObjSource,
    ) ||
    !/struct\s+wasm32_obj_context_t\s*\{/.test(wasmObjSource) ||
    !/ag_diagnostic_context_t\s*\*diagnostic_context\s*;/.test(
      wasmObjSource,
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
    !/\bdata_for_machine_inst\s*\(\s*context\s*,\s*module\s*,/.test(
      wasmObjFunctionCodegen,
    ) ||
    !/static obj_data_t \*data_for_machine_inst[\s\S]*?\bir_module_find_symbol\s*\(/.test(
      wasmObjSource,
    )) {
  throw new Error(
    "Wasm object function codegen must consume resolved IR symbols instead of parser registries" +
      (wasmObjFunctionCodegenViolations.length
        ? `:\n${wasmObjFunctionCodegenViolations.sort().join("\n")}`
        : ""),
  );
}

const irHeaderSource = await readFile("src/ir/ir.h", "utf8");
const irDataHeaderSource = await readFile("src/ir/ir_data.h", "utf8");
const resolvedGlobalAstSource = await readFile(
  "src/semantic/resolved_node.h",
  "utf8",
);
const constantExpressionSource = await readFile(
  "src/semantic/constant_expression.c",
  "utf8",
);
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
const wasmMachineFunctionPlanSource = await readFile(
  "src/arch/wasm32/wasm32_machine_function.c",
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
if (/\bnode_gvar_t\b/.test(resolvedGlobalAstSource) ||
    /\bps_find_global_var\s*\(/.test(constantExpressionSource) ||
    /\bps_find_global_var\s*\(/.test(irSymbolLoweringSource) ||
    !/psx_resolved_object_ref_global\s*\(/.test(
      resolvedTreeMaterialization,
    )) {
  throw new Error(
    "resolved global references must retain symbol identity in resolution state without active-registry lookup or specialized node payloads",
  );
}
if (/\b(?:semantic_context|ps_ctx_|ps_gvar_symbol_ref_named_function_in)\b/.test(
      irSymbolLoweringSource,
    ) ||
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
    !/\bps_type_sizeof_id\s*\(/.test(abiLoweringSource)) {
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
if (/\bx8\b|\bret_type\b/.test(irHeaderSource) ||
    /\bIR_(?:PARAM|RESULT_AREA)\b/.test(irHeaderSource) ||
    /\bresult_area_vreg\b/.test(abiLoweringHeader) ||
    !/\bir_abi_reference_signature\s*\(/.test(
      wasmMachineFunctionPlanSource,
    ) ||
    !/\bir_abi_data_relocation_signature\s*\(/.test(wasmIrSource) ||
    !/\bir_abi_data_relocation_signature\s*\(/.test(wasmObjSource)) {
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
    !/\bag_target_info_call_abi\s*\(/.test(abiLoweringSource) ||
    /\[[ \t]*(?:16|32)[ \t]*\]/.test(
      `${irHeaderSource}\n${abiLoweringHeader}\n${abiLoweringSource}`,
    )) {
  throw new Error(
    "AbiLowering must own dynamic target-specific parameter and result piece sequences without legacy result flags or fixed callable caps",
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
if (!/ir_opt_const_fold\s*\(m\)[\s\S]*?ir_opt_dce\s*\(m\)[\s\S]*?lower_module_abi\s*\(m/.test(
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
const resolvedNodeKindHeader = await readFile(
  "src/semantic/resolved_node_kind.h",
  "utf8",
);
const resolvedNodeKindImplementation = await readFile(
  "src/semantic/resolved_node_kind.c",
  "utf8",
);
const resolvedNodeHeader = await readFile(
  "src/semantic/resolved_node.h",
  "utf8",
);
const resolvedFunctionHeader = await readFile(
  "src/semantic/resolved_function.h",
  "utf8",
);
const resolvedFunctionSource = await readFile(
  "src/semantic/resolved_function.c",
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
const caseLabelResolutionHeader = await readFile(
  "src/semantic/case_label_resolution.h",
  "utf8",
);
const caseLabelResolutionSource = await readFile(
  "src/semantic/case_label_resolution.c",
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
const legacySemanticLabelApi =
  /\b(?:psx_ctx_register_goto_ref_in|psx_ctx_register_label_def_in|psx_ctx_validate_goto_refs_in)\s*\(/;
if (!/psx_scope_graph_declare_at\s*\([^]*?PSX_NAMESPACE_LABEL[^]*?PSX_DECL_LABEL/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_scope_graph_lookup_in_scope\s*\([^]*?PSX_NAMESPACE_LABEL/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/forget_direct_label_declarations\s*\(/.test(
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
    !/psx_resolve_parsed_function_typed_hir_from_syntax_in_contexts\s*\([^]*?diagnose_direct_syntax_rejection\s*\(/.test(
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
const nodeTypePublicSource = await readFile(
  "src/semantic/resolved_node_type.h",
  "utf8",
);
const resolvedNodeTypeSource = await readFile(
  "src/semantic/resolved_node_type.c",
  "utf8",
);
const resolutionStoreSource = await readFile(
  "src/semantic/resolution_store.c",
  "utf8",
);
const nodeResolutionStateSource = await readFile(
  "src/semantic/resolution_state.h",
  "utf8",
);
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
    /(?:type_system\/type_ids|parser\/type|["<]type\.h[">])/.test(
      astSource,
    ) ||
    /\b(?:unsigned_override|has_unsigned_override)\b/.test(nodeStruct[1])) {
  throw new Error(
    "syntax node_t must be typeless and must not own semantic resolution state",
  );
}
if (!/\bpsx_resolved_node_kind_t\s+node_kind\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bpsx_resolution_node_kind\s*\(/.test(
      resolvedNodeKindHeader,
    ) ||
    !/\bpsx_resolution_node_set_kind\s*\(/.test(
      resolvedNodeKindHeader,
    ) ||
    !/state->node_kind\s*=\s*kind/.test(
      resolvedNodeKindImplementation,
    ) ||
    !/node->kind\s*=\s*PSX_SYNTAX_NODE_INVALID/.test(
      resolvedNodeKindImplementation,
    ) ||
    !/psx_resolution_node_set_kind\s*\(\s*ps_ctx_resolution_store\s*\(\s*semantic_context\s*\),\s*&node->base,\s*ND_FUNCDEF\s*\)/.test(
      frontendFunctionDefinitionSource,
    ) ||
    /node->base\.kind\s*=\s*ND_FUNCDEF/.test(
      frontendFunctionDefinitionSource,
    ) ||
    !/psx_syntax_node_kind_is_valid\s*\(\s*source->kind\s*\)[^]*?ps_node_has_resolution_state\s*\(\s*resolution_store,\s*source\s*\)/.test(
      resolutionWorkTree,
    ) ||
    /\bND_(?:FUNCDEF|LVAR|FUNCREF|DEREF|GVAR|VLA_ALLOC|FP_TO_INT|INT_TO_FP|VA_ARG_AREA)\b/.test(
      resolutionWorkTree,
    )) {
  throw new Error(
    "Syntax node kinds must stay in Syntax AST while resolver-created kinds live in semantic sidecars",
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
if (!/\bpsx_resolution_node_alloc_in\s*\(/.test(
      resolvedNodeTypeSource,
    ) ||
    !/buckets\s*\[PSX_RESOLUTION_STORE_BUCKET_COUNT\]/.test(
      resolutionStoreSource,
    ) ||
    !/arena_register_cleanup_in\s*\([^]*?remove_resolution_binding/.test(
      resolutionStoreSource,
    ) ||
    !/psx_resolution_store_t\s*\*resolution_store\s*;/.test(
      compilationSessionInternalHeader,
    ) ||
    !/ps_ctx_bind_resolution_store\s*\(\s*session->semantic_context\s*,\s*session->resolution_store\s*\)/.test(
      compilationSessionSource,
    ) ||
    !/psx_resolution_store_semantic_types\s*\(\s*session->resolution_store\s*\)\s*==\s*ps_ctx_semantic_type_table_in\s*\(\s*session->semantic_context\s*\)/.test(
      compilationSessionSource,
    ) ||
    /PSX_RESOLUTION_NODE_PREFIX_MAGIC|is_resolution_work_node|has_external_resolution_state/.test(
      resolvedNodeTypeSource,
    ) ||
    !/\bps_node_resolution_state_const\s*\(/.test(
      resolvedNodeTypeSource,
    ) ||
    !/size_t\s+size\s*=\s*node_storage_size\s*\(\s*source\s*\)[^]*?psx_resolution_node_alloc_in\s*\(\s*resolution_store,\s*arena_context,\s*size\s*\)/.test(
      resolutionWorkTree,
    )) {
  throw new Error(
    "semantic node metadata must be owned by semantic sidecars and accessed through semantic APIs",
  );
}
const semanticMetadataAccessSource = (
  await Promise.all(
    allSourceFiles
      .filter((path) => path !== "src/semantic/resolution_state.h")
      .map((path) => readFile(path, "utf8")),
  )
).join("\n");
if (/\b(?:\.|->)resolution_state\b/.test(
      semanticMetadataAccessSource,
    )) {
  throw new Error(
    "semantic resolution state must not be stored or accessed through Syntax AST fields",
  );
}
if (/\bnode_(?:lvar|funcref|gvar|vla_alloc)_t\b/.test(astSource) ||
    /\bnode_(?:lvar|funcref|gvar)_t\b/.test(resolvedNodeHeader) ||
    !/\}\s*node_vla_alloc_t\s*;/.test(resolvedNodeHeader)) {
  throw new Error(
    "resolved references must use resolution state without specialized payload structs, and VLA work nodes must live outside Syntax AST",
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
        path !== "src/parser/node_utils.c")
      .map((path) => readFile(path, "utf8")),
  )
).join("\n");
if (!/typedef\s+enum\s*\{[^]*?\}\s*psx_syntax_node_kind_t\s*;/.test(
      syntaxNodeKindHeader,
    ) ||
    !/typedef\s+enum\s*\{[^]*?\}\s*psx_resolved_node_kind_t\s*;/.test(
      resolvedNodeKindHeader,
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
    "Syntax AST and resolver-created working node kinds must remain separate, and syntax parsing must depend only on syntax node construction",
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
    !/case\s+PSX_IDENTIFIER_BUILTIN_VA_ARG_AREA\s*:\s*return\s+materialize_builtin_va_arg_area/.test(
      identifierBindingSource,
    ) ||
    !/psx_bind_va_arg_area_reference_in\s*\(/.test(
      identifierBindingSource,
    ) ||
    !/case\s+PSX_RESOLVED_OBJECT_REF_VARARG_CURSOR\s*:\s*return\s+ND_VARARG_CURSOR/.test(
      resolvedObjectRefSource,
    )) {
  throw new Error(
    "__va_arg_area must remain identifier syntax, be classified by semantic identifier resolution, and use a sidecar only in the compatibility binder",
  );
}
if (!/psx_bind_local_reference_in\s*\(/.test(identifierBindingSource) ||
    !/psx_bind_global_reference_in\s*\(/.test(identifierBindingSource) ||
    !/psx_bind_function_reference_in\s*\(/.test(identifierBindingSource) ||
    /psx_node_new_(?:lvar|gvar|function_reference|va_arg_area_reference)_in\s*\(/.test(
      identifierBindingSource,
    ) ||
    /identifier->base\.kind\s*=/.test(identifierBindingSource) ||
    !/node->kind\s*!=\s*ND_IDENTIFIER[^]*?psx_resolved_object_ref_kind\s*\(\s*store,\s*node\s*\)/.test(
      resolvedObjectRefSource,
    )) {
  throw new Error(
    "identifier binding must preserve ND_IDENTIFIER and derive semantic reference kinds only from resolution state",
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
    !/case\s+ND_STATIC_ASSERT\s*:[^]*?psx_resolve_static_assert\s*\(/.test(
      semanticPassSource,
    ) ||
    !/MAP\s*\(\s*ND_STATIC_ASSERT\s*,\s*PSX_HIR_NOP\s*\)/.test(
      resolvedTreeMaterialization,
    ) ||
    !/case\s+PSX_HIR_NOP\s*:\s*return\s+1\s*;/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "block static assertions must remain immutable Syntax AST until resolver processing and materialize as Typed HIR no-ops",
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
    !/psx_decl_begin_lvar_usage_region_in\s*\(\s*resolver->local_registry\s*\)/.test(
      localDeclarationTreeResolutionSource,
    ) ||
    !/ps_node_set_lvar_usage_region\s*\(\s*ps_lowering_resolution_store\s*\(\s*resolver->lowering_context\s*\),\s*block->body\[i\],\s*region\s*\)/.test(
      localDeclarationTreeResolutionSource,
    ) ||
    !/psx_stmt_stmt_syntax\s*\(\s*&syntax\s*\)/.test(
      statementLegacySource,
    ) ||
    !/psx_parse_statement_expression_syntax\s*\(\s*&syntax\s*\)/.test(
      statementLegacySource,
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
    ) ||
    /ps_local_registry_capture_lookup_point_in\s*\(/.test(
      legacyNameClassifierSource,
    ) ||
    !/PSX_SCOPE_ID_INVALID/.test(
      legacyNameClassifierSource,
    ) ||
    !/psx_legacy_name_classifier_init\s*\(/.test(
      `${parserLegacySource}\n${statementLegacySource}\n${localDeclarationLegacySource}`,
    )) {
  throw new Error(
    "production syntax adapters must depend only on NameClassifier; missing legacy lookup points must remain explicitly invalid",
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
    !/\bvalidate_function_jumps\s*\(/.test(
      statementControlFlowValidationSource,
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
  /lvar_t\s*\*ps_local_registry_create_internal_storage_object_in\s*\([^]*?\n\}/,
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
    !/!definition->body/.test(frontendFunctionDefinitionSource) ||
    !/const\s+psx_parsed_function_definition_t\s*\*definition/.test(
      frontendFunctionDefinitionSource,
    ) ||
    !/node->base\.rhs\s*=\s*definition->body/.test(
      frontendFunctionDefinitionSource,
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
    /\bpsx_clone_syntax_tree_for_resolution\s*\(/.test(
      `${declaratorBoundResolutionSource}\n${resolutionWorkTreeInternalHeader}`,
    ) ||
    !/psx_resolution_work_tree_create_from_syntax\s*\([^]*?create_work_tree\s*\([^]*?clone_node\s*\(/.test(
      resolutionWorkTree,
    ) ||
    !/tree->compatibility_root\s*=\s*compatibility_root/.test(
      resolutionWorkTree,
    ) ||
    !/tree->typed_hir\s*=\s*NULL/.test(
      resolutionWorkTree,
    ) ||
    /psx_semantic_tree_create\s*\(/.test(resolutionWorkTree) ||
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
    !/ps_local_registry_create_internal_storage_object_in\s*\(/.test(
      vlaLoweringSource,
    ) ||
    !/registry->all_locals\s*=\s*var/.test(
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
if (!/case\s+ND_CASE:[^]*?psx_eval_const_int\s*\([^]*?psx_case_label_bind_value\s*\(/.test(
      semanticPassSource,
    ) ||
    /case\s+ND_CASE:[^]*?node->lhs\s*=\s*NULL/.test(
      semanticPassSource,
    ) ||
    !/case\s+ND_CASE:[^]*?!psx_case_label_is_resolved\s*\([^]*?PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS/.test(
      resolvedTreeMaterialization,
    ) ||
    !/case\s+ND_CASE:[^]*?include_common_children\s*=\s*0[^]*?source->rhs/.test(
      resolvedTreeMaterialization,
    ) ||
    !caseNodeStruct ||
    /\b(?:val|has_resolved_value|label_id)\b/.test(caseNodeStruct[1]) ||
    /\blabel_id\b/.test(astSource) ||
    !/\bpsx_case_label_resolution_state_t\s+case_label\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bpsx_case_label_bind_value\s*\(/.test(
      caseLabelResolutionHeader,
    ) ||
    !/\bps_node_resolution_state(?:_const)?\s*\(/.test(
      caseLabelResolutionSource,
    )) {
  throw new Error(
    "case label expressions must remain Syntax while resolved values live in semantic state and Typed HIR",
  );
}
if (!/\bpsx_node_type_binding_t\s+type_binding\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bPSX_NODE_TYPE_PENDING\b/.test(nodeResolutionStateSource) ||
    !/\bPSX_NODE_TYPE_CANONICAL\b/.test(nodeResolutionStateSource) ||
    !/\bconst\s+psx_type_t\s*\*\s*pending_type\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bpsx_qual_type_t\s+canonical_type\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    /\bconst\s+psx_type_t\s*\*\s*type\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/psx_semantic_type_table_lookup_qual_type\s*\(\s*psx_resolution_store_semantic_types\s*\(\s*store\s*\)/.test(
      resolvedNodeTypeSource,
    )) {
  throw new Error(
    "node resolution state must use a tagged pending-or-canonical type binding",
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
const functionDefinitionStruct = resolvedFunctionHeader.match(
  /struct node_function_definition_t\s*\{([^{}]*)\};/,
);
const functionCallStruct = astSource.match(
  /struct node_function_call_t\s*\{([^{}]*)\};/,
);
if (/\bnode_func_t\b/.test(astSource) ||
    /\bnode_function_definition_t\b/.test(astSource) ||
    /\bND_FUNCDEF\b/.test(syntaxNodeKindHeader) ||
    !/\bND_FUNCDEF\b/.test(resolvedNodeKindHeader) ||
    !functionDefinitionStruct ||
    !/\bnode_t\s*\*\*\s*parameters\s*;/.test(
      functionDefinitionStruct[1],
    ) ||
    !/\bpsx_qual_type_t\s+signature_qual_type\s*;/.test(
      functionDefinitionStruct[1],
    ) ||
    /\bpsx_type_t\b|\bsignature\s*;/.test(
      functionDefinitionStruct[1],
    ) ||
    /\b(?:arguments|callee|callee_type|direct_name)\b/.test(
      functionDefinitionStruct[1],
    ) ||
    !functionCallStruct ||
    !/\bnode_t\s*\*\*\s*arguments\s*;/.test(functionCallStruct[1]) ||
    !/\bnode_t\s*\*\s*callee\s*;/.test(functionCallStruct[1]) ||
    /\b(?:callee_type|callee_qual_type|direct_name|is_implicit_declaration|parameters|signature|lvars|is_static)\b/.test(
      functionCallStruct[1],
    ) ||
    /\bis_implicit_func_decl\b/.test(astSource) ||
    !/\bpsx_function_call_resolution_state_t\s+function_call\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bconst\s+psx_semantic_type_table_t\s*\*\s*callee_type_table\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    /\bconst\s+psx_type_t\s*\*\s*callee_type\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bpsx_qual_type_t\s+callee_qual_type\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bchar\s*\*\s*direct_name\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bunsigned\s+char\s+is_implicit_declaration\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    /\bps_function_call_callee_qual_type\s*\(/.test(
      nodeTypePublicSource,
    ) ||
    !/\bpsx_function_call_qual_type\s*\(/.test(
      functionCallResolutionHeader,
    ) ||
    /\bpsx_function_call_bind_type\s*\(/.test(
      functionCallResolutionHeader,
    ) ||
    !/\bps_node_resolution_state(?:_const)?\s*\(/.test(
      functionCallResolutionSource,
    )) {
  throw new Error(
    "function definitions and resolved call metadata must live outside Syntax AST",
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
    !/\bPSX_TYPE_NAME_BOUND\b/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bPSX_TYPE_NAME_RESOLVED\b/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bpsx_type_name_resolution_kind_t\s+kind\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bconst\s+psx_type_t\s*\*\s*base_type\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bconst\s+psx_runtime_declarator_application_t\s*\*\s*runtime_application\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bconst\s+psx_semantic_type_table_t\s*\*\s*type_table\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bpsx_qual_type_t\s+qual_type\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    /\b(?:bound_base_type|resolved_type_table|resolved_qual_type|bound_runtime_application)\b/.test(
      nodeResolutionStateSource,
    ) ||
    !compoundLiteralNode ||
    !/\bpsx_type_name_ref_t\s+type_name\s*;/.test(
      compoundLiteralNode[1],
    ) ||
    /\bobject_type\b/.test(compoundLiteralNode[1]) ||
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
    "type-name expressions must keep semantic types only in resolution state",
  );
}

const nodeUtilsSource = await readFile("src/parser/node_utils.c", "utf8");
const nodeUtilsHeaderSource = await readFile(
  "src/parser/node_utils.h",
  "utf8",
);
if (allSourceFiles.includes("src/parser/node_type_public.h") ||
    allSourceFiles.includes("src/parser/node_resolution_state.h") ||
    !/\bconst\s+psx_type_t\s*\*\s*ps_node_get_type\s*\(/.test(
      resolvedNodeTypeSource,
    ) ||
    !/\bint\s+ps_node_prepare_resolution_state_in\s*\(/.test(
      resolvedNodeTypeSource,
    ) ||
    /^(?:const\s+psx_type_t\s*\*|psx_qual_type_t|int)\s+(?:ps_node_get_type|ps_node_qual_type|ps_node_prepare_resolution_state_in|ps_node_copy_resolution_state_in)\s*\(/m.test(
      nodeUtilsSource,
    )) {
  throw new Error(
    "resolved node type state and its core API must be owned by the semantic layer",
  );
}
if (/\bps_ctx_(?:get|find)_tag_member_info(?:_at_scope)?_in\s*\(/.test(
      nodeUtilsSource,
    ) ||
    !/\bps_ctx_get_tag_member_in\s*\(/.test(nodeUtilsSource) ||
    !/\bps_ctx_get_tag_member_at_scope_in\s*\(/.test(nodeUtilsSource)) {
  throw new Error(
    "parser node utilities must query member declarations and layouts through the split API",
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
    !/\bag_target_info_t\b/.test(canonicalSlotCounter)) {
  throw new Error(
    "resolved aggregate slot traversal must use the canonical TypeId and target-layout calculation",
  );
}
const splitTagTraversalFunctions = [
  "ps_tag_flat_cover_state_note_in",
  "ps_tag_find_unnamed_union_covering_offset_in",
  "ps_tag_flat_slot_count_in",
  "ps_tag_member_at_flat_slot_in",
  "ps_tag_next_named_member_in",
  "ps_tag_find_named_member_in",
  "ps_tag_select_union_member_for_init_slot_in",
  "ps_tag_union_init_member_for_slot_in",
  "ps_tag_member_designator_slot_in",
].map((name) =>
  nodeUtilsSource.match(
    new RegExp(`(?:void|int)\\s+${name}\\s*\\([^]*?\\n\\}`),
  )?.[0]
);
const splitTagTraversalSource = splitTagTraversalFunctions.join("\n");
if (splitTagTraversalFunctions.some((source) => !source) ||
    /\btag_member_info_t\b|\bctx_get_tag_member_compat_view\s*\(|\bsplit_tag_member_compat_view\s*\(/.test(
      splitTagTraversalSource,
    ) ||
    !/\bps_ctx_get_tag_member_in\s*\(/.test(splitTagTraversalSource)) {
  throw new Error(
    "tag lookup and flat-slot traversal must process declarations and target layouts directly",
  );
}
const initializerLoweringSourceForLocalLayout = await readFile(
  "src/lowering/initializer_lowering.c",
  "utf8",
);
const arrayDecayPointerArithmeticType = nodeUtilsSource.match(
  /const\s+psx_type_t\s*\*ps_node_array_decay_pointer_arith_type_in\s*\([^]*?\n\}/,
);
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
const storageSlotConstructor = resolvedObjectRefSource.match(
  /node_t\s*\*ps_node_new_lvar_storage_slot_for_in\s*\([^]*?\n\}/,
);
if (!storageSlotConstructor ||
    /\bps_(?:lvar|type)_[A-Za-z0-9_]*(?:size|sizeof)\s*\(/.test(
      storageSlotConstructor[0],
    ) ||
    !/ps_node_new_lvar_type_at_for_in\s*\([^;]*\belement\s*\)/s.test(
      initializerLoweringSourceForLocalLayout,
    ) ||
    !/ps_node_new_lvar_storage_slot_for_in\s*\(/.test(
      initializerLoweringSourceForLocalLayout,
    )) {
  throw new Error(
    "local initializer lowering must pass semantic element types directly and label byte-wise zero fill as storage slots",
  );
}
const resolvedObjectRefFactories = [
  "psx_node_new_lvar_in",
  "ps_node_new_lvar_typed_in",
  "ps_node_new_lvar_storage_slot_for_in",
  "ps_node_new_lvar_type_at_for_in",
  "psx_node_new_lvar_scalar_slot_at_in",
  "psx_node_new_lvar_fp_slot_at_in",
  "ps_node_new_lvar_fp_slot_for_in",
  "ps_node_new_param_placeholder_in",
  "ps_node_new_unsigned_lvar_typed_in",
  "psx_node_new_lvar_for_in",
  "psx_node_new_lvar_object_ref_for_in",
  "ps_node_new_lvar_expr_ref_for_in",
  "psx_node_new_lvar_identifier_ref_for_in",
  "psx_node_new_vla_decay_ref_for_in",
  "ps_node_new_param_lvar_for_in",
  "ps_node_new_gvar_array_addr_for_in",
  "psx_node_new_static_local_array_addr_for_in",
  "ps_node_new_lvar_array_addr_for_in",
  "ps_node_new_tag_member_lvar_ref_with_layout_for_in",
  "ps_node_new_gvar_for_in",
  "psx_node_new_gvar_array_base_for_in",
  "psx_node_new_static_local_gvar_for_in",
  "psx_node_new_function_reference_in",
  "psx_node_new_va_arg_area_reference_in",
  "ps_node_lvar_symbol",
];
for (const factory of resolvedObjectRefFactories) {
  const declaration = new RegExp(`\\b${factory}\\s*\\(`);
  const definition = new RegExp(
    `\\b(?:node_t|lvar_t)\\s*\\*\\s*${factory}\\s*\\(`,
  );
  if (definition.test(nodeUtilsSource) ||
      declaration.test(nodeUtilsHeaderSource) ||
      !definition.test(resolvedObjectRefSource) ||
      !declaration.test(resolvedObjectRefHeader)) {
    throw new Error(
      `resolved object reference factory ${factory} must be owned by the semantic layer`,
    );
  }
}
if (/(?:base\.)?kind\s*=\s*ND_(?:LVAR|GVAR)/.test(nodeUtilsSource) ||
    !/psx_resolution_node_set_kind\s*\(\s*store,\s*node,\s*ND_LVAR\s*\)/.test(
      resolvedObjectRefSource,
    ) ||
    !/psx_resolution_node_set_kind\s*\(\s*store,\s*node,\s*ND_GVAR\s*\)/.test(
      resolvedObjectRefSource,
    ) ||
    /(?:base\.)?kind\s*=\s*ND_(?:FUNCREF|VARARG_CURSOR)/.test(
      identifierBindingSource,
    ) ||
    !/psx_resolution_node_set_kind\s*\(\s*store,\s*reference,\s*ND_FUNCREF\s*\)/.test(
      resolvedObjectRefSource,
    ) ||
    !/psx_resolution_node_set_kind\s*\(\s*store,\s*node,\s*ND_VARARG_CURSOR\s*\)/.test(
      resolvedObjectRefSource,
    ) ||
    /(?:base\.)?kind\s*=\s*ND_(?:LVAR|GVAR|FUNCREF|VARARG_CURSOR)/.test(
      resolvedObjectRefSource,
    )) {
  throw new Error(
    "resolved object and function references must be constructed only by the semantic factory",
  );
}
const resolvedReferencePayloadPattern =
  /\bnode_(?:lvar|gvar|funcref)_t\b/;
if (resolvedReferencePayloadPattern.test(resolvedObjectRefSource) ||
    /resolved_node\.h/.test(nodeUtilsHeaderSource) ||
    !/typedef\s+struct\s*\{[^]*?psx_resolved_reference_kind_t\s+kind\s*;[^]*?\}\s*psx_resolved_reference_state_t\s*;/.test(
      earlyNodeResolutionState,
    ) ||
    !/psx_resolved_object_ref_storage_offset\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    !/psx_resolved_object_ref_name\s*\(/.test(
      resolvedTreeMaterialization,
    )) {
  throw new Error(
    "resolved reference payload must be owned by resolution state and consumed through semantic accessors",
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
if (!arrayDecayPointerArithmeticType ||
    /\bps_type_(?:size|align)of\s*\(/.test(
      arrayDecayPointerArithmeticType[0],
    ) ||
    !/try_build_pointer_arithmetic\s*\(/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "array decay result typing must follow semantic type shape without querying target layout",
  );
}
const parserTypeImplementationSource = await readFile(
  "src/parser/type.c",
  "utf8",
);
const explicitDiagnosticCompoundLiteralLoweringSource = await readFile(
  "src/lowering/compound_literal_lowering.c",
  "utf8",
);
const explicitDiagnosticCastLoweringSource = await readFile(
  "src/lowering/cast_lowering.c",
  "utf8",
);
const explicitWidthShiftConstructor = nodeUtilsSource.match(
  /node_t\s*\*ps_node_new_shift_trunc_extend_for_width_in\s*\([^]*?\n\}/,
);
const explicitHirCastCoercion = hirIrBuilder.match(
  /static\s+ir_val_t\s+coerce_explicit_cast_value\s*\([^]*?\n\}/,
);
if (!explicitWidthShiftConstructor ||
    /\bps_type_(?:size|align)of\s*\(/.test(
      explicitWidthShiftConstructor[0],
    ) ||
    !explicitHirCastCoercion ||
    /\bps_type_(?:size|align)of\s*\(/.test(explicitHirCastCoercion[0]) ||
    !/\btarget\.source_size\b/.test(explicitHirCastCoercion[0])) {
  throw new Error(
    "shift truncation must consume a width already resolved against the active target",
  );
}
const targetAwareBinaryConstructor = nodeUtilsSource.match(
  /node_t\s*\*ps_node_new_binary_for_target_in\s*\([^]*?\n\}/,
);
if (!targetAwareBinaryConstructor ||
    !/const\s+ag_target_info_t\s*\*target/.test(
      targetAwareBinaryConstructor[0],
    ) ||
    !/ps_type_binary_result_for_target_in\s*\(\s*arena_context\s*,\s*target\s*,/s.test(
      targetAwareBinaryConstructor[0],
    ) ||
    /ps_type_binary_result_in\s*\(/.test(
      targetAwareBinaryConstructor[0],
    )) {
  throw new Error(
    "typed binary construction must resolve result types against an explicit target",
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
  /typedef struct\s*\{([\s\S]*?)\}\s*psx_initializer_target_t\s*;/,
);
const initializerLeafType = initializerResolutionHeader.match(
  /typedef struct\s*\{([\s\S]*?)\}\s*psx_initializer_scalar_leaf_t\s*;/,
);
if (!initializerTargetType ||
    !/\bpsx_type_id_t\s+type_id\s*;/.test(initializerTargetType[1]) ||
    /\bpsx_type_t\b/.test(initializerTargetType[1]) ||
    !initializerLeafType ||
    !/\bpsx_type_id_t\s+type_id\s*;/.test(initializerLeafType[1]) ||
    /\bpsx_type_t\b/.test(initializerLeafType[1]) ||
    !/\bpsx_type_id_t\s+string_array_type_id\s*;/.test(
      initializerLeafType[1],
    ) ||
    !/\bpsx_resolve_initializer_designator_path_with_records\s*\([\s\S]*?\bpsx_type_id_t\s+root_type_id\b/.test(
      initializerResolutionHeader,
    ) ||
    !/\bpsx_collect_initializer_scalar_leaves_with_records\s*\([\s\S]*?\bpsx_type_id_t\s+type_id\b/.test(
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
    !/\bpsx_semantic_type_table_lookup\s*\(/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    !/\bps_type_sizeof_id\s*\(/.test(
      explicitDiagnosticInitializerResolutionSource,
    ) ||
    /\bps_type_sizeof_id_for_target\s*\(/.test(
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
const explicitDiagnosticInitializerLoweringSource = await readFile(
  "src/lowering/initializer_lowering.c",
  "utf8",
);
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
const semanticInvariantsSource = await readFile(
  "src/semantic/semantic_invariants.c",
  "utf8",
);
const semanticTreeWalkSource = await readFile(
  "src/semantic/tree_walk.c",
  "utf8",
);
const semanticTypeIdentityPassSource = await readFile(
  "src/semantic/type_identity_pass.c",
  "utf8",
);
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
  "src/semantic/parameter_declaration_plan.h",
  "utf8",
);
const parameterDeclarationPlanSource = await readFile(
  "src/semantic/parameter_declaration_plan.c",
  "utf8",
);
const controlFlowValidationSource = await readFile(
  "src/semantic/control_flow_validation.c",
  "utf8",
);
const semanticDiagnosticsSource = await readFile(
  "src/semantic/semantic_diagnostics.c",
  "utf8",
);
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
  !/\bpsx_resolve_binary_result_qual_type_in\s*\(/.test(
    semanticPassSource,
  ) ||
  !/\bpsx_resolve_subscript_qual_types_in\s*\(/.test(
    semanticPassSource,
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
    "expression QualType rules and operators must be AST-independent while semantic pass adapts syntax directly",
  );
}
if (!/\bps_type_integer_rank\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    /\bps_type_sizeof\s*\(/.test(expressionOperandResolutionSource) ||
    /\bps_type_sizeof\s*\(/.test(semanticDiagnosticsSource) ||
    /\bps_type_sizeof\s*\(/.test(semanticPassSource)) {
  throw new Error(
    "semantic rank and category checks must not use target layout size",
  );
}
const functionParameterSyntaxSource = await readFile(
  "src/parser/function_parameter_syntax.c",
  "utf8",
);
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
if (!/\bDIAG_ERR_INTERNAL_INVARIANT_FAILED\b/.test(semanticInvariantsSource) ||
    /\bps_diag_ctx\s*\(/.test(semanticInvariantsSource)) {
  throw new Error(
    "semantic invariant failures must be reported as internal compiler errors",
  );
}
if ([
      semanticInvariantsSource,
      controlFlowValidationSource,
      semanticDiagnosticsSource,
      lvarUsageAnalysisSource,
      semanticPassSource,
      identifierBindingSource,
      enumConstantResolutionSource,
      declarationRegistrationSource,
      declarationApplicationSource,
      functionParameterResolutionSource,
      functionParameterSyntaxSource,
      parserSource,
      expressionParserSource,
      parserDeclarationSyntaxSource,
      declaratorSyntaxSource,
      aggregateMemberSyntaxSource,
      localDeclarationSyntaxSource,
      toplevelDeclarationSyntaxSource,
      initializerSyntaxSource,
      enumConstSource,
      statementParserSource,
      staticAssertDeclarationSource,
      parserSemanticContextImplementation,
      localRegistrySource,
      nodeUtilsSource,
      parserTypeImplementationSource,
      frontendFunctionDefinitionSource,
      localDeclarationFrontendSource,
      localDeclarationTreeResolutionSource,
      toplevelDeclarationFrontendSource,
      frontendTranslationUnitSource,
      vlaLoweringSource,
      globalObjectLoweringSource,
      explicitDiagnosticCompoundLiteralLoweringSource,
      explicitDiagnosticCastLoweringSource,
      hirIrBuilder,
      compilerMainSource,
      explicitDiagnosticInitializerResolutionSource,
      explicitDiagnosticInitializerLoweringSource,
      explicitDiagnosticStaticDataInitializerSource,
      tokenizerSource,
      tokenizerNumberSource,
      tokenizerLiteralsSource,
      tokenizerCursorSource,
      tokenizerDiagnosticHelperSource,
      tokenizerAllocatorSource,
      tokenizerConfigRuntimeSource,
      configSource,
    ].some((source) => contextFreeParserDiagnosticApi.test(source)) ||
    contextFreeParserDiagnosticApi.test(compilerOwnedDiagnosticSource) ||
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
    ) ||
    /\bps_node_(?:reject_const_assign_at|reject_const_qual_discard_at|expect_lvalue_at)\s*\(/.test(
      semanticPassSource,
    )) {
  throw new Error(
    "migrated parser and semantic phases must preserve and use explicit diagnostic contexts",
  );
}
const syntaxNodeKindSource = await readFile(
  "src/parser/syntax_node_kind.h",
  "utf8",
);
const resolvedNodeKindSource = await readFile(
  "src/semantic/resolved_node_kind.h",
  "utf8",
);
const syntaxNodeKindEnum = syntaxNodeKindSource.match(
  /typedef enum\s*\{([\s\S]*?)\}\s*psx_syntax_node_kind_t\s*;/,
);
const resolvedNodeKindEnum = resolvedNodeKindSource.match(
  /typedef enum\s*\{([\s\S]*?)\}\s*psx_resolved_node_kind_t\s*;/,
);
const semanticRoleClassifier = semanticInvariantsSource.match(
  /static\s+node_semantic_role_t\s+semantic_role\s*\([^)]*\)\s*\{([\s\S]*?)\n\}/,
);
if (!syntaxNodeKindEnum || !resolvedNodeKindEnum ||
    !semanticRoleClassifier) {
  throw new Error("semantic node role classification must remain inspectable");
}
const declaredNodeKinds = [
  ...new Set(
    [
      ...syntaxNodeKindEnum[1].matchAll(/\bND_[A-Z0-9_]+\b/g),
      ...resolvedNodeKindEnum[1].matchAll(/\bND_[A-Z0-9_]+\b/g),
    ].map(
      (match) => match[0],
    ),
  ),
];
const classifiedNodeKinds = [
  ...semanticRoleClassifier[1].matchAll(/\bcase\s+(ND_[A-Z0-9_]+)\s*:/g),
].map((match) => match[1]);
const classifiedNodeKindSet = new Set(classifiedNodeKinds);
const missingNodeKinds = declaredNodeKinds.filter(
  (kind) => !classifiedNodeKindSet.has(kind),
);
const duplicateNodeKinds = classifiedNodeKinds.filter(
  (kind, index) => classifiedNodeKinds.indexOf(kind) !== index,
);
if (missingNodeKinds.length || duplicateNodeKinds.length ||
    classifiedNodeKindSet.size !== declaredNodeKinds.length) {
  throw new Error(
    "every syntax and resolved working node kind must have exactly one semantic expression role; " +
      `missing: ${missingNodeKinds.join(", ") || "none"}; ` +
      `duplicates: ${[...new Set(duplicateNodeKinds)].join(", ") || "none"}`,
  );
}
const completeSemanticBoundaryCheckCount = [
  ...legacySyntaxDiagnosticsSource.matchAll(
    /\bpsx_require_semantic_tree_has_canonical_expression_types\s*\(/g,
  ),
].length;
const initializerSemanticBoundaryCheckCount = [
  ...legacySyntaxDiagnosticsSource.matchAll(
    /\bpsx_require_semantic_initializer_has_canonical_expression_types\s*\(/g,
  ),
].length;
const internedSemanticBoundaryCheckCount = [
  ...legacySyntaxDiagnosticsSource.matchAll(
    /\bpsx_require_semantic_tree_has_interned_expression_types\s*\(/g,
  ),
].length;
const internedInitializerBoundaryCheckCount = [
  ...legacySyntaxDiagnosticsSource.matchAll(
    /\bpsx_require_semantic_initializer_has_interned_expression_types\s*\(/g,
  ),
].length;
const availableTypeInterningCheckCount = [
  ...legacySyntaxDiagnosticsSource.matchAll(
    /\bpsx_require_available_semantic_tree_types_interned\s*\(/g,
  ),
].length;
if (completeSemanticBoundaryCheckCount !== 2 ||
    initializerSemanticBoundaryCheckCount !== 1 ||
    internedSemanticBoundaryCheckCount !== 2 ||
    internedInitializerBoundaryCheckCount !== 1 ||
    availableTypeInterningCheckCount !== 1) {
  throw new Error(
    "every semantic pipeline entry must pre-intern available types and require interned and canonical expression contracts",
  );
}
const typedResolutionBoundary =
  legacySyntaxDiagnosticsSource.match(
    /static\s+int\s+resolve_typed_tree\s*\([^)]*\)\s*\{([\s\S]*?)\n\}/,
  )?.[1] ?? "";
const expressionFinalizationBoundary =
  legacySyntaxDiagnosticsSource.match(
    /static\s+int\s+finalize_expression_compatibility_tree\s*\([^)]*\)\s*\{([\s\S]*?)\n\}/,
  )?.[1] ?? "";
const functionSemanticPipelineBody =
  legacySyntaxDiagnosticsSource.match(
    /static\s+int\s+resolve_function_compatibility_tree_in_contexts\s*\([^)]*\)\s*\{([\s\S]*?)\n\}/,
  )?.[1] ?? "";
const expressionSemanticPipelineBody =
  legacySyntaxDiagnosticsSource.match(
    /static\s+int\s+resolve_nonfunction_compatibility_tree\s*\([^)]*\)\s*\{([\s\S]*?)\n\}/,
  )?.[1] ?? "";
if (!/psx_require_available_semantic_tree_types_interned\s*\(/.test(
      typedResolutionBoundary,
    ) ||
    !/psx_require_semantic_tree_has_interned_expression_types\s*\([\s\S]*?psx_require_semantic_tree_has_canonical_expression_types\s*\(/.test(
      expressionFinalizationBoundary,
    ) ||
    !/psx_require_semantic_initializer_has_interned_expression_types\s*\([\s\S]*?psx_require_semantic_initializer_has_canonical_expression_types\s*\(/.test(
      expressionFinalizationBoundary,
    ) ||
    !/psx_require_semantic_tree_has_interned_expression_types\s*\([\s\S]*?psx_require_semantic_tree_has_canonical_expression_types\s*\(/.test(
      functionSemanticPipelineBody,
    ) ||
    !/resolve_typed_tree\s*\([\s\S]*?lower_tree\s*\([\s\S]*?finalize_expression_compatibility_tree\s*\(/.test(
      expressionSemanticPipelineBody,
    )) {
  throw new Error(
    "semantic tree resolution must intern available types before lowering and enforce final expression contracts",
  );
}
if (/\bpsx_lower_implicit_conversions\s*\(/.test(
      functionSemanticPipelineBody,
    ) ||
    /\bpsx_lower_implicit_conversions\s*\(/.test(
      expressionSemanticPipelineBody,
    ) ||
    !/\bhir_ir_coerce_direct_value_to_qual_type\s*\(/.test(
      hirIrBuilder,
    ) ||
    !/semantic_target\.kind\s*==\s*PSX_TYPE_BOOL/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "function and expression semantic trees must preserve operands while HIR-to-IR owns implicit conversion and Bool normalization",
  );
}
if (!/\bpsx_walk_semantic_tree\s*\(/.test(semanticInvariantsSource) ||
    !/\bpsx_walk_semantic_tree_mut\s*\(/.test(
      semanticTypeIdentityPassSource,
    ) ||
    !/\bps_node_set_qual_type_identity\s*\(\s*pass->resolution_store\s*,\s*node\s*,\s*type\s*\)/.test(
      semanticTypeIdentityPassSource,
    ) ||
    !/actual\.type_id\s*==\s*PSX_TYPE_ID_INVALID/.test(
      semanticInvariantsSource,
    ) ||
    !/node_type\s*!=\s*psx_semantic_type_table_lookup_qual_type\s*\(/.test(
      semanticInvariantsSource,
    ) ||
    !/\bpsx_finalize_semantic_tree_types\s*\(/.test(
      semanticInvariantsSource,
    ) ||
    !/\bwalk_node\s*\(\s*store\s*,\s*node->lhs\b/.test(semanticTreeWalkSource) ||
    /\bvalidate_tree\s*\(\s*validation\s*,\s*node->(?:lhs|rhs)\b/.test(
      semanticInvariantsSource,
    )) {
  throw new Error(
    "semantic type finalization must intern QualType and materialize canonical TypeId objects in one AST traversal",
  );
}
if (/\bps_function_definition_(?:return_type|signature_qual_type)\s*\(/.test(
      nodeTypePublicSource,
    ) ||
    /\bps_function_definition_(?:return_type|signature_qual_type)\s*\(/.test(
      nodeUtilsSource,
    ) ||
    !/\bps_function_definition_return_qual_type\s*\(/.test(
      resolvedFunctionHeader,
    ) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      resolvedFunctionSource,
    ) ||
    /\bconst\s+psx_type_t\s*\*\s*signature\s*;/.test(
      resolvedFunctionHeader,
    ) ||
    /function->signature\b/.test(
      resolvedFunctionSource + semanticInvariantsSource,
    ) ||
    /\bfunction_node\b/.test(
      declarationPipelineHeader + declarationPipelineSource,
    ) ||
    !/psx_semantic_type_table_describe\s*\(/.test(
      semanticInvariantsSource,
    ) ||
    !/node_type\s*!=\s*NULL/.test(semanticInvariantsSource)) {
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
const sourceCastResolutionHeader = await readFile(
  "src/semantic/source_cast_resolution.h",
  "utf8",
);
const compoundLiteralResolutionHeader = await readFile(
  "src/semantic/compound_literal_resolution.h",
  "utf8",
);
const compoundLiteralLoweringSource = await readFile(
  "src/lowering/compound_literal_lowering.c",
  "utf8",
);
const compoundLiteralLoweringHeader = await readFile(
  "src/lowering/compound_literal_lowering.h",
  "utf8",
);
const runtimeInitializerPlanHeader = await readFile(
  "src/lowering/runtime_initializer_plan.h",
  "utf8",
);
const runtimeInitializerPlanSource = await readFile(
  "src/lowering/runtime_initializer_plan.c",
  "utf8",
);
const typedHirMaterializationSource = await readFile(
  "src/semantic/typed_hir_tree_materialization.c",
  "utf8",
);
const aggregateQualTypeCastPlanStart = castLoweringSource.indexOf(
  "psx_plan_aggregate_source_cast_qual_types",
);
const aggregateCompatibilityCastPlanStart = castLoweringSource.indexOf(
  "int psx_plan_aggregate_source_cast(",
  aggregateQualTypeCastPlanStart,
);
const aggregateQualTypeCastPlan =
  aggregateQualTypeCastPlanStart >= 0 &&
  aggregateCompatibilityCastPlanStart > aggregateQualTypeCastPlanStart
    ? castLoweringSource.slice(
        aggregateQualTypeCastPlanStart,
        aggregateCompatibilityCastPlanStart,
      )
    : "";
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
    !/ps_type_sizeof_id\s*\(/.test(
      aggregateCastResolutionSource,
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
    !/target_type->kind\s*==\s*PSX_TYPE_VOID/.test(
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
if (!/\bint\s+psx_plan_aggregate_source_cast\s*\(/.test(
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
    !/psx_validate_source_cast_qual_types\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
    !/psx_plan_validated_aggregate_source_cast\s*\(/.test(
      semanticLoweringPassSource,
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
    /\bnode->is_source_cast\s*=\s*0\s*;/.test(
      castLoweringSource,
    ) ||
    !/\bmaterialize_source_cast\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    !/PSX_HIR_CAST/.test(
      resolvedTreeMaterialization,
    ) ||
    !/PSX_SOURCE_CAST_DIRECT_HIR/.test(nodeResolutionStateSource) ||
    !/PSX_SOURCE_CAST_AGGREGATE_DIRECT_HIR/.test(
      nodeResolutionStateSource,
    ) ||
    !/PSX_SOURCE_CAST_AGGREGATE_TEMPORARY/.test(
      nodeResolutionStateSource,
    ) ||
    /aggregate_value|lowered_value|is_lowered/.test(
      `${sourceCastResolutionHeader}\n${nodeResolutionStateSource}`,
    ) ||
    /\bps_node_new_/.test(castLoweringSource) ||
    !/\bmaterialize_aggregate_temporary_parts\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    !/PSX_HIR_ASSIGN/.test(resolvedTreeMaterialization) ||
    !/resolution->kind\s*=\s*PSX_SOURCE_CAST_DIRECT_HIR/.test(
      semanticLoweringPassSource,
    ) ||
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
if (!directSizeofTypeName ||
    /\bps_ctx_type_sizeof_in\s*\(/.test(directSizeofTypeName[0]) ||
    !/\bps_type_sizeof_id\s*\(/.test(
      directSizeofTypeName[0],
    ) ||
    /direct_type_before_application\(\s*base_type\s*,\s*runtime_application\s*\);[\s\S]{0,150}if\s*\(factor\s*<=\s*0\)\s*return\s+0/.test(
      directSizeofTypeName[0],
    ) ||
    !/op->array_len\s*<=\s*0\s*\|\|\s*factor\s*<=\s*0/.test(
      directSizeofTypeName[0],
    )) {
  throw new Error(
    "direct sizeof(type-name) must apply pointer declarators before rejecting an unsized function base",
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
    !/\.runtime_application\s*=\s*[^,]*ps_type_contains_vla_array/.test(
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
    !/psx_type_name_bound_runtime_application\s*\(\s*&base_state\s*\)/.test(
      directSizeofTypeName[0],
    ) ||
    !/ps_ctx_semantic_expression_in\s*\([^]*?runtime_factor_ids\[i\]/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/runtime_application/.test(nodeResolutionStateSource) ||
    !/ps_ctx_find_typedef_name_at_in_contexts\s*\(/.test(
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
    ) ||
    /\blower_aggregate_address_expression\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
    !/\bmaterialize_address_expression\s*\(/.test(
      typedHirMaterializationSource,
    ) ||
    !/\baddress_requires_typed_hir_lowering\s*\(/.test(
      typedHirMaterializationSource,
    )) {
  throw new Error(
    "aggregate address representation must be lowered while materializing Typed HIR, without rewriting the semantic node tree",
  );
}
if (!/\bint\s+psx_plan_compound_literal_storage_in_contexts\s*\(/.test(
      compoundLiteralLoweringHeader,
    ) ||
    !/\bpsx_apply_resolved_global_declaration_pipeline\s*\(/.test(
      compoundLiteralLoweringSource,
    ) ||
    /\bpsx_apply_global_declaration_pipeline\s*\(/.test(
      compoundLiteralLoweringSource,
    ) ||
    !/initializer_is_resolved\s*\?\s*psx_build_static_aggregate_initializer_plan\s*\(/.test(
      declarationPipelineSource,
    ) ||
    /\blower_compound_literal_expression_in_contexts\s*\(/.test(
      compoundLiteralLoweringSource,
    ) ||
    !/\bmaterialize_compound_literal\s*\(/.test(
      typedHirMaterializationSource,
    ) ||
    !/resolved_kind\s*==\s*ND_COMPOUND_LITERAL/.test(
      typedHirMaterializationSource,
    ) ||
    /selected_expression|direct_value/.test(nodeResolutionStateSource) ||
    !/PSX_COMPOUND_LITERAL_DIRECT_INITIALIZER/.test(
      nodeResolutionStateSource,
    ) ||
    !/direct_initializer_index/.test(nodeResolutionStateSource) ||
    !/psx_compound_literal_direct_initializer_const\s*\(/.test(
      compoundLiteralResolutionHeader,
    ) ||
    /struct\s+node_t\s*\*runtime_initialization/.test(
      nodeResolutionStateSource,
    ) ||
    !/struct\s+psx_runtime_initializer_plan_t\s*\*runtime_initializer/.test(
      nodeResolutionStateSource,
    ) ||
    !/psx_build_runtime_initializer_plan\s*\(/.test(
      runtimeInitializerPlanHeader,
    ) ||
    !/PSX_RUNTIME_INITIALIZER_ASSIGN/.test(runtimeInitializerPlanSource) ||
    !/materialize_runtime_initializer\s*\(/.test(
      typedHirMaterializationSource,
    ) ||
    /node->rhs\s*=\s*NULL/.test(semanticLoweringPassSource) ||
    /_Static_assert\s*\(\s*sizeof\s*\(\s*node_compound_literal_t\s*\)/.test(
      compoundLiteralLoweringSource,
    ) ||
    /\*\s*\(\s*node_num_t\s*\*\s*\)\s*node\s*=/.test(
      compoundLiteralLoweringSource,
    )) {
  throw new Error(
    "compound literals must use a storage plan and materialize directly into Typed HIR without AST replacement",
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

const typeSource = await readFile("src/parser/type.h", "utf8");
const typeBuilderSource = await readFile(
  "src/parser/type_builder.h",
  "utf8",
);
const typeBuilderApiNames = [
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
  "ps_type_adjust_parameter_type_in",
  "ps_type_complete_array",
  "ps_type_set_decl_spec_qualifiers",
  "ps_type_add_qualifiers",
  "ps_type_remove_qualifiers",
  "ps_type_remove_all_qualifiers_recursive",
];
for (const functionName of typeBuilderApiNames) {
  const name = new RegExp(`\\b${functionName}\\b`);
  if (name.test(typeSource)) {
    throw new Error(`${functionName} must not be exposed by parser/type.h`);
  }
  if (!name.test(typeBuilderSource)) {
    throw new Error(`${functionName} must be declared by parser/type_builder.h`);
  }
}
for (const functionName of [
  "ps_type_new_integer_kind_in",
  "ps_type_new_integer_in",
  "ps_type_new_enum_in",
  "ps_type_new_record_in",
  "ps_type_new_floating_in",
  "ps_type_new_float_in",
  "ps_type_new_array_in",
  "ps_type_new_tag_in",
]) {
  const declaration = typeBuilderSource.match(
    new RegExp(`\\b${functionName}\\s*\\(([^;]*)\\)\\s*;`),
  );
  if (!declaration || /\b(?:size|align)\b/.test(declaration[1])) {
    throw new Error(
      `${functionName} must construct semantic identity without target layout arguments`,
    );
  }
}
if (/^\s*psx_type_t\s*\*/m.test(typeSource)) {
  throw new Error("parser/type.h must not publish mutable type results");
}

const typeBuilderApiRe = new RegExp(
  `\\b(?:${typeBuilderApiNames.join("|")})\\b`,
);
const typeBuilderUsers = new Set([
  "src/parser/type_builder.h",
  "src/parser/type.c",
  "src/parser/expr.c",
  "src/parser/semantic_ctx.c",
  "src/parser/local_registry.c",
  "src/parser/global_registry.c",
  "src/parser/node_utils.c",
  "src/declaration_pipeline.c",
  "src/semantic/declaration_resolution.c",
  "src/semantic/parameter_declaration_resolution.c",
  "src/semantic/resolved_object_ref.c",
  "src/semantic/declaration_application.c",
  "src/semantic/type_name_resolution.c",
  "src/semantic/semantic_pass.c",
  "src/semantic/literal_resolution.c",
  "src/semantic/generic_selection_resolution.c",
  "src/semantic/static_initializer_resolution.c",
  "src/semantic/type_query_resolution.c",
  "src/semantic/identifier_binding.c",
  "src/semantic/function_call_resolution.c",
  "src/semantic/expression_operand_resolution.c",
  "src/semantic/type_identity.c",
  "src/lowering/vla_lowering.c",
  "src/lowering/cast_lowering.c",
  "test/test_parser.c",
]);
const typeBuilderFiles = [...sourceFiles, "test/test_parser.c"];
const typeBuilderViolations = [];
for (const file of typeBuilderFiles) {
  const source = await readFile(file, "utf8");
  if (!typeBuilderUsers.has(file) &&
      (typeBuilderApiRe.test(source) ||
       /#[ \t]*include[^\n]*(?:[\/"]type_builder\.h")/.test(source))) {
    typeBuilderViolations.push(file);
  }
  if (/\bpsx_type_(?:rebuild_array_dims|copy_common_qualifiers|rebase_declarator)\b/.test(
    source,
  )) {
    typeBuilderViolations.push(`${file}:removed type mutation API`);
  }
}
if (typeBuilderViolations.length) {
  throw new Error(
    "type builders must stay inside construction owners:\n" +
      typeBuilderViolations.sort().join("\n"),
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
if (/\bpsx_declarator_(?:op_kind_t|op_t|shape_t)\b|\bPSX_DECL_OP_/.test(
  typeSource,
)) {
  throw new Error(
    "canonical type declarations must not contain declarator shape state",
  );
}
if (!/\bpsx_declarator_shape_t\b/.test(declaratorShapeSource) ||
    /#[ \t]*include[^\n]*type\.h/.test(declaratorShapeSource)) {
  throw new Error(
    "declarator shape state must depend only on the canonical type forward declaration",
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
  "ps_declarator_op_set_function_params_in",
  "ps_declarator_shape_set_array_bound",
  "ps_declarator_op_set_variadic",
  "ps_declarator_shape_count_ops",
];
for (const functionName of declaratorShapeBuilderApiNames) {
  const name = new RegExp(`\\b${functionName}\\b`);
  if (name.test(typeSource)) {
    throw new Error(`${functionName} must not be exposed by parser/type.h`);
  }
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
  "src/parser/type.c",
  "src/parser/declaration_syntax.c",
  "src/semantic/type_name_resolution.c",
  "src/semantic/function_parameter_resolution.c",
  "src/semantic/declaration_application.c",
  "src/semantic/parameter_declaration_resolution.c",
  "src/semantic/type_query_resolution.c",
  "src/declaration_pipeline.c",
  "test/test_parser.c",
]);
const declaratorShapeBuilderViolations = [];
for (const file of typeBuilderFiles) {
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
  if (file !== "src/parser/type.c" &&
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

for (const functionName of [
  "ps_type_usual_arithmetic_result_for_target_in",
  "ps_type_binary_result_for_target_in",
  "ps_type_conditional_result_for_target_in",
  "ps_type_address_result_in",
  "ps_type_decay_array_in",
  "ps_type_subscript_result_in",
  "ps_type_generic_control_in",
]) {
  const signature = new RegExp(
    `\\bconst\\s+psx_type_t\\s*\\*\\s*${functionName}\\s*\\(`,
  );
  if (!signature.test(typeSource)) {
    throw new Error(`${functionName} must publish a const type view`);
  }
}

const canonicalTypeStruct = typeSource.match(
  /struct psx_type_t\s*\{([\s\S]*?)\n\};/,
);
const tagContextSource = await readFile(
  "src/parser/semantic_ctx.c",
  "utf8",
);
const tagPublicSource = await readFile(
  "src/parser/tag_public.h",
  "utf8",
);
const semanticContextHeaderSource = await readFile(
  "src/parser/semantic_ctx.h",
  "utf8",
);
const tagFlatCoverSource = await readFile(
  "src/parser/tag_flat_cover.h",
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
if (/\btag_member_info_t\b|#include\s+"tag_member_public\.h"/.test(
      tagPublicSource,
    ) ||
    !/\bps_record_member_decl_is_tag_aggregate\s*\([^;]*const\s+psx_record_member_decl_t\s*\*/s.test(
      tagPublicSource,
    ) ||
    !/\bps_record_member_decl_flat_slots_in\s*\([^;]*const\s+psx_record_member_decl_t\s*\*/s.test(
      tagPublicSource,
    )) {
  throw new Error(
    "public tag APIs must expose semantic member declarations without compatibility member views",
  );
}
if (/#include\s+"tag_member_public\.h"/.test(
      `${semanticContextHeaderSource}\n${parserTypeImplementationSource}`,
    ) ||
    /\btag_member_info_t\b/.test(nodeUtilsHeaderSource)) {
  throw new Error(
    "compatibility tag member views must stay out of production parser headers and type implementation",
  );
}
for (const functionName of [
  "ps_tag_member_at_flat_slot_in",
  "ps_tag_next_named_member_in",
  "ps_tag_first_named_member_in",
  "ps_tag_find_named_member_in",
  "ps_tag_select_union_member_for_init_slot_in",
  "ps_tag_union_init_member_for_slot_in",
]) {
  const signature = tagPublicSource.match(
    new RegExp(`\\b${functionName}\\s*\\([^;]*;`, "s"),
  )?.[0];
  if (!signature ||
      !/psx_record_member_decl_t\s*\*/.test(signature) ||
      !/psx_record_member_layout_t\s*\*/.test(signature)) {
    throw new Error(
      `${functionName} must return member declarations and target layouts separately`,
    );
  }
}
if (/\btag_member_info_t\b/.test(tagFlatCoverSource) ||
    !/ps_tag_flat_cover_state_covers\s*\([^;]*int\s+member_offset/s.test(
      tagFlatCoverSource,
    ) ||
    !/ps_tag_flat_cover_state_note_in\s*\([^;]*const\s+psx_record_member_decl_t\s*\*\s*declaration\s*,[^;]*const\s+psx_record_member_layout_t\s*\*\s*layout/s.test(
      tagFlatCoverSource,
    )) {
  throw new Error(
    "flat-cover APIs must receive member declaration and target layout separately",
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
  /int\s+ps_ctx_register_tag_type_in_contexts\s*\([^]*?\n\}/,
);
const tagDeclarationRequestStruct =
  tagDeclarationResolutionHeader.match(
    /typedef\s+struct\s*\{([^]*?)\}\s*psx_tag_declaration_resolution_request_t\s*;/,
  );
const refreshRecordDeclFunction = tagContextSource.match(
  /static\s+void\s+refresh_cached_record_decl\s*\([^]*?\n\}/,
);
const getTagMemberFunction = tagContextSource.match(
  /static\s+bool\s+get_tag_member_impl_in\s*\([^]*?\n\}/,
);
const recordDeclStruct = typeSource.match(
  /typedef struct psx_record_decl_t\s*\{([\s\S]*?)\}\s*psx_record_decl_t\s*;/,
);
const recordMemberDeclStruct = typeSource.match(
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
if (!getTagMemberFunction ||
    !/\bcollect_tag_member_declarations_in\s*\(/.test(
      getTagMemberFunction[0],
    ) ||
    /\bsort_tag_members_in\s*\(/.test(getTagMemberFunction[0]) ||
    !/\bps_ctx_get_tag_member_in\s*\([^;]*psx_record_member_decl_t\s*\*\s*out_declaration\s*,[^;]*psx_record_member_layout_t\s*\*\s*out_layout/s.test(
      aggregateRegistryHeader,
    ) ||
    !/\bps_ctx_find_tag_member_in\s*\([^;]*psx_record_member_decl_t\s*\*\s*out_declaration\s*,[^;]*psx_record_member_layout_t\s*\*\s*out_layout/s.test(
      aggregateRegistryHeader,
    )) {
  throw new Error(
    "tag member queries must preserve declaration order and return declaration and layout separately",
  );
}
if (!recordMemberDeclStruct ||
    !/\bconst\s+psx_semantic_type_table_t\s*\*\s*decl_type_table\s*;/.test(
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
    !refreshRecordDeclFunction ||
    !/members\s*\[\s*i\s*\]\s*=\s*\(psx_record_member_decl_t\)/.test(
      refreshRecordDeclFunction[0],
    )) {
  throw new Error(
    "RecordDecl members must be physically independent from target placement",
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
    !/\bag_target_info_t\s+target\s*;/.test(
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
if (!canonicalTypeStruct ||
    !/\bconst\s+psx_type_t\s*\*\s*base\s*;/.test(
      canonicalTypeStruct[1],
    ) ||
    !/\bconst\s+psx_type_t\s*\*\s*const\s*\*\s*param_types\s*;/.test(
      canonicalTypeStruct[1],
    ) ||
    /\baggregate_definition\b/.test(canonicalTypeStruct[1]) ||
    !/\bpsx_record_id_t\s+record_id\s*;/.test(canonicalTypeStruct[1]) ||
    !/\btoken_kind_t\s+ps_type_tag_token_kind\s*\(/.test(typeSource) ||
    !/\bpsx_type_qualifiers_t\s+qualifiers\s*;/.test(
      canonicalTypeStruct[1],
    ) ||
    /\b(?:int\s+)?size\s*;/.test(canonicalTypeStruct[1]) ||
    /\b(?:int\s+)?align\s*;/.test(canonicalTypeStruct[1]) ||
    /\bis_(?:const_qualified|volatile_qualified|atomic)\b/.test(
      canonicalTypeStruct[1],
    ) ||
    !/\bpsx_integer_kind_t\s+integer_kind\s*;/.test(
      canonicalTypeStruct[1],
    ) ||
    /\btoken_kind_t\s+scalar_kind\s*;/.test(canonicalTypeStruct[1]) ||
    /\btoken_kind_t\s+tag_kind\s*;/.test(canonicalTypeStruct[1]) ||
    /\bis_long_long\b/.test(canonicalTypeStruct[1]) ||
    !/\bpsx_floating_kind_t\s+floating_kind\s*;/.test(
      canonicalTypeStruct[1],
    ) ||
    /\btk_float_kind_t\s+fp_kind\s*;/.test(canonicalTypeStruct[1]) ||
    !/\btk_float_kind_t\s+ps_type_floating_token_kind\s*\(/.test(
      typeSource,
    ) ||
    !/\bps_type_new_floating_in\s*\(/.test(typeBuilderSource) ||
    /\bis_long_double\b/.test(canonicalTypeStruct[1]) ||
    !recordDeclStruct ||
    !/\bpsx_record_id_t\s+record_id\s*;/.test(recordDeclStruct[1]) ||
    !/\bpsx_type_kind_t\s+record_kind\s*;/.test(recordDeclStruct[1]) ||
    /\btoken_kind_t\s+tag_kind\s*;/.test(recordDeclStruct[1]) ||
    !/\bunsigned\s+char\s+is_complete\s*;/.test(recordDeclStruct[1]) ||
    /\b(?:size|align)\s*;/.test(recordDeclStruct[1]) ||
    !/\bconst\s+psx_record_member_decl_t\s*\*\s*members\s*;/.test(
      recordDeclStruct[1],
    )) {
  throw new Error(
    "canonical recursive types must expose one qualifier value and const record declarations with stable identity and explicit completeness",
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
    !/ps_type_new_record_in\s*\([^]*?tag->record_decl\s*\)/.test(
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
const typeIdLayoutFunction = typeLayoutSource.match(
  /int\s+ps_type_layout_of_id\s*\([^]*?\n\}/,
);
if (!/\bps_type_layout_of_id\s*\(/.test(typeLayoutSource) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(typeLayoutSource) ||
    /\bpsx_semantic_type_table_lookup\s*\(/.test(typeLayoutSource) ||
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
    !/\blayout_of_id_recursive\s*\(/.test(typeIdLayoutFunction[0]) ||
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
    "layout must resolve TypeId with an explicit target and get record completeness from RecordLayoutTable",
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
  if (/\bps_type_(?:layout_of_id_with_records|(?:size|align)of_id_with_records|(?:size|align)of_id_for_target)\s*\(/.test(source)) {
    throw new Error(
      `${path} must use the single record-aware TypeId layout API`,
    );
  }
}
if (/parser\/type\.h/.test(typeShapeLoweringSources) ||
    /\bpsx_semantic_type_table_lookup\s*\(/.test(typeShapeLoweringSources) ||
    /\bpsx_type_t\b/.test(typeShapeLoweringSources) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      typeShapeLoweringSources,
    ) ||
    !/\bir_mir_integer_promotion_is_unsigned\s*\(/.test(
      typeShapeLoweringSources,
    ) ||
    !/\bir_mir_usual_arithmetic_result_is_unsigned\s*\(/.test(
      typeShapeLoweringSources,
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
    !/\bag_target_info_scalar_size\s*\(/.test(tagContextSource) ||
    !/\bag_target_info_scalar_alignment\s*\(/.test(tagContextSource) ||
    !/\bpsx_ctx_find_typedef_layout_in\s*\(/.test(tagContextSource) ||
    !/\bag_target_info_pointer_alignment\s*\(/.test(
      alignasValueSource,
    ) ||
    !/wants_alignment\s*\?\s*alignment\s*:\s*size/.test(
      enumConstSource,
    )) {
  throw new Error(
    "parser type-name constants must derive size and alignment independently from TargetSpec",
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
      !/\bps_type_sizeof_id\s*\(/.test(source) ||
      /\bps_type_(?:size|align)of_id_for_target\s*\(/.test(source) ||
      /\bps_type_(?:size|align)of_for_target\s*\(/.test(source) ||
      (name === "local" && /storage_size\s*>=/.test(source)) ||
      /\bpsx_plan_(?:local|parameter)_storage_for_target\s*\(/.test(
        header,
      )) {
    throw new Error(
      `${name} storage planning must accept TypeId and explicit record layouts`,
    );
  }
}

for (const [name, source, requiredLayoutCall] of [
  ["pointer arithmetic", hirIrBuilder, /\bps_type_sizeof_id\s*\(/],
  ["subscript", hirIrBuilder, /\bps_type_sizeof_id\s*\(/],
  ["initializer", explicitDiagnosticInitializerLoweringSource, /\bps_type_sizeof_id\s*\(/],
  ["VLA", vlaLoweringSource, /\bps_type_sizeof_id\s*\(/],
  ["static data initializer", explicitDiagnosticStaticDataInitializerSource, /\bps_type_sizeof_id\s*\(/],
  ["translation unit data", translationUnitDataLoweringSource, /\bps_type_sizeof_id\s*\(/],
]) {
  if (!requiredLayoutCall.test(source) ||
      /\bps_type_(?:size|align)of_for_target\s*\(/.test(source)) {
    throw new Error(
      `${name} lowering must obtain target layout through an interned TypeId`,
    );
  }
}
const automaticLocalPipeline = declarationPipelineSource.match(
  /static\s+int\s+begin_automatic_local_declaration_pipeline\s*\([^]*?\n\}/,
);
if (!automaticLocalPipeline ||
    !/\bps_ctx_intern_qual_type_in\s*\([^]*?\bpsx_resolve_local_declaration\s*\(/.test(
      automaticLocalPipeline[0],
    ) ||
    !/\bps_ctx_intern_qual_type_in\s*\([^]*?\bpsx_plan_parameter_storage_for_type_id\s*\(/.test(
      parameterDeclarationResolutionSource,
    )) {
  throw new Error(
    "local and parameter declaration types must be interned before layout-dependent planning and lowering",
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
    !/\bpsx_bind_local_initializer_target_in\s*\(/.test(
      localInitializerBindingHeader,
    ) ||
    !/\bpsx_bind_local_initializer_target_in\s*\([^]*?psx_node_new_raw_decl_initializer_in\s*\(/.test(
      localInitializerBindingSource,
    ) ||
    !/\bpsx_bind_local_initializer_target_in\s*\(/.test(
      declarationPipelineSource,
    )) {
  throw new Error(
    "parser declarations must leave local initializer target binding to the semantic declaration pipeline",
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
if (!/\bps_lowering_type_size\s*\(/.test(loweringRuntimeHeader) ||
    !/\bps_lowering_type_id_size\s*\(/.test(loweringRuntimeHeader) ||
    !/\bps_lowering_type_id_alignment\s*\(/.test(loweringRuntimeHeader) ||
    !/\bps_lowering_type_deref_size\s*\(/.test(loweringRuntimeHeader) ||
    !/\bps_lowering_type_alignment\s*\(/.test(loweringRuntimeHeader) ||
    !/\bps_type_sizeof_id\s*\(/.test(
      loweringRuntimeSource,
    ) ||
    !/\bps_type_alignof_id\s*\(/.test(
      loweringRuntimeSource,
    ) ||
    !/\bps_type_sizeof_id\s*\(/.test(hirIrBuilder) ||
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
const typeIdentityImplementationSource = await readFile(
  "src/semantic/type_identity.c",
  "utf8",
);
if (!/\bscalar\s*\[\s*AG_TARGET_SCALAR_COUNT\s*\]/.test(targetInfoHeaderSource) ||
    !/\bpointer_alignment\s*;/.test(targetInfoHeaderSource) ||
    !/\bag_target_info_scalar_size\s*\(/.test(targetInfoHeaderSource) ||
    !/\bag_target_info_scalar_alignment\s*\(/.test(targetInfoHeaderSource) ||
    !/\bag_target_info_equal\s*\(/.test(targetInfoHeaderSource) ||
    !/target\s*&&\s*target->pointer_size\s*>\s*0/.test(
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
    !/\bag_target_info_equal\s*\(/.test(recordLayoutImplementationSource) ||
    /\bps_type_clear_(?:cached_layout|record_layout_cache)\s*\(/.test(
      typeSource +
        typeIdentityImplementationSource +
        await readFile("src/semantic/declaration_resolution.c", "utf8"),
    )) {
  throw new Error(
    "scalar size and alignment must be selected by TargetSpec instead of semantic type caches",
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

const canonicalTypeSource = await readFile("src/parser/type.c", "utf8");
const targetIntegerConversionSection = canonicalTypeSource.match(
  /static\s+int\s+integer_rank_size\s*\([^]*?static\s+ag_target_scalar_kind_t\s+floating_target_kind/,
);
if (!targetIntegerConversionSection ||
    /\bps_type_sizeof\s*\(|->(?:size|align)\b/.test(
      targetIntegerConversionSection[0],
    ) ||
    !/\bag_target_info_scalar_size\s*\(/.test(
      targetIntegerConversionSection[0],
    ) ||
    !/\bps_type_binary_result_for_target_in\s*\([^]*?ps_ctx_target_info\s*\(/.test(
      expressionOperandResolutionSource,
    ) ||
    !/\bps_type_conditional_result_for_target_in\s*\([^]*?ps_ctx_target_info\s*\(/.test(
      expressionOperandResolutionSource,
    )) {
  throw new Error(
    "semantic arithmetic conversion must use scalar rank and explicit TargetSpec instead of cached type layout",
  );
}
const targetCanonicalSignatureSection = canonicalTypeSource.match(
  /static\s+void\s+canonical_sig_type\s*\([^]*?int\s+ps_type_format_canonical_signature_for_target\s*\([^]*?\n\}/,
);
const typeIdCanonicalSignatureSource = await readFile(
  "src/type_signature.c",
  "utf8",
);
if (!targetCanonicalSignatureSection ||
    /\bps_type_sizeof\s*\(/.test(targetCanonicalSignatureSection[0]) ||
    !/\bconst\s+ag_target_info_t\s*\*target\b/.test(
      targetCanonicalSignatureSection[0],
    ) ||
    !/\bag_target_info_scalar_size\s*\(/.test(
      targetCanonicalSignatureSection[0],
    ) ||
    /parser\/type\.h|\bpsx_semantic_type_table_lookup\s*\(/.test(
      typeIdCanonicalSignatureSource,
    ) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      typeIdCanonicalSignatureSource,
    ) ||
    !/\bag_target_info_scalar_size\s*\(/.test(
      typeIdCanonicalSignatureSource,
    ) ||
    !/\bpsx_format_canonical_type_signature\s*\([^]*?options->target/.test(
      hirIrBuilder,
    )) {
  throw new Error(
    "canonical C signatures must derive ABI widths from explicit TargetSpec",
  );
}
const scalarIdentityNormalizer = canonicalTypeSource.match(
  /void\s+ps_type_normalize_scalar_identity\s*\([^]*?\n\}/,
);
if (!/static\s+psx_integer_kind_t\s+integer_kind_from_token\s*\(\s*token_kind_t\s+token_kind\s*\)/.test(
      canonicalTypeSource,
    ) ||
    !scalarIdentityNormalizer ||
    /->(?:size|align)\b/.test(scalarIdentityNormalizer[0])) {
  throw new Error(
    "scalar TypeId identity must follow C type kind instead of cached target layout",
  );
}
const tagIdentityFunction = canonicalTypeSource.match(
  /int\s+ps_type_tag_identity_matches\s*\([^]*?\n\}/,
);
if (!tagIdentityFunction ||
    !/ps_type_record_id\s*\(\s*a\s*\)/.test(tagIdentityFunction[0]) ||
    !/ps_type_record_id\s*\(\s*b\s*\)/.test(tagIdentityFunction[0])) {
  throw new Error("tag identity must prefer stable RecordId values");
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
const semanticTypeShapeHeader = await readFile(
  "src/type_system/type_shape.h",
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
    /#include\s+"\.\.\/semantic\/type_identity\.h"/.test(astSource) ||
    !/relation\.qualifiers\s*==\s*ps_type_qualifiers\s*\(\s*candidate\s*\)/.test(
      semanticTypeIdentitySource,
    ) ||
    /#include\s+"\.\.\/(?:target_info|type_layout)\.h"/.test(
      semanticTypeIdentitySource,
    ) ||
    /->(?:size|align)\b/.test(semanticTypeIdentitySource)) {
  throw new Error(
    "QualType must pair an interned TypeId with qualifiers, independent of target layout",
  );
}
if (!/table->entries\[id\]\.type\s*=\s*canonical\s*;[^]*?table->next_id\s*=\s*id\s*;[^]*?populate_type_relations\s*\(\s*table\s*,\s*id\s*,\s*type\s*\)/.test(
      semanticTypeIdentitySource,
    ) ||
    !/populate_type_relations_body\s*\([^]*?psx_record_decl_table_lookup\s*\([^]*?record->member_count[^]*?psx_semantic_type_table_intern\s*\(/.test(
      semanticTypeIdentitySource,
    ) ||
    !/psx_semantic_type_table_intern\s*\([^]*?ps_type_clone_in\s*\(\s*table->arena_context\s*,\s*type\s*\)/.test(
      semanticTypeIdentitySource,
    ) ||
    !/\bconst\s+psx_type_t\s*\*\s*psx_semantic_type_table_lookup\s*\(\s*const\s+psx_semantic_type_table_t\s*\*table\s*,\s*psx_type_id_t\s+type_id\s*\)\s*;/.test(
      semanticTypeIdentityHeader,
    ) ||
    !/\bps_type_remove_all_qualifiers_recursive\s*\(/.test(
      semanticTypeIdentitySource,
    ) ||
    !/\bsemantic_type_entry_matches\s*\(/.test(
      semanticTypeIdentitySource,
    ) ||
    /\bps_type_clone_for_identity_in\s*\(/.test(
      `${semanticTypeIdentitySource}\n${canonicalTypeSource}`,
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
    !/typedef\s+struct\s*\{[^]*?\}\s*psx_type_shape_t\s*;/.test(
      semanticTypeShapeHeader,
    ) ||
    /\b(?:qualifiers|size|alignment)\s*;/.test(semanticTypeShapeHeader) ||
    !/\brecord_tag_name\s*;/.test(semanticTypeShapeHeader) ||
    !/\benum_tag_name\s*;/.test(semanticTypeShapeHeader) ||
    !/\benum_tag_scope_depth_p1\s*;/.test(
      semanticTypeShapeHeader,
    )) {
  throw new Error(
    "semantic TypeId shape must own target-independent identity and resolve record relations through RecordDeclTable",
  );
}
const resolvedRecordIdentityGuard = semanticTypeIdentitySource.match(
  /static\s+int\s+semantic_type_has_resolved_record_identity\s*\([^]*?\n\}/,
);
const semanticTypeNodeMatcher = semanticTypeIdentitySource.match(
  /static\s+int\s+semantic_type_node_matches\s*\([^]*?\n\}/,
);
if (!resolvedRecordIdentityGuard ||
    !/\bps_type_record_id\s*\([^)]*\)\s*==\s*PSX_RECORD_ID_INVALID/.test(
      resolvedRecordIdentityGuard[0],
    ) ||
    !semanticTypeNodeMatcher ||
    !/canonical->record_id\s*!=\s*PSX_RECORD_ID_INVALID/.test(
      semanticTypeIdentitySource,
    ) ||
    !/psx_semantic_type_table_find\s*\([^]*?semantic_type_has_resolved_record_identity\s*\(\s*type\s*\)/.test(
      semanticTypeIdentitySource,
    ) ||
    !/psx_semantic_type_table_intern\s*\([^]*?semantic_type_has_resolved_record_identity\s*\(\s*type\s*\)/.test(
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
    !/\bpsx_qual_type_t\s*\*\s*record_member_types\s*;/.test(
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
    "interned TypeIds must retain recursive base, parameter, and record member QualType relationships",
  );
}

const semanticContextSource = await readFile(
  "src/parser/semantic_ctx.h",
  "utf8",
);
if (!/\bconst\s+psx_record_decl_t\s*\*\s*ps_ctx_ensure_tag_record_decl_in\s*\(/.test(
      semanticContextSource,
    ) ||
    !/\bconst\s+psx_record_decl_t\s*\*\s*ps_ctx_get_record_decl_in\s*\(/.test(
      semanticContextSource,
    ) ||
    !/\bps_ctx_register_record_members_in\s*\(/.test(
      semanticContextSource,
    ) ||
    !/\bpsx_qual_type_t\s+ps_ctx_intern_qual_type_in\s*\(/.test(
      semanticContextSource,
    ) ||
    !/\bpsx_qual_type_t\s+ps_ctx_find_interned_qual_type_in\s*\(/.test(
      semanticContextSource,
    ) ||
    !/\bconst\s+psx_type_t\s*\*\s*ps_ctx_type_by_id_in\s*\(/.test(
      semanticContextSource,
    ) ||
    /\bps_ctx_type_(?:size|align)of_in\s*\(/.test(
      `${semanticContextSource}\n${parserSemanticContextImplementation}`,
    ) ||
    !/\bps_type_sizeof_id\s*\(/.test(
      parserSemanticContextImplementation,
    ) ||
    !/\bps_type_alignof_id\s*\(/.test(
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
}
const recordIdBinder = parserSemanticContextImplementation.match(
  /void\s+ps_ctx_bind_record_ids_in\s*\([^]*?\n\}/,
);
const sourcesWithLegacyRecordOwnership = [];
for (const path of allSourceFiles) {
  const source = await readFile(path, "utf8");
  if (/\b(?:ps_ctx_attach_aggregate_definitions_in|ps_ctx_get_tag_definition_in|psx_aggregate_definition_t)\b/.test(
        source,
      )) {
    sourcesWithLegacyRecordOwnership.push(path);
  }
}
if (sourcesWithLegacyRecordOwnership.length > 0 ||
    !/\bvoid\s+ps_ctx_bind_record_ids_in\s*\(/.test(
      semanticContextSource,
    ) ||
    !recordIdBinder ||
    !/type->record_id\s*=\s*record_id/.test(recordIdBinder[0]) ||
    !/ps_ctx_resolve_tag_record_id_in\s*\(/.test(recordIdBinder[0]) ||
    !/psx_type_owned_base_mut\s*\(/.test(recordIdBinder[0]) ||
    !/psx_type_owned_param_mut\s*\(/.test(recordIdBinder[0]) ||
    !/i\s*<\s*type->param_count/.test(recordIdBinder[0])) {
  throw new Error(
    "semantic type normalization must bind RecordId recursively through pointer-free semantic types",
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
    ) ||
    /\bpsx_semantic_expr_id_t\b/.test(canonicalTypeStruct[1])) {
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
    !/parameter_storage_size\s*=\s*type_size\s*\(/.test(
      parameterVlaLoweringFunction[0],
    ) ||
    !/parameter_alignment\s*=\s*ps_lowering_type_alignment\s*\(/.test(
      parameterVlaLoweringFunction[0],
    ) ||
    /request->name_len\s*,\s*8\b/.test(parameterVlaLoweringFunction[0]) ||
    /ps_decl_find_lvar_in\s*\(/.test(parameterVlaLoweringFunction[0])) {
  throw new Error(
    "parameter VLA bounds must cross semantic/lowering by expression identity and use target layout",
  );
}
const vlaGeneratedSemanticNodeRe =
  /\bps_(?:node_new_binary_for_target_in|node_new_num_in|node_new_assign_in|node_new_lvar_typed_in)\s*\(/;
if (!/typedef\s+struct\s*\{[^]*?const\s+psx_typed_hir_tree_t\s*\*\s*expression\s*;[^]*?long\s+long\s+constant_value\s*;[^]*?is_constant\s*;[^]*?\}\s*psx_vla_runtime_dimension_t\s*;/.test(
      vlaRuntimePlanHeaderSource,
    ) ||
    !/typedef\s+struct\s+psx_vla_runtime_plan_t\s*\{[^]*?psx_vla_runtime_dimension_t\s*\*\s*dimensions\s*;[^]*?psx_qual_type_t\s+constant_qual_type\s*;[^]*?\bstride_store_offsets\s*;[^]*?\bstride_start_dimensions\s*;[^]*?\bperforms_allocation\s*;[^]*?\}\s*psx_vla_runtime_plan_t\s*;/.test(
      vlaRuntimePlanHeaderSource,
    ) ||
    vlaGeneratedSemanticNodeRe.test(vlaLoweringSource) ||
    !/\bps_node_new_vla_runtime_in\s*\(/.test(vlaLoweringSource) ||
    !/psx_syntax_node_kind_is_valid\s*\(\s*source->kind\s*\)/.test(
      resolutionWorkTree,
    ) ||
    /\b(?:ND_VLA_ALLOC|clone_vla_runtime_plan)\b/.test(
      resolutionWorkTree,
    ) ||
    !/PSX_HIR_EDGE_VLA_DIMENSION/.test(hirHeader) ||
    !/vla_runtime_store_offsets/.test(hirInternalHeader) ||
    !/materialize_vla_runtime\s*\([^]*?psx_semantic_node_builder_vla_runtime/.test(
      resolvedTreeMaterialization,
    ) ||
    !/psx_semantic_node_builder_vla_runtime\s*\([^]*?PSX_HIR_EDGE_VLA_DIMENSION[^]*?vla_runtime_store_offsets/.test(
      semanticNodeBuilderSource,
    ) ||
    /build_node\s*\(\s*builder\s*,\s*plan->dimensions/.test(
      resolvedTreeMaterialization,
    ) ||
    !/dimension->is_constant[^]*?PSX_HIR_NUMBER[^]*?dimension->expression->root/.test(
      semanticNodeBuilderSource,
    ) ||
    /plan->dimensions/.test(semanticPassSource) ||
    /plan->dimensions/.test(identifierBindingSource) ||
    /plan->dimensions/.test(semanticLoweringPassSource) ||
    /plan->dimensions/.test(lvarUsageAnalysisSource) ||
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
if (!/PSX_VLA_RUNTIME_SLOT_SIZE\s*=\s*8\b/.test(
      vlaRuntimeHeaderSource,
    ) ||
    !/PSX_VLA_RUNTIME_DESCRIPTOR_HEADER_SIZE/.test(frameLayoutSource) ||
    !/PSX_VLA_RUNTIME_SLOT_SIZE/.test(frameLayoutSource) ||
    !/PSX_VLA_RUNTIME_SLOT_SIZE/.test(vlaLoweringSource) ||
    !/pointer_stride_value\s*\([^]*?vla_stride_slot_size/.test(
      hirIrBuilder,
    ) ||
    !/source->kind\s*==\s*ND_SUBSCRIPT[^]*?PSX_VLA_RUNTIME_SLOT_SIZE/.test(
      resolvedTreeMaterialization,
    ) ||
    !/PSX_VLA_RUNTIME_SLOT_SIZE/.test(typeQueryResolutionSource) ||
    !/AG_TARGET_SCALAR_LONG_LONG[^]*?PSX_VLA_RUNTIME_SLOT_SIZE/.test(
      parameterDeclarationResolutionSource,
    ) ||
    !/ps_type_new_integer_kind_in\s*\([^]*?PSX_INTEGER_KIND_LONG_LONG/.test(
      parameterDeclarationResolutionSource,
    ) ||
    /\b8\s*\*\s*(?:count|level|stride_count|subscript_depth)/.test(
      frameLayoutSource + vlaLoweringSource + typeQueryResolutionSource +
        hirIrBuilder,
    )) {
  throw new Error(
    "VLA runtime descriptor ABI must be explicit and independent from C target layout",
  );
}
if (!/static\s+int\s+semantic_bind_address_result_type\s*\([^]*?ps_ctx_intern_pointer_to_qual_type_in\s*\(/.test(
      semanticPassSource,
    ) ||
    !/psx_semantic_type_table_intern_pointer_to\s*\([^]*?base\.type_id\s*==\s*pointee\.type_id[^]*?base\.qualifiers\s*==\s*pointee\.qualifiers/.test(
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
    !/\bpsx_type_id_t\s+type_id\s*;/.test(
      localDeclarationResolutionSource,
    ) ||
    /\bconst\s+psx_type_t\s*\*\s*type\s*;/.test(
      localDeclarationResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_lookup\s*\(/.test(
      localDeclarationResolutionImplementation,
    ) ||
    !/\bps_type_sizeof_id\s*\(/.test(
      localDeclarationResolutionImplementation,
    ) ||
    /\bps_type_(?:size|align)of_id_for_target\s*\(/.test(
      localDeclarationResolutionImplementation,
    ) ||
    /\bps_type_(?:size|align)of_for_target\s*\(/.test(
      localDeclarationResolutionImplementation,
    )) {
  throw new Error(
    "local declaration resolution must derive layout from TypeId and record layouts",
  );
}

const lvarInternalSource = await readFile("src/parser/lvar_internal.h", "utf8");
const lvarStruct = lvarInternalSource.match(/struct lvar_t\s*\{([\s\S]*?)\n\};/);
const symtabSource = await readFile("src/parser/symtab.h", "utf8");
const gvarStruct = symtabSource.match(
  /struct global_var_t\s*\{([\s\S]*?)\n\};/,
);
const lvarDeclTypeViewFunction = nodeUtilsSource.match(
  /static\s+const\s+psx_type_t\s*\*lvar_decl_type_consistent\s*\([^)]*\)\s*\{[^]*?\n\}/,
);
const gvarDeclTypeViewFunction = nodeUtilsSource.match(
  /static\s+const\s+psx_type_t\s*\*gvar_decl_type_consistent\s*\([^)]*\)\s*\{[^]*?\n\}/,
);
const lvarPublicSource = await readFile("src/parser/lvar_public.h", "utf8");
const gvarPublicSource = await readFile("src/parser/gvar_public.h", "utf8");
if (!lvarStruct ||
    /\bpsx_type_t\s*\*\s*decl_type\s*;/.test(lvarStruct[1]) ||
    !gvarStruct ||
    /\bpsx_type_t\s*\*\s*decl_type\s*;/.test(gvarStruct[1]) ||
    !/\bconst\s+psx_type_t\s*\*\s*ps_lvar_get_decl_type\s*\(\s*const\s+lvar_t\s*\*/.test(
      lvarPublicSource,
    ) ||
    !/\bconst\s+psx_type_t\s*\*\s*ps_gvar_get_decl_type\s*\(\s*const\s+global_var_t\s*\*/.test(
      gvarPublicSource,
    )) {
  throw new Error(
    "symbols must own only QualType identity while canonical types remain const views",
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
    !/\bstatic\s+psx_qual_type_t\s+resolve_local_decl_type\s*\(/.test(
      localRegistrySource,
    ) ||
    !/\bpsx_semantic_type_table_lookup_qual_type\s*\(/.test(
      localRegistrySource,
    ) ||
    !/\bpsx_semantic_type_table_find\s*\(/.test(localRegistrySource) ||
    /\bps_type_clone_persistent\s*\(/.test(localRegistrySource) ||
    !/\bps_local_registry_bind_semantic_types\s*\(/.test(
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
    !lvarDeclTypeViewFunction ||
    !gvarDeclTypeViewFunction ||
    !/psx_semantic_type_table_lookup_qual_type\s*\([^]*?decl_type_table[^]*?decl_qual_type/.test(
      lvarDeclTypeViewFunction[0],
    ) ||
    /return\s+var->decl_type\s*;/.test(lvarDeclTypeViewFunction[0]) ||
    !/psx_semantic_type_table_lookup_qual_type\s*\([^]*?decl_type_table[^]*?decl_qual_type/.test(
      gvarDeclTypeViewFunction[0],
    ) ||
    /return\s+gv->decl_type\s*;/.test(gvarDeclTypeViewFunction[0]) ||
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
    !/\bps_gvar_get_decl_type\s*\(/.test(staticLocalLoweringSource)) {
  throw new Error(
    "production symbol type views must materialize from declaration QualType identity",
  );
}
if (!recordMemberDeclStruct ||
    !/decl_type_table/.test(recordMemberDeclStruct[1]) ||
    !/decl_qual_type/.test(recordMemberDeclStruct[1]) ||
    /\bpsx_type_t\b/.test(recordMemberDeclStruct[1]) ||
    !/psx_record_member_decl_type\s*\([^]*?psx_semantic_type_table_lookup_qual_type\s*\(/.test(
      parserTypeImplementationSource,
    ) ||
    /return\s+member->decl_type\s*;/.test(parserTypeImplementationSource) ||
    !/ps_ctx_intern_qual_type_in\s*\([^]*?m->declaration\.qual_type\s*=\s*identity/.test(
      parserSemanticContextImplementation,
    ) ||
    !/qualified_views\s*\[PSX_QUALIFIER_VIEW_COUNT\]/.test(
      semanticTypeIdentitySource,
    )) {
  throw new Error(
    "record member type views must be materialized exclusively from QualType",
  );
}
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
    !/\bstatic\s+psx_qual_type_t\s+resolve_global_decl_type\s*\(/.test(
      globalRegistrySource,
    ) ||
    !/\bpsx_semantic_type_table_lookup_qual_type\s*\(/.test(
      globalRegistrySource,
    ) ||
    !/\bpsx_semantic_type_table_find\s*\(/.test(globalRegistrySource) ||
    /\bps_type_clone_persistent\s*\(/.test(globalRegistrySource) ||
    !/\bps_global_registry_bind_decl_qual_type\s*\(/.test(
      globalRegistrySource,
    ) ||
    !/\bps_global_registry_bind_semantic_types\s*\(/.test(
      compilationSessionSource,
    )) {
  throw new Error(
    "global symbols must retain their declaration QualType from the compilation unit semantic type table",
  );
}

const globalDeclarationResolutionHeader = await readFile(
  "src/semantic/global_declaration_resolution.h",
  "utf8",
);
if (!/\bpsx_qual_type_t\s+declaration_qual_type\s*;/.test(
      globalDeclarationResolutionHeader,
    ) ||
    !/\bps_ctx_intern_qual_type_in\s*\(/.test(
      globalDeclarationResolutionSource,
    ) ||
    !/\bps_global_registry_bind_decl_qual_type\s*\(/.test(
      await readFile("src/lowering/global_object_lowering.c", "utf8"),
    )) {
  throw new Error(
    "global declaration resolution must pass an interned QualType identity to lowering",
  );
}

const staticInitializerSource = await readFile(
  "src/semantic/static_initializer_resolution.h",
  "utf8",
);
const staticInitializerRequest = staticInitializerSource.match(
  /typedef struct\s*\{([\s\S]*?)\}\s*psx_static_initializer_resolution_request_t\s*;/,
);
const staticInitializerResolution = staticInitializerSource.match(
  /typedef struct\s*\{((?:(?!typedef struct)[\s\S])*?)\}\s*psx_static_initializer_resolution_t\s*;/,
);
if (!staticInitializerRequest ||
    !/\bconst\s+psx_type_t\s*\*\s*type\s*;/.test(
      staticInitializerRequest[1],
    ) ||
    !staticInitializerResolution ||
    !/\bpsx_qual_type_t\s+object_qual_type\s*;/.test(
      staticInitializerResolution[1],
    ) ||
    /\bpsx_type_t\b/.test(staticInitializerResolution[1])) {
  throw new Error(
    "static initializer resolution must consume a read-only construction type and return only canonical QualType identity",
  );
}

const readonlyTypeFields = [
  ["src/semantic/declaration_application.h", "psx_declaration_phase_t", "base_type"],
  ["src/declaration_pipeline.h", "psx_global_declaration_pipeline_request_t", "type"],
  ["src/declaration_pipeline.h", "psx_static_local_declaration_pipeline_request_t", "type"],
  ["src/declaration_pipeline.h", "psx_automatic_local_declaration_pipeline_request_t", "type"],
  ["src/declaration_pipeline.h", "psx_block_extern_declaration_pipeline_request_t", "type"],
  ["src/declaration_pipeline.h", "psx_temporary_local_declaration_pipeline_request_t", "type"],
  ["src/declaration_pipeline.h", "psx_function_definition_pipeline_result_t", "function_type"],
  ["src/lowering/local_object_lowering.h", "psx_local_object_request_t", "type"],
  ["src/lowering/static_local_lowering.h", "psx_static_local_declaration_request_t", "type"],
];
const readonlyTypeSources = new Map();
for (const [file, typeName, fieldName] of readonlyTypeFields) {
  let source = readonlyTypeSources.get(file);
  if (!source) {
    source = await readFile(file, "utf8");
    readonlyTypeSources.set(file, source);
  }
  const body = source.match(
    new RegExp(`typedef struct\\s*\\{([\\s\\S]*?)\\}\\s*${typeName}\\s*;`),
  );
  const field = new RegExp(
    `\\bconst\\s+psx_type_t\\s*\\*\\s*${fieldName}\\s*;`,
  );
  if (!body || !field.test(body[1])) {
    throw new Error(`${typeName}.${fieldName} must be a const type view`);
  }
}

const readonlySemanticTypeResults = [
  ["src/semantic/declaration_resolution.h", "psx_resolve_decl_type"],
  ["src/semantic/declaration_resolution.h", "psx_resolve_decl_specifier_syntax_in_context"],
  ["src/semantic/declaration_application.h", "psx_apply_parsed_decl_specifier_in_contexts"],
  ["src/semantic/declaration_application.h", "psx_apply_parsed_type_name_in_contexts"],
  ["src/semantic/declaration_application.h", "psx_apply_parsed_declarator_type_in_contexts"],
  ["src/semantic/declaration_application.h", "psx_apply_runtime_declarator_type_in_context"],
  ["src/semantic/function_call_resolution.h", "psx_resolve_function_reference_type"],
  ["src/parser/node_utils.h", "ps_node_array_decay_pointer_arith_type_in"],
];
for (const [file, functionName] of readonlySemanticTypeResults) {
  const source = await readFile(file, "utf8");
  const signature = new RegExp(
    `\\bconst\\s+psx_type_t\\s*\\*\\s*${functionName}\\s*\\(`,
  );
  if (!signature.test(source)) {
    throw new Error(`${functionName} must publish a const type view`);
  }
}

const declarationTypeBuilderUsers = new Set([
  "src/semantic/declaration_type_builder.h",
  "src/semantic/declaration_resolution.c",
  "src/semantic/declaration_application.c",
  "src/semantic/type_name_resolution.c",
  "src/semantic/type_query_resolution.c",
  "src/semantic/aggregate_member_resolution.c",
  "src/semantic/parameter_declaration_resolution.c",
]);
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

const staticDataInitializerHeader = await readFile(
  "src/lowering/static_data_initializer.h",
  "utf8",
);
const staticDataInitializerSource = await readFile(
  "src/lowering/static_data_initializer.c",
  "utf8",
);
const initializerLoweringSource = await readFile(
  "src/lowering/initializer_lowering.c",
  "utf8",
);
const initializerResolutionSource = await readFile(
  "src/semantic/initializer_resolution.c",
  "utf8",
);
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
    /\bdirect_member\b/.test(initializerLoweringSource) ||
    /\bdirect_member\b/.test(staticDataInitializerSource) ||
    /\bps_node_new_tag_member_lvar_ref_for_in\s*\(/.test(
      initializerLoweringSource,
    )) {
  throw new Error(
    "initializer member identity and placement must remain explicit and separate",
  );
}
if (/\bmember->offset\b/.test(initializerLoweringSource) ||
    /\b(?:first|selected|container)->offset\b/.test(
      initializerLoweringSource,
    ) ||
    /\baggregate_definition\b/.test(initializerLoweringSource) ||
    /\bmember->offset\b/.test(staticDataInitializerSource) ||
    /\baggregate_definition\b/.test(staticDataInitializerSource) ||
    !/\bpsx_record_decl_table_lookup\s*\(/.test(
      initializerLoweringSource,
    ) ||
    !/\bpsx_record_decl_table_lookup\s*\(/.test(
      staticDataInitializerSource,
    ) ||
    !/\brecord_member_offset\s*\(/.test(initializerLoweringSource) ||
    !/\brecord_member_offset\s*\(/.test(staticDataInitializerSource)) {
  throw new Error(
    "initializer lowering must resolve member offsets from RecordLayoutTable",
  );
}
if (/\baggregate_definition\b/.test(initializerResolutionSource) ||
    !/\bpsx_record_decl_table_lookup\s*\(/.test(
      initializerResolutionSource,
    )) {
  throw new Error(
    "initializer semantics must resolve aggregate declarations through RecordDeclTable",
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
    !/\bps_lowering_context_bind_record_decls\s*\(/.test(
      loweringRuntimeSource,
    ) ||
    !/\bps_lowering_record_decls\s*\(/.test(loweringRuntimeSource) ||
    !/\bps_lowering_context_bind_record_decls\s*\(/.test(
      compilationSessionSource,
    )) {
  throw new Error(
    "RecordDeclTable must be an explicit semantic-to-lowering phase input",
  );
}
for (const functionName of [
  "lower_static_object_initializer",
  "lower_static_scalar_array_initializer",
]) {
  const signature = new RegExp(
    `\\b${functionName}\\s*\\([\\s\\S]*?\\bconst\\s+psx_type_t\\s*\\*\\s*type\\b[\\s\\S]*?\\)\\s*;`,
  );
  if (!signature.test(staticDataInitializerHeader)) {
    throw new Error(`${functionName} must consume a const type view`);
  }
}
if (!/\bpsx_type_id_t\s+type_id\s*=\s*ps_gvar_decl_type_id\s*\(\s*global\s*\)\s*;[^]*?\btype_size_id\s*\(\s*lowering\s*,\s*type_id\s*\)/.test(
      translationUnitDataLoweringSource,
    ) ||
    !/\bpsx_type_id_t\s+type_id\s*=\s*ps_gvar_decl_type_id\s*\(\s*ctx->global\s*\)\s*;/.test(
      translationUnitDataLoweringSource,
    ) ||
    !/\bpsx_collect_initializer_scalar_leaves_with_records\s*\([\s\S]*?\bps_gvar_decl_type_id\s*\(\s*global\s*\)\s*,\s*0\s*,/.test(
      staticDataInitializerSource,
    )) {
  throw new Error(
    "global data and static initializer root layout must consume the symbol declaration TypeId",
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
if (!aggregateWalkerLayoutSection ||
    /->\s*aggregate_definition\b/.test(nodeUtilsSource) ||
    !/\bpsx_record_decl_table_lookup\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !/\bps_type_sizeof_id\s*\(/.test(
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
    /\bps_type_sizeof_for_target\s*\(|\bps_tag_member_decl_value_size\s*\(/.test(
      aggregateWalkerLayoutSection[0],
    ) ||
    !/\bps_gvar_walk_resolved_aggregate_initializer\s*\(\s*const\s+psx_semantic_type_table_t\s*\*[^,]*,\s*const\s+psx_record_decl_table_t\s*\*[^,]*,\s*const\s+psx_record_layout_table_t\s*\*[^,]*,\s*const\s+ag_target_info_t\s*\*[^,]*,\s*psx_type_id_t\s+root_type_id/.test(
      gvarPublicSource,
    ) ||
    /\bps_gvar_storage_size\s*\(|\bsymbol_alignment\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    !/\bps_type_(?:size|align)of_id\s*\(/.test(
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
    "aggregate walking and IR symbol layout must consume TypeId and explicit TargetSpec",
  );
}
const typedInitializerSection = initializerLoweringSource.match(
  /static\s+node_t\s*\*append_typed_object_zero_fill\s*\([^]*?static\s+node_t\s*\*lower_struct_list_initializer/,
);
if (!typedInitializerSection ||
    /\btype_id\s*\(\s*context\s*,/.test(typedInitializerSection[0]) ||
    !/\bps_lvar_decl_type_id\s*\(/.test(initializerLoweringSource) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      typedInitializerSection[0],
    ) ||
    !/\bpsx_semantic_type_table_record_member\s*\(/.test(
      typedInitializerSection[0],
    )) {
  throw new Error(
    "typed local initializer lowering must traverse declaration, base, and member TypeIds without type pointer reverse lookup",
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
    !/source->alignas_specifier_count[^]*?alignas->expression\s*=\s*clone_node[^]*?alignas->type_name\s*=\s*clone_type_name_syntax/.test(
      resolutionWorkTree,
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
  "src/parser/alignas_value.c",
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
    !/#include\s+"semantic_ctx\.h"/.test(parserLegacySource) ||
    !/psx_consume_type_kind_in_contexts\s*\(/.test(
      parserLegacySource,
    ) ||
    !/ps_expr_in_contexts\s*\(/.test(parserLegacySource)) {
  throw new Error(
    "parser core must depend on NameClassifier and syntax services; semantic-context wrappers belong in parser_legacy.c",
  );
}

const parsedNumberLiteral = parserExpressionSource.match(
  /static\s+node_t\s*\*parse_num_literal\s*\([^]*?\n\}\n\n\/\/ 内容文字列/,
);
const parsedStringLiteral = parserExpressionSource.match(
  /static\s+node_string_t\s*\*make_string_lit_node\s*\([^]*?\n\}\n\n\/\/ C11/,
);
const stringNodeStruct = astSource.match(
  /struct node_string_t\s*\{([^{}]*)\};/,
);
const syntaxIntConstructor = nodeUtilsSource.match(
  /node_t\s*\*psx_node_new_syntax_int_in\s*\([^]*?\n\}\n\nnode_t\s*\*ps_node_new_num_in/,
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
    !/\bsemantic_resolve_number_literal\s*\(/.test(semanticPassSource) ||
    !/\bsemantic_resolve_string_literal\s*\(/.test(semanticPassSource) ||
    !/case\s+ND_NUM\s*:[^]*?semantic_resolve_number_literal\s*\(/.test(
      semanticPassSource,
    ) ||
    !/case\s+ND_STRING\s*:[^]*?semantic_resolve_string_literal\s*\(/.test(
      semanticPassSource,
    ) ||
    /\bfval_id\b/.test(numberNodeStruct?.[1] ?? "") ||
    !stringNodeStruct ||
    /\bstring_label\b/.test(stringNodeStruct[1]) ||
    !/\bpsx_literal_resolution_state_t\s+literal\s*;/.test(
      nodeResolutionStateSource,
    ) ||
    !/\bpsx_string_literal_bind_label\s*\(/.test(
      literalResolutionHeader,
    ) ||
    !/\bps_node_resolution_state(?:_const)?\s*\(/.test(
      literalResolutionSource,
    ) ||
    !/\bpsx_string_literal_bind_label\s*\(/.test(
      semanticPassSource,
    ) ||
    /\bliteral->(?:fval_id|string_label)\b/.test(
      semanticPassSource,
    )) {
  throw new Error(
    "parser literals must remain typeless syntax while registry metadata lives in semantic resolution state",
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
    !/\bpsx_resolve_string_literal_semantics_in_contexts\s*\([^]*?\bpsx_register_string_lit_in\s*\(/.test(
      literalResolutionSource,
    )) {
  throw new Error(
    "expression parser must build typeless Syntax AST nodes without canonical type or literal-pool registration",
  );
}

if (!/psx_resolve_number_literal_semantics_in_contexts\s*\(/.test(
      semanticPassSource,
    ) ||
    !/psx_resolve_string_literal_semantics_in_contexts\s*\(/.test(
      semanticPassSource,
    ) ||
    !/psx_resolve_number_literal_semantics_in_contexts\s*\(/.test(
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
    "literal semantics must be a Syntax-preserving value result shared by direct Typed HIR and compatibility resolution",
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
      resolvedTreeMaterialization,
    ) ||
    /ps_type_(?:sizeof|alignof)_id\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    !/ps_type_sizeof_id\s*\(/.test(
      hirSymbolResolutionSource,
    ) ||
    !/ps_type_alignof_id\s*\(/.test(
      hirSymbolResolutionSource,
    )) {
  throw new Error(
    "identifier decay and global HIR layout must be canonical semantic values shared by direct and compatibility materialization",
  );
}

const compatibilityLocalHirSpecCalls =
  resolvedTreeMaterialization.match(
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
    compatibilityLocalHirSpecCalls.length < 2 ||
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

if (/\bnode_t\b|\bND_[A-Z0-9_]+\b|PSX_HIR_|parser\/ast\.h/.test(
      `${assignmentResolutionHeader}\n${assignmentResolutionSource}`,
    ) ||
    !/psx_resolve_assignment_qual_types_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_compound_assignment_qual_types_in\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_resolve_assignment_qual_types_in\s*\(/.test(
      assignmentValidationSource,
    ) ||
    !/psx_resolve_compound_assignment_qual_types_in\s*\(/.test(
      assignmentValidationSource,
    ) ||
    !/\bPSX_HIR_ASSIGN\b/.test(syntaxTypedHirResolutionSource) ||
    !/\bPSX_HIR_COMPOUND_ASSIGN\b/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/DIRECT_IDENTIFIER_USAGE_INITIALIZED/.test(
      syntaxTypedHirResolutionSource,
    )) {
  throw new Error(
    "assignment typing must be an AST-independent QualType rule shared by direct Typed HIR and compatibility diagnostics",
  );
}

const directCallBindingAdapter = identifierBindingSource.match(
  /static\s+void\s+bind_direct_call\s*\([^]*?\n\}/,
);
const sharedCallTypeRuleUses = [
  syntaxTypedHirResolutionSource,
  semanticPassSource,
].filter((source) =>
  /\bpsx_resolve_call_qual_types_in\s*\(/.test(source)
).length;
if (/\bnode_t\b|\bND_[A-Z0-9_]+\b|PSX_HIR_|parser\/ast\.h/.test(
      `${callResolutionHeader}\n${callResolutionSource}`,
    ) ||
    !/\bhas_function_prototype\b/.test(typeSource) ||
    !/canonical->has_function_prototype\s*==\s*[^;]*candidate->has_function_prototype/.test(
      typeIdentityImplementationSource,
    ) ||
    !/function\.has_function_prototype/.test(callResolutionSource) ||
    !/\bpsx_semantic_type_table_describe\s*\(/.test(
      callResolutionSource,
    ) ||
    /\bpsx_semantic_type_table_lookup\s*\(/.test(
      callResolutionSource,
    ) ||
    !/callable_semantic_type\.has_function_prototype/.test(
      hirIrBuilder,
    ) ||
    /enforce_empty_parameter_count/.test(
      `${callResolutionHeader}\n${callResolutionSource}\n${syntaxTypedHirResolutionSource}\n${semanticPassSource}`,
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
    /\bpsx_resolve_function_call_type\b|\bpsx_function_call_resolution_t\b|\bPSX_FUNCTION_CALL_RESOLUTION_/.test(
      `${functionCallResolutionHeader}\n${functionCallResolutionSource}\n${semanticPassSource}`,
    ) ||
    sharedCallTypeRuleUses !== 2 ||
    !/\bPSX_HIR_CALL\b/.test(syntaxTypedHirResolutionSource) ||
    !/\bPSX_HIR_EDGE_CALLEE\b/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/\bPSX_HIR_EDGE_ARGUMENT\b/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/resolution\.function_qual_type/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !directCallBindingAdapter ||
    /argument_count|param_count|is_variadic_function/.test(
      directCallBindingAdapter[0],
    )) {
  throw new Error(
    "function call typing must be one AST-independent QualType rule shared by direct Typed HIR and compatibility diagnostics",
  );
}

const controlExpressionQualTypeRule =
  expressionOperandResolutionSource.match(
    /void\s+psx_resolve_control_expression_qual_type_in\s*\([^]*?\n\}/,
  );
const sharedControlExpressionRuleUses = [
  syntaxTypedHirResolutionSource,
  semanticPassSource,
].filter((source) =>
  /\bpsx_resolve_control_expression_qual_type_in\s*\(/.test(source)
).length;
if (
  !controlExpressionQualTypeRule ||
  /\bnode_t\b|\bND_[A-Z0-9_]+\b|\bPSX_HIR_/.test(
    controlExpressionQualTypeRule[0],
  ) ||
  sharedControlExpressionRuleUses !== 2 ||
  !/PSX_CONTROL_EXPRESSION_NOT_SCALAR/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/PSX_CONTROL_EXPRESSION_NOT_INTEGER/.test(
    syntaxTypedHirResolutionSource,
  )
) {
  throw new Error(
    "control-expression typing must be an AST-independent QualType rule shared by direct Typed HIR and compatibility diagnostics",
  );
}

const returnQualTypeRule = assignmentResolutionSource.match(
  /void\s+psx_resolve_return_qual_types_in\s*\([^]*?\n\}/,
);
const sharedReturnTypeRuleUses = [
  syntaxTypedHirResolutionSource,
  loweredTreeValidationSource,
].filter((source) =>
  /\bpsx_resolve_return_qual_types_in\s*\(/.test(source)
).length;
if (
  !returnQualTypeRule ||
  /\bnode_t\b|\bND_[A-Z0-9_]+\b|\bPSX_HIR_/.test(
    returnQualTypeRule[0],
  ) ||
  sharedReturnTypeRuleUses !== 2 ||
  /\bpsx_resolve_return_qual_types_in\s*\(/.test(
    semanticPassSource,
  ) ||
  !/PSX_RETURN_TYPES_INCOMPATIBLE/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/PSX_RETURN_TYPES_DISCARDS_QUALIFIERS/.test(
    syntaxTypedHirResolutionSource,
  )
) {
  throw new Error(
    "return conversion must be an AST-independent QualType rule shared by direct Typed HIR and compatibility diagnostics",
  );
}

const addressOperandQualTypeRule =
  expressionOperandResolutionSource.match(
    /void\s+psx_resolve_address_operand_qual_type_in\s*\([^]*?\n\}/,
  );
const sharedAddressOperandRuleUses = [
  syntaxTypedHirResolutionSource,
  semanticPassSource,
].filter((source) =>
  /\bpsx_resolve_address_operand_qual_type_in\s*\(/.test(source)
).length;
const directAddressPreflight = syntaxTypedHirResolutionSource.match(
  /if\s*\(syntax->kind\s*==\s*ND_ADDR\s*&&\s*syntax->is_explicit_addr_expr\)\s*\{[^]*?\n\s*if\s*\(syntax->kind\s*==\s*ND_UNARY_NEGATE/,
);
if (
  !addressOperandQualTypeRule ||
  /\bnode_t\b|\bND_[A-Z0-9_]+\b|\bPSX_HIR_/.test(
    addressOperandQualTypeRule[0],
  ) ||
  sharedAddressOperandRuleUses !== 2 ||
  !/PSX_ADDRESS_OPERAND_REQUIRES_ADDRESSABLE_VALUE/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/PSX_ADDRESS_OPERAND_IS_BITFIELD/.test(
    syntaxTypedHirResolutionSource,
  ) ||
  !/int\s+ps_node_is_lvalue_in\s*\(/.test(nodeUtilsSource) ||
  !/if\s*\(\s*!ps_node_is_lvalue_in\s*\(/.test(nodeUtilsSource) ||
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
  ) ||
  /ビットフィールドのアドレスは取得できません/.test(
    semanticPassSource,
  )
) {
  throw new Error(
    "explicit address operands must use one AST-independent value-category and QualType rule in direct and compatibility resolution",
  );
}

const subscriptQualTypeCore = expressionOperandResolutionSource.match(
  /static\s+int\s+qual_type_is_subscript_base\s*\([^]*?void\s+psx_resolve_subscript_qual_types_in\s*\([^]*?\n\}/,
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
    !/ps_type_contains_vla_array\s*\(result_type\)/.test(
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
    !/declaration_scope[^]*?ps_ctx_enter_block_scope_in\s*\([^]*?ps_decl_enter_scope_in\s*\([^]*?preflight_direct_statement\s*\(\s*context,\s*control->init\s*\)/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/ps_decl_leave_scope_in\s*\([^]*?ps_ctx_leave_block_scope_in\s*\(/.test(
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
if (!/\bpsx_resolve_type_name_qual_type_in_contexts\s*\(/.test(
      typeNameResolutionHeader,
    ) ||
    !/\bpsx_resolve_bound_type_name_qual_type_in_contexts\s*\(/.test(
      typeNameResolutionHeader,
    ) ||
    !typeNameQualTypeValueAdapter ||
    !/psx_type_name_resolution_state_t\s+state\s*=\s*\{0\}/.test(
      typeNameQualTypeValueAdapter[0],
    ) ||
    !/psx_resolve_bound_type_name_qual_type_in_contexts\s*\(/.test(
      typeNameQualTypeValueAdapter[0],
    ) ||
    !/psx_resolve_bound_type_name_qual_type_in_contexts\s*\(/.test(
      semanticPassSource,
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
    "source cast type names must resolve to QualType values for direct Typed HIR while compatibility state uses the same core",
  );
}

const sharedOperandTypeRuleCores = [
  "resolve_arithmetic_unary_result_type_value",
  "resolve_binary_result_type_value",
  "resolve_conditional_result_type_value",
];
for (const core of sharedOperandTypeRuleCores) {
  const calls = expressionOperandResolutionSource.match(
    new RegExp(`\\b${core}\\s*\\(`, "g"),
  ) ?? [];
  if (calls.length !== 2) {
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
  /\bps_type_binary_result_for_target_in\s*\(/g,
) ?? [];
const conditionalTargetRuleCalls = expressionOperandResolutionSource.match(
  /\bps_type_conditional_result_for_target_in\s*\(/g,
) ?? [];
if (binaryTargetRuleCalls.length !== 1 ||
    conditionalTargetRuleCalls.length !== 1) {
  throw new Error(
    "operand result rules must be implemented once below the QualType and compatibility adapters",
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
    !/PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE/.test(
      semanticNodeBuilderSource,
    ) ||
    !/expression->qual_type\s*=\s*qual_type/.test(
      semanticNodeBuilderSource,
    ) ||
    !/psx_semantic_node_builder_expression\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    !/psx_semantic_node_builder_statement\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    !/materialize_typed_leaf\s*\([^]*?case\s+ND_NUM[^]*?case\s+ND_STRING[^]*?case\s+ND_LVAR[^]*?case\s+ND_GVAR[^]*?case\s+ND_FUNCREF[^]*?psx_semantic_node_builder_leaf_expression\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    /MAP\s*\(\s*ND_(?:NUM|STRING|LVAR|GVAR|FUNCREF)\s*,/.test(
      resolvedTreeMaterialization,
    ) ||
    !/build_node\s*\([^]*?materialize_typed_leaf\s*\([^]*?handled_typed_leaf[^]*?if\s*\(\s*handled_typed_leaf\s*\)\s*return\s+typed_leaf/.test(
      resolvedTreeMaterialization,
    ) ||
    /psx_semantic_expression_t\s*\*|->qual_type\s*=/.test(
      resolvedTreeMaterialization,
    )) {
  throw new Error(
    "Typed HIR construction must structurally require canonical QualType for expressions",
  );
}
if (!/PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS/.test(
      resolvedTreeMaterialization,
    )) {
  throw new Error(
    "resolved-tree HIR emission must reject unresolved syntax node kinds",
  );
}
if (!/MAP\s*\(\s*ND_UNARY_DEREF\s*,\s*PSX_HIR_DEREF\s*\)/.test(
      resolvedTreeMaterialization,
    ) ||
    /\blower_unary_deref_expression\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/unary_deref_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "typed unary dereference must materialize directly into Typed HIR without parser-shaped lowering",
  );
}
if (!/MAP\s*\(\s*ND_SUBSCRIPT\s*,\s*PSX_HIR_SUBSCRIPT\s*\)/.test(
      resolvedTreeMaterialization,
    ) ||
    !/\bPSX_HIR_SUBSCRIPT\b/.test(hirHeader) ||
    !/kind\s*==\s*PSX_HIR_SUBSCRIPT/.test(hirIrBuilder) ||
    !/\bsubscript_address\s*\(/.test(hirIrBuilder) ||
    /\blower_subscript_expression\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
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
    !/ps_type_sizeof_id\s*\(/.test(hirIrBuilder) ||
    /\blower_additive_expression\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/expr_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "pointer arithmetic must preserve source operators through Typed HIR and apply target stride in HIR lowering",
  );
}
if (!/MAP\s*\(\s*ND_MEMBER_ACCESS\s*,\s*PSX_HIR_MEMBER_ACCESS\s*\)/.test(
      resolvedTreeMaterialization,
    ) ||
    !/\bPSX_HIR_MEMBER_ACCESS\b/.test(hirHeader) ||
    !/\bmember_address\s*\(/.test(hirIrBuilder) ||
    !/kind\s*==\s*PSX_HIR_MEMBER_ACCESS/.test(hirIrBuilder) ||
    !/psx_hir_node_member_offset\s*\(/.test(hirIrBuilder) ||
    !/case\s+ND_MEMBER_ACCESS\s*:[^]*?node->lhs\s*=\s*lower_tree[^]*?break\s*;/.test(
      semanticLoweringPassSource,
    ) ||
    /\blower_member_access_expression(?:_in)?\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/member_access_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "typed member access must materialize directly into Typed HIR without parser-shaped lowering",
  );
}
if (!/\bPSX_HIR_COMPOUND_ASSIGN\b/.test(hirHeader) ||
    !/resolved_kind\s*==\s*ND_ASSIGN[^]*?is_source_compound_assignment[^]*?PSX_HIR_COMPOUND_ASSIGN/.test(
      resolvedTreeMaterialization,
    ) ||
    !/\bbuild_compound_assignment\s*\(/.test(hirIrBuilder) ||
    /\blower_compound_assignment_expression\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/assignment_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "compound assignment must preserve one lvalue evaluation directly through Typed HIR",
  );
}
if (!/MAP\s*\(\s*ND_UNARY_NEGATE\s*,\s*PSX_HIR_NEGATE\s*\)/.test(
      resolvedTreeMaterialization,
    ) ||
    !/\bPSX_HIR_NEGATE\b/.test(hirHeader) ||
    !/kind\s*==\s*PSX_HIR_NEGATE[^]*?build_complex_negate\s*\(/.test(
      hirIrBuilder,
    ) ||
    !/psx_hir_node_kind\s*\(\s*node\s*\)\s*==\s*PSX_HIR_NEGATE[^]*?build_scalar_negate\s*\(/.test(
      hirIrBuilder,
    ) ||
    /\blower_unary_negate_expression\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
    /\bND_FNEG\b/.test(resolvedNodeKindHeader) ||
    allSourceFiles.some(
      (path) =>
        /src\/lowering\/unary_operator_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "typed unary negate must remain distinct from syntax lowering and materialize as PSX_HIR_NEGATE",
  );
}
const directComplexComponentKindChecks =
  syntaxTypedHirResolutionSource.match(
    /syntax->kind\s*==\s*ND_CREAL\s*\|\|\s*\n\s*syntax->kind\s*==\s*ND_CIMAG/g,
  ) ?? [];
if (!/MAP\s*\(\s*ND_CREAL\s*,\s*PSX_HIR_CREAL\s*\)/.test(
      resolvedTreeMaterialization,
    ) ||
    !/MAP\s*\(\s*ND_CIMAG\s*,\s*PSX_HIR_CIMAG\s*\)/.test(
      resolvedTreeMaterialization,
    ) ||
    !/case\s+ND_CREAL\s*:\s*case\s+ND_CIMAG\s*:[^]*?node->lhs\s*=\s*lower_tree[^]*?break\s*;/.test(
      semanticLoweringPassSource,
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
    /\blower_complex_part_expression\s*\(/.test(
      semanticLoweringPassSource,
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
    !/resolved_kind\s*==\s*ND_GENERIC_SELECTION/.test(
      resolvedTreeMaterialization,
    ) ||
    !/psx_generic_selection_selected_expression_const\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    /typedef\s+struct\s*\{[^}]*\bselected_index\b[^}]*\}\s*node_generic_selection_t\s*;/.test(
      astHeader,
    ) ||
    /selection->associations\[selected\]\.expression\s*=\s*lower_tree/.test(
      semanticLoweringPassSource,
    ) ||
    /\blower_generic_selection_expression\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
    allSourceFiles.some(
      (path) =>
        /src\/lowering\/generic_selection_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "generic selection resolution must stay outside Syntax AST and materialize its selected expression directly into Typed HIR",
  );
}
if (!/MAP\s*\(\s*ND_ALIGNOF_QUERY\s*,\s*PSX_HIR_NUMBER\s*\)/.test(
      resolvedTreeMaterialization,
    ) ||
    !/psx_alignof_query_resolved_alignment\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    /\blower_alignof_query_expression\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
    allSourceFiles.some(
      (path) => /src\/lowering\/alignof_lowering\.[ch]$/.test(path),
    )) {
  throw new Error(
    "resolved alignof queries must materialize directly as Typed HIR numbers",
  );
}
if (!/resolved_kind\s*==\s*ND_SIZEOF_QUERY/.test(
      resolvedTreeMaterialization,
    ) ||
    !/materialize_sizeof_value\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    !/psx_sizeof_query_runtime_plan_const\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    !/PSX_HIR_MUL/.test(resolvedTreeMaterialization) ||
    !/runtime_size_slot/.test(
      resolvedTreeMaterialization,
    ) ||
    !/materialize_sizeof_vla_indices\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    !/psx_sizeof_runtime_plan_t/.test(typeQueryResolutionSource) ||
    !/runtime_bounds/.test(typeQueryResolutionSource) ||
    /\bps_node_new_[A-Za-z0-9_]*\s*\(/.test(
      typeQueryResolutionSource,
    ) ||
    /runtime_size_expr|resolved_size|runtime_size_slot|evaluates_vla_operand/.test(
      sizeofQueryNode?.[1] ?? "",
    ) ||
    /\blower_sizeof_query_expression\s*\(/.test(
      semanticLoweringPassSource,
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
    !/ps_local_registry_create_internal_storage_object_in\s*\(/.test(
      localObjectLoweringSource,
    ) ||
    !/ps_local_registry_create_internal_storage_object_in\s*\(/.test(
      castLoweringSource,
    )) {
  throw new Error(
    "direct compound literals must use Syntax-external QualType plans, internal automatic storage, and registered static global storage without perturbing source name binding",
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
const typedHirMaterializationBoundary = resolutionWorkTree.match(
  /int\s+psx_resolution_work_tree_materialize_typed_hir\s*\([^)]*\)\s*\{([^]*?)\n\}/,
  )?.[1] ?? "";
const typedHirMaterializationDeclaration = resolutionWorkTreeInternalHeader.match(
  /int\s+psx_resolution_work_tree_materialize_typed_hir\s*\(([^]*?)\);/,
)?.[1] ?? "";
const semanticMaterializationCalls =
  legacySyntaxDiagnosticsSource.match(
    /\bmaterialize_resolved_tree\s*\(/g,
  ) ?? [];
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
    "psx_resolve_syntax_initializer_direct_to_typed_hir_with_lowering_in_contexts",
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
    !/psx_resolve_syntax_initializer_direct_to_typed_hir_with_lowering_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionHeader,
    ) ||
    !/PSX_HIR_INITIALIZER_LIST/.test(syntaxTypedHirResolutionSource) ||
    !/PSX_HIR_INITIALIZER_ENTRY/.test(syntaxTypedHirResolutionSource) ||
    !/PSX_HIR_MEMBER_DESIGNATOR/.test(syntaxTypedHirResolutionSource) ||
    !/PSX_HIR_INDEX_DESIGNATOR/.test(syntaxTypedHirResolutionSource)) {
  throw new Error(
    "initializer resolution must build structural Typed HIR from immutable Syntax before direct diagnostics",
  );
}
if (!/designator->range_end_expr[^]*?range_end_value\s*<\s*index_value/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/PSX_HIR_EDGE_DESIGNATOR_RANGE_END/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/static\s+int\s+aggregate_entry_range\s*\(/.test(
      staticHirInitializerSource,
    ) ||
    !/PSX_HIR_EDGE_DESIGNATOR_RANGE_END/.test(
      staticHirInitializerSource,
    ) ||
    !/designator\s*==\s*range_override\s*\?\s*range_index/.test(
      staticHirInitializerSource,
    ) ||
    !/for\s*\(\s*long long index\s*=\s*range_begin\s*;\s*;\s*index\+\+\s*\)/.test(
      staticHirInitializerSource,
    ) ||
    !/static\s+int\s+flat_initializer_entry_ranges\s*\(/.test(
      initializerResolutionSource,
    ) ||
    !/static\s+int\s+flat_initializer_apply_designated_ranges\s*\(/.test(
      initializerResolutionSource,
    ) ||
    !/designator_index\s*\+\s*1/.test(initializerResolutionSource) ||
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
      constantExpressionSource,
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
  "psx_resolve_parsed_function_typed_hir_from_syntax_in_contexts",
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
    !/psx_resolve_incomplete_array_initializer_shape\s*\(/.test(
      declarationResolutionSource,
    ) ||
    !/resolve_direct_completed_array_type\s*\(/.test(
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
    /ps_prepare_constant_declarator_expressions_in_context\s*\(|ps_prepare_decl_specifier_alignments_in_context\s*\(|psx_apply_parsed_decl_specifier_in_contexts\s*\(|psx_apply_parsed_standalone_tag_in_contexts\s*\(|psx_apply_local_declaration_syntax_in_contexts\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_node_builder_vla_runtime\s*\(/.test(
      semanticNodeBuilderSource,
    ) ||
    !/psx_semantic_node_builder_vla_runtime\s*\(/.test(
      syntaxTypedHirResolutionSource,
    ) ||
    !/psx_semantic_node_builder_vla_runtime\s*\(/.test(
      resolvedTreeMaterialization,
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
    !/psx_resolve_syntax_initializer_direct_to_typed_hir_with_lowering_in_contexts\s*\(/.test(
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
    !/lvar_t\s*\*\*\s*parameters\s*;/.test(
      functionDefinitionResolutionHeader,
    ) ||
    !/result->parameter_types\s*,\s*result->nargs/.test(
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
    !/node->signature_qual_type\s*=\s*resolution\.signature_qual_type\s*;/.test(
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
    !/ps_function_symbol_qual_type\s*\(/.test(
      identifierResolutionSource,
    ) ||
    /ps_function_symbol_type\s*\(|ps_ctx_intern_qual_type_in\s*\([^]*?resolution->symbol\.function/.test(
      identifierResolutionSource,
    ) ||
    !/psx_function_call_bind_qual_type\s*\(/.test(
      identifierBindingSource,
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
    !/ps_ctx_typedef_decl_type\s*\([^]*?psx_semantic_type_table_lookup_qual_type\s*\(/.test(
      semanticContextHeaderSource,
    ) ||
    /\bt->decl_type\b/.test(semanticContextOwnershipSource) ||
    !/typedef_record_decl_type\s*\([^]*?psx_semantic_type_table_lookup_qual_type\s*\(/.test(
      semanticContextOwnershipSource,
    ) ||
    !/resolve_typedef_decl_qual_type\s*\([^]*?info->decl_type_table\s*!=\s*context->semantic_types/.test(
      semanticContextOwnershipSource,
    ) ||
    !/ps_ctx_intern_declaration_qual_type_in\s*\([^]*?request->type/.test(
      typedefDeclarationResolutionSource,
    ) ||
    !/psx_prepare_function_definition_resolution_in_contexts\s*\([^]*?resolve_function_definition_header\s*\(/.test(
      functionDefinitionResolutionSource,
    )) {
  throw new Error(
    "function and typedef symbols must own canonical QualType values before any mutable compatibility node is projected",
  );
}
if (!parsedFunctionResolutionBoundary ||
    !expressionResolutionBoundary ||
    !initializerResolutionBoundary ||
    /psx_resolution_work_tree_|\bpsx_resolution_work_tree_t\b|compatibility_root|semantic_pass\.h|identifier_binding\.h|semantic_lowering_pass\.h|typed_hir_materialization\.h/.test(
      semanticTreeResolutionSource,
    ) ||
    /legacy_syntax_diagnostics|psx_legacy_syntax_diagnostics_accept_/.test(
      semanticTreeResolutionSource,
    ) ||
    !/diagnose_direct_syntax_rejection\s*\(/.test(
      semanticTreeResolutionSource,
    ) ||
    allSourceFiles.includes("src/semantic/legacy_syntax_diagnostics.h") ||
    /\$\(OBJROOT\)\/semantic\/legacy_syntax_diagnostics\.o/.test(
      makefileSource,
    ) ||
    !/^TEST_ONLY_SRCS=src\/semantic\/legacy_syntax_diagnostics\.c$/m.test(
      makefileSource,
    ) ||
    !/^SRCS=\$\(filter-out\s+\$\(TEST_ONLY_SRCS\),/m.test(
      makefileSource,
    ) ||
    !/\$\(TEST_PARSER\):[^\n]*src\/semantic\/legacy_syntax_diagnostics\.c/.test(
      makefileSource,
    ) ||
    /psx_legacy_syntax_diagnostics_accept_/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    !/psx_resolve_parsed_function_typed_hir_from_syntax_in_contexts\s*\(/.test(
      parsedFunctionResolutionBoundary[1],
    ) ||
    /psx_resolution_work_tree_|\bpsx_resolution_work_tree_t\b|compatibility_root/.test(
      parsedFunctionResolutionBoundary[1],
    ) ||
    !/static\s+psx_resolution_work_tree_t\s*\*resolve_function_compatibility_tree\s*\([^]*?psx_resolution_work_tree_create_from_syntax\s*\([^]*?syntax_function->body[^]*?work_definition\.body\s*=\s*body[^]*?psx_prepare_function_definition_resolution_in_contexts\s*\([^]*?psx_resolution_work_tree_replace_compatibility_root\s*\(/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    !/psx_frontend_next_function_with_resolver\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    /psx_resolution_work_tree_|\bpsx_resolution_work_tree_t\b|compatibility_root/.test(
      `${semanticPipelineSource}\n${semanticPipelineHeader}\n${semanticPipelineInternalHeader}\n${frontendTranslationUnitSource}\n${frontendTranslationUnitResolverHeader}`,
    ) ||
    /ag_compilation_session_parser_runtime_context\s*\(/.test(
      semanticPipelineSource,
    ) ||
    !/psx_resolve_expression_typed_hir_from_syntax_in_contexts\s*\(/.test(
      expressionResolutionBoundary[1],
    ) ||
    /psx_resolution_work_tree_(?:create_from_syntax|compatibility_root_mut|replace_compatibility_root)\s*\(/.test(
      expressionResolutionBoundary[1],
    ) ||
    !/psx_resolve_initializer_typed_hir_from_syntax_in_contexts\s*\(/.test(
      initializerResolutionBoundary[1],
    ) ||
    /psx_resolution_work_tree_(?:create_from_syntax|compatibility_root_mut|replace_compatibility_root)\s*\(/.test(
      initializerResolutionBoundary[1],
    ) ||
    !/resolve_nonfunction_typed_hir_from_syntax_in_contexts\s*\([^]*?diagnose_direct_syntax_rejection\s*\([^]*?return\s+NULL\s*;/.test(
      semanticTreeResolutionSource,
    ) ||
    /resolve_nonfunction_typed_hir_from_syntax_in_contexts\s*\([^]*?return\s+psx_resolution_work_tree_typed_hir\s*\(/.test(
      semanticTreeResolutionSource,
    ) ||
    /psx_resolve_parsed_function_typed_hir_from_syntax_in_contexts\s*\([^]*?return\s+psx_resolution_work_tree_typed_hir\s*\(/.test(
      semanticTreeResolutionSource,
    ) ||
    !/psx_resolve_parsed_function_compatibility_for_test_in_contexts\s*\(/.test(
      semanticTreeResolutionTestSupportHeader,
    ) ||
    !/psx_resolve_expression_compatibility_work_tree_for_test_in_contexts\s*\(/.test(
      semanticTreeResolutionTestSupportHeader,
    ) ||
    /semantic_tree_resolution_internal\.h/.test(
      `${semanticTreeResolutionSource}\n${legacySyntaxDiagnosticsSource}\n${parserCompatibilityTestHook}`,
    ) ||
    !/psx_resolution_work_tree_create_from_syntax\s*\(\s*psx_resolution_store_t\s*\*resolution_store\s*,\s*arena_context_t\s*\*arena_context\s*,\s*const\s+node_t\s*\*syntax_root\s*\)/.test(
      resolutionWorkTreeInternalHeader,
    ) ||
    !/struct\s+psx_resolution_work_tree_t\s*\{[^]*?node_t\s*\*compatibility_root\s*;[^]*?psx_typed_hir_tree_t\s*\*typed_hir\s*;[^]*?psx_resolution_work_phase_t\s+phase\s*;[^]*?\};/.test(
      resolutionWorkTree,
    ) ||
    /psx_semantic_tree_t\s*\*semantic_tree/.test(
      resolutionWorkTree,
    ) ||
    /\bsyntax_root\s*;/.test(resolutionWorkTree) ||
    /\bnode_t\b|parser\//.test(resolutionWorkTreeHeader) ||
    /psx_resolution_work_tree_(?:create_from_syntax|semantic_tree_mut|semantic_tree|export_compatibility_ast|advance|materialize_typed_hir)\s*\(/.test(
      resolutionWorkTreeHeader,
    ) ||
    /psx_resolution_work_tree_semantic_tree(?:_mut)?\s*\(/.test(
      resolutionWorkTreeInternalHeader,
    ) ||
    !/psx_resolution_work_tree_advance\s*\(/.test(
      resolutionWorkTreeInternalHeader,
    ) ||
    !/psx_resolution_work_tree_materialize_typed_hir\s*\(/.test(
      resolutionWorkTreeInternalHeader,
    ) ||
    /PSX_RESOLUTION_WORK_SEMANTIC_READY/.test(
      resolutionWorkTreeHeader,
    ) ||
    !/tree->phase\s*!=\s*PSX_RESOLUTION_WORK_FINALIZED/.test(
      typedHirMaterializationBoundary,
    ) ||
    !/tree->typed_hir\s*=\s*psx_typed_hir_tree_materialize\s*\(\s*tree->compatibility_root/.test(
      typedHirMaterializationBoundary,
    ) ||
    !/tree->phase\s*=\s*PSX_RESOLUTION_WORK_HIR_READY/.test(
      typedHirMaterializationBoundary,
    ) ||
    !/psx_semantic_context_t/.test(
      typedHirMaterializationDeclaration,
    ) ||
    semanticMaterializationCalls.length !== 3 ||
    /if\s*\(\s*is_initializer\s*\)\s*return\s+1/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    !/psx_resolve_parsed_function_compatibility_for_test_in_contexts\s*\([^]*?materialize_resolved_tree\s*\(/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    !/psx_resolve_expression_compatibility_work_tree_for_test_in_contexts\s*\([^]*?materialize_resolved_tree\s*\(/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    !/const\s+psx_typed_hir_tree_t\s*\*psx_resolution_work_tree_typed_hir\s*\(/.test(
      resolutionWorkTreeHeader,
    ) ||
    !/tree->phase\s*==\s*PSX_RESOLUTION_WORK_HIR_READY[^]*?\?\s*tree->typed_hir\s*:\s*NULL/.test(
      resolutionWorkTree,
    ) ||
    /hir_root|psx_semantic_node_t/.test(resolutionWorkTree) ||
    allSourceFiles.some(
      (path) => /src\/semantic\/semantic_tree(?:_internal)?\.[ch]$/.test(path),
    ) ||
    /parser\/|\bnode_t\b|\bnode_kind_t\b/.test(resolvedTreeHeader) ||
    !/typedef\s+struct\s+psx_typed_hir_tree_t\s+psx_typed_hir_tree_t\s*;/.test(
      resolvedTreeHeader,
    ) ||
    /parser\/|\bnode_t\b|\bnode_kind_t\b/.test(
      resolvedHirNodeInternalHeader,
    ) ||
    /parser\/|\bnode_t\b|\bnode_kind_t\b|\bpsx_semantic_context_t\b/.test(
      resolvedTreeHir,
    ) ||
    /psx_semantic_tree_t|psx_materialize_typed_hir_tree/.test(
      resolvedTreeHirHeader,
    ) ||
    /\bpsx_resolution_work_tree_t\b/.test(resolvedTreeHirHeader) ||
    !/const\s+psx_typed_hir_tree_t\s*\*typed_tree/.test(
      resolvedTreeHirHeader,
    ) ||
    !/PSX_RESOLVED_HIR_BUILD_UNFINALIZED_RESOLUTION/.test(
      typedHirBuildStatusHeader,
    ) ||
    !/#include\s+"typed_hir_build_status\.h"/.test(
      resolvedTreeHirHeader,
    ) ||
    !/psx_typed_hir_tree_t\s*\*psx_typed_hir_tree_materialize\s*\(/.test(
      resolvedTreeMaterialization,
    ) ||
    !/const\s+node_t\s*\*resolution_root/.test(
      resolvedTreeMaterialization,
    ) ||
    /\bpsx_resolution_work_tree_t\b/.test(resolvedTreeMaterialization) ||
    !/tree->phase\s*!=\s*PSX_RESOLUTION_WORK_FINALIZED/.test(
      resolutionWorkTree,
    ) ||
    !/psx_typed_hir_tree_materialize\s*\(\s*tree->compatibility_root/.test(
      resolutionWorkTree,
    ) ||
    /psx_resolution_work_tree_(?:materialize_semantic|attach_typed_hir|build_typed_hir)\s*\(/.test(
      resolutionWorkTree,
    ) ||
    !/tree->phase\s*=\s*PSX_RESOLUTION_WORK_HIR_READY/.test(
      resolutionWorkTree,
    ) ||
    !/struct\s+psx_typed_hir_tree_t\s*\{\s*const\s+psx_semantic_node_t\s*\*root\s*;\s*\}/.test(
      resolvedTreeInternalHeader,
    ) ||
    !/psx_hir_expression_spec_t\s+expression\s*=\s*\{[^]*?\.qual_type\s*=\s*typed_expression->qual_type/.test(
      resolvedTreeHir,
    ) ||
    !/psx_semantic_node_t\s*\*root\s*=\s*build_node/.test(
      resolvedTreeMaterialization,
    ) ||
    !/typed_tree->root/.test(resolvedTreeHir) ||
    !/psx_hir_node_id_t\s+psx_typed_hir_tree_emit\s*\(/.test(
      resolvedTreeHir,
    ) ||
    /ps_node_|ND_[A-Z0-9_]+/.test(resolvedTreeHir) ||
    /psx_resolution_work_tree_build_typed_hir\s*\(/.test(
      semanticPipelineSource,
    ) ||
    !/psx_typed_hir_tree_emit\s*\(/.test(semanticPipelineSource) ||
    /typed_hir_builder/.test(
      `${semanticPipelineSource}\n${semanticPipelineInternalHeader}`,
    ) ||
    allSourceFiles.some((path) => /typed_hir_builder\.[ch]$/.test(path)) ||
    !/next\s*!=\s*\(psx_resolution_work_phase_t\)\(expected\s*\+\s*1\)/.test(
      resolutionWorkTree,
    ) ||
    !/psx_resolution_work_tree_compatibility_root_mut\s*\(/.test(
      legacySyntaxDiagnosticsSource,
    ) ||
    /const\s+node_t\s*\*psx_resolution_work_tree_compatibility_root\s*\(/.test(
      `${resolutionWorkTree}\n${resolutionWorkTreeInternalHeader}`,
    ) ||
    /\bpsx_frontend_analyze_program_(?:in_contexts|in_session)\s*\(/.test(
      `${semanticPipelineSource}\n${semanticPipelineInternalHeader}`,
    ) ||
    /\bpsx_lower_implicit_conversions\s*\(|\blower_implicit_value_conversion\s*\(/.test(
      `${semanticLoweringPassSource}\n${castLoweringSource}`,
    ) ||
    /\bpsx_(?:bind_identifier_(?:tree|initializer_tree)|semantic_resolve_(?:tree|initializer_tree)|lower_semantic_(?:tree|initializer_syntax))_in_contexts\s*\(/.test(
      semanticPipelineSource,
    )) {
  throw new Error(
    "frontend resolvers must keep mutable resolution work trees separate from materialized Typed HIR trees",
  );
}
const lowerTreeBoundary =
  legacySyntaxDiagnosticsSource.match(
    /static\s+node_t\s*\*lower_tree\s*\([^)]*\)\s*\{([^]*?)\n\}/,
  )?.[1] ?? "";
const resolveTreeCalls =
  typedResolutionBoundary.match(
    /\bpsx_semantic_resolve_tree_in_contexts\s*\(/g,
  ) ?? [];
const resolveInitializerCalls =
  typedResolutionBoundary.match(
    /\bpsx_semantic_resolve_initializer_tree_in_contexts\s*\(/g,
  ) ?? [];
if (resolveTreeCalls.length !== 1 ||
    resolveInitializerCalls.length !== 1 ||
    !/\bpsx_lower_semantic_tree_in_contexts\s*\(/.test(lowerTreeBoundary) ||
    !/\bpsx_lower_semantic_initializer_syntax_in_contexts\s*\(/.test(
      lowerTreeBoundary,
    ) ||
    !/resolve_typed_tree\s*\([\s\S]*?lower_tree\s*\([\s\S]*?psx_validate_lowered_tree_in_context\s*\(/.test(
      functionSemanticPipelineBody,
    ) ||
    !/resolve_typed_tree\s*\([\s\S]*?lower_tree\s*\([\s\S]*?finalize_expression_compatibility_tree\s*\(/.test(
      expressionSemanticPipelineBody,
    ) ||
    /\bpsx_semantic_resolve_(?:tree|initializer_tree)_in_contexts\s*\(/.test(
      `${functionSemanticPipelineBody}\n${expressionSemanticPipelineBody}`,
    ) ||
    !/psx_walk_semantic_tree\s*\(\s*ps_ctx_resolution_store\s*\(\s*semantic_context\s*\)\s*,\s*root\s*,\s*validate_lowered_node/.test(
      loweredTreeValidationSource,
    ) ||
    /psx_walk_semantic_tree_mut\s*\(/.test(
      loweredTreeValidationSource,
    ) ||
    /static\s+void\s+semantic_validate_assignment/.test(
      semanticPassSource,
    ) ||
    !/void\s+psx_validate_assignment_in_context\s*\([^]*?const\s+node_t\s*\*node/.test(
      assignmentValidationSource,
    )) {
  throw new Error(
    "semantic pipeline must resolve once before lowering and use read-only validation afterward",
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
const hirIrCallSource = await readFile(
  "src/lowering/hir_ir_call.c",
  "utf8",
);
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
    /\bnode_t\s*\*\s*lower_decl_initializer\s*\([^;]*\)\s*\{/.test(
      hirIrBuilderSource,
    ) ||
    !/\bnode_t\s*\*\s*lower_decl_initializer\s*\([^;]*\)\s*\{/.test(
      initializerLoweringSource,
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
const wasmMachineAbiSource = await readFile(
  "src/arch/wasm32/wasm32_machine_abi.c",
  "utf8",
);
const wasmMachineFunctionSource = await readFile(
  "src/arch/wasm32/wasm32_machine_function.c",
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
    !/wasm32_machine_signature_from_abi\s*\(/.test(wasmWatWriterSource) ||
    !/wasm32_machine_signature_from_abi\s*\(/.test(wasmObjectWriterSource) ||
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
    !/\bvariadic_arguments\b/.test(wasmWatWriterSource) ||
    !/\bvariadic_arguments\b/.test(wasmObjectWriterSource) ||
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

if (!/wasm32_machine_function_build\s*\(/.test(
      wasmMachineFunctionSource,
    ) ||
    !/wasm32_machine_function_build\s*\(/.test(wasmWatWriterSource) ||
    !/wasm32_machine_function_build\s*\(/.test(wasmObjectWriterSource) ||
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
    "Wasm WAT and object writers must share the Machine function analysis plan",
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

console.log(
  "design invariants: ok (IR/backend isolation, canonical type ownership, and record identity verified)",
);
