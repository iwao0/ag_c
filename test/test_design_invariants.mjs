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

const irFiles = (await readdir("src/ir", { withFileTypes: true }))
  .filter((entry) => entry.isFile() && /\.[ch]$/.test(entry.name))
  .map((entry) => `src/ir/${entry.name}`)
  .sort();

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

const archFiles = (await sourceFilesUnder("src/arch"))
  .filter((file) => file.endsWith(".c"))
  .sort();

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
    !/psx_semantic_context_t\s*\*ps_ctx_activate\s*\(/.test(semanticContextOwnershipSource) ||
    !/void\s+ps_ctx_destroy\s*\(/.test(semanticContextOwnershipSource)) {
  throw new Error("semantic state must be owned by an explicit context lifecycle");
}
const legacySemanticGlobals =
  /^static\s+.*\b(?:goto_refs_all|label_defs_by_bucket|deferred_parser_warnings_all|tag_types_by_bucket|all_tag_types|tag_members_by_bucket|enum_consts_by_bucket|all_enum_consts|typedefs_by_bucket|all_typedefs|func_names_by_bucket|tag_scope_depth|tag_member_decl_order)\b/gm;
if (legacySemanticGlobals.test(semanticContextOwnershipSource)) {
  throw new Error("semantic registries must not return to file-scope global ownership");
}
const compilerContextHeader = await readFile("src/compiler_context.h", "utf8");
const compilerContextSource = await readFile("src/compiler_context.c", "utf8");
const compilerMainSource = await readFile("src/main.c", "utf8");
if (!/psx_semantic_context_t\s*\*semantic_context\s*;/.test(
      compilerContextHeader,
    ) ||
    !/psx_global_registry_t\s*\*global_registry\s*;/.test(
      compilerContextHeader,
    ) ||
    !/psx_local_registry_t\s*\*local_registry\s*;/.test(
      compilerContextHeader,
    ) ||
    !/ps_global_registry_create\s*\(/.test(compilerContextSource) ||
    !/ps_global_registry_activate\s*\(/.test(compilerContextSource) ||
    !/ps_global_registry_destroy\s*\(/.test(compilerContextSource) ||
    !/ps_local_registry_create\s*\(/.test(compilerContextSource) ||
    !/ps_local_registry_activate\s*\(/.test(compilerContextSource) ||
    !/ps_local_registry_destroy\s*\(/.test(compilerContextSource) ||
    !/ag_compiler_context_is_complete\s*\(/.test(compilerContextSource) ||
    !/psx_frontend_reset_translation_unit_state_in_compiler_context\s*\(/.test(
      compilerMainSource,
    ) ||
    !/ag_compiler_context_init\s*\(/.test(compilerMainSource) ||
    !/ag_compiler_context_activate\s*\(/.test(compilerMainSource) ||
    !/ag_compiler_context_dispose\s*\(/.test(compilerMainSource)) {
  throw new Error("compilation entry points must own semantic state through CompilerContext");
}
const globalRegistrySource = await readFile(
  "src/parser/global_registry.c",
  "utf8",
);
if (!/struct\s+psx_global_registry_t\s*\{/.test(globalRegistrySource) ||
    !/psx_global_registry_t\s*\*ps_global_registry_create\s*\(/.test(
      globalRegistrySource,
    ) ||
    !/psx_global_registry_t\s*\*ps_global_registry_activate\s*\(/.test(
      globalRegistrySource,
    ) ||
    !/void\s+ps_global_registry_destroy\s*\(/.test(
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
    !/psx_local_registry_t\s*\*ps_local_registry_create\s*\(/.test(
      localRegistrySource,
    ) ||
    !/psx_local_registry_t\s*\*ps_local_registry_activate\s*\(/.test(
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
    legacyActiveLocalRegistryMacros.test(localRegistrySource)) {
  throw new Error(
    "function-local symbols, scopes, and usage events must be owned by an explicit registry",
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
    !/ps_local_registry_scope_is_visible_from_in\s*\(/.test(
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
const parserStreamHeader = await readFile("src/parser/parser.h", "utf8");
const parserStreamSource = await readFile("src/parser/parser.c", "utf8");
const semanticPipelineSource = await readFile(
  "src/frontend/semantic_pipeline.c",
  "utf8",
);
const identifierBindingSource = await readFile(
  "src/semantic/identifier_binding.c",
  "utf8",
);
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
const toplevelDeclarationHeader = await readFile(
  "src/parser/toplevel_declaration_syntax.h",
  "utf8",
);
const toplevelDeclarationFrontendSource = await readFile(
  "src/frontend/toplevel_declaration.c",
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
const staticLocalLoweringSource = await readFile(
  "src/lowering/static_local_lowering.c",
  "utf8",
);
const localDeclarationPipelineSource = await readFile(
  "src/declaration_pipeline.c",
  "utf8",
);
if (/\bps_(?:ctx_active|global_registry_active|local_registry_active)\s*\(/.test(
      localDeclarationPipelineSource,
    )) {
  throw new Error(
    "declaration pipelines must use only their explicit compiler contexts",
  );
}
if (!/ag_compiler_context_t\s*\*compiler_context\s*;/.test(
      frontendTranslationUnitHeader,
    ) ||
    !/int\s+psx_frontend_stream_begin\s*\(/.test(
      frontendTranslationUnitHeader,
    ) ||
    !/psx_frontend_stream_begin\s*\([\s\S]*?ag_compiler_context_t\s*\*compiler_context/.test(
      frontendTranslationUnitHeader,
    ) ||
    !/ps_global_registry_reset_diag_state_in\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    !/psx_local_registry_t\s*\*local_registry\s*;/.test(
      parserStreamHeader,
    ) ||
    !/ps_parser_stream_begin_in_contexts\s*\(/.test(parserStreamSource) ||
    !/psx_frontend_analyze_function_in_compiler_context\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    !/psx_bind_identifier_tree_in_contexts\s*\(/.test(
      semanticPipelineSource,
    ) ||
    !/\.local_registry\s*=\s*local_registry/.test(
      identifierBindingSource,
    ) ||
    !/psx_analyze_function_lvar_usage_in\s*\(/.test(
      semanticPipelineSource,
    ) ||
    !/psx_lower_semantic_tree_in_contexts\s*\(/.test(
      semanticPipelineSource,
    ) ||
    !/ps_decl_replay_lvar_usage_events_in\s*\(/.test(
      lvarUsageAnalysisSource,
    )) {
  throw new Error("frontend stream must receive the compilation-unit context explicitly");
}
const frontendStreamCore = frontendTranslationUnitSource.slice(
  frontendTranslationUnitSource.indexOf("int psx_frontend_stream_begin("),
  frontendTranslationUnitSource.indexOf("void psx_frontend_free_processed_ast("),
);
if (/\bps_(?:ctx_active|global_registry_active|local_registry_active)\s*\(/.test(
      frontendStreamCore,
    ) ||
    !/ag_compiler_context_is_complete\s*\(compiler_context\)/.test(
      frontendStreamCore,
    ) ||
    /if\s*\(\s*!compiler_context\s*\)/.test(semanticPipelineSource)) {
  throw new Error(
    "frontend stream core must require a complete CompilerContext without active-state fallback",
  );
}
if (!/psx_global_registry_t\s*\*global_registry\s*;/.test(
      toplevelDeclarationHeader,
    ) ||
    !/psx_local_registry_t\s*\*local_registry\s*;/.test(
      toplevelDeclarationHeader,
    ) ||
    !/\.context\s*=\s*callbacks/.test(
      toplevelDeclarationFrontendSource,
    ) ||
    !/psx_apply_parsed_decl_specifier_in_contexts\s*\(/.test(
      toplevelDeclarationFrontendSource,
    ) ||
    !/psx_apply_parsed_declarator_type_in_contexts\s*\(/.test(
      toplevelDeclarationFrontendSource,
    ) ||
    !/psx_frontend_init_toplevel_declaration_callbacks_in_contexts\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    !/psx_apply_toplevel_declaration_in_contexts\s*\(/.test(
      frontendTranslationUnitSource,
    )) {
  throw new Error(
    "top-level declaration callbacks must carry all compilation registries",
  );
}
const explicitLocalDeclarationLowering = [
  localObjectLoweringSource,
  parameterLoweringSource,
  vlaLoweringSource,
  staticLocalLoweringSource,
].join("\n");
if (!/psx_global_registry_t\s*\*global_registry\s*;/.test(
      localDeclarationHeader,
    ) ||
    !/psx_local_registry_t\s*\*local_registry\s*;/.test(
      localDeclarationHeader,
    ) ||
    !/callbacks->context\s*=\s*callbacks\s*;/.test(
      localDeclarationFrontendSource,
    ) ||
    !/psx_apply_parsed_decl_specifier_in_contexts\s*\(/.test(
      localDeclarationFrontendSource,
    ) ||
    !/psx_apply_parsed_standalone_tag_in_contexts\s*\(/.test(
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
    "local declaration and parameter lowering must use explicitly passed registries",
  );
}
if (!/psx_frontend_analyze_initializer_syntax_in_contexts\s*\([\s\S]*?request->local_registry/.test(
      localDeclarationPipelineSource,
    ) ||
    !/psx_frontend_analyze_expression_in_contexts\s*\([\s\S]*?request->local_registry/.test(
      localDeclarationPipelineSource,
    )) {
  throw new Error(
    "static local initializer semantics must use the declaration registry",
  );
}
const identifierResolutionSource = await readFile(
  "src/semantic/identifier_resolution.c",
  "utf8",
);
const functionDeclarationResolutionSource = await readFile(
  "src/semantic/function_declaration_resolution.c",
  "utf8",
);
if (!/ps_ctx_find_function_symbol_in\s*\(/.test(identifierResolutionSource) ||
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
const aggregateMemberResolutionSource = await readFile(
  "src/semantic/aggregate_member_resolution.c",
  "utf8",
);
const declarationApplicationSource = await readFile(
  "src/semantic/declaration_application.c",
  "utf8",
);
const frontendDeclarationSources = [
  await readFile("src/frontend/toplevel_declaration.c", "utf8"),
  await readFile("src/frontend/local_declaration.c", "utf8"),
  await readFile("src/frontend/function_definition.c", "utf8"),
].join("\n");
const contextFreeTagRegistryCall =
  /\bps_ctx_(?:has_tag_type|register_tag_type|get_tag_size|get_tag_align|get_tag_definition|get_tag_member_count|register_tag_members|find_tag_member_info)\s*\(/;
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
const semanticPassSource = await readFile(
  "src/semantic/semantic_pass.c",
  "utf8",
);
const memberAccessResolutionSource = await readFile(
  "src/semantic/member_access_resolution.c",
  "utf8",
);
const typeNameResolutionSource = await readFile(
  "src/semantic/type_name_resolution.c",
  "utf8",
);
const typeQueryResolutionSource = await readFile(
  "src/semantic/type_query_resolution.c",
  "utf8",
);
const declarationResolutionSource = await readFile(
  "src/semantic/declaration_resolution.c",
  "utf8",
);
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
  declarationApplicationSource,
  await readFile("src/semantic/declaration_registration.c", "utf8"),
].join("\n");
const contextFreeSemanticTraversalCall =
  /\bpsx_semantic_resolve_(?:tree|initializer_tree)\s*\(/;
const contextFreeMemberRegistryCall =
  /\bps_ctx_(?:find_tag_member_info|find_tag_member_info_at_scope)\s*\(/;
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
      frontendSemanticPipelineSource,
    ) ||
    contextFreeMemberRegistryCall.test(memberAccessResolutionSource) ||
    !/ps_ctx_find_tag_member_info_at_scope_in\s*\(/.test(
      memberAccessResolutionSource,
    ) ||
    !/ps_ctx_find_tag_member_info_in\s*\(/.test(
      memberAccessResolutionSource,
    ) ||
    !/ps_ctx_find_typedef_decl_type_at_in_contexts\s*\(/.test(
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
    !/ps_ctx_attach_aggregate_definitions_in\s*\(/.test(
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
const globalDeclarationResolutionSource = await readFile(
  "src/semantic/global_declaration_resolution.c",
  "utf8",
);
const declarationRegistrationSource = await readFile(
  "src/semantic/declaration_registration.c",
  "utf8",
);
const ordinaryNamespaceResolutionSources = [
  identifierResolutionSource,
  enumConstantResolutionSource,
  typedefDeclarationResolutionSource,
  globalDeclarationResolutionSource,
].join("\n");
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
    !/ps_ctx_register_enum_const_in_contexts\s*\(/.test(
      enumConstantResolutionSource,
    ) ||
    !/ps_ctx_register_typedef_name_in_contexts\s*\(/.test(
      typedefDeclarationResolutionSource,
    ) ||
    !/ps_ctx_find_enum_const_at_in_contexts\s*\(/.test(
      identifierResolutionSource,
    ) ||
    !/ps_ctx_find_typedef_name_in\s*\(/.test(
      globalDeclarationResolutionSource,
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
if (!/ps_find_global_var_in\s*\(/.test(
      globalDeclarationResolutionSource,
    ) ||
    !/ps_find_global_var_in\s*\(/.test(
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
      semanticPipelineSource,
    ) ||
    !/psx_lower_semantic_initializer_syntax_in_contexts\s*\(/.test(
      semanticPipelineSource,
    ) ||
    !/lower_compound_literal_expression_in_contexts\s*\(/.test(
      semanticLoweringPassSource,
    ) ||
    !/\.global_registry\s*=\s*global_registry/.test(
      semanticLoweringPassSource,
    )) {
  throw new Error(
    "global declaration and semantic lowering must use explicit registries",
  );
}
const frontendFunctionDefinitionSource = await readFile(
  "src/frontend/function_definition.c",
  "utf8",
);
const parserSource = await readFile("src/parser/parser.c", "utf8");
const statementParserSource = await readFile("src/parser/stmt.c", "utf8");
const expressionParserSource = await readFile("src/parser/expr.c", "utf8");
const initializerSyntaxSource = await readFile(
  "src/parser/initializer_syntax.c",
  "utf8",
);
const localDeclarationSyntaxSource = await readFile(
  "src/parser/local_declaration_syntax.c",
  "utf8",
);
const toplevelDeclarationSyntaxSource = await readFile(
  "src/parser/toplevel_declaration_syntax.c",
  "utf8",
);
const enumConstSource = await readFile("src/parser/enum_const.c", "utf8");
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
  /\bps_ctx_(?:reset_translation_unit_scope|reset_function_diag_state|reset_tag_diag_state|reset_function_scope|enter_block_scope|leave_block_scope|record_unsupported_gnu_extension_warning|emit_deferred_parser_warnings|promote_tag_to_file_scope)\s*\(/;
const contextFreeJumpRegistryCall =
  /\bpsx_ctx_(?:register_goto_ref|register_label_def|validate_goto_refs)\s*\(/;
const implicitActiveContextFallback =
  /semantic_context\s*\?\s*semantic_context\s*:\s*ps_ctx_active\s*\(\)|if\s*\(\s*!semantic_context\s*\)\s*semantic_context\s*=\s*ps_ctx_active\s*\(\)/;
const explicitParserContextSources = [
  expressionParserSource,
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
    !/ps_parser_stream_begin_in_contexts\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    !/psx_stmt_stmt_in_contexts\s*\(/.test(parserSource) ||
    !/psx_ctx_is_typedef_name_token_in\s*\(/.test(
      statementParserSource,
    ) ||
    /active_local_declarations/.test(statementParserSource) ||
    !/psx_expr_expr_in_contexts\s*\(/.test(statementParserSource) ||
    !/psx_parse_statement_expression_in_contexts\s*\(/.test(
      expressionParserSource,
    ) ||
    !/psx_parse_initializer_syntax_list_in_context\s*\(/.test(
      expressionParserSource,
    ) ||
    !/ps_ctx_record_unsupported_gnu_extension_warning_in\s*\(/.test(
      initializerSyntaxSource,
    ) ||
    !/psx_parse_declarator_syntax_tree_into_with_typedef_lookup\s*\(/.test(
      localDeclarationSyntaxSource,
    ) ||
    !/psx_try_parse_toplevel_declarator_syntax_tree_with_typedef_lookup\s*\(/.test(
      toplevelDeclarationSyntaxSource,
    ) ||
    !/ps_ctx_find_enum_const_in\s*\(/.test(enumConstSource) ||
    !/psx_frontend_reset_translation_unit_state_in_compiler_context\s*\(/.test(
      compilerMainSource,
    ) ||
    !/ps_ctx_reset_function_scope_in\s*\(/.test(
      frontendFunctionDefinitionSource,
    ) ||
    !/ps_ctx_emit_deferred_parser_warnings_in\s*\(/.test(
      frontendTranslationUnitSource,
    ) ||
    !/psx_apply_parsed_function_parameters_in_contexts\s*\(/.test(
      declarationApplicationSource,
    ) ||
    !/ps_ctx_record_unsupported_gnu_extension_warning_in\s*\(/.test(
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
  if ((file.startsWith("src/arch/") || file.startsWith("src/ir/")) &&
      (/#[ \t]*include[^\n]*parser\//.test(source) ||
       /\bpsx?_[A-Za-z0-9_]+\b/.test(source) ||
       /\b(?:global_var_t|lvar_t|node_t|tag_member_info_t|string_lit_t|float_lit_t)\b/.test(
         source,
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

const wasmIrSource = await readFile("src/arch/wasm32/wasm32_ir.c", "utf8");
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
    !/\bdata_for_ir_inst\s*\(\s*module\s*,/.test(
      wasmObjFunctionCodegen,
    ) ||
    !/static obj_data_t \*data_for_ir_inst[\s\S]*?\bir_module_find_symbol\s*\(/.test(
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
const irBuilderSource = await readFile("src/lowering/ir_builder.c", "utf8");
const resolvedGlobalAstSource = await readFile("src/parser/ast.h", "utf8");
const constantExpressionSource = await readFile(
  "src/semantic/constant_expression.c",
  "utf8",
);
const irSymbolLoweringSource = await readFile(
  "src/lowering/ir_symbol_lowering.c",
  "utf8",
);
if (!/typedef struct ir_symbol_t\s*\{/.test(irHeaderSource) ||
    !/\bir_symbol_t\s*\*symbols\s*;/.test(irHeaderSource) ||
    !/\blower_ir_global_symbol\s*\(/.test(irBuilderSource) ||
    !/object_size\s*=\s*\(s->byte_len\s*\+\s*1\)/.test(irBuilderSource)) {
  throw new Error(
    "IR lowering must materialize global layout and string size before backend codegen",
  );
}
if (!/struct node_gvar_t\s*\{[\s\S]*?struct global_var_t\s*\*symbol\s*;/.test(
      resolvedGlobalAstSource,
    ) ||
    /\bps_find_global_var\s*\(/.test(constantExpressionSource) ||
    /\bps_find_global_var\s*\(/.test(irSymbolLoweringSource) ||
    !/lower_ir_global_symbol\s*\(\s*ctx->m\s*,\s*gv->symbol\s*\)/.test(
      irBuilderSource,
    )) {
  throw new Error(
    "resolved global references must retain symbol identity without active-registry lookup",
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
const nodeStruct = astSource.match(/struct node_t\s*\{([\s\S]*?)\n\};/);
if (!nodeStruct ||
    !/\bconst\s+psx_type_t\s*\*\s*type\s*;/.test(nodeStruct[1]) ||
    /\b(?:unsigned_override|has_unsigned_override)\b/.test(nodeStruct[1])) {
  throw new Error(
    "node_t value identity must come only from its canonical semantic type",
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
const functionDefinitionStruct = astSource.match(
  /struct node_function_definition_t\s*\{([^{}]*)\};/,
);
const functionCallStruct = astSource.match(
  /struct node_function_call_t\s*\{([^{}]*)\};/,
);
if (/\bnode_func_t\b/.test(astSource) ||
    !functionDefinitionStruct ||
    !/\bnode_t\s*\*\*\s*parameters\s*;/.test(
      functionDefinitionStruct[1],
    ) ||
    !/\bconst\s+psx_type_t\s*\*\s*signature\s*;/.test(
      functionDefinitionStruct[1],
    ) ||
    /\b(?:arguments|callee|callee_type|direct_name)\b/.test(
      functionDefinitionStruct[1],
    ) ||
    !functionCallStruct ||
    !/\bnode_t\s*\*\*\s*arguments\s*;/.test(functionCallStruct[1]) ||
    !/\bnode_t\s*\*\s*callee\s*;/.test(functionCallStruct[1]) ||
    !/\bconst\s+psx_type_t\s*\*\s*callee_type\s*;/.test(
      functionCallStruct[1],
    ) ||
    /\b(?:parameters|signature|lvars|is_static)\b/.test(
      functionCallStruct[1],
    )) {
  throw new Error(
    "function definitions and calls must use disjoint canonical AST records",
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
if (!typeNameRef ||
    !/\bconst\s+psx_type_t\s*\*\s*bound_base_type\s*;/.test(
      typeNameRef[1],
    ) ||
    !/\bconst\s+psx_type_t\s*\*\s*resolved_type\s*;/.test(
      typeNameRef[1],
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
    /\bqueried_type\b/.test(sizeofQueryNode[1])) {
  throw new Error(
    "type-name expressions must keep resolved types only in their type-name reference",
  );
}

const nodeUtilsSource = await readFile("src/parser/node_utils.c", "utf8");
const nodeTypePublicSource = await readFile(
  "src/parser/node_type_public.h",
  "utf8",
);
const declarationPipelineSource = await readFile(
  "src/declaration_pipeline.c",
  "utf8",
);
const semanticInvariantsSource = await readFile(
  "src/semantic/semantic_invariants.c",
  "utf8",
);
const functionNodeBinding = declarationPipelineSource.match(
  /if\s*\(request->function_node\)\s*\{([^{}]*)\}/,
);
if (!/\bps_function_definition_return_type\s*\(/.test(
      nodeTypePublicSource,
    ) ||
    !/return\s+function->signature->base\s*;/.test(nodeUtilsSource) ||
    !functionNodeBinding ||
    /ps_node_bind_type\s*\(/.test(functionNodeBinding[1]) ||
    !/node->type\s*!=\s*NULL/.test(semanticInvariantsSource)) {
  throw new Error(
    "function definition return types must be owned only by the canonical signature",
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
const compoundLiteralLoweringSource = await readFile(
  "src/lowering/compound_literal_lowering.c",
  "utf8",
);
const compoundLiteralLoweringHeader = await readFile(
  "src/lowering/compound_literal_lowering.h",
  "utf8",
);
if (!/arena_alloc\s*\(\s*sizeof\s*\(\s*node_source_cast_t\s*\)\s*\)/.test(
      nodeUtilsSource,
    ) ||
    /node_source_cast_t\s*\*[^;=]*=\s*arena_alloc\s*\(\s*sizeof\s*\(\s*node_num_t\s*\)/.test(
      nodeUtilsSource,
    )) {
  throw new Error("source casts must use their own arena allocation size");
}
if (!/\bnode_t\s*\*\s*lower_source_cast_expression\s*\(/.test(
      castLoweringHeader,
    ) ||
    /\*\s*\(\s*node_num_t\s*\*\s*\)\s*node\s*=/.test(
      castLoweringSource,
    )) {
  throw new Error(
    "source cast lowering must return a replacement node without cross-struct overwrite",
  );
}
if (!/\bnode_t\s*\*\s*lower_compound_literal_expression\s*\(/.test(
      compoundLiteralLoweringHeader,
    ) ||
    /_Static_assert\s*\(\s*sizeof\s*\(\s*node_compound_literal_t\s*\)/.test(
      compoundLiteralLoweringSource,
    ) ||
    /\*\s*\(\s*node_num_t\s*\*\s*\)\s*node\s*=/.test(
      compoundLiteralLoweringSource,
    )) {
  throw new Error(
    "compound literal lowering must return a replacement node without size coupling or cross-struct overwrite",
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
  "ps_type_new",
  "ps_type_new_integer",
  "ps_type_new_enum",
  "ps_type_new_float",
  "ps_type_new_pointer",
  "ps_type_new_function",
  "ps_type_new_array",
  "ps_type_clone",
  "ps_type_clone_persistent",
  "ps_type_new_tag",
  "ps_type_normalize_integer_identity",
  "ps_type_set_function_params",
  "ps_type_wrap_array_dims",
  "ps_type_apply_declarator_shape",
  "ps_type_adjust_parameter_type",
  "ps_type_complete_array",
  "ps_type_set_decl_spec_qualifiers",
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
  "src/semantic/declaration_application.c",
  "src/semantic/type_name_resolution.c",
  "src/semantic/semantic_pass.c",
  "src/semantic/generic_selection_resolution.c",
  "src/semantic/static_initializer_resolution.c",
  "src/semantic/type_query_resolution.c",
  "src/semantic/identifier_binding.c",
  "src/semantic/function_call_resolution.c",
  "src/semantic/expression_operand_resolution.c",
  "src/lowering/vla_lowering.c",
  "src/lowering/sizeof_lowering.c",
  "src/lowering/cast_lowering.c",
  "src/lowering/member_access_lowering.c",
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
  "ps_declarator_shape_copy",
  "ps_declarator_shape_append_pointer",
  "ps_declarator_shape_append_array",
  "ps_declarator_shape_append_array_ex",
  "ps_declarator_shape_append_vla_array",
  "ps_declarator_shape_append_function",
  "ps_declarator_op_set_function_params",
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
  "ps_type_usual_arithmetic_result",
  "ps_type_binary_result",
  "ps_type_conditional_result",
  "ps_type_address_result",
  "ps_type_decay_array",
  "ps_type_subscript_result",
  "ps_type_generic_control",
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
const aggregateDefinitionStruct = typeSource.match(
  /typedef struct psx_aggregate_definition_t\s*\{([\s\S]*?)\}\s*psx_aggregate_definition_t\s*;/,
);
if (!canonicalTypeStruct ||
    !/\bconst\s+psx_type_t\s*\*\s*base\s*;/.test(
      canonicalTypeStruct[1],
    ) ||
    !/\bconst\s+psx_type_t\s*\*\s*const\s*\*\s*param_types\s*;/.test(
      canonicalTypeStruct[1],
    ) ||
    !/\bconst\s+psx_aggregate_definition_t\s*\*\s*aggregate_definition\s*;/.test(
      canonicalTypeStruct[1],
    ) ||
    !aggregateDefinitionStruct ||
    !/\bconst\s+struct\s+tag_member_info_t\s*\*\s*members\s*;/.test(
      aggregateDefinitionStruct[1],
    )) {
  throw new Error(
    "canonical recursive type children and aggregate definitions must be exposed as const views",
  );
}

const semanticContextSource = await readFile(
  "src/parser/semantic_ctx.h",
  "utf8",
);
if (!/\bconst\s+psx_aggregate_definition_t\s*\*\s*ps_ctx_get_tag_definition\s*\(/.test(
  semanticContextSource,
)) {
  throw new Error("aggregate definition lookup must return a const view");
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
    !/\bconst\s+psx_type_t\s*\*\s*decl_type\s*;/.test(lvarStruct[1]) ||
    !gvarStruct ||
    !/\bconst\s+psx_type_t\s*\*\s*decl_type\s*;/.test(gvarStruct[1]) ||
    !/\bconst\s+psx_type_t\s*\*\s*ps_lvar_get_decl_type\s*\(\s*const\s+lvar_t\s*\*/.test(
      lvarPublicSource,
    ) ||
    !/\bconst\s+psx_type_t\s*\*\s*ps_gvar_get_decl_type\s*\(\s*const\s+global_var_t\s*\*/.test(
      gvarPublicSource,
    )) {
  throw new Error(
    "local and global symbol canonical types must be exposed as const views",
  );
}

const staticInitializerSource = await readFile(
  "src/semantic/static_initializer_resolution.h",
  "utf8",
);
const staticInitializerRequest = staticInitializerSource.match(
  /typedef struct\s*\{([\s\S]*?)\}\s*psx_static_initializer_resolution_request_t\s*;/,
);
if (!staticInitializerRequest ||
    !/\bconst\s+psx_type_t\s*\*\s*type\s*;/.test(
      staticInitializerRequest[1],
    )) {
  throw new Error(
    "static initializer resolution must consume a read-only canonical type",
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
  ["src/semantic/local_declaration_resolution.h", "psx_local_declaration_resolution_request_t", "type"],
  ["src/semantic/parameter_declaration_resolution.h", "psx_parameter_declaration_resolution_t", "type"],
  ["src/semantic/aggregate_member_resolution.h", "psx_aggregate_member_declaration_resolution_t", "type"],
  ["src/semantic/static_initializer_resolution.h", "psx_static_initializer_resolution_t", "type"],
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
  ["src/semantic/declaration_resolution.h", "psx_resolve_decl_specifier_syntax"],
  ["src/semantic/declaration_application.h", "psx_apply_parsed_decl_specifier"],
  ["src/semantic/declaration_application.h", "psx_apply_parsed_type_name"],
  ["src/semantic/declaration_application.h", "psx_apply_parsed_declarator_type"],
  ["src/semantic/declaration_application.h", "psx_apply_runtime_declarator_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_indirection_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_arithmetic_unary_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_binary_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_conditional_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_sequence_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_address_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_incdec_result_type"],
  ["src/semantic/function_call_resolution.h", "psx_resolve_function_reference_type"],
  ["src/parser/node_utils.h", "ps_node_row_decay_pointer_arith_type"],
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

const staticDataInitializerSource = await readFile(
  "src/lowering/static_data_initializer.h",
  "utf8",
);
for (const functionName of [
  "lower_static_object_initializer",
  "lower_static_scalar_array_initializer",
]) {
  const signature = new RegExp(
    `\\b${functionName}\\s*\\([\\s\\S]*?\\bconst\\s+psx_type_t\\s*\\*\\s*type\\b[\\s\\S]*?\\)\\s*;`,
  );
  if (!signature.test(staticDataInitializerSource)) {
    throw new Error(`${functionName} must consume a const type view`);
  }
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

console.log(
  "design invariants: ok (IR/backend isolation and read-only canonical type ownership verified)",
);
