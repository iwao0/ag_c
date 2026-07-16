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
const hirHeader = await readFile("src/hir/hir.h", "utf8");
const hirImplementation = await readFile("src/hir/hir.c", "utf8");
const typedHirBuilder = await readFile(
  "src/semantic/typed_hir_builder.c",
  "utf8",
);
const resolutionWorkTree = await readFile(
  "src/semantic/resolution_work_tree.c",
  "utf8",
);
const hirIrBuilder = `${await readFile(
  "src/lowering/hir_ir_builder.h",
  "utf8",
)}\n${await readFile("src/lowering/hir_ir_builder.c", "utf8")}`;
const compilationSession = await readFile(
  "src/compilation_session.c",
  "utf8",
);
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
      "ps_node_new_vla_alloc",
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
    !/int\s+value_size\s*=\s*ps_type_sizeof_id_with_records\s*\(/.test(
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
  /^static\s+.*\b(?:goto_refs_all|label_defs_by_bucket|deferred_parser_warnings_all|tag_types_by_bucket|all_tag_types|tag_members_by_bucket|enum_consts_by_bucket|all_enum_consts|typedefs_by_bucket|all_typedefs|func_names_by_bucket|tag_scope_depth|tag_member_decl_order)\b/gm;
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
const wasmIrSource = await readFile("src/arch/wasm32/wasm32_ir.c", "utf8");
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
const loweringRuntimeHeader = await readFile(
  "src/lowering/runtime_context.h",
  "utf8",
);
const loweringRuntimeSource = await readFile(
  "src/lowering/runtime_context.c",
  "utf8",
);
const expressionLoweringSource = await readFile(
  "src/lowering/expr_lowering.c",
  "utf8",
);
const subscriptLoweringSource = await readFile(
  "src/lowering/subscript_lowering.c",
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
const irBuilderSource = await readFile(
  "src/lowering/ir_builder.c",
  "utf8",
);
const irBuilderHeader = await readFile(
  "src/lowering/ir_builder.h",
  "utf8",
);
if (/\bps_node_atomic_pointer_info\s*\(/.test(
      `${parserLayerSource}\n${loweringLayerSource}`,
    ) ||
    !/int\s+width\s*=\s*ir_type_deref_size\s*\(\s*ctx\s*,\s*ps_node_get_type\s*\(\s*parg\s*\)\s*\)/.test(
      irBuilderSource,
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
    !/session->backend_activate\s*\(session->backend_context\)/.test(
      compilationSessionSource,
    ) ||
    !/session->backend_deactivate\s*\(session->backend_context\)/.test(
      compilationSessionSource,
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
    !/ag_compilation_session_activate\s*\(/.test(compilerMainSource) ||
    !/ag_compilation_session_destroy\s*\(/.test(compilerMainSource) ||
    !/ag_compilation_session_tokenizer\s*\(/.test(compilerMainSource) ||
    /tk_get_default_context\s*\(/.test(compilerMainSource) ||
    /ag_target_set_pointer_size\s*\(/.test(compilerMainSource) ||
    !/pp_stream_open_for_target\s*\(/.test(compilerMainSource) ||
    !/diag_context_publish\s*\(/.test(compilerMainSource) ||
    !/static\s+ag_compilation_session_t\s*\*wasm_adapter_session\s*;/.test(
      compilerMainSource,
    ) ||
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
    !/ag_compilation_session_is_active\s*\(/.test(
      compilationSessionHeader,
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

const irBuilderActiveSessionReads =
  irBuilderSource.match(/\bag_compilation_session_active_compat\s*\(\)/g) ?? [];
const irBuilderRange = (start, end) => {
  const startIndex = irBuilderSource.indexOf(start);
  return irBuilderSource.slice(
    startIndex,
    irBuilderSource.indexOf(end, startIndex),
  );
};
const irTargetOnlyEntryBodies = [
  irBuilderRange(
    "ir_build_module_for_target(",
    "int ir_build_emit_function_for_target(",
  ),
  irBuilderRange(
    "ir_build_emit_function_for_target(",
    "\nint ir_build_emit_function_with_options(",
  ),
  irBuilderRange(
    "ir_build_function_module_for_target(",
    "\nir_module_t *ir_build_function_module_with_options(",
  ),
  irBuilderRange(
    "ir_build_each_and_emit_for_target(",
    "\nint ir_build_each_and_emit_with_options(",
  ),
];
if (!/typedef\s+struct\s*\{[\s\S]*?const\s+ag_target_info_t\s*\*target\s*;[\s\S]*?const\s+psx_record_decl_table_t\s*\*record_decls\s*;[\s\S]*?const\s+ag_continuation_options_t\s*\*continuation\s*;[\s\S]*?ag_diagnostic_context_t\s*\*diagnostic_context\s*;[\s\S]*?\}\s*ir_build_options_t\s*;/.test(
      irBuilderHeader,
    ) ||
    !/ctx->configured_continuation/.test(irBuilderSource) ||
    !/ctx->continuation\s*=\s*NULL\s*;[\s\S]*?ctx->continuation_while\s*=\s*NULL\s*;/.test(
      irBuilderSource,
    ) ||
    irBuilderActiveSessionReads.length !== 0 ||
    irTargetOnlyEntryBodies.some((body) =>
      !body.includes("ir_build_options_for_target(") ||
      !body.includes("diag_context_create(") ||
      !body.includes("diag_context_destroy(") ||
      body.includes("ir_build_options_for_active_session(")
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

if (!/int\s+ag_compilation_session_deactivate\s*\(/.test(
      compilationSessionHeader,
    ) ||
    !/int\s+ag_compilation_session_dispose\s*\(/.test(
      compilationSessionInternalHeader,
    ) ||
    !/active_compilation_session\s*!=\s*session/.test(
      compilationSessionSource,
    ) ||
    !/ag_compilation_session_is_active\s*\([^)]*\)\s*\{[\s\S]*?active_compilation_session\s*==\s*session/.test(
      compilationSessionSource,
    ) ||
    !/session->is_active\s*&&\s*!ag_compilation_session_deactivate\s*\(session\)/.test(
      compilationSessionSource,
    )) {
  throw new Error(
    "CompilationSession lifecycle must reject out-of-order deactivation and disposal",
  );
}

if (/ag_compilation_session_(?:active_compat|effective_target_compat)\s*\(/.test(
      `${compilationSessionSource}\n${preprocessSource}\n${irBuilderSource}`,
    ) ||
    /\bag_target_pointer_size\s*\(/.test(preprocessSource) ||
    /\bag_target_pointer_size\s*\(/.test(irBuilderSource)) {
  throw new Error(
    "preprocess and IR core APIs must receive their target explicitly",
  );
}

const frontendTranslationUnitSessionSource = await readFile(
  "src/frontend/translation_unit.c",
  "utf8",
);

if (!/arena_alloc_in\s*\(/.test(parserArenaSource) ||
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
    !/cg_emitf_in\s*\(wasm32_ir_emit_context\s*\(\)/.test(wasmIrSource) ||
    /\bcg_context_active\s*\(/.test(wasmIrSource) ||
    /\bcg_emitf\s*\(/.test(wasmIrSource)) {
  throw new Error(
    "Wasm text backend must emit through its injected CompilationSession context",
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

if (/s\.ctx\s*=\s*ctx\s*\?\s*ctx\s*:\s*tk_get_default_context\s*\(\)/.test(
      tokenizerSource,
    ) ||
    /tk_tokenize_ctx\s*\(\s*tk_get_default_context\s*\(\)/.test(
      tokenizerSource,
    ) ||
    !/s\.ctx\s*=\s*ctx\s*\?\s*ctx\s*:\s*tk_context_active\s*\(\)/.test(
      tokenizerSource,
    )) {
  throw new Error(
    "context-free tokenization must use the active tokenizer context",
  );
}

if (!/wasm32_ir_context_create\s*\(/.test(wasmBackendContextSource) ||
    !/wasm32_obj_context_create\s*\(/.test(wasmBackendContextSource) ||
    !/wasm32_ir_context_activate\s*\(/.test(wasmBackendContextSource) ||
    !/wasm32_obj_context_activate\s*\(/.test(wasmBackendContextSource) ||
    !/wasm32_ir_context_destroy\s*\(/.test(wasmBackendContextSource) ||
    !/wasm32_obj_context_destroy\s*\(/.test(wasmBackendContextSource) ||
    !/attach_wasm_backend_context\s*\(/.test(compilerMainSource)) {
  throw new Error(
    "Wasm compilation entry points must attach session-owned IR and object contexts",
  );
}
const loweringStateSources = await Promise.all([
  readFile("src/lowering/local_storage.c", "utf8"),
  readFile("src/lowering/static_local_lowering.c", "utf8"),
  readFile("src/lowering/compound_literal_lowering.c", "utf8"),
  readFile("src/lowering/cast_lowering.c", "utf8"),
  readFile("src/lowering/assignment_lowering.c", "utf8"),
  readFile("src/lowering/member_access_lowering.c", "utf8"),
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
    )) {
  throw new Error(
    "frontend function iteration must expose only Typed HIR roots",
  );
}
const parserStreamHeader = await readFile("src/parser/parser.h", "utf8");
const parserStreamSource = await readFile("src/parser/parser.c", "utf8");
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
if (/node_t\s*\*psx_frontend_/.test(semanticPipelineHeader) ||
    !/psx_frontend_resolve_function_to_hir_in_session/.test(
      semanticPipelineHeader,
    ) ||
    !/psx_frontend_resolve_function_work_tree_in_session/.test(
      semanticPipelineInternalHeader,
    ) ||
    /semantic_pipeline_internal\.h/.test(compilerMainSource)) {
  throw new Error(
    "public semantic pipeline APIs must expose Typed HIR while work-tree APIs remain internal",
  );
}
const semanticPassSource = await readFile(
  "src/semantic/semantic_pass.c",
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
const toplevelDeclarationHeader = await readFile(
  "src/parser/toplevel_declaration_syntax.h",
  "utf8",
);
const toplevelDeclarationFrontendSource = await readFile(
  "src/frontend/toplevel_declaration.c",
  "utf8",
);
const frontendFunctionDefinitionSource = await readFile(
  "src/frontend/function_definition.c",
  "utf8",
);
const toplevelDeclarationSyntaxSource = await readFile(
  "src/parser/toplevel_declaration_syntax.c",
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
if (/\bps_(?:ctx_active|global_registry_active|local_registry_active)\s*\(/.test(
      localDeclarationPipelineSource,
    )) {
  throw new Error(
    "declaration pipelines must use only their explicit compiler contexts",
  );
}
const ambiguousFrontendContextApis =
  /psx_(?:frontend_analyze_(?:function|expression|initializer_syntax|program)|apply_function_definition_header|apply_toplevel_declaration|frontend_init_(?:toplevel|local)_declaration_callbacks)_in_context\s*\(/;
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
    !/unsigned\s+char\s+owns_session_activation\s*;/.test(
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
    !/psx_local_registry_t\s*\*local_registry\s*;/.test(
      parserStreamHeader,
    ) ||
    !/ps_parser_stream_begin_in_contexts\s*\(/.test(parserStreamSource) ||
    !/psx_frontend_resolve_function_work_tree_in_session\s*\(/.test(
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
    !/!ag_compilation_session_is_active\s*\(session\)/.test(
      frontendStreamCore,
    ) ||
    !/ag_compilation_session_activate\s*\(session\)/.test(
      frontendStreamCore,
    ) ||
    !/!ag_compilation_session_is_active\s*\(stream->session\)/.test(
      frontendStreamCore,
    ) ||
    /ag_compilation_session_active_compat\s*\(/.test(
      frontendStreamCore,
    ) ||
    !/ag_compilation_session_deactivate\s*\(stream->session\)/.test(
      frontendStreamCore,
    )) {
  throw new Error(
    "frontend stream core must bind explicit registries and active subsystem state to one CompilationSession scope",
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
  irBuilderSource,
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
    core: irBuilderHeader,
    source: irBuilderSource,
    names: [
      "ir_build_module",
      "ir_build_each_and_emit",
      "ir_build_emit_function",
      "ir_build_function_module",
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
const toplevelCallbackCore = toplevelDeclarationFrontendSource.slice(
  toplevelDeclarationFrontendSource.indexOf("static void *begin_declaration("),
  toplevelDeclarationFrontendSource.indexOf(
    "const psx_toplevel_declaration_callbacks_t *",
  ),
);
if (/\bps_(?:ctx_active|global_registry_active|local_registry_active)\s*\(/.test(
      toplevelCallbackCore,
    ) ||
    /callbacks\s*\?\s*callbacks->(?:semantic_context|global_registry|local_registry)/.test(
      toplevelCallbackCore,
    ) ||
    /callbacks\s*&&\s*callbacks->context\s*\?\s*callbacks->context/.test(
      toplevelDeclarationSyntaxSource,
    )) {
  throw new Error(
    "declaration callback core must use its explicit registry ownership",
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
  /typedef struct\s*\{([\s\S]*?)\}\s*psx_aggregate_member_declaration_resolution_t\s*;/,
);
const aggregateLayoutStateType = aggregateMemberResolutionHeader.match(
  /typedef struct\s*\{([\s\S]*?)\}\s*psx_aggregate_layout_state_t\s*;/,
);
const aggregateMemberRequestType = aggregateMemberResolutionHeader.match(
  /typedef struct\s*\{([\s\S]*?)\}\s*psx_aggregate_member_declaration_request_t\s*;/,
);
if (!aggregateMemberResolutionType ||
    !/\bpsx_type_id_t\s+type_id\s*;/.test(
      aggregateMemberResolutionType[1],
    ) ||
    !/\bps_ctx_intern_qual_type_in\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bps_type_sizeof_id_for_target\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bps_type_sizeof_id_with_records\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bps_type_alignof_id_with_records\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    !/\bps_ctx_record_layout_table_in\s*\(/.test(
      aggregateMemberResolutionSource,
    ) ||
    /\baggregate_definition\b/.test(aggregateMemberResolutionSource) ||
    !/\bpsx_semantic_type_table_array_leaf\s*\(/.test(
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
  await readFile("src/frontend/local_declaration.c", "utf8"),
  await readFile("src/frontend/function_definition.c", "utf8"),
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
const memberAccessAstHeader = await readFile(
  "src/parser/ast.h",
  "utf8",
);
const memberAccessTargetLoweringSource = await readFile(
  "src/lowering/member_access_lowering.c",
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
      memberAccessResolutionSource,
    ) ||
    /\baggregate_definition\b/.test(memberAccessResolutionSource) ||
    /\bps_type_find_aggregate_member\s*\(/.test(
      memberAccessResolutionSource,
    ) ||
    !/\bps_ctx_get_record_decl_in\s*\(/.test(
      memberAccessResolutionSource,
    ) ||
    !/resolution->member_index\s*=\s*aggregate_member_index/.test(
      memberAccessResolutionSource,
    ) ||
    !/resolution->record_id\s*=/.test(memberAccessResolutionSource) ||
    !/\bpsx_record_member_decl_t\s+declaration\s*;/.test(
      memberAccessResolutionHeader,
    ) ||
    /\btag_member_info_t\s+member\s*;/.test(
      memberAccessResolutionHeader,
    ) ||
    !/\bpsx_record_member_decl_t\s*\*\s*resolved_member\s*;/.test(
      memberAccessAstHeader,
    ) ||
    /\btag_member_info_t\s*\*\s*resolved_member\s*;/.test(
      memberAccessAstHeader,
    ) ||
    /resolution->declaration\.(?:offset|bit_offset)\b/.test(
      memberAccessResolutionSource,
    ) ||
    !/\bpsx_record_layout_table_lookup\s*\(/.test(
      memberAccessTargetLoweringSource,
    ) ||
    !/\bpsx_record_layout_member\s*\(/.test(
      memberAccessTargetLoweringSource,
    ) ||
    /member->(?:offset|bit_offset|bit_width)\s*=/.test(
      memberAccessTargetLoweringSource,
    ) ||
    /\btag_member_info_t\b/.test(
      memberAccessTargetLoweringSource,
    ) ||
    !/\bps_node_new_tag_member_deref_with_layout_for_in\s*\(/.test(
      memberAccessTargetLoweringSource,
    )) {
  throw new Error(
    "member access semantics must retain RecordId and ordinal while lowering resolves target offsets",
  );
}
if (!/\bpsx_qual_type_t\s+base_object_qual_type\s*;/.test(
      memberAccessResolutionHeader,
    ) ||
    !/\bpsx_semantic_type_table_aggregate_object\s*\(/.test(
      memberAccessResolutionSource,
    ) ||
    /\bps_type_find_aggregate_object_type\s*\(/.test(
      memberAccessResolutionSource,
    ) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(
      memberTypeIdentitySource.match(
        /psx_qual_type_t\s+psx_semantic_type_table_aggregate_object\s*\([^]*?\n\}/,
      )?.[0] || "",
    ) ||
    !/resolution\.base_object_qual_type\.qualifiers/.test(
      semanticPassSource,
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
      semanticPassSource,
    ) ||
    !/ps_node_reject_const_qual_discard_at_in\s*\(\s*semantic_context\s*,/.test(
      semanticPassSource,
    )) {
  throw new Error(
    "semantic qualifier diagnostics must read self and pointee qualifiers through QualType relations",
  );
}
const typeNameResolutionSource = await readFile(
  "src/semantic/type_name_resolution.c",
  "utf8",
);
const typeQueryResolutionSource = await readFile(
  "src/semantic/type_query_resolution.c",
  "utf8",
);
if (!/\bps_ctx_type_sizeof_in\s*\(/.test(typeQueryResolutionSource) ||
    !/\bps_ctx_type_alignof_in\s*\(/.test(typeQueryResolutionSource) ||
    /\bps_type_(?:size|align)of_id_with_records\s*\(/.test(
      typeQueryResolutionSource,
    ) ||
    /\bps_type_(?:size|align)of_for_target\s*\(/.test(
      typeQueryResolutionSource,
    )) {
  throw new Error(
    "sizeof and alignof resolution must intern semantic types and query target layout by TypeId",
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
      frontendSemanticPipelineSource,
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
const enumConstSource = await readFile("src/parser/enum_const.c", "utf8");
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
    !/ps_name_classifier_is_typedef_name\s*\(/.test(
      statementParserSource,
    ) ||
    /active_local_declarations/.test(statementParserSource) ||
    !/psx_expr_expr_in_contexts\s*\(/.test(statementParserSource) ||
    !/psx_parse_statement_expression_in_contexts\s*\(/.test(
      expressionParserSource,
    ) ||
    !/psx_parse_initializer_syntax_list_in_contexts\s*\(/.test(
      expressionParserSource,
    ) ||
    !/ps_ctx_record_unsupported_gnu_extension_warning_in\s*\(/.test(
      initializerSyntaxSource,
    ) ||
    !/psx_parse_declarator_syntax_tree_into_with_typedef_lookup_in_contexts\s*\(/.test(
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
const resolvedGlobalAstSource = await readFile("src/parser/ast.h", "utf8");
const constantExpressionSource = await readFile(
  "src/semantic/constant_expression.c",
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
    !/lower_ir_global_symbol\s*\(\s*ctx->m\s*,\s*gv->symbol\s*,\s*ctx->semantic_types\s*,\s*ctx->record_decls\s*,\s*ctx->record_layouts\s*,\s*ctx->target\s*\)/.test(
      irBuilderSource,
    )) {
  throw new Error(
    "resolved global references must retain symbol identity without active-registry lookup",
  );
}
if (/\b(?:semantic_context|ps_ctx_|ps_gvar_symbol_ref_named_function_in)\b/.test(
      irSymbolLoweringSource,
    ) ||
    !/ps_gvar_walk_resolved_aggregate_initializer\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    !/ir_abi_callable_sig_from_type_id\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    /\bps_type_sizeof\s*\(/.test(abiLoweringSource) ||
    !/\bps_type_sizeof_id_with_records\s*\(/.test(abiLoweringSource)) {
  throw new Error(
    "IR ABI lowering must classify resolved function references from TypeId and target layout",
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
    !/\bpsx_qual_type_t\s+qual_type\s*;/.test(nodeStruct[1]) ||
    /\b(?:unsigned_override|has_unsigned_override)\b/.test(nodeStruct[1])) {
  throw new Error(
    "node_t value identity must retain both its canonical type view and QualType",
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
    !/\bpsx_qual_type_t\s+signature_qual_type\s*;/.test(
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
    !/\bpsx_qual_type_t\s+callee_qual_type\s*;/.test(
      functionCallStruct[1],
    ) ||
    /\b(?:parameters|signature|lvars|is_static)\b/.test(
      functionCallStruct[1],
    )) {
  throw new Error(
    "function definitions and calls must use disjoint canonical AST records",
  );
}
const classifyCallParam = irBuilderSource.match(
  /static\s+ir_abi_param_info_t\s+classify_call_param\s*\([^]*?\n\}/,
);
const attachCallableFromCallee = irBuilderSource.match(
  /static\s+void\s+attach_callable_type_from_callee\s*\([^]*?\n\}/,
);
if (!classifyCallParam ||
    !/ps_function_call_callee_qual_type\s*\(/.test(classifyCallParam[0]) ||
    !/psx_semantic_type_table_parameter\s*\(/.test(classifyCallParam[0]) ||
    /(?:callee_type|param_types)\s*(?:->|\[)/.test(classifyCallParam[0]) ||
    !attachCallableFromCallee ||
    !/ps_node_qual_type\s*\(/.test(attachCallableFromCallee[0]) ||
    /ps_node_get_type\s*\(/.test(attachCallableFromCallee[0])) {
  throw new Error(
    "IR callable ABI lowering must consume finalized TypeId relations",
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
const storageSlotConstructor = nodeUtilsSource.match(
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
    !/ps_node_array_decay_pointer_arith_type_in\s*\(/.test(
      expressionLoweringSource,
    )) {
  throw new Error(
    "array decay result typing must follow semantic type shape without querying target layout",
  );
}
const parserTypeImplementationSource = await readFile(
  "src/parser/type.c",
  "utf8",
);
const memberAccessLoweringSource = await readFile(
  "src/lowering/member_access_lowering.c",
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
if (!explicitWidthShiftConstructor ||
    /\bps_type_(?:size|align)of\s*\(/.test(
      explicitWidthShiftConstructor[0],
    ) ||
    !/ps_node_new_shift_trunc_extend_for_width_in\s*\([^;]*\bsource_width\s*\/\s*8\s*,/s.test(
      explicitDiagnosticCastLoweringSource,
    )) {
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
const assignmentLoweringSource = await readFile(
  "src/lowering/assignment_lowering.c",
  "utf8",
);
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
    !initializerLeafType ||
    !/\bpsx_type_id_t\s+type_id\s*;/.test(initializerLeafType[1]) ||
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
    !/\bps_type_sizeof_id_with_records\s*\(/.test(
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
const irWarningCalls = callBodies(irBuilderSource, "diag_warn_tokf_in");
const continuationValidationSource = irBuilderSource.slice(
  irBuilderSource.indexOf("static int prepare_continuation_entry("),
  irBuilderSource.indexOf("\nstatic void fail(", irBuilderSource.indexOf(
    "static int prepare_continuation_entry(",
  )),
);
const continuationDiagnosticCalls = callBodies(
  continuationValidationSource, "diag_emit_tokf_in",
);
const wasmObjectOutputSource = wasmObjSource.slice(
  wasmObjSource.indexOf("void wasm32_obj_end("),
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
    continuationDiagnosticCalls.length !== 4 ||
    continuationDiagnosticCalls.some((body) =>
      !body.includes("diag_message_for_in(")) ||
    wasmObjectDiagnosticCalls.length !== 3 ||
    wasmObjectDiagnosticCalls.some((body) =>
      !body.includes("diag_message_for_in(")) ||
    !/DIAG_ERR_CODEGEN_WASM_OBJECT_OPEN_FAILED[\s\S]*?diag_emit_internalf_in\s*\([\s\S]*?diag_message_for_in\s*\(/.test(
      compilerMainSource,
    ) ||
    /continuation entry (?:must|does|requires|permits)|continuation frame condition must/.test(
      irBuilderSource,
    ) ||
    /failed to (?:open|write) Wasm object output|missing Wasm object output sink/.test(
      `${compilerMainSource}\n${wasmObjSource}`,
    ) ||
    !/agc_wasm_set_diagnostic_locale\s*\(/.test(compilerMainSource) ||
    !/agc_wasm_set_diagnostic_locale/.test(diagnosticLocaleAdapterSource) ||
    !/\{ diagnosticLocale \}/.test(diagnosticLocaleToolchainSource) ||
    !/--export=agc_wasm_set_diagnostic_locale/.test(selfHostBuildSource) ||
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
      toplevelDeclarationFrontendSource,
      frontendTranslationUnitSource,
      vlaLoweringSource,
      globalObjectLoweringSource,
      explicitDiagnosticCompoundLiteralLoweringSource,
      memberAccessLoweringSource,
      explicitDiagnosticCastLoweringSource,
      irBuilderSource,
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
    /\b(?:diag_context_(?:active|activate)|active_diagnostic_context|diag_current_context)\b/.test(
      `${diagnosticImplementationSource}\n${diagnosticPublicHeaderSource}`,
    ) ||
    !/diag_published_context\s*\(/.test(diagnosticImplementationSource) ||
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
const nodeKindEnum = astSource.match(
  /typedef enum\s*\{([\s\S]*?)\}\s*node_kind_t\s*;/,
);
const semanticRoleClassifier = semanticInvariantsSource.match(
  /static\s+node_semantic_role_t\s+semantic_role\s*\([^)]*\)\s*\{([\s\S]*?)\n\}/,
);
if (!nodeKindEnum || !semanticRoleClassifier) {
  throw new Error("semantic node role classification must remain inspectable");
}
const declaredNodeKinds = [
  ...new Set(
    [...nodeKindEnum[1].matchAll(/\bND_[A-Z0-9_]+\b/g)].map(
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
    "every AST node kind must have exactly one semantic expression role; " +
      `missing: ${missingNodeKinds.join(", ") || "none"}; ` +
      `duplicates: ${[...new Set(duplicateNodeKinds)].join(", ") || "none"}`,
  );
}
const completeSemanticBoundaryCheckCount = [
  ...semanticPipelineSource.matchAll(
    /\bpsx_require_semantic_tree_has_canonical_expression_types\s*\(/g,
  ),
].length;
const initializerSemanticBoundaryCheckCount = [
  ...semanticPipelineSource.matchAll(
    /\bpsx_require_semantic_initializer_has_canonical_expression_types\s*\(/g,
  ),
].length;
const internedSemanticBoundaryCheckCount = [
  ...semanticPipelineSource.matchAll(
    /\bpsx_require_semantic_tree_has_interned_expression_types\s*\(/g,
  ),
].length;
const internedInitializerBoundaryCheckCount = [
  ...semanticPipelineSource.matchAll(
    /\bpsx_require_semantic_initializer_has_interned_expression_types\s*\(/g,
  ),
].length;
const availableTypeInterningCheckCount = [
  ...semanticPipelineSource.matchAll(
    /\bpsx_require_available_semantic_tree_types_interned\s*\(/g,
  ),
].length;
if (completeSemanticBoundaryCheckCount !== 3 ||
    initializerSemanticBoundaryCheckCount !== 1 ||
    internedSemanticBoundaryCheckCount !== 3 ||
    internedInitializerBoundaryCheckCount !== 1 ||
    availableTypeInterningCheckCount !== 4) {
  throw new Error(
    "every semantic pipeline entry must pre-intern available types and require interned and canonical expression contracts",
  );
}
const semanticPipelineContracts = [
  [
    "function",
    /static\s+node_t\s*\*analyze_function_in_contexts\s*\([^)]*\)\s*\{([\s\S]*?)\n\}/,
    /\bpsx_require_semantic_tree_has_canonical_expression_types\s*\(/,
    /\bpsx_require_semantic_tree_has_interned_expression_types\s*\([\s\S]*?\bpsx_require_semantic_tree_has_canonical_expression_types\s*\(/,
    /\bpsx_require_available_semantic_tree_types_interned\s*\([\s\S]*?\bpsx_lower_semantic_tree_in_contexts\s*\(/,
  ],
  [
    "expression",
    /node_t\s*\*psx_frontend_analyze_expression_in_contexts\s*\([^)]*\)\s*\{([\s\S]*?)\n\}/,
    /\bpsx_require_semantic_tree_has_canonical_expression_types\s*\(/,
    /\bpsx_require_semantic_tree_has_interned_expression_types\s*\([\s\S]*?\bpsx_require_semantic_tree_has_canonical_expression_types\s*\(/,
    /\bpsx_require_available_semantic_tree_types_interned\s*\([\s\S]*?\bpsx_lower_semantic_tree_in_contexts\s*\(/,
  ],
  [
    "initializer",
    /node_t\s*\*psx_frontend_analyze_initializer_syntax_in_contexts\s*\([^)]*\)\s*\{([\s\S]*?)\n\}/,
    /\bpsx_require_semantic_initializer_has_canonical_expression_types\s*\(/,
    /\bpsx_require_semantic_initializer_has_interned_expression_types\s*\([\s\S]*?\bpsx_require_semantic_initializer_has_canonical_expression_types\s*\(/,
    /\bpsx_require_available_semantic_tree_types_interned\s*\([\s\S]*?\bpsx_lower_semantic_initializer_syntax_in_contexts\s*\(/,
  ],
  [
    "program",
    /void\s+psx_frontend_analyze_program_in_contexts\s*\([^)]*\)\s*\{([\s\S]*?)\n\}/,
    /\bpsx_require_semantic_tree_has_canonical_expression_types\s*\(/,
    /\bpsx_require_semantic_tree_has_interned_expression_types\s*\([\s\S]*?\bpsx_require_semantic_tree_has_canonical_expression_types\s*\(/,
    /\bpsx_require_available_semantic_tree_types_interned\s*\([\s\S]*?\bpsx_lower_semantic_tree_in_contexts\s*\(/,
  ],
];
for (const [
  name,
  boundaryPattern,
  contractPattern,
  internedBeforeCanonicalPattern,
  availableBeforeLoweringPattern,
] of semanticPipelineContracts) {
  const boundary = semanticPipelineSource.match(boundaryPattern);
  if (!boundary || !contractPattern.test(boundary[1]) ||
      !internedBeforeCanonicalPattern.test(boundary[1]) ||
      !availableBeforeLoweringPattern.test(boundary[1])) {
    throw new Error(
      `${name} semantic pipeline must intern available types before lowering and enforce its final expression contracts`,
    );
  }
}
if (!/\bpsx_walk_semantic_tree\s*\(/.test(semanticInvariantsSource) ||
    !/\bpsx_walk_semantic_tree_mut\s*\(/.test(
      semanticTypeIdentityPassSource,
    ) ||
    !/node->qual_type\s*=\s*type\s*;/.test(
      semanticTypeIdentityPassSource,
    ) ||
    !/actual\.type_id\s*==\s*PSX_TYPE_ID_INVALID/.test(
      semanticInvariantsSource,
    ) ||
    !/node->type\s*!=\s*ps_ctx_type_by_id_in\s*\(/.test(
      semanticInvariantsSource,
    ) ||
    !/\bpsx_finalize_semantic_tree_types\s*\(/.test(
      semanticInvariantsSource,
    ) ||
    !/\bwalk_node\s*\(\s*node->lhs\b/.test(semanticTreeWalkSource) ||
    /\bvalidate_tree\s*\(\s*validation\s*,\s*node->(?:lhs|rhs)\b/.test(
      semanticInvariantsSource,
    )) {
  throw new Error(
    "semantic type finalization must intern QualType and materialize canonical TypeId objects in one AST traversal",
  );
}
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
if (!/arena_alloc_in\s*\(\s*arena_context\s*,\s*sizeof\s*\(\s*node_source_cast_t\s*\)\s*\)/.test(
      nodeUtilsSource,
    ) ||
    /node_source_cast_t\s*\*[^;=]*=\s*arena_alloc(?:_in)?\s*\([^;]*sizeof\s*\(\s*node_num_t\s*\)/.test(
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
if (!/\bnode_t\s*\*\s*lower_compound_literal_expression_in_contexts\s*\(/.test(
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
  "src/semantic/declaration_application.c",
  "src/semantic/type_name_resolution.c",
  "src/semantic/semantic_pass.c",
  "src/semantic/generic_selection_resolution.c",
  "src/semantic/static_initializer_resolution.c",
  "src/semantic/type_query_resolution.c",
  "src/semantic/identifier_binding.c",
  "src/semantic/function_call_resolution.c",
  "src/semantic/expression_operand_resolution.c",
  "src/semantic/type_identity.c",
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
const nodeUtilsHeaderSource = await readFile(
  "src/parser/node_utils.h",
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
    !/\bconst\s+psx_type_t\s*\*\s*decl_type\s*;/.test(
      recordMemberDeclStruct[1],
    ) ||
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
    !/\bpsx_type_t\s*\*\s*type\s*;/.test(tagMemberDeclStruct[1]) ||
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
const typeIdLayoutFunction = typeLayoutSource.match(
  /int\s+ps_type_layout_of_id\s*\([^]*?\n\}/,
);
const recordTypeIdLayoutFunction = typeLayoutSource.match(
  /int\s+ps_type_layout_of_id_with_records\s*\([^]*?\n\}/,
);
if (!/\bps_type_layout_of_id\s*\(/.test(typeLayoutSource) ||
    !/\bpsx_semantic_type_table_lookup\s*\(/.test(typeLayoutSource) ||
    !/\bpsx_semantic_type_table_base\s*\(/.test(typeLayoutSource) ||
    /\baggregate_definition\b/.test(typeLayoutSource) ||
    /\bps_type_(?:layout_of|sizeof_for_target|alignof_for_target)\s*\(/.test(
      `${typeLayoutHeader}\n${typeLayoutSource}`,
    ) ||
    /\btype\s*->\s*(?:size|align)\b/.test(typeLayoutSource) ||
    !typeIdLayoutFunction ||
    !/\blayout_of_id\s*\(/.test(typeIdLayoutFunction[0]) ||
    /\bps_type_layout_of\s*\(/.test(typeIdLayoutFunction[0]) ||
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
if (!recordTypeIdLayoutFunction ||
    !/\blayout_of_id_with_records\s*\(/.test(
      recordTypeIdLayoutFunction[0],
    ) ||
    !/\bpsx_record_layout_table_lookup\s*\(/.test(typeLayoutSource) ||
    /aggregate_definition\s*->\s*(?:size|align)/.test(
      typeLayoutSource.match(
        /static\s+int\s+layout_non_array_with_records\s*\([^]*?\n\}/,
      )?.[0] ?? "",
    )) {
  throw new Error(
    "record ABI layout must be an explicit RecordLayoutTable input separate from TypeId",
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
      !/\bps_type_sizeof_id_with_records\s*\(/.test(source) ||
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
  ["expression", expressionLoweringSource, /\bps_lowering_type_deref_size\s*\(/],
  ["subscript", subscriptLoweringSource, /\bps_type_sizeof_id_with_records\s*\(/],
  ["initializer", explicitDiagnosticInitializerLoweringSource, /\bps_type_sizeof_id_with_records\s*\(/],
  ["VLA", vlaLoweringSource, /\bps_type_sizeof_id_with_records\s*\(/],
  ["static data initializer", explicitDiagnosticStaticDataInitializerSource, /\bps_type_sizeof_id_with_records\s*\(/],
  ["translation unit data", translationUnitDataLoweringSource, /\bps_type_sizeof_id_with_records\s*\(/],
]) {
  if (!requiredLayoutCall.test(source) ||
      /\bps_type_(?:size|align)of_for_target\s*\(/.test(source)) {
    throw new Error(
      `${name} lowering must obtain target layout through an interned TypeId`,
    );
  }
}
const automaticLocalPipeline = declarationPipelineSource.match(
  /int\s+psx_begin_automatic_local_declaration_pipeline\s*\([^]*?\n\}/,
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
  memberAccessLoweringSource,
  declarationPipelineSource,
  irBuilderSource,
].join("\n");
const irNodeTypeSize = irBuilderSource.match(
  /static\s+int\s+ir_node_type_size\s*\([^]*?\n\}/,
);
const irAggregateSizeFromNode = irBuilderSource.match(
  /static\s+int\s+aggregate_size_from_node\s*\([^]*?\n\}/,
);
if (!/\bps_lowering_type_size\s*\(/.test(loweringRuntimeHeader) ||
    !/\bps_lowering_type_id_size\s*\(/.test(loweringRuntimeHeader) ||
    !/\bps_lowering_type_id_alignment\s*\(/.test(loweringRuntimeHeader) ||
    !/\bps_lowering_type_deref_size\s*\(/.test(loweringRuntimeHeader) ||
    !/\bps_lowering_type_alignment\s*\(/.test(loweringRuntimeHeader) ||
    !/\bps_type_sizeof_id_with_records\s*\(/.test(
      loweringRuntimeSource,
    ) ||
    !/\bps_type_alignof_id_with_records\s*\(/.test(
      loweringRuntimeSource,
    ) ||
    !irNodeTypeSize ||
    !/\bps_node_qual_type\s*\(/.test(irNodeTypeSize[0]) ||
    /\bps_node_get_type\s*\(/.test(irNodeTypeSize[0]) ||
    !irAggregateSizeFromNode ||
    !/\bps_node_qual_type\s*\(/.test(irAggregateSizeFromNode[0]) ||
    !/\baggregate_size_from_type_id\s*\(/.test(
      irAggregateSizeFromNode[0],
    ) ||
    /\bps_node_get_type\s*\(/.test(irAggregateSizeFromNode[0]) ||
    /\baggregate_size_from_type\s*\(/.test(irBuilderSource) ||
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
  ["expression", expressionLoweringSource],
  ["assignment", assignmentLoweringSource],
  ["cast", explicitDiagnosticCastLoweringSource],
  ["IR builder", irBuilderSource],
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
if (!targetCanonicalSignatureSection ||
    /\bps_type_sizeof\s*\(/.test(targetCanonicalSignatureSection[0]) ||
    !/\bconst\s+ag_target_info_t\s*\*target\b/.test(
      targetCanonicalSignatureSection[0],
    ) ||
    !/\bag_target_info_scalar_size\s*\(/.test(
      targetCanonicalSignatureSection[0],
    ) ||
    !/\bps_type_format_canonical_signature_for_target\s*\([^]*?ctx->target/.test(
      irBuilderSource,
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
const qualTypeStruct = typeIdsHeader.match(
  /typedef\s+struct\s*\{([^]*?)\}\s*psx_qual_type_t\s*;/,
);
if (!qualTypeStruct ||
    !/\bpsx_type_id_t\s+type_id\s*;/.test(qualTypeStruct[1]) ||
    !/\bpsx_type_qualifiers_t\s+qualifiers\s*;/.test(qualTypeStruct[1]) ||
    !/#include\s+"\.\.\/type_system\/type_ids\.h"/.test(
      semanticTypeIdentityHeader,
    ) ||
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
    )) {
  throw new Error(
    "semantic type interning must resolve record relations through RecordDeclTable without retaining embedded declarations",
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
    !/ps_type_record_id\s*\(\s*canonical\s*\)\s*!=\s*PSX_RECORD_ID_INVALID/.test(
      semanticTypeNodeMatcher[0],
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
    !/\bps_ctx_type_sizeof_in\s*\(/.test(semanticContextSource) ||
    !/\bps_ctx_type_alignof_in\s*\(/.test(semanticContextSource) ||
    /\bps_ctx_refresh_type_completeness_in\s*\(/.test(
      `${semanticContextSource}\n${parserSemanticContextImplementation}`,
    ) ||
    /\bps_type_sizeof\s*\(/.test(parserSemanticContextImplementation)) {
  throw new Error(
    "semantic context must expose immutable type identity and target layout queries",
  );
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

const vlaDeclarationResolutionSource = await readFile(
  "src/semantic/declaration_resolution.h",
  "utf8",
);
const runtimeArrayBoundStruct = vlaDeclarationResolutionSource.match(
  /typedef\s+struct\s*\{([^]*?)\}\s*psx_runtime_array_bound_t\s*;/,
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
if (!runtimeArrayBoundStruct ||
    !/\bpsx_semantic_expr_id_t\s+expression_id\s*;/.test(
      runtimeArrayBoundStruct[1],
    ) ||
    /\bnode_t\s*\*/.test(runtimeArrayBoundStruct[1]) ||
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
    !/\bpsx_semantic_expr_id_t\s+pointer_row_dimension_id\s*;/.test(
      localDeclarationResolutionSource,
    ) ||
    /\bnode_t\s*\*\s*pointer_row_dimension\s*;/.test(
      localDeclarationResolutionSource,
    ) ||
    /\bpsx_semantic_expr_id_t\b/.test(canonicalTypeStruct[1])) {
  throw new Error(
    "VLA runtime bounds must use semantic expression IDs outside canonical types",
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
if (!/PSX_VLA_RUNTIME_SLOT_SIZE\s*=\s*8\b/.test(
      vlaRuntimeHeaderSource,
    ) ||
    !/PSX_VLA_RUNTIME_DESCRIPTOR_HEADER_SIZE/.test(frameLayoutSource) ||
    !/PSX_VLA_RUNTIME_SLOT_SIZE/.test(frameLayoutSource) ||
    !/PSX_VLA_RUNTIME_SLOT_SIZE/.test(vlaLoweringSource) ||
    !/PSX_VLA_RUNTIME_SLOT_SIZE/.test(expressionLoweringSource) ||
    !/PSX_VLA_RUNTIME_SLOT_SIZE/.test(subscriptLoweringSource) ||
    !/PSX_VLA_RUNTIME_SLOT_SIZE/.test(typeQueryResolutionSource) ||
    !/PSX_VLA_RUNTIME_SLOT_SIZE/.test(irBuilderSource) ||
    !/AG_TARGET_SCALAR_LONG_LONG[^]*?PSX_VLA_RUNTIME_SLOT_SIZE/.test(
      parameterDeclarationResolutionSource,
    ) ||
    !/ps_type_new_integer_kind_in\s*\([^]*?PSX_INTEGER_KIND_LONG_LONG/.test(
      parameterDeclarationResolutionSource,
    ) ||
    /\b8\s*\*\s*(?:count|level|stride_count|subscript_depth)/.test(
      frameLayoutSource + vlaLoweringSource + typeQueryResolutionSource +
        irBuilderSource,
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
    !/psx_semantic_type_table_callable_function\s*\([^]*?psx_semantic_type_table_base\s*\(/.test(
      semanticPassSource,
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
    !/\bps_type_sizeof_id_with_records\s*\(/.test(
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
    !/\bstatic\s+int\s+resolve_local_decl_type\s*\(/.test(
      localRegistrySource,
    ) ||
    !/\bpsx_semantic_type_table_lookup\s*\(/.test(
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
    !/decl_type_table[^]*?decl_qual_type\.type_id[^]*?psx_semantic_type_table_lookup_qual_type\s*\(/.test(
      lvarDeclTypeViewFunction[0],
    ) ||
    !/decl_type_table[^]*?decl_qual_type\.type_id[^]*?psx_semantic_type_table_lookup_qual_type\s*\(/.test(
      gvarDeclTypeViewFunction[0],
    ) ||
    !/decl_type_table\s*=\s*registry->semantic_types\s*;/.test(
      localRegistrySource,
    ) ||
    !/decl_type_table\s*=\s*registry->semantic_types\s*;/.test(
      globalRegistrySource,
    ) ||
    /\bglobal->decl_type\b/.test(staticLocalLoweringSource) ||
    !/\bps_gvar_get_decl_type\s*\(/.test(staticLocalLoweringSource)) {
  throw new Error(
    "production symbol type views must materialize from declaration QualType identity",
  );
}
if (!/typedef\s+struct\s+psx_record_member_decl_t\s*\{[^]*?decl_type_table[^]*?decl_qual_type[^]*?decl_type[^]*?\}/.test(
      typeSource,
    ) ||
    !/psx_record_member_decl_type\s*\([^]*?psx_semantic_type_table_lookup_qual_type\s*\(/.test(
      parserTypeImplementationSource,
    ) ||
    !/ps_ctx_intern_qual_type_in\s*\([^]*?m->declaration\.qual_type\s*=\s*identity/.test(
      parserSemanticContextImplementation,
    ) ||
    !/qualified_views\s*\[PSX_QUALIFIER_VIEW_COUNT\]/.test(
      semanticTypeIdentitySource,
    )) {
  throw new Error(
    "record member and symbol compatibility views must be materialized from QualType",
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
    !/\bstatic\s+int\s+resolve_global_decl_type\s*\(/.test(
      globalRegistrySource,
    ) ||
    !/\bpsx_semantic_type_table_lookup\s*\(/.test(
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
  ["src/semantic/declaration_resolution.h", "psx_resolve_decl_specifier_syntax_in_context"],
  ["src/semantic/declaration_application.h", "psx_apply_parsed_decl_specifier_in_contexts"],
  ["src/semantic/declaration_application.h", "psx_apply_parsed_type_name_in_contexts"],
  ["src/semantic/declaration_application.h", "psx_apply_parsed_declarator_type_in_contexts"],
  ["src/semantic/declaration_application.h", "psx_apply_runtime_declarator_type_in_context"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_indirection_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_arithmetic_unary_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_binary_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_conditional_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_sequence_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_address_result_type"],
  ["src/semantic/expression_operand_resolution.h", "psx_resolve_incdec_result_type"],
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
if (!/\btype_size_id\s*\(\s*lowering\s*,\s*ps_gvar_decl_type_id\s*\(\s*global\s*\)\s*\)/.test(
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
    !/\bps_type_sizeof_id_with_records\s*\(/.test(
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
    !/\bps_type_(?:size|align)of_id_with_records\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    /\bps_type_(?:size|align)of_id_for_target\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    /\bpsx_semantic_type_table_(?:create|intern)\s*\(/.test(
      irSymbolLoweringSource,
    ) ||
    (irBuilderSource.match(
      /ctx\.record_layouts\s*=\s*options\s*\?\s*options->record_layouts\s*:\s*NULL\s*;/g,
    ) ?? []).length !== 2 ||
    (irBuilderSource.match(
      /ctx\.record_decls\s*=\s*options\s*\?\s*options->record_decls\s*:\s*NULL\s*;/g,
    ) ?? []).length !== 2 ||
    !/\bconst\s+psx_semantic_type_table_t\s*\*semantic_types\s*;/.test(
      irBuilderHeader,
    ) ||
    !/\bconst\s+psx_record_decl_table_t\s*\*record_decls\s*;/.test(
      irBuilderHeader,
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

const parsedNumberLiteral = parserExpressionSource.match(
  /static\s+node_t\s*\*parse_num_literal\s*\([^]*?\n\}\n\n\/\/ 内容文字列/,
);
const parsedStringLiteral = parserExpressionSource.match(
  /static\s+node_string_t\s*\*make_string_lit_node\s*\([^]*?\n\}\n\n\/\/ C11/,
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
    )) {
  throw new Error(
    "parser literals must remain typeless syntax and receive canonical types only in semantic resolution",
  );
}
if (/\bps_type_new_[a-z0-9_]*\s*\(|\bps_node_bind_(?:type|qual_type)\s*\(|(?:->|\.)base\.type\s*=/.test(
      parserExpressionSource,
    ) ||
    /\bps_global_registry_next_(?:string|float)_literal_id\s*\(|\bpsx_register_(?:string|float)_lit_in\s*\(/.test(
      parserExpressionSource,
    ) ||
    !/\bsemantic_resolve_number_literal\s*\([^]*?\bpsx_register_float_lit_in\s*\(/.test(
      semanticPassSource,
    ) ||
    !/\bsemantic_resolve_string_literal\s*\([^]*?\bpsx_register_string_lit_in\s*\(/.test(
      semanticPassSource,
    )) {
  throw new Error(
    "expression parser must build typeless Syntax AST nodes without canonical type or literal-pool registration",
  );
}

if (/parser\/ast\.h|\bnode_t\b|\bnode_kind_t\b|\bpsx_type_t\b/.test(
      hirHeader,
    )) {
  throw new Error(
    "public Typed HIR must not expose parser AST or mutable type objects",
  );
}
if (!/psx_qual_type_t\s+psx_hir_node_qual_type/.test(hirHeader) ||
    !/spec->role\s*==\s*PSX_HIR_ROLE_EXPRESSION[^]*?spec->qual_type\.type_id\s*!=\s*PSX_TYPE_ID_INVALID/.test(
      hirImplementation,
    )) {
  throw new Error(
    "Typed HIR construction must structurally require canonical QualType for expressions",
  );
}
if (!/PSX_TYPED_HIR_BUILD_RAW_SYNTAX_REMAINS/.test(typedHirBuilder)) {
  throw new Error(
    "Typed HIR builder must reject unresolved syntax node kinds",
  );
}
if (!/session->hir_module\s*=\s*psx_hir_module_create\(\)/.test(
      compilationSession,
    )) {
  throw new Error("CompilationSession must own the Typed HIR module");
}
const functionResolutionBoundary = semanticPipelineSource.match(
  /node_t\s*\*psx_frontend_resolve_function_work_tree_in_session\s*\([^)]*\)\s*\{([^]*?)\n\}/,
);
const expressionResolutionBoundary = semanticPipelineSource.match(
  /node_t\s*\*psx_frontend_analyze_expression_in_contexts\s*\([^)]*\)\s*\{([^]*?)\n\}/,
);
const initializerResolutionBoundary = semanticPipelineSource.match(
  /node_t\s*\*psx_frontend_analyze_initializer_syntax_in_contexts\s*\([^)]*\)\s*\{([^]*?)\n\}/,
);
if (!functionResolutionBoundary ||
    !expressionResolutionBoundary ||
    !initializerResolutionBoundary ||
    !/psx_clone_syntax_tree_for_resolution\s*\([^]*?analyze_function_in_contexts\s*\(/.test(
      functionResolutionBoundary[1],
    ) ||
    !/const\s+node_t\s*\*syntax_expression/.test(
      semanticPipelineSource,
    ) ||
    !/psx_clone_syntax_tree_for_resolution\s*\([^]*?psx_bind_identifier_tree_in_contexts\s*\(/.test(
      expressionResolutionBoundary[1],
    ) ||
    !/const\s+node_t\s*\*syntax\s*,\s*const\s+token_t\s*\*fallback_diag_tok/.test(
      semanticPipelineSource,
    ) ||
    !/psx_clone_syntax_tree_for_resolution\s*\([^]*?psx_bind_identifier_initializer_tree_in_contexts\s*\(/.test(
      initializerResolutionBoundary[1],
    ) ||
    !/psx_clone_syntax_tree_for_resolution\s*\([^,]+,\s*const\s+node_t\s*\*syntax_root\s*\)/.test(
      resolutionWorkTree,
    )) {
  throw new Error(
    "frontend resolvers must analyze private working trees and leave parser syntax nodes unchanged",
  );
}
if (!/int\s+psx_frontend_resolve_function_to_hir_in_session\s*\([^)]*psx_hir_node_id_t\s*\*hir_root/.test(
      semanticPipelineSource,
    ) ||
    /node_t\s*\*psx_frontend_analyze_function_in_session/.test(
      semanticPipelineSource,
    )) {
  throw new Error(
    "public function resolution must return a Typed HIR root instead of a semantic node tree",
  );
}
if (/\bnode_t\b|\bND_[A-Z0-9_]+\b|parser\/ast\.h/.test(hirIrBuilder) ||
    !/ir_build_function_module_from_hir\s*\(/.test(hirIrBuilder)) {
  throw new Error(
    "direct Typed HIR IR lowering must not depend on parser AST nodes",
  );
}
if (!/ir_build_function_module_from_hir\s*\(/.test(compilerMainSource) ||
    /ir_build_function_module_with_options\s*\(|AG_(?:DISABLE|REQUIRE)_TYPED_HIR|translation_unit_legacy_ast/.test(
      compilerMainSource,
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

console.log(
  "design invariants: ok (IR/backend isolation, canonical type ownership, and record identity verified)",
);
