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
  .filter((entry) => entry.isFile() && entry.name.endsWith(".c"))
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

const actual = new Map();
const contextBridgeRe =
  /\b(?:psx?_ctx_[A-Za-z0-9_]+|ir_abi_classify_function_param)\b/g;
const irSymbolTypeRe = /\bps_(?:lvar|gvar)_get_decl_type\b/g;
for (const file of backendFiles) {
  const source = await readFile(file, "utf8");
  if (/#[ \t]*include[^\n]*parser\/semantic_ctx\.h/.test(source)) {
    throw new Error(`${file} directly includes parser/semantic_ctx.h`);
  }
  const forbidden = file.startsWith("src/ir/")
    ? [contextBridgeRe, irSymbolTypeRe]
    : [contextBridgeRe];
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
  "static void emit_string_literal_wat_byte",
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
const irBuilderSource = await readFile("src/ir/ir_builder.c", "utf8");
if (!/typedef struct ir_symbol_t\s*\{/.test(irHeaderSource) ||
    !/\bir_symbol_t\s*\*symbols\s*;/.test(irHeaderSource) ||
    !/\blower_ir_global_symbol\s*\(/.test(irBuilderSource) ||
    !/object_size\s*=\s*\(s->byte_len\s*\+\s*1\)/.test(irBuilderSource)) {
  throw new Error(
    "IR lowering must materialize global layout and string size before backend codegen",
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
  "design invariants: ok (backend isolation and read-only canonical type ownership verified)",
);
