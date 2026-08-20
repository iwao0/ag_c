#include "../src/language_analysis.h"
#include "../src/target_info.h"
#include "../src/tokenizer/tokenizer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  unsigned char *bytes;
  size_t length;
} header_bundle_t;

static const char *last_occurrence(const char *text, const char *needle);
static char *project_enum_macro_spaced_call_source(
    const char *source, const char *name);
static char *project_enum_macro_identifier_argument_source(
    const char *source, const char *name, const char *declaration,
    const char *argument);
static char *enum_two_argument_macro_source(
    const char *source, int argument_index, int revision);
static char *enum_two_argument_paired_macro_source(
    const char *source, int missing_argument_mode);
static char *enum_two_argument_paired_rename_source(
    const char *source, int renamed_argument_index,
    int other_argument_missing);
static char *enum_two_argument_paired_update_source(
    const char *source, int updated_argument_index,
    int other_argument_missing);
static char *enum_two_argument_paired_rename_update_source(
    const char *source, int renamed_argument_index,
    int updated_argument_missing);
static char *enum_two_argument_both_renamed_source(
    const char *source, int missing_argument_mode);
static char *enum_three_argument_macro_source(
    const char *source, int missing_argument_mode);

static char *read_fixture_source(const char *path, size_t *length) {
  if (length) *length = 0;
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  char *source = malloc((size_t)size + 1);
  if (!source) {
    fclose(file);
    return NULL;
  }
  size_t read_length = fread(source, 1, (size_t)size, file);
  int close_status = fclose(file);
  if (read_length != (size_t)size || close_status != 0) {
    free(source);
    return NULL;
  }
  source[read_length] = '\0';
  if (length) *length = read_length;
  return source;
}

static void put_u32(unsigned char *out, uint32_t value) {
  out[0] = (unsigned char)value;
  out[1] = (unsigned char)(value >> 8);
  out[2] = (unsigned char)(value >> 16);
  out[3] = (unsigned char)(value >> 24);
}

static header_bundle_t make_bundle(const char **paths, const char **sources,
                                   int count) {
  size_t length = 4;
  for (int i = 0; i < count; i++)
    length += 8 + strlen(paths[i]) + 1 + strlen(sources[i]) + 1;
  unsigned char *bytes = calloc(length, 1);
  put_u32(bytes, (uint32_t)count);
  size_t offset = 4;
  for (int i = 0; i < count; i++) {
    size_t path_len = strlen(paths[i]);
    size_t source_len = strlen(sources[i]);
    put_u32(bytes + offset, (uint32_t)path_len);
    put_u32(bytes + offset + 4, (uint32_t)source_len);
    offset += 8;
    memcpy(bytes + offset, paths[i], path_len);
    offset += path_len + 1;
    memcpy(bytes + offset, sources[i], source_len);
    offset += source_len + 1;
  }
  return (header_bundle_t){bytes, length};
}

static const char project_guard_move_header[] =
    "#ifndef MOVE_H\n"
    "#define MOVE_H\n"
    "\n"
    "/* 日本語 */\n"
    "void move_and_draw(void);\n"
    "\n"
    "#endif\n";
static const char project_guard_other_header[] =
    "#ifndef OTHER_H\n"
    "#define OTHER_H\n"
    "void other_action(void);\n"
    "#endif\n";
static const char project_guard_move_source[] =
    "#include \"move.h\"\n\n"
    "void move_and_draw(void) {}\n";
static const char project_guard_moved_move_source[] =
    "#include \"move.h\"\n\n\n\n"
    "void move_and_draw(void) {}\n";
static const char project_guard_other_source[] =
    "#include \"other.h\"\n\n"
    "void other_action(void) {}\n";
static const char project_guard_main_source[] =
    "#include \"move.h\"\n"
    "#include \"other.h\"\n\n"
    "int main(void) { move_and_draw(); other_action(); return 0; }\n";
static const char project_guard_unterminated_header[] =
    "#ifndef MOVE_H\n"
    "#define MOVE_H\n\n"
    "void move_and_draw(void);\n";
static const char for_control_hover_source[] =
    "/* 日本語 */\n"
    "#define LOOP_LIMIT 8\n"
    "enum { ENEMY_COUNT = 8 };\n"
    "int identity(int value) { return value; }\n"
    "int main(void) {\n"
    "  int outer = 1;\n"
    "  for (\n"
    "#define LOOP_SEED 0; for (;;);\n"
    "       outer = identity(\"for (;;);\"[0] + "
    "/* for (;;); */ outer); // for (;;);\n"
    "       outer < ENEMY_COUNT && outer < LOOP_LIMIT;\n"
    "       outer++, outer += 0) {}\n"
    "  for (;;) { outer += 0; break; }\n"
    "  for (outer = 0; outer < 1;) { outer++; }\n"
    "  for (int inner = 0; inner < ENEMY_COUNT; inner++) {\n"
    "    for (int nested = 0; nested < inner; nested++) {\n"
    "      outer += inner;\n"
    "    }\n"
    "  }\n"
    "  outer += LOOP_LIMIT;\n"
    "  return outer;\n"
    "}\n";
static const char conditional_hover_source[] =
    "/* 日本語 */\n"
    "#define CHOICE_MACRO 7\n"
    "#if 1 ? 1 : 0\n"
    "#define ACTIVE_BRANCH 1\n"
    "#endif\n"
    "enum { FIRST = 1, SECOND = 2, THIRD = 3 };\n"
    "struct Flags { unsigned ready : 1; };\n"
    "int choose(int left, ...) { return left; }\n"
    "int frame(int alternate, int other) {\n"
    "  int local = 4;\n"
    "  const char *text = \"? : \\\"quoted\\\"\";\n"
    "  // ? : ignored\n"
    "  /* ? : ignored */\n"
    "  switch (alternate) { case 0: break; default: break; }\n"
    "conditional_label:\n"
    "  local += choose(alternate ? FIRST : SECOND, "
    "other ? SECOND : THIRD);\n"
    "  local += (alternate ? FIRST : SECOND);\n"
    "  int values[2] = { FIRST, SECOND };\n"
    "  local += values[alternate ? 0 : 1];\n"
    "  local += alternate ? (choose(local, FIRST), SECOND) :\n"
    "                       (choose(local, SECOND), THIRD);\n"
    "  local += alternate ? other ? FIRST : SECOND : THIRD;\n"
    "  local += alternate ? FIRST : other ? SECOND : THIRD;\n"
    "  return alternate ? other ? FIRST : SECOND :\n"
    "         local ? CHOICE_MACRO : THIRD;\n"
    "}\n";
static const char generic_hover_source[] =
    "#define GENERIC_MACRO 9\n"
    "typedef int GenericScore;\n"
    "#define GenericScore() long\n"
    "struct GenericPlayer { int score; };\n"
    "union GenericPayload { int score; };\n"
    "enum GenericState { GENERIC_IDLE = 0 };\n"
    "#define GenericState() int\n"
    "enum { GENERIC_MODE = 3 };\n"
    "#define GENERIC_MODE() 4\n"
    "int generic_value;\n"
    "#define generic_value() 5\n"
    "#define GENERIC_CALL() 6\n"
    "int GENERIC_INVOKED;\n"
    "#define GENERIC_INVOKED() 7\n"
    "?" "?=define GENERIC_TRIGRAPH_PREFIX 11\n"
    "#define GENERIC_SPLICE_PREFIX 12 \\\n"
    "+ 0\n"
    "#define GENERIC_AFTER_TRANSLATION 13\n"
    "int generic_after_translation;\n"
    "int generic_literal_lf_\\\n"
    "value;\n"
    "int generic_literal_crlf_\\\r\n"
    "value;\n"
    "int generic_trigraph_lf_?" "?/\n"
    "value;\n"
    "int generic_trigraph_crlf_?" "?/\r\n"
    "value;\n"
    "int generic_identity(int value) { return value; }\n"
    "int generic_comment_invocation(void) { return GENERIC_INVOKED /* gap */ (); }\n"
    "int generic_lf_invocation(void) { return GENERIC_INVOKED \\\n"
    "(); }\n"
    "int generic_crlf_invocation(void) { return GENERIC_INVOKED \\\r\n"
    "(); }\n"
    "int generic_trigraph_lf_invocation(void) { return GENERIC_INVOKED "
    "?" "?/\n"
    "(); }\n"
    "int generic_trigraph_crlf_invocation(void) { return GENERIC_INVOKED "
    "?" "?/\r\n"
    "(); }\n"
    "int generic_score(struct GenericPlayer value) { return value.score; }\n"
    "int generic_pointer_score(const struct GenericPlayer *value) { return value ? value->score : 0; }\n"
    "int generic_pointer_present(const void *value) { return value != 0; }\n"
    "int generic_array_pointer_present(struct GenericPlayer (*value)[2]) { return value != 0; }\n"
    "int generic_factory_present(struct GenericPlayer (*value)(void)) { return value != 0; }\n"
    "const struct GenericPlayer *generic_player_pointer;\n"
    "struct GenericPlayer (*generic_player_array_pointer)[2];\n"
    "int main(void) {\n"
    "  int result = _Generic(generic_value, int: 1, default: 0);\n"
    "  result += _Generic(generic_identity(generic_value), int: 2, default: 0);\n"
    "  result += _Generic(GENERIC_MODE, int: 3, default: 0);\n"
    "  result += _Generic(GENERIC_MACRO, int: 4, default: 0);\n"
    "  result += _Generic(GENERIC_CALL(), int: 6, default: 0);\n"
    "  result += GENERIC_TRIGRAPH_PREFIX + GENERIC_AFTER_TRANSLATION + generic_after_translation;\n"
    "  result += generic_literal_lf_\\\n"
    "value;\n"
    "  result += generic_literal_crlf_\\\r\n"
    "value;\n"
    "  result += generic_trigraph_lf_?" "?/\n"
    "value;\n"
    "  result += generic_trigraph_crlf_?" "?/\r\n"
    "value;\n"
    "  result += _Generic(generic_value, GenericScore: 5, default: 0);\n"
    "  result += (int)sizeof(_Atomic /* type */ (GenericScore));\n"
    "  result += (int)_Alignof /* query */ (const GenericScore *);\n"
    "  result += (int)_Alignof(int [1 + GENERIC_MODE]);\n"
    "  result += (int)sizeof /* query */ (const struct /* tag */ GenericPlayer);\n"
    "  result += (int)_Alignof(union /* tag */ GenericPayload);\n"
    "  result += (int)sizeof(enum /* tag */ GenericState);\n"
    "  result += generic_score((struct /* literal */ GenericPlayer){ 7 });\n"
    "  result += (int)(enum /* cast */ GenericState)GENERIC_IDLE;\n"
    "  result += generic_pointer_score((const struct /* pointer cast */ GenericPlayer * const)0);\n"
    "  result += generic_pointer_present((const struct /* pointer chain */ GenericPlayer * /* inner */ const * restrict)0);\n"
    "  result += _Generic((struct GenericPlayer){ 0 }, struct /* association */ GenericPlayer: 8, default: 0);\n"
    "  result += _Generic(generic_player_pointer, struct GenericPlayer: 0, const struct /* pointer association */ GenericPlayer * const: 9, default: 0);\n"
    "  result += _Generic(generic_player_pointer, default: 0, const struct /* default first association */ GenericPlayer *: 11);\n"
    "  result += _Generic(generic_player_array_pointer, struct GenericPlayer *: 0, struct /* array pointer association */ GenericPlayer (*)[2]: 10, default: 0);\n"
    "  result += generic_array_pointer_present((struct /* array pointer cast */ GenericPlayer (*)[2])0);\n"
    "  result += generic_array_pointer_present((struct /* quoted array bound */ GenericPlayer (*)[sizeof(\")\")])0);\n"
    "  result += generic_factory_present((struct /* function pointer cast */ GenericPlayer (*)(void))0);\n"
    "  result += generic_array_pointer_present(&(struct /* array literal */ GenericPlayer [2]){{ 1 }, { 2 }});\n"
    "  return result + _Generic(1, int: generic_value, default: 0);\n"
    "}\n";
static const char function_declarator_hover_source[] =
    "int increment(int value) { return value + 1; }\n"
    "int old_sum(left, right) /* ; { comment } */\n"
    "int left, right;\n"
    "{ return left + right; }\n"
    "int old_apply(callback, value)\n"
    "int callback(int);\n"
    "register int value;\n"
    "{ return callback(value); }\n"
    "int old_member(value)\n"
    "struct LocalValue { int member; } value;\n"
    "{ return value.member; }\n"
    "int old_array(values)\n"
    "int values[';'];\n"
    "{ return values[0]; }\n"
    "int first(void), second(void);\n"
    "int third(void), brace_values[2] = { 1, 2 };\n"
    "typedef int Scalar;\n"
    "struct FileRecord { const int *member; unsigned bits : 3; int values[4]; int (*callback_member)(int); };\n"
    "union FileUnion { long member; };\n"
    "int takes_scalar(Scalar);\n"
    "int parameter_prototype(int named, const int *pointer, int proto_callback(int));\n"
    "int tagged_parameter_prototype(struct /* scope */ PrototypeRecord { int member; } value);\n"
    "int nested_tag_parameter_prototype(int callback(union /* scope */ NestedPayload { int member; } value));\n"
    "int enum_parameter_prototype(enum /* scope */ PrototypeState { PROTOTYPE_READY = 3, PROTOTYPE_BUSY } value);\n"
    "int nested_enum_parameter_prototype(int callback(enum /* scope */ NestedState { NESTED_READY = 7 } value));\n"
    "int unicode_parameter(int 値) { return 値; }\n"
    "int (parenthesized)(int value) { return value + 1; }\n"
    "int target(int value);\n"
    "int (*(factory(void)))(int) { return target; }\n"
    "int (*(seeded_factory(int seed)))(int) { return target; }\n"
    "#define active_member_macro() 9\n"
    "struct MacroRecord { int active_member_macro; int future_member_macro; };\n"
    "int read_macro_member(struct MacroRecord value) { return value.active_member_macro; }\n"
    "#define future_member_macro 7\n"
    "int read_file_members(struct FileRecord *record, union FileUnion value) {\n"
    "  return (record->member != 0) + (int)value.member;\n"
    "}\n"
    "struct InitializerInt { int shared; };\n"
    "struct InitializerLong { long shared; };\n"
    "struct InitializerInner { unsigned short nested; };\n"
    "struct InitializerOuter { struct InitializerInner inner; };\n"
    "struct InitializerInt initializer_object = { .shared = 1 };\n"
    "struct InitializerLong initializer_array[] = { [1].shared = 2 };\n"
    "struct InitializerOuter initializer_nested = { .inner.nested = 3 };\n"
    "enum { INITIALIZER_OFFSET = __builtin_offsetof(struct InitializerInt, shared) };\n"
    "enum { INITIALIZER_NESTED_OFFSET = __builtin_offsetof(struct InitializerOuter, inner.nested) };\n"
    "int initializer_member_block(void) {\n"
    "  struct InitializerLong local = { .shared = 4 };\n"
    "  return ((struct InitializerInt){ .shared = 5 }).shared + local.shared;\n"
    "}\n"
    "int forward_label(int value) {\n"
    "  {\n"
    "    int shared_label = value;\n"
    "    goto /* forward */ shared_label;\n"
    "  }\n"
    "shared_label:\n"
    "  return value;\n"
    "}\n"
    "int backward_label(int value) {\n"
    "shared_label:\n"
    "  {\n"
    "    int shared_label = value;\n"
    "    if (value) goto /* backward */ shared_label;\n"
    "    return shared_label;\n"
    "  }\n"
    "}\n";
static const char inline_tag_object_source[] =
    "struct { int member; } inline_anonymous_record;\n"
    "struct InlineNamedRecord { int member; } inline_named_record;\n"
    "union { int integer_member; long long_member; } inline_anonymous_union;\n"
    "union InlineNamedUnion { int integer_member; long long_member; } inline_named_union;\n"
    "enum { INLINE_ANONYMOUS_VALUE = 2 } inline_anonymous_enum;\n"
    "enum InlineNamedEnum { INLINE_NAMED_VALUE = 3 } inline_named_enum;\n"
    "struct { struct { int value; } nested; int (*callback)(int); } inline_nested_record;\n"
    "struct { int member; } inline_initialized = { 1 };\n"
    "struct { int member; } inline_first, inline_second;\n"
    "int inline_tag_block(void) {\n"
    "  struct { int member; } inline_local;\n"
    "  int inline_after_local;\n"
    "  return inline_local.member + inline_after_local;\n"
    "}\n"
    "int inline_parameter(struct { int member; } inline_parameter_value);\n"
    "typedef struct { int member; } InlineRecordTypedef;\n"
    "typedef struct InlineTypedefRecord { int member; } NamedInlineRecordTypedef;\n"
    "typedef enum { INLINE_TYPEDEF_VALUE = 4 } InlineEnumTypedef;\n"
    "struct { int first; /* } */ int second; } inline_commented_record;\n"
    "int inline_after_file;\n";
static const char for_init_declaration_hover_source[] =
    "typedef int ForInitType;\n"
    "int for_init_hover(int for_limit) {\n"
    "  int for_before = for_limit;\n"
    "  for (int loop_plain = 0; loop_plain < for_limit; loop_plain++) {}\n"
    "  for (int loop_uninitialized; for_before; ) { break; }\n"
    "  for (int loop_first = 0, *loop_second = &loop_first; loop_first < for_limit; loop_first++) {}\n"
    "  for (ForInitType loop_typedef = 0; loop_typedef < for_limit; loop_typedef++) {}\n"
    "  for (struct { int value; } loop_record = { 0 }; loop_record.value < for_limit; loop_record.value++) {}\n"
    "  for (int (*loop_callback)(int) = 0; loop_callback; ) {}\n"
    "  for (int (loop_parenthesized) = 0; loop_parenthesized < for_limit; loop_parenthesized++) {}\n"
    "  for /* keyword gap */ (int loop_commented /* name gap */ = 0; loop_commented < for_limit; loop_commented++) {}\n"
    "  int for_after = for_before;\n"
    "  return for_after;\n"
    "}\n"
    "int for_file_after;\n";
static const char prototype_parameter_bound_hover_source[] =
    "/// prototype bound macro documentation\n"
    "#define PROTO_BOUND_MACRO 7\n"
    "enum { PROTO_BOUND_ENUM = 5 };\n"
    "int proto_bound_file = 4;\n"
    "int proto_direct(int direct_count, int direct_values[direct_count], int direct_later);\n"
    "int proto_static(int static_count, int static_values[static static_count], int static_later);\n"
    "int proto_qualified(int qualified_count, int qualified_values[const qualified_count], int qualified_later);\n"
    "int proto_expression(int expr_rows, int expr_columns, int expr_values[(expr_rows + expr_columns)], int expr_later);\n"
    "int proto_grouped(int grouped_count, int grouped_values[(grouped_count)], int grouped_later);\n"
    "int proto_inner(int inner_rows, int inner_columns, int inner_values[inner_rows][inner_columns], int inner_later);\n"
    "int proto_pointer(int pointer_count, int (*pointer_values)[pointer_count], int pointer_later);\n"
    "int proto_comment(int comment_count, int comment_values[/* gap */ comment_count], int comment_later);\n"
    "int proto_splice_lf(int splice_lf_count, int splice_lf_values[\\\n"
    "splice_lf_count], int splice_lf_later);\n"
    "int proto_splice_crlf(int splice_crlf_count, int splice_crlf_values[\\\r\n"
    "splice_crlf_count], int splice_crlf_later);\r\n"
    "typedef int ProtoBoundFunction(int typedef_count, int typedef_values[typedef_count], int typedef_later);\n"
    "int proto_definition(int definition_count, int definition_values[definition_count], int definition_later) { int definition_body; return definition_count + definition_values[0] + definition_later + definition_body; }\n"
    "int proto_file_object(int file_values[proto_bound_file], int file_later);\n"
    "int proto_enum(int enum_values[PROTO_BOUND_ENUM], int enum_later);\n"
    "int proto_macro(int macro_values[PROTO_BOUND_MACRO], int macro_later);\n"
    "int proto_bound_after;\n";
static const char block_static_assert_hover_source[] =
    "/// block static assert macro documentation\n"
    "#define BLOCK_ASSERT_MACRO 1\n"
    "int block_static_assert_hover(void) {\n"
    "  typedef unsigned long BlockAssertType;\n"
    "  struct BlockAssertRecord { int member; };\n"
    "  enum { BLOCK_ASSERT_ENUM = 4 };\n"
    "  _Static_assert(sizeof(BlockAssertType) >= 1, \"type\");\n"
    "  _Static_assert(_Alignof(BlockAssertType) >= 1, \"align\");\n"
    "  _Static_assert(sizeof(struct BlockAssertRecord) >= sizeof(int), \"tag\");\n"
    "  _Static_assert(sizeof(int[BLOCK_ASSERT_ENUM]) >= sizeof(int), \"bound\");\n"
    "  _Static_assert(BLOCK_ASSERT_ENUM == 4, \"enum\");\n"
    "  _Static_assert(BLOCK_ASSERT_MACRO, \"macro\");\n"
    "  _Static_assert((BlockAssertType)1 == 1, \"cast\");\n"
    "  _Static_assert((sizeof(BlockAssertType) /* ) , */) >= 1, \"quoted , ) ;\");\n"
    "  _Static_assert /* gap */ (sizeof(BlockAssertType) >= 1, \"keyword comment\");\n"
    "  _Static_assert(sizeof(\\\nBlockAssertType) >= 1, \"LF splice\");\n"
    "  _Static_assert(sizeof(\\\r\nBlockAssertType) >= 1, \"CRLF splice\");\r\n"
    "  {\n"
    "    _Static_assert(sizeof(BlockAssertType) >= 1, \"nested\");\n"
    "    int block_assert_nested_after;\n"
    "  }\n"
    "  int block_assert_after;\n"
    "  return 0;\n"
    "}\n"
    "int block_assert_file_after;\n";
static const char do_body_hover_source[] =
    "/// do body macro documentation\n"
    "#define DO_BODY_MACRO 1\n"
    "enum { DO_BODY_ENUM = 3 };\n"
    "int do_body_helper(int value);\n"
    "int do_body_hover(int do_body_parameter) {\n"
    "  int do_body_object = do_body_parameter;\n"
    "  do { do_body_object--; } while (do_body_object > 20);\n"
    "  do { do_body_parameter--; } while (do_body_parameter > 19);\n"
    "  do { do_body_object += DO_BODY_ENUM; } while (do_body_object < 18);\n"
    "  do { do_body_object += DO_BODY_MACRO; } while (do_body_object < 17);\n"
    "  do { do_body_object = do_body_helper(do_body_parameter); } while (do_body_object < 16);\n"
    "  do { int do_body_local = do_body_object; do_body_object += do_body_local; int do_body_local_after; } while (do_body_object < 15);\n"
    "  do { { int do_body_nested = do_body_object; do_body_object += do_body_nested; int do_body_nested_after; } } while (do_body_object < 14);\n"
    "  do /* gap */ { do_body_object--; } while (do_body_object < 13);\n"
    "  do \\\n{ do_body_object--; } while (do_body_object < 12);\n"
    "  do \\\r\n{ do_body_object--; } while (do_body_object < 11);\r\n"
    "  do do_body_object--; while (do_body_object < 10);\n"
    "  do if (do_body_parameter) do_body_object--; while (do_body_object < 9);\n"
    "  do { do { do_body_object--; } while (do_body_object < 8); } while (do_body_object < 7);\n"
    "  do_body_object += do_body_parameter;\n"
    "  int do_body_after;\n"
    "  return do_body_object;\n"
    "}\n"
    "int do_body_file_after;\n";
static const char offsetof_type_hover_source[] =
    "#define offsetof(type, member) __builtin_offsetof(type, member)\n"
    "struct OffsetInner { int member; };\n"
    "struct OffsetRecord { int member; struct OffsetInner inner; };\n"
    "union OffsetUnion { int member; long other; };\n"
    "struct OffsetOuter { struct OffsetRecord inner; };\n"
    "typedef struct OffsetRecord OffsetType;\n"
    "typedef union OffsetUnion OffsetUnionType;\n"
    "int offset_builtin_tag = __builtin_offsetof(struct OffsetRecord, member);\n"
    "int offset_builtin_typedef = __builtin_offsetof(OffsetType, member);\n"
    "int offset_builtin_union = __builtin_offsetof(union OffsetUnion, member);\n"
    "int offset_builtin_qualified = __builtin_offsetof(const OffsetType, member);\n"
    "int offset_builtin_nested = __builtin_offsetof(struct OffsetOuter, inner.inner.member);\n"
    "int offset_macro = offsetof(OffsetType, member);\n"
    "int offset_comment = __builtin_offsetof /* gap */ (OffsetType, member);\n"
    "int offset_lf = __builtin_offsetof \\\n(OffsetType, member);\n"
    "int offset_crlf = __builtin_offsetof \\\r\n(OffsetType, member);\r\n"
    "enum { OFFSET_ENUM = __builtin_offsetof(OffsetType, member) };\n"
    "int offset_sink(int value);\n"
    "int offset_function(void) {\n"
    "  typedef struct OffsetLocalRecord { int member; } OffsetLocalType;\n"
    "  int offset_local_typedef = __builtin_offsetof(OffsetLocalType, member);\n"
    "  int offset_local_tag = __builtin_offsetof(struct OffsetLocalRecord, member);\n"
    "  int offset_argument = offset_sink(__builtin_offsetof(OffsetType, member));\n"
    "  int offset_after;\n"
    "  return offset_local_typedef + offset_local_tag + offset_argument;\n"
    "}\n"
    "int offset_file_after;\n";
static const char initializer_operand_hover_source[] =
    "struct InitializerRecord { int member; };\n"
    "union InitializerUnion { int member; long other; };\n"
    "typedef struct InitializerRecord InitializerType;\n"
    "typedef union InitializerUnion InitializerUnionType;\n"
    "int initializer_file_size = sizeof((InitializerType){ .member = 1 });\n"
    "enum { INITIALIZER_ENUM_SIZE = sizeof((InitializerType){ .member = 2 }) };\n"
    "int initializer_sink(InitializerType value);\n"
    "int initializer_hover(InitializerType initializer_parameter) {\n"
    "  InitializerType initializer_before = initializer_parameter;\n"
    "  InitializerType initializer_compound = (InitializerType){ .member = 3 };\n"
    "  InitializerType initializer_qualified = (const InitializerType){ .member = 4 };\n"
    "  InitializerType initializer_tag = (struct InitializerRecord){ .member = 5 };\n"
    "  InitializerUnionType initializer_union = (InitializerUnionType){ .member = 6 };\n"
    "  InitializerType initializer_comment = (/* type */ InitializerType /* tail */){ .member = 7 };\n"
    "  InitializerType initializer_lf = (\\\nInitializerType){ .member = 8 };\n"
    "  InitializerType initializer_crlf = (\\\r\nInitializerType){ .member = 9 };\r\n"
    "  int initializer_argument = initializer_sink((InitializerType){ .member = 10 });\n"
    "  InitializerType initializer_parameter_copy = initializer_parameter;\n"
    "  InitializerType initializer_local_copy = initializer_compound;\n"
    "  InitializerType initializer_comment_copy = initializer_compound /* tail */;\n"
    "  for (InitializerType initializer_loop = initializer_compound; initializer_loop.member; ) { break; }\n"
    "  int initializer_scalar = 1;\n"
    "  int initializer_scalar_copy = initializer_scalar;\n"
    "  int initializer_after;\n"
    "  return initializer_argument + initializer_parameter_copy.member +\n"
    "         initializer_local_copy.member + initializer_comment_copy.member +\n"
    "         initializer_scalar_copy;\n"
    "}\n"
    "int initializer_file_after;\n";
static const char direct_aggregate_operand_hover_source[] =
    "struct DirectAggregateRecord { int member; };\n"
    "union DirectAggregateUnion { int member; long other; };\n"
    "typedef struct DirectAggregateRecord DirectAggregateType;\n"
    "typedef union DirectAggregateUnion DirectAggregateUnionType;\n"
    "int direct_aggregate_sink(DirectAggregateType value);\n"
    "DirectAggregateType direct_aggregate_identity(DirectAggregateType value);\n"
    "DirectAggregateType direct_aggregate_return(DirectAggregateType direct_return_parameter) {\n"
    "  return direct_return_parameter;\n"
    "}\n"
    "DirectAggregateUnionType direct_union_return(DirectAggregateUnionType direct_union_return_parameter) {\n"
    "  return direct_union_return_parameter;\n"
    "}\n"
    "int direct_aggregate_operands(DirectAggregateType direct_operand_parameter, DirectAggregateUnionType direct_operand_union_parameter) {\n"
    "  DirectAggregateType direct_source = direct_operand_parameter;\n"
    "  DirectAggregateType direct_target = (DirectAggregateType){ .member = 0 };\n"
    "  DirectAggregateUnionType direct_union_source = direct_operand_union_parameter;\n"
    "  DirectAggregateUnionType direct_union_target = (DirectAggregateUnionType){ .member = 0 };\n"
    "  direct_target = direct_source;\n"
    "  direct_target = direct_operand_parameter;\n"
    "  direct_union_target = direct_union_source;\n"
    "  int direct_call = direct_aggregate_sink(direct_source);\n"
    "  int direct_nested = direct_aggregate_sink(direct_aggregate_identity(direct_source));\n"
    "  int direct_comment = direct_aggregate_sink(direct_source /* tail */);\n"
    "  int direct_lf = direct_aggregate_sink(direct_source \\\n);\n"
    "  int direct_crlf = direct_aggregate_sink(direct_source \\\r\n);\r\n"
    "  direct_source;\n"
    "  DirectAggregateType *direct_address = &direct_source;\n"
    "  int direct_size = sizeof direct_source;\n"
    "  int direct_member = direct_source.member;\n"
    "  int direct_after;\n"
    "  return direct_call + direct_nested + direct_comment + direct_lf +\n"
    "         direct_crlf + direct_size + direct_member +\n"
    "         direct_address->member + direct_target.member + direct_union_target.member;\n"
    "}\n"
    "int direct_file_after;\n";
static const char simple_remaining_call_argument_hover_source[] =
    "struct SimpleCallRecord { int member; };\n"
    "union SimpleCallUnion { int number; long wide; };\n"
    "typedef struct SimpleCallRecord SimpleCallType;\n"
    "enum SimpleCallNumber { SIMPLE_CALL_ENUM = 1 };\n"
    "#define SIMPLE_CALL_MACRO SIMPLE_CALL_ENUM\n"
    "int simple_take_pair(SimpleCallType value, int number);\n"
    "int simple_take_three(int first, SimpleCallType value, int last);\n"
    "int simple_take_scalars(int first, int second);\n"
    "int simple_take_union(union SimpleCallUnion value, int number);\n"
    "int simple_take_two_items(SimpleCallType first, SimpleCallType second);\n"
    "int simple_take_four(SimpleCallType first, int second, int third, int fourth);\n"
    "int simple_wrap_scalar(int value);\n"
    "int simple_remaining_call_arguments(SimpleCallType simple_parameter, union SimpleCallUnion simple_union_parameter, int simple_scalar_parameter) {\n"
    "  SimpleCallType simple_local = simple_parameter;\n"
    "  int simple_aggregate_first = simple_take_pair(simple_local, 1);\n"
    "  int simple_aggregate_middle = simple_take_three(1, simple_local, 2);\n"
    "  int simple_aggregate_tail = simple_take_two_items(simple_local, simple_parameter);\n"
    "  int simple_scalar_first = simple_take_scalars(simple_scalar_parameter, SIMPLE_CALL_ENUM);\n"
    "  int simple_enum_first = simple_take_scalars(SIMPLE_CALL_ENUM, simple_scalar_parameter);\n"
    "  int simple_macro_first = simple_take_scalars(SIMPLE_CALL_MACRO, simple_scalar_parameter);\n"
    "  int simple_union_first = simple_take_union(simple_union_parameter, simple_scalar_parameter);\n"
    "  int simple_comment = simple_take_pair(simple_local /* first */, simple_scalar_parameter);\n"
    "  int simple_lf = simple_take_pair(simple_local \\\n, simple_scalar_parameter);\n"
    "  int simple_crlf = simple_take_pair(simple_local \\\r\n, simple_scalar_parameter);\r\n"
    "  int simple_nested = simple_wrap_scalar(simple_take_pair(simple_local, simple_scalar_parameter));\n"
    "  int simple_many = simple_take_four(simple_local, simple_scalar_parameter, SIMPLE_CALL_ENUM, 3);\n"
    "  return simple_take_pair(simple_local, 3);\n"
    "  int simple_after;\n"
    "}\n"
    "int simple_file_after;\n";
static const char documentation_hover_source[] =
    "/** 敵の現在位置 */\n"
    "static int enemy_x;\n"
    "\n"
    "/// 歩行中の画像番号を返す\n"
    "/// alternateが0以外なら第一フレーム\n"
    "static int walk_frame(int alternate) {\n"
    "  return alternate ? 1 : 2;\n"
    "}\n"
    "\n"
    "/**\n"
    " * 読み取り専用の値\n"
    " *\n"
    " * 日本語の段落を維持する\n"
    " */\n"
    "static const int qualified_value = 3;\n"
    "\n"
    "/** 左右の座標 */\n"
    "int left_value, right_value;\n"
    "\n"
    "/** 外部オブジェクト */\n"
    "extern int external_value;\n"
    "\n"
    "/** prototype only */\n"
    "int prototype_only(int value);\n"
    "\n"
    "/** definition only */\n"
    "int definition_only(int value) { return value; }\n"
    "\n"
    "/** prototype wins */\n"
    "int documented_both(int value);\n"
    "/** definition loses */\n"
    "int documented_both(int value) { return value; }\n"
    "\n"
    "int fallback_definition(int value);\n"
    "/** definition fallback */\n"
    "int fallback_definition(int value) { return value; }\n"
    "\n"
    "/// ヘビが進む方向を表します。\n"
    "enum DocumentedDirection {\n"
    "  /// 左へ進む方向です。\n"
    "  DOCUMENTED_DIRECTION_LEFT,\n"
    "  DOCUMENTED_DIRECTION_RIGHT,\n"
    "  /**\n"
    "   * 上へ進む方向です。\n"
    "   *\n"
    "   * 明示値を使用します。\n"
    "   */\n"
    "  DOCUMENTED_DIRECTION_UP = (1 << 2)\n"
    "  ,\r\n"
    "\t/// 下へ進む方向です。\r\n"
    "\t/// CRLFでも関連付けます。\r\n"
    "\tDOCUMENTED_DIRECTION_DOWN\r\n"
    "};\n"
    "int read_documented_direction(enum DocumentedDirection direction) {\n"
    "  return direction == DOCUMENTED_DIRECTION_LEFT\n"
    "             ? DOCUMENTED_DIRECTION_UP\n"
    "             : DOCUMENTED_DIRECTION_RIGHT;\n"
    "}\n"
    "\n"
    "/// tagだけの説明です。\n"
    "enum TagOnlyDirection { TAG_ONLY_LEFT, TAG_ONLY_RIGHT };\n"
    "enum ConstantOnlyDirection {\n"
    "  /// constantだけの説明です。\n"
    "  CONSTANT_ONLY_DIRECTION = 7\n"
    "};\n"
    "enum {\n"
    "  /** anonymous constantの説明です。 */\n"
    "  ANONYMOUS_DIRECTION = 8,\n"
    "  ANONYMOUS_DIRECTION_UNDOCUMENTED\n"
    "};\n"
    "/** enum空行で切れる */\n"
    "\n"
    "enum BlankGapDirection { BLANK_GAP_DIRECTION };\n"
    "/** enum通常commentで切れる */\n"
    "/* separator */\n"
    "enum OrdinaryGapDirection { ORDINARY_GAP_DIRECTION };\n"
    "/** enumdirectiveで切れる */\n"
    "#define ENUM_DOCUMENTATION_BREAK 1\n"
    "enum DirectiveGapDirection { DIRECTIVE_GAP_DIRECTION };\n"
    "int read_misc_directions(enum TagOnlyDirection tag_only,\n"
    "                         enum ConstantOnlyDirection constant_only) {\n"
    "  return tag_only == TAG_ONLY_LEFT ||\n"
    "         constant_only == CONSTANT_ONLY_DIRECTION ||\n"
    "         ANONYMOUS_DIRECTION;\n"
    "}\n"
    "\n"
    "/** 空行で切れる */\n"
    "\n"
    "int blank_gap;\n"
    "/** directiveで切れる */\n"
    "#define DOCUMENTATION_BREAK 1\n"
    "int directive_gap;\n"
    "#define DOCUMENTATION_CONTINUATION \\\r\n"
    "/** macro continuation */ 1\r\n"
    "int directive_continuation_gap;\n"
    "/** 最初の宣言だけ */\n"
    "int first_only;\n"
    "int declaration_after;\n"
    "/* 通常block comment */\n"
    "int ordinary_block;\n"
    "// 通常line comment\n"
    "int ordinary_line;\n"
    "const char *comment_text = \"/** fake */\";\n"
    "int string_after;\n"
    "int comment_character = '/';\n"
    "int character_after;\n"
    "\n"
    "/**\r\n"
    "\t * CRLFの説明\r\n"
    "\t * 二行目\r\n"
    "\t */\r\n"
    "static const int crlf_value = 5;\n"
    "\n"
    "\t/// 四角形の一辺の長さ（ピクセル）です。\r\n"
    "\t/// プレイヤーの描画に使用します。\r\n"
    "\t#define PLAYER_SIZE 12\r\n"
    "\n"
    "/**\n"
    " * 値を二倍にします。\n"
    " *\n"
    " * 引数は一度だけ評価してください。\n"
    " */\n"
    "#define DOUBLE(value) ((value) * 2)\n"
    "/** 継続object macro */\n"
    "#define DOCUMENTED_LINE_OBJECT (1 + \\\n"
    "  2)\n"
    "/** 継続function macro */\n"
    "#define DOCUMENTED_LINE_FUNCTION(value) ((value) + \\\n"
    "  1)\n"
    "/** 古いmacro説明 */\n"
    "#define REDEFINED_DOC 1\n"
    "#undef REDEFINED_DOC\n"
    "/** 新しいmacro説明 */\n"
    "#define REDEFINED_DOC 2\n"
    "#if 0\n"
    "/** inactive macro説明 */\n"
    "#define INACTIVE_DOCUMENTATION 99\n"
    "#endif\n"
    "/** 空行で切れるmacro */\n"
    "\n"
    "#define BLANK_DOC_MACRO 3\n"
    "/** 通常commentで切れるmacro */\n"
    "/* separator */\n"
    "#define ORDINARY_GAP_MACRO 4\n"
    "/** 条件directiveで切れるmacro */\n"
    "#if 1\n"
    "#define CONDITIONAL_GAP_MACRO 5\n"
    "#endif\n"
    "/** pragmaで切れるmacro */\n"
    "#pragma pack(push, 1)\n"
    "#define PRAGMA_GAP_MACRO 6\n"
    "#pragma pack(pop)\n"
    "\n"
    "int documentation_main(void) {\n"
    "  /** local object */\n"
    "  int local_value = 1;\n"
    "  enemy_x = walk_frame(local_value);\n"
    "  return enemy_x + qualified_value + left_value + right_value +\n"
    "         external_value + prototype_only(local_value) +\n"
    "         definition_only(local_value) + documented_both(local_value) +\n"
    "         fallback_definition(local_value) + crlf_value + PLAYER_SIZE +\n"
    "         DOUBLE(local_value) + DOCUMENTED_LINE_OBJECT +\n"
    "         DOCUMENTED_LINE_FUNCTION(local_value) + REDEFINED_DOC +\n"
    "         BLANK_DOC_MACRO + ORDINARY_GAP_MACRO +\n"
    "         CONDITIONAL_GAP_MACRO + PRAGMA_GAP_MACRO;\n"
    "}\n";

static const char macro_definition_game_header[] =
    "#define GAME_SCREEN_WIDTH 320\n"
    "#define GAME_SCREEN_HEIGHT 180\n"
    "#define BUTTON_LEFT 0\n"
    "#define BUTTON_RIGHT 1\n"
    "#define BUTTON_UP 2\n"
    "#define BUTTON_DOWN 3\n"
    "#define BUTTON_A 4\n"
    "#define COLOR_WHITE 0xffffff\n"
    "#define RGB(red, green, blue) ((red) + (green) + (blue))\n"
    "unsigned int random_next(void);\n"
    "void random_seed(unsigned int seed);\n"
    "unsigned int tick_count(void);\n"
    "int input_pressed(int button);\n"
    "void screen_clear(int color);\n"
    "void draw_text(const char *text, int x, int y, int color);\n"
    "void draw_rect(int x, int y, int width, int height, int color);\n"
    "int game_running(void);\n";

static const char cast_operand_hover_source[] =
    "/// cast operand macro documentation\n"
    "#define CAST_OPERAND_MACRO 17\n"
    "typedef unsigned long CastSize;\n"
    "struct CastRecord { int value; };\n"
    "enum CastMode { CAST_MODE_VALUE = 3 };\n"
    "static int cast_object = 5;\n"
    "static int cast_seed = 9;\n"
    "static int cast_choose(int value) { return value; }\n"
    "static int (*cast_pointer)(int) = cast_choose;\n"
    "static int cast_context(int parameter_value, int condition,\n"
    "                        int *values, int index_value) {\n"
    "  int simple = (int)CAST_OPERAND_MACRO;\n"
    "  int nested = (int)((unsigned long)cast_object);\n"
    "  int binary_rhs = cast_seed % (unsigned int)cast_object;\n"
    "  int argument = cast_choose((const int)parameter_value);\n"
    "  int conditional = condition ? (int)CAST_OPERAND_MACRO : 0;\n"
    "  int subscript = values[(unsigned int)index_value];\n"
    "  int typedef_name = (CastSize)cast_object;\n"
    "  const volatile int *pointer = (const volatile int *)values;\n"
    "  struct CastRecord *tag_pointer = (struct CastRecord *)values;\n"
    "  int enum_cast = (enum CastMode)CAST_MODE_VALUE;\n"
    "  int comment_gap = (int) /* operand gap */ CAST_OPERAND_MACRO;\n"
    "  int splice_lf = (unsigned int) \\\n"
    "cast_object;\n"
    "  int splice_crlf = (unsigned int) \\\r\n"
    "cast_object;\r\n"
    "  int nested_cast = (int)((unsigned long)CAST_OPERAND_MACRO);\n"
    "  int adjacent_builtin = (int)(long)cast_object;\n"
    "  int adjacent_typedef = (CastSize)(long)cast_object;\n"
    "  int adjacent_typedef_operand = (int)(CastSize)cast_object;\n"
    "  int adjacent_comment = (int) /* adjacent cast */ "
    "(long)CAST_OPERAND_MACRO;\n"
    "  int adjacent_splice_lf = (int) \\\n"
    "(long)cast_object;\n"
    "  int adjacent_splice_crlf = (int) \\\r\n"
    "(long)CAST_MODE_VALUE;\r\n"
    "  (void)(int[CAST_MODE_VALUE]){ 1 };\n"
    "  (void)(int[CAST_MODE_VALUE]){ 1 }[0];\n"
    "  (void)(int (*)[CAST_MODE_VALUE])0;\n"
    "  int normal_call = cast_choose(parameter_value) + cast_object;\n"
    "  int parenthesized_call = (cast_choose)(parameter_value);\n"
    "  int parenthesized_pointer_call = (cast_pointer)(parameter_value);\n"
    "  int dereferenced_pointer_call = (*cast_pointer)(parameter_value);\n"
    "  int addressed_call = (&cast_choose)(parameter_value);\n"
    "  int grouped = (cast_object + cast_seed) + CAST_OPERAND_MACRO;\n"
    "  int type_size = (int)sizeof(unsigned int) + CAST_OPERAND_MACRO;\n"
    "  int type_align = (int)_Alignof(unsigned int) + CAST_OPERAND_MACRO;\n"
    "  int compound = ((struct CastRecord){ 1 }).value + "
    "CAST_OPERAND_MACRO;\n"
    "  return simple + nested + binary_rhs + argument + conditional +\n"
    "         subscript + typedef_name + (pointer != 0) +\n"
    "         (tag_pointer != 0) + enum_cast +\n"
    "         comment_gap + splice_lf + splice_crlf + nested_cast +\n"
    "         adjacent_builtin + adjacent_typedef +\n"
    "         adjacent_typedef_operand + adjacent_comment +\n"
    "         adjacent_splice_lf + adjacent_splice_crlf + normal_call +\n"
    "         parenthesized_call +\n"
    "         parenthesized_pointer_call + dereferenced_pointer_call +\n"
    "         addressed_call + grouped + type_size + type_align + compound;\n"
    "}\n";
static const char *const cast_operand_project_sources[] = {
    "/// project cast v1\n#define PROJECT_CAST_VALUE 31\n"
    "int project_cast(void) { return (unsigned int)PROJECT_CAST_VALUE; }\n",
    "\n/// project cast v2\n#define PROJECT_CAST_VALUE 32\n"
    "int project_cast(void) { return (long)PROJECT_CAST_VALUE; }\n",
};

static const char sizeof_expression_operand_hover_source[] =
    "/// sizeof operand macro documentation\n"
    "#define SIZEOF_OPERAND_MACRO 17\n"
    "enum SizeofOperandMode { SIZEOF_OPERAND_ENUM = 3 };\n"
    "static int sizeof_global = 5;\n"
    "static int sizeof_context(int sizeof_parameter) {\n"
    "  int sizeof_local = 9;\n"
    "  int global_size = (int)sizeof sizeof_global;\n"
    "  int parameter_size = (int)sizeof sizeof_parameter;\n"
    "  int local_size = (int)sizeof sizeof_local;\n"
    "  int macro_size = (int)sizeof SIZEOF_OPERAND_MACRO;\n"
    "  int enum_size = (int)sizeof SIZEOF_OPERAND_ENUM;\n"
    "  int comment_size = (int)sizeof /* operand gap */ sizeof_global;\n"
    "  int splice_lf_size = (int)sizeof \\\n"
    "sizeof_parameter;\n"
    "  int splice_crlf_size = (int)sizeof \\\r\n"
    "sizeof_local;\r\n"
    "  return global_size + parameter_size + local_size + macro_size +\n"
    "         enum_size + comment_size + splice_lf_size + splice_crlf_size;\n"
    "}\n";

static const char statement_keyword_operand_hover_source[] =
    "/// statement operand macro documentation\n"
    "#define STATEMENT_OPERAND_MACRO 7\n"
    "enum StatementOperandMode { STATEMENT_OPERAND_ENUM = 3 };\n"
    "static int statement_global = 5;\n"
    "static int statement_return_context(int statement_parameter) {\n"
    "  int statement_local = statement_parameter;\n"
    "  if (statement_parameter == 0) return statement_local;\n"
    "  if (statement_parameter == 1) return statement_global;\n"
    "  if (statement_parameter == 2) return statement_parameter;\n"
    "  if (statement_parameter == 3) return STATEMENT_OPERAND_MACRO;\n"
    "  if (statement_parameter == 4) return STATEMENT_OPERAND_ENUM;\n"
    "  if (statement_parameter == 5)\n"
    "    return /* operand gap */ statement_global;\n"
    "  if (statement_parameter == 6) return \\\n"
    "statement_parameter;\n"
    "  return \\\r\n"
    "statement_local;\r\n"
    "}\n"
    "static int statement_case_context(int statement_parameter) {\n"
    "  int statement_local = statement_parameter;\n"
    "  switch (statement_parameter) {\n"
    "    case STATEMENT_OPERAND_ENUM: return statement_global;\n"
    "    case /* operand gap */ STATEMENT_OPERAND_MACRO:\n"
    "      return statement_parameter;\n"
    "    default: return statement_local;\n"
    "  }\n"
    "}\n";

static const char statement_call_operand_hover_source[] =
    "/// statement call macro documentation\n"
    "#define STATEMENT_CALL_MACRO(value) (7 + (value))\n"
    "static int statement_call_target(int value) { return value; }\n"
    "static int statement_return_call(int parameter) {\n"
    "  if (parameter == 0) return statement_call_target(parameter);\n"
    "  if (parameter == 1)\n"
    "    return statement_call_target /* operand gap */ (parameter);\n"
    "  if (parameter == 2) return statement_call_target \\\n"
    "(parameter);\n"
    "  return statement_call_target \\\r\n"
    "(parameter);\r\n"
    "}\n"
    "static int statement_case_call(int parameter) {\n"
    "  switch (parameter) {\n"
    "    case STATEMENT_CALL_MACRO(0): return parameter;\n"
    "    case STATEMENT_CALL_MACRO /* operand gap */ (1): return parameter;\n"
    "    case STATEMENT_CALL_MACRO \\\n"
    "(2): return parameter;\n"
    "    case STATEMENT_CALL_MACRO \\\r\n"
    "(3): return parameter;\r\n"
    "    default: return 0;\n"
    "  }\n"
    "}\n";

static const char case_expression_operand_hover_source[] =
    "/// case expression macro documentation\n"
    "#define CASE_EXPRESSION_MACRO 5\n"
    "enum CaseExpressionValue {\n"
    "  CASE_EXPRESSION_A = 2,\n"
    "  CASE_EXPRESSION_B = 3,\n"
    "  CASE_EXPRESSION_C = 4,\n"
    "  CASE_EXPRESSION_CONDITION = 0\n"
    "};\n"
    "static int case_expression_unary(int value) {\n"
    "  switch (value) { case -CASE_EXPRESSION_A: return 1; default: return 0; }\n"
    "}\n"
    "static int case_expression_binary(int value) {\n"
    "  switch (value) { case 1 + CASE_EXPRESSION_B: return 1; default: return 0; }\n"
    "}\n"
    "static int case_expression_grouped(int value) {\n"
    "  switch (value) { case (CASE_EXPRESSION_C): return 1; default: return 0; }\n"
    "}\n"
    "static int case_expression_second(int value) {\n"
    "  switch (value) { case CASE_EXPRESSION_A + CASE_EXPRESSION_B: return 1; default: return 0; }\n"
    "}\n"
    "static int case_expression_conditional(int value) {\n"
    "  switch (value) { case CASE_EXPRESSION_CONDITION ? CASE_EXPRESSION_B : CASE_EXPRESSION_C: return 1; default: return 0; }\n"
    "}\n"
    "static int case_expression_comment(int value) {\n"
    "  switch (value) { case CASE_EXPRESSION_A /* expression gap */ + CASE_EXPRESSION_MACRO: return 1; default: return 0; }\n"
    "}\n"
    "static int case_expression_splice_lf(int value) {\n"
    "  switch (value) { case CASE_EXPRESSION_A + \\\n"
    "CASE_EXPRESSION_B: return 1; default: return 0; }\n"
    "}\n"
    "static int case_expression_splice_crlf(int value) {\n"
    "  switch (value) { case CASE_EXPRESSION_A + \\\r\n"
    "CASE_EXPRESSION_C: return 1; default: return 0; }\r\n"
    "}\n";

static const char enum_initializer_operand_hover_source[] =
    "/// enum initializer macro documentation\n"
    "#define ENUM_INITIALIZER_MACRO 5\n"
    "enum EnumInitializerValue {\n"
    "  ENUM_INITIALIZER_BASE = 3,\n"
    "  ENUM_INITIALIZER_OTHER = 4,\n"
    "  ENUM_INITIALIZER_CONDITION = 0,\n"
    "  ENUM_INITIALIZER_UNARY = -ENUM_INITIALIZER_BASE,\n"
    "  ENUM_INITIALIZER_BINARY = 1 + ENUM_INITIALIZER_OTHER,\n"
    "  ENUM_INITIALIZER_GROUPED = (ENUM_INITIALIZER_BASE),\n"
    "  ENUM_INITIALIZER_SECOND = ENUM_INITIALIZER_BASE + ENUM_INITIALIZER_OTHER,\n"
    "  ENUM_INITIALIZER_CONDITIONAL = ENUM_INITIALIZER_CONDITION\n"
    "      ? ENUM_INITIALIZER_BASE : ENUM_INITIALIZER_OTHER,\n"
    "  ENUM_INITIALIZER_MACRO_USE = ENUM_INITIALIZER_BASE + ENUM_INITIALIZER_MACRO,\n"
    "  ENUM_INITIALIZER_COMMENT = ENUM_INITIALIZER_BASE /* expression gap */ + ENUM_INITIALIZER_OTHER,\n"
    "  ENUM_INITIALIZER_SPLICE_LF = ENUM_INITIALIZER_BASE + \\\n"
    "ENUM_INITIALIZER_OTHER,\n"
    "  ENUM_INITIALIZER_SPLICE_CRLF = ENUM_INITIALIZER_BASE + \\\r\n"
    "ENUM_INITIALIZER_OTHER\r\n"
    "};\n"
    "static int enum_initializer_block(void) {\n"
    "  enum { ENUM_INITIALIZER_BLOCK_BASE = 7,\n"
    "         ENUM_INITIALIZER_BLOCK_DERIVED = ENUM_INITIALIZER_BLOCK_BASE + 1 };\n"
    "  return ENUM_INITIALIZER_BLOCK_DERIVED;\n"
    "}\n";

static const char *const incomplete_enum_initializer_sources[] = {
    "/// incomplete enum macro documentation\n"
    "#define INCOMPLETE_ENUM_MACRO 5\n"
    "enum IncompleteEnumBase {\n"
    "  INCOMPLETE_ENUM_BASE = 3,\n"
    "  INCOMPLETE_ENUM_OTHER = 4\n"
    "};\n"
    "enum IncompleteEnumPending {\n"
    "  INCOMPLETE_ENUM_DERIVED = INCOMPLETE_ENUM_BASE /* gap */ + \\\n"
    "INCOMPLETE_ENUM_MACRO",
    "enum { INCOMPLETE_ENUM_BLOCK_BASE = 7 };\n"
    "static int incomplete_enum_block(int parameter) {\n"
    "  int before = parameter;\n"
    "  enum { INCOMPLETE_ENUM_BLOCK_DERIVED = "
    "(INCOMPLETE_ENUM_BLOCK_BASE)",
    "enum { INCOMPLETE_ENUM_PARTIAL_BASE = 11,\n"
    "       INCOMPLETE_ENUM_PARTIAL_OTHER = 12 };\n"
    "enum { INCOMPLETE_ENUM_PARTIAL_DERIVED = "
    "INCOMPLETE_ENUM_PARTIAL_BASE + \\\r\n"
    "INCOMPLETE_ENUM_PARTIAL_OT",
    "/// incomplete enum partial macro documentation\n"
    "#define INCOMPLETE_ENUM_PARTIAL_MACRO 13\n"
    "enum { INCOMPLETE_ENUM_PARTIAL_MACRO_DERIVED = "
    "/* gap */ INCOMPLETE_ENUM_PARTIAL_MAC",
    "enum { INCOMPLETE_ENUM_PARTIAL_BLOCK_BASE = 14 };\n"
    "static int incomplete_enum_partial_block(int parameter) {\n"
    "  int before = parameter;\n"
    "  enum { INCOMPLETE_ENUM_PARTIAL_BLOCK_DERIVED = "
    "+INCOMPLETE_ENUM_PARTIAL_BLOCK_BA"};

static const char incomplete_enum_header_source[] =
    "/// header enum documentation\n"
    "enum IncompleteHeaderEnum {\n"
    "  /// header enum value documentation\n"
    "  INCOMPLETE_HEADER_ENUM_VALUE = 17\n"
    "};\n"
    "/// header enum macro documentation\n"
    "#define INCOMPLETE_HEADER_ENUM_MACRO 19\n";

static const char *const incomplete_enum_header_revisions[] = {
    incomplete_enum_header_source,
    "\n/** header enum revision 2 */\n"
    "enum IncompleteHeaderEnum {\n"
    "  /** header enum value revision 2 */\n"
    "  INCOMPLETE_HEADER_ENUM_VALUE = 27\n"
    "};\n"
    "/** header enum macro revision 2 */\n"
    "#define INCOMPLETE_HEADER_ENUM_MACRO 29\n",
    "enum IncompleteHeaderEnum {\n"
    "  INCOMPLETE_HEADER_ENUM_VALUE = 37\n"
    "};\n"
    "#define INCOMPLETE_HEADER_ENUM_MACRO 39\n",
    "/** renamed header enum documentation */\n"
    "enum RenamedHeaderEnum {\n"
    "  /** renamed header enum value documentation */\n"
    "  RENAMED_HEADER_ENUM_VALUE = 47\n"
    "};\n"
    "/** renamed header enum macro documentation */\n"
    "#define RENAMED_HEADER_ENUM_MACRO 49\n",
    "/// switched value macro documentation\n"
    "#define INCOMPLETE_HEADER_ENUM_VALUE 57\n"
    "enum SwitchedHeaderEnum {\n"
    "  /// switched macro enum documentation\n"
    "  INCOMPLETE_HEADER_ENUM_MACRO = 59\n"
    "};\n",
    "/// function-like header macro documentation\n"
    "#define INCOMPLETE_HEADER_ENUM_MACRO(left, right) 61\n",
    "/// colliding header macro documentation\n"
    "#define COLLIDING_HEADER_SYMBOL(value) ((value) + 70)\n"
    "enum CollidingHeaderEnum {\n"
    "  /// colliding header enum documentation\n"
    "  COLLIDING_HEADER_SYMBOL = 73\n"
    "};\n",
    "enum EnumOnlyCollidingHeader {\n"
    "  /// enum-only colliding header documentation\n"
    "  COLLIDING_HEADER_SYMBOL = 83\n"
    "};\n",
    "/// macro-only colliding header documentation\n"
    "#define COLLIDING_HEADER_SYMBOL(value) ((value) + 80)\n",
};

static const char *const incomplete_enum_header_main_sources[] = {
    "#include <incomplete-enum.h>\n"
    "enum { INCOMPLETE_HEADER_DERIVED = INCOMPLETE_HEADER_ENUM_VALUE",
    "#include <incomplete-enum.h>\n"
    "enum { INCOMPLETE_HEADER_DERIVED = INCOMPLETE_HEADER_ENUM_MACRO",
    "#include <incomplete-enum.h>\n"
    "enum { INCOMPLETE_HEADER_DERIVED = INCOMPLETE_HEADER_ENUM_VA",
    "#include <incomplete-enum.h>\n"
    "enum { INCOMPLETE_HEADER_DERIVED = INCOMPLETE_HEADER_ENUM_MA",
    "#include <incomplete-enum.h>\n"
    "enum { INCOMPLETE_HEADER_DERIVED = RENAMED_HEADER_ENUM_VALUE",
    "#include <incomplete-enum.h>\n"
    "enum { INCOMPLETE_HEADER_DERIVED = RENAMED_HEADER_ENUM_MACRO",
    "#include <incomplete-enum.h>\n"
    "enum { INCOMPLETE_HEADER_DERIVED = COLLIDING_HEADER_SYMBOL",
    "#include <incomplete-enum.h>\n"
    "enum { INCOMPLETE_HEADER_DERIVED = COLLIDING_HEADER_SYMBOL(1)",
};

static const char *const project_enum_macro_headers[] = {
    "/// project header macro v1\n"
    "#define PROJECT_COLLIDING_SYMBOL(value) ((value) + 110)\n",
    "",
    "/// project header macro v2\n"
    "#define PROJECT_COLLIDING_SYMBOL(value) ((value) + 220)\n",
    "/// project header macro v3\n"
    "#define PROJECT_COLLIDING_SYMBOL(entry) ((entry) + 330)\n",
    "/// project header macro v4\n"
    "#define PROJECT_COLLIDING_SYMBOL(restored) ((restored) + 440)\n",
    "/// project header macro v5\n"
    "#define PROJECT_COLLIDING_SYMBOL(final_value) ((final_value) + 550)\n",
};

static const char *const project_enum_macro_index_sources[] = {
    "#include <project-collision.h>\n"
    "enum ProjectSourceCollisionV1 {\n"
    "  /// project source enum v1\n"
    "  PROJECT_COLLIDING_SYMBOL = 101\n};\n"
    "int project_collision_anchor(void) { return 1; }\n",
    "#include <project-collision.h>\n"
    "int project_collision_anchor(void) { return 1; }\n",
    "#include <project-collision.h>\n"
    "enum ProjectSourceCollisionV2 {\n"
    "  /// project source enum v2\n"
    "  PROJECT_COLLIDING_SYMBOL = 202\n};\n"
    "int project_collision_anchor(void) { return 2; }\n",
    "#include <project-collision.h>\n"
    "enum ProjectSourceRenamedV1 {\n"
    "  /// project source renamed enum v1\n"
    "  PROJECT_RENAMED_SYMBOL = 303\n};\n"
    "int project_collision_anchor(void) { return 3; }\n",
    "#include <project-collision.h>\n"
    "enum ProjectSourceCollisionV3 {\n"
    "  /// project source restored enum v3\n"
    "  PROJECT_COLLIDING_SYMBOL = 303\n};\n"
    "int project_collision_anchor(void) { return 3; }\n",
    "#include <project-collision.h>\n"
    "enum ProjectSourceRenamedV2 {\n"
    "  /// project source renamed enum v2\n"
    "  PROJECT_RENAMED_SYMBOL = 404\n};\n"
    "int project_collision_anchor(void) { return 4; }\n",
    "#include <project-collision.h>\n"
    "enum ProjectSourceCollisionV4 {\n"
    "  /// project source restored enum v4\n"
    "  PROJECT_COLLIDING_SYMBOL = 505\n};\n"
    "int project_collision_anchor(void) { return 5; }\n",
};

static const char *const project_enum_macro_edit_sources[][2] = {
    {
        "#include <project-collision.h>\n"
        "enum ProjectSourceCollisionV1 {\n"
        "  /// project source enum v1\n"
        "  PROJECT_COLLIDING_SYMBOL = 101\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
        "#include <project-collision.h>\n"
        "enum ProjectSourceCollisionV1 {\n"
        "  /// project source enum v1\n"
        "  PROJECT_COLLIDING_SYMBOL = 101\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
    {
        "#include <project-collision.h>\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
        "#include <project-collision.h>\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
    {
        "#include <project-collision.h>\n"
        "enum ProjectSourceCollisionV2 {\n"
        "  /// project source enum v2\n"
        "  PROJECT_COLLIDING_SYMBOL = 202\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
        "#include <project-collision.h>\n"
        "enum ProjectSourceCollisionV2 {\n"
        "  /// project source enum v2\n"
        "  PROJECT_COLLIDING_SYMBOL = 202\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
    {
        "#include <project-collision.h>\n"
        "enum ProjectSourceRenamedV1 {\n"
        "  /// project source renamed enum v1\n"
        "  PROJECT_RENAMED_SYMBOL = 303\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
        "#include <project-collision.h>\n"
        "enum ProjectSourceRenamedV1 {\n"
        "  /// project source renamed enum v1\n"
        "  PROJECT_RENAMED_SYMBOL = 303\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
    {
        "#include <project-collision.h>\n"
        "enum ProjectSourceCollisionV3 {\n"
        "  /// project source restored enum v3\n"
        "  PROJECT_COLLIDING_SYMBOL = 303\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
        "#include <project-collision.h>\n"
        "enum ProjectSourceCollisionV3 {\n"
        "  /// project source restored enum v3\n"
        "  PROJECT_COLLIDING_SYMBOL = 303\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
    {
        "#include <project-collision.h>\n"
        "enum ProjectSourceRenamedV2 {\n"
        "  /// project source renamed enum v2\n"
        "  PROJECT_RENAMED_SYMBOL = 404\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
        "#include <project-collision.h>\n"
        "enum ProjectSourceRenamedV2 {\n"
        "  /// project source renamed enum v2\n"
        "  PROJECT_RENAMED_SYMBOL = 404\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
    {
        "#include <project-collision.h>\n"
        "enum ProjectSourceCollisionV4 {\n"
        "  /// project source restored enum v4\n"
        "  PROJECT_COLLIDING_SYMBOL = 505\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL",
        "#include <project-collision.h>\n"
        "enum ProjectSourceCollisionV4 {\n"
        "  /// project source restored enum v4\n"
        "  PROJECT_COLLIDING_SYMBOL = 505\n};\n"
        "enum { PROJECT_COLLISION_DERIVED = PROJECT_COLLIDING_SYMBOL(1)",
    },
};

typedef struct {
  size_t source_index;
  size_t header_index;
  const char *enum_value;
  const char *enum_documentation;
  const char *enum_comment;
  const char *macro_replacement;
  const char *macro_invocation_value;
  const char *macro_documentation;
  const char *macro_comment;
  const char *macro_parameter;
  const char *renamed_enum_value;
  const char *renamed_enum_documentation;
  const char *renamed_enum_comment;
} project_enum_macro_revision_t;

static const project_enum_macro_revision_t project_enum_macro_revisions[] = {
    {0, 0, "101", "project source enum v1", "/// project source enum v1",
     "( ( value ) + 110 )", "111", "project header macro v1",
     "/// project header macro v1", NULL, NULL, NULL, NULL},
    {1, 0, NULL, NULL, NULL, "( ( value ) + 110 )", "111",
     "project header macro v1", "/// project header macro v1",
     NULL, NULL, NULL, NULL},
    {2, 0, "202", "project source enum v2", "/// project source enum v2",
     "( ( value ) + 110 )", "111", "project header macro v1",
     "/// project header macro v1", NULL, NULL, NULL, NULL},
    {2, 1, "202", "project source enum v2", "/// project source enum v2",
     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {2, 2, "202", "project source enum v2", "/// project source enum v2",
     "( ( value ) + 220 )", "221", "project header macro v2",
     "/// project header macro v2", NULL, NULL, NULL, NULL},
    {2, 1, "202", "project source enum v2", "/// project source enum v2",
     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {1, 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
     NULL, NULL, NULL, NULL},
    {1, 2, NULL, NULL, NULL, "( ( value ) + 220 )", "221",
     "project header macro v2", "/// project header macro v2",
     NULL, NULL, NULL, NULL},
    {2, 2, "202", "project source enum v2", "/// project source enum v2",
     "( ( value ) + 220 )", "221", "project header macro v2",
     "/// project header macro v2", NULL, NULL, NULL, NULL},
    {3, 2, NULL, NULL, NULL, "( ( value ) + 220 )", "221",
     "project header macro v2", "/// project header macro v2", "value",
     "303", "project source renamed enum v1",
     "/// project source renamed enum v1"},
    {3, 3, NULL, NULL, NULL, "( ( entry ) + 330 )", "331",
     "project header macro v3", "/// project header macro v3", "entry",
     "303", "project source renamed enum v1",
     "/// project source renamed enum v1"},
    {4, 3, "303", "project source restored enum v3",
     "/// project source restored enum v3", "( ( entry ) + 330 )", "331",
     "project header macro v3", "/// project header macro v3", "entry",
     NULL, NULL, NULL},
    {4, 4, "303", "project source restored enum v3",
     "/// project source restored enum v3", "( ( restored ) + 440 )", "441",
     "project header macro v4", "/// project header macro v4", "restored",
     NULL, NULL, NULL},
    {5, 4, NULL, NULL, NULL, "( ( restored ) + 440 )", "441",
     "project header macro v4", "/// project header macro v4", "restored",
     "404", "project source renamed enum v2",
     "/// project source renamed enum v2"},
    {6, 4, "505", "project source restored enum v4",
     "/// project source restored enum v4", "( ( restored ) + 440 )", "441",
     "project header macro v4", "/// project header macro v4", "restored",
     NULL, NULL, NULL},
    {6, 5, "505", "project source restored enum v4",
     "/// project source restored enum v4", "( ( final_value ) + 550 )",
     "551", "project header macro v5", "/// project header macro v5",
     "final_value", NULL, NULL, NULL},
};

static const char enum_two_argument_call_header[] =
    "/// enum two argument function-like macro\n"
    "#define ENUM_TWO_ARGUMENT_CALL(left, right) "
    "((left) + (right) + 100)\n";

static const char *const enum_two_argument_call_sources[] = {
    "#include <enum-two-argument-call.h>\n"
    "enum EnumTwoArgumentValues {\n"
    "  /// enum two argument callee\n"
    "  ENUM_TWO_ARGUMENT_CALL = 7,\n"
    "  /// enum two argument first\n"
    "  ENUM_TWO_ARGUMENT_FIRST = 1,\n"
    "  /// enum two argument second\n"
    "  ENUM_TWO_ARGUMENT_SECOND = 2\n"
    "};\n"
    "enum { ENUM_TWO_ARGUMENT_DERIVED = "
    "ENUM_TWO_ARGUMENT_CALL(  ENUM_TWO_ARGUMENT_FIRST  ,  "
    "ENUM_TWO_ARGUMENT_SECOND  )",
    "#include <enum-two-argument-call.h>\n"
    "enum EnumTwoArgumentValues {\n"
    "  /// enum two argument callee\n"
    "  ENUM_TWO_ARGUMENT_CALL = 7,\n"
    "  /// enum two argument first\n"
    "  ENUM_TWO_ARGUMENT_FIRST = 1,\n"
    "  /// enum two argument second\n"
    "  ENUM_TWO_ARGUMENT_SECOND = 2\n"
    "};\n"
    "enum { ENUM_TWO_ARGUMENT_DERIVED = "
    "ENUM_TWO_ARGUMENT_CALL(  ENUM_TWO_ARGUMENT_FIRST  "
    "/* before comma */ , /* after comma */  "
    "ENUM_TWO_ARGUMENT_SECOND  )",
    "#include <enum-two-argument-call.h>\n"
    "enum EnumTwoArgumentValues {\n"
    "  /// enum two argument callee\n"
    "  ENUM_TWO_ARGUMENT_CALL = 7,\n"
    "  /// enum two argument first\n"
    "  ENUM_TWO_ARGUMENT_FIRST = 1,\n"
    "  /// enum two argument second\n"
    "  ENUM_TWO_ARGUMENT_SECOND = 2\n"
    "};\n"
    "enum { ENUM_TWO_ARGUMENT_DERIVED = "
    "ENUM_TWO_ARGUMENT_CALL(  ENUM_TWO_ARGUMENT_FIRST  \\\n"
    "  ,  \\\n"
    "  ENUM_TWO_ARGUMENT_SECOND  )",
    "#include <enum-two-argument-call.h>\n"
    "enum EnumTwoArgumentValues {\n"
    "  /// enum two argument callee\n"
    "  ENUM_TWO_ARGUMENT_CALL = 7,\n"
    "  /// enum two argument first\n"
    "  ENUM_TWO_ARGUMENT_FIRST = 1,\n"
    "  /// enum two argument second\n"
    "  ENUM_TWO_ARGUMENT_SECOND = 2\n"
    "};\n"
    "enum { ENUM_TWO_ARGUMENT_DERIVED = "
    "ENUM_TWO_ARGUMENT_CALL(  ENUM_TWO_ARGUMENT_FIRST  \\\r\n"
    "  ,  \\\r\n"
    "  ENUM_TWO_ARGUMENT_SECOND  )",
};

static const char *const enum_three_argument_call_headers[] = {
    "/// enum three argument function-like macro\n"
    "#define ENUM_THREE_ARGUMENT_CALL(first, middle, last) "
    "((first) + (middle) + (last) + 100)\n",
    "/// enum three argument updated function-like macro\n"
    "#define ENUM_THREE_ARGUMENT_CALL(left, center, right) "
    "((left) + (center) + (right) + 200)\n",
    "/// enum three argument metadata-only function-like macro\n"
    "#define ENUM_THREE_ARGUMENT_CALL(lhs, mid, rhs) "
    "((lhs) + (mid) + (rhs) + 100)\n"};

static const char *const enum_three_argument_call_sources[] = {
    "#include <enum-three-argument-call.h>\n"
    "/// enum three argument first macro\n"
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n"
    "/// enum three argument middle macro\n"
    "#define ENUM_THREE_ARGUMENT_MIDDLE_MACRO 2\n"
    "/// enum three argument last macro\n"
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO "
    "/* first comma */ , ENUM_THREE_ARGUMENT_MIDDLE_MACRO "
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_MACRO)",
    "#include <enum-three-argument-call.h>\n"
    "/// enum three argument first macro\n"
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n"
    "/// enum three argument middle macro\n"
    "#define ENUM_THREE_ARGUMENT_MIDDLE_MACRO 2\n"
    "/// enum three argument last macro\n"
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO \\\r\n"
    "  , ENUM_THREE_ARGUMENT_MIDDLE_MACRO \\\r\n"
    "  , ENUM_THREE_ARGUMENT_LAST_MACRO)",
    "#include <enum-three-argument-call.h>\n"
    "enum EnumThreeArgumentMixedValues {\n"
    "  /// enum three argument first enum\n"
    "  ENUM_THREE_ARGUMENT_FIRST_ENUM = 4,\n"
    "  /// enum three argument last enum\n"
    "  ENUM_THREE_ARGUMENT_LAST_ENUM = 5\n"
    "};\n"
    "/// enum three argument middle macro\n"
    "#define ENUM_THREE_ARGUMENT_MIDDLE_MACRO 2\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_ENUM "
    "/* first comma */ , ENUM_THREE_ARGUMENT_MIDDLE_MACRO "
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_ENUM)",
    "#include <enum-three-argument-call.h>\n"
    "enum EnumThreeArgumentMixedValues {\n"
    "  /// enum three argument first enum\n"
    "  ENUM_THREE_ARGUMENT_FIRST_ENUM = 4,\n"
    "  /// enum three argument last enum\n"
    "  ENUM_THREE_ARGUMENT_LAST_ENUM = 5\n"
    "};\n"
    "/// enum three argument middle macro\n"
    "#define ENUM_THREE_ARGUMENT_MIDDLE_MACRO 2\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_ENUM \\\r\n"
    "  , ENUM_THREE_ARGUMENT_MIDDLE_MACRO \\\r\n"
    "  , ENUM_THREE_ARGUMENT_LAST_ENUM)",
    "#include <enum-three-argument-call.h>\n"
    "/// enum three argument first macro\n"
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n"
    "enum EnumThreeArgumentMiddleValue {\n"
    "  /// enum three argument middle enum\n"
    "  ENUM_THREE_ARGUMENT_MIDDLE_ENUM = 6\n"
    "};\n"
    "/// enum three argument last macro\n"
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO "
    "/* first comma */ , ENUM_THREE_ARGUMENT_MIDDLE_ENUM "
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_MACRO)",
    "#include <enum-three-argument-call.h>\n"
    "/// enum three argument first macro\n"
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n"
    "enum EnumThreeArgumentMiddleValue {\n"
    "  /// enum three argument middle enum\n"
    "  ENUM_THREE_ARGUMENT_MIDDLE_ENUM = 6\n"
    "};\n"
    "/// enum three argument last macro\n"
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO \\\r\n"
    "  , ENUM_THREE_ARGUMENT_MIDDLE_ENUM \\\r\n"
    "  , ENUM_THREE_ARGUMENT_LAST_MACRO)",
    "#include <enum-three-argument-call.h>\n"
    "/// enum three argument first macro\n"
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n"
    "enum EnumThreeArgumentMiddleValue {\n"
    "  /// enum three argument middle enum\n"
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 6\n"
    "};\n"
    "/// enum three argument last macro\n"
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO "
    "/* first comma */ , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM "
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_MACRO)",
    "#include <enum-three-argument-call.h>\n"
    "/// enum three argument first macro\n"
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n"
    "enum EnumThreeArgumentMiddleValue {\n"
    "  /// enum three argument middle enum\n"
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 6\n"
    "};\n"
    "/// enum three argument last macro\n"
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO \\\r\n"
    "  , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM \\\r\n"
    "  , ENUM_THREE_ARGUMENT_LAST_MACRO)",
    "#include <enum-three-argument-call.h>\n"
    "/// enum three argument first macro\n"
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n"
    "enum EnumThreeArgumentMiddleValue {\n"
    "  /// enum three argument updated middle enum\n"
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 9\n"
    "};\n"
    "/// enum three argument last macro\n"
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO "
    "/* first comma */ , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM "
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_MACRO)",
    "#include <enum-three-argument-call.h>\n"
    "/// enum three argument first macro\n"
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n"
    "enum EnumThreeArgumentMiddleValue {\n"
    "  /// enum three argument updated middle enum\n"
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 9\n"
    "};\n"
    "/// enum three argument last macro\n"
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO \\\r\n"
    "  , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM \\\r\n"
    "  , ENUM_THREE_ARGUMENT_LAST_MACRO)",
    "#include <enum-three-argument-call.h>\n"
    "/// enum three argument first macro\n"
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 7\n"
    "enum EnumThreeArgumentMiddleValue {\n"
    "  /// enum three argument updated middle enum\n"
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 9\n"
    "};\n"
    "/// enum three argument last macro\n"
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 11\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO "
    "/* first comma */ , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM "
    "/* second comma */ , ENUM_THREE_ARGUMENT_LAST_MACRO)",
    "#include <enum-three-argument-call.h>\n"
    "/// enum three argument first macro\n"
    "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 7\n"
    "enum EnumThreeArgumentMiddleValue {\n"
    "  /// enum three argument updated middle enum\n"
    "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 9\n"
    "};\n"
    "/// enum three argument last macro\n"
    "#define ENUM_THREE_ARGUMENT_LAST_MACRO 11\n"
    "enum { ENUM_THREE_ARGUMENT_DERIVED = "
    "ENUM_THREE_ARGUMENT_CALL(ENUM_THREE_ARGUMENT_FIRST_MACRO \\\r\n"
    "  , ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM \\\r\n"
    "  , ENUM_THREE_ARGUMENT_LAST_MACRO)",
};

static const char initializer_designator_operand_hover_source[] =
    "/// initializer designator macro documentation\n"
    "#define INITIALIZER_DESIGNATOR_MACRO 5\n"
    "enum InitializerDesignatorValue {\n"
    "  INITIALIZER_DESIGNATOR_A = 2,\n"
    "  INITIALIZER_DESIGNATOR_B = 3,\n"
    "  INITIALIZER_DESIGNATOR_C = 4,\n"
    "  INITIALIZER_DESIGNATOR_CONDITION = 1\n"
    "};\n"
    "int initializer_designator_direct[16] = { [INITIALIZER_DESIGNATOR_A] = 1 };\n"
    "int initializer_designator_unary[16] = { [+INITIALIZER_DESIGNATOR_A] = 1 };\n"
    "int initializer_designator_binary[16] = { [INITIALIZER_DESIGNATOR_A + INITIALIZER_DESIGNATOR_B] = 1 };\n"
    "int initializer_designator_grouped[16] = { [(INITIALIZER_DESIGNATOR_C)] = 1 };\n"
    "int initializer_designator_conditional[16] = { [INITIALIZER_DESIGNATOR_CONDITION ? INITIALIZER_DESIGNATOR_A : INITIALIZER_DESIGNATOR_B] = 1 };\n"
    "int initializer_designator_macro[16] = { [INITIALIZER_DESIGNATOR_MACRO] = 1 };\n"
    "int initializer_designator_comment[16] = { [/* expression gap */ INITIALIZER_DESIGNATOR_A] = 1 };\n"
    "int initializer_designator_splice_lf[16] = { [\\\n"
    "INITIALIZER_DESIGNATOR_B] = 1 };\n"
    "int initializer_designator_splice_crlf[16] = { [\\\r\n"
    "INITIALIZER_DESIGNATOR_C] = 1 };\r\n"
    "int initializer_designator_nested[2][16] = { [1] = { [INITIALIZER_DESIGNATOR_A] = 1 } };\n"
    "struct InitializerDesignatorRecord { int value; };\n"
    "struct InitializerDesignatorRecord initializer_designator_member_chain[8] = { [INITIALIZER_DESIGNATOR_A].value = 1 };\n"
    "int initializer_designator_array_chain[8][8] = { [INITIALIZER_DESIGNATOR_A][INITIALIZER_DESIGNATOR_B] = 1 };\n"
    "int initializer_operand_scalar = { INITIALIZER_DESIGNATOR_A };\n"
    "int initializer_operand_nested[2][2] = { { INITIALIZER_DESIGNATOR_B, 0 }, { 0, 0 } };\n"
    "int initializer_operand_binary = { INITIALIZER_DESIGNATOR_A + INITIALIZER_DESIGNATOR_C };\n"
    "int initializer_operand_macro = { /* value gap */ INITIALIZER_DESIGNATOR_MACRO };\n"
    "int initializer_operand_multi = { INITIALIZER_DESIGNATOR_C }, initializer_operand_later;\n"
    "int initializer_designator_multi[16] = { [INITIALIZER_DESIGNATOR_B] = 1 }, initializer_designator_later[16];\n"
    "static int initializer_designator_block(int designator_parameter) {\n"
    "  enum { INITIALIZER_DESIGNATOR_LOCAL = 6 };\n"
    "  int designator_before = designator_parameter;\n"
    "  int designator_local[16] = { [INITIALIZER_DESIGNATOR_LOCAL] = 1 };\n"
    "  int designator_operand_local[2] = { designator_parameter, INITIALIZER_DESIGNATOR_LOCAL };\n"
    "  int designator_after = designator_before;\n"
    "  return designator_local[INITIALIZER_DESIGNATOR_LOCAL] + designator_after;\n"
    "}\n";

static const char compound_literal_designator_operand_hover_source[] =
    "/// compound literal designator macro documentation\n"
    "#define COMPOUND_LITERAL_DESIGNATOR_MACRO 5\n"
    "typedef int CompoundLiteralDesignatorArray[8];\n"
    "enum CompoundLiteralDesignatorValue {\n"
    "  COMPOUND_LITERAL_DESIGNATOR_A = 2,\n"
    "  COMPOUND_LITERAL_DESIGNATOR_B = 3,\n"
    "  COMPOUND_LITERAL_DESIGNATOR_C = 4,\n"
    "  COMPOUND_LITERAL_DESIGNATOR_CONDITION = 1\n"
    "};\n"
    "struct CompoundLiteralDesignatorRecord { int values[8]; };\n"
    "int *compound_literal_designator_file_direct = (int[8]){ "
    "[COMPOUND_LITERAL_DESIGNATOR_A] = 1 };\n"
    "int *compound_literal_designator_file_unary = (int[8]){ "
    "[+COMPOUND_LITERAL_DESIGNATOR_A] = 1 };\n"
    "int *compound_literal_designator_file_binary = (int[8]){ "
    "[COMPOUND_LITERAL_DESIGNATOR_A + COMPOUND_LITERAL_DESIGNATOR_B] = 1 };\n"
    "int *compound_literal_designator_file_grouped = (int[8]){ "
    "[(COMPOUND_LITERAL_DESIGNATOR_C)] = 1 };\n"
    "int *compound_literal_designator_file_conditional = (int[8]){ "
    "[COMPOUND_LITERAL_DESIGNATOR_CONDITION ? "
    "COMPOUND_LITERAL_DESIGNATOR_A : COMPOUND_LITERAL_DESIGNATOR_B] = 1 };\n"
    "int *compound_literal_designator_file_macro = (int[8]){ "
    "[COMPOUND_LITERAL_DESIGNATOR_MACRO] = 1 };\n"
    "int *compound_literal_designator_file_comment = (int[8]){ "
    "[/* expression gap */ COMPOUND_LITERAL_DESIGNATOR_A] = 1 };\n"
    "int *compound_literal_designator_file_splice_lf = (int[8]){ [\\\n"
    "COMPOUND_LITERAL_DESIGNATOR_B] = 1 };\n"
    "int *compound_literal_designator_file_splice_crlf = (int[8]){ [\\\r\n"
    "COMPOUND_LITERAL_DESIGNATOR_C] = 1 };\r\n"
    "int *compound_literal_designator_file_typedef = "
    "(CompoundLiteralDesignatorArray){ "
    "[COMPOUND_LITERAL_DESIGNATOR_A] = 1 };\n"
    "int (*compound_literal_designator_file_nested)[8] = (int[2][8]){ "
    "[1] = { [COMPOUND_LITERAL_DESIGNATOR_B] = 1 } };\n"
    "struct CompoundLiteralDesignatorRecord "
    "*compound_literal_designator_file_member = "
    "&(struct CompoundLiteralDesignatorRecord){ "
    ".values[COMPOUND_LITERAL_DESIGNATOR_A] = 1 };\n"
    "int (*compound_literal_designator_file_chain)[8] = (int[8][8]){ "
    "[COMPOUND_LITERAL_DESIGNATOR_A][COMPOUND_LITERAL_DESIGNATOR_B] = 1 };\n"
    "int *compound_literal_operand_file = (int[8]){ "
    "COMPOUND_LITERAL_DESIGNATOR_A, 0 };\n"
    "struct CompoundLiteralDesignatorRecord "
    "*compound_literal_operand_record = "
    "&(struct CompoundLiteralDesignatorRecord){ .values = { "
    "COMPOUND_LITERAL_DESIGNATOR_B, 0 } };\n"
    "int *compound_literal_designator_file_multi = (int[8]){ "
    "[COMPOUND_LITERAL_DESIGNATOR_C] = 1 }, "
    "*compound_literal_designator_file_later;\n"
    "int compound_literal_designator_file_after;\n"
    "static int compound_literal_designator_take(int *values) { "
    "return values[0]; }\n"
    "static int compound_literal_designator_block(int designator_parameter) {\n"
    "  int designator_before = designator_parameter;\n"
    "  int designator_value = ((int[8]){ "
    "[COMPOUND_LITERAL_DESIGNATOR_A] = 1 })"
    "[COMPOUND_LITERAL_DESIGNATOR_A];\n"
    "  int designator_call = compound_literal_designator_take((int[8]){ "
    "[COMPOUND_LITERAL_DESIGNATOR_B] = 1 });\n"
    "  int operand_value = ((int[8]){ designator_parameter, "
    "COMPOUND_LITERAL_DESIGNATOR_A })[0];\n"
    "  int operand_record = ((struct CompoundLiteralDesignatorRecord){ "
    ".values = { COMPOUND_LITERAL_DESIGNATOR_B, 0 } }).values[0];\n"
    "  int operand_macro = ((int[8]){ "
    "COMPOUND_LITERAL_DESIGNATOR_MACRO, 0 })[0];\n"
    "  int *designator_multi = (int[8]){ "
    "[COMPOUND_LITERAL_DESIGNATOR_C] = 1 }, *designator_later;\n"
    "  int designator_after = designator_before;\n"
    "  return designator_value + designator_call + designator_after +\n"
    "         (designator_multi != 0) + (designator_later != 0);\n"
    "}\n";

static const char type_name_array_bound_operand_hover_source[] =
    "/// type-name array bound macro documentation\n"
    "#define TYPE_NAME_ARRAY_BOUND_MACRO 5\n"
    "enum TypeNameArrayBoundValue {\n"
    "  TYPE_NAME_ARRAY_BOUND_A = 2,\n"
    "  TYPE_NAME_ARRAY_BOUND_B = 3,\n"
    "  TYPE_NAME_ARRAY_BOUND_C = 4,\n"
    "  TYPE_NAME_ARRAY_BOUND_CONDITION = 1\n"
    "};\n"
    "int type_name_array_bound_values[8];\n"
    "int type_name_array_bound_file = sizeof(int[TYPE_NAME_ARRAY_BOUND_A]), type_name_array_bound_later;\n"
    "static int type_name_array_bound_block(int bound_parameter) {\n"
    "  enum { TYPE_NAME_ARRAY_BOUND_LOCAL = 6 };\n"
    "  int bound_before = bound_parameter;\n"
    "  int bound_sizeof_direct = sizeof(int[TYPE_NAME_ARRAY_BOUND_A]);\n"
    "  int bound_sizeof_unary = sizeof(int[+TYPE_NAME_ARRAY_BOUND_A]);\n"
    "  int bound_sizeof_binary = sizeof(int[TYPE_NAME_ARRAY_BOUND_A + TYPE_NAME_ARRAY_BOUND_B]);\n"
    "  int bound_sizeof_grouped = sizeof(int[(TYPE_NAME_ARRAY_BOUND_C)]);\n"
    "  int bound_sizeof_conditional = sizeof(int[TYPE_NAME_ARRAY_BOUND_CONDITION ? TYPE_NAME_ARRAY_BOUND_A : TYPE_NAME_ARRAY_BOUND_B]);\n"
    "  int bound_sizeof_macro = sizeof(int[TYPE_NAME_ARRAY_BOUND_MACRO]);\n"
    "  int bound_sizeof_comment = sizeof(int[/* expression gap */ TYPE_NAME_ARRAY_BOUND_A]);\n"
    "  int bound_sizeof_splice_lf = sizeof(int[\\\n"
    "TYPE_NAME_ARRAY_BOUND_B]);\n"
    "  int bound_sizeof_splice_crlf = sizeof(int[\\\r\n"
    "TYPE_NAME_ARRAY_BOUND_C]);\r\n"
    "  int bound_alignof = _Alignof(int[TYPE_NAME_ARRAY_BOUND_A]);\n"
    "  void *bound_cast = (int (*)[TYPE_NAME_ARRAY_BOUND_B])0;\n"
    "  int *bound_compound = (int[TYPE_NAME_ARRAY_BOUND_C]){ 0 };\n"
    "  int bound_cast_postfix = (*(int (*)[TYPE_NAME_ARRAY_BOUND_B])&type_name_array_bound_values)[0];\n"
    "  int bound_compound_postfix = (int[TYPE_NAME_ARRAY_BOUND_C]){ 1 }[0];\n"
    "  int bound_generic = _Generic(&type_name_array_bound_values, int (*)[TYPE_NAME_ARRAY_BOUND_A]: 1, default: 0);\n"
    "  int bound_local = sizeof(int[TYPE_NAME_ARRAY_BOUND_LOCAL]);\n"
    "  int bound_after = bound_before;\n"
    "  return bound_sizeof_direct + bound_sizeof_unary + bound_sizeof_binary +\n"
    "         bound_sizeof_grouped + bound_sizeof_conditional +\n"
    "         bound_sizeof_macro + bound_sizeof_comment +\n"
    "         bound_sizeof_splice_lf + bound_sizeof_splice_crlf +\n"
    "         bound_alignof + bound_cast_postfix + bound_compound_postfix +\n"
    "         bound_generic + bound_local + bound_after;\n"
    "}\n";

static const char declarator_array_bound_operand_hover_source[] =
    "/// declarator array bound macro documentation\n"
    "#define DECLARATOR_ARRAY_BOUND_MACRO 4\n"
    "enum DeclaratorArrayBoundValue {\n"
    "  DECLARATOR_ARRAY_BOUND_ENUM = 3\n"
    "};\n"
    "typedef int DeclaratorArrayElement;\n"
    "int declarator_array_bound_file[DECLARATOR_ARRAY_BOUND_MACRO], "
    "declarator_array_bound_later;\n"
    "DeclaratorArrayElement declarator_array_bound_typedef["
    "DECLARATOR_ARRAY_BOUND_MACRO];\n"
    "int declarator_array_bound_enum[DECLARATOR_ARRAY_BOUND_ENUM];\n"
    "struct DeclaratorArrayBoundRecord {\n"
    "  int member[DECLARATOR_ARRAY_BOUND_ENUM];\n"
    "  int later_member;\n"
    "};\n"
    "static int declarator_array_bound_block(int bound_parameter) {\n"
    "  int bound_before = bound_parameter;\n"
    "  int local_values[bound_parameter];\n"
    "  int bound_after = bound_before;\n"
    "  return sizeof(local_values) + bound_after;\n"
    "}\n"
    "static int declarator_array_bound_subscript(int subscript_index) {\n"
    "  return declarator_array_bound_file[subscript_index];\n"
    "}\n";

static const char macro_definition_forms_source[] =
    "#define SIMPLE_MACRO 1\n"
    "# define PARENTHESIZED_MACRO (2 + 3)\r\n"
    "#define FUNCTION_MACRO(value, other) ((value) + (other))\n"
    "#define EMPTY_MACRO\n"
    "#define CONTINUED_OBJECT (1 + \\\n"
    "  2)\n"
    "#define CONTINUED_FUNCTION(value) ((value) + \\\r\n"
    "  1)\r\n"
    "?" "?= define TRIGRAPH_HASH_MACRO 7\n"
    "#define SPL\\\n"
    "ICED_NAME_MACRO 8\n"
    "#def\\\n"
    "ine SPLIT_DEFINE_MACRO 9\n"
    "#if 0\n"
    "#define BRANCH_MACRO 10\n"
    "#else\n"
    "#define BRANCH_MACRO 11\n"
    "#endif\n"
    "#define REDEFINED_MACRO 12\n"
    "#undef REDEFINED_MACRO\n"
    "#define REDEFINED_MACRO 13\n"
    "#pragma macro_definition_boundary\n"
    "#define COMMENT_UNDEF_MACRO 14\n"
    "#undef /* undef gap */ COMMENT_UNDEF_MACRO\n"
    "#define SPLICED_UNDEF_MACRO 15\n"
    "#undef \\\n"
    "  SPLICED_UNDEF_MACRO\n"
    "#undef NEVER_DEFINED_UNDEF_MACRO\n"
    "#define CONDITIONAL_FALSE_MACRO 0\n"
    "#ifdef SIMPLE_MACRO\n"
    "int conditional_ifdef_value;\n"
    "#endif\n"
    "#ifndef SIMPLE_MACRO\n"
    "int conditional_ifndef_hidden_value;\n"
    "#endif\n"
    "#ifndef /* condition gap */ SIMPLE_MACRO\n"
    "int conditional_ifndef_comment_hidden_value;\n"
    "#endif\n"
    "#ifndef \\\n"
    "  SIMPLE_MACRO\n"
    "int conditional_ifndef_spliced_hidden_value;\n"
    "#endif\n"
    "#ifndef NEVER_DEFINED_CONDITIONAL_MACRO\n"
    "int conditional_ifndef_undefined_value;\n"
    "#endif\n"
    "#if SIMPLE_MACRO\n"
    "int conditional_direct_value;\n"
    "#endif\n"
    "#if defined(SIMPLE_MACRO)\n"
    "int conditional_defined_call_value;\n"
    "#endif\n"
    "#if defined SIMPLE_MACRO\n"
    "int conditional_defined_space_value;\n"
    "#endif\n"
    "#if /* condition gap */ SIMPLE_MACRO\n"
    "int conditional_comment_value;\n"
    "#endif\n"
    "#if \\\n"
    "  SIMPLE_MACRO\n"
    "int conditional_spliced_value;\n"
    "#endif\n"
    "#if 0\n"
    "int conditional_hidden_value;\n"
    "#elif SIMPLE_MACRO\n"
    "int conditional_elif_value;\n"
    "#endif\n"
    "#if 0\n"
    "int conditional_elif_false_first_hidden;\n"
    "#elif CONDITIONAL_FALSE_MACRO\n"
    "int conditional_elif_false_hidden_value;\n"
    "#endif\n"
    "#if 0\n"
    "int conditional_elif_defined_first_hidden;\n"
    "#elif defined(SIMPLE_MACRO)\n"
    "int conditional_elif_defined_value;\n"
    "#endif\n"
    "#if 0\n"
    "int conditional_elif_comment_first_hidden;\n"
    "#elif /* condition gap */ SIMPLE_MACRO\n"
    "int conditional_elif_comment_value;\n"
    "#endif\n"
    "#if 0\n"
    "int conditional_elif_spliced_first_hidden;\n"
    "#elif \\\n"
    "  SIMPLE_MACRO\n"
    "int conditional_elif_spliced_value;\n"
    "#endif\n"
    "#if 0\n"
    "#if 1\n"
    "int conditional_elif_nested_first_hidden;\n"
    "#endif\n"
    "#elif SIMPLE_MACRO\n"
    "int conditional_elif_nested_value;\n"
    "#endif\n"
    "#if 0\n"
    "int conditional_elif_undefined_first_hidden;\n"
    "#elif NEVER_DEFINED_ELIF_MACRO\n"
    "int conditional_elif_undefined_hidden_value;\n"
    "#endif\n"
    "#if 0\n"
    "int conditional_elif_defined_undefined_first_hidden;\n"
    "#elif defined(NEVER_DEFINED_ELIF_MACRO)\n"
    "int conditional_elif_defined_undefined_hidden_value;\n"
    "#endif\n"
    "#if CONDITIONAL_FALSE_MACRO\n"
    "int conditional_false_hidden_value;\n"
    "#endif\n"
    "static int conditional_block(void) {\n"
    "#if SIMPLE_MACRO\n"
    "  return 1;\n"
    "#else\n"
    "  return 0;\n"
    "#endif\n"
    "}\n"
    "static int conditional_false_block(void) {\n"
    "#if CONDITIONAL_FALSE_MACRO\n"
    "  return 1;\n"
    "#else\n"
    "  return 0;\n"
    "#endif\n"
    "}\n"
    "static int conditional_elif_false_block(void) {\n"
    "#if 0\n"
    "  return 1;\n"
    "#elif CONDITIONAL_FALSE_MACRO\n"
    "  return 2;\n"
    "#else\n"
    "  return 0;\n"
    "#endif\n"
    "}\n"
    "EMPTY_MACRO\n"
    "enum MacroDefinitionEnum { MACRO_DEFINITION_ENUM = 1 };\n"
    "int macro_definition_left, macro_definition_right[2];\n"
    "int macro_definition_prototype(int value);\n"
    "int macro_definition_function(void) {\n"
    "  return SIMPLE_MACRO + PARENTHESIZED_MACRO +\n"
    "         FUNCTION_MACRO(CONTINUED_OBJECT, CONTINUED_FUNCTION(1)) +\n"
    "         TRIGRAPH_HASH_MACRO + SPLICED_NAME_MACRO +\n"
    "         SPLIT_DEFINE_MACRO + BRANCH_MACRO + REDEFINED_MACRO;\n"
    "}\n";

static const char conditional_logical_line_source[] =
    "#define CONDITIONAL_FALSE_MACRO 0\n"
    "#i\\\n"
    "f 0\n"
    "int conditional_elif_split_opener_first_hidden;\n"
    "#elif CONDITIONAL_FALSE_MACRO\n"
    "int conditional_elif_split_opener_hidden_value;\n"
    "#endif\n"
    "# /* opener gap */ if 0\n"
    "int conditional_elif_comment_opener_first_hidden;\n"
    "#elif CONDITIONAL_FALSE_MACRO\n"
    "int conditional_elif_comment_opener_hidden_value;\n"
    "#endif\n"
    "# \\\r\n"
    "if 0\n"
    "int conditional_elif_spliced_opener_first_hidden;\n"
    "#elif CONDITIONAL_FALSE_MACRO\n"
    "int conditional_elif_spliced_opener_hidden_value;\n"
    "#endif\n"
    "#if 0\n"
    "#if 1\n"
    "int conditional_elif_split_endif_first_hidden;\n"
    "#end\\\n"
    "if\n"
    "#elif CONDITIONAL_FALSE_MACRO\n"
    "int conditional_elif_split_endif_hidden_value;\n"
    "#endif\n"
    "#if 0\n"
    "int conditional_elif_split_current_first_hidden;\n"
    "#el\\\n"
    "if CONDITIONAL_FALSE_MACRO\n"
    "int conditional_elif_split_current_hidden_value;\n"
    "#endif\n";
static const char *const macro_definition_project_sources[] = {
    "/// project definition v1\n#define PROJECT_DEFINITION 21\n"
    "int project_value(void) { return PROJECT_DEFINITION; }\n",
    "\n/// project definition v2\n#define PROJECT_DEFINITION 22\n"
    "int project_value(void) { return PROJECT_DEFINITION; }\n",
};
static const char macro_definition_header_source[] =
    "/// virtual header definition\n"
    "#define HEADER_DEFINITION(value) ((value) + 2)\n";

static const char *const enum_documentation_header_revisions[] = {
    "/// header enum v1\n"
    "enum HeaderDirection {\n"
    "  /// header constant v1\n"
    "  HEADER_DIRECTION_VALUE = 1\n"
    "};\n",
    "/** header enum v2 */\n"
    "enum HeaderDirection {\n"
    "  /** header constant v2 */\n"
    "  HEADER_DIRECTION_VALUE = 2\n"
    "};\n",
    "enum HeaderDirection { HEADER_DIRECTION_VALUE = 3 };\n",
};
static const char enum_documentation_header_main[] =
    "#include \"enum-doc.h\"\n"
    "int read_header_direction(enum HeaderDirection direction) {\n"
    "  return direction == HEADER_DIRECTION_VALUE;\n"
    "}\n";
static const char *const enum_documentation_project_revisions[] = {
    "/// project enum v1\n"
    "enum ProjectDirection {\n"
    "  /// project constant v1\n"
    "  PROJECT_DIRECTION_VALUE = 1\n"
    "};\n"
    "int read_project_direction(enum ProjectDirection direction) {\n"
    "  return direction == PROJECT_DIRECTION_VALUE;\n"
    "}\n",
    "/** project enum v2 */\n"
    "enum ProjectDirection {\n"
    "  /** project constant v2 */\n"
    "  PROJECT_DIRECTION_VALUE = 2\n"
    "};\n"
    "int read_project_direction(enum ProjectDirection direction) {\n"
    "  return direction == PROJECT_DIRECTION_VALUE;\n"
    "}\n",
    "enum ProjectDirection { PROJECT_DIRECTION_VALUE = 3 };\n"
    "int read_project_direction(enum ProjectDirection direction) {\n"
    "  return direction == PROJECT_DIRECTION_VALUE;\n"
    "}\n",
};
static const char enum_documentation_scope_source[] =
    "/** outer enum */\n"
    "enum ScopedDirection {\n"
    "  /** outer value */\n"
    "  SCOPED_DIRECTION_VALUE = 1\n"
    "};\n"
    "int read_inner_direction(void) {\n"
    "  /** inner enum */\n"
    "  enum ScopedDirection {\n"
    "    /** inner value */\n"
    "    SCOPED_DIRECTION_VALUE = 2\n"
    "  };\n"
    "  enum ScopedDirection value = SCOPED_DIRECTION_VALUE;\n"
    "  return value;\n"
    "}\n"
    "enum ScopedDirection outer_direction = SCOPED_DIRECTION_VALUE;\n";

static int update_guard_project(
    ag_compilation_session_t *session,
    ag_language_project_index_t *project, unsigned int revision,
    const char *move_source, header_bundle_t bundle,
    ag_language_analysis_limits_t limits,
    ag_language_analysis_error_t *error) {
  ag_language_project_source_t sources[] = {
      {"move.c", move_source, strlen(move_source)},
      {"other.c", project_guard_other_source,
       strlen(project_guard_other_source)},
      {"main.c", project_guard_main_source,
       strlen(project_guard_main_source)},
  };
  return ag_language_project_index_update(
      session, project,
      &(ag_language_project_update_request_t){
          .revision = revision,
          .sources = sources,
          .source_count = 3,
          .virtual_header_bundle = bundle.bytes,
          .virtual_header_bundle_length = bundle.length,
          .max_header_files = 32,
          .max_header_file_bytes = 1024 * 1024,
          .max_header_total_bytes = 4 * 1024 * 1024,
          .max_include_depth = 16,
          .limits = limits,
      },
      error);
}

static int analyze_named(
    ag_compilation_session_t *session, const char *source_name,
    const char *source, size_t cursor, header_bundle_t bundle,
    ag_language_analysis_limits_t limits,
    ag_language_analysis_snapshot_t *snapshot,
    ag_language_analysis_error_t *error) {
  return ag_language_analyze_source(
      session,
      &(ag_language_analysis_request_t){
          .source_name = source_name,
          .source = source,
          .source_length = strlen(source),
          .cursor_source_name = source_name,
          .cursor_byte_offset = cursor,
          .virtual_header_bundle = bundle.bytes,
          .virtual_header_bundle_length = bundle.length,
          .max_header_files = 32,
          .max_header_file_bytes = 1024 * 1024,
          .max_header_total_bytes = 4 * 1024 * 1024,
          .max_include_depth = 16,
          .limits = limits,
      },
      snapshot, error);
}

static int analyze(ag_compilation_session_t *session, const char *source,
                   size_t cursor, header_bundle_t bundle,
                   ag_language_analysis_limits_t limits,
                   ag_language_analysis_snapshot_t *snapshot,
                   ag_language_analysis_error_t *error) {
  return analyze_named(
      session, "main.c", source, cursor, bundle, limits, snapshot, error);
}

static int analyze_project_named(
    ag_compilation_session_t *session,
    const ag_language_project_index_t *project,
    const char *source_name, const char *source, size_t cursor,
    header_bundle_t bundle, ag_language_analysis_limits_t limits,
    ag_language_analysis_snapshot_t *snapshot,
    ag_language_analysis_error_t *error) {
  return ag_language_analyze_project_source(
      session, project,
      &(ag_language_analysis_request_t){
          .source_name = source_name,
          .source = source,
          .source_length = strlen(source),
          .cursor_source_name = source_name,
          .cursor_byte_offset = cursor,
          .virtual_header_bundle = bundle.bytes,
          .virtual_header_bundle_length = bundle.length,
          .max_header_files = 32,
          .max_header_file_bytes = 1024 * 1024,
          .max_header_total_bytes = 4 * 1024 * 1024,
          .max_include_depth = 16,
          .limits = limits,
      },
      snapshot, error);
}

static int update_single_source_project(
    ag_compilation_session_t *session,
    ag_language_project_index_t *project, unsigned int revision,
    const char *source, header_bundle_t bundle,
    ag_language_analysis_limits_t limits,
    ag_language_analysis_error_t *error) {
  ag_language_project_source_t project_source = {
      "main.c", source, strlen(source)};
  return ag_language_project_index_update(
      session, project,
      &(ag_language_project_update_request_t){
          .revision = revision,
          .sources = &project_source,
          .source_count = 1,
          .virtual_header_bundle = bundle.bytes,
          .virtual_header_bundle_length = bundle.length,
          .max_header_files = 32,
          .max_header_file_bytes = 1024 * 1024,
          .max_header_total_bytes = 4 * 1024 * 1024,
          .max_include_depth = 16,
          .limits = limits,
      },
      error);
}

static const ag_language_symbol_t *find_symbol(
    const ag_language_analysis_snapshot_t *snapshot, const char *name,
    ag_language_symbol_kind_t kind) {
  for (int i = 0; i < snapshot->completion_item_count; i++) {
    const ag_language_symbol_t *symbol = &snapshot->completion_items[i];
    if (symbol->kind == kind && strcmp(symbol->name, name) == 0) return symbol;
  }
  return NULL;
}

static const ag_language_symbol_t *hover_symbol(
    const ag_language_analysis_snapshot_t *snapshot) {
  return snapshot && snapshot->hover_index >= 0 &&
                 snapshot->hover_index < snapshot->completion_item_count
             ? &snapshot->completion_items[snapshot->hover_index]
             : NULL;
}

static const ag_language_diagnostic_t *find_diagnostic(
    const ag_language_analysis_snapshot_t *snapshot, const char *code) {
  for (int i = 0; i < snapshot->diagnostic_count; i++)
    if (strcmp(snapshot->diagnostics[i].code, code) == 0)
      return &snapshot->diagnostics[i];
  return NULL;
}

static int same_range(
    const ag_language_source_range_t *left,
    const ag_language_source_range_t *right) {
  return strcmp(left->source_name, right->source_name) == 0 &&
         left->start.line == right->start.line &&
         left->start.column == right->start.column &&
         left->start.offset == right->start.offset &&
         left->end.line == right->end.line &&
         left->end.column == right->end.column &&
         left->end.offset == right->end.offset;
}

static int same_optional_definition(
    const ag_language_symbol_t *left,
    const ag_language_symbol_t *right) {
  if (left->has_definition != right->has_definition ||
      left->definition_conflict != right->definition_conflict ||
      left->definition_candidate_count !=
          right->definition_candidate_count)
    return 0;
  if (left->has_definition &&
      !same_range(&left->definition, &right->definition))
    return 0;
  for (int i = 0; i < left->definition_candidate_count; i++)
    if (!same_range(&left->definition_candidates[i],
                    &right->definition_candidates[i]))
      return 0;
  return 1;
}

static int same_documentation(
    const ag_language_symbol_t *left,
    const ag_language_symbol_t *right) {
  if (!left || !right || !left->documentation || !right->documentation ||
      strcmp(left->documentation, right->documentation) != 0 ||
      left->has_documentation_range != right->has_documentation_range)
    return 0;
  return !left->has_documentation_range ||
         same_range(&left->documentation_range,
                    &right->documentation_range);
}

static int same_object_hover(
    const ag_language_symbol_t *left,
    const ag_language_symbol_t *right) {
  if (!left || !right ||
      left->kind != AG_LANGUAGE_SYMBOL_OBJECT ||
      right->kind != AG_LANGUAGE_SYMBOL_OBJECT ||
      strcmp(left->name, right->name) != 0 ||
      strcmp(left->type, right->type) != 0 ||
      strcmp(left->signature, right->signature) != 0 ||
      !same_range(&left->declaration, &right->declaration) ||
      !same_documentation(left, right) ||
      left->initializer_state != right->initializer_state ||
      strcmp(left->constant_value, right->constant_value) != 0 ||
      left->has_initializer_range != right->has_initializer_range)
    return 0;
  return !left->has_initializer_range ||
         same_range(
             &left->initializer_range, &right->initializer_range);
}

static int same_object_display(
    const ag_language_symbol_t *left,
    const ag_language_symbol_t *right) {
  return left && right &&
         left->kind == AG_LANGUAGE_SYMBOL_OBJECT &&
         right->kind == AG_LANGUAGE_SYMBOL_OBJECT &&
         strcmp(left->name, right->name) == 0 &&
         strcmp(left->type, right->type) == 0 &&
         strcmp(left->signature, right->signature) == 0 &&
         same_documentation(left, right) &&
         left->initializer_state == right->initializer_state &&
         strcmp(left->constant_value, right->constant_value) == 0;
}

static int same_function_hover(
    const ag_language_symbol_t *left,
    const ag_language_symbol_t *right) {
  if (!left || !right ||
      left->kind != AG_LANGUAGE_SYMBOL_FUNCTION ||
      right->kind != AG_LANGUAGE_SYMBOL_FUNCTION ||
      strcmp(left->name, right->name) != 0 ||
      strcmp(left->type, right->type) != 0 ||
      strcmp(left->signature, right->signature) != 0 ||
      strcmp(left->return_type, right->return_type) != 0 ||
      strcmp(left->storage_class, right->storage_class) != 0 ||
      !same_range(&left->declaration, &right->declaration) ||
      !same_documentation(left, right) ||
      !same_optional_definition(left, right) ||
      left->has_function_prototype != right->has_function_prototype ||
      left->is_variadic != right->is_variadic ||
      left->parameter_count != right->parameter_count)
    return 0;
  for (int i = 0; i < left->parameter_count; i++)
    if (strcmp(left->parameters[i].name, right->parameters[i].name) != 0 ||
        strcmp(left->parameters[i].type, right->parameters[i].type) != 0)
      return 0;
  return 1;
}

#define CHECK(condition, label)                                                  \
  do {                                                                           \
    if (!(condition)) {                                                           \
      fprintf(stderr, "language analysis check failed: %s (line %d)\n", label, \
              __LINE__);                                                         \
      return 1;                                                                  \
    }                                                                            \
  } while (0)

static int test_project_failure_recovery(int print_json) {
  static const char valid_source_12[] =
      "enum { PLAYER_SIZE = 12, PLAYER_SPEED = 2 };\n"
      "int main(void) { return PLAYER_SIZE; }\n";
  static const char invalid_source[] =
      "enum { PLAYER_SIZE = , PLAYER_SPEED = 2 };\n"
      "int main(void) { return PLAYER_SIZE; }\n";
  static const char valid_source_13[] =
      "enum { PLAYER_SIZE = 13, PLAYER_SPEED = 2 };\n"
      "int main(void) { return PLAYER_SIZE; }\n";
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  ag_language_project_index_t *project =
      ag_language_project_index_create();
  CHECK(session && project, "project failure recovery state");
  ag_language_analysis_limits_t limits =
      ag_language_analysis_default_limits();
  ag_language_analysis_error_t error = {0};
  ag_language_analysis_snapshot_t snapshot = {0};

  CHECK(update_single_source_project(
            session, project, 1, valid_source_12,
            (header_bundle_t){0}, limits, &error),
        "project failure recovery revision 1");
  const char *use_12 =
      strstr(valid_source_12, "return PLAYER_SIZE") + strlen("return ");
  CHECK(analyze_project_named(
            session, project, "main.c", valid_source_12,
            (size_t)(use_12 - valid_source_12) + strlen("PLAYER_SIZE"),
            (header_bundle_t){0}, limits, &snapshot, &error),
        "project failure recovery revision 1 hover");
  CHECK(hover_symbol(&snapshot) &&
            strcmp(hover_symbol(&snapshot)->constant_value, "12") == 0,
        "project failure recovery revision 1 value");
  ag_language_analysis_snapshot_dispose(&snapshot);

  CHECK(!update_single_source_project(
            session, project, 2, invalid_source,
            (header_bundle_t){0}, limits, &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_FAILED &&
            strcmp(error.code, "E3064") == 0,
        "project syntax failure remains structured");
  CHECK(ag_language_project_index_revision(project) == 1,
        "failed project revision is not committed");

  CHECK(update_single_source_project(
            session, project, 3, valid_source_13,
            (header_bundle_t){0}, limits, &error),
        "project failure recovery revision 3");
  const char *use_13 =
      strstr(valid_source_13, "return PLAYER_SIZE") + strlen("return ");
  CHECK(analyze_project_named(
            session, project, "main.c", valid_source_13,
            (size_t)(use_13 - valid_source_13) + strlen("PLAYER_SIZE"),
            (header_bundle_t){0}, limits, &snapshot, &error),
        "project failure recovery revision 3 hover");
  CHECK(hover_symbol(&snapshot) &&
            strcmp(hover_symbol(&snapshot)->constant_value, "13") == 0,
        "project failure recovery revision 3 value");
  if (print_json) {
    int length = ag_language_analysis_snapshot_write_json(
        &snapshot, NULL, 0);
    char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
    CHECK(json && ag_language_analysis_snapshot_write_json(
                      &snapshot, json, (size_t)length + 1) == length,
          "project failure recovery parity json");
    puts(json);
    free(json);
  }
  ag_language_analysis_snapshot_dispose(&snapshot);

  ag_language_analysis_limits_t tiny = limits;
  tiny.max_source_bytes = 8;
  CHECK(!update_single_source_project(
            session, project, 4, valid_source_13,
            (header_bundle_t){0}, tiny, &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.limit, "maxSourceBytes") == 0 &&
            ag_language_project_index_revision(project) == 3,
        "project resource failure preserves revision");
  CHECK(update_single_source_project(
            session, project, 5, valid_source_13,
            (header_bundle_t){0}, limits, &error),
        "project recovers after resource failure");

  ag_language_project_source_t duplicate_sources[] = {
      {"same.c", "int first;", strlen("int first;")},
      {"same.c", "int second;", strlen("int second;")},
  };
  CHECK(!ag_language_project_index_update(
            session, project,
            &(ag_language_project_update_request_t){
                .revision = 6,
                .sources = duplicate_sources,
                .source_count = 2,
                .limits = limits,
            },
            &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_INVALID_REQUEST &&
            ag_language_project_index_revision(project) == 5,
        "invalid project request preserves revision");

  const char *header_paths[] = {"recovery.h"};
  const char *broken_headers[] = {"#error broken header\n"};
  header_bundle_t broken_bundle = make_bundle(
      header_paths, broken_headers, 1);
  const char *header_source =
      "#include \"recovery.h\"\n"
      "enum { PLAYER_SIZE = 14 };\n"
      "int main(void) { return PLAYER_SIZE; }\n";
  CHECK(!update_single_source_project(
            session, project, 7, header_source, broken_bundle,
            limits, &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_FAILED &&
            error.code[0] != '\0' &&
            ag_language_project_index_revision(project) == 5,
        "project header failure preserves revision");
  free(broken_bundle.bytes);
  const char *fixed_headers[] = {"#define RECOVERY_READY 1\n"};
  header_bundle_t fixed_bundle = make_bundle(
      header_paths, fixed_headers, 1);
  CHECK(update_single_source_project(
            session, project, 8, header_source, fixed_bundle,
            limits, &error),
        "project recovers after header failure");
  const char *header_use =
      strstr(header_source, "return PLAYER_SIZE") + strlen("return ");
  CHECK(analyze_project_named(
            session, project, "main.c", header_source,
            (size_t)(header_use - header_source) + strlen("PLAYER_SIZE"),
            fixed_bundle, limits, &snapshot, &error) &&
            hover_symbol(&snapshot) &&
            strcmp(hover_symbol(&snapshot)->constant_value, "14") == 0,
        "project header recovery hover");
  ag_language_analysis_snapshot_dispose(&snapshot);
  free(fixed_bundle.bytes);

  const char *failure_sources[] = {
      "int broken(int value;\n",
      "#error broken preprocessor state\nint value;\n",
      "int main(void) { return missing_name; }\n",
  };
  unsigned int revision = 9;
  for (size_t failure_index = 0;
       failure_index < sizeof(failure_sources) / sizeof(failure_sources[0]);
       failure_index++) {
    unsigned int failed_revision = revision++;
    int failed_ok = update_single_source_project(
        session, project, failed_revision,
        failure_sources[failure_index], (header_bundle_t){0},
        limits, &error);
    CHECK(!failed_ok &&
              error.status == AG_LANGUAGE_ANALYSIS_FAILED &&
              error.code[0] != '\0' &&
              ag_language_project_index_revision(project) ==
                  failed_revision - 1,
          "project failure class preserves revision");
    CHECK(update_single_source_project(
              session, project, revision++, valid_source_13,
              (header_bundle_t){0}, limits, &error),
          "project recovers after failure class");
  }
  ag_language_project_index_destroy(project);
  ag_compilation_session_destroy(session);
  return 0;
}

static int print_parity_snapshot(void) {
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  if (!session) return 1;
  const char *paths[] = {"parity.h"};
  const char *headers[] = {
      "#define PARITY_WIDTH 320\nint parity_sum(int left, int right);\n"};
  header_bundle_t bundle = make_bundle(paths, headers, 1);
  const char *source = "/* 日本語 */\n#include <parity.h>\n"
                       "typedef unsigned long Size; int global_value;\n"
                       "int main(int parameter) { const int *local; parity_";
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = analyze(
      session, source, strlen(source), bundle,
      ag_language_analysis_default_limits(), &snapshot, &error);
  free(bundle.bytes);
  if (!ok) {
    ag_compilation_session_destroy(session);
    return 1;
  }
  int length = ag_language_analysis_snapshot_write_json(&snapshot, NULL, 0);
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  if (!json || ag_language_analysis_snapshot_write_json(
                   &snapshot, json, (size_t)length + 1) != length) {
    free(json);
    ag_language_analysis_snapshot_dispose(&snapshot);
    ag_compilation_session_destroy(session);
    return 1;
  }
  puts(json);
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return 0;
}

static int print_enum_parity_snapshot(void) {
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  if (!session) return 1;
  const char *source =
      "enum {\n"
      "  PLAYER_SIZE = 12,\n"
      "  PLAYER_SPEED = 2\n"
      "};\n"
      "int main(void) { return PLAYER_SIZE + PLAYER_SPEED; }\n";
  const char *name = strstr(source, "PLAYER_SIZE");
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = analyze(
      session, source, (size_t)(name - source) + 4,
      (header_bundle_t){0}, ag_language_analysis_default_limits(),
      &snapshot, &error);
  if (!ok) {
    ag_compilation_session_destroy(session);
    return 1;
  }
  int length = ag_language_analysis_snapshot_write_json(&snapshot, NULL, 0);
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  if (!json || ag_language_analysis_snapshot_write_json(
                   &snapshot, json, (size_t)length + 1) != length) {
    free(json);
    ag_language_analysis_snapshot_dispose(&snapshot);
    ag_compilation_session_destroy(session);
    return 1;
  }
  puts(json);
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return 0;
}

static int print_include_only_parity_snapshot(void) {
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  if (!session) return 1;
  const char *paths[] = {"game.h"};
  const char *headers[] = {
      "#define GAME_SCREEN_WIDTH 640\n"
      "int game_running(void);\n"};
  header_bundle_t bundle = make_bundle(paths, headers, 1);
  const char *source = "#include <game.h>\n\n";
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = analyze_named(
      session, "aab/a.c", source, strlen(source), bundle,
      ag_language_analysis_default_limits(), &snapshot, &error);
  free(bundle.bytes);
  if (!ok) {
    ag_compilation_session_destroy(session);
    return 1;
  }
  int length = ag_language_analysis_snapshot_write_json(&snapshot, NULL, 0);
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  if (!json || ag_language_analysis_snapshot_write_json(
                   &snapshot, json, (size_t)length + 1) != length) {
    free(json);
    ag_language_analysis_snapshot_dispose(&snapshot);
    ag_compilation_session_destroy(session);
    return 1;
  }
  puts(json);
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return 0;
}

static int print_object_parity_snapshot(void) {
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  if (!session) return 1;
  const char *paths[] = {"game.h"};
  const char *headers[] = {"int game_running(void);\n"};
  header_bundle_t bundle = make_bundle(paths, headers, 1);
  const char *source =
      "#include <game.h>\n"
      "static int player_x;\n"
      "\n"
      "int main(void) {\n"
      "  while (game_running()) {\n"
      "    player_x++;\n"
      "  }\n"
      "  return 0;\n"
      "}\n";
  const char *name = strstr(source, "player_x");
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = analyze(
      session, source, (size_t)(name - source) + 4, bundle,
      ag_language_analysis_default_limits(), &snapshot, &error);
  free(bundle.bytes);
  if (!ok) {
    ag_compilation_session_destroy(session);
    return 1;
  }
  int length = ag_language_analysis_snapshot_write_json(&snapshot, NULL, 0);
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  if (!json || ag_language_analysis_snapshot_write_json(
                   &snapshot, json, (size_t)length + 1) != length) {
    free(json);
    ag_language_analysis_snapshot_dispose(&snapshot);
    ag_compilation_session_destroy(session);
    return 1;
  }
  puts(json);
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return 0;
}

static int print_function_definition_parity_snapshot(void) {
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  if (!session) return 1;
  const char *paths[] = {"game.h"};
  const char *headers[] = {""};
  header_bundle_t bundle = make_bundle(paths, headers, 1);
  const char *source =
      "#include <game.h>\n"
      "static void move_and_draw(void) {}\n"
      "int main(void) { move_and_draw(); return 0; }\n";
  const char *name = strstr(source, "move_and_draw");
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = analyze(
      session, source,
      (size_t)(name - source) + strlen("move_and_draw") / 2,
      bundle, ag_language_analysis_default_limits(),
      &snapshot, &error);
  free(bundle.bytes);
  if (!ok) {
    ag_compilation_session_destroy(session);
    return 1;
  }
  int length = ag_language_analysis_snapshot_write_json(
      &snapshot, NULL, 0);
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  if (!json || ag_language_analysis_snapshot_write_json(
                   &snapshot, json, (size_t)length + 1) != length) {
    free(json);
    ag_language_analysis_snapshot_dispose(&snapshot);
    ag_compilation_session_destroy(session);
    return 1;
  }
  puts(json);
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return 0;
}

static int print_project_function_parity_snapshot(void) {
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  ag_language_project_index_t *project =
      ag_language_project_index_create();
  if (!session || !project) {
    ag_language_project_index_destroy(project);
    ag_compilation_session_destroy(session);
    return 1;
  }
  const char *header_paths[] = {"player.h"};
  const char *header_sources[] = {
      "void move_and_draw(void);\n"
      "void declared_only_project(void);\n"
      "void local_only(void);\n"};
  header_bundle_t bundle = make_bundle(
      header_paths, header_sources, 1);
  const char *player_source =
      "#include \"player.h\"\n/* プレイヤー */\n"
      "void move_and_draw(void) {}\n";
  const char *main_source =
      "#include \"player.h\"\n\n"
      "int main(void) {\n"
      "  move_and_draw();\n"
      "  declared_only_project();\n"
      "  local_only();\n"
      "  return 0;\n"
      "}\n";
  const char *static_source = "static void local_only(void) {}\n";
  ag_language_project_source_t sources[] = {
      {"player.c", player_source, 0},
      {"main.c", main_source, 0},
      {"static_a.c", static_source, 0},
      {"static_b.c", static_source, 0},
  };
  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++)
    sources[i].source_length = strlen(sources[i].source);
  ag_language_analysis_limits_t limits =
      ag_language_analysis_default_limits();
  ag_language_analysis_error_t error = {0};
  if (!ag_language_project_index_update(
          session, project,
          &(ag_language_project_update_request_t){
              .revision = 1,
              .sources = sources,
              .source_count =
                  sizeof(sources) / sizeof(sources[0]),
              .virtual_header_bundle = bundle.bytes,
              .virtual_header_bundle_length = bundle.length,
              .max_header_files = 32,
              .max_header_file_bytes = 1024 * 1024,
              .max_header_total_bytes = 4 * 1024 * 1024,
              .max_include_depth = 16,
              .limits = limits,
          },
          &error)) {
    free(bundle.bytes);
    ag_language_project_index_destroy(project);
    ag_compilation_session_destroy(session);
    return 1;
  }
  const char *use = strstr(main_source, "move_and_draw");
  ag_language_analysis_snapshot_t snapshot = {0};
  int ok = analyze_project_named(
      session, project, "main.c", main_source,
      (size_t)(use - main_source) + strlen("move_and_draw") / 2,
      bundle, limits, &snapshot, &error);
  int length = ok ? ag_language_analysis_snapshot_write_json(
                        &snapshot, NULL, 0) : -1;
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  int result = 1;
  if (json && ag_language_analysis_snapshot_write_json(
                  &snapshot, json, (size_t)length + 1) == length) {
    puts(json);
    result = 0;
  }
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  free(bundle.bytes);
  ag_language_project_index_destroy(project);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_project_header_guard_parity_snapshot(int unterminated) {
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  ag_language_project_index_t *project =
      ag_language_project_index_create();
  if (!session || !project) {
    ag_language_project_index_destroy(project);
    ag_compilation_session_destroy(session);
    return 1;
  }
  const char *paths[] = {"move.h", "other.h"};
  const char *headers[] = {
      project_guard_move_header, project_guard_other_header,
  };
  header_bundle_t bundle = make_bundle(paths, headers, 2);
  ag_language_analysis_limits_t limits =
      ag_language_analysis_default_limits();
  ag_language_analysis_error_t error = {0};
  const char *move_source = unterminated
                                ? project_guard_moved_move_source
                                : project_guard_move_source;
  if (!update_guard_project(
          session, project, unterminated ? 35 : 34, move_source,
          bundle, limits, &error)) {
    free(bundle.bytes);
    ag_language_project_index_destroy(project);
    ag_compilation_session_destroy(session);
    return 1;
  }
  const char *current_header = unterminated
                                   ? project_guard_unterminated_header
                                   : project_guard_move_header;
  const char *declaration = strstr(current_header, "move_and_draw");
  ag_language_analysis_snapshot_t snapshot = {0};
  int ok = analyze_project_named(
      session, project, "move.h", current_header,
      (size_t)(declaration - current_header) + 1,
      bundle, limits, &snapshot, &error);
  int length = ok ? ag_language_analysis_snapshot_write_json(
                        &snapshot, NULL, 0) : -1;
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  int result = 1;
  if (json && ag_language_analysis_snapshot_write_json(
                  &snapshot, json, (size_t)length + 1) == length) {
    puts(json);
    result = 0;
  }
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  free(bundle.bytes);
  ag_language_project_index_destroy(project);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_for_control_hover_parity_snapshot(const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(for_control_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  if (!session) return 1;
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = analyze_named(
      session, "for-control.c", for_control_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0},
      ag_language_analysis_default_limits(), &snapshot, &error);
  int length = ok ? ag_language_analysis_snapshot_write_json(
                        &snapshot, NULL, 0) : -1;
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  int result = 1;
  if (json && ag_language_analysis_snapshot_write_json(
                  &snapshot, json, (size_t)length + 1) == length) {
    puts(json);
    result = 0;
  }
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_conditional_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(conditional_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  if (!session) return 1;
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = analyze_named(
      session, "conditional.c", conditional_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0},
      ag_language_analysis_default_limits(), &snapshot, &error);
  int length = ok ? ag_language_analysis_snapshot_write_json(
                        &snapshot, NULL, 0) : -1;
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  int result = 1;
  if (json && ag_language_analysis_snapshot_write_json(
                  &snapshot, json, (size_t)length + 1) == length) {
    puts(json);
    result = 0;
  }
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_generic_hover_parity_snapshot(const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(generic_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  if (!session) return 1;
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = analyze_named(
      session, "generic.c", generic_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0},
      ag_language_analysis_default_limits(), &snapshot, &error);
  int length = ok ? ag_language_analysis_snapshot_write_json(
                        &snapshot, NULL, 0) : -1;
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  int result = 1;
  if (json && ag_language_analysis_snapshot_write_json(
                  &snapshot, json, (size_t)length + 1) == length) {
    puts(json);
    result = 0;
  }
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_function_declarator_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(function_declarator_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  if (!session) return 1;
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = analyze_named(
      session, "function-declarator.c", function_declarator_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0},
      ag_language_analysis_default_limits(), &snapshot, &error);
  int length = ok ? ag_language_analysis_snapshot_write_json(
                        &snapshot, NULL, 0) : -1;
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  int result = 1;
  if (json && ag_language_analysis_snapshot_write_json(
                  &snapshot, json, (size_t)length + 1) == length) {
    puts(json);
    result = 0;
  }
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_documentation_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(documentation_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  if (!session) return 1;
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = analyze_named(
      session, "documentation.c", documentation_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0},
      ag_language_analysis_default_limits(), &snapshot, &error);
  int length = ok ? ag_language_analysis_snapshot_write_json(
                        &snapshot, NULL, 0) : -1;
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  int result = 1;
  if (json && ag_language_analysis_snapshot_write_json(
                  &snapshot, json, (size_t)length + 1) == length) {
    puts(json);
    result = 0;
  }
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_documentation_project_parity_snapshot(
    const char *revision_text) {
  char *end = NULL;
  unsigned long parsed_revision = strtoul(revision_text, &end, 10);
  if (!revision_text[0] || !end || *end != '\0' ||
      parsed_revision < 1 || parsed_revision > 4)
    return 1;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  ag_language_project_index_t *project =
      ag_language_project_index_create();
  if (!session || !project) {
    ag_language_project_index_destroy(project);
    ag_compilation_session_destroy(session);
    return 1;
  }
  const char *header_paths[] = {"player.h"};
  const char *header_with_documentation =
      "/** header prototype */\nvoid update_player(void);\n";
  const char *header_without_documentation =
      "void update_player(void);\n";
  const char *definition_v1 =
      "#include \"player.h\"\n"
      "/** definition v1 */\nvoid update_player(void) {}\n";
  const char *definition_v2 =
      "#include \"player.h\"\n"
      "/** definition v2 */\nvoid update_player(void) {}\n";
  const char *call_source =
      "#include \"player.h\"\n"
      "void update(void) { update_player(); }\n";
  ag_language_analysis_limits_t limits =
      ag_language_analysis_default_limits();
  ag_language_analysis_error_t error = {0};
  ag_language_analysis_snapshot_t snapshot = {0};
  int result = 1;
  for (unsigned int revision = 1;
       revision <= (unsigned int)parsed_revision; revision++) {
    const char *header = revision == 1 ? header_with_documentation
                                       : header_without_documentation;
    const char *header_sources[] = {header};
    header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
    const char *definition = revision <= 2 ? definition_v1 : definition_v2;
    ag_language_project_source_t sources[] = {
        {"player.c", definition, strlen(definition)},
        {"main.c", call_source, strlen(call_source)},
    };
    if (revision == 4) sources[0] = sources[1];
    int ok = ag_language_project_index_update(
        session, project,
        &(ag_language_project_update_request_t){
            .revision = revision,
            .sources = sources,
            .source_count = revision == 4 ? 1 : 2,
            .virtual_header_bundle = bundle.bytes,
            .virtual_header_bundle_length = bundle.length,
            .max_header_files = 32,
            .max_header_file_bytes = 1024 * 1024,
            .max_header_total_bytes = 4 * 1024 * 1024,
            .max_include_depth = 16,
            .limits = limits,
        },
        &error);
    if (ok && revision == (unsigned int)parsed_revision) {
      const char *call = strstr(call_source, "update_player");
      ok = analyze_project_named(
          session, project, "main.c", call_source,
          (size_t)(call - call_source) + 3, bundle, limits,
          &snapshot, &error);
      int length = ok ? ag_language_analysis_snapshot_write_json(
                            &snapshot, NULL, 0) : -1;
      char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
      if (json && ag_language_analysis_snapshot_write_json(
                      &snapshot, json, (size_t)length + 1) == length) {
        puts(json);
        result = 0;
      }
      free(json);
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
    free(bundle.bytes);
    if (!ok) break;
  }
  ag_language_project_index_destroy(project);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_macro_documentation_header_parity_snapshot(
    const char *revision_text) {
  char *end = NULL;
  unsigned long revision = strtoul(revision_text, &end, 10);
  if (!revision_text[0] || !end || *end != '\0' ||
      revision < 1 || revision > 3)
    return 1;
  const char *paths[] = {"macro-doc.h", "empty.h"};
  const char *headers[] = {
      "/** include boundary */\n"
      "#include \"empty.h\"\n"
      "#define INCLUDE_GAP_MACRO 4\n"
      "/// header macro v1\n"
      "#define HEADER_DOC(value) ((value) + 1)\n",
      "/** include boundary */\n"
      "#include \"empty.h\"\n"
      "#define INCLUDE_GAP_MACRO 4\n"
      "/** header macro v2 */\n"
      "#define HEADER_DOC(value) ((value) + 2)\n",
      "/** include boundary */\n"
      "#include \"empty.h\"\n"
      "#define INCLUDE_GAP_MACRO 4\n"
      "#define HEADER_DOC(value) ((value) + 3)\n",
  };
  const char *header_sources[] = {headers[revision - 1], ""};
  header_bundle_t bundle = make_bundle(paths, header_sources, 2);
  const char *source =
      "#include \"macro-doc.h\"\n"
      "int macro_header_main(void) { return HEADER_DOC(INCLUDE_GAP_MACRO); }\n";
  const char *use = strstr(source, "HEADER_DOC");
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = session && bundle.bytes && use && analyze_named(
      session, "macro-header-main.c", source,
      (size_t)(use - source) + 3, bundle,
      ag_language_analysis_default_limits(), &snapshot, &error);
  int length = ok ? ag_language_analysis_snapshot_write_json(
                        &snapshot, NULL, 0) : -1;
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  int result = 1;
  if (json && ag_language_analysis_snapshot_write_json(
                  &snapshot, json, (size_t)length + 1) == length) {
    puts(json);
    result = 0;
  }
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  free(bundle.bytes);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_macro_documentation_project_parity_snapshot(
    const char *revision_text) {
  char *end = NULL;
  unsigned long requested_revision = strtoul(revision_text, &end, 10);
  if (!revision_text[0] || !end || *end != '\0' ||
      requested_revision < 1 || requested_revision > 3)
    return 1;
  const char *sources[] = {
      "/** project macro v1 */\n"
      "#define PROJECT_DOC 10\n"
      "int macro_project_main(void) { return PROJECT_DOC; }\n",
      "/** project macro v2 */\n"
      "#define PROJECT_DOC 20\n"
      "int macro_project_main(void) { return PROJECT_DOC; }\n",
      ("#define PROJECT_DOC 30\n"
       "int macro_project_main(void) { return PROJECT_DOC; }\n"),
  };
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  ag_language_project_index_t *project = ag_language_project_index_create();
  ag_language_analysis_limits_t limits =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int result = 1;
  for (unsigned int revision = 1;
       session && project && revision <= requested_revision; revision++) {
    const char *source = sources[revision - 1];
    ag_language_project_source_t project_source = {
        "macro-project.c", source, strlen(source)};
    int ok = ag_language_project_index_update(
        session, project,
        &(ag_language_project_update_request_t){
            .revision = revision,
            .sources = &project_source,
            .source_count = 1,
            .limits = limits,
        },
        &error);
    if (!ok) break;
    if (revision != requested_revision) continue;
    const char *definition = strstr(source, "PROJECT_DOC");
    const char *use = definition
                          ? strstr(definition + strlen("PROJECT_DOC"),
                                   "PROJECT_DOC")
                          : NULL;
    ok = use && analyze_project_named(
        session, project, "macro-project.c", source,
        (size_t)(use - source) + 3, (header_bundle_t){0}, limits,
        &snapshot, &error);
    int length = ok ? ag_language_analysis_snapshot_write_json(
                          &snapshot, NULL, 0) : -1;
    char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
    if (json && ag_language_analysis_snapshot_write_json(
                    &snapshot, json, (size_t)length + 1) == length) {
      puts(json);
      result = 0;
    }
    free(json);
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  ag_language_project_index_destroy(project);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_macro_definition_source_snapshot(
    const char *source_name, const char *source, size_t cursor,
    header_bundle_t bundle) {
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  if (!session) return 1;
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  int ok = analyze_named(
      session, source_name, source, cursor, bundle,
      ag_language_analysis_default_limits(), &snapshot, &error);
  int length = ok ? ag_language_analysis_snapshot_write_json(
                        &snapshot, NULL, 0) : -1;
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  int result = 1;
  if (json && ag_language_analysis_snapshot_write_json(
                  &snapshot, json, (size_t)length + 1) == length) {
    puts(json);
    result = 0;
  }
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_enum_documentation_header_parity_snapshot(
    const char *revision_text) {
  char *end = NULL;
  unsigned long revision = strtoul(revision_text, &end, 10);
  if (!revision_text[0] || !end || *end != '\0' ||
      revision < 1 || revision > 3)
    return 1;
  const char *paths[] = {"enum-doc.h"};
  const char *headers[] = {
      enum_documentation_header_revisions[revision - 1]};
  header_bundle_t bundle = make_bundle(paths, headers, 1);
  const char *use = last_occurrence(
      enum_documentation_header_main, "HEADER_DIRECTION_VALUE");
  int result = bundle.bytes && use
                   ? print_macro_definition_source_snapshot(
                         "enum-header-main.c",
                         enum_documentation_header_main,
                         (size_t)(use - enum_documentation_header_main) + 3,
                         bundle)
                   : 1;
  free(bundle.bytes);
  return result;
}

static int print_enum_documentation_scope_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(enum_documentation_scope_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "enum-scope.c", enum_documentation_scope_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_enum_documentation_project_parity_snapshot(
    const char *revision_text) {
  char *end = NULL;
  unsigned long requested_revision = strtoul(revision_text, &end, 10);
  if (!revision_text[0] || !end || *end != '\0' ||
      requested_revision < 1 || requested_revision > 3)
    return 1;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  ag_language_project_index_t *project =
      ag_language_project_index_create();
  ag_language_analysis_limits_t limits =
      ag_language_analysis_default_limits();
  ag_language_analysis_error_t error = {0};
  ag_language_analysis_snapshot_t snapshot = {0};
  int result = 1;
  for (unsigned long revision = 1;
       session && project && revision <= requested_revision; revision++) {
    const char *source = enum_documentation_project_revisions[revision - 1];
    if (!update_single_source_project(
            session, project, (unsigned int)revision, source,
            (header_bundle_t){0}, limits, &error))
      break;
    if (revision != requested_revision) continue;
    const char *use = last_occurrence(source, "PROJECT_DIRECTION_VALUE");
    int ok = use && analyze_project_named(
        session, project, "main.c", source,
        (size_t)(use - source) + 3, (header_bundle_t){0}, limits,
        &snapshot, &error);
    int length = ok ? ag_language_analysis_snapshot_write_json(
                          &snapshot, NULL, 0) : -1;
    char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
    if (json && ag_language_analysis_snapshot_write_json(
                    &snapshot, json, (size_t)length + 1) == length) {
      puts(json);
      result = 0;
    }
    free(json);
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  ag_language_project_index_destroy(project);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_macro_definition_forms_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(macro_definition_forms_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "macro-definition.c", macro_definition_forms_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_conditional_logical_line_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(conditional_logical_line_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "conditional-logical-lines.c", conditional_logical_line_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_inline_tag_object_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(inline_tag_object_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "inline-tag-object.c", inline_tag_object_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_for_init_declaration_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(for_init_declaration_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "for-init-hover.c", for_init_declaration_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_prototype_parameter_bound_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(prototype_parameter_bound_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "prototype-parameter-bound.c",
      prototype_parameter_bound_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_block_static_assert_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(block_static_assert_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "block-static-assert.c", block_static_assert_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_do_body_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(do_body_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "do-body-hover.c", do_body_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_offsetof_type_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(offsetof_type_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "offsetof-type-hover.c", offsetof_type_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_initializer_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(initializer_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "initializer-operand-hover.c", initializer_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_direct_aggregate_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(direct_aggregate_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "direct-aggregate-operand-hover.c",
      direct_aggregate_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_simple_remaining_call_argument_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length =
      strlen(simple_remaining_call_argument_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "simple-remaining-call-argument-hover.c",
      simple_remaining_call_argument_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_cast_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(cast_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "cast-operand.c", cast_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_sizeof_expression_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(sizeof_expression_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "sizeof-expression-operand.c", sizeof_expression_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_statement_keyword_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(statement_keyword_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "statement-keyword-operand.c", statement_keyword_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_statement_call_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(statement_call_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "statement-call-operand.c", statement_call_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_case_expression_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(case_expression_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "case-expression-operand.c", case_expression_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_enum_initializer_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(enum_initializer_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "enum-initializer-operand.c", enum_initializer_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_incomplete_enum_initializer_hover_parity_snapshot(
    const char *source_index_text, const char *cursor_text) {
  char *source_end = NULL;
  char *cursor_end = NULL;
  unsigned long long parsed_source_index =
      strtoull(source_index_text, &source_end, 10);
  unsigned long long parsed_cursor = strtoull(cursor_text, &cursor_end, 10);
  size_t source_count = sizeof(incomplete_enum_initializer_sources) /
                        sizeof(incomplete_enum_initializer_sources[0]);
  if (!source_index_text[0] || !source_end || *source_end != '\0' ||
      parsed_source_index >= (unsigned long long)source_count ||
      !cursor_text[0] || !cursor_end || *cursor_end != '\0')
    return 1;
  const char *source =
      incomplete_enum_initializer_sources[(size_t)parsed_source_index];
  size_t source_length = strlen(source);
  if (parsed_cursor > (unsigned long long)source_length) return 1;
  return print_macro_definition_source_snapshot(
      "incomplete-enum-initializer.c", source, (size_t)parsed_cursor,
      (header_bundle_t){0});
}

static int print_incomplete_enum_header_hover_parity_snapshot(
    const char *source_index_text, const char *cursor_text) {
  char *source_end = NULL;
  char *cursor_end = NULL;
  unsigned long long parsed_source_index =
      strtoull(source_index_text, &source_end, 10);
  unsigned long long parsed_cursor = strtoull(cursor_text, &cursor_end, 10);
  size_t source_count = sizeof(incomplete_enum_header_main_sources) /
                        sizeof(incomplete_enum_header_main_sources[0]);
  if (!source_index_text[0] || !source_end || *source_end != '\0' ||
      parsed_source_index >= (unsigned long long)source_count ||
      !cursor_text[0] || !cursor_end || *cursor_end != '\0')
    return 1;
  const char *source =
      incomplete_enum_header_main_sources[(size_t)parsed_source_index];
  size_t source_length = strlen(source);
  if (parsed_cursor > (unsigned long long)source_length) return 1;
  const char *paths[] = {"incomplete-enum.h"};
  const char *headers[] = {incomplete_enum_header_source};
  header_bundle_t bundle = make_bundle(paths, headers, 1);
  int result = bundle.bytes
                   ? print_macro_definition_source_snapshot(
                         "incomplete-enum-header-main.c", source,
                         (size_t)parsed_cursor, bundle)
                   : 1;
  free(bundle.bytes);
  return result;
}

static int print_incomplete_enum_header_revision_parity_snapshot(
    const char *revision_text, const char *source_index_text,
    const char *cursor_text) {
  char *revision_end = NULL;
  char *source_end = NULL;
  char *cursor_end = NULL;
  unsigned long long parsed_revision =
      strtoull(revision_text, &revision_end, 10);
  unsigned long long parsed_source_index =
      strtoull(source_index_text, &source_end, 10);
  unsigned long long parsed_cursor = strtoull(cursor_text, &cursor_end, 10);
  size_t revision_count = sizeof(incomplete_enum_header_revisions) /
                          sizeof(incomplete_enum_header_revisions[0]);
  size_t source_count = sizeof(incomplete_enum_header_main_sources) /
                        sizeof(incomplete_enum_header_main_sources[0]);
  if (!revision_text[0] || !revision_end || *revision_end != '\0' ||
      parsed_revision == 0 ||
      parsed_revision > (unsigned long long)revision_count ||
      !source_index_text[0] || !source_end || *source_end != '\0' ||
      parsed_source_index >= (unsigned long long)source_count ||
      !cursor_text[0] || !cursor_end || *cursor_end != '\0')
    return 1;
  const char *source =
      incomplete_enum_header_main_sources[(size_t)parsed_source_index];
  size_t source_length = strlen(source);
  if (parsed_cursor > (unsigned long long)source_length) return 1;
  const char *paths[] = {"incomplete-enum.h"};
  const char *headers[] = {
      incomplete_enum_header_revisions[(size_t)parsed_revision - 1]};
  header_bundle_t bundle = make_bundle(paths, headers, 1);
  int result = bundle.bytes
                   ? print_macro_definition_source_snapshot(
                         "incomplete-enum-header-main.c", source,
                         (size_t)parsed_cursor, bundle)
                   : 1;
  free(bundle.bytes);
  return result;
}

static int print_project_enum_macro_revision_parity_snapshot(
    const char *revision_text, const char *source_mode_text,
    const char *cursor_text) {
  char *revision_end = NULL;
  char *source_mode_end = NULL;
  char *cursor_end = NULL;
  unsigned long long parsed_revision =
      strtoull(revision_text, &revision_end, 10);
  unsigned long long parsed_source_mode =
      strtoull(source_mode_text, &source_mode_end, 10);
  unsigned long long parsed_cursor =
      strtoull(cursor_text, &cursor_end, 10);
  size_t revision_count = sizeof(project_enum_macro_revisions) /
                          sizeof(project_enum_macro_revisions[0]);
  if (!revision_text[0] || !revision_end || *revision_end != '\0' ||
      parsed_revision == 0 ||
      parsed_revision > (unsigned long long)revision_count ||
      !source_mode_text[0] || !source_mode_end ||
      *source_mode_end != '\0' || parsed_source_mode > 4 ||
      !cursor_text[0] || !cursor_end ||
      *cursor_end != '\0')
    return 1;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  ag_language_project_index_t *project =
      ag_language_project_index_create();
  if (!session || !project) {
    ag_language_project_index_destroy(project);
    ag_compilation_session_destroy(session);
    return 1;
  }
  const char *header_paths[] = {"project-collision.h"};
  ag_language_analysis_limits_t limits =
      ag_language_analysis_default_limits();
  ag_language_analysis_error_t error = {0};
  int result = 1;
  char *owned_source = NULL;
  for (size_t revision = 0; revision < (size_t)parsed_revision; revision++) {
    const project_enum_macro_revision_t *current =
        &project_enum_macro_revisions[revision];
    const char *header_sources[] = {
        project_enum_macro_headers[current->header_index]};
    header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
    int ok = bundle.bytes && update_single_source_project(
        session, project, (unsigned int)revision + 1,
        project_enum_macro_index_sources[current->source_index],
        bundle, limits, &error);
    free(bundle.bytes);
    if (!ok) goto cleanup;
  }
  const project_enum_macro_revision_t *current =
      &project_enum_macro_revisions[(size_t)parsed_revision - 1];
  if (parsed_source_mode && !current->enum_value &&
      !current->macro_replacement)
    goto cleanup;
  const char *source = project_enum_macro_edit_sources[
      current->source_index][parsed_source_mode ? 1 : 0];
  if (parsed_source_mode == 2) {
    owned_source = project_enum_macro_spaced_call_source(
        source, "PROJECT_COLLIDING_SYMBOL");
    if (!owned_source) goto cleanup;
    source = owned_source;
  } else if (parsed_source_mode == 3) {
    owned_source = project_enum_macro_identifier_argument_source(
        source, "PROJECT_COLLIDING_SYMBOL",
        "enum {\n"
        "  /// project call enum argument\n"
        "  PROJECT_CALL_ENUM_ARGUMENT = 1\n"
        "};\n",
        "PROJECT_CALL_ENUM_ARGUMENT");
    if (!owned_source) goto cleanup;
    source = owned_source;
  } else if (parsed_source_mode == 4) {
    owned_source = project_enum_macro_identifier_argument_source(
        source, "PROJECT_COLLIDING_SYMBOL",
        "/// project call macro argument\n"
        "#define PROJECT_CALL_MACRO_ARGUMENT 1\n",
        "PROJECT_CALL_MACRO_ARGUMENT");
    if (!owned_source) goto cleanup;
    source = owned_source;
  }
  if (parsed_cursor > (unsigned long long)strlen(source)) goto cleanup;
  const char *header_sources[] = {
      project_enum_macro_headers[current->header_index]};
  header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
  ag_language_analysis_snapshot_t snapshot = {0};
  int ok = bundle.bytes && analyze_project_named(
      session, project, "main.c", source, (size_t)parsed_cursor,
      bundle, limits, &snapshot, &error);
  if (ok) {
    int length = ag_language_analysis_snapshot_write_json(
        &snapshot, NULL, 0);
    char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
    if (json && ag_language_analysis_snapshot_write_json(
                    &snapshot, json, (size_t)length + 1) == length) {
      puts(json);
      result = 0;
    }
    free(json);
  }
  ag_language_analysis_snapshot_dispose(&snapshot);
  free(bundle.bytes);
cleanup:
  free(owned_source);
  ag_language_project_index_destroy(project);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_enum_two_argument_call_parity_snapshot(
    const char *variant_text, const char *state_text,
    const char *argument_mode_text,
    const char *argument_revision_text,
    const char *cursor_text) {
  char *variant_end = NULL;
  char *state_end = NULL;
  char *argument_mode_end = NULL;
  char *argument_revision_end = NULL;
  char *cursor_end = NULL;
  unsigned long long parsed_variant =
      strtoull(variant_text, &variant_end, 10);
  unsigned long long parsed_state =
      strtoull(state_text, &state_end, 10);
  unsigned long long parsed_argument_mode =
      strtoull(argument_mode_text, &argument_mode_end, 10);
  unsigned long long parsed_argument_revision =
      strtoull(argument_revision_text, &argument_revision_end, 10);
  unsigned long long parsed_cursor =
      strtoull(cursor_text, &cursor_end, 10);
  size_t source_count = sizeof(enum_two_argument_call_sources) /
                        sizeof(enum_two_argument_call_sources[0]);
  if (!variant_text[0] || !variant_end || *variant_end != '\0' ||
      parsed_variant >= (unsigned long long)source_count ||
      !state_text[0] || !state_end || *state_end != '\0' ||
      parsed_state > 1 || !argument_mode_text[0] ||
      !argument_mode_end || *argument_mode_end != '\0' ||
      parsed_argument_mode > 10 || !argument_revision_text[0] ||
      !argument_revision_end || *argument_revision_end != '\0' ||
      parsed_argument_revision > 3 ||
      (parsed_argument_mode == 0 && parsed_argument_revision != 0) ||
      (parsed_argument_mode >= 4 && parsed_argument_mode <= 9 &&
       parsed_argument_revision > 1) ||
      !cursor_text[0] || !cursor_end ||
      *cursor_end != '\0')
    return 1;
  const char *source =
      enum_two_argument_call_sources[(size_t)parsed_variant];
  char *owned_source = NULL;
  if (parsed_argument_mode == 10) {
    owned_source = enum_two_argument_both_renamed_source(
        source, (int)parsed_argument_revision);
    if (!owned_source) return 1;
    source = owned_source;
  } else if (parsed_argument_mode >= 8) {
    owned_source = enum_two_argument_paired_rename_update_source(
        source, (int)parsed_argument_mode - 7,
        (int)parsed_argument_revision);
    if (!owned_source) return 1;
    source = owned_source;
  } else if (parsed_argument_mode >= 6) {
    owned_source = enum_two_argument_paired_update_source(
        source, (int)parsed_argument_mode - 5,
        (int)parsed_argument_revision);
    if (!owned_source) return 1;
    source = owned_source;
  } else if (parsed_argument_mode >= 4) {
    owned_source = enum_two_argument_paired_rename_source(
        source, (int)parsed_argument_mode - 3,
        (int)parsed_argument_revision);
    if (!owned_source) return 1;
    source = owned_source;
  } else if (parsed_argument_mode == 3) {
    owned_source = enum_two_argument_paired_macro_source(
        source, (int)parsed_argument_revision);
    if (!owned_source) return 1;
    source = owned_source;
  } else if (parsed_argument_mode > 0) {
    owned_source = enum_two_argument_macro_source(
        source, (int)parsed_argument_mode,
        (int)parsed_argument_revision);
    if (!owned_source) return 1;
    source = owned_source;
  }
  if (parsed_cursor > (unsigned long long)strlen(source)) {
    free(owned_source);
    return 1;
  }
  const char *paths[] = {"enum-two-argument-call.h"};
  const char *headers[] = {
      parsed_state == 0 ? enum_two_argument_call_header : ""};
  header_bundle_t bundle = make_bundle(paths, headers, 1);
  int result = bundle.bytes
                   ? print_macro_definition_source_snapshot(
                         "enum-two-argument-call.c",
                         source,
                         (size_t)parsed_cursor, bundle)
                   : 1;
  free(bundle.bytes);
  free(owned_source);
  return result;
}

static int print_enum_three_argument_call_parity_snapshot(
    const char *variant_text, const char *missing_argument_mode_text,
    const char *cursor_text, const char *header_revision_text) {
  char *variant_end = NULL;
  char *missing_argument_mode_end = NULL;
  char *cursor_end = NULL;
  char *header_revision_end = NULL;
  unsigned long long parsed_variant =
      strtoull(variant_text, &variant_end, 10);
  unsigned long long parsed_missing_argument_mode =
      strtoull(missing_argument_mode_text,
              &missing_argument_mode_end, 10);
  unsigned long long parsed_cursor =
      strtoull(cursor_text, &cursor_end, 10);
  unsigned long long parsed_header_revision =
      header_revision_text
          ? strtoull(header_revision_text, &header_revision_end, 10)
          : 0;
  size_t source_count = sizeof(enum_three_argument_call_sources) /
                        sizeof(enum_three_argument_call_sources[0]);
  if (!variant_text[0] || !variant_end || *variant_end != '\0' ||
      parsed_variant >= (unsigned long long)source_count ||
      !missing_argument_mode_text[0] ||
      !missing_argument_mode_end || *missing_argument_mode_end != '\0' ||
      parsed_missing_argument_mode > 7 ||
      (parsed_variant >= 2 && parsed_variant < 4 &&
       parsed_missing_argument_mode != 0 &&
       parsed_missing_argument_mode != 1 &&
       parsed_missing_argument_mode != 2 &&
       parsed_missing_argument_mode != 3 &&
       parsed_missing_argument_mode != 4 &&
       parsed_missing_argument_mode != 5 &&
       parsed_missing_argument_mode != 6 &&
       parsed_missing_argument_mode != 7) ||
      (parsed_variant >= 4 && parsed_missing_argument_mode != 0 &&
       parsed_missing_argument_mode != 1 &&
       parsed_missing_argument_mode != 2 &&
       parsed_missing_argument_mode != 3 &&
       parsed_missing_argument_mode != 4 &&
       parsed_missing_argument_mode != 5 &&
       parsed_missing_argument_mode != 6 &&
       parsed_missing_argument_mode != 7) || !cursor_text[0] ||
      !cursor_end || *cursor_end != '\0' ||
      (header_revision_text &&
       (!header_revision_text[0] || !header_revision_end ||
        *header_revision_end != '\0' ||
        parsed_header_revision >=
            sizeof(enum_three_argument_call_headers) /
                sizeof(enum_three_argument_call_headers[0]))))
    return 1;
  char *source = enum_three_argument_macro_source(
      enum_three_argument_call_sources[(size_t)parsed_variant],
      (int)parsed_missing_argument_mode);
  if (!source) return 1;
  if (parsed_cursor > (unsigned long long)strlen(source)) {
    free(source);
    return 1;
  }
  const char *paths[] = {"enum-three-argument-call.h"};
  const char *headers[] = {
      enum_three_argument_call_headers[(size_t)parsed_header_revision]};
  header_bundle_t bundle = make_bundle(paths, headers, 1);
  int result = bundle.bytes
                   ? print_macro_definition_source_snapshot(
                         "enum-three-argument-call.c", source,
                         (size_t)parsed_cursor, bundle)
                   : 1;
  free(bundle.bytes);
  free(source);
  return result;
}

static int print_initializer_designator_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(initializer_designator_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "initializer-designator-operand.c",
      initializer_designator_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_compound_literal_designator_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length =
      strlen(compound_literal_designator_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "compound-literal-designator-operand.c",
      compound_literal_designator_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_type_name_array_bound_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length = strlen(type_name_array_bound_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "type-name-array-bound-operand.c",
      type_name_array_bound_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_declarator_array_bound_operand_hover_parity_snapshot(
    const char *cursor_text) {
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  size_t source_length =
      strlen(declarator_array_bound_operand_hover_source);
  if (!cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length)
    return 1;
  return print_macro_definition_source_snapshot(
      "declarator-array-bound-operand.c",
      declarator_array_bound_operand_hover_source,
      (size_t)parsed_cursor, (header_bundle_t){0});
}

static int print_cast_operand_project_parity_snapshot(
    const char *revision_text) {
  char *end = NULL;
  unsigned long requested_revision = strtoul(revision_text, &end, 10);
  if (!revision_text[0] || !end || *end != '\0' ||
      requested_revision < 1 || requested_revision > 2)
    return 1;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  ag_language_project_index_t *project =
      ag_language_project_index_create();
  ag_language_analysis_limits_t limits =
      ag_language_analysis_default_limits();
  ag_language_analysis_error_t error = {0};
  ag_language_analysis_snapshot_t snapshot = {0};
  int result = 1;
  for (unsigned long revision = 1;
       session && project && revision <= requested_revision; revision++) {
    const char *source = cast_operand_project_sources[revision - 1];
    if (!update_single_source_project(
            session, project, (unsigned int)revision, source,
            (header_bundle_t){0}, limits, &error))
      goto done;
  }
  const char *source =
      cast_operand_project_sources[requested_revision - 1];
  const char *fragment = strstr(source, ")PROJECT_CAST_VALUE");
  const char *use = fragment
                        ? strstr(fragment, "PROJECT_CAST_VALUE")
                        : NULL;
  int ok = use && analyze_project_named(
      session, project, "main.c", source,
      (size_t)(use - source) + 4, (header_bundle_t){0}, limits,
      &snapshot, &error);
  int length = ok ? ag_language_analysis_snapshot_write_json(
                        &snapshot, NULL, 0) : -1;
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  if (json && ag_language_analysis_snapshot_write_json(
                  &snapshot, json, (size_t)length + 1) == length) {
    puts(json);
    result = 0;
  }
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
done:
  ag_language_project_index_destroy(project);
  ag_compilation_session_destroy(session);
  return result;
}

static int print_macro_definition_snake_parity_snapshot(
    const char *cursor_text) {
  size_t source_length = 0;
  char *source = read_fixture_source(
      "test/fixtures/language_analysis/macro_definition_snake.txt",
      &source_length);
  char *end = NULL;
  unsigned long long parsed_cursor = strtoull(cursor_text, &end, 10);
  if (!source || !cursor_text[0] || !end || *end != '\0' ||
      parsed_cursor > (unsigned long long)source_length) {
    free(source);
    return 1;
  }
  const char *paths[] = {"game.h"};
  const char *headers[] = {macro_definition_game_header};
  header_bundle_t bundle = make_bundle(paths, headers, 1);
  int result = print_macro_definition_source_snapshot(
      "snake.c", source, (size_t)parsed_cursor, bundle);
  free(bundle.bytes);
  free(source);
  return result;
}

static int print_macro_definition_header_parity_snapshot(void) {
  const char *definition = strstr(
      macro_definition_header_source, "HEADER_DEFINITION");
  if (!definition) return 1;
  return print_macro_definition_source_snapshot(
      "macro-definition.h", macro_definition_header_source,
      (size_t)(definition - macro_definition_header_source) + 3,
      (header_bundle_t){0});
}

static int print_macro_definition_project_parity_snapshot(
    const char *revision_text) {
  char *end = NULL;
  unsigned long parsed_revision = strtoul(revision_text, &end, 10);
  if (!revision_text[0] || !end || *end != '\0' ||
      parsed_revision < 1 || parsed_revision > 2)
    return 1;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  ag_language_project_index_t *project =
      ag_language_project_index_create();
  if (!session || !project) {
    ag_language_project_index_destroy(project);
    ag_compilation_session_destroy(session);
    return 1;
  }
  ag_language_analysis_limits_t limits =
      ag_language_analysis_default_limits();
  ag_language_analysis_error_t error = {0};
  int result = 1;
  for (unsigned long revision = 1;
       revision <= parsed_revision; revision++) {
    if (!update_single_source_project(
            session, project, (unsigned int)revision,
            macro_definition_project_sources[revision - 1],
            (header_bundle_t){0}, limits, &error))
      goto done;
  }
  const char *source =
      macro_definition_project_sources[parsed_revision - 1];
  const char *definition = strstr(source, "PROJECT_DEFINITION");
  ag_language_analysis_snapshot_t snapshot = {0};
  int ok = definition && analyze_project_named(
      session, project, "main.c", source,
      (size_t)(definition - source) + 4, (header_bundle_t){0}, limits,
      &snapshot, &error);
  int length = ok ? ag_language_analysis_snapshot_write_json(
                        &snapshot, NULL, 0) : -1;
  char *json = length >= 0 ? malloc((size_t)length + 1) : NULL;
  if (json && ag_language_analysis_snapshot_write_json(
                  &snapshot, json, (size_t)length + 1) == length) {
    puts(json);
    result = 0;
  }
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);
done:
  ag_language_project_index_destroy(project);
  ag_compilation_session_destroy(session);
  return result;
}

static const char *last_occurrence(const char *text, const char *needle) {
  const char *result = NULL;
  const char *cursor = text;
  while ((cursor = strstr(cursor, needle)) != NULL) {
    result = cursor;
    cursor++;
  }
  return result;
}

static char *project_enum_macro_spaced_call_source(
    const char *source, const char *name) {
  static const char spaced_call_tail[] =
      " /* project call gap */ ( /* project argument before */ 1"
      " /* project argument after */ )";
  if (!source || !name) return NULL;
  const char *use = last_occurrence(source, name);
  size_t name_length = strlen(name);
  if (!use || strcmp(use + name_length, "(1)") != 0) return NULL;
  size_t prefix_length = (size_t)(use - source) + name_length;
  size_t tail_length = sizeof(spaced_call_tail) - 1;
  if (prefix_length > SIZE_MAX - tail_length - 1) return NULL;
  char *result = malloc(prefix_length + tail_length + 1);
  if (!result) return NULL;
  memcpy(result, source, prefix_length);
  memcpy(result + prefix_length, spaced_call_tail, tail_length + 1);
  return result;
}

static char *project_enum_macro_identifier_argument_source(
    const char *source, const char *name, const char *declaration,
    const char *argument) {
  static const char derived_prefix[] =
      "enum { PROJECT_COLLISION_DERIVED";
  if (!source || !name || !declaration || !argument) return NULL;
  const char *use = last_occurrence(source, name);
  const char *insertion = last_occurrence(source, derived_prefix);
  size_t name_length = strlen(name);
  if (!use || !insertion || insertion >= use ||
      strcmp(use + name_length, "(1)") != 0)
    return NULL;
  size_t prefix_length = (size_t)(insertion - source);
  size_t middle_length = (size_t)(use - insertion) + name_length;
  size_t declaration_length = strlen(declaration);
  size_t argument_length = strlen(argument);
  if (declaration_length > SIZE_MAX - prefix_length ||
      middle_length > SIZE_MAX - prefix_length - declaration_length)
    return NULL;
  size_t fixed_length = prefix_length + declaration_length + middle_length;
  if (fixed_length > SIZE_MAX - 6 ||
      argument_length > SIZE_MAX - fixed_length - 6)
    return NULL;
  size_t result_length = fixed_length + argument_length + 6;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  size_t output = 0;
  memcpy(result + output, source, prefix_length);
  output += prefix_length;
  memcpy(result + output, declaration, declaration_length);
  output += declaration_length;
  memcpy(result + output, insertion, middle_length);
  output += middle_length;
  result[output++] = '(';
  result[output++] = ' ';
  result[output++] = ' ';
  memcpy(result + output, argument, argument_length);
  output += argument_length;
  result[output++] = ' ';
  result[output++] = ' ';
  result[output++] = ')';
  result[output] = '\0';
  return result;
}

static char *enum_two_argument_macro_source(
    const char *source, int argument_index, int revision) {
  static const char *const enum_names[] = {
      "ENUM_TWO_ARGUMENT_FIRST", "ENUM_TWO_ARGUMENT_SECOND"};
  static const char *const macro_names[][2] = {
      {"ENUM_TWO_ARGUMENT_FIRST_MACRO",
       "ENUM_TWO_ARGUMENT_SECOND_MACRO"},
      {"ENUM_TWO_ARGUMENT_FIRST_MACRO",
       "ENUM_TWO_ARGUMENT_SECOND_MACRO"},
      {"ENUM_TWO_ARGUMENT_FIRST_RENAMED_MACRO",
       "ENUM_TWO_ARGUMENT_SECOND_RENAMED_MACRO"},
      {"ENUM_TWO_ARGUMENT_FIRST_MACRO",
       "ENUM_TWO_ARGUMENT_SECOND_MACRO"}};
  static const char *const declarations[][4] = {
      {"/// enum two argument first macro\n"
       "#define ENUM_TWO_ARGUMENT_FIRST_MACRO 1\n",
       "/// enum two argument first macro updated\n"
       "#define ENUM_TWO_ARGUMENT_FIRST_MACRO 11\n",
       "/// enum two argument first renamed macro\n"
       "#define ENUM_TWO_ARGUMENT_FIRST_RENAMED_MACRO 1\n",
       ""},
      {"/// enum two argument second macro\n"
       "#define ENUM_TWO_ARGUMENT_SECOND_MACRO 2\n",
       "/// enum two argument second macro updated\n"
       "#define ENUM_TWO_ARGUMENT_SECOND_MACRO 12\n",
       "/// enum two argument second renamed macro\n"
       "#define ENUM_TWO_ARGUMENT_SECOND_RENAMED_MACRO 2\n",
       ""}};
  if (!source || argument_index < 1 || argument_index > 2 ||
      revision < 0 || revision > 3)
    return NULL;
  size_t index = (size_t)argument_index - 1;
  const char *insertion = strchr(source, '\n');
  const char *use = last_occurrence(source, enum_names[index]);
  if (!insertion || !use || use <= insertion) return NULL;
  insertion++;
  size_t prefix_length = (size_t)(insertion - source);
  const char *declaration = declarations[index][revision];
  size_t declaration_length = strlen(declaration);
  size_t middle_length = (size_t)(use - insertion);
  const char *macro_name = macro_names[revision][index];
  size_t macro_length = strlen(macro_name);
  size_t enum_length = strlen(enum_names[index]);
  size_t suffix_length = strlen(use + enum_length);
  if (declaration_length > SIZE_MAX - prefix_length ||
      middle_length > SIZE_MAX - prefix_length - declaration_length)
    return NULL;
  size_t fixed_length = prefix_length + declaration_length + middle_length;
  if (macro_length > SIZE_MAX - fixed_length ||
      suffix_length > SIZE_MAX - fixed_length - macro_length)
    return NULL;
  size_t result_length = fixed_length + macro_length + suffix_length;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  size_t output = 0;
  memcpy(result + output, source, prefix_length);
  output += prefix_length;
  memcpy(result + output, declaration, declaration_length);
  output += declaration_length;
  memcpy(result + output, insertion, middle_length);
  output += middle_length;
  memcpy(result + output, macro_name, macro_length);
  output += macro_length;
  memcpy(result + output, use + enum_length, suffix_length + 1);
  return result;
}

static char *enum_two_argument_paired_macro_source(
    const char *source, int missing_argument_mode) {
  if (!source || missing_argument_mode < 0 ||
      missing_argument_mode > 3)
    return NULL;
  char *first = enum_two_argument_macro_source(
      source, 1, (missing_argument_mode & 1) ? 3 : 0);
  if (!first) return NULL;
  char *second = enum_two_argument_macro_source(
      first, 2, (missing_argument_mode & 2) ? 3 : 0);
  free(first);
  return second;
}

static char *enum_two_argument_paired_rename_source(
    const char *source, int renamed_argument_index,
    int other_argument_missing) {
  if (!source || renamed_argument_index < 1 ||
      renamed_argument_index > 2 || other_argument_missing < 0 ||
      other_argument_missing > 1)
    return NULL;
  int first_revision = renamed_argument_index == 1
                           ? 2
                           : other_argument_missing ? 3 : 0;
  int second_revision = renamed_argument_index == 2
                            ? 2
                            : other_argument_missing ? 3 : 0;
  char *first = enum_two_argument_macro_source(
      source, 1, first_revision);
  if (!first) return NULL;
  char *second = enum_two_argument_macro_source(
      first, 2, second_revision);
  free(first);
  return second;
}

static char *enum_two_argument_paired_update_source(
    const char *source, int updated_argument_index,
    int other_argument_missing) {
  if (!source || updated_argument_index < 1 ||
      updated_argument_index > 2 || other_argument_missing < 0 ||
      other_argument_missing > 1)
    return NULL;
  int first_revision = updated_argument_index == 1
                           ? 1
                           : other_argument_missing ? 3 : 0;
  int second_revision = updated_argument_index == 2
                            ? 1
                            : other_argument_missing ? 3 : 0;
  char *first = enum_two_argument_macro_source(
      source, 1, first_revision);
  if (!first) return NULL;
  char *second = enum_two_argument_macro_source(
      first, 2, second_revision);
  free(first);
  return second;
}

static char *enum_two_argument_paired_rename_update_source(
    const char *source, int renamed_argument_index,
    int updated_argument_missing) {
  if (!source || renamed_argument_index < 1 ||
      renamed_argument_index > 2 || updated_argument_missing < 0 ||
      updated_argument_missing > 1)
    return NULL;
  int first_revision = renamed_argument_index == 1
                           ? 2
                           : updated_argument_missing ? 3 : 1;
  int second_revision = renamed_argument_index == 2
                            ? 2
                            : updated_argument_missing ? 3 : 1;
  char *first = enum_two_argument_macro_source(
      source, 1, first_revision);
  if (!first) return NULL;
  char *second = enum_two_argument_macro_source(
      first, 2, second_revision);
  free(first);
  return second;
}

static char *enum_two_argument_both_renamed_source(
    const char *source, int missing_argument_mode) {
  static const char *const renamed_declarations[] = {
      "/// enum two argument first renamed macro\n"
      "#define ENUM_TWO_ARGUMENT_FIRST_RENAMED_MACRO 1\n",
      "/// enum two argument second renamed macro\n"
      "#define ENUM_TWO_ARGUMENT_SECOND_RENAMED_MACRO 2\n"};
  if (!source || missing_argument_mode < 0 ||
      missing_argument_mode > 3)
    return NULL;
  char *first = enum_two_argument_macro_source(
      source, 1, 2);
  if (!first) return NULL;
  char *renamed = enum_two_argument_macro_source(first, 2, 2);
  free(first);
  if (!renamed) return NULL;
  for (int argument_index = 0; argument_index < 2; argument_index++) {
    if (!(missing_argument_mode & (1 << argument_index))) continue;
    const char *declaration = renamed_declarations[argument_index];
    char *position = strstr(renamed, declaration);
    if (!position) {
      free(renamed);
      return NULL;
    }
    size_t declaration_length = strlen(declaration);
    memmove(position, position + declaration_length,
            strlen(position + declaration_length) + 1);
  }
  return renamed;
}

static char *enum_three_argument_macro_source(
    const char *source, int missing_argument_mode) {
  static const char mixed_outer_enum_declaration[] =
      "enum EnumThreeArgumentMixedValues {\n"
      "  /// enum three argument first enum\n"
      "  ENUM_THREE_ARGUMENT_FIRST_ENUM = 4,\n"
      "  /// enum three argument last enum\n"
      "  ENUM_THREE_ARGUMENT_LAST_ENUM = 5\n"
      "};\n";
  static const char *const declarations[][4] = {
      {"/// enum three argument first macro\n"
       "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 1\n",
       "/// enum three argument first macro\n"
       "#define ENUM_THREE_ARGUMENT_FIRST_MACRO 7\n",
       "  /// enum three argument first enum\n"
       "  ENUM_THREE_ARGUMENT_FIRST_ENUM = 4,\n",
       NULL},
      {"/// enum three argument middle macro\n"
       "#define ENUM_THREE_ARGUMENT_MIDDLE_MACRO 2\n",
       "enum EnumThreeArgumentMiddleValue {\n"
       "  /// enum three argument middle enum\n"
       "  ENUM_THREE_ARGUMENT_MIDDLE_ENUM = 6\n"
       "};\n",
       "enum EnumThreeArgumentMiddleValue {\n"
       "  /// enum three argument middle enum\n"
       "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 6\n"
       "};\n",
       "enum EnumThreeArgumentMiddleValue {\n"
       "  /// enum three argument updated middle enum\n"
       "  ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM = 9\n"
       "};\n"},
      {"/// enum three argument last macro\n"
       "#define ENUM_THREE_ARGUMENT_LAST_MACRO 3\n",
       "/// enum three argument last macro\n"
       "#define ENUM_THREE_ARGUMENT_LAST_MACRO 11\n",
       "  /// enum three argument last enum\n"
       "  ENUM_THREE_ARGUMENT_LAST_ENUM = 5\n",
       NULL}};
  if (!source || missing_argument_mode < 0 ||
      missing_argument_mode > 7)
    return NULL;
  size_t source_length = strlen(source);
  char *result = malloc(source_length + 1);
  if (!result) return NULL;
  memcpy(result, source, source_length + 1);
  int pending_missing_argument_mode = missing_argument_mode;
  if ((pending_missing_argument_mode & 5) == 5) {
    char *position = strstr(result, mixed_outer_enum_declaration);
    if (position) {
      size_t declaration_length = strlen(mixed_outer_enum_declaration);
      memmove(position, position + declaration_length,
              strlen(position + declaration_length) + 1);
      pending_missing_argument_mode &= ~5;
    }
  }
  for (int argument_index = 0; argument_index < 3; argument_index++) {
    if (!(pending_missing_argument_mode & (1 << argument_index))) continue;
    const char *declaration = NULL;
    char *position = NULL;
    for (int revision = 0; revision < 4 && !position; revision++) {
      declaration = declarations[argument_index][revision];
      if (declaration) position = strstr(result, declaration);
    }
    if (!position) {
      free(result);
      return NULL;
    }
    size_t declaration_length = strlen(declaration);
    memmove(position, position + declaration_length,
            strlen(position + declaration_length) + 1);
  }
  return result;
}

static int check_documentation_symbol(
    const ag_language_symbol_t *symbol, const char *expected,
    const char *source_name, size_t comment_start, size_t comment_end) {
  if (!symbol || !symbol->documentation ||
      strcmp(symbol->documentation, expected) != 0)
    return 0;
  if (!expected[0]) return !symbol->has_documentation_range;
  return symbol->has_documentation_range &&
         strcmp(symbol->documentation_range.source_name, source_name) == 0 &&
         symbol->documentation_range.start.offset == (int)comment_start &&
         symbol->documentation_range.end.offset == (int)comment_end;
}

static int macro_snapshot_fields_match(
    const ag_language_analysis_snapshot_t *snapshot, const char *name,
    const char *replacement, int parameter_count) {
  const ag_language_symbol_t *hover = hover_symbol(snapshot);
  const ag_language_symbol_t *completion = find_symbol(
      snapshot, name, AG_LANGUAGE_SYMBOL_MACRO);
  return hover && completion &&
         hover->kind == AG_LANGUAGE_SYMBOL_MACRO &&
         strcmp(hover->name, name) == 0 &&
         hover->macro_replacement && completion->macro_replacement &&
         strcmp(hover->macro_replacement, replacement) == 0 &&
         strcmp(completion->macro_replacement, replacement) == 0 &&
         hover->macro_parameter_count == parameter_count &&
         completion->macro_parameter_count == parameter_count &&
         same_range(&hover->declaration, &completion->declaration);
}

static int macro_definition_snapshot_matches(
    const ag_language_analysis_snapshot_t *snapshot, const char *name,
    const char *replacement, int parameter_count) {
  return macro_snapshot_fields_match(
             snapshot, name, replacement, parameter_count) &&
         !snapshot->partial && snapshot->diagnostic_count == 0;
}

static int test_cast_operand_hover(ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "cast operand session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
  } cases[] = {
      {"simple = (int)CAST_OPERAND_MACRO", "CAST_OPERAND_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO},
      {"nested = (int)((unsigned long)cast_object", "cast_object",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"binary_rhs = cast_seed % (unsigned int)cast_object", "cast_object",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"argument = cast_choose((const int)parameter_value", "parameter_value",
       AG_LANGUAGE_SYMBOL_PARAMETER},
      {"conditional = condition ? (int)CAST_OPERAND_MACRO",
       "CAST_OPERAND_MACRO", AG_LANGUAGE_SYMBOL_MACRO},
      {"subscript = values[(unsigned int)index_value", "index_value",
       AG_LANGUAGE_SYMBOL_PARAMETER},
      {"typedef_name = (CastSize)cast_object", "cast_object",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"pointer = (const volatile int *)values", "values",
       AG_LANGUAGE_SYMBOL_PARAMETER},
      {"tag_pointer = (struct CastRecord *)values", "values",
       AG_LANGUAGE_SYMBOL_PARAMETER},
      {"enum_cast = (enum CastMode)CAST_MODE_VALUE", "CAST_MODE_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT},
      {"comment_gap = (int) /* operand gap */ CAST_OPERAND_MACRO",
       "CAST_OPERAND_MACRO", AG_LANGUAGE_SYMBOL_MACRO},
      {"splice_lf = (unsigned int) \\\ncast_object", "cast_object",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"splice_crlf = (unsigned int) \\\r\ncast_object", "cast_object",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"nested_cast = (int)((unsigned long)CAST_OPERAND_MACRO",
       "CAST_OPERAND_MACRO", AG_LANGUAGE_SYMBOL_MACRO},
      {"adjacent_builtin = (int)(long)cast_object", "cast_object",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"adjacent_typedef = (CastSize)(long)cast_object", "cast_object",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"adjacent_typedef_operand = (int)(CastSize)cast_object", "cast_object",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"adjacent_comment = (int) /* adjacent cast */ "
       "(long)CAST_OPERAND_MACRO",
       "CAST_OPERAND_MACRO", AG_LANGUAGE_SYMBOL_MACRO},
      {"adjacent_splice_lf = (int) \\\n(long)cast_object", "cast_object",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"adjacent_splice_crlf = (int) \\\r\n(long)CAST_MODE_VALUE",
       "CAST_MODE_VALUE", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT},
      {"(void)(int[CAST_MODE_VALUE]){ 1 };", "CAST_MODE_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT},
      {"(void)(int[CAST_MODE_VALUE]){ 1 }[0]", "CAST_MODE_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT},
      {"(void)(int (*)[CAST_MODE_VALUE])0", "CAST_MODE_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT},
  };
  const char *macro_declaration =
      strstr(cast_operand_hover_source, "CAST_OPERAND_MACRO");
  const char *macro_comment = strstr(
      cast_operand_hover_source, "/// cast operand macro documentation");
  CHECK(macro_declaration && macro_comment, "cast operand macro anchors");
  for (size_t case_index = 0;
       case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
    const char *fragment = strstr(
        cast_operand_hover_source, cases[case_index].fragment);
    const char *use = fragment
                          ? strstr(fragment, cases[case_index].name)
                          : NULL;
    CHECK(use != NULL, "cast operand case anchor");
    size_t name_length = strlen(cases[case_index].name);
    size_t deltas[] = {0, name_length / 2, name_length};
    for (size_t delta_index = 0;
         delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
      CHECK(analyze_named(
                session, "cast-operand.c", cast_operand_hover_source,
                (size_t)(use - cast_operand_hover_source) +
                    deltas[delta_index],
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "cast operand analysis");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      const ag_language_symbol_t *completion = find_symbol(
          &snapshot, cases[case_index].name, cases[case_index].kind);
      CHECK(hover && completion && !snapshot.partial &&
                snapshot.diagnostic_count == 0 &&
                hover->kind == cases[case_index].kind &&
                strcmp(hover->name, cases[case_index].name) == 0 &&
                same_range(&hover->declaration, &completion->declaration),
            "cast operand symbol fields");
      if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO) {
        CHECK(hover->macro_replacement &&
                  strcmp(hover->macro_replacement, "17") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(macro_declaration - cast_operand_hover_source) &&
                  check_documentation_symbol(
                      hover, "cast operand macro documentation",
                      "cast-operand.c",
                      (size_t)(macro_comment - cast_operand_hover_source),
                      (size_t)(macro_comment - cast_operand_hover_source) +
                          strlen("/// cast operand macro documentation")),
              "cast operand macro fields");
      }
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
  } non_cast_cases[] = {
      {"normal_call = cast_choose(parameter_value) + cast_object",
       "cast_object", AG_LANGUAGE_SYMBOL_OBJECT},
      {"parenthesized_call = (cast_choose)(parameter_value)",
       "parameter_value", AG_LANGUAGE_SYMBOL_PARAMETER},
      {"parenthesized_pointer_call = (cast_pointer)(parameter_value)",
       "parameter_value", AG_LANGUAGE_SYMBOL_PARAMETER},
      {"dereferenced_pointer_call = (*cast_pointer)(parameter_value)",
       "parameter_value", AG_LANGUAGE_SYMBOL_PARAMETER},
      {"addressed_call = (&cast_choose)(parameter_value)",
       "parameter_value", AG_LANGUAGE_SYMBOL_PARAMETER},
      {"grouped = (cast_object + cast_seed) + CAST_OPERAND_MACRO",
       "CAST_OPERAND_MACRO", AG_LANGUAGE_SYMBOL_MACRO},
      {"type_size = (int)sizeof(unsigned int) + CAST_OPERAND_MACRO",
       "CAST_OPERAND_MACRO", AG_LANGUAGE_SYMBOL_MACRO},
      {"type_align = (int)_Alignof(unsigned int) + CAST_OPERAND_MACRO",
       "CAST_OPERAND_MACRO", AG_LANGUAGE_SYMBOL_MACRO},
      {"compound = ((struct CastRecord){ 1 }).value + CAST_OPERAND_MACRO",
       "CAST_OPERAND_MACRO", AG_LANGUAGE_SYMBOL_MACRO},
  };
  for (size_t i = 0;
       i < sizeof(non_cast_cases) / sizeof(non_cast_cases[0]); i++) {
    const char *fragment = strstr(
        cast_operand_hover_source, non_cast_cases[i].fragment);
    const char *use = fragment
                          ? strstr(fragment, non_cast_cases[i].name)
                          : NULL;
    CHECK(use && analyze_named(
              session, "cast-operand.c", cast_operand_hover_source,
              (size_t)(use - cast_operand_hover_source) +
                  strlen(non_cast_cases[i].name) / 2,
              (header_bundle_t){0}, defaults, &snapshot, &error),
          "non-cast parenthesized context analysis");
    const ag_language_symbol_t *hover = hover_symbol(&snapshot);
    const ag_language_symbol_t *completion = find_symbol(
        &snapshot, non_cast_cases[i].name, non_cast_cases[i].kind);
    CHECK(hover && completion && hover->kind == non_cast_cases[i].kind &&
              strcmp(hover->name, non_cast_cases[i].name) == 0 &&
              same_range(&hover->declaration, &completion->declaration) &&
              !snapshot.partial && snapshot.diagnostic_count == 0,
          "non-cast parenthesized context fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }

  size_t snake_length = 0;
  char *snake_source = read_fixture_source(
      "test/fixtures/language_analysis/macro_definition_snake.txt",
      &snake_length);
  const char *game_paths[] = {"game.h"};
  const char *game_headers[] = {macro_definition_game_header};
  header_bundle_t game_bundle = make_bundle(game_paths, game_headers, 1);
  const char *cast_fragment = snake_source
                                  ? strstr(snake_source,
                                           "(unsigned int)MAX_SNAKE_LENGTH")
                                  : NULL;
  const char *cast_use = cast_fragment
                             ? strstr(cast_fragment, "MAX_SNAKE_LENGTH")
                             : NULL;
  const char *ordinary_use = snake_source
                                 ? last_occurrence(
                                       snake_source, "MAX_SNAKE_LENGTH")
                                 : NULL;
  CHECK(snake_source && snake_length == strlen(snake_source) &&
            game_bundle.bytes && cast_use && ordinary_use &&
            cast_use != ordinary_use,
        "snake cast operand anchors");
  size_t snake_deltas[] = {0, strlen("MAX_SNAKE_LENGTH") / 2,
                           strlen("MAX_SNAKE_LENGTH")};
  for (size_t i = 0;
       i < sizeof(snake_deltas) / sizeof(snake_deltas[0]); i++) {
    CHECK(analyze_named(
              session, "snake.c", snake_source,
              (size_t)(cast_use - snake_source) + snake_deltas[i],
              game_bundle, defaults, &snapshot, &error),
          "snake cast operand analysis");
    CHECK(macro_definition_snapshot_matches(
              &snapshot, "MAX_SNAKE_LENGTH",
              "( BOARD_COLUMNS * BOARD_ROWS )", 0) &&
              strcmp(hover_symbol(&snapshot)->documentation,
                     "盤面に収まるヘビの最大の長さです。") == 0,
          "snake cast operand fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  CHECK(analyze_named(
            session, "snake.c", snake_source,
            (size_t)(ordinary_use - snake_source) +
                strlen("MAX_SNAKE_LENGTH") / 2,
            game_bundle, defaults, &snapshot, &error),
        "snake ordinary macro use analysis");
  const ag_language_symbol_t *ordinary_hover = hover_symbol(&snapshot);
  CHECK(macro_definition_snapshot_matches(
            &snapshot, "MAX_SNAKE_LENGTH",
            "( BOARD_COLUMNS * BOARD_ROWS )", 0) &&
            ordinary_hover->declaration.start.offset ==
                (int)(strstr(snake_source, "MAX_SNAKE_LENGTH") -
                      snake_source),
        "snake cast and ordinary use resolve equally");
  ag_language_analysis_snapshot_dispose(&snapshot);

  ag_compilation_session_t *fresh =
      ag_compilation_session_create(&target);
  CHECK(fresh && analyze_named(
            fresh, "snake.c", snake_source,
            (size_t)(cast_use - snake_source) +
                strlen("MAX_SNAKE_LENGTH") / 2,
            game_bundle, defaults, &snapshot, &error),
        "fresh snake cast operand analysis");
  CHECK(macro_definition_snapshot_matches(
            &snapshot, "MAX_SNAKE_LENGTH",
            "( BOARD_COLUMNS * BOARD_ROWS )", 0),
        "fresh snake cast operand fields");
  ag_language_analysis_snapshot_dispose(&snapshot);
  const char *fresh_adjacent_fragment = strstr(
      cast_operand_hover_source, "adjacent_builtin = (int)(long)cast_object");
  const char *fresh_adjacent_use = fresh_adjacent_fragment
                                       ? strstr(fresh_adjacent_fragment,
                                                "cast_object")
                                       : NULL;
  CHECK(fresh_adjacent_use && analyze_named(
            fresh, "cast-operand.c", cast_operand_hover_source,
            (size_t)(fresh_adjacent_use - cast_operand_hover_source) + 5,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "fresh adjacent cast operand analysis");
  const ag_language_symbol_t *fresh_adjacent_hover = hover_symbol(&snapshot);
  const ag_language_symbol_t *fresh_adjacent_completion = find_symbol(
      &snapshot, "cast_object", AG_LANGUAGE_SYMBOL_OBJECT);
  CHECK(fresh_adjacent_hover && fresh_adjacent_completion &&
            fresh_adjacent_hover->kind == AG_LANGUAGE_SYMBOL_OBJECT &&
            strcmp(fresh_adjacent_hover->name, "cast_object") == 0 &&
            same_range(&fresh_adjacent_hover->declaration,
                       &fresh_adjacent_completion->declaration) &&
            !snapshot.partial && snapshot.diagnostic_count == 0,
        "fresh adjacent cast operand fields");
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(fresh);
  free(game_bundle.bytes);
  free(snake_source);

  const char *invalid_sources[] = {
      "int target; int f(void) { return (unsigned mystery)target; }\n",
      "int f(void) { return (unsigned int); }\n",
      "int target; int f(void) { return (struct)target; }\n",
  };
  for (size_t i = 0;
       i < sizeof(invalid_sources) / sizeof(invalid_sources[0]); i++) {
    const char *cursor_name = strstr(invalid_sources[i], "target");
    size_t cursor = cursor_name
                        ? (size_t)(last_occurrence(
                              invalid_sources[i], "target") -
                          invalid_sources[i]) + 3
                        : strlen(invalid_sources[i]);
    int ok = analyze_named(
        session, "invalid-cast.c", invalid_sources[i], cursor,
        (header_bundle_t){0}, defaults, &snapshot, &error);
    CHECK((ok && snapshot.partial && snapshot.diagnostic_count > 0) ||
              (!ok && error.status == AG_LANGUAGE_ANALYSIS_FAILED),
          "invalid cast diagnostic preserved");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }

  ag_language_project_index_t *project =
      ag_language_project_index_create();
  CHECK(project != NULL, "cast operand project");
  for (size_t revision = 0; revision < 2; revision++) {
    CHECK(update_single_source_project(
              session, project, (unsigned int)revision + 1,
              cast_operand_project_sources[revision],
              (header_bundle_t){0}, defaults,
              &error),
          "cast operand project update");
    const char *fragment = strstr(
        cast_operand_project_sources[revision], ")PROJECT_CAST_VALUE");
    const char *use = fragment
                          ? strstr(fragment, "PROJECT_CAST_VALUE")
                          : NULL;
    CHECK(use && analyze_project_named(
              session, project, "main.c",
              cast_operand_project_sources[revision],
              (size_t)(use - cast_operand_project_sources[revision]) + 4,
              (header_bundle_t){0}, defaults, &snapshot, &error),
          "cast operand project analysis");
    char expected[3] = {'3', (char)('1' + revision), '\0'};
    CHECK(macro_definition_snapshot_matches(
              &snapshot, "PROJECT_CAST_VALUE", expected, 0) &&
              strstr(hover_symbol(&snapshot)->documentation,
                     revision == 0 ? "v1" : "v2"),
          "cast operand project revision fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  ag_language_project_index_destroy(project);

  const char *limit_use = strstr(
      cast_operand_hover_source, "simple = (int)CAST_OPERAND_MACRO");
  limit_use = limit_use
                  ? strstr(limit_use, "CAST_OPERAND_MACRO")
                  : NULL;
  ag_language_analysis_limits_t tiny = defaults;
  tiny.max_source_bytes = strlen(cast_operand_hover_source);
  CHECK(limit_use && analyze_named(
            session, "cast-operand.c", cast_operand_hover_source,
            (size_t)(limit_use - cast_operand_hover_source) + 3,
            (header_bundle_t){0}, tiny, &snapshot, &error),
        "cast operand exact source limit");
  size_t snapshot_bytes = snapshot.allocated_bytes;
  ag_language_analysis_snapshot_dispose(&snapshot);
  tiny.max_source_bytes = strlen(cast_operand_hover_source) - 1;
  CHECK(!analyze_named(
            session, "cast-operand.c", cast_operand_hover_source,
            (size_t)(limit_use - cast_operand_hover_source) + 3,
            (header_bundle_t){0}, tiny, &snapshot, &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code, "AGC_LIMIT_MAX_SOURCE_BYTES") == 0,
        "cast operand source limit rejection");
  tiny = defaults;
  tiny.max_snapshot_bytes = snapshot_bytes - 1;
  CHECK(!analyze_named(
            session, "cast-operand.c", cast_operand_hover_source,
            (size_t)(limit_use - cast_operand_hover_source) + 3,
            (header_bundle_t){0}, tiny, &snapshot, &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code,
                   "AGC_LIMIT_MAX_ANALYSIS_SNAPSHOT_BYTES") == 0,
        "cast operand snapshot limit rejection");
  CHECK(analyze_named(
            session, "cast-operand.c", cast_operand_hover_source,
            (size_t)(limit_use - cast_operand_hover_source) + 3,
            (header_bundle_t){0}, defaults, &snapshot, &error) &&
            macro_definition_snapshot_matches(
                &snapshot, "CAST_OPERAND_MACRO", "17", 0),
        "cast operand session reusable after limits");
  ag_language_analysis_snapshot_dispose(&snapshot);

  ag_compilation_session_destroy(session);
  return 0;
}

static int test_sizeof_expression_operand_hover(ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "sizeof expression operand session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration_fragment;
    const char *constant_value;
  } cases[] = {
      {"global_size = (int)sizeof sizeof_global", "sizeof_global",
       AG_LANGUAGE_SYMBOL_OBJECT, "sizeof_global = 5", ""},
      {"parameter_size = (int)sizeof sizeof_parameter", "sizeof_parameter",
       AG_LANGUAGE_SYMBOL_PARAMETER, "sizeof_parameter) {", ""},
      {"local_size = (int)sizeof sizeof_local", "sizeof_local",
       AG_LANGUAGE_SYMBOL_OBJECT, "sizeof_local = 9", ""},
      {"macro_size = (int)sizeof SIZEOF_OPERAND_MACRO",
       "SIZEOF_OPERAND_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "SIZEOF_OPERAND_MACRO 17", ""},
      {"enum_size = (int)sizeof SIZEOF_OPERAND_ENUM", "SIZEOF_OPERAND_ENUM",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "SIZEOF_OPERAND_ENUM = 3", "3"},
      {"comment_size = (int)sizeof /* operand gap */ sizeof_global",
       "sizeof_global", AG_LANGUAGE_SYMBOL_OBJECT, "sizeof_global = 5", ""},
      {"splice_lf_size = (int)sizeof \\\nsizeof_parameter", "sizeof_parameter",
       AG_LANGUAGE_SYMBOL_PARAMETER, "sizeof_parameter) {", ""},
      {"splice_crlf_size = (int)sizeof \\\r\nsizeof_local", "sizeof_local",
       AG_LANGUAGE_SYMBOL_OBJECT, "sizeof_local = 9", ""},
  };
  const char *macro_comment = strstr(
      sizeof_expression_operand_hover_source,
      "/// sizeof operand macro documentation");
  CHECK(macro_comment != NULL, "sizeof expression macro comment anchor");
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          sizeof_expression_operand_hover_source,
          cases[case_index].fragment);
      const char *use = fragment
                            ? strstr(fragment, cases[case_index].name)
                            : NULL;
      const char *declaration = strstr(
          sizeof_expression_operand_hover_source,
          cases[case_index].declaration_fragment);
      CHECK(use && declaration, "sizeof expression operand anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh sizeof expression operand session");
        }
        CHECK(analyze_named(
                  analysis_session, "sizeof-expression-operand.c",
                  sizeof_expression_operand_hover_source,
                  (size_t)(use - sizeof_expression_operand_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "sizeof expression operand analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(hover && completion && !snapshot.partial &&
                  snapshot.diagnostic_count == 0 &&
                  hover->kind == cases[case_index].kind &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            sizeof_expression_operand_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            sizeof_expression_operand_hover_source +
                            name_length) &&
                  same_range(&hover->declaration, &completion->declaration),
              "sizeof expression operand fields");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_OBJECT ||
            cases[case_index].kind == AG_LANGUAGE_SYMBOL_PARAMETER)
          CHECK(strcmp(hover->type, "int") == 0,
                "sizeof expression object type");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(strcmp(hover->constant_value,
                       cases[case_index].constant_value) == 0,
                "sizeof expression enum value");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
          CHECK(hover->macro_replacement &&
                    strcmp(hover->macro_replacement, "17") == 0 &&
                    check_documentation_symbol(
                        hover, "sizeof operand macro documentation",
                        "sizeof-expression-operand.c",
                        (size_t)(macro_comment -
                                 sizeof_expression_operand_hover_source),
                        (size_t)(macro_comment -
                                 sizeof_expression_operand_hover_source) +
                            strlen("/// sizeof operand macro documentation")),
                "sizeof expression macro fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_statement_keyword_operand_hover(ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "statement keyword operand session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration_fragment;
    const char *constant_value;
  } cases[] = {
      {"return statement_local;", "statement_local",
       AG_LANGUAGE_SYMBOL_OBJECT, "statement_local = statement_parameter", ""},
      {"return statement_global;", "statement_global",
       AG_LANGUAGE_SYMBOL_OBJECT, "statement_global = 5", ""},
      {"return statement_parameter;", "statement_parameter",
       AG_LANGUAGE_SYMBOL_PARAMETER, "statement_parameter) {", ""},
      {"return STATEMENT_OPERAND_MACRO;", "STATEMENT_OPERAND_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "STATEMENT_OPERAND_MACRO 7", ""},
      {"return STATEMENT_OPERAND_ENUM;", "STATEMENT_OPERAND_ENUM",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "STATEMENT_OPERAND_ENUM = 3", "3"},
      {"return /* operand gap */ statement_global;", "statement_global",
       AG_LANGUAGE_SYMBOL_OBJECT, "statement_global = 5", ""},
      {"return \\\nstatement_parameter;", "statement_parameter",
       AG_LANGUAGE_SYMBOL_PARAMETER, "statement_parameter) {", ""},
      {"return \\\r\nstatement_local;", "statement_local",
       AG_LANGUAGE_SYMBOL_OBJECT, "statement_local = statement_parameter", ""},
      {"case STATEMENT_OPERAND_ENUM:", "STATEMENT_OPERAND_ENUM",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "STATEMENT_OPERAND_ENUM = 3", "3"},
      {"case /* operand gap */ STATEMENT_OPERAND_MACRO:",
       "STATEMENT_OPERAND_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "STATEMENT_OPERAND_MACRO 7", ""},
  };
  const char *macro_comment = strstr(
      statement_keyword_operand_hover_source,
      "/// statement operand macro documentation");
  CHECK(macro_comment != NULL, "statement keyword macro comment anchor");
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          statement_keyword_operand_hover_source,
          cases[case_index].fragment);
      const char *use = fragment
                            ? strstr(fragment, cases[case_index].name)
                            : NULL;
      const char *declaration = strstr(
          statement_keyword_operand_hover_source,
          cases[case_index].declaration_fragment);
      CHECK(use && declaration, "statement keyword operand anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh statement keyword operand session");
        }
        CHECK(analyze_named(
                  analysis_session, "statement-keyword-operand.c",
                  statement_keyword_operand_hover_source,
                  (size_t)(use - statement_keyword_operand_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "statement keyword operand analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(hover && completion && !snapshot.partial &&
                  snapshot.diagnostic_count == 0 &&
                  hover->kind == cases[case_index].kind &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            statement_keyword_operand_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            statement_keyword_operand_hover_source +
                            name_length) &&
                  same_range(&hover->declaration, &completion->declaration),
              "statement keyword operand fields");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_OBJECT ||
            cases[case_index].kind == AG_LANGUAGE_SYMBOL_PARAMETER)
          CHECK(strcmp(hover->type, "int") == 0,
                "statement keyword object type");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(strcmp(hover->constant_value,
                       cases[case_index].constant_value) == 0,
                "statement keyword enum value");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
          CHECK(hover->macro_replacement &&
                    strcmp(hover->macro_replacement, "7") == 0 &&
                    check_documentation_symbol(
                        hover, "statement operand macro documentation",
                        "statement-keyword-operand.c",
                        (size_t)(macro_comment -
                                 statement_keyword_operand_hover_source),
                        (size_t)(macro_comment -
                                 statement_keyword_operand_hover_source) +
                            strlen("/// statement operand macro documentation")),
                "statement keyword macro fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_statement_call_operand_hover(ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "statement call operand session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration_fragment;
  } cases[] = {
      {"return statement_call_target(parameter)", "statement_call_target",
       AG_LANGUAGE_SYMBOL_FUNCTION, "statement_call_target(int value)"},
      {"return statement_call_target /* operand gap */ (parameter)",
       "statement_call_target", AG_LANGUAGE_SYMBOL_FUNCTION,
       "statement_call_target(int value)"},
      {"return statement_call_target \\\n(parameter)", "statement_call_target",
       AG_LANGUAGE_SYMBOL_FUNCTION, "statement_call_target(int value)"},
      {"return statement_call_target \\\r\n(parameter)",
       "statement_call_target", AG_LANGUAGE_SYMBOL_FUNCTION,
       "statement_call_target(int value)"},
      {"case STATEMENT_CALL_MACRO(0):", "STATEMENT_CALL_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "STATEMENT_CALL_MACRO(value)"},
      {"case STATEMENT_CALL_MACRO /* operand gap */ (1):",
       "STATEMENT_CALL_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "STATEMENT_CALL_MACRO(value)"},
      {"case STATEMENT_CALL_MACRO \\\n(2):", "STATEMENT_CALL_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "STATEMENT_CALL_MACRO(value)"},
      {"case STATEMENT_CALL_MACRO \\\r\n(3):", "STATEMENT_CALL_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "STATEMENT_CALL_MACRO(value)"},
  };
  const char *macro_comment = strstr(
      statement_call_operand_hover_source,
      "/// statement call macro documentation");
  CHECK(macro_comment != NULL, "statement call macro comment anchor");
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          statement_call_operand_hover_source,
          cases[case_index].fragment);
      const char *use = fragment
                            ? strstr(fragment, cases[case_index].name)
                            : NULL;
      const char *declaration = strstr(
          statement_call_operand_hover_source,
          cases[case_index].declaration_fragment);
      CHECK(use && declaration, "statement call operand anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh statement call operand session");
        }
        CHECK(analyze_named(
                  analysis_session, "statement-call-operand.c",
                  statement_call_operand_hover_source,
                  (size_t)(use - statement_call_operand_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "statement call operand analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(hover && completion && !snapshot.partial &&
                  snapshot.diagnostic_count == 0 &&
                  hover->kind == cases[case_index].kind &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            statement_call_operand_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            statement_call_operand_hover_source +
                            name_length) &&
                  same_range(&hover->declaration, &completion->declaration),
              "statement call operand fields");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_FUNCTION)
          CHECK(strcmp(hover->return_type, "int") == 0 &&
                    hover->has_function_prototype &&
                    hover->parameter_count == 1 && hover->has_definition,
                "statement ordinary call fields");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
          CHECK(hover->macro_is_function_like &&
                    hover->macro_parameter_count == 1 &&
                    strcmp(hover->macro_parameters[0], "value") == 0 &&
                    hover->macro_replacement &&
                    strcmp(hover->macro_replacement,
                           "( 7 + ( value ) )") == 0 &&
                    check_documentation_symbol(
                        hover, "statement call macro documentation",
                        "statement-call-operand.c",
                        (size_t)(macro_comment -
                                 statement_call_operand_hover_source),
                        (size_t)(macro_comment -
                                 statement_call_operand_hover_source) +
                            strlen("/// statement call macro documentation")),
                "statement macro call fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_case_expression_operand_hover(ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "case expression operand session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration_fragment;
    const char *constant_value;
  } cases[] = {
      {"case -CASE_EXPRESSION_A:", "CASE_EXPRESSION_A",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "CASE_EXPRESSION_A = 2", "2"},
      {"case 1 + CASE_EXPRESSION_B:", "CASE_EXPRESSION_B",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "CASE_EXPRESSION_B = 3", "3"},
      {"case (CASE_EXPRESSION_C):", "CASE_EXPRESSION_C",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "CASE_EXPRESSION_C = 4", "4"},
      {"case CASE_EXPRESSION_A + CASE_EXPRESSION_B:",
       "CASE_EXPRESSION_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "CASE_EXPRESSION_B = 3", "3"},
      {"? CASE_EXPRESSION_B : CASE_EXPRESSION_C:",
       "CASE_EXPRESSION_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "CASE_EXPRESSION_C = 4", "4"},
      {"case CASE_EXPRESSION_A /* expression gap */ + CASE_EXPRESSION_MACRO:",
       "CASE_EXPRESSION_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "CASE_EXPRESSION_MACRO 5", ""},
      {"case CASE_EXPRESSION_A + \\\nCASE_EXPRESSION_B:",
       "CASE_EXPRESSION_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "CASE_EXPRESSION_B = 3", "3"},
      {"case CASE_EXPRESSION_A + \\\r\nCASE_EXPRESSION_C:",
       "CASE_EXPRESSION_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "CASE_EXPRESSION_C = 4", "4"},
  };
  const char *macro_comment = strstr(
      case_expression_operand_hover_source,
      "/// case expression macro documentation");
  CHECK(macro_comment != NULL, "case expression macro comment anchor");
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          case_expression_operand_hover_source, cases[case_index].fragment);
      const char *use = fragment
                            ? strstr(fragment, cases[case_index].name)
                            : NULL;
      const char *declaration = strstr(
          case_expression_operand_hover_source,
          cases[case_index].declaration_fragment);
      CHECK(use && declaration, "case expression operand anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh case expression operand session");
        }
        CHECK(analyze_named(
                  analysis_session, "case-expression-operand.c",
                  case_expression_operand_hover_source,
                  (size_t)(use - case_expression_operand_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "case expression operand analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(hover && completion && !snapshot.partial &&
                  snapshot.diagnostic_count == 0 &&
                  hover->kind == cases[case_index].kind &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            case_expression_operand_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            case_expression_operand_hover_source +
                            name_length) &&
                  same_range(&hover->declaration, &completion->declaration),
              "case expression operand fields");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(strcmp(hover->constant_value,
                       cases[case_index].constant_value) == 0,
                "case expression enum value");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
          CHECK(hover->macro_replacement &&
                    strcmp(hover->macro_replacement, "5") == 0 &&
                    check_documentation_symbol(
                        hover, "case expression macro documentation",
                        "case-expression-operand.c",
                        (size_t)(macro_comment -
                                 case_expression_operand_hover_source),
                        (size_t)(macro_comment -
                                 case_expression_operand_hover_source) +
                            strlen("/// case expression macro documentation")),
                "case expression macro fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_enum_initializer_operand_hover(ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "enum initializer operand session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration_fragment;
    const char *constant_value;
  } cases[] = {
      {"ENUM_INITIALIZER_UNARY = -ENUM_INITIALIZER_BASE",
       "ENUM_INITIALIZER_BASE", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "ENUM_INITIALIZER_BASE = 3", "3"},
      {"ENUM_INITIALIZER_BINARY = 1 + ENUM_INITIALIZER_OTHER",
       "ENUM_INITIALIZER_OTHER", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "ENUM_INITIALIZER_OTHER = 4", "4"},
      {"ENUM_INITIALIZER_GROUPED = (ENUM_INITIALIZER_BASE)",
       "ENUM_INITIALIZER_BASE", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "ENUM_INITIALIZER_BASE = 3", "3"},
      {"ENUM_INITIALIZER_BASE + ENUM_INITIALIZER_OTHER",
       "ENUM_INITIALIZER_OTHER", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "ENUM_INITIALIZER_OTHER = 4", "4"},
      {"? ENUM_INITIALIZER_BASE : ENUM_INITIALIZER_OTHER",
       "ENUM_INITIALIZER_OTHER", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "ENUM_INITIALIZER_OTHER = 4", "4"},
      {"ENUM_INITIALIZER_BASE + ENUM_INITIALIZER_MACRO",
       "ENUM_INITIALIZER_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "ENUM_INITIALIZER_MACRO 5", ""},
      {"/* expression gap */ + ENUM_INITIALIZER_OTHER",
       "ENUM_INITIALIZER_OTHER", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "ENUM_INITIALIZER_OTHER = 4", "4"},
      {"ENUM_INITIALIZER_BASE + \\\nENUM_INITIALIZER_OTHER",
       "ENUM_INITIALIZER_OTHER", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "ENUM_INITIALIZER_OTHER = 4", "4"},
      {"ENUM_INITIALIZER_BASE + \\\r\nENUM_INITIALIZER_OTHER",
       "ENUM_INITIALIZER_OTHER", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "ENUM_INITIALIZER_OTHER = 4", "4"},
      {"ENUM_INITIALIZER_BLOCK_DERIVED = ENUM_INITIALIZER_BLOCK_BASE + 1",
       "ENUM_INITIALIZER_BLOCK_BASE", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "ENUM_INITIALIZER_BLOCK_BASE = 7", "7"},
  };
  const char *macro_comment = strstr(
      enum_initializer_operand_hover_source,
      "/// enum initializer macro documentation");
  CHECK(macro_comment != NULL, "enum initializer macro comment anchor");
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          enum_initializer_operand_hover_source,
          cases[case_index].fragment);
      const char *use = fragment
                            ? strstr(fragment, cases[case_index].name)
                            : NULL;
      const char *declaration = strstr(
          enum_initializer_operand_hover_source,
          cases[case_index].declaration_fragment);
      CHECK(use && declaration, "enum initializer operand anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh enum initializer operand session");
        }
        CHECK(analyze_named(
                  analysis_session, "enum-initializer-operand.c",
                  enum_initializer_operand_hover_source,
                  (size_t)(use - enum_initializer_operand_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "enum initializer operand analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(hover && completion && !snapshot.partial &&
                  snapshot.diagnostic_count == 0 &&
                  hover->kind == cases[case_index].kind &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            enum_initializer_operand_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            enum_initializer_operand_hover_source +
                            name_length) &&
                  same_range(&hover->declaration, &completion->declaration),
              "enum initializer operand fields");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(strcmp(hover->constant_value,
                       cases[case_index].constant_value) == 0,
                "enum initializer operand value");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
          CHECK(hover->macro_replacement &&
                    strcmp(hover->macro_replacement, "5") == 0 &&
                    check_documentation_symbol(
                        hover, "enum initializer macro documentation",
                        "enum-initializer-operand.c",
                        (size_t)(macro_comment -
                                 enum_initializer_operand_hover_source),
                        (size_t)(macro_comment -
                                 enum_initializer_operand_hover_source) +
                            strlen("/// enum initializer macro documentation")),
                "enum initializer macro fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_incomplete_enum_initializer_hover(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "incomplete enum initializer session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  struct {
    size_t source_index;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration_fragment;
    const char *constant_value;
  } cases[] = {
      {0, "INCOMPLETE_ENUM_BASE", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INCOMPLETE_ENUM_BASE = 3", "3"},
      {0, "INCOMPLETE_ENUM_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "INCOMPLETE_ENUM_MACRO 5", ""},
      {1, "INCOMPLETE_ENUM_BLOCK_BASE", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INCOMPLETE_ENUM_BLOCK_BASE = 7", "7"},
  };
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *source =
          incomplete_enum_initializer_sources[cases[case_index].source_index];
      const char *use = last_occurrence(source, cases[case_index].name);
      const char *declaration = strstr(
          source, cases[case_index].declaration_fragment);
      CHECK(use && declaration, "incomplete enum initializer anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh incomplete enum initializer session");
        }
        CHECK(analyze_named(
                  analysis_session, "incomplete-enum-initializer.c", source,
                  (size_t)(use - source) + deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "incomplete enum initializer analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(hover && completion && snapshot.partial &&
                  snapshot.diagnostic_count == 0 &&
                  hover->kind == cases[case_index].kind &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration - source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration - source + name_length) &&
                  same_range(&hover->declaration, &completion->declaration),
              "incomplete enum initializer fields");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(strcmp(hover->constant_value,
                       cases[case_index].constant_value) == 0,
                "incomplete enum initializer value");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO) {
          const char *comment = strstr(
              source, "/// incomplete enum macro documentation");
          CHECK(hover->macro_replacement &&
                    strcmp(hover->macro_replacement, "5") == 0 &&
                    comment && check_documentation_symbol(
                        hover, "incomplete enum macro documentation",
                        "incomplete-enum-initializer.c",
                        (size_t)(comment - source),
                        (size_t)(comment - source) +
                            strlen("/// incomplete enum macro documentation")),
                "incomplete enum macro fields");
        }
        if (cases[case_index].source_index == 1)
          CHECK(find_symbol(
                    &snapshot, "parameter", AG_LANGUAGE_SYMBOL_PARAMETER) &&
                    find_symbol(
                        &snapshot, "before", AG_LANGUAGE_SYMBOL_OBJECT),
                "incomplete enum block lookup point");
        const ag_language_symbol_t *derived = find_symbol(
            &snapshot,
            cases[case_index].source_index == 0
                ? "INCOMPLETE_ENUM_DERIVED"
                : "INCOMPLETE_ENUM_BLOCK_DERIVED",
            AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
        CHECK(derived && derived->constant_value &&
                  strcmp(derived->constant_value,
                         cases[case_index].source_index == 0
                             ? "8" : "7") == 0,
              "incomplete enum resolved operand value preserved");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  struct {
    size_t source_index;
    const char *partial_name;
    const char *completion_name;
    ag_language_symbol_kind_t kind;
    const char *declaration_fragment;
    const char *constant_value;
  } partial_cases[] = {
      {2, "INCOMPLETE_ENUM_PARTIAL_OT", "INCOMPLETE_ENUM_PARTIAL_OTHER",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INCOMPLETE_ENUM_PARTIAL_OTHER = 12", "12"},
      {3, "INCOMPLETE_ENUM_PARTIAL_MAC", "INCOMPLETE_ENUM_PARTIAL_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "INCOMPLETE_ENUM_PARTIAL_MACRO 13", ""},
      {4, "INCOMPLETE_ENUM_PARTIAL_BLOCK_BA",
       "INCOMPLETE_ENUM_PARTIAL_BLOCK_BASE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INCOMPLETE_ENUM_PARTIAL_BLOCK_BASE = 14", "14"},
  };
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(partial_cases) / sizeof(partial_cases[0]);
         case_index++) {
      const char *source = incomplete_enum_initializer_sources[
          partial_cases[case_index].source_index];
      const char *partial = last_occurrence(
          source, partial_cases[case_index].partial_name);
      const char *declaration = strstr(
          source, partial_cases[case_index].declaration_fragment);
      CHECK(partial && declaration,
            "partial incomplete enum initializer anchors");
      size_t partial_length = strlen(partial_cases[case_index].partial_name);
      size_t completion_length =
          strlen(partial_cases[case_index].completion_name);
      size_t deltas[] = {0, partial_length / 2, partial_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh partial incomplete enum initializer session");
        }
        CHECK(analyze_named(
                  analysis_session, "incomplete-enum-initializer.c", source,
                  (size_t)(partial - source) + deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "partial incomplete enum initializer analysis");
        const ag_language_diagnostic_t *partial_diagnostic =
            find_diagnostic(&snapshot, "AGC_PARTIAL_IDENTIFIER");
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, partial_cases[case_index].completion_name,
            partial_cases[case_index].kind);
        CHECK(snapshot.partial && snapshot.diagnostic_count == 1 &&
                  partial_diagnostic && !hover_symbol(&snapshot) &&
                  completion &&
                  partial_diagnostic->range.start.offset ==
                      (int)(partial - source) &&
                  partial_diagnostic->range.end.offset ==
                      (int)(partial - source + partial_length) &&
                  completion->declaration.start.offset ==
                      (int)(declaration - source) &&
                  completion->declaration.end.offset ==
                      (int)(declaration - source + completion_length),
              "partial incomplete enum initializer fields");
        if (partial_cases[case_index].kind ==
            AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(strcmp(completion->constant_value,
                       partial_cases[case_index].constant_value) == 0,
                "partial incomplete enum initializer value");
        if (partial_cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO) {
          const char *comment = strstr(
              source, "/// incomplete enum partial macro documentation");
          CHECK(completion->macro_replacement &&
                    strcmp(completion->macro_replacement, "13") == 0 &&
                    comment && check_documentation_symbol(
                        completion,
                        "incomplete enum partial macro documentation",
                        "incomplete-enum-initializer.c",
                        (size_t)(comment - source),
                        (size_t)(comment - source) +
                            strlen("/// incomplete enum partial macro documentation")),
                "partial incomplete enum macro fields");
        }
        if (partial_cases[case_index].source_index == 4)
          CHECK(find_symbol(
                    &snapshot, "parameter", AG_LANGUAGE_SYMBOL_PARAMETER) &&
                    find_symbol(
                        &snapshot, "before", AG_LANGUAGE_SYMBOL_OBJECT),
                "partial incomplete enum block lookup point");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  const char *invalid_source =
      "enum { INCOMPLETE_ENUM_INVALID_BASE = 3, "
      "INCOMPLETE_ENUM_INVALID_DERIVED = INCOMPLETE_ENUM_INVALID_BASE +";
  const char *invalid_use = last_occurrence(
      invalid_source, "INCOMPLETE_ENUM_INVALID_BASE");
  int invalid_ok = invalid_use && analyze_named(
      session, "incomplete-enum-invalid.c", invalid_source,
      (size_t)(invalid_use - invalid_source) + 4,
      (header_bundle_t){0}, defaults, &snapshot, &error);
  CHECK(invalid_ok && snapshot.partial,
        "incomplete enum operator remains incomplete");
  ag_language_analysis_snapshot_dispose(&snapshot);
  const char *reuse_source = incomplete_enum_initializer_sources[0];
  const char *reuse_use = last_occurrence(
      reuse_source, "INCOMPLETE_ENUM_BASE");
  CHECK(reuse_use && analyze_named(
            session, "incomplete-enum-initializer.c", reuse_source,
            (size_t)(reuse_use - reuse_source) + 4,
            (header_bundle_t){0}, defaults, &snapshot, &error) &&
            snapshot.partial && hover_symbol(&snapshot) &&
            strcmp(hover_symbol(&snapshot)->name,
                   "INCOMPLETE_ENUM_BASE") == 0,
        "incomplete enum session reusable after invalid expression");
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_incomplete_enum_header_hover(ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "incomplete enum header session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *header_paths[] = {"incomplete-enum.h"};
  const char *header_sources[] = {incomplete_enum_header_source};
  header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
  CHECK(bundle.bytes != NULL, "incomplete enum header bundle");
  struct {
    size_t source_index;
    const char *cursor_name;
    const char *candidate_name;
    ag_language_symbol_kind_t kind;
    const char *constant_value;
    const char *documentation;
    const char *comment;
    int partial_identifier;
  } cases[] = {
      {0, "INCOMPLETE_HEADER_ENUM_VALUE", "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "17",
       "header enum value documentation",
       "/// header enum value documentation", 0},
      {1, "INCOMPLETE_HEADER_ENUM_MACRO", "INCOMPLETE_HEADER_ENUM_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "19", "header enum macro documentation",
       "/// header enum macro documentation", 0},
      {2, "INCOMPLETE_HEADER_ENUM_VA", "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "17",
       "header enum value documentation",
       "/// header enum value documentation", 1},
      {3, "INCOMPLETE_HEADER_ENUM_MA", "INCOMPLETE_HEADER_ENUM_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "19", "header enum macro documentation",
       "/// header enum macro documentation", 1},
  };
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *source =
          incomplete_enum_header_main_sources[cases[case_index].source_index];
      const char *cursor_name = last_occurrence(
          source, cases[case_index].cursor_name);
      const char *declaration = strstr(
          incomplete_enum_header_source, cases[case_index].candidate_name);
      const char *comment = strstr(
          incomplete_enum_header_source, cases[case_index].comment);
      CHECK(cursor_name && declaration && comment,
            "incomplete enum header anchors");
      size_t cursor_name_length = strlen(cases[case_index].cursor_name);
      size_t candidate_name_length =
          strlen(cases[case_index].candidate_name);
      size_t deltas[] = {
          0, cursor_name_length / 2, cursor_name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh incomplete enum header session");
        }
        CHECK(analyze_named(
                  analysis_session, "incomplete-enum-header-main.c", source,
                  (size_t)(cursor_name - source) + deltas[delta_index],
                  bundle, defaults, &snapshot, &error),
              "incomplete enum header analysis");
        const ag_language_symbol_t *candidate = find_symbol(
            &snapshot, cases[case_index].candidate_name,
            cases[case_index].kind);
        CHECK(snapshot.partial && candidate &&
                  candidate->declaration.source_name &&
                  strcmp(candidate->declaration.source_name,
                         "incomplete-enum.h") == 0 &&
                  candidate->declaration.start.offset ==
                      (int)(declaration - incomplete_enum_header_source) &&
                  candidate->declaration.end.offset ==
                      (int)(declaration - incomplete_enum_header_source +
                            candidate_name_length) &&
                  check_documentation_symbol(
                      candidate, cases[case_index].documentation,
                      "incomplete-enum.h",
                      (size_t)(comment - incomplete_enum_header_source),
                      (size_t)(comment - incomplete_enum_header_source) +
                          strlen(cases[case_index].comment)) &&
                  snapshot.dependency_count == 1 &&
                  strcmp(snapshot.dependencies[0],
                         "incomplete-enum.h") == 0,
              "incomplete enum header candidate fields");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(candidate->constant_value &&
                    strcmp(candidate->constant_value,
                           cases[case_index].constant_value) == 0,
                "incomplete enum header value");
        else
          CHECK(candidate->macro_replacement &&
                    strcmp(candidate->macro_replacement,
                           cases[case_index].constant_value) == 0,
                "incomplete enum header macro replacement");
        if (cases[case_index].partial_identifier) {
          const ag_language_diagnostic_t *partial =
              find_diagnostic(&snapshot, "AGC_PARTIAL_IDENTIFIER");
          CHECK(!hover_symbol(&snapshot) &&
                    snapshot.diagnostic_count == 1 && partial &&
                    partial->range.start.offset ==
                        (int)(cursor_name - source) &&
                    partial->range.end.offset ==
                        (int)(cursor_name - source + cursor_name_length),
                "incomplete enum header partial identifier fields");
        } else {
          const ag_language_symbol_t *hover = hover_symbol(&snapshot);
          const ag_language_symbol_t *derived = find_symbol(
              &snapshot, "INCOMPLETE_HEADER_DERIVED",
              AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
          CHECK(snapshot.diagnostic_count == 0 && hover &&
                    strcmp(hover->name,
                           cases[case_index].candidate_name) == 0 &&
                    same_range(&hover->declaration,
                               &candidate->declaration) &&
                    derived && derived->constant_value &&
                    strcmp(derived->constant_value,
                           cases[case_index].constant_value) == 0,
                "incomplete enum header resolved operand fields");
        }
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  free(bundle.bytes);
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_incomplete_enum_header_revisions(ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "incomplete enum header revision session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *header_paths[] = {"incomplete-enum.h"};
  struct {
    size_t source_index;
    const char *cursor_name;
    const char *candidate_name;
    ag_language_symbol_kind_t kind;
    int partial_identifier;
  } cases[] = {
      {0, "INCOMPLETE_HEADER_ENUM_VALUE", "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, 0},
      {1, "INCOMPLETE_HEADER_ENUM_MACRO", "INCOMPLETE_HEADER_ENUM_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, 0},
      {2, "INCOMPLETE_HEADER_ENUM_VA", "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, 1},
      {3, "INCOMPLETE_HEADER_ENUM_MA", "INCOMPLETE_HEADER_ENUM_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, 1},
  };
  struct {
    size_t revision_index;
    const char *enum_value;
    const char *macro_value;
    const char *enum_documentation;
    const char *enum_comment;
    const char *macro_documentation;
    const char *macro_comment;
  } revisions[] = {
      {0, "17", "19", "header enum value documentation",
       "/// header enum value documentation",
       "header enum macro documentation",
       "/// header enum macro documentation"},
      {1, "27", "29", "header enum value revision 2",
       "/** header enum value revision 2 */",
       "header enum macro revision 2",
       "/** header enum macro revision 2 */"},
      {2, "37", "39", "", NULL, "", NULL},
      {0, "17", "19", "header enum value documentation",
       "/// header enum value documentation",
       "header enum macro documentation",
       "/// header enum macro documentation"},
  };
  for (size_t revision = 0;
       revision < sizeof(revisions) / sizeof(revisions[0]); revision++) {
    const char *header = incomplete_enum_header_revisions[
        revisions[revision].revision_index];
    const char *header_sources[] = {header};
    header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
    CHECK(bundle.bytes != NULL, "incomplete enum header revision bundle");
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *source =
          incomplete_enum_header_main_sources[cases[case_index].source_index];
      const char *cursor_name = last_occurrence(
          source, cases[case_index].cursor_name);
      const char *declaration = strstr(
          header, cases[case_index].candidate_name);
      const char *documentation =
          cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT
              ? revisions[revision].enum_documentation
              : revisions[revision].macro_documentation;
      const char *comment_text =
          cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT
              ? revisions[revision].enum_comment
              : revisions[revision].macro_comment;
      const char *comment = comment_text ? strstr(header, comment_text) : NULL;
      const char *expected_value =
          cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT
              ? revisions[revision].enum_value
              : revisions[revision].macro_value;
      CHECK(cursor_name && declaration && (!comment_text || comment),
            "incomplete enum header revision anchors");
      size_t cursor_name_length = strlen(cases[case_index].cursor_name);
      size_t candidate_name_length = strlen(cases[case_index].candidate_name);
      CHECK(analyze_named(
                session, "incomplete-enum-header-main.c", source,
                (size_t)(cursor_name - source) + cursor_name_length / 2,
                bundle, defaults, &snapshot, &error),
            "incomplete enum header revision analysis");
      const ag_language_symbol_t *candidate = find_symbol(
          &snapshot, cases[case_index].candidate_name,
          cases[case_index].kind);
      CHECK(snapshot.partial && candidate &&
                candidate->declaration.source_name &&
                strcmp(candidate->declaration.source_name,
                       "incomplete-enum.h") == 0 &&
                candidate->declaration.start.offset ==
                    (int)(declaration - header) &&
                candidate->declaration.end.offset ==
                    (int)(declaration - header + candidate_name_length) &&
                check_documentation_symbol(
                    candidate, documentation, "incomplete-enum.h",
                    comment ? (size_t)(comment - header) : 0,
                    comment ? (size_t)(comment - header) +
                                  strlen(comment_text)
                            : 0) &&
                snapshot.dependency_count == 1 &&
                strcmp(snapshot.dependencies[0],
                       "incomplete-enum.h") == 0,
            "incomplete enum header revision candidate fields");
      if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
        CHECK(candidate->constant_value &&
                  strcmp(candidate->constant_value,
                         expected_value) == 0,
              "incomplete enum header revision value");
      else
        CHECK(candidate->macro_replacement &&
                  strcmp(candidate->macro_replacement,
                         expected_value) == 0,
              "incomplete enum header revision macro replacement");
      if (cases[case_index].partial_identifier) {
        const ag_language_diagnostic_t *partial =
            find_diagnostic(&snapshot, "AGC_PARTIAL_IDENTIFIER");
        CHECK(!hover_symbol(&snapshot) &&
                  snapshot.diagnostic_count == 1 && partial &&
                  partial->range.start.offset ==
                      (int)(cursor_name - source) &&
                  partial->range.end.offset ==
                      (int)(cursor_name - source + cursor_name_length),
              "incomplete enum header revision partial fields");
      } else {
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *derived = find_symbol(
            &snapshot, "INCOMPLETE_HEADER_DERIVED",
            AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
        CHECK(snapshot.diagnostic_count == 0 && hover &&
                  strcmp(hover->name,
                         cases[case_index].candidate_name) == 0 &&
                  same_range(&hover->declaration,
                             &candidate->declaration) &&
                  derived && derived->constant_value &&
                  strcmp(derived->constant_value,
                         expected_value) == 0,
              "incomplete enum header revision resolved fields");
      }
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
    free(bundle.bytes);
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_incomplete_enum_header_rename_transitions(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "incomplete enum header rename session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *header_paths[] = {"incomplete-enum.h"};
  struct {
    size_t revision_index;
    size_t source_index;
    const char *operand_name;
    ag_language_symbol_kind_t kind;
    int resolves;
    const char *value;
    const char *documentation;
    const char *comment;
  } cases[] = {
      {0, 0, "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, 1, "17",
       "header enum value documentation",
       "/// header enum value documentation"},
      {0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", AG_LANGUAGE_SYMBOL_MACRO, 1,
       "19", "header enum macro documentation",
       "/// header enum macro documentation"},
      {3, 0, "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, 0, NULL, NULL, NULL},
      {3, 1, "INCOMPLETE_HEADER_ENUM_MACRO", AG_LANGUAGE_SYMBOL_MACRO, 0,
       NULL, NULL, NULL},
      {3, 4, "RENAMED_HEADER_ENUM_VALUE", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       1, "47", "renamed header enum value documentation",
       "/** renamed header enum value documentation */"},
      {3, 5, "RENAMED_HEADER_ENUM_MACRO", AG_LANGUAGE_SYMBOL_MACRO, 1,
       "49", "renamed header enum macro documentation",
       "/** renamed header enum macro documentation */"},
      {0, 0, "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, 1, "17",
       "header enum value documentation",
       "/// header enum value documentation"},
      {0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", AG_LANGUAGE_SYMBOL_MACRO, 1,
       "19", "header enum macro documentation",
       "/// header enum macro documentation"},
      {0, 4, "RENAMED_HEADER_ENUM_VALUE", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       0, NULL, NULL, NULL},
      {0, 5, "RENAMED_HEADER_ENUM_MACRO", AG_LANGUAGE_SYMBOL_MACRO, 0,
       NULL, NULL, NULL},
  };
  for (size_t case_index = 0;
       case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
    const char *header =
        incomplete_enum_header_revisions[cases[case_index].revision_index];
    const char *header_sources[] = {header};
    header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
    const char *source =
        incomplete_enum_header_main_sources[cases[case_index].source_index];
    const char *operand = last_occurrence(source, cases[case_index].operand_name);
    CHECK(bundle.bytes && operand,
          "incomplete enum header rename anchors");
    size_t operand_length = strlen(cases[case_index].operand_name);
    CHECK(analyze_named(
              session, "incomplete-enum-header-main.c", source,
              (size_t)(operand - source) + operand_length / 2,
              bundle, defaults, &snapshot, &error),
          "incomplete enum header rename analysis");
    CHECK(snapshot.partial && snapshot.dependency_count == 1 &&
              strcmp(snapshot.dependencies[0], "incomplete-enum.h") == 0,
          "incomplete enum header rename common fields");
    const ag_language_symbol_t *candidate = find_symbol(
        &snapshot, cases[case_index].operand_name, cases[case_index].kind);
    if (cases[case_index].resolves) {
      const char *declaration = strstr(header, cases[case_index].operand_name);
      const char *comment = strstr(header, cases[case_index].comment);
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      const ag_language_symbol_t *derived = find_symbol(
          &snapshot, "INCOMPLETE_HEADER_DERIVED",
          AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
      CHECK(declaration && comment && candidate && hover && derived &&
                snapshot.diagnostic_count == 0 &&
                strcmp(hover->name, cases[case_index].operand_name) == 0 &&
                same_range(&hover->declaration,
                           &candidate->declaration) &&
                candidate->declaration.start.offset ==
                    (int)(declaration - header) &&
                candidate->declaration.end.offset ==
                    (int)(declaration - header + operand_length) &&
                check_documentation_symbol(
                    candidate, cases[case_index].documentation,
                    "incomplete-enum.h", (size_t)(comment - header),
                    (size_t)(comment - header) +
                        strlen(cases[case_index].comment)) &&
                derived->constant_value &&
                strcmp(derived->constant_value,
                       cases[case_index].value) == 0,
            "incomplete enum header rename resolved fields");
      if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
        CHECK(candidate->constant_value &&
                  strcmp(candidate->constant_value,
                         cases[case_index].value) == 0,
              "incomplete enum header rename enum value");
      else
        CHECK(candidate->macro_replacement &&
                  strcmp(candidate->macro_replacement,
                         cases[case_index].value) == 0,
              "incomplete enum header rename macro replacement");
    } else {
      const ag_language_diagnostic_t *partial =
          find_diagnostic(&snapshot, "AGC_PARTIAL_IDENTIFIER");
      CHECK(!candidate && !hover_symbol(&snapshot) &&
                snapshot.diagnostic_count == 1 && partial &&
                partial->range.start.offset == (int)(operand - source) &&
                partial->range.end.offset ==
                    (int)(operand - source + operand_length),
            "incomplete enum header rename unresolved fields");
    }
    ag_language_analysis_snapshot_dispose(&snapshot);
    free(bundle.bytes);
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_incomplete_enum_header_kind_transitions(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "incomplete enum header kind session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *header_paths[] = {"incomplete-enum.h"};
  struct {
    size_t revision_index;
    size_t source_index;
    const char *cursor_name;
    const char *candidate_name;
    ag_language_symbol_kind_t kind;
    const char *value;
    const char *documentation;
    const char *comment;
    int partial_identifier;
  } cases[] = {
      {0, 0, "INCOMPLETE_HEADER_ENUM_VALUE", "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "17",
       "header enum value documentation",
       "/// header enum value documentation", 0},
      {0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", "INCOMPLETE_HEADER_ENUM_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "19", "header enum macro documentation",
       "/// header enum macro documentation", 0},
      {0, 2, "INCOMPLETE_HEADER_ENUM_VA", "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "17",
       "header enum value documentation",
       "/// header enum value documentation", 1},
      {0, 3, "INCOMPLETE_HEADER_ENUM_MA", "INCOMPLETE_HEADER_ENUM_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "19", "header enum macro documentation",
       "/// header enum macro documentation", 1},
      {4, 0, "INCOMPLETE_HEADER_ENUM_VALUE", "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_MACRO, "57", "switched value macro documentation",
       "/// switched value macro documentation", 0},
      {4, 1, "INCOMPLETE_HEADER_ENUM_MACRO", "INCOMPLETE_HEADER_ENUM_MACRO",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "59",
       "switched macro enum documentation",
       "/// switched macro enum documentation", 0},
      {4, 2, "INCOMPLETE_HEADER_ENUM_VA", "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_MACRO, "57", "switched value macro documentation",
       "/// switched value macro documentation", 1},
      {4, 3, "INCOMPLETE_HEADER_ENUM_MA", "INCOMPLETE_HEADER_ENUM_MACRO",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "59",
       "switched macro enum documentation",
       "/// switched macro enum documentation", 1},
      {0, 0, "INCOMPLETE_HEADER_ENUM_VALUE", "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "17",
       "header enum value documentation",
       "/// header enum value documentation", 0},
      {0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", "INCOMPLETE_HEADER_ENUM_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "19", "header enum macro documentation",
       "/// header enum macro documentation", 0},
      {0, 2, "INCOMPLETE_HEADER_ENUM_VA", "INCOMPLETE_HEADER_ENUM_VALUE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "17",
       "header enum value documentation",
       "/// header enum value documentation", 1},
      {0, 3, "INCOMPLETE_HEADER_ENUM_MA", "INCOMPLETE_HEADER_ENUM_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "19", "header enum macro documentation",
       "/// header enum macro documentation", 1},
  };
  for (size_t case_index = 0;
       case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
    const char *header =
        incomplete_enum_header_revisions[cases[case_index].revision_index];
    const char *header_sources[] = {header};
    header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
    const char *source =
        incomplete_enum_header_main_sources[cases[case_index].source_index];
    const char *cursor_name = last_occurrence(
        source, cases[case_index].cursor_name);
    const char *declaration = strstr(header, cases[case_index].candidate_name);
    const char *comment = strstr(header, cases[case_index].comment);
    CHECK(bundle.bytes && cursor_name && declaration && comment,
          "incomplete enum header kind anchors");
    size_t cursor_name_length = strlen(cases[case_index].cursor_name);
    size_t candidate_name_length = strlen(cases[case_index].candidate_name);
    CHECK(analyze_named(
              session, "incomplete-enum-header-main.c", source,
              (size_t)(cursor_name - source) + cursor_name_length / 2,
              bundle, defaults, &snapshot, &error),
          "incomplete enum header kind analysis");
    const ag_language_symbol_t *candidate = find_symbol(
        &snapshot, cases[case_index].candidate_name,
        cases[case_index].kind);
    ag_language_symbol_kind_t previous_kind =
        cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT
            ? AG_LANGUAGE_SYMBOL_MACRO
            : AG_LANGUAGE_SYMBOL_ENUM_CONSTANT;
    CHECK(snapshot.partial && candidate &&
              !find_symbol(&snapshot, cases[case_index].candidate_name,
                           previous_kind) &&
              candidate->declaration.source_name &&
              strcmp(candidate->declaration.source_name,
                     "incomplete-enum.h") == 0 &&
              candidate->declaration.start.offset ==
                  (int)(declaration - header) &&
              candidate->declaration.end.offset ==
                  (int)(declaration - header + candidate_name_length) &&
              check_documentation_symbol(
                  candidate, cases[case_index].documentation,
                  "incomplete-enum.h", (size_t)(comment - header),
                  (size_t)(comment - header) +
                      strlen(cases[case_index].comment)) &&
              snapshot.dependency_count == 1 &&
              strcmp(snapshot.dependencies[0], "incomplete-enum.h") == 0,
          "incomplete enum header kind candidate fields");
    if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
      CHECK(candidate->constant_value &&
                strcmp(candidate->constant_value,
                       cases[case_index].value) == 0 &&
                (!candidate->macro_replacement ||
                 !candidate->macro_replacement[0]),
            "incomplete enum header kind enum metadata");
    else
      CHECK(candidate->macro_replacement &&
                strcmp(candidate->macro_replacement,
                       cases[case_index].value) == 0 &&
                (!candidate->constant_value || !candidate->constant_value[0]),
            "incomplete enum header kind macro metadata");
    if (cases[case_index].partial_identifier) {
      const ag_language_diagnostic_t *partial =
          find_diagnostic(&snapshot, "AGC_PARTIAL_IDENTIFIER");
      CHECK(!hover_symbol(&snapshot) &&
                snapshot.diagnostic_count == 1 && partial &&
                partial->range.start.offset ==
                    (int)(cursor_name - source) &&
                partial->range.end.offset ==
                    (int)(cursor_name - source + cursor_name_length),
            "incomplete enum header kind partial fields");
    } else {
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      const ag_language_symbol_t *derived = find_symbol(
          &snapshot, "INCOMPLETE_HEADER_DERIVED",
          AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
      CHECK(snapshot.diagnostic_count == 0 && hover &&
                hover->kind == cases[case_index].kind &&
                strcmp(hover->name, cases[case_index].candidate_name) == 0 &&
                same_range(&hover->declaration,
                           &candidate->declaration) &&
                derived && derived->constant_value &&
                strcmp(derived->constant_value,
                       cases[case_index].value) == 0,
            "incomplete enum header kind resolved fields");
    }
    ag_language_analysis_snapshot_dispose(&snapshot);
    free(bundle.bytes);
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_incomplete_enum_header_macro_shape_transitions(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "incomplete enum header macro shape session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *header_paths[] = {"incomplete-enum.h"};
  struct {
    size_t revision_index;
    size_t source_index;
    const char *cursor_name;
    int function_like;
    int resolves;
    const char *replacement;
    const char *documentation;
    const char *comment;
  } cases[] = {
      {0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", 0, 1, "19",
       "header enum macro documentation",
       "/// header enum macro documentation"},
      {0, 3, "INCOMPLETE_HEADER_ENUM_MA", 0, 0, "19",
       "header enum macro documentation",
       "/// header enum macro documentation"},
      {5, 1, "INCOMPLETE_HEADER_ENUM_MACRO", 1, 0, "61",
       "function-like header macro documentation",
       "/// function-like header macro documentation"},
      {5, 3, "INCOMPLETE_HEADER_ENUM_MA", 1, 0, "61",
       "function-like header macro documentation",
       "/// function-like header macro documentation"},
      {0, 1, "INCOMPLETE_HEADER_ENUM_MACRO", 0, 1, "19",
       "header enum macro documentation",
       "/// header enum macro documentation"},
      {0, 3, "INCOMPLETE_HEADER_ENUM_MA", 0, 0, "19",
       "header enum macro documentation",
       "/// header enum macro documentation"},
  };
  for (size_t case_index = 0;
       case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
    const char *header =
        incomplete_enum_header_revisions[cases[case_index].revision_index];
    const char *header_sources[] = {header};
    header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
    const char *source =
        incomplete_enum_header_main_sources[cases[case_index].source_index];
    const char *cursor_name = last_occurrence(
        source, cases[case_index].cursor_name);
    const char *declaration = strstr(header, "INCOMPLETE_HEADER_ENUM_MACRO");
    const char *comment = strstr(header, cases[case_index].comment);
    CHECK(bundle.bytes && cursor_name && declaration && comment,
          "incomplete enum header macro shape anchors");
    size_t cursor_name_length = strlen(cases[case_index].cursor_name);
    size_t candidate_name_length = strlen("INCOMPLETE_HEADER_ENUM_MACRO");
    CHECK(analyze_named(
              session, "incomplete-enum-header-main.c", source,
              (size_t)(cursor_name - source) + cursor_name_length / 2,
              bundle, defaults, &snapshot, &error),
          "incomplete enum header macro shape analysis");
    const ag_language_symbol_t *candidate = find_symbol(
        &snapshot, "INCOMPLETE_HEADER_ENUM_MACRO",
        AG_LANGUAGE_SYMBOL_MACRO);
    CHECK(snapshot.partial && candidate &&
              candidate->declaration.source_name &&
              strcmp(candidate->declaration.source_name,
                     "incomplete-enum.h") == 0 &&
              candidate->declaration.start.offset ==
                  (int)(declaration - header) &&
              candidate->declaration.end.offset ==
                  (int)(declaration - header + candidate_name_length) &&
              check_documentation_symbol(
                  candidate, cases[case_index].documentation,
                  "incomplete-enum.h", (size_t)(comment - header),
                  (size_t)(comment - header) +
                      strlen(cases[case_index].comment)) &&
              candidate->macro_replacement &&
              strcmp(candidate->macro_replacement,
                     cases[case_index].replacement) == 0 &&
              candidate->macro_is_function_like ==
                  cases[case_index].function_like &&
              !candidate->macro_is_variadic &&
              candidate->macro_parameter_count ==
                  (cases[case_index].function_like ? 2 : 0) &&
              (!cases[case_index].function_like ||
               (candidate->macro_parameters &&
                strcmp(candidate->macro_parameters[0], "left") == 0 &&
                strcmp(candidate->macro_parameters[1], "right") == 0)) &&
              snapshot.dependency_count == 1 &&
              strcmp(snapshot.dependencies[0], "incomplete-enum.h") == 0,
          "incomplete enum header macro shape candidate fields");
    if (cases[case_index].resolves) {
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      const ag_language_symbol_t *derived = find_symbol(
          &snapshot, "INCOMPLETE_HEADER_DERIVED",
          AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
      CHECK(snapshot.diagnostic_count == 0 && hover &&
                strcmp(hover->name, "INCOMPLETE_HEADER_ENUM_MACRO") == 0 &&
                same_range(&hover->declaration,
                           &candidate->declaration) &&
                derived && derived->constant_value &&
                strcmp(derived->constant_value,
                       cases[case_index].replacement) == 0,
            "incomplete enum header macro shape resolved fields");
    } else {
      const ag_language_diagnostic_t *partial =
          find_diagnostic(&snapshot, "AGC_PARTIAL_IDENTIFIER");
      CHECK(!hover_symbol(&snapshot) &&
                snapshot.diagnostic_count == 1 && partial &&
                partial->range.start.offset ==
                    (int)(cursor_name - source) &&
                partial->range.end.offset ==
                    (int)(cursor_name - source + cursor_name_length),
            "incomplete enum header macro shape partial fields");
    }
    ag_language_analysis_snapshot_dispose(&snapshot);
    free(bundle.bytes);
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_incomplete_enum_header_namespace_collision(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "incomplete enum header namespace session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *header_paths[] = {"incomplete-enum.h"};
  const char *header_sources[] = {incomplete_enum_header_revisions[6]};
  const char *header = header_sources[0];
  header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
  CHECK(bundle.bytes != NULL, "incomplete enum header namespace bundle");
  const char *name = "COLLIDING_HEADER_SYMBOL";
  const char *macro_declaration = strstr(header, name);
  const char *enum_declaration = last_occurrence(header, name);
  const char *macro_comment = strstr(
      header, "/// colliding header macro documentation");
  const char *enum_comment = strstr(
      header, "/// colliding header enum documentation");
  CHECK(macro_declaration && enum_declaration &&
            macro_declaration != enum_declaration &&
            macro_comment && enum_comment,
        "incomplete enum header namespace header anchors");
  struct {
    size_t source_index;
    ag_language_symbol_kind_t hover_kind;
    const char *derived_value;
  } cases[] = {
      {6, AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "73"},
      {7, AG_LANGUAGE_SYMBOL_MACRO, "71"},
  };
  size_t name_length = strlen(name);
  size_t deltas[] = {0, name_length / 2, name_length};
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *source =
          incomplete_enum_header_main_sources[cases[case_index].source_index];
      const char *use = last_occurrence(source, name);
      CHECK(use != NULL, "incomplete enum header namespace source anchor");
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh incomplete enum header namespace session");
        }
        CHECK(analyze_named(
                  analysis_session, "incomplete-enum-header-main.c", source,
                  (size_t)(use - source) + deltas[delta_index],
                  bundle, defaults, &snapshot, &error),
              "incomplete enum header namespace analysis");
        const ag_language_symbol_t *enum_candidate = find_symbol(
            &snapshot, name, AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
        const ag_language_symbol_t *macro_candidate = find_symbol(
            &snapshot, name, AG_LANGUAGE_SYMBOL_MACRO);
        const ag_language_symbol_t *derived = find_symbol(
            &snapshot, "INCOMPLETE_HEADER_DERIVED",
            AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        CHECK(snapshot.partial && snapshot.diagnostic_count == 0 &&
                  enum_candidate && macro_candidate && derived && hover &&
                  hover->kind == cases[case_index].hover_kind &&
                  strcmp(hover->name, name) == 0 &&
                  derived->constant_value &&
                  strcmp(derived->constant_value,
                         cases[case_index].derived_value) == 0 &&
                  snapshot.dependency_count == 1 &&
                  strcmp(snapshot.dependencies[0],
                         "incomplete-enum.h") == 0,
              "incomplete enum header namespace common fields");
        CHECK(enum_candidate->constant_value &&
                  strcmp(enum_candidate->constant_value, "73") == 0 &&
                  enum_candidate->declaration.start.offset ==
                      (int)(enum_declaration - header) &&
                  enum_candidate->declaration.end.offset ==
                      (int)(enum_declaration - header + name_length) &&
                  check_documentation_symbol(
                      enum_candidate, "colliding header enum documentation",
                      "incomplete-enum.h",
                      (size_t)(enum_comment - header),
                      (size_t)(enum_comment - header) +
                          strlen("/// colliding header enum documentation")),
              "incomplete enum header namespace enum fields");
        CHECK(macro_candidate->macro_is_function_like &&
                  !macro_candidate->macro_is_variadic &&
                  macro_candidate->macro_parameter_count == 1 &&
                  macro_candidate->macro_parameters &&
                  strcmp(macro_candidate->macro_parameters[0], "value") == 0 &&
                  macro_candidate->macro_replacement &&
                  strcmp(macro_candidate->macro_replacement,
                         "( ( value ) + 70 )") == 0 &&
                  macro_candidate->declaration.start.offset ==
                      (int)(macro_declaration - header) &&
                  macro_candidate->declaration.end.offset ==
                      (int)(macro_declaration - header + name_length) &&
                  check_documentation_symbol(
                      macro_candidate, "colliding header macro documentation",
                      "incomplete-enum.h",
                      (size_t)(macro_comment - header),
                      (size_t)(macro_comment - header) +
                          strlen("/// colliding header macro documentation")),
              "incomplete enum header namespace macro fields");
        const ag_language_symbol_t *expected_hover =
            cases[case_index].hover_kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT
                ? enum_candidate
                : macro_candidate;
        CHECK(same_range(&hover->declaration,
                         &expected_hover->declaration),
              "incomplete enum header namespace hover range");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  free(bundle.bytes);
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_incomplete_enum_header_namespace_revisions(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "incomplete enum header namespace revision session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *header_paths[] = {"incomplete-enum.h"};
  const char *name = "COLLIDING_HEADER_SYMBOL";
  size_t name_length = strlen(name);
  struct {
    size_t revision_index;
    int has_enum;
    int has_macro;
    const char *enum_value;
    const char *enum_documentation;
    const char *enum_comment;
    const char *macro_replacement;
    const char *macro_invocation_value;
    const char *macro_documentation;
    const char *macro_comment;
  } revisions[] = {
      {6, 1, 1, "73", "colliding header enum documentation",
       "/// colliding header enum documentation", "( ( value ) + 70 )", "71",
       "colliding header macro documentation",
       "/// colliding header macro documentation"},
      {7, 1, 0, "83", "enum-only colliding header documentation",
       "/// enum-only colliding header documentation", NULL, NULL, NULL, NULL},
      {8, 0, 1, NULL, NULL, NULL, "( ( value ) + 80 )", "81",
       "macro-only colliding header documentation",
       "/// macro-only colliding header documentation"},
      {6, 1, 1, "73", "colliding header enum documentation",
       "/// colliding header enum documentation", "( ( value ) + 70 )", "71",
       "colliding header macro documentation",
       "/// colliding header macro documentation"},
  };
  struct {
    size_t source_index;
    ag_language_symbol_kind_t hover_when_both;
  } sources[] = {
      {6, AG_LANGUAGE_SYMBOL_ENUM_CONSTANT},
      {7, AG_LANGUAGE_SYMBOL_MACRO},
  };
  size_t deltas[] = {0, name_length / 2, name_length};
  for (size_t revision = 0;
       revision < sizeof(revisions) / sizeof(revisions[0]); revision++) {
    const char *header =
        incomplete_enum_header_revisions[revisions[revision].revision_index];
    const char *header_sources[] = {header};
    header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
    CHECK(bundle.bytes != NULL,
          "incomplete enum header namespace revision bundle");
    for (size_t source_index = 0;
         source_index < sizeof(sources) / sizeof(sources[0]); source_index++) {
      const char *source =
          incomplete_enum_header_main_sources[sources[source_index].source_index];
      const char *use = last_occurrence(source, name);
      CHECK(use != NULL,
            "incomplete enum header namespace revision source anchor");
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        CHECK(analyze_named(
                  session, "incomplete-enum-header-main.c", source,
                  (size_t)(use - source) + deltas[delta_index],
                  bundle, defaults, &snapshot, &error),
              "incomplete enum header namespace revision analysis");
        const ag_language_symbol_t *enum_candidate = find_symbol(
            &snapshot, name, AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
        const ag_language_symbol_t *macro_candidate = find_symbol(
            &snapshot, name, AG_LANGUAGE_SYMBOL_MACRO);
        CHECK(snapshot.partial &&
                  (!!enum_candidate == revisions[revision].has_enum) &&
                  (!!macro_candidate == revisions[revision].has_macro) &&
                  snapshot.dependency_count == 1 &&
                  strcmp(snapshot.dependencies[0],
                         "incomplete-enum.h") == 0,
              "incomplete enum header namespace revision common fields");
        if (enum_candidate) {
          const char *declaration = last_occurrence(header, name);
          const char *comment = strstr(
              header, revisions[revision].enum_comment);
          CHECK(declaration && comment && enum_candidate->constant_value &&
                    strcmp(enum_candidate->constant_value,
                           revisions[revision].enum_value) == 0 &&
                    enum_candidate->declaration.start.offset ==
                        (int)(declaration - header) &&
                    enum_candidate->declaration.end.offset ==
                        (int)(declaration - header + name_length) &&
                    check_documentation_symbol(
                        enum_candidate,
                        revisions[revision].enum_documentation,
                        "incomplete-enum.h",
                        (size_t)(comment - header),
                        (size_t)(comment - header) +
                            strlen(revisions[revision].enum_comment)),
                "incomplete enum header namespace revision enum fields");
        }
        if (macro_candidate) {
          const char *declaration = strstr(header, name);
          const char *comment = strstr(
              header, revisions[revision].macro_comment);
          CHECK(declaration && comment &&
                    macro_candidate->macro_is_function_like &&
                    !macro_candidate->macro_is_variadic &&
                    macro_candidate->macro_parameter_count == 1 &&
                    macro_candidate->macro_parameters &&
                    strcmp(macro_candidate->macro_parameters[0],
                           "value") == 0 &&
                    macro_candidate->macro_replacement &&
                    strcmp(macro_candidate->macro_replacement,
                           revisions[revision].macro_replacement) == 0 &&
                    macro_candidate->declaration.start.offset ==
                        (int)(declaration - header) &&
                    macro_candidate->declaration.end.offset ==
                        (int)(declaration - header + name_length) &&
                    check_documentation_symbol(
                        macro_candidate,
                        revisions[revision].macro_documentation,
                        "incomplete-enum.h",
                        (size_t)(comment - header),
                        (size_t)(comment - header) +
                            strlen(revisions[revision].macro_comment)),
                "incomplete enum header namespace revision macro fields");
        }
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *derived = find_symbol(
            &snapshot, "INCOMPLETE_HEADER_DERIVED",
            AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
        int invocation = sources[source_index].hover_when_both ==
                         AG_LANGUAGE_SYMBOL_MACRO;
        if (!revisions[revision].has_enum && !invocation) {
          const ag_language_diagnostic_t *partial =
              find_diagnostic(&snapshot, "AGC_PARTIAL_IDENTIFIER");
          CHECK(!hover && snapshot.diagnostic_count == 1 && partial &&
                    partial->range.start.offset == (int)(use - source) &&
                    partial->range.end.offset ==
                        (int)(use - source + name_length),
                "incomplete enum header namespace revision bare partial");
        } else {
          ag_language_symbol_kind_t expected_kind =
              invocation && revisions[revision].has_macro
                  ? AG_LANGUAGE_SYMBOL_MACRO
                  : AG_LANGUAGE_SYMBOL_ENUM_CONSTANT;
          const ag_language_symbol_t *expected =
              expected_kind == AG_LANGUAGE_SYMBOL_MACRO
                  ? macro_candidate
                  : enum_candidate;
          int invalid_enum_invocation =
              invocation && !revisions[revision].has_macro;
          int expected_diagnostic_count =
              invalid_enum_invocation &&
                      deltas[delta_index] == name_length
                  ? 1
                  : 0;
          CHECK(snapshot.diagnostic_count == expected_diagnostic_count &&
                    hover && expected &&
                    hover->kind == expected_kind &&
                    same_range(&hover->declaration,
                               &expected->declaration),
                "incomplete enum header namespace revision hover fields");
          if (invalid_enum_invocation) {
            CHECK(!derived,
                  "incomplete enum header namespace invalid invocation");
            if (expected_diagnostic_count) {
              const ag_language_diagnostic_t *invalid_call =
                  find_diagnostic(&snapshot, "E3102");
              CHECK(invalid_call &&
                        invalid_call->range.start.offset ==
                            (int)(use - source + name_length) &&
                        invalid_call->range.end.offset ==
                            (int)(use - source + name_length + 1),
                    "incomplete enum header namespace invalid call range");
            }
          } else {
            const char *derived_value = expected_kind ==
                                                AG_LANGUAGE_SYMBOL_MACRO
                                            ? revisions[revision]
                                                  .macro_invocation_value
                                            : revisions[revision].enum_value;
            CHECK(derived && derived->constant_value &&
                      strcmp(derived->constant_value, derived_value) == 0,
                  "incomplete enum header namespace revision derived value");
          }
        }
        ag_language_analysis_snapshot_dispose(&snapshot);
      }
    }
    free(bundle.bytes);
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_project_enum_macro_revision_order(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  ag_language_project_index_t *project =
      ag_language_project_index_create();
  CHECK(session && project, "project enum macro revision session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *header_paths[] = {"project-collision.h"};
  const char *name = "PROJECT_COLLIDING_SYMBOL";
  size_t name_length = strlen(name);
  const size_t default_source_modes[] = {0, 1, 0, 1};
  const size_t spaced_source_modes[] = {0, 1, 2, 1, 2, 0};
  const size_t identifier_argument_source_modes[] = {
      0, 1, 2, 3, 4, 1, 2, 3, 4, 0};
  for (size_t revision = 0;
       revision < sizeof(project_enum_macro_revisions) /
                      sizeof(project_enum_macro_revisions[0]);
       revision++) {
    const project_enum_macro_revision_t *expected =
        &project_enum_macro_revisions[revision];
    const char *header_sources[] = {
        project_enum_macro_headers[expected->header_index]};
    header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
    CHECK(bundle.bytes != NULL, "project enum macro revision bundle");
    CHECK(update_single_source_project(
              session, project, (unsigned int)revision + 1,
              project_enum_macro_index_sources[expected->source_index],
              bundle, defaults, &error) &&
              ag_language_project_index_revision(project) == revision + 1,
          "project enum macro revision update");
    int tests_spaced_call =
        revision == 0 || revision == 3 || revision == 7 ||
        revision == 9 || revision == 11 || revision == 15;
    int tests_identifier_arguments =
        revision == 0 || revision == 3 || revision == 7 ||
        revision == 15;
    const size_t *source_modes =
        tests_identifier_arguments ? identifier_argument_source_modes
        : tests_spaced_call         ? spaced_source_modes
                                    : default_source_modes;
    size_t source_mode_count =
        tests_identifier_arguments
            ? sizeof(identifier_argument_source_modes) /
                  sizeof(identifier_argument_source_modes[0])
        : tests_spaced_call
            ? sizeof(spaced_source_modes) /
                  sizeof(spaced_source_modes[0])
            : sizeof(default_source_modes) /
                  sizeof(default_source_modes[0]);
    for (size_t source_mode_index = 0;
         source_mode_index < source_mode_count;
         source_mode_index++) {
      size_t source_mode = source_modes[source_mode_index];
      int invocation = source_mode != 0;
      if (invocation && !expected->enum_value &&
          !expected->macro_replacement)
        continue;
      const char *source = project_enum_macro_edit_sources[
          expected->source_index][invocation ? 1 : 0];
      char *owned_source = NULL;
      if (source_mode == 2) {
        owned_source = project_enum_macro_spaced_call_source(source, name);
        CHECK(owned_source != NULL,
              "project enum macro spaced call source");
        source = owned_source;
      } else if (source_mode == 3) {
        owned_source = project_enum_macro_identifier_argument_source(
            source, name,
            "enum {\n"
            "  /// project call enum argument\n"
            "  PROJECT_CALL_ENUM_ARGUMENT = 1\n"
            "};\n",
            "PROJECT_CALL_ENUM_ARGUMENT");
        CHECK(owned_source != NULL,
              "project enum macro enum argument source");
        source = owned_source;
      } else if (source_mode == 4) {
        owned_source = project_enum_macro_identifier_argument_source(
            source, name,
            "/// project call macro argument\n"
            "#define PROJECT_CALL_MACRO_ARGUMENT 1\n",
            "PROJECT_CALL_MACRO_ARGUMENT");
        CHECK(owned_source != NULL,
              "project enum macro macro argument source");
        source = owned_source;
      }
      const char *use = last_occurrence(source, name);
      CHECK(use && use > source, "project enum macro revision use anchor");
      size_t use_offset = (size_t)(use - source);
      const char *call_open = invocation
                                  ? strstr(use + name_length, "(")
                                  : NULL;
      const char *argument_name =
          source_mode == 3 ? "PROJECT_CALL_ENUM_ARGUMENT"
          : source_mode == 4 ? "PROJECT_CALL_MACRO_ARGUMENT"
                             : "1";
      size_t argument_length = strlen(argument_name);
      const char *argument =
          call_open ? strstr(call_open + 1, argument_name) : NULL;
      const char *call_close = argument ? strstr(argument + 1, ")") : NULL;
      CHECK(!invocation || (call_open && argument && call_close),
            "project enum macro invocation anchors");
      size_t cursor_offsets[13] = {
          use_offset - 1, use_offset, use_offset + name_length / 2,
          use_offset + name_length, use_offset - 1};
      int cursor_outside[13] = {1, 0, 0, 0, 1};
      size_t cursor_step_count = 5;
      if (invocation) {
        cursor_offsets[cursor_step_count++] = use_offset + name_length;
        cursor_offsets[cursor_step_count++] =
            (size_t)(call_open - source) + 1;
        if (source_mode == 3 || source_mode == 4) {
          cursor_offsets[cursor_step_count++] =
              (size_t)(argument - source);
          cursor_offsets[cursor_step_count++] =
              (size_t)(argument - source) + argument_length / 2;
          cursor_offsets[cursor_step_count++] =
              (size_t)(argument - source) + argument_length;
          cursor_offsets[cursor_step_count++] =
              (size_t)(argument - source) + argument_length + 1;
        } else {
          cursor_offsets[cursor_step_count++] =
              (size_t)(argument - source) + argument_length;
        }
        cursor_offsets[cursor_step_count++] =
            (size_t)(call_close - source) + 1;
        cursor_offsets[cursor_step_count++] = use_offset + name_length;
      }
      for (size_t cursor_index = 0;
           cursor_index < cursor_step_count;
           cursor_index++) {
        size_t cursor_offset = cursor_offsets[cursor_index];
        CHECK(analyze_project_named(
                  session, project, "main.c", source, cursor_offset,
                  bundle, defaults, &snapshot, &error),
              "project enum macro revision analysis");
        CHECK(ag_language_project_index_revision(project) == revision + 1,
              "project enum macro source toggle keeps index revision");
        const ag_language_symbol_t *enum_candidate = find_symbol(
            &snapshot, name, AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
        const ag_language_symbol_t *macro_candidate = find_symbol(
            &snapshot, name, AG_LANGUAGE_SYMBOL_MACRO);
        const char *renamed_name = "PROJECT_RENAMED_SYMBOL";
        const ag_language_symbol_t *renamed_enum_candidate = find_symbol(
            &snapshot, renamed_name, AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
        const char *argument_enum_name = "PROJECT_CALL_ENUM_ARGUMENT";
        const char *argument_macro_name = "PROJECT_CALL_MACRO_ARGUMENT";
        const ag_language_symbol_t *argument_enum_candidate = find_symbol(
            &snapshot, argument_enum_name,
            AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
        const ag_language_symbol_t *argument_macro_candidate = find_symbol(
            &snapshot, argument_macro_name, AG_LANGUAGE_SYMBOL_MACRO);
        CHECK(snapshot.partial &&
                  (!!enum_candidate == !!expected->enum_value) &&
                  (!!macro_candidate == !!expected->macro_replacement) &&
                  (!!renamed_enum_candidate ==
                   !!expected->renamed_enum_value) &&
                  (!!argument_enum_candidate == (source_mode == 3)) &&
                  (!!argument_macro_candidate == (source_mode == 4)) &&
                  snapshot.dependency_count == 1 &&
                  strcmp(snapshot.dependencies[0],
                         "project-collision.h") == 0,
              "project enum macro revision common fields");
        if (enum_candidate) {
          const char *declaration = strstr(source, name);
          const char *comment = strstr(source, expected->enum_comment);
          CHECK(declaration && comment && enum_candidate->constant_value &&
                    strcmp(enum_candidate->constant_value,
                           expected->enum_value) == 0 &&
                    enum_candidate->declaration.source_name &&
                    strcmp(enum_candidate->declaration.source_name,
                           "main.c") == 0 &&
                    enum_candidate->declaration.start.offset ==
                        (int)(declaration - source) &&
                    enum_candidate->declaration.end.offset ==
                        (int)(declaration - source + name_length) &&
                    check_documentation_symbol(
                        enum_candidate, expected->enum_documentation,
                        "main.c", (size_t)(comment - source),
                        (size_t)(comment - source) +
                            strlen(expected->enum_comment)),
                "project enum macro revision enum fields");
        }
        if (renamed_enum_candidate) {
          const char *declaration = strstr(source, renamed_name);
          const char *comment = strstr(
              source, expected->renamed_enum_comment);
          CHECK(declaration && comment &&
                    renamed_enum_candidate->constant_value &&
                    strcmp(renamed_enum_candidate->constant_value,
                           expected->renamed_enum_value) == 0 &&
                    renamed_enum_candidate->declaration.source_name &&
                    strcmp(renamed_enum_candidate->declaration.source_name,
                           "main.c") == 0 &&
                    renamed_enum_candidate->declaration.start.offset ==
                        (int)(declaration - source) &&
                    renamed_enum_candidate->declaration.end.offset ==
                        (int)(declaration - source + strlen(renamed_name)) &&
                    check_documentation_symbol(
                        renamed_enum_candidate,
                        expected->renamed_enum_documentation, "main.c",
                        (size_t)(comment - source),
                        (size_t)(comment - source) +
                            strlen(expected->renamed_enum_comment)),
                "project enum macro revision renamed enum fields");
        }
        if (argument_enum_candidate) {
          const char *declaration = strstr(source, argument_enum_name);
          const char *comment = strstr(
              source, "/// project call enum argument");
          CHECK(declaration && comment &&
                    argument_enum_candidate->constant_value &&
                    strcmp(argument_enum_candidate->constant_value,
                           "1") == 0 &&
                    argument_enum_candidate->declaration.source_name &&
                    strcmp(argument_enum_candidate->declaration.source_name,
                           "main.c") == 0 &&
                    argument_enum_candidate->declaration.start.offset ==
                        (int)(declaration - source) &&
                    argument_enum_candidate->declaration.end.offset ==
                        (int)(declaration - source +
                              strlen(argument_enum_name)) &&
                    check_documentation_symbol(
                        argument_enum_candidate,
                        "project call enum argument", "main.c",
                        (size_t)(comment - source),
                        (size_t)(comment - source) +
                            strlen("/// project call enum argument")),
                "project enum macro enum argument fields");
        }
        if (argument_macro_candidate) {
          const char *declaration = strstr(source, argument_macro_name);
          const char *comment = strstr(
              source, "/// project call macro argument");
          CHECK(declaration && comment &&
                    !argument_macro_candidate->macro_is_function_like &&
                    argument_macro_candidate->macro_replacement &&
                    strcmp(argument_macro_candidate->macro_replacement,
                           "1") == 0 &&
                    argument_macro_candidate->declaration.source_name &&
                    strcmp(argument_macro_candidate->declaration.source_name,
                           "main.c") == 0 &&
                    argument_macro_candidate->declaration.start.offset ==
                        (int)(declaration - source) &&
                    argument_macro_candidate->declaration.end.offset ==
                        (int)(declaration - source +
                              strlen(argument_macro_name)) &&
                    check_documentation_symbol(
                        argument_macro_candidate,
                        "project call macro argument", "main.c",
                        (size_t)(comment - source),
                        (size_t)(comment - source) +
                            strlen("/// project call macro argument")),
                "project enum macro macro argument fields");
        }
        if (macro_candidate) {
          const char *header =
              project_enum_macro_headers[expected->header_index];
          const char *declaration = strstr(header, name);
          const char *comment = strstr(header, expected->macro_comment);
          const char *macro_parameter = expected->macro_parameter
                                            ? expected->macro_parameter
                                            : "value";
          CHECK(declaration && comment &&
                    macro_candidate->macro_is_function_like &&
                    !macro_candidate->macro_is_variadic &&
                    macro_candidate->macro_parameter_count == 1 &&
                    macro_candidate->macro_parameters &&
                    strcmp(macro_candidate->macro_parameters[0],
                           macro_parameter) == 0 &&
                    macro_candidate->macro_replacement &&
                    strcmp(macro_candidate->macro_replacement,
                           expected->macro_replacement) == 0 &&
                    macro_candidate->declaration.source_name &&
                    strcmp(macro_candidate->declaration.source_name,
                           "project-collision.h") == 0 &&
                    macro_candidate->declaration.start.offset ==
                        (int)(declaration - header) &&
                    macro_candidate->declaration.end.offset ==
                        (int)(declaration - header + name_length) &&
                    check_documentation_symbol(
                        macro_candidate, expected->macro_documentation,
                        "project-collision.h",
                        (size_t)(comment - header),
                        (size_t)(comment - header) +
                            strlen(expected->macro_comment)),
                "project enum macro revision macro fields");
        }
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *derived = find_symbol(
            &snapshot, "PROJECT_COLLISION_DERIVED",
            AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
        if (cursor_outside[cursor_index]) {
          CHECK(!hover && !derived && snapshot.diagnostic_count == 0,
                "project enum macro cursor outside fields");
        } else if (!invocation && !expected->enum_value) {
          const ag_language_diagnostic_t *partial =
              find_diagnostic(&snapshot, "AGC_PARTIAL_IDENTIFIER");
          CHECK(!hover && snapshot.diagnostic_count == 1 && partial &&
                    partial->range.start.offset == (int)(use - source) &&
                    partial->range.end.offset ==
                        (int)(use - source + name_length),
                "project enum macro revision bare partial");
        } else {
          ag_language_symbol_kind_t expected_kind =
              invocation && expected->macro_replacement
                  ? AG_LANGUAGE_SYMBOL_MACRO
                  : AG_LANGUAGE_SYMBOL_ENUM_CONSTANT;
          const ag_language_symbol_t *expected_symbol =
              expected_kind == AG_LANGUAGE_SYMBOL_MACRO
                  ? macro_candidate
                  : enum_candidate;
          int cursor_in_argument =
              (source_mode == 3 || source_mode == 4) &&
              cursor_offset >= (size_t)(argument - source) &&
              cursor_offset <=
                  (size_t)(argument - source) + argument_length;
          const ag_language_symbol_t *expected_argument_symbol =
              source_mode == 3 ? argument_enum_candidate
                               : argument_macro_candidate;
          int invalid_enum_invocation =
              invocation && !expected->macro_replacement;
          int cursor_after_name =
              invocation && cursor_offset > use_offset + name_length;
          int expected_diagnostic_count =
              invalid_enum_invocation && call_open &&
                      cursor_offset >= (size_t)(call_open - source)
                  ? 1
                  : 0;
          CHECK(snapshot.diagnostic_count == expected_diagnostic_count &&
                    ((cursor_in_argument && hover &&
                      expected_argument_symbol &&
                      hover->kind == expected_argument_symbol->kind &&
                      same_range(&hover->declaration,
                                 &expected_argument_symbol->declaration)) ||
                     (cursor_after_name && !cursor_in_argument && !hover) ||
                     (!cursor_after_name && hover && expected_symbol &&
                      hover->kind == expected_kind &&
                      same_range(&hover->declaration,
                                 &expected_symbol->declaration))),
                "project enum macro revision hover fields");
          if (invalid_enum_invocation) {
            CHECK(!derived,
                  "project enum macro invalid invocation derived");
            if (expected_diagnostic_count) {
              const ag_language_diagnostic_t *invalid_call =
                  find_diagnostic(&snapshot, "E3102");
              CHECK(invalid_call &&
                        invalid_call->range.start.offset ==
                            (int)(call_open - source) &&
                        invalid_call->range.end.offset ==
                            (int)(call_open - source + 1),
                    "project enum macro invalid invocation range");
            }
          } else {
            const char *derived_value = invocation
                                            ? expected->macro_invocation_value
                                            : expected->enum_value;
            CHECK(derived && derived->constant_value &&
                      strcmp(derived->constant_value,
                             derived_value) == 0,
                  "project enum macro revision derived value");
          }
        }
        ag_language_analysis_snapshot_dispose(&snapshot);
      }
      free(owned_source);
    }
    free(bundle.bytes);
  }
  ag_language_project_index_destroy(project);
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_enum_two_argument_call_cursor(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "enum two argument call session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *header_paths[] = {"enum-two-argument-call.h"};
  const char *callee_name = "ENUM_TWO_ARGUMENT_CALL";
  const char *enum_names[] = {
      callee_name, "ENUM_TWO_ARGUMENT_FIRST", "ENUM_TWO_ARGUMENT_SECOND"};
  const char *enum_values[] = {"7", "1", "2"};
  const char *enum_documentation[] = {
      "enum two argument callee", "enum two argument first",
      "enum two argument second"};
  const char *enum_comments[] = {
      "/// enum two argument callee",
      "/// enum two argument first",
      "/// enum two argument second"};
  const char *argument_macro_names[][3] = {
      {NULL, "ENUM_TWO_ARGUMENT_FIRST_MACRO",
       "ENUM_TWO_ARGUMENT_SECOND_MACRO"},
      {NULL, "ENUM_TWO_ARGUMENT_FIRST_MACRO",
       "ENUM_TWO_ARGUMENT_SECOND_MACRO"},
      {NULL, "ENUM_TWO_ARGUMENT_FIRST_RENAMED_MACRO",
       "ENUM_TWO_ARGUMENT_SECOND_RENAMED_MACRO"},
      {NULL, "ENUM_TWO_ARGUMENT_FIRST_MACRO",
       "ENUM_TWO_ARGUMENT_SECOND_MACRO"}};
  const char *argument_macro_values[][3] = {
      {NULL, "1", "2"}, {NULL, "11", "12"},
      {NULL, "1", "2"}, {NULL, NULL, NULL}};
  const char *argument_macro_documentation[][3] = {
      {NULL, "enum two argument first macro",
       "enum two argument second macro"},
      {NULL, "enum two argument first macro updated",
       "enum two argument second macro updated"},
      {NULL, "enum two argument first renamed macro",
       "enum two argument second renamed macro"},
      {NULL, NULL, NULL}};
  const char *argument_macro_comments[][3] = {
      {NULL, "/// enum two argument first macro",
       "/// enum two argument second macro"},
      {NULL, "/// enum two argument first macro updated",
       "/// enum two argument second macro updated"},
      {NULL, "/// enum two argument first renamed macro",
       "/// enum two argument second renamed macro"},
      {NULL, NULL, NULL}};
  size_t callee_length = strlen(callee_name);
  struct {
    size_t variant;
    int enum_only;
    int argument_mode;
    int argument_revision;
  } passes[] = {
      {0, 0, 0, 0}, {1, 0, 0, 0}, {2, 0, 0, 0}, {3, 0, 0, 0},
      {0, 1, 0, 0}, {1, 1, 0, 0}, {2, 1, 0, 0}, {3, 1, 0, 0},
      {1, 0, 1, 0}, {2, 0, 1, 1}, {3, 0, 1, 0},
      {1, 0, 1, 1}, {2, 0, 1, 0}, {3, 0, 1, 1}, {1, 0, 1, 0},
      {3, 0, 2, 0}, {2, 0, 2, 1}, {1, 0, 2, 0},
      {3, 0, 2, 1}, {2, 0, 2, 0}, {1, 0, 2, 1}, {3, 0, 2, 0},
      {1, 1, 1, 1}, {3, 1, 2, 1},
      {2, 0, 1, 2}, {3, 0, 1, 0}, {1, 0, 1, 2},
      {2, 0, 1, 0}, {3, 0, 1, 2}, {1, 0, 1, 0},
      {2, 0, 2, 2}, {1, 0, 2, 0}, {3, 0, 2, 2},
      {2, 0, 2, 0}, {1, 0, 2, 2}, {3, 0, 2, 0},
      {1, 1, 1, 2}, {3, 1, 2, 2},
      {1, 0, 1, 3}, {2, 0, 1, 0},
      {3, 0, 1, 3}, {1, 0, 1, 0},
      {2, 0, 2, 3}, {1, 0, 2, 0},
      {3, 0, 2, 3}, {2, 0, 2, 0},
      {1, 1, 1, 3}, {3, 1, 2, 3},
      {1, 0, 3, 0}, {2, 0, 3, 1},
      {3, 0, 3, 0}, {1, 0, 3, 2},
      {2, 0, 3, 0}, {3, 0, 3, 1},
      {1, 0, 3, 0}, {1, 1, 3, 0},
      {3, 1, 3, 2},
      {2, 0, 3, 3}, {3, 0, 3, 0},
      {1, 0, 3, 3}, {2, 0, 3, 0},
      {2, 1, 3, 3},
      {1, 0, 3, 3}, {1, 0, 3, 2}, {1, 0, 3, 0},
      {3, 0, 3, 3}, {3, 0, 3, 1}, {3, 0, 3, 0},
      {1, 0, 3, 0}, {1, 0, 3, 1}, {1, 0, 3, 3},
      {1, 0, 3, 2}, {1, 0, 3, 0},
      {2, 0, 3, 0}, {2, 0, 3, 2}, {2, 0, 3, 3},
      {2, 0, 3, 1}, {2, 0, 3, 0},
      {1, 0, 3, 0}, {1, 0, 4, 0}, {1, 0, 4, 1},
      {1, 0, 4, 0}, {1, 0, 3, 0}, {1, 1, 4, 1},
      {3, 0, 3, 0}, {3, 0, 5, 0}, {3, 0, 5, 1},
      {3, 0, 5, 0}, {3, 0, 3, 0}, {3, 1, 5, 1},
      {1, 0, 3, 0}, {1, 0, 6, 0}, {1, 0, 6, 1},
      {1, 0, 6, 0}, {1, 0, 3, 0}, {1, 1, 6, 1},
      {3, 0, 3, 0}, {3, 0, 7, 0}, {3, 0, 7, 1},
      {3, 0, 7, 0}, {3, 0, 3, 0}, {3, 1, 7, 1},
      {1, 0, 3, 0}, {1, 0, 8, 0}, {1, 0, 8, 1},
      {1, 0, 8, 0}, {1, 0, 3, 0}, {1, 1, 8, 1},
      {3, 0, 3, 0}, {3, 0, 9, 0}, {3, 0, 9, 1},
      {3, 0, 9, 0}, {3, 0, 3, 0}, {3, 1, 9, 1},
      {1, 0, 3, 0}, {1, 0, 10, 0}, {1, 0, 10, 1},
      {1, 0, 10, 0}, {1, 0, 10, 2}, {1, 0, 10, 0},
      {1, 0, 3, 0}, {1, 1, 10, 1},
      {3, 0, 3, 0}, {3, 0, 10, 0}, {3, 0, 10, 2},
      {3, 0, 10, 0}, {3, 0, 10, 1}, {3, 0, 10, 0},
      {3, 0, 3, 0}, {3, 1, 10, 2},
      {1, 0, 10, 0}, {1, 0, 10, 3}, {1, 0, 10, 0},
      {1, 1, 10, 3},
      {3, 0, 10, 0}, {3, 0, 10, 3}, {3, 0, 10, 0},
      {3, 1, 10, 3},
      {1, 0, 10, 3}, {1, 0, 10, 2}, {1, 0, 10, 0},
      {3, 0, 10, 3}, {3, 0, 10, 1}, {3, 0, 10, 0},
      {1, 0, 10, 0}, {1, 0, 10, 1}, {1, 0, 10, 3},
      {1, 0, 10, 2}, {1, 0, 10, 0},
      {3, 0, 10, 0}, {3, 0, 10, 2}, {3, 0, 10, 3},
      {3, 0, 10, 1}, {3, 0, 10, 0},
      {0, 0, 0, 0}, {3, 1, 0, 0},
  };
  for (size_t pass_index = 0;
       pass_index < sizeof(passes) / sizeof(passes[0]); pass_index++) {
    size_t variant = passes[pass_index].variant;
    int enum_only = passes[pass_index].enum_only;
    int argument_mode = passes[pass_index].argument_mode;
    int argument_revision = passes[pass_index].argument_revision;
    const char *source = enum_two_argument_call_sources[variant];
    char *owned_source = NULL;
    if (argument_mode == 10) {
      owned_source = enum_two_argument_both_renamed_source(
          source, argument_revision);
      CHECK(owned_source != NULL,
            "enum two argument both renamed source");
      source = owned_source;
    } else if (argument_mode >= 8) {
      owned_source = enum_two_argument_paired_rename_update_source(
          source, argument_mode - 7, argument_revision);
      CHECK(owned_source != NULL,
            "enum two argument paired rename update source");
      source = owned_source;
    } else if (argument_mode >= 6) {
      owned_source = enum_two_argument_paired_update_source(
          source, argument_mode - 5, argument_revision);
      CHECK(owned_source != NULL,
            "enum two argument paired update source");
      source = owned_source;
    } else if (argument_mode >= 4) {
      owned_source = enum_two_argument_paired_rename_source(
          source, argument_mode - 3, argument_revision);
      CHECK(owned_source != NULL,
            "enum two argument paired rename source");
      source = owned_source;
    } else if (argument_mode == 3) {
      owned_source = enum_two_argument_paired_macro_source(
          source, argument_revision);
      CHECK(owned_source != NULL,
            "enum two argument paired macro source");
      source = owned_source;
    } else if (argument_mode > 0) {
      owned_source = enum_two_argument_macro_source(
          source, argument_mode, argument_revision);
      CHECK(owned_source != NULL,
            "enum two argument macro source");
      source = owned_source;
    }
    const char *active_macro_names[] = {
        NULL,
        argument_mode == 1
            ? argument_macro_names[argument_revision][1]
            : argument_mode == 3 || argument_mode == 5 ||
                  argument_mode == 6 || argument_mode == 7 ||
                  argument_mode == 9
                ? argument_macro_names[0][1]
            : argument_mode == 4 || argument_mode == 8 ||
                  argument_mode == 10
                ? argument_macro_names[2][1]
                : NULL,
        argument_mode == 2
            ? argument_macro_names[argument_revision][2]
            : argument_mode == 3 || argument_mode == 4 ||
                  argument_mode == 6 || argument_mode == 7 ||
                  argument_mode == 8
                ? argument_macro_names[0][2]
            : argument_mode == 5 || argument_mode == 9 ||
                  argument_mode == 10
                ? argument_macro_names[2][2]
                : NULL};
    int missing_argument_mode =
        argument_mode == 3
            ? argument_revision
        : argument_mode == 4
            ? argument_revision ? 2 : 0
        : argument_mode == 5
            ? argument_revision ? 1 : 0
        : argument_mode == 6
            ? argument_revision ? 2 : 0
        : argument_mode == 7
            ? argument_revision ? 1 : 0
        : argument_mode == 8
            ? argument_revision ? 2 : 0
        : argument_mode == 9
            ? argument_revision ? 1 : 0
        : argument_mode == 10
            ? argument_revision
            : argument_revision == 3 ? argument_mode : 0;
    int argument_missing[] = {
        0, (missing_argument_mode & 1) != 0,
        (missing_argument_mode & 2) != 0};
    int first_missing_argument_index =
        argument_missing[1] ? 1 : argument_missing[2] ? 2 : 0;
    int argument_metadata_revisions[] = {0, 0, 0};
    if (argument_mode == 1 || argument_mode == 2)
      argument_metadata_revisions[argument_mode] = argument_revision;
    else if (argument_mode == 4)
      argument_metadata_revisions[1] = 2;
    else if (argument_mode == 5)
      argument_metadata_revisions[2] = 2;
    else if (argument_mode == 6)
      argument_metadata_revisions[1] = 1;
    else if (argument_mode == 7)
      argument_metadata_revisions[2] = 1;
    else if (argument_mode == 8) {
      argument_metadata_revisions[1] = 2;
      argument_metadata_revisions[2] = 1;
    } else if (argument_mode == 9) {
      argument_metadata_revisions[1] = 1;
      argument_metadata_revisions[2] = 2;
    } else if (argument_mode == 10) {
      argument_metadata_revisions[1] = 2;
      argument_metadata_revisions[2] = 2;
    }
    const char *argument_names[] = {
        NULL,
        active_macro_names[1]
            ? active_macro_names[1]
            : enum_names[1],
        active_macro_names[2]
            ? active_macro_names[2]
            : enum_names[2]};
    const char *callee_use = last_occurrence(source, callee_name);
    const char *call_open = callee_use
                                ? strchr(callee_use + callee_length, '(')
                                : NULL;
    const char *first_use = call_open
                                ? strstr(call_open + 1, argument_names[1])
                                : NULL;
    const char *comma = first_use
                            ? strchr(first_use + strlen(argument_names[1]), ',')
                            : NULL;
    const char *second_use =
        comma ? strstr(comma + 1, argument_names[2]) : NULL;
    const char *call_close = second_use
                                 ? strchr(second_use +
                                              strlen(argument_names[2]), ')')
                                 : NULL;
    CHECK(callee_use && call_open && first_use && comma && second_use &&
              call_close,
          "enum two argument call anchors");
    size_t callee_start = (size_t)(callee_use - source);
    size_t first_start = (size_t)(first_use - source);
    size_t second_start = (size_t)(second_use - source);
    size_t actual_first_length = strlen(argument_names[1]);
    size_t actual_second_length = strlen(argument_names[2]);
    typedef struct {
      size_t cursor;
      int hover_enum_index;
    } enum_two_argument_cursor_step_t;
    enum_two_argument_cursor_step_t cursor_steps[24] = {
        {callee_start + callee_length / 2, 0},
        {callee_start + callee_length, 0},
        {(size_t)(call_open - source) + 1, -1},
        {first_start, 1},
        {first_start + actual_first_length / 2, 1},
        {first_start + actual_first_length, 1},
        {first_start + actual_first_length + 1, -1},
        {(size_t)(comma - source), -1},
        {(size_t)(comma - source) + 1, -1},
        {second_start - 1, -1},
        {second_start, 2},
        {second_start + actual_second_length / 2, 2},
        {second_start + actual_second_length, 2},
        {second_start + actual_second_length + 1, -1},
        {(size_t)(call_close - source) + 1, -1},
        {callee_start + callee_length / 2, 0},
    };
    size_t cursor_step_count = 16;
    if (variant == 1) {
      const char *before_comment = strstr(
          first_use + actual_first_length, "/* before comma */");
      const char *after_comment = strstr(comma + 1, "/* after comma */");
      CHECK(before_comment && after_comment,
            "enum two argument comment anchors");
      cursor_steps[cursor_step_count++] =
          (enum_two_argument_cursor_step_t){
              (size_t)(before_comment - source) + 4, -1};
      cursor_steps[cursor_step_count++] =
          (enum_two_argument_cursor_step_t){
              (size_t)(after_comment - source) + 4, -1};
    } else if (variant == 2 || variant == 3) {
      const char *first_splice = strchr(
          first_use + actual_first_length, '\\');
      const char *second_splice = comma ? strchr(comma + 1, '\\') : NULL;
      CHECK(first_splice && first_splice < comma && second_splice &&
                second_splice < second_use,
            "enum two argument splice anchors");
      cursor_steps[cursor_step_count++] =
          (enum_two_argument_cursor_step_t){
              (size_t)(first_splice - source), -1};
      cursor_steps[cursor_step_count++] =
          (enum_two_argument_cursor_step_t){
              (size_t)(first_splice - source) + 1, -1};
      cursor_steps[cursor_step_count++] =
          (enum_two_argument_cursor_step_t){
              (size_t)(second_splice - source), -1};
      cursor_steps[cursor_step_count++] =
          (enum_two_argument_cursor_step_t){
              (size_t)(second_splice - source) + 1, -1};
    }
    const char *header_sources[] = {
        enum_only ? "" : enum_two_argument_call_header};
    header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
    CHECK(bundle.bytes != NULL, "enum two argument call bundle");
    for (size_t cursor_index = 0;
         cursor_index < cursor_step_count;
         cursor_index++) {
      size_t cursor = cursor_steps[cursor_index].cursor;
      CHECK(analyze_named(
                session, "enum-two-argument-call.c",
                source, cursor, bundle, defaults,
                &snapshot, &error),
            "enum two argument call analysis");
      CHECK(snapshot.partial && snapshot.dependency_count == 1 &&
                strcmp(snapshot.dependencies[0],
                       "enum-two-argument-call.h") == 0,
            "enum two argument call common fields");
      const ag_language_symbol_t *enum_candidates[3] = {0};
      const char *enum_block = strstr(
          source, "enum EnumTwoArgumentValues {\n");
      for (size_t enum_index = 0; enum_index < 3; enum_index++) {
        enum_candidates[enum_index] = find_symbol(
            &snapshot, enum_names[enum_index],
            AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
        const char *comment = strstr(
            enum_block ? enum_block : source,
            enum_comments[enum_index]);
        const char *declaration = comment
                                      ? strstr(
                                            comment,
                                            enum_names[enum_index])
                                      : NULL;
        CHECK(declaration && comment && enum_candidates[enum_index] &&
                  enum_candidates[enum_index]->constant_value &&
                  strcmp(enum_candidates[enum_index]->constant_value,
                         enum_values[enum_index]) == 0 &&
                  enum_candidates[enum_index]->declaration.source_name &&
                  strcmp(enum_candidates[enum_index]->declaration.source_name,
                         "enum-two-argument-call.c") == 0 &&
                  enum_candidates[enum_index]->declaration.start.offset ==
                      (int)(declaration - source) &&
                  enum_candidates[enum_index]->declaration.end.offset ==
                      (int)(declaration - source +
                            strlen(enum_names[enum_index])) &&
                  check_documentation_symbol(
                      enum_candidates[enum_index],
                      enum_documentation[enum_index],
                      "enum-two-argument-call.c",
                      (size_t)(comment - source),
                      (size_t)(comment - source) +
                          strlen(enum_comments[enum_index])),
              "enum two argument enum fields");
      }
      const ag_language_symbol_t *macro_candidate = find_symbol(
          &snapshot, callee_name, AG_LANGUAGE_SYMBOL_MACRO);
      if (enum_only) {
        CHECK(!macro_candidate, "enum two argument macro removed");
      } else {
        const char *declaration = strstr(
            enum_two_argument_call_header, callee_name);
        const char *comment = strstr(
            enum_two_argument_call_header,
            "/// enum two argument function-like macro");
        CHECK(declaration && comment && macro_candidate &&
                  macro_candidate->macro_is_function_like &&
                  !macro_candidate->macro_is_variadic &&
                  macro_candidate->macro_parameter_count == 2 &&
                  macro_candidate->macro_parameters &&
                  strcmp(macro_candidate->macro_parameters[0], "left") == 0 &&
                  strcmp(macro_candidate->macro_parameters[1], "right") == 0 &&
                  macro_candidate->macro_replacement &&
                  strcmp(macro_candidate->macro_replacement,
                         "( ( left ) + ( right ) + 100 )") == 0 &&
                  macro_candidate->declaration.source_name &&
                  strcmp(macro_candidate->declaration.source_name,
                         "enum-two-argument-call.h") == 0 &&
                  macro_candidate->declaration.start.offset ==
                      (int)(declaration - enum_two_argument_call_header) &&
                  macro_candidate->declaration.end.offset ==
                      (int)(declaration - enum_two_argument_call_header +
                            callee_length) &&
                  check_documentation_symbol(
                      macro_candidate,
                      "enum two argument function-like macro",
                      "enum-two-argument-call.h",
                      (size_t)(comment - enum_two_argument_call_header),
                      (size_t)(comment - enum_two_argument_call_header) +
                          strlen("/// enum two argument function-like macro")),
              "enum two argument macro fields");
      }
      const ag_language_symbol_t *argument_macro_candidates[3] = {0};
      for (int macro_index = 1; macro_index < 3; macro_index++) {
        const char *argument_macro_name =
            active_macro_names[macro_index];
        if (!argument_macro_name) continue;
        argument_macro_candidates[macro_index] = find_symbol(
            &snapshot, argument_macro_name, AG_LANGUAGE_SYMBOL_MACRO);
        if (argument_missing[macro_index]) {
          CHECK(!argument_macro_candidates[macro_index],
                "enum two argument deleted macro removed");
          continue;
        }
        const char *declaration = strstr(
            source, argument_macro_name);
        const char *comment = strstr(
            source,
            argument_macro_comments[argument_metadata_revisions[macro_index]]
                                    [macro_index]);
        CHECK(declaration && comment &&
                  argument_macro_candidates[macro_index] &&
                  !argument_macro_candidates[macro_index]
                       ->macro_is_function_like &&
                  !argument_macro_candidates[macro_index]
                       ->macro_is_variadic &&
                  argument_macro_candidates[macro_index]
                          ->macro_parameter_count == 0 &&
                  argument_macro_candidates[macro_index]
                      ->macro_replacement &&
                  strcmp(argument_macro_candidates[macro_index]
                             ->macro_replacement,
                         argument_macro_values
                             [argument_metadata_revisions[macro_index]]
                                              [macro_index]) == 0 &&
                  argument_macro_candidates[macro_index]
                      ->declaration.source_name &&
                  strcmp(argument_macro_candidates[macro_index]
                             ->declaration.source_name,
                         "enum-two-argument-call.c") == 0 &&
                  argument_macro_candidates[macro_index]
                          ->declaration.start.offset ==
                      (int)(declaration - source) &&
                  argument_macro_candidates[macro_index]
                          ->declaration.end.offset ==
                      (int)(declaration - source +
                            strlen(argument_macro_name)) &&
                  check_documentation_symbol(
                      argument_macro_candidates[macro_index],
                      argument_macro_documentation
                          [argument_metadata_revisions[macro_index]]
                                                  [macro_index],
                      "enum-two-argument-call.c",
                      (size_t)(comment - source),
                      (size_t)(comment - source) +
                          strlen(argument_macro_comments
                                     [argument_metadata_revisions[macro_index]]
                                     [macro_index])),
              "enum two argument object macro fields");
      }
      if (argument_mode > 0) {
        for (size_t revision_index = 0; revision_index < 4;
             revision_index++) {
          for (size_t mode_index = 1; mode_index < 3; mode_index++) {
            const char *inactive_name =
                argument_macro_names[revision_index][mode_index];
            int active = 0;
            for (int macro_index = 1; macro_index < 3; macro_index++)
              if (!argument_missing[macro_index] &&
                  active_macro_names[macro_index] &&
                  strcmp(inactive_name,
                         active_macro_names[macro_index]) == 0)
                active = 1;
            if (!active)
              CHECK(!find_symbol(
                        &snapshot, inactive_name,
                        AG_LANGUAGE_SYMBOL_MACRO),
                    "enum two argument inactive macro removed");
          }
        }
      }
      const ag_language_symbol_t *derived = find_symbol(
          &snapshot, "ENUM_TWO_ARGUMENT_DERIVED",
          AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
      int argument_updated =
          argument_metadata_revisions[1] == 1 ||
          argument_metadata_revisions[2] == 1;
      CHECK(((enum_only || first_missing_argument_index) && !derived) ||
                (!enum_only && !first_missing_argument_index && derived &&
                 derived->constant_value &&
                 strcmp(derived->constant_value,
                        argument_updated ? "113" : "103") == 0),
            "enum two argument derived value");
      int expected_diagnostic_count =
          (!enum_only && first_missing_argument_index) ||
          (enum_only && cursor >= (size_t)(call_open - source));
      CHECK(snapshot.diagnostic_count == expected_diagnostic_count,
            "enum two argument diagnostics count");
      if (!enum_only && first_missing_argument_index) {
        const ag_language_diagnostic_t *undefined_argument =
            find_diagnostic(&snapshot, "E3066");
        CHECK(undefined_argument &&
                  undefined_argument->message &&
                  strstr(undefined_argument->message,
                         active_macro_names[first_missing_argument_index]) &&
                  (!argument_missing[1] || !argument_missing[2] ||
                   !strstr(undefined_argument->message,
                           active_macro_names[2])) &&
                  undefined_argument->range.start.offset ==
                      (int)(callee_use - source) &&
                  undefined_argument->range.end.offset ==
                      (int)(call_open - source),
              "enum two argument undefined macro range");
      } else if (expected_diagnostic_count) {
        const ag_language_diagnostic_t *invalid_call =
            find_diagnostic(&snapshot, "E3102");
        CHECK(invalid_call &&
                  invalid_call->range.start.offset ==
                      (int)(call_open - source) &&
                  invalid_call->range.end.offset ==
                      (int)(call_open - source + 1),
              "enum two argument invalid call range");
      }
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      int hover_enum_index = cursor_steps[cursor_index].hover_enum_index;
      const ag_language_symbol_t *expected_hover =
          hover_enum_index < 0
              ? NULL
          : hover_enum_index == 0 && !enum_only
              ? macro_candidate
          : hover_enum_index > 0 &&
                active_macro_names[hover_enum_index]
              ? argument_macro_candidates[hover_enum_index]
              : enum_candidates[hover_enum_index];
      CHECK((!expected_hover && !hover) ||
                (expected_hover && hover &&
                 hover->kind == expected_hover->kind &&
                 same_range(&hover->declaration,
                            &expected_hover->declaration)),
            "enum two argument hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
    free(bundle.bytes);
    free(owned_source);
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_enum_three_argument_call_cursor(
    ag_target_info_t target) {
  enum {
    ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET = 12,
    ENUM_THREE_ARGUMENT_HEADER_REVISION_COUNT =
        sizeof(enum_three_argument_call_headers) /
        sizeof(enum_three_argument_call_headers[0])
  };
  static const char *const header_parameter_names[][3] = {
      {"first", "middle", "last"},
      {"left", "center", "right"},
      {"lhs", "mid", "rhs"}};
  static const char *const header_replacements[] = {
      "( ( first ) + ( middle ) + ( last ) + 100 )",
      "( ( left ) + ( center ) + ( right ) + 200 )",
      "( ( lhs ) + ( mid ) + ( rhs ) + 100 )"};
  static const char *const header_documentation[] = {
      "enum three argument function-like macro",
      "enum three argument updated function-like macro",
      "enum three argument metadata-only function-like macro"};
  static const char *const header_comments[] = {
      "/// enum three argument function-like macro",
      "/// enum three argument updated function-like macro",
      "/// enum three argument metadata-only function-like macro"};
  static const char *const macro_argument_names[] = {
      "ENUM_THREE_ARGUMENT_FIRST_MACRO",
      "ENUM_THREE_ARGUMENT_MIDDLE_MACRO",
      "ENUM_THREE_ARGUMENT_LAST_MACRO"};
  static const char *const mixed_argument_names[] = {
      "ENUM_THREE_ARGUMENT_FIRST_ENUM",
      "ENUM_THREE_ARGUMENT_MIDDLE_MACRO",
      "ENUM_THREE_ARGUMENT_LAST_ENUM"};
  static const char *const middle_enum_argument_names[] = {
      "ENUM_THREE_ARGUMENT_FIRST_MACRO",
      "ENUM_THREE_ARGUMENT_MIDDLE_ENUM",
      "ENUM_THREE_ARGUMENT_LAST_MACRO"};
  static const char *const renamed_middle_enum_argument_names[] = {
      "ENUM_THREE_ARGUMENT_FIRST_MACRO",
      "ENUM_THREE_ARGUMENT_RENAMED_MIDDLE_ENUM",
      "ENUM_THREE_ARGUMENT_LAST_MACRO"};
  static const char *const macro_argument_values[] = {"1", "2", "3"};
  static const char *const mixed_argument_values[] = {"4", "2", "5"};
  static const char *const middle_enum_argument_values[] = {"1", "6", "3"};
  static const char *const updated_middle_enum_argument_values[] = {
      "1", "9", "3"};
  static const char *const updated_macro_argument_values[] = {
      "7", "9", "11"};
  static const char *const macro_argument_documentation[] = {
      "enum three argument first macro",
      "enum three argument middle macro",
      "enum three argument last macro"};
  static const char *const mixed_argument_documentation[] = {
      "enum three argument first enum",
      "enum three argument middle macro",
      "enum three argument last enum"};
  static const char *const middle_enum_argument_documentation[] = {
      "enum three argument first macro",
      "enum three argument middle enum",
      "enum three argument last macro"};
  static const char *const updated_middle_enum_argument_documentation[] = {
      "enum three argument first macro",
      "enum three argument updated middle enum",
      "enum three argument last macro"};
  static const char *const macro_argument_comments[] = {
      "/// enum three argument first macro",
      "/// enum three argument middle macro",
      "/// enum three argument last macro"};
  static const char *const mixed_argument_comments[] = {
      "/// enum three argument first enum",
      "/// enum three argument middle macro",
      "/// enum three argument last enum"};
  static const char *const middle_enum_argument_comments[] = {
      "/// enum three argument first macro",
      "/// enum three argument middle enum",
      "/// enum three argument last macro"};
  static const char *const updated_middle_enum_argument_comments[] = {
      "/// enum three argument first macro",
      "/// enum three argument updated middle enum",
      "/// enum three argument last macro"};
  static const struct {
    size_t variant;
    int missing_argument_mode;
  } passes[] = {
      {0, 0}, {0, 2}, {0, 0}, {0, 4}, {0, 0},
      {1, 0}, {1, 4}, {1, 6}, {1, 2}, {1, 0}, {0, 0},
      {2, 0}, {2, 2}, {2, 0},
      {3, 0}, {3, 2}, {3, 0}, {2, 0},
      {4, 0}, {4, 1}, {4, 5}, {4, 4}, {4, 0},
      {5, 0}, {5, 4}, {5, 5}, {5, 1}, {5, 0}, {4, 0},
      {6, 0}, {6, 1}, {6, 5}, {6, 4}, {6, 0}, {4, 0},
      {5, 0}, {7, 0}, {7, 4}, {7, 5}, {7, 1}, {7, 0}, {5, 0}, {4, 0},
      {6, 0}, {6, 1}, {8, 1}, {8, 5}, {8, 4}, {8, 1}, {8, 0}, {6, 0},
      {7, 0}, {7, 4}, {9, 4}, {9, 5}, {9, 1}, {9, 4}, {9, 0}, {7, 0},
      {6, 0},
      {8, 0}, {8, 1}, {10, 1}, {10, 5}, {10, 4}, {10, 1}, {10, 0},
      {8, 0},
      {9, 0}, {9, 4}, {11, 4}, {11, 5}, {11, 1}, {11, 4}, {11, 0},
      {9, 0}, {8, 0},
      {10, 0}, {10, 2}, {10, 0},
      {11, 0}, {11, 2}, {11, 0}, {10, 0},
      {10, 0}, {10, 1}, {10, 3}, {10, 2}, {10, 6}, {10, 4}, {10, 0},
      {11, 0}, {11, 4}, {11, 6}, {11, 2}, {11, 3}, {11, 1}, {11, 0},
      {10, 0}, {10, 1}, {10, 3}, {10, 7}, {10, 6}, {10, 4}, {10, 0},
      {11, 0}, {11, 4}, {11, 6}, {11, 7}, {11, 3}, {11, 1}, {11, 0},
      {8, 0}, {8, 7}, {10, 7}, {10, 6}, {10, 4}, {10, 0}, {8, 0},
      {9, 0}, {9, 7}, {11, 7}, {11, 3}, {11, 1}, {11, 0}, {9, 0},
      {6, 0}, {6, 7}, {8, 7}, {8, 6}, {8, 4}, {8, 0}, {6, 0},
      {7, 0}, {7, 7}, {9, 7}, {9, 3}, {9, 1}, {9, 0}, {7, 0}, {6, 0},
      {6, 0}, {6, 7}, {10, 7}, {10, 6}, {10, 4}, {10, 0}, {6, 0},
      {7, 0}, {7, 7}, {11, 7}, {11, 3}, {11, 1}, {11, 0}, {7, 0}, {6, 0},
      {4, 0}, {4, 7}, {6, 7}, {6, 6}, {6, 4}, {6, 0}, {4, 0},
      {5, 0}, {5, 7}, {7, 7}, {7, 3}, {7, 1}, {7, 0}, {5, 0}, {4, 0},
      {4, 0}, {4, 7}, {8, 7}, {8, 6}, {8, 4}, {8, 0}, {4, 0},
      {5, 0}, {5, 7}, {9, 7}, {9, 3}, {9, 1}, {9, 0}, {5, 0}, {4, 0},
      {4, 0}, {4, 7}, {10, 7}, {10, 6}, {10, 4}, {10, 0}, {4, 0},
      {5, 0}, {5, 7}, {11, 7}, {11, 3}, {11, 1}, {11, 0}, {5, 0}, {4, 0},
      {4, 0}, {4, 7}, {10, 7}, {10, 0}, {10, 7}, {4, 7}, {4, 0},
      {5, 0}, {5, 7}, {11, 7}, {11, 0}, {11, 7}, {5, 7}, {5, 0}, {4, 0},
      {10, 0}, {4, 0}, {10, 0},
      {5, 0}, {11, 0}, {5, 0}, {11, 0}, {4, 0}, {10, 0},
      {4, 0}, {11, 0}, {4, 0}, {11, 0},
      {5, 0}, {10, 0}, {5, 0}, {10, 0}, {4, 0}, {10, 0},
      {4, 0}, {11, 7}, {11, 0}, {4, 0},
      {5, 0}, {10, 7}, {10, 0}, {5, 0}, {4, 0}, {10, 0},
      {10, 0}, {5, 7}, {5, 0}, {10, 0},
      {11, 0}, {4, 7}, {4, 0}, {11, 0}, {4, 0}, {10, 0},
      {4, 7}, {11, 7}, {4, 7}, {11, 7},
      {5, 7}, {10, 7}, {5, 7}, {10, 7}, {4, 0}, {10, 0},
      {4, 6}, {11, 6}, {4, 6}, {11, 6},
      {5, 6}, {10, 6}, {5, 6}, {10, 6}, {4, 0}, {10, 0},
      {4, 5}, {11, 5}, {4, 5}, {11, 5},
      {5, 5}, {10, 5}, {5, 5}, {10, 5}, {4, 0}, {10, 0},
      {4, 3}, {11, 3}, {4, 3}, {11, 3},
      {5, 3}, {10, 3}, {5, 3}, {10, 3}, {4, 0}, {10, 0},
      {4, 4}, {11, 4}, {4, 4}, {11, 4},
      {5, 4}, {10, 4}, {5, 4}, {10, 4}, {4, 0}, {10, 0},
      {4, 2}, {11, 2}, {4, 2}, {11, 2},
      {5, 2}, {10, 2}, {5, 2}, {10, 2}, {4, 0}, {10, 0},
      {4, 1}, {11, 1}, {4, 1}, {11, 1},
      {5, 1}, {10, 1}, {5, 1}, {10, 1}, {4, 0}, {10, 0},
      {4, 1}, {11, 2}, {4, 4}, {11, 1}, {4, 2}, {11, 4}, {4, 1},
      {5, 1}, {10, 2}, {5, 4}, {10, 1}, {5, 2}, {10, 4}, {5, 1},
      {4, 0}, {10, 0},
      {4, 4}, {11, 2}, {4, 1}, {11, 4}, {4, 2}, {11, 1}, {4, 4},
      {5, 4}, {10, 2}, {5, 1}, {10, 4}, {5, 2}, {10, 1}, {5, 4},
      {4, 0}, {10, 0},
      {4, 3}, {11, 6}, {4, 5}, {11, 3}, {4, 6}, {11, 5}, {4, 3},
      {5, 3}, {10, 6}, {5, 5}, {10, 3}, {5, 6}, {10, 5}, {5, 3},
      {4, 0}, {10, 0},
      {4, 5}, {11, 6}, {4, 3}, {11, 5}, {4, 6}, {11, 3}, {4, 5},
      {5, 5}, {10, 6}, {5, 3}, {10, 5}, {5, 6}, {10, 3}, {5, 5},
      {4, 0}, {11, 1}, {4, 3}, {11, 2}, {4, 6},
      {11, 7}, {4, 5}, {11, 4}, {4, 0},
      {5, 0}, {10, 1}, {5, 3}, {10, 2}, {5, 6},
      {10, 7}, {5, 5}, {10, 4}, {5, 0},
      {4, 0}, {11, 4}, {4, 5}, {11, 7}, {4, 6},
      {11, 2}, {4, 3}, {11, 1}, {4, 0},
      {5, 0}, {10, 4}, {5, 5}, {10, 7}, {5, 6},
      {10, 2}, {5, 3}, {10, 1}, {5, 0},
      {4, 1}, {11, 3}, {4, 2}, {11, 6}, {4, 7},
      {11, 5}, {4, 4}, {11, 0}, {4, 1},
      {5, 1}, {10, 3}, {5, 2}, {10, 6}, {5, 7},
      {10, 5}, {5, 4}, {10, 0}, {5, 1},
      {4, 1}, {11, 0}, {4, 4}, {11, 5}, {4, 7},
      {11, 6}, {4, 2}, {11, 3}, {4, 1},
      {5, 1}, {10, 0}, {5, 4}, {10, 5}, {5, 7},
      {10, 6}, {5, 2}, {10, 3}, {5, 1},
      {4, 0}, {10, 0},
      {4, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 0},
      {11, 0}, {4, 0},
      {5, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 0},
      {10, 0}, {5, 0},
      {4, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 2},
      {11, 2}, {4, 2},
      {5, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 2},
      {10, 2}, {5, 2},
      {6, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {10, 7}, {6, 7},
      {7, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {11, 7}, {7, 7},
      {6, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {6, 0},
      {7, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {7, 0},
      {10, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {10, 0},
      {11, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {11, 0},
      {6, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {6, 0},
      {7, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {7, 0},
      {10, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {10, 0},
      {11, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {11, 0},
      {6, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 2},
      {6, 2},
      {7, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 2},
      {7, 2},
      {10, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {10, 2},
      {11, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {11, 2},
      {6, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 1},
      {6, 1},
      {7, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 1},
      {7, 1},
      {10, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 1},
      {10, 1},
      {11, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 1},
      {11, 1},
      {6, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 4},
      {6, 4},
      {7, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 4},
      {7, 4},
      {10, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 4},
      {10, 4},
      {11, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 4},
      {11, 4},
      {6, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 3},
      {6, 3},
      {7, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 3},
      {7, 3},
      {10, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 3},
      {10, 3},
      {11, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 3},
      {11, 3},
      {6, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 5},
      {6, 5},
      {7, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 5},
      {7, 5},
      {10, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 5},
      {10, 5},
      {11, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 5},
      {11, 5},
      {6, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 6},
      {6, 6},
      {7, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 6},
      {7, 6},
      {10, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 6},
      {10, 6},
      {11, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 6},
      {11, 6},
      {6, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {6, 7},
      {7, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {7, 7},
      {10, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {10, 7},
      {11, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {11, 7},
      {6, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {6, 0},
      {7, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {7, 0},
      {10, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {10, 0},
      {11, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {11, 0},
      {6, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 2},
      {6, 2},
      {7, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 2},
      {7, 2},
      {10, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {10, 2},
      {11, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {11, 2},
      {6, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 1},
      {6, 1},
      {7, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 1},
      {7, 1},
      {10, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 1},
      {10, 1},
      {11, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 1},
      {11, 1},
      {6, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 4},
      {6, 4},
      {7, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 4},
      {7, 4},
      {10, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 4},
      {10, 4},
      {11, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 4},
      {11, 4},
      {6, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 3},
      {6, 3},
      {7, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 3},
      {7, 3},
      {10, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 3},
      {10, 3},
      {11, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 3},
      {11, 3},
      {6, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 5},
      {6, 5},
      {7, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 5},
      {7, 5},
      {10, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 5},
      {10, 5},
      {11, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 5},
      {11, 5},
      {6, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 6},
      {6, 6},
      {7, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 6},
      {7, 6},
      {10, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 6},
      {10, 6},
      {11, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 6},
      {11, 6},
      {6, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {6, 7},
      {7, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {7, 7},
      {10, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {10, 7},
      {11, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {11, 7},
      {6, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {6, 0},
      {7, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {7, 0},
      {10, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {10, 0},
      {11, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {11, 0},
      {6, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 2},
      {6, 2},
      {7, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 2},
      {7, 2},
      {10, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {10, 2},
      {11, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {11, 2},
      {6, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 1},
      {6, 1},
      {7, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 1},
      {7, 1},
      {10, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 1},
      {10, 1},
      {11, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 1},
      {11, 1},
      {6, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 4},
      {6, 4},
      {7, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 4},
      {7, 4},
      {10, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 4},
      {10, 4},
      {11, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 4},
      {11, 4},
      {6, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 3},
      {6, 3},
      {7, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 3},
      {7, 3},
      {10, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 3},
      {10, 3},
      {11, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 3},
      {11, 3},
      {6, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 5},
      {6, 5},
      {7, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 5},
      {7, 5},
      {10, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 5},
      {10, 5},
      {11, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 5},
      {11, 5},
      {6, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 6},
      {6, 6},
      {7, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 6},
      {7, 6},
      {10, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 6},
      {10, 6},
      {11, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 6},
      {11, 6},
      {6, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {6, 7},
      {7, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {7, 7},
      {10, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {10, 7},
      {11, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {11, 7},
      {6, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {6, 0},
      {7, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {7, 0},
      {10, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {10, 0},
      {11, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {11, 0},
      {6, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 2},
      {6, 2},
      {7, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 2},
      {7, 2},
      {10, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {10, 2},
      {11, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {11, 2},
      {6, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 1},
      {6, 1},
      {7, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 1},
      {7, 1},
      {10, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 1},
      {10, 1},
      {11, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 1},
      {11, 1},
      {6, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 4},
      {6, 4},
      {7, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 4},
      {7, 4},
      {10, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 4},
      {10, 4},
      {11, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 4},
      {11, 4},
      {6, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 3},
      {6, 3},
      {7, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 3},
      {7, 3},
      {10, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 3},
      {10, 3},
      {11, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 3},
      {11, 3},
      {6, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 5},
      {6, 5},
      {7, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 5},
      {7, 5},
      {10, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 5},
      {10, 5},
      {11, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 5},
      {11, 5},
      {6, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 6},
      {6, 6},
      {7, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 6},
      {7, 6},
      {10, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 6},
      {10, 6},
      {11, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 6},
      {11, 6},
      {6, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {6, 7},
      {7, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {7, 7},
      {10, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {10, 7},
      {11, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {11, 7},
      {6, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 0},
      {6, 0},
      {7, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 0},
      {7, 0},
      {10, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 0},
      {10, 0},
      {11, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 0},
      {11, 0},
      {6, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 2},
      {6, 2},
      {7, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 2},
      {7, 2},
      {10, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 2},
      {10, 2},
      {11, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 2},
      {11, 2},
      {6, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 1},
      {6, 1},
      {7, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 1},
      {7, 1},
      {10, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 1},
      {10, 1},
      {11, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 1},
      {11, 1},
      {6, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 4},
      {6, 4},
      {7, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 4},
      {7, 4},
      {10, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 4},
      {10, 4},
      {11, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 4},
      {11, 4},
      {6, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 3},
      {6, 3},
      {7, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 3},
      {7, 3},
      {10, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 3},
      {10, 3},
      {11, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 3},
      {11, 3},
      {6, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 5},
      {6, 5},
      {7, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 5},
      {7, 5},
      {10, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 5},
      {10, 5},
      {11, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 5},
      {11, 5},
      {6, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 6},
      {6, 6},
      {7, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 6},
      {7, 6},
      {10, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 6},
      {10, 6},
      {11, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 6},
      {11, 6},
      {6, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 6, 7},
      {6, 7},
      {7, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 7, 7},
      {7, 7},
      {10, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 10, 7},
      {10, 7},
      {11, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 11, 7},
      {11, 7},
      {4, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 0},
      {4, 0},
      {5, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 0},
      {5, 0},
      {8, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 0},
      {8, 0},
      {9, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 0},
      {9, 0},
      {4, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 2},
      {4, 2},
      {5, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 2},
      {5, 2},
      {8, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 2},
      {8, 2},
      {9, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 2},
      {9, 2},
      {4, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 1},
      {4, 1},
      {5, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 1},
      {5, 1},
      {8, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 1},
      {8, 1},
      {9, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 1},
      {9, 1},
      {4, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 4},
      {4, 4},
      {5, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 4},
      {5, 4},
      {8, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 4},
      {8, 4},
      {9, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 4},
      {9, 4},
      {4, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 3},
      {4, 3},
      {5, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 3},
      {5, 3},
      {8, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 3},
      {8, 3},
      {9, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 3},
      {9, 3},
      {4, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 5},
      {4, 5},
      {5, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 5},
      {5, 5},
      {8, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 5},
      {8, 5},
      {9, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 5},
      {9, 5},
      {4, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 6},
      {4, 6},
      {5, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 6},
      {5, 6},
      {8, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 6},
      {8, 6},
      {9, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 6},
      {9, 6},
      {4, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 4, 7},
      {4, 7},
      {5, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 5, 7},
      {5, 7},
      {8, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 8, 7},
      {8, 7},
      {9, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 9, 7},
      {9, 7},
      {0, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 0},
      {0, 0},
      {1, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 0},
      {1, 0},
      {2, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 0},
      {2, 0},
      {3, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 0},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 0},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 0},
      {3, 0},
      {0, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 2},
      {0, 2},
      {1, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 2},
      {1, 2},
      {2, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 2},
      {2, 2},
      {3, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 2},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 2},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 2},
      {3, 2},
      {0, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 1},
      {0, 1},
      {1, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 1},
      {1, 1},
      {2, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 1},
      {2, 1},
      {3, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 1},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 1},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 1},
      {3, 1},
      {0, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 4},
      {0, 4},
      {1, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 4},
      {1, 4},
      {2, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 4},
      {2, 4},
      {3, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 4},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 4},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 4},
      {3, 4},
      {0, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 3},
      {0, 3},
      {1, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 3},
      {1, 3},
      {2, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 3},
      {2, 3},
      {3, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 3},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 3},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 3},
      {3, 3},
      {0, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 6},
      {0, 6},
      {1, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 6},
      {1, 6},
      {2, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 6},
      {2, 6},
      {3, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 6},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 6},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 6},
      {3, 6},
      {0, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 5},
      {0, 5},
      {1, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 5},
      {1, 5},
      {2, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 5},
      {2, 5},
      {3, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 5},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 5},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 5},
      {3, 5},
      {0, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET, 7},
      {0, 7},
      {1, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 1, 7},
      {1, 7},
      {2, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 2, 7},
      {2, 7},
      {3, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 7},
      {2 * ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 7},
      {ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET + 3, 7},
      {3, 7},
      {4, 0}, {10, 0}};
  const char *callee_name = "ENUM_THREE_ARGUMENT_CALL";
  size_t callee_length = strlen(callee_name);
  CHECK(sizeof(enum_three_argument_call_sources) /
                sizeof(enum_three_argument_call_sources[0]) ==
            ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET,
        "enum three argument header revision variant offset");
  const char *header_paths[] = {"enum-three-argument-call.h"};
  header_bundle_t bundles[ENUM_THREE_ARGUMENT_HEADER_REVISION_COUNT] = {0};
  for (size_t header_revision = 0;
       header_revision < ENUM_THREE_ARGUMENT_HEADER_REVISION_COUNT;
       header_revision++) {
    const char *header_sources[] = {
        enum_three_argument_call_headers[header_revision]};
    bundles[header_revision] = make_bundle(
        header_paths, header_sources, 1);
    CHECK(bundles[header_revision].bytes != NULL,
          "enum three argument call bundle");
  }
  static const size_t all_missing_revision_pairs[][2] = {
      {6, 8}, {7, 9}, {8, 10}, {9, 11}, {6, 10}, {7, 11}};
  for (size_t pair_index = 0;
       pair_index < sizeof(all_missing_revision_pairs) /
                        sizeof(all_missing_revision_pairs[0]);
       pair_index++) {
    size_t old_variant = all_missing_revision_pairs[pair_index][0];
    size_t updated_variant = all_missing_revision_pairs[pair_index][1];
    char *old_all_missing = enum_three_argument_macro_source(
        enum_three_argument_call_sources[old_variant], 7);
    char *updated_all_missing = enum_three_argument_macro_source(
        enum_three_argument_call_sources[updated_variant], 7);
    int same_all_missing_source =
        old_all_missing && updated_all_missing &&
        strcmp(old_all_missing, updated_all_missing) == 0;
    free(old_all_missing);
    free(updated_all_missing);
    CHECK(same_all_missing_source,
          "enum three argument all missing revision source identity");
  }
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "enum three argument call session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  for (size_t pass_index = 0;
       pass_index < sizeof(passes) / sizeof(passes[0]); pass_index++) {
    size_t encoded_variant = passes[pass_index].variant;
    int header_revision =
        (int)(encoded_variant /
              ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET);
    size_t variant =
        encoded_variant %
        ENUM_THREE_ARGUMENT_HEADER_REVISION_VARIANT_OFFSET;
    CHECK(header_revision >= 0 &&
              header_revision <
                  ENUM_THREE_ARGUMENT_HEADER_REVISION_COUNT,
          "enum three argument header revision");
    int missing_argument_mode = passes[pass_index].missing_argument_mode;
    const char *header = enum_three_argument_call_headers[header_revision];
    const char *callee_declaration = strstr(header, callee_name);
    const char *callee_comment = strstr(
        header, header_comments[header_revision]);
    CHECK(callee_declaration && callee_comment,
          "enum three argument header anchors");
    int outer_enum_arguments = variant >= 2 && variant < 4;
    int middle_enum_argument = variant >= 4;
    int renamed_middle_enum_argument = variant >= 6;
    int updated_middle_enum_argument = variant >= 8;
    int updated_macro_arguments = variant >= 10;
    const char *const *argument_names =
        renamed_middle_enum_argument ? renamed_middle_enum_argument_names
        : middle_enum_argument ? middle_enum_argument_names
        : outer_enum_arguments ? mixed_argument_names
                               : macro_argument_names;
    const char *const *argument_values =
        updated_macro_arguments ? updated_macro_argument_values
        : updated_middle_enum_argument ? updated_middle_enum_argument_values
        : middle_enum_argument ? middle_enum_argument_values
        : outer_enum_arguments ? mixed_argument_values
                               : macro_argument_values;
    const char *const *argument_documentation =
        updated_middle_enum_argument
            ? updated_middle_enum_argument_documentation
        : middle_enum_argument ? middle_enum_argument_documentation
        : outer_enum_arguments ? mixed_argument_documentation
                               : macro_argument_documentation;
    const char *const *argument_comments =
        updated_middle_enum_argument ? updated_middle_enum_argument_comments
        : middle_enum_argument ? middle_enum_argument_comments
        : outer_enum_arguments ? mixed_argument_comments
                               : macro_argument_comments;
    char *source = enum_three_argument_macro_source(
        enum_three_argument_call_sources[variant], missing_argument_mode);
    CHECK(source != NULL, "enum three argument source");
    const char *callee_use = last_occurrence(source, callee_name);
    const char *call_open = callee_use
                                ? strchr(callee_use + callee_length, '(')
                                : NULL;
    const char *argument_uses[3] = {0};
    const char *commas[2] = {0};
    argument_uses[0] = call_open
                           ? strstr(call_open + 1, argument_names[0])
                           : NULL;
    commas[0] = argument_uses[0]
                    ? strchr(argument_uses[0] + strlen(argument_names[0]), ',')
                    : NULL;
    argument_uses[1] = commas[0]
                           ? strstr(commas[0] + 1, argument_names[1])
                           : NULL;
    commas[1] = argument_uses[1]
                    ? strchr(argument_uses[1] + strlen(argument_names[1]), ',')
                    : NULL;
    argument_uses[2] = commas[1]
                           ? strstr(commas[1] + 1, argument_names[2])
                           : NULL;
    const char *call_close = argument_uses[2]
                                 ? strchr(argument_uses[2] +
                                              strlen(argument_names[2]), ')')
                                 : NULL;
    CHECK(callee_use && call_open && argument_uses[0] && commas[0] &&
              argument_uses[1] && commas[1] && argument_uses[2] &&
              call_close,
          "enum three argument call anchors");
    typedef struct {
      size_t cursor;
      int hover_index;
    } enum_three_argument_cursor_step_t;
    enum_three_argument_cursor_step_t cursor_steps[24] = {
        {(size_t)(callee_use - source) + callee_length / 2, -2},
        {(size_t)(call_open - source) + 1, 0},
        {(size_t)(argument_uses[0] - source), 0},
        {(size_t)(argument_uses[0] - source) + strlen(argument_names[0]) / 2,
         0},
        {(size_t)(argument_uses[0] - source) + strlen(argument_names[0]), 0},
        {(size_t)(commas[0] - source), -1},
        {(size_t)(argument_uses[1] - source), 1},
        {(size_t)(argument_uses[1] - source) + strlen(argument_names[1]) / 2,
         1},
        {(size_t)(argument_uses[1] - source) + strlen(argument_names[1]), 1},
        {(size_t)(commas[1] - source), -1},
        {(size_t)(argument_uses[2] - source), 2},
        {(size_t)(argument_uses[2] - source) + strlen(argument_names[2]) / 2,
         2},
        {(size_t)(argument_uses[2] - source) + strlen(argument_names[2]), 2},
        {(size_t)(call_close - source) + 1, -1}};
    size_t cursor_step_count = 14;
    if ((variant & 1) == 0) {
      const char *first_comment = strstr(
          argument_uses[0] + strlen(argument_names[0]),
          "/* first comma */");
      const char *second_comment = strstr(
          argument_uses[1] + strlen(argument_names[1]),
          "/* second comma */");
      CHECK(first_comment && second_comment,
            "enum three argument comment anchors");
      cursor_steps[cursor_step_count++] =
          (enum_three_argument_cursor_step_t){
              (size_t)(first_comment - source) + 4, -1};
      cursor_steps[cursor_step_count++] =
          (enum_three_argument_cursor_step_t){
              (size_t)(second_comment - source) + 4, -1};
    } else {
      const char *first_splice = strchr(
          argument_uses[0] + strlen(argument_names[0]), '\\');
      const char *second_splice = strchr(
          argument_uses[1] + strlen(argument_names[1]), '\\');
      CHECK(first_splice && first_splice < commas[0] &&
                second_splice && second_splice < commas[1],
            "enum three argument splice anchors");
      cursor_steps[cursor_step_count++] =
          (enum_three_argument_cursor_step_t){
              (size_t)(first_splice - source), -1};
      cursor_steps[cursor_step_count++] =
          (enum_three_argument_cursor_step_t){
              (size_t)(first_splice - source) + 1, -1};
      cursor_steps[cursor_step_count++] =
          (enum_three_argument_cursor_step_t){
              (size_t)(second_splice - source), -1};
      cursor_steps[cursor_step_count++] =
          (enum_three_argument_cursor_step_t){
              (size_t)(second_splice - source) + 1, -1};
    }
    for (size_t cursor_index = 0;
         cursor_index < cursor_step_count; cursor_index++) {
      CHECK(analyze_named(
                session, "enum-three-argument-call.c", source,
                cursor_steps[cursor_index].cursor,
                bundles[header_revision], defaults,
                &snapshot, &error),
            "enum three argument call analysis");
      CHECK(snapshot.partial && snapshot.dependency_count == 1 &&
                strcmp(snapshot.dependencies[0],
                       "enum-three-argument-call.h") == 0,
            "enum three argument common fields");
      const ag_language_symbol_t *callee_candidate = find_symbol(
          &snapshot, callee_name, AG_LANGUAGE_SYMBOL_MACRO);
      CHECK(callee_candidate && callee_candidate->macro_is_function_like &&
                !callee_candidate->macro_is_variadic &&
                callee_candidate->macro_parameter_count == 3 &&
                callee_candidate->macro_parameters &&
                strcmp(callee_candidate->macro_parameters[0],
                       header_parameter_names[header_revision][0]) == 0 &&
                strcmp(callee_candidate->macro_parameters[1],
                       header_parameter_names[header_revision][1]) == 0 &&
                strcmp(callee_candidate->macro_parameters[2],
                       header_parameter_names[header_revision][2]) == 0 &&
                callee_candidate->macro_replacement &&
                strcmp(callee_candidate->macro_replacement,
                       header_replacements[header_revision]) == 0 &&
                callee_candidate->declaration.source_name &&
                strcmp(callee_candidate->declaration.source_name,
                       "enum-three-argument-call.h") == 0 &&
                callee_candidate->declaration.start.offset ==
                    (int)(callee_declaration - header) &&
                callee_candidate->declaration.end.offset ==
                    (int)(callee_declaration - header + callee_length) &&
                check_documentation_symbol(
                    callee_candidate,
                    header_documentation[header_revision],
                    "enum-three-argument-call.h",
                    (size_t)(callee_comment - header),
                    (size_t)(callee_comment - header) +
                        strlen(header_comments[header_revision])),
            "enum three argument callee macro fields");
      const ag_language_symbol_t *argument_candidates[3] = {0};
      for (int argument_index = 0; argument_index < 3; argument_index++) {
        int enum_argument =
            (outer_enum_arguments && argument_index != 1) ||
            (middle_enum_argument && argument_index == 1);
        argument_candidates[argument_index] = find_symbol(
            &snapshot, argument_names[argument_index],
            enum_argument ? AG_LANGUAGE_SYMBOL_ENUM_CONSTANT
                          : AG_LANGUAGE_SYMBOL_MACRO);
        if (missing_argument_mode & (1 << argument_index)) {
          CHECK(!argument_candidates[argument_index],
                "enum three argument missing operand removed");
          continue;
        }
        const char *declaration = strstr(source, argument_names[argument_index]);
        const char *comment = strstr(source, argument_comments[argument_index]);
        CHECK(declaration && comment && argument_candidates[argument_index] &&
                  ((enum_argument &&
                    argument_candidates[argument_index]->constant_value &&
                    strcmp(argument_candidates[argument_index]->constant_value,
                           argument_values[argument_index]) == 0) ||
                   (!enum_argument &&
                    !argument_candidates[argument_index]
                         ->macro_is_function_like &&
                    !argument_candidates[argument_index]
                         ->macro_is_variadic &&
                    argument_candidates[argument_index]
                            ->macro_parameter_count == 0 &&
                    argument_candidates[argument_index]
                        ->macro_replacement &&
                    strcmp(argument_candidates[argument_index]
                               ->macro_replacement,
                           argument_values[argument_index]) == 0)) &&
                  argument_candidates[argument_index]
                      ->declaration.source_name &&
                  strcmp(argument_candidates[argument_index]
                             ->declaration.source_name,
                         "enum-three-argument-call.c") == 0 &&
                  argument_candidates[argument_index]->declaration.start.offset ==
                      (int)(declaration - source) &&
                  argument_candidates[argument_index]->declaration.end.offset ==
                      (int)(declaration - source +
                            strlen(argument_names[argument_index])) &&
                  check_documentation_symbol(
                      argument_candidates[argument_index],
                      argument_documentation[argument_index],
                      "enum-three-argument-call.c",
                      (size_t)(comment - source),
                      (size_t)(comment - source) +
                          strlen(argument_comments[argument_index])),
              "enum three argument operand fields");
      }
      if (middle_enum_argument) {
        const char *inactive_middle_enum_name =
            renamed_middle_enum_argument
                ? middle_enum_argument_names[1]
                : renamed_middle_enum_argument_names[1];
        CHECK(!find_symbol(&snapshot, inactive_middle_enum_name,
                           AG_LANGUAGE_SYMBOL_ENUM_CONSTANT),
              "enum three argument inactive middle enum removed");
      }
      const ag_language_symbol_t *derived = find_symbol(
          &snapshot, "ENUM_THREE_ARGUMENT_DERIVED",
          AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
      CHECK((missing_argument_mode && !derived) ||
                (!missing_argument_mode && derived &&
                 derived->constant_value &&
                 strcmp(derived->constant_value,
                        header_revision == 1
                            ? updated_macro_arguments ? "227"
                              : updated_middle_enum_argument ? "213"
                              : middle_enum_argument ? "210"
                              : outer_enum_arguments ? "211"
                                                     : "206"
                            : updated_macro_arguments ? "127"
                              : updated_middle_enum_argument ? "113"
                              : middle_enum_argument ? "110"
                              : outer_enum_arguments ? "111"
                                                     : "106") == 0),
            "enum three argument derived value");
      int first_missing_argument_index = 0;
      while (first_missing_argument_index < 3 &&
             !(missing_argument_mode &
               (1 << first_missing_argument_index)))
        first_missing_argument_index++;
      CHECK(snapshot.diagnostic_count ==
                (missing_argument_mode ? 1u : 0u),
            "enum three argument diagnostic count");
      if (missing_argument_mode) {
        const ag_language_diagnostic_t *undefined_argument =
            find_diagnostic(&snapshot, "E3066");
        CHECK(undefined_argument && undefined_argument->message &&
                  strstr(undefined_argument->message,
                         argument_names[first_missing_argument_index]) &&
                  undefined_argument->range.start.offset ==
                      (int)(callee_use - source) &&
                  undefined_argument->range.end.offset ==
                      (int)(call_open - source),
              "enum three argument undefined argument range");
        for (int argument_index = first_missing_argument_index + 1;
             argument_index < 3; argument_index++)
          if (missing_argument_mode & (1 << argument_index))
            CHECK(!strstr(undefined_argument->message,
                          argument_names[argument_index]),
                  "enum three argument later missing argument omitted");
        if (missing_argument_mode == 5)
          CHECK(!strstr(undefined_argument->message,
                        argument_names[1]),
                "enum three argument surviving middle diagnostic omitted");
        if (missing_argument_mode == 3)
          CHECK(!strstr(undefined_argument->message,
                        argument_names[2]),
                "enum three argument surviving last diagnostic omitted");
        if (missing_argument_mode == 6)
          CHECK(!strstr(undefined_argument->message,
                        argument_names[0]),
                "enum three argument surviving first diagnostic omitted");
        if (missing_argument_mode == 4)
          CHECK(!strstr(undefined_argument->message,
                        argument_names[0]) &&
                    !strstr(undefined_argument->message,
                            argument_names[1]),
                "enum three argument surviving prefix diagnostic omitted");
        if (missing_argument_mode == 2)
          CHECK(!strstr(undefined_argument->message,
                        argument_names[0]) &&
                    !strstr(undefined_argument->message,
                            argument_names[2]),
                "enum three argument surviving outer diagnostic omitted");
        if (missing_argument_mode == 1)
          CHECK(!strstr(undefined_argument->message,
                        argument_names[1]) &&
                    !strstr(undefined_argument->message,
                            argument_names[2]),
                "enum three argument surviving suffix diagnostic omitted");
        if (middle_enum_argument) {
          const char *inactive_middle_enum_name =
              renamed_middle_enum_argument
                  ? middle_enum_argument_names[1]
                  : renamed_middle_enum_argument_names[1];
          CHECK(!strstr(undefined_argument->message,
                        inactive_middle_enum_name),
                "enum three argument inactive middle diagnostic omitted");
        }
      }
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      int hover_index = cursor_steps[cursor_index].hover_index;
      const ag_language_symbol_t *expected_hover =
          hover_index == -2
              ? callee_candidate
          : hover_index >= 0
              ? argument_candidates[hover_index]
              : NULL;
      CHECK((!expected_hover && !hover) ||
                (expected_hover && hover &&
                 hover->kind == expected_hover->kind &&
                 same_range(&hover->declaration,
                            &expected_hover->declaration)),
            "enum three argument hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
    free(source);
  }
  ag_compilation_session_destroy(session);
  for (size_t header_revision = 0;
       header_revision < ENUM_THREE_ARGUMENT_HEADER_REVISION_COUNT;
       header_revision++)
    free(bundles[header_revision].bytes);
  return 0;
}

static int test_initializer_designator_operand_hover(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "initializer designator operand session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration_fragment;
    const char *constant_value;
    int boundary_case;
  } cases[] = {
      {"[INITIALIZER_DESIGNATOR_A] = 1", "INITIALIZER_DESIGNATOR_A",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "INITIALIZER_DESIGNATOR_A = 2",
       "2", 0},
      {"[+INITIALIZER_DESIGNATOR_A] = 1", "INITIALIZER_DESIGNATOR_A",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "INITIALIZER_DESIGNATOR_A = 2",
       "2", 0},
      {"[INITIALIZER_DESIGNATOR_A + INITIALIZER_DESIGNATOR_B] = 1",
       "INITIALIZER_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_A = 2", "2", 0},
      {"+ INITIALIZER_DESIGNATOR_B] = 1", "INITIALIZER_DESIGNATOR_B",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "INITIALIZER_DESIGNATOR_B = 3",
       "3", 0},
      {"[(INITIALIZER_DESIGNATOR_C)] = 1", "INITIALIZER_DESIGNATOR_C",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "INITIALIZER_DESIGNATOR_C = 4",
       "4", 0},
      {"? INITIALIZER_DESIGNATOR_A : INITIALIZER_DESIGNATOR_B] = 1",
       "INITIALIZER_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_B = 3", "3", 0},
      {"[INITIALIZER_DESIGNATOR_MACRO] = 1",
       "INITIALIZER_DESIGNATOR_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "INITIALIZER_DESIGNATOR_MACRO 5", "", 0},
      {"[/* expression gap */ INITIALIZER_DESIGNATOR_A] = 1",
       "INITIALIZER_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_A = 2", "2", 0},
      {"[\\\nINITIALIZER_DESIGNATOR_B] = 1",
       "INITIALIZER_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_B = 3", "3", 0},
      {"[\\\r\nINITIALIZER_DESIGNATOR_C] = 1",
       "INITIALIZER_DESIGNATOR_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_C = 4", "4", 0},
      {"[1] = { [INITIALIZER_DESIGNATOR_A] = 1 }",
       "INITIALIZER_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_A = 2", "2", 0},
      {"[INITIALIZER_DESIGNATOR_A].value = 1",
       "INITIALIZER_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_A = 2", "2", 0},
      {"[INITIALIZER_DESIGNATOR_A][INITIALIZER_DESIGNATOR_B] = 1",
       "INITIALIZER_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_A = 2", "2", 0},
      {"][INITIALIZER_DESIGNATOR_B] = 1",
       "INITIALIZER_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_B = 3", "3", 0},
      {"initializer_operand_scalar = { INITIALIZER_DESIGNATOR_A }",
       "INITIALIZER_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_A = 2", "2", 0},
      {"initializer_operand_nested[2][2] = { { INITIALIZER_DESIGNATOR_B, 0 }",
       "INITIALIZER_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_B = 3", "3", 0},
      {"initializer_operand_binary = { INITIALIZER_DESIGNATOR_A + INITIALIZER_DESIGNATOR_C }",
       "INITIALIZER_DESIGNATOR_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_C = 4", "4", 0},
      {"initializer_operand_macro = { /* value gap */ INITIALIZER_DESIGNATOR_MACRO }",
       "INITIALIZER_DESIGNATOR_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "INITIALIZER_DESIGNATOR_MACRO 5", "", 0},
      {"initializer_operand_multi = { INITIALIZER_DESIGNATOR_C }",
       "INITIALIZER_DESIGNATOR_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_C = 4", "4", 3},
      {"initializer_designator_multi[16] = { [INITIALIZER_DESIGNATOR_B] = 1 }",
       "INITIALIZER_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_B = 3", "3", 1},
      {"designator_local[16] = { [INITIALIZER_DESIGNATOR_LOCAL] = 1 }",
       "INITIALIZER_DESIGNATOR_LOCAL", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "INITIALIZER_DESIGNATOR_LOCAL = 6", "6", 2},
      {"designator_operand_local[2] = { designator_parameter,",
       "designator_parameter", AG_LANGUAGE_SYMBOL_PARAMETER,
       "designator_parameter)", "", 2},
  };
  const char *macro_comment = strstr(
      initializer_designator_operand_hover_source,
      "/// initializer designator macro documentation");
  CHECK(macro_comment != NULL, "initializer designator macro comment anchor");
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          initializer_designator_operand_hover_source,
          cases[case_index].fragment);
      const char *use = fragment
                            ? strstr(fragment, cases[case_index].name)
                            : NULL;
      const char *declaration = strstr(
          initializer_designator_operand_hover_source,
          cases[case_index].declaration_fragment);
      CHECK(use && declaration, "initializer designator operand anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh initializer designator operand session");
        }
        CHECK(analyze_named(
                  analysis_session, "initializer-designator-operand.c",
                  initializer_designator_operand_hover_source,
                  (size_t)(use - initializer_designator_operand_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "initializer designator operand analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(hover && completion && !snapshot.partial &&
                  snapshot.diagnostic_count == 0 &&
                  hover->kind == cases[case_index].kind &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            initializer_designator_operand_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            initializer_designator_operand_hover_source +
                            name_length) &&
                  same_range(&hover->declaration, &completion->declaration),
              "initializer designator operand fields");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(strcmp(hover->constant_value,
                       cases[case_index].constant_value) == 0,
                "initializer designator operand value");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
          CHECK(hover->macro_replacement &&
                    strcmp(hover->macro_replacement, "5") == 0 &&
                    check_documentation_symbol(
                        hover, "initializer designator macro documentation",
                        "initializer-designator-operand.c",
                        (size_t)(macro_comment -
                                 initializer_designator_operand_hover_source),
                        (size_t)(macro_comment -
                                 initializer_designator_operand_hover_source) +
                            strlen("/// initializer designator macro documentation")),
                "initializer designator macro fields");
        if (cases[case_index].boundary_case == 1)
          CHECK(!find_symbol(
                    &snapshot, "initializer_designator_later",
                    AG_LANGUAGE_SYMBOL_OBJECT),
                "later comma declarator remains invisible");
        if (cases[case_index].boundary_case == 2)
          CHECK(find_symbol(
                    &snapshot, "designator_parameter",
                    AG_LANGUAGE_SYMBOL_PARAMETER) &&
                    find_symbol(
                        &snapshot, "designator_before",
                        AG_LANGUAGE_SYMBOL_OBJECT) &&
                    !find_symbol(
                        &snapshot, "designator_after",
                        AG_LANGUAGE_SYMBOL_OBJECT),
                "block designator preserves cursor lookup point");
        if (cases[case_index].boundary_case == 3)
          CHECK(!find_symbol(
                    &snapshot, "initializer_operand_later",
                    AG_LANGUAGE_SYMBOL_OBJECT),
                "later initializer operand declarator remains invisible");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_compound_literal_designator_operand_hover(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "compound literal designator operand session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration_fragment;
    const char *constant_value;
    int boundary_case;
  } cases[] = {
      {"file_direct = (int[8]){ [COMPOUND_LITERAL_DESIGNATOR_A] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_A = 2", "2", 0},
      {"file_unary = (int[8]){ [+COMPOUND_LITERAL_DESIGNATOR_A] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_A = 2", "2", 0},
      {"file_binary = (int[8]){ [COMPOUND_LITERAL_DESIGNATOR_A +",
       "COMPOUND_LITERAL_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_A = 2", "2", 0},
      {"+ COMPOUND_LITERAL_DESIGNATOR_B] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_B = 3", "3", 0},
      {"file_grouped = (int[8]){ [(COMPOUND_LITERAL_DESIGNATOR_C)] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_C = 4", "4", 0},
      {"? COMPOUND_LITERAL_DESIGNATOR_A : "
       "COMPOUND_LITERAL_DESIGNATOR_B] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_B = 3", "3", 0},
      {"file_macro = (int[8]){ [COMPOUND_LITERAL_DESIGNATOR_MACRO] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "COMPOUND_LITERAL_DESIGNATOR_MACRO 5", "", 0},
      {"[/* expression gap */ COMPOUND_LITERAL_DESIGNATOR_A] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_A = 2", "2", 0},
      {"[\\\nCOMPOUND_LITERAL_DESIGNATOR_B] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_B = 3", "3", 0},
      {"[\\\r\nCOMPOUND_LITERAL_DESIGNATOR_C] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_C = 4", "4", 0},
      {"(CompoundLiteralDesignatorArray){ "
       "[COMPOUND_LITERAL_DESIGNATOR_A] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_A = 2", "2", 0},
      {"[1] = { [COMPOUND_LITERAL_DESIGNATOR_B] = 1 }",
       "COMPOUND_LITERAL_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_B = 3", "3", 0},
      {".values[COMPOUND_LITERAL_DESIGNATOR_A] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_A = 2", "2", 0},
      {"file_chain)[8] = (int[8][8]){ "
       "[COMPOUND_LITERAL_DESIGNATOR_A][",
       "COMPOUND_LITERAL_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_A = 2", "2", 0},
      {"][COMPOUND_LITERAL_DESIGNATOR_B] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_B = 3", "3", 0},
      {"file_multi = (int[8]){ [COMPOUND_LITERAL_DESIGNATOR_C] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_C = 4", "4", 1},
      {"designator_value = ((int[8]){ "
       "[COMPOUND_LITERAL_DESIGNATOR_A] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_A = 2", "2", 2},
      {"compound_literal_designator_take((int[8]){ "
       "[COMPOUND_LITERAL_DESIGNATOR_B] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_B = 3", "3", 2},
      {"designator_multi = (int[8]){ "
       "[COMPOUND_LITERAL_DESIGNATOR_C] = 1",
       "COMPOUND_LITERAL_DESIGNATOR_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_C = 4", "4", 3},
      {"compound_literal_operand_file = (int[8]){ "
       "COMPOUND_LITERAL_DESIGNATOR_A, 0",
       "COMPOUND_LITERAL_DESIGNATOR_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_A = 2", "2", 0},
      {".values = { COMPOUND_LITERAL_DESIGNATOR_B, 0 }",
       "COMPOUND_LITERAL_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_B = 3", "3", 0},
      {"operand_value = ((int[8]){ designator_parameter,",
       "designator_parameter", AG_LANGUAGE_SYMBOL_PARAMETER,
       "designator_parameter)", "", 2},
      {"operand_record = ((struct CompoundLiteralDesignatorRecord){ "
       ".values = { COMPOUND_LITERAL_DESIGNATOR_B, 0 }",
       "COMPOUND_LITERAL_DESIGNATOR_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "COMPOUND_LITERAL_DESIGNATOR_B = 3", "3", 2},
      {"operand_macro = ((int[8]){ "
       "COMPOUND_LITERAL_DESIGNATOR_MACRO, 0",
       "COMPOUND_LITERAL_DESIGNATOR_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "COMPOUND_LITERAL_DESIGNATOR_MACRO 5", "", 2},
  };
  const char *macro_comment = strstr(
      compound_literal_designator_operand_hover_source,
      "/// compound literal designator macro documentation");
  CHECK(macro_comment != NULL,
        "compound literal designator macro comment anchor");
  for (size_t case_index = 0;
       case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
    const char *fragment = strstr(
        compound_literal_designator_operand_hover_source,
        cases[case_index].fragment);
    const char *use = fragment
                          ? strstr(fragment, cases[case_index].name)
                          : NULL;
    const char *declaration = strstr(
        compound_literal_designator_operand_hover_source,
        cases[case_index].declaration_fragment);
    CHECK(use && declaration,
          "compound literal designator operand anchors");
    size_t name_length = strlen(cases[case_index].name);
    size_t deltas[] = {0, name_length / 2, name_length};
    for (size_t delta_index = 0;
         delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
      CHECK(analyze_named(
                session, "compound-literal-designator-operand.c",
                compound_literal_designator_operand_hover_source,
                (size_t)(use -
                         compound_literal_designator_operand_hover_source) +
                    deltas[delta_index],
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "compound literal designator operand analysis");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      const ag_language_symbol_t *completion = find_symbol(
          &snapshot, cases[case_index].name, cases[case_index].kind);
      CHECK(hover && completion && !snapshot.partial &&
                snapshot.diagnostic_count == 0 &&
                hover->kind == cases[case_index].kind &&
                strcmp(hover->name, cases[case_index].name) == 0 &&
                hover->declaration.start.offset ==
                    (int)(declaration -
                          compound_literal_designator_operand_hover_source) &&
                hover->declaration.end.offset ==
                    (int)(declaration -
                          compound_literal_designator_operand_hover_source +
                          name_length) &&
                same_range(&hover->declaration, &completion->declaration),
            "compound literal designator operand fields");
      if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
        CHECK(strcmp(hover->constant_value,
                     cases[case_index].constant_value) == 0,
              "compound literal designator operand value");
      if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
        CHECK(hover->macro_replacement &&
                  strcmp(hover->macro_replacement, "5") == 0 &&
                  check_documentation_symbol(
                      hover, "compound literal designator macro documentation",
                      "compound-literal-designator-operand.c",
                      (size_t)(macro_comment -
                               compound_literal_designator_operand_hover_source),
                      (size_t)(macro_comment -
                               compound_literal_designator_operand_hover_source) +
                          strlen("/// compound literal designator macro documentation")),
              "compound literal designator macro fields");
      if (cases[case_index].boundary_case <= 1)
        CHECK(!find_symbol(
                  &snapshot, "compound_literal_designator_file_after",
                  AG_LANGUAGE_SYMBOL_OBJECT),
              "later file object remains invisible");
      if (cases[case_index].boundary_case == 1)
        CHECK(!find_symbol(
                  &snapshot, "compound_literal_designator_file_later",
                  AG_LANGUAGE_SYMBOL_OBJECT),
              "later compound literal comma declarator remains invisible");
      if (cases[case_index].boundary_case >= 2)
        CHECK(find_symbol(
                  &snapshot, "designator_parameter",
                  AG_LANGUAGE_SYMBOL_PARAMETER) &&
                  find_symbol(
                      &snapshot, "designator_before",
                      AG_LANGUAGE_SYMBOL_OBJECT) &&
                  !find_symbol(
                      &snapshot, "designator_after",
                      AG_LANGUAGE_SYMBOL_OBJECT),
              "compound literal block lookup point");
      if (cases[case_index].boundary_case == 3)
        CHECK(!find_symbol(
                  &snapshot, "designator_later",
                  AG_LANGUAGE_SYMBOL_OBJECT),
              "later block comma declarator remains invisible");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  const size_t fresh_case_indices[] = {11, 18, 22};
  for (size_t i = 0;
       i < sizeof(fresh_case_indices) / sizeof(fresh_case_indices[0]); i++) {
    size_t case_index = fresh_case_indices[i];
    const char *fragment = strstr(
        compound_literal_designator_operand_hover_source,
        cases[case_index].fragment);
    const char *use = fragment
                          ? strstr(fragment, cases[case_index].name)
                          : NULL;
    ag_compilation_session_t *fresh =
        ag_compilation_session_create(&target);
    CHECK(fresh && use && analyze_named(
              fresh, "compound-literal-designator-operand.c",
              compound_literal_designator_operand_hover_source,
              (size_t)(use -
                       compound_literal_designator_operand_hover_source) +
                  strlen(cases[case_index].name) / 2,
              (header_bundle_t){0}, defaults, &snapshot, &error),
          "fresh compound literal designator operand analysis");
    const ag_language_symbol_t *hover = hover_symbol(&snapshot);
    CHECK(hover && hover->kind == cases[case_index].kind &&
              strcmp(hover->name, cases[case_index].name) == 0 &&
              !snapshot.partial && snapshot.diagnostic_count == 0,
          "fresh compound literal designator operand fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
    ag_compilation_session_destroy(fresh);
  }

  const char *invalid_sources[] = {
      "enum { INDEX = 2 }; int f(void) { "
      "return ((int[4]){ [INDEX] }); }\n",
      "enum { INDEX = 2 }; int f(void) { "
      "return ((int[4]){ [INDEX] = 7;\n",
  };
  for (size_t i = 0;
       i < sizeof(invalid_sources) / sizeof(invalid_sources[0]); i++) {
    const char *use = strstr(invalid_sources[i], "[INDEX]");
    int ok = analyze_named(
        session, "invalid-compound-literal-designator.c",
        invalid_sources[i],
        (size_t)(use - invalid_sources[i]) + 3,
        (header_bundle_t){0}, defaults, &snapshot, &error);
    CHECK((ok && snapshot.partial) ||
              (!ok && error.status == AG_LANGUAGE_ANALYSIS_FAILED),
          "invalid compound literal designator remains incomplete");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  const char *reuse_fragment = strstr(
      compound_literal_designator_operand_hover_source,
      cases[0].fragment);
  const char *reuse_use = reuse_fragment
                              ? strstr(reuse_fragment, cases[0].name)
                              : NULL;
  CHECK(reuse_use && analyze_named(
            session, "compound-literal-designator-operand.c",
            compound_literal_designator_operand_hover_source,
            (size_t)(reuse_use -
                     compound_literal_designator_operand_hover_source) + 3,
            (header_bundle_t){0}, defaults, &snapshot, &error) &&
            !snapshot.partial && snapshot.diagnostic_count == 0,
        "compound literal designator session reusable after failure");
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_type_name_array_bound_operand_hover(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "type-name array bound operand session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration_fragment;
    const char *constant_value;
    int boundary_case;
  } cases[] = {
      {"type_name_array_bound_file = sizeof(int[TYPE_NAME_ARRAY_BOUND_A])",
       "TYPE_NAME_ARRAY_BOUND_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_A = 2", "2", 1},
      {"bound_sizeof_direct = sizeof(int[TYPE_NAME_ARRAY_BOUND_A])",
       "TYPE_NAME_ARRAY_BOUND_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_A = 2", "2", 2},
      {"sizeof(int[+TYPE_NAME_ARRAY_BOUND_A])",
       "TYPE_NAME_ARRAY_BOUND_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_A = 2", "2", 0},
      {"sizeof(int[TYPE_NAME_ARRAY_BOUND_A + TYPE_NAME_ARRAY_BOUND_B])",
       "TYPE_NAME_ARRAY_BOUND_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_A = 2", "2", 0},
      {"+ TYPE_NAME_ARRAY_BOUND_B])",
       "TYPE_NAME_ARRAY_BOUND_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_B = 3", "3", 0},
      {"sizeof(int[(TYPE_NAME_ARRAY_BOUND_C)])",
       "TYPE_NAME_ARRAY_BOUND_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_C = 4", "4", 0},
      {": TYPE_NAME_ARRAY_BOUND_B])",
       "TYPE_NAME_ARRAY_BOUND_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_B = 3", "3", 0},
      {"sizeof(int[TYPE_NAME_ARRAY_BOUND_MACRO])",
       "TYPE_NAME_ARRAY_BOUND_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "TYPE_NAME_ARRAY_BOUND_MACRO 5", "", 0},
      {"sizeof(int[/* expression gap */ TYPE_NAME_ARRAY_BOUND_A])",
       "TYPE_NAME_ARRAY_BOUND_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_A = 2", "2", 0},
      {"sizeof(int[\\\nTYPE_NAME_ARRAY_BOUND_B])",
       "TYPE_NAME_ARRAY_BOUND_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_B = 3", "3", 0},
      {"sizeof(int[\\\r\nTYPE_NAME_ARRAY_BOUND_C])",
       "TYPE_NAME_ARRAY_BOUND_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_C = 4", "4", 0},
      {"_Alignof(int[TYPE_NAME_ARRAY_BOUND_A])",
       "TYPE_NAME_ARRAY_BOUND_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_A = 2", "2", 0},
      {"(int (*)[TYPE_NAME_ARRAY_BOUND_B])0",
       "TYPE_NAME_ARRAY_BOUND_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_B = 3", "3", 0},
      {"(int[TYPE_NAME_ARRAY_BOUND_C]){ 0 }",
       "TYPE_NAME_ARRAY_BOUND_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_C = 4", "4", 0},
      {"bound_cast_postfix = (*(int (*)[TYPE_NAME_ARRAY_BOUND_B])",
       "TYPE_NAME_ARRAY_BOUND_B", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_B = 3", "3", 0},
      {"bound_compound_postfix = (int[TYPE_NAME_ARRAY_BOUND_C])",
       "TYPE_NAME_ARRAY_BOUND_C", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_C = 4", "4", 0},
      {"int (*)[TYPE_NAME_ARRAY_BOUND_A]: 1",
       "TYPE_NAME_ARRAY_BOUND_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_A = 2", "2", 0},
      {"bound_local = sizeof(int[TYPE_NAME_ARRAY_BOUND_LOCAL])",
       "TYPE_NAME_ARRAY_BOUND_LOCAL", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "TYPE_NAME_ARRAY_BOUND_LOCAL = 6", "6", 2},
  };
  const char *macro_comment = strstr(
      type_name_array_bound_operand_hover_source,
      "/// type-name array bound macro documentation");
  CHECK(macro_comment != NULL, "type-name array bound macro comment anchor");
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          type_name_array_bound_operand_hover_source,
          cases[case_index].fragment);
      const char *use = fragment ? strstr(fragment, cases[case_index].name)
                                 : NULL;
      const char *declaration = strstr(
          type_name_array_bound_operand_hover_source,
          cases[case_index].declaration_fragment);
      CHECK(use && declaration, "type-name array bound operand anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh type-name array bound operand session");
        }
        CHECK(analyze_named(
                  analysis_session, "type-name-array-bound-operand.c",
                  type_name_array_bound_operand_hover_source,
                  (size_t)(use - type_name_array_bound_operand_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "type-name array bound operand analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(hover && completion && !snapshot.partial &&
                  snapshot.diagnostic_count == 0 &&
                  hover->kind == cases[case_index].kind &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            type_name_array_bound_operand_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            type_name_array_bound_operand_hover_source +
                            name_length) &&
                  same_range(&hover->declaration, &completion->declaration),
              "type-name array bound operand fields");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(strcmp(hover->constant_value,
                       cases[case_index].constant_value) == 0,
                "type-name array bound enum value");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
          CHECK(hover->macro_replacement &&
                    strcmp(hover->macro_replacement, "5") == 0 &&
                    check_documentation_symbol(
                        hover, "type-name array bound macro documentation",
                        "type-name-array-bound-operand.c",
                        (size_t)(macro_comment -
                                 type_name_array_bound_operand_hover_source),
                        (size_t)(macro_comment -
                                 type_name_array_bound_operand_hover_source) +
                            strlen("/// type-name array bound macro documentation")),
                "type-name array bound macro fields");
        if (cases[case_index].boundary_case == 1)
          CHECK(!find_symbol(
                    &snapshot, "type_name_array_bound_later",
                    AG_LANGUAGE_SYMBOL_OBJECT),
                "later type-name array bound declarator remains invisible");
        if (cases[case_index].boundary_case == 2)
          CHECK(find_symbol(
                    &snapshot, "bound_parameter",
                    AG_LANGUAGE_SYMBOL_PARAMETER) &&
                    find_symbol(
                        &snapshot, "bound_before",
                        AG_LANGUAGE_SYMBOL_OBJECT) &&
                    !find_symbol(
                        &snapshot, "bound_after",
                        AG_LANGUAGE_SYMBOL_OBJECT),
                "type-name array bound preserves cursor lookup point");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  const char *invalid_sources[] = {
      "enum { ZERO_BOUND = 0 }; int f(void) { return sizeof(int[ZERO_BOUND]); }\n",
      "enum { INCOMPLETE_BOUND = 2 }; int f(void) { return sizeof(int[INCOMPLETE_BOUND",
  };
  const char *invalid_names[] = {"ZERO_BOUND", "INCOMPLETE_BOUND"};
  for (size_t i = 0;
       i < sizeof(invalid_sources) / sizeof(invalid_sources[0]); i++) {
    const char *use = last_occurrence(invalid_sources[i], invalid_names[i]);
    CHECK(use != NULL, "invalid type-name array bound anchor");
    int ok = analyze_named(
        session, "invalid-type-name-array-bound.c", invalid_sources[i],
        (size_t)(use - invalid_sources[i]) + strlen(invalid_names[i]) / 2,
        (header_bundle_t){0}, defaults, &snapshot, &error);
    CHECK((ok && snapshot.partial && snapshot.diagnostic_count > 0) ||
              (!ok && error.status == AG_LANGUAGE_ANALYSIS_FAILED),
          "invalid type-name array bound diagnostic preserved");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  const char *reused_fragment = strstr(
      type_name_array_bound_operand_hover_source,
      "bound_sizeof_direct = sizeof(int[TYPE_NAME_ARRAY_BOUND_A])");
  const char *reused_use = reused_fragment
                               ? strstr(reused_fragment,
                                        "TYPE_NAME_ARRAY_BOUND_A")
                               : NULL;
  CHECK(reused_use && analyze_named(
            session, "type-name-array-bound-operand.c",
            type_name_array_bound_operand_hover_source,
            (size_t)(reused_use -
                     type_name_array_bound_operand_hover_source) + 3,
            (header_bundle_t){0}, defaults, &snapshot, &error) &&
            hover_symbol(&snapshot) && !snapshot.partial &&
            snapshot.diagnostic_count == 0 &&
            strcmp(hover_symbol(&snapshot)->name,
                   "TYPE_NAME_ARRAY_BOUND_A") == 0,
        "type-name array bound session reusable after invalid source");
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_declarator_array_bound_operand_hover(
    ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "declarator array bound operand session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration_fragment;
    const char *constant_value;
    int boundary_case;
  } cases[] = {
      {"declarator_array_bound_file[DECLARATOR_ARRAY_BOUND_MACRO]",
       "DECLARATOR_ARRAY_BOUND_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "DECLARATOR_ARRAY_BOUND_MACRO 4", "", 1},
      {"declarator_array_bound_typedef[DECLARATOR_ARRAY_BOUND_MACRO]",
       "DECLARATOR_ARRAY_BOUND_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       "DECLARATOR_ARRAY_BOUND_MACRO 4", "", 0},
      {"declarator_array_bound_enum[DECLARATOR_ARRAY_BOUND_ENUM]",
       "DECLARATOR_ARRAY_BOUND_ENUM", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "DECLARATOR_ARRAY_BOUND_ENUM = 3", "3", 0},
      {"member[DECLARATOR_ARRAY_BOUND_ENUM]",
       "DECLARATOR_ARRAY_BOUND_ENUM", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "DECLARATOR_ARRAY_BOUND_ENUM = 3", "3", 0},
      {"local_values[bound_parameter]", "bound_parameter",
       AG_LANGUAGE_SYMBOL_PARAMETER, "bound_parameter)", "", 2},
      {"declarator_array_bound_file[subscript_index]", "subscript_index",
       AG_LANGUAGE_SYMBOL_PARAMETER, "subscript_index)", "", 0},
  };
  const char *macro_comment = strstr(
      declarator_array_bound_operand_hover_source,
      "/// declarator array bound macro documentation");
  CHECK(macro_comment != NULL,
        "declarator array bound macro comment anchor");
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          declarator_array_bound_operand_hover_source,
          cases[case_index].fragment);
      const char *use = fragment ? strstr(fragment, cases[case_index].name)
                                 : NULL;
      const char *declaration = strstr(
          declarator_array_bound_operand_hover_source,
          cases[case_index].declaration_fragment);
      CHECK(use && declaration, "declarator array bound operand anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh declarator array bound operand session");
        }
        CHECK(analyze_named(
                  analysis_session, "declarator-array-bound-operand.c",
                  declarator_array_bound_operand_hover_source,
                  (size_t)(use -
                           declarator_array_bound_operand_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "declarator array bound operand analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(hover && completion && !snapshot.partial &&
                  snapshot.diagnostic_count == 0 &&
                  hover->kind == cases[case_index].kind &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            declarator_array_bound_operand_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            declarator_array_bound_operand_hover_source +
                            name_length) &&
                  same_range(&hover->declaration, &completion->declaration),
              "declarator array bound operand fields");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(strcmp(hover->constant_value,
                       cases[case_index].constant_value) == 0,
                "declarator array bound enum value");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
          CHECK(hover->macro_replacement &&
                    strcmp(hover->macro_replacement, "4") == 0 &&
                    check_documentation_symbol(
                        hover, "declarator array bound macro documentation",
                        "declarator-array-bound-operand.c",
                        (size_t)(macro_comment -
                                 declarator_array_bound_operand_hover_source),
                        (size_t)(macro_comment -
                                 declarator_array_bound_operand_hover_source) +
                            strlen("/// declarator array bound macro documentation")),
                "declarator array bound macro fields");
        if (cases[case_index].boundary_case == 1)
          CHECK(!find_symbol(
                    &snapshot, "declarator_array_bound_later",
                    AG_LANGUAGE_SYMBOL_OBJECT),
                "later declarator array bound object remains invisible");
        if (cases[case_index].boundary_case == 2)
          CHECK(find_symbol(
                    &snapshot, "bound_before", AG_LANGUAGE_SYMBOL_OBJECT) &&
                    !find_symbol(
                        &snapshot, "bound_after", AG_LANGUAGE_SYMBOL_OBJECT),
                "declarator array bound preserves cursor lookup point");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_inline_tag_object_hover(ag_target_info_t target) {
  static const struct {
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *later_object;
  } cases[] = {
      {"inline_anonymous_record", AG_LANGUAGE_SYMBOL_OBJECT,
       "inline_after_file"},
      {"inline_named_record", AG_LANGUAGE_SYMBOL_OBJECT,
       "inline_after_file"},
      {"inline_anonymous_union", AG_LANGUAGE_SYMBOL_OBJECT,
       "inline_after_file"},
      {"inline_named_union", AG_LANGUAGE_SYMBOL_OBJECT,
       "inline_after_file"},
      {"inline_anonymous_enum", AG_LANGUAGE_SYMBOL_OBJECT,
       "inline_after_file"},
      {"inline_named_enum", AG_LANGUAGE_SYMBOL_OBJECT,
       "inline_after_file"},
      {"inline_nested_record", AG_LANGUAGE_SYMBOL_OBJECT,
       "inline_after_file"},
      {"inline_initialized", AG_LANGUAGE_SYMBOL_OBJECT,
       "inline_after_file"},
      {"inline_second", AG_LANGUAGE_SYMBOL_OBJECT,
       "inline_after_file"},
      {"inline_local", AG_LANGUAGE_SYMBOL_OBJECT,
       "inline_after_local"},
      {"inline_parameter_value", AG_LANGUAGE_SYMBOL_PARAMETER,
       "inline_after_file"},
      {"InlineRecordTypedef", AG_LANGUAGE_SYMBOL_TYPEDEF,
       "inline_after_file"},
      {"NamedInlineRecordTypedef", AG_LANGUAGE_SYMBOL_TYPEDEF,
       "inline_after_file"},
      {"InlineEnumTypedef", AG_LANGUAGE_SYMBOL_TYPEDEF,
       "inline_after_file"},
      {"inline_commented_record", AG_LANGUAGE_SYMBOL_OBJECT,
       "inline_after_file"},
  };
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  CHECK(session != NULL, "inline tag object session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  for (size_t case_index = 0;
       case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
    const char *declaration = strstr(
        inline_tag_object_source, cases[case_index].name);
    CHECK(declaration != NULL, "inline tag object declaration anchor");
    size_t name_length = strlen(cases[case_index].name);
    size_t deltas[] = {0, name_length / 2, name_length};
    for (size_t delta_index = 0;
         delta_index < sizeof(deltas) / sizeof(deltas[0]);
         delta_index++) {
      for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
        ag_compilation_session_t *analysis_session =
            fresh_session ? ag_compilation_session_create(&target) : session;
        CHECK(analysis_session != NULL,
              "inline tag object fresh session");
        CHECK(analyze_named(
                  analysis_session, "inline-tag-object.c",
                  inline_tag_object_source,
                  (size_t)(declaration - inline_tag_object_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "inline tag object analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        CHECK(!snapshot.partial && snapshot.diagnostic_count == 0 &&
                  hover && strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->kind == cases[case_index].kind &&
                  hover->declaration.source_name &&
                  strcmp(hover->declaration.source_name,
                         "inline-tag-object.c") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration - inline_tag_object_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration - inline_tag_object_source) +
                          (int)name_length &&
                  !find_symbol(&snapshot, cases[case_index].later_object,
                               AG_LANGUAGE_SYMBOL_OBJECT),
              "inline tag object snapshot");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_for_init_declaration_hover(ag_target_info_t target) {
  static const char *const names[] = {
      "loop_plain",
      "loop_uninitialized",
      "loop_first",
      "loop_second",
      "loop_typedef",
      "loop_record",
      "loop_callback",
      "loop_parenthesized",
      "loop_commented",
  };
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  CHECK(session != NULL, "for init declaration session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *for_limit = strstr(
      for_init_declaration_hover_source, "for_limit");
  const char *for_before = strstr(
      for_init_declaration_hover_source, "for_before");
  CHECK(for_limit && for_before, "for init lookup anchors");
  for (size_t case_index = 0;
       case_index < sizeof(names) / sizeof(names[0]); case_index++) {
    const char *declaration = strstr(
        for_init_declaration_hover_source, names[case_index]);
    CHECK(declaration != NULL, "for init declaration anchor");
    size_t name_length = strlen(names[case_index]);
    size_t deltas[] = {0, name_length / 2, name_length};
    for (size_t delta_index = 0;
         delta_index < sizeof(deltas) / sizeof(deltas[0]);
         delta_index++) {
      for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
        ag_compilation_session_t *analysis_session =
            fresh_session ? ag_compilation_session_create(&target) : session;
        CHECK(analysis_session != NULL,
              "for init declaration fresh session");
        CHECK(analyze_named(
                  analysis_session, "for-init-hover.c",
                  for_init_declaration_hover_source,
                  (size_t)(declaration -
                           for_init_declaration_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "for init declaration analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        CHECK(!snapshot.partial && snapshot.diagnostic_count == 0 &&
                  hover &&
                  strcmp(hover->name, names[case_index]) == 0 &&
                  hover->kind == AG_LANGUAGE_SYMBOL_OBJECT &&
                  hover->declaration.source_name &&
                  strcmp(hover->declaration.source_name,
                         "for-init-hover.c") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            for_init_declaration_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            for_init_declaration_hover_source) +
                          (int)name_length &&
                  find_symbol(&snapshot, "for_limit",
                              AG_LANGUAGE_SYMBOL_PARAMETER) &&
                  find_symbol(&snapshot, "for_before",
                              AG_LANGUAGE_SYMBOL_OBJECT) &&
                  !find_symbol(&snapshot, "for_after",
                               AG_LANGUAGE_SYMBOL_OBJECT) &&
                  !find_symbol(&snapshot, "for_file_after",
                               AG_LANGUAGE_SYMBOL_OBJECT),
              "for init declaration snapshot");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  static const struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
  } neighboring_uses[] = {
      {"for (ForInitType", "ForInitType", AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"loop_plain < for_limit", "loop_plain",
       AG_LANGUAGE_SYMBOL_OBJECT},
  };
  for (size_t case_index = 0;
       case_index < sizeof(neighboring_uses) /
                        sizeof(neighboring_uses[0]);
       case_index++) {
    const char *fragment = strstr(
        for_init_declaration_hover_source,
        neighboring_uses[case_index].fragment);
    const char *use = fragment ? strstr(
        fragment, neighboring_uses[case_index].name) : NULL;
    const char *declaration = strstr(
        for_init_declaration_hover_source,
        neighboring_uses[case_index].name);
    CHECK(use && declaration, "for init neighboring use anchors");
    CHECK(analyze_named(
              session, "for-init-hover.c",
              for_init_declaration_hover_source,
              (size_t)(use - for_init_declaration_hover_source) +
                  strlen(neighboring_uses[case_index].name) / 2,
              (header_bundle_t){0}, defaults, &snapshot, &error),
          "for init neighboring use analysis");
    const ag_language_symbol_t *hover = hover_symbol(&snapshot);
    CHECK(!snapshot.partial && snapshot.diagnostic_count == 0 &&
              hover &&
              strcmp(hover->name,
                     neighboring_uses[case_index].name) == 0 &&
              hover->kind == neighboring_uses[case_index].kind &&
              hover->declaration.start.offset ==
                  (int)(declaration -
                        for_init_declaration_hover_source) &&
              hover->declaration.end.offset ==
                  (int)(declaration -
                        for_init_declaration_hover_source) +
                      (int)strlen(neighboring_uses[case_index].name),
          "for init neighboring use snapshot");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_prototype_parameter_bound_hover(
    ag_target_info_t target) {
  static const struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *later_parameter;
  } cases[] = {
      {"direct_values[direct_count]", "direct_count",
       AG_LANGUAGE_SYMBOL_PARAMETER, "direct_later"},
      {"static_values[static static_count]", "static_count",
       AG_LANGUAGE_SYMBOL_PARAMETER, "static_later"},
      {"qualified_values[const qualified_count]", "qualified_count",
       AG_LANGUAGE_SYMBOL_PARAMETER, "qualified_later"},
      {"expr_values[(expr_rows + expr_columns)]", "expr_rows",
       AG_LANGUAGE_SYMBOL_PARAMETER, "expr_later"},
      {"expr_values[(expr_rows + expr_columns)]", "expr_columns",
       AG_LANGUAGE_SYMBOL_PARAMETER, "expr_later"},
      {"grouped_values[(grouped_count)]", "grouped_count",
       AG_LANGUAGE_SYMBOL_PARAMETER, "grouped_later"},
      {"inner_values[inner_rows][inner_columns]", "inner_columns",
       AG_LANGUAGE_SYMBOL_PARAMETER, "inner_later"},
      {"(*pointer_values)[pointer_count]", "pointer_count",
       AG_LANGUAGE_SYMBOL_PARAMETER, "pointer_later"},
      {"comment_values[/* gap */ comment_count]", "comment_count",
       AG_LANGUAGE_SYMBOL_PARAMETER, "comment_later"},
      {"splice_lf_values[\\\nsplice_lf_count]", "splice_lf_count",
       AG_LANGUAGE_SYMBOL_PARAMETER, "splice_lf_later"},
      {"splice_crlf_values[\\\r\nsplice_crlf_count]",
       "splice_crlf_count", AG_LANGUAGE_SYMBOL_PARAMETER,
       "splice_crlf_later"},
      {"typedef_values[typedef_count]", "typedef_count",
       AG_LANGUAGE_SYMBOL_PARAMETER, "typedef_later"},
      {"definition_values[definition_count]", "definition_count",
       AG_LANGUAGE_SYMBOL_PARAMETER, "definition_later"},
      {"file_values[proto_bound_file]", "proto_bound_file",
       AG_LANGUAGE_SYMBOL_OBJECT, "file_later"},
      {"enum_values[PROTO_BOUND_ENUM]", "PROTO_BOUND_ENUM",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "enum_later"},
      {"macro_values[PROTO_BOUND_MACRO]", "PROTO_BOUND_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, "macro_later"},
  };
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  CHECK(session != NULL, "prototype parameter bound session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *macro_comment = strstr(
      prototype_parameter_bound_hover_source,
      "/// prototype bound macro documentation");
  CHECK(macro_comment != NULL, "prototype bound macro comment anchor");
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          prototype_parameter_bound_hover_source,
          cases[case_index].fragment);
      const char *use = fragment ? strstr(
          fragment, cases[case_index].name) : NULL;
      const char *declaration = strstr(
          prototype_parameter_bound_hover_source,
          cases[case_index].name);
      CHECK(use && declaration, "prototype parameter bound anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]);
           delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "prototype parameter bound fresh session");
        }
        CHECK(analyze_named(
                  analysis_session, "prototype-parameter-bound.c",
                  prototype_parameter_bound_hover_source,
                  (size_t)(use -
                           prototype_parameter_bound_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "prototype parameter bound analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(!snapshot.partial && snapshot.diagnostic_count == 0 &&
                  hover && completion &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->kind == cases[case_index].kind &&
                  hover->declaration.source_name &&
                  strcmp(hover->declaration.source_name,
                         "prototype-parameter-bound.c") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            prototype_parameter_bound_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            prototype_parameter_bound_hover_source) +
                          (int)name_length &&
                  same_range(&hover->declaration,
                             &completion->declaration) &&
                  !find_symbol(&snapshot,
                               cases[case_index].later_parameter,
                               AG_LANGUAGE_SYMBOL_PARAMETER) &&
                  !find_symbol(&snapshot, "proto_bound_after",
                               AG_LANGUAGE_SYMBOL_OBJECT) &&
                  (strcmp(cases[case_index].name,
                          "definition_count") != 0 ||
                   !find_symbol(&snapshot, "definition_body",
                                AG_LANGUAGE_SYMBOL_OBJECT)),
              "prototype parameter bound snapshot");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(strcmp(hover->constant_value, "5") == 0,
                "prototype bound enum value");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
          CHECK(hover->macro_replacement &&
                    strcmp(hover->macro_replacement, "7") == 0 &&
                    check_documentation_symbol(
                        hover, "prototype bound macro documentation",
                        "prototype-parameter-bound.c",
                        (size_t)(macro_comment -
                                 prototype_parameter_bound_hover_source),
                        (size_t)(macro_comment -
                                 prototype_parameter_bound_hover_source) +
                            strlen("/// prototype bound macro documentation")),
                "prototype bound macro fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_block_static_assert_hover(ag_target_info_t target) {
  static const struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    int nested;
  } cases[] = {
      {"sizeof(BlockAssertType) >= 1, \"type\"", "BlockAssertType",
       AG_LANGUAGE_SYMBOL_TYPEDEF, 0},
      {"_Alignof(BlockAssertType)", "BlockAssertType",
       AG_LANGUAGE_SYMBOL_TYPEDEF, 0},
      {"sizeof(struct BlockAssertRecord)", "BlockAssertRecord",
       AG_LANGUAGE_SYMBOL_TAG, 0},
      {"sizeof(int[BLOCK_ASSERT_ENUM])", "BLOCK_ASSERT_ENUM",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, 0},
      {"BLOCK_ASSERT_ENUM == 4", "BLOCK_ASSERT_ENUM",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, 0},
      {"_Static_assert(BLOCK_ASSERT_MACRO", "BLOCK_ASSERT_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, 0},
      {"(BlockAssertType)1 == 1", "BlockAssertType",
       AG_LANGUAGE_SYMBOL_TYPEDEF, 0},
      {"sizeof(BlockAssertType) /* ) , */", "BlockAssertType",
       AG_LANGUAGE_SYMBOL_TYPEDEF, 0},
      {"sizeof(BlockAssertType) >= 1, \"keyword comment\"",
       "BlockAssertType", AG_LANGUAGE_SYMBOL_TYPEDEF, 0},
      {"sizeof(\\\nBlockAssertType)", "BlockAssertType",
       AG_LANGUAGE_SYMBOL_TYPEDEF, 0},
      {"sizeof(\\\r\nBlockAssertType)", "BlockAssertType",
       AG_LANGUAGE_SYMBOL_TYPEDEF, 0},
      {"sizeof(BlockAssertType) >= 1, \"nested\"", "BlockAssertType",
       AG_LANGUAGE_SYMBOL_TYPEDEF, 1},
  };
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  CHECK(session != NULL, "block static assert session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *macro_comment = strstr(
      block_static_assert_hover_source,
      "/// block static assert macro documentation");
  CHECK(macro_comment != NULL, "block static assert macro comment anchor");
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          block_static_assert_hover_source, cases[case_index].fragment);
      const char *use = fragment ? strstr(
          fragment, cases[case_index].name) : NULL;
      const char *declaration = strstr(
          block_static_assert_hover_source, cases[case_index].name);
      CHECK(use && declaration, "block static assert hover anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]);
           delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "block static assert fresh session");
        }
        CHECK(analyze_named(
                  analysis_session, "block-static-assert.c",
                  block_static_assert_hover_source,
                  (size_t)(use - block_static_assert_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "block static assert analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(!snapshot.partial && snapshot.diagnostic_count == 0 &&
                  hover && completion &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->kind == cases[case_index].kind &&
                  hover->declaration.source_name &&
                  strcmp(hover->declaration.source_name,
                         "block-static-assert.c") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration - block_static_assert_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration - block_static_assert_hover_source) +
                          (int)name_length &&
                  same_range(&hover->declaration,
                             &completion->declaration) &&
                  !find_symbol(&snapshot, "block_assert_after",
                               AG_LANGUAGE_SYMBOL_OBJECT) &&
                  !find_symbol(&snapshot, "block_assert_file_after",
                               AG_LANGUAGE_SYMBOL_OBJECT) &&
                  (!cases[case_index].nested ||
                   !find_symbol(&snapshot, "block_assert_nested_after",
                                AG_LANGUAGE_SYMBOL_OBJECT)),
              "block static assert hover snapshot");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(hover->constant_value &&
                    strcmp(hover->constant_value, "4") == 0,
                "block static assert enum value");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
          CHECK(hover->macro_replacement &&
                    strcmp(hover->macro_replacement, "1") == 0 &&
                    check_documentation_symbol(
                        hover, "block static assert macro documentation",
                        "block-static-assert.c",
                        (size_t)(macro_comment -
                                 block_static_assert_hover_source),
                        (size_t)(macro_comment -
                                 block_static_assert_hover_source) +
                            strlen(
                                "/// block static assert macro documentation")),
                "block static assert macro fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_do_body_hover(ag_target_info_t target) {
  static const struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *later_body_object;
  } cases[] = {
      {"do { do_body_object--", "do_body_object",
       AG_LANGUAGE_SYMBOL_OBJECT, NULL},
      {"do { do_body_parameter--", "do_body_parameter",
       AG_LANGUAGE_SYMBOL_PARAMETER, NULL},
      {"+= DO_BODY_ENUM", "DO_BODY_ENUM",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, NULL},
      {"+= DO_BODY_MACRO", "DO_BODY_MACRO",
       AG_LANGUAGE_SYMBOL_MACRO, NULL},
      {"= do_body_helper", "do_body_helper",
       AG_LANGUAGE_SYMBOL_FUNCTION, NULL},
      {"do_body_helper(do_body_parameter)", "do_body_parameter",
       AG_LANGUAGE_SYMBOL_PARAMETER, NULL},
      {"+= do_body_local", "do_body_local",
       AG_LANGUAGE_SYMBOL_OBJECT, "do_body_local_after"},
      {"+= do_body_nested", "do_body_nested",
       AG_LANGUAGE_SYMBOL_OBJECT, "do_body_nested_after"},
      {"do /* gap */ { do_body_object--", "do_body_object",
       AG_LANGUAGE_SYMBOL_OBJECT, NULL},
      {"do \\\n{ do_body_object--", "do_body_object",
       AG_LANGUAGE_SYMBOL_OBJECT, NULL},
      {"do \\\r\n{ do_body_object--", "do_body_object",
       AG_LANGUAGE_SYMBOL_OBJECT, NULL},
      {"do do_body_object--", "do_body_object",
       AG_LANGUAGE_SYMBOL_OBJECT, NULL},
      {"if (do_body_parameter)", "do_body_parameter",
       AG_LANGUAGE_SYMBOL_PARAMETER, NULL},
      {") do_body_object--; while (do_body_object < 9)",
       "do_body_object", AG_LANGUAGE_SYMBOL_OBJECT, NULL},
      {"do { do { do_body_object--", "do_body_object",
       AG_LANGUAGE_SYMBOL_OBJECT, NULL},
      {"while (do_body_object > 20)", "do_body_object",
       AG_LANGUAGE_SYMBOL_OBJECT, NULL},
      {"do_body_object += do_body_parameter", "do_body_object",
       AG_LANGUAGE_SYMBOL_OBJECT, NULL},
      {"do_body_object += do_body_parameter", "do_body_parameter",
       AG_LANGUAGE_SYMBOL_PARAMETER, NULL},
  };
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  CHECK(session != NULL, "do body hover session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  const char *macro_comment = strstr(
      do_body_hover_source, "/// do body macro documentation");
  CHECK(macro_comment != NULL, "do body macro comment anchor");
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          do_body_hover_source, cases[case_index].fragment);
      const char *use = fragment ? strstr(
          fragment, cases[case_index].name) : NULL;
      const char *declaration = strstr(
          do_body_hover_source, cases[case_index].name);
      CHECK(use && declaration, "do body hover anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]);
           delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL, "do body hover fresh session");
        }
        CHECK(analyze_named(
                  analysis_session, "do-body-hover.c",
                  do_body_hover_source,
                  (size_t)(use - do_body_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "do body hover analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(!snapshot.partial && snapshot.diagnostic_count == 0 &&
                  hover && completion &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->kind == cases[case_index].kind &&
                  hover->declaration.source_name &&
                  strcmp(hover->declaration.source_name,
                         "do-body-hover.c") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration - do_body_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration - do_body_hover_source) +
                          (int)name_length &&
                  same_range(&hover->declaration,
                             &completion->declaration) &&
                  !find_symbol(&snapshot, "do_body_after",
                               AG_LANGUAGE_SYMBOL_OBJECT) &&
                  !find_symbol(&snapshot, "do_body_file_after",
                               AG_LANGUAGE_SYMBOL_OBJECT) &&
                  (!cases[case_index].later_body_object ||
                   !find_symbol(&snapshot,
                                cases[case_index].later_body_object,
                                AG_LANGUAGE_SYMBOL_OBJECT)),
              "do body hover snapshot");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT)
          CHECK(hover->constant_value &&
                    strcmp(hover->constant_value, "3") == 0,
                "do body enum value");
        if (cases[case_index].kind == AG_LANGUAGE_SYMBOL_MACRO)
          CHECK(hover->macro_replacement &&
                    strcmp(hover->macro_replacement, "1") == 0 &&
                    check_documentation_symbol(
                        hover, "do body macro documentation",
                        "do-body-hover.c",
                        (size_t)(macro_comment - do_body_hover_source),
                        (size_t)(macro_comment - do_body_hover_source) +
                            strlen("/// do body macro documentation")),
                "do body macro fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_offsetof_type_hover(ag_target_info_t target) {
  static const struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
  } cases[] = {
      {"__builtin_offsetof(struct OffsetRecord", "OffsetRecord",
       AG_LANGUAGE_SYMBOL_TAG},
      {"__builtin_offsetof(OffsetType", "OffsetType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"__builtin_offsetof(union OffsetUnion", "OffsetUnion",
       AG_LANGUAGE_SYMBOL_TAG},
      {"__builtin_offsetof(const OffsetType", "OffsetType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"__builtin_offsetof(struct OffsetOuter", "OffsetOuter",
       AG_LANGUAGE_SYMBOL_TAG},
      {"int offset_macro = offsetof(OffsetType", "OffsetType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"/* gap */ (OffsetType", "OffsetType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"__builtin_offsetof \\\n(OffsetType", "OffsetType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"__builtin_offsetof \\\r\n(OffsetType", "OffsetType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"OFFSET_ENUM = __builtin_offsetof(OffsetType", "OffsetType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"__builtin_offsetof(OffsetLocalType", "OffsetLocalType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"__builtin_offsetof(struct OffsetLocalRecord", "OffsetLocalRecord",
       AG_LANGUAGE_SYMBOL_TAG},
      {"offset_sink(__builtin_offsetof(OffsetType", "OffsetType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
  };
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  CHECK(session != NULL, "offsetof type hover session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          offsetof_type_hover_source, cases[case_index].fragment);
      const char *use = fragment ? strstr(
          fragment, cases[case_index].name) : NULL;
      const char *declaration = strstr(
          offsetof_type_hover_source, cases[case_index].name);
      CHECK(use && declaration, "offsetof type hover anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]);
           delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "offsetof type hover fresh session");
        }
        CHECK(analyze_named(
                  analysis_session, "offsetof-type-hover.c",
                  offsetof_type_hover_source,
                  (size_t)(use - offsetof_type_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "offsetof type hover analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(!snapshot.partial && snapshot.diagnostic_count == 0 &&
                  hover && completion &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->kind == cases[case_index].kind &&
                  hover->declaration.source_name &&
                  strcmp(hover->declaration.source_name,
                         "offsetof-type-hover.c") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration - offsetof_type_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration - offsetof_type_hover_source) +
                          (int)name_length &&
                  same_range(&hover->declaration,
                             &completion->declaration) &&
                  !find_symbol(&snapshot, "offset_after",
                               AG_LANGUAGE_SYMBOL_OBJECT) &&
                  !find_symbol(&snapshot, "offset_file_after",
                               AG_LANGUAGE_SYMBOL_OBJECT),
              "offsetof type hover snapshot");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_initializer_operand_hover(ag_target_info_t target) {
  static const struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
  } cases[] = {
      {"sizeof((InitializerType", "InitializerType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"initializer_compound = (InitializerType", "InitializerType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"initializer_qualified = (const InitializerType", "InitializerType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"initializer_tag = (struct InitializerRecord", "InitializerRecord",
       AG_LANGUAGE_SYMBOL_TAG},
      {"initializer_union = (InitializerUnionType", "InitializerUnionType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"(/* type */ InitializerType", "InitializerType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"initializer_lf = (\\\nInitializerType", "InitializerType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"initializer_crlf = (\\\r\nInitializerType", "InitializerType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"initializer_sink((InitializerType", "InitializerType",
       AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"copy = initializer_parameter",
       "initializer_parameter", AG_LANGUAGE_SYMBOL_PARAMETER},
      {"initializer_local_copy = initializer_compound",
       "initializer_compound", AG_LANGUAGE_SYMBOL_OBJECT},
      {"initializer_comment_copy = initializer_compound",
       "initializer_compound", AG_LANGUAGE_SYMBOL_OBJECT},
      {"initializer_loop = initializer_compound",
       "initializer_compound", AG_LANGUAGE_SYMBOL_OBJECT},
      {"copy = initializer_scalar",
       "initializer_scalar", AG_LANGUAGE_SYMBOL_OBJECT},
  };
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  CHECK(session != NULL, "initializer operand hover session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          initializer_operand_hover_source, cases[case_index].fragment);
      const char *use = fragment ? strstr(fragment, cases[case_index].name)
                                 : NULL;
      const char *declaration = strstr(
          initializer_operand_hover_source, cases[case_index].name);
      CHECK(use && declaration, "initializer operand hover anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]);
           delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "initializer operand hover fresh session");
        }
        CHECK(analyze_named(
                  analysis_session, "initializer-operand-hover.c",
                  initializer_operand_hover_source,
                  (size_t)(use - initializer_operand_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "initializer operand hover analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(!snapshot.partial && snapshot.diagnostic_count == 0 &&
                  hover && completion &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->kind == cases[case_index].kind &&
                  hover->declaration.source_name &&
                  strcmp(hover->declaration.source_name,
                         "initializer-operand-hover.c") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration - initializer_operand_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration - initializer_operand_hover_source) +
                          (int)name_length &&
                  same_range(&hover->declaration,
                             &completion->declaration) &&
                  !find_symbol(&snapshot, "initializer_after",
                               AG_LANGUAGE_SYMBOL_OBJECT) &&
                  !find_symbol(&snapshot, "initializer_file_after",
                               AG_LANGUAGE_SYMBOL_OBJECT),
              "initializer operand hover snapshot");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_direct_aggregate_operand_hover(ag_target_info_t target) {
  static const struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
  } cases[] = {
      {"return direct_return_parameter", "direct_return_parameter",
       AG_LANGUAGE_SYMBOL_PARAMETER},
      {"return direct_union_return_parameter",
       "direct_union_return_parameter", AG_LANGUAGE_SYMBOL_PARAMETER},
      {"direct_target = direct_source", "direct_source",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"direct_target = direct_operand_parameter",
       "direct_operand_parameter", AG_LANGUAGE_SYMBOL_PARAMETER},
      {"direct_union_target = direct_union_source", "direct_union_source",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"direct_aggregate_sink(direct_source", "direct_source",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"direct_aggregate_identity(direct_source", "direct_source",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"direct_source /* tail */", "direct_source",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"direct_source \\\n);", "direct_source", AG_LANGUAGE_SYMBOL_OBJECT},
      {"direct_source \\\r\n);", "direct_source",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"  direct_source;", "direct_source", AG_LANGUAGE_SYMBOL_OBJECT},
      {"&direct_source", "direct_source", AG_LANGUAGE_SYMBOL_OBJECT},
      {"sizeof direct_source", "direct_source", AG_LANGUAGE_SYMBOL_OBJECT},
      {"direct_source.member", "direct_source", AG_LANGUAGE_SYMBOL_OBJECT},
  };
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  CHECK(session != NULL, "direct aggregate operand hover session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          direct_aggregate_operand_hover_source,
          cases[case_index].fragment);
      const char *use = fragment ? strstr(fragment, cases[case_index].name)
                                 : NULL;
      const char *declaration = strstr(
          direct_aggregate_operand_hover_source, cases[case_index].name);
      CHECK(use && declaration, "direct aggregate operand hover anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]);
           delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "direct aggregate operand hover fresh session");
        }
        CHECK(analyze_named(
                  analysis_session, "direct-aggregate-operand-hover.c",
                  direct_aggregate_operand_hover_source,
                  (size_t)(use - direct_aggregate_operand_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "direct aggregate operand hover analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(!snapshot.partial && snapshot.diagnostic_count == 0 &&
                  hover && completion &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->kind == cases[case_index].kind &&
                  hover->declaration.source_name &&
                  strcmp(hover->declaration.source_name,
                         "direct-aggregate-operand-hover.c") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            direct_aggregate_operand_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            direct_aggregate_operand_hover_source) +
                          (int)name_length &&
                  same_range(&hover->declaration,
                             &completion->declaration) &&
                  !find_symbol(&snapshot, "direct_after",
                               AG_LANGUAGE_SYMBOL_OBJECT) &&
                  !find_symbol(&snapshot, "direct_file_after",
                               AG_LANGUAGE_SYMBOL_OBJECT),
              "direct aggregate operand hover snapshot");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_simple_remaining_call_argument_hover(
    ag_target_info_t target) {
  static const struct {
    const char *fragment;
    const char *name;
    ag_language_symbol_kind_t kind;
  } cases[] = {
      {"simple_take_pair(simple_local, 1)", "simple_local",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"simple_take_three(1, simple_local, 2)", "simple_local",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"simple_take_two_items(simple_local, simple_parameter)",
       "simple_local", AG_LANGUAGE_SYMBOL_OBJECT},
      {"simple_take_scalars(simple_scalar_parameter, SIMPLE_CALL_ENUM)",
       "simple_scalar_parameter", AG_LANGUAGE_SYMBOL_PARAMETER},
      {"simple_take_scalars(SIMPLE_CALL_ENUM, simple_scalar_parameter)",
       "SIMPLE_CALL_ENUM", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT},
      {"simple_take_scalars(SIMPLE_CALL_MACRO, simple_scalar_parameter)",
       "SIMPLE_CALL_MACRO", AG_LANGUAGE_SYMBOL_MACRO},
      {"simple_take_union(simple_union_parameter, simple_scalar_parameter)",
       "simple_union_parameter", AG_LANGUAGE_SYMBOL_PARAMETER},
      {"simple_local /* first */, simple_scalar_parameter", "simple_local",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"simple_local \\\n, simple_scalar_parameter", "simple_local",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"simple_local \\\r\n, simple_scalar_parameter", "simple_local",
       AG_LANGUAGE_SYMBOL_OBJECT},
      {"simple_wrap_scalar(simple_take_pair(simple_local, simple_scalar_parameter))",
       "simple_local", AG_LANGUAGE_SYMBOL_OBJECT},
      {"simple_take_four(simple_local, simple_scalar_parameter, SIMPLE_CALL_ENUM, 3)",
       "simple_local", AG_LANGUAGE_SYMBOL_OBJECT},
      {"return simple_take_pair(simple_local, 3)", "simple_local",
       AG_LANGUAGE_SYMBOL_OBJECT},
  };
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  CHECK(session != NULL, "simple remaining call argument hover session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
      const char *fragment = strstr(
          simple_remaining_call_argument_hover_source,
          cases[case_index].fragment);
      const char *use = fragment ? strstr(fragment, cases[case_index].name)
                                 : NULL;
      const char *declaration = strstr(
          simple_remaining_call_argument_hover_source,
          cases[case_index].name);
      CHECK(use && declaration,
            "simple remaining call argument hover anchors");
      size_t name_length = strlen(cases[case_index].name);
      size_t deltas[] = {0, name_length / 2, name_length};
      for (size_t delta_index = 0;
           delta_index < sizeof(deltas) / sizeof(deltas[0]);
           delta_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "simple remaining call argument hover fresh session");
        }
        CHECK(analyze_named(
                  analysis_session,
                  "simple-remaining-call-argument-hover.c",
                  simple_remaining_call_argument_hover_source,
                  (size_t)(use -
                           simple_remaining_call_argument_hover_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "simple remaining call argument hover analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, cases[case_index].name, cases[case_index].kind);
        CHECK(!snapshot.partial && snapshot.diagnostic_count == 0 &&
                  hover && completion &&
                  strcmp(hover->name, cases[case_index].name) == 0 &&
                  hover->kind == cases[case_index].kind &&
                  hover->declaration.source_name &&
                  strcmp(hover->declaration.source_name,
                         "simple-remaining-call-argument-hover.c") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(declaration -
                            simple_remaining_call_argument_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(declaration -
                            simple_remaining_call_argument_hover_source) +
                          (int)name_length &&
                  same_range(&hover->declaration,
                             &completion->declaration) &&
                  !find_symbol(&snapshot, "simple_after",
                               AG_LANGUAGE_SYMBOL_OBJECT) &&
                  !find_symbol(&snapshot, "simple_file_after",
                               AG_LANGUAGE_SYMBOL_OBJECT),
              "simple remaining call argument hover snapshot");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  ag_compilation_session_destroy(session);
  return 0;
}

static int test_macro_definition_hover(ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "macro definition session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};

  struct {
    const char *name;
    const char *definition_fragment;
    const char *raw_definition_name;
    const char *replacement;
    int parameter_count;
  } forms[] = {
      {"SIMPLE_MACRO", "#define SIMPLE_MACRO 1", "SIMPLE_MACRO",
       "1", 0},
      {"PARENTHESIZED_MACRO", "# define PARENTHESIZED_MACRO (2 + 3)",
       "PARENTHESIZED_MACRO", "( 2 + 3 )", 0},
      {"FUNCTION_MACRO",
       "#define FUNCTION_MACRO(value, other) ((value) + (other))",
       "FUNCTION_MACRO", "( ( value ) + ( other ) )", 2},
      {"EMPTY_MACRO", "#define EMPTY_MACRO", "EMPTY_MACRO", "", 0},
      {"CONTINUED_OBJECT", "#define CONTINUED_OBJECT (1 + \\\n  2)",
       "CONTINUED_OBJECT", "( 1 + 2 )", 0},
      {"CONTINUED_FUNCTION",
       "#define CONTINUED_FUNCTION(value) ((value) + \\\r\n  1)",
       "CONTINUED_FUNCTION", "( ( value ) + 1 )", 1},
      {"TRIGRAPH_HASH_MACRO", "?" "?= define TRIGRAPH_HASH_MACRO 7",
       "TRIGRAPH_HASH_MACRO", "7", 0},
      {"SPLICED_NAME_MACRO", "#define SPL\\\nICED_NAME_MACRO 8",
       "SPL\\\nICED_NAME_MACRO", "8", 0},
      {"SPLIT_DEFINE_MACRO", "#def\\\nine SPLIT_DEFINE_MACRO 9",
       "SPLIT_DEFINE_MACRO", "9", 0},
      {"BRANCH_MACRO", "#define BRANCH_MACRO 11", "BRANCH_MACRO",
       "11", 0},
      {"REDEFINED_MACRO", "#define REDEFINED_MACRO 13",
       "REDEFINED_MACRO", "13", 0},
  };
  for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
    const char *fragment = strstr(
        macro_definition_forms_source, forms[i].definition_fragment);
    const char *definition = fragment
                                 ? strstr(fragment, forms[i].raw_definition_name)
                                 : NULL;
    CHECK(definition, "macro definition form anchors");
    size_t raw_length = strlen(forms[i].raw_definition_name);
    size_t deltas[] = {0, raw_length / 2, raw_length};
    for (size_t delta = 0;
         delta < sizeof(deltas) / sizeof(deltas[0]); delta++) {
      CHECK(analyze_named(
                session, "macro-definition.c",
                macro_definition_forms_source,
                (size_t)(definition - macro_definition_forms_source) +
                    deltas[delta],
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "macro definition form analysis");
      CHECK(macro_definition_snapshot_matches(
                &snapshot, forms[i].name, forms[i].replacement,
                forms[i].parameter_count),
            "macro definition form fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  const char *old_redefinition = strstr(
      macro_definition_forms_source, "#define REDEFINED_MACRO 12");
  old_redefinition = old_redefinition
                         ? strstr(old_redefinition, "REDEFINED_MACRO")
                         : NULL;
  CHECK(old_redefinition && analyze_named(
            session, "macro-definition.c", macro_definition_forms_source,
            (size_t)(old_redefinition - macro_definition_forms_source) + 3,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "macro definition before undef analysis");
  CHECK(macro_definition_snapshot_matches(
            &snapshot, "REDEFINED_MACRO", "12", 0),
        "macro definition before undef fields");
  ag_language_analysis_snapshot_dispose(&snapshot);

  struct {
    const char *fragment;
    const char *name;
    const char *replacement;
  } undef_cases[] = {
      {"#undef REDEFINED_MACRO", "REDEFINED_MACRO", "12"},
      {"#undef /* undef gap */ COMMENT_UNDEF_MACRO",
       "COMMENT_UNDEF_MACRO", "14"},
      {"#undef \\\n  SPLICED_UNDEF_MACRO",
       "SPLICED_UNDEF_MACRO", "15"},
  };
  for (size_t case_index = 0;
       case_index < sizeof(undef_cases) / sizeof(undef_cases[0]);
       case_index++) {
    const char *fragment = strstr(
        macro_definition_forms_source, undef_cases[case_index].fragment);
    const char *operand = fragment
                              ? strstr(fragment,
                                       undef_cases[case_index].name)
                              : NULL;
    const char *definition = strstr(
        macro_definition_forms_source, undef_cases[case_index].name);
    CHECK(fragment && operand && definition,
          "undef directive macro anchors");
    size_t name_length = strlen(undef_cases[case_index].name);
    size_t deltas[] = {0, name_length / 2, name_length};
    for (size_t delta_index = 0;
         delta_index < sizeof(deltas) / sizeof(deltas[0]);
         delta_index++) {
      for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
        ag_compilation_session_t *analysis_session =
            fresh_session ? ag_compilation_session_create(&target) : session;
        CHECK(analysis_session != NULL,
              "undef directive macro fresh session");
        CHECK(analyze_named(
                  analysis_session, "macro-definition.c",
                  macro_definition_forms_source,
                  (size_t)(operand - macro_definition_forms_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "undef directive macro analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        int snapshot_matches =
            macro_definition_snapshot_matches(
                &snapshot, undef_cases[case_index].name,
                undef_cases[case_index].replacement, 0) &&
            hover->declaration.source_name &&
            strcmp(hover->declaration.source_name,
                   "macro-definition.c") == 0 &&
            hover->declaration.start.offset ==
                (int)(definition - macro_definition_forms_source) &&
            hover->declaration.end.offset ==
                (int)(definition - macro_definition_forms_source) +
                    (int)name_length;
        if (!snapshot_matches)
          fprintf(
              stderr,
              "undef directive case=%zu delta=%zu fresh=%d "
              "hover=%s partial=%d diagnostics=%d\n",
              case_index, deltas[delta_index], fresh_session,
              hover && hover->name ? hover->name : "<null>",
              snapshot.partial, snapshot.diagnostic_count);
        CHECK(snapshot_matches, "undef directive macro snapshot");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }

  struct {
    const char *fragment;
    const char *name;
    const char *later_object;
  } undefined_directive_cases[] = {
      {"#undef NEVER_DEFINED_UNDEF_MACRO",
       "NEVER_DEFINED_UNDEF_MACRO", NULL},
      {"#ifndef NEVER_DEFINED_CONDITIONAL_MACRO\n"
       "int conditional_ifndef_undefined_value",
       "NEVER_DEFINED_CONDITIONAL_MACRO",
       "conditional_ifndef_undefined_value"},
      {"#elif NEVER_DEFINED_ELIF_MACRO\n"
       "int conditional_elif_undefined_hidden_value",
       "NEVER_DEFINED_ELIF_MACRO",
       "conditional_elif_undefined_hidden_value"},
      {"#elif defined(NEVER_DEFINED_ELIF_MACRO)\n"
       "int conditional_elif_defined_undefined_hidden_value",
       "NEVER_DEFINED_ELIF_MACRO",
       "conditional_elif_defined_undefined_hidden_value"},
  };
  for (size_t case_index = 0;
       case_index < sizeof(undefined_directive_cases) /
                        sizeof(undefined_directive_cases[0]);
       case_index++) {
    const char *fragment = strstr(
        macro_definition_forms_source,
        undefined_directive_cases[case_index].fragment);
    const char *operand = fragment
                              ? strstr(
                                    fragment,
                                    undefined_directive_cases[case_index].name)
                              : NULL;
    CHECK(fragment && operand, "undefined directive macro anchors");
    size_t name_length = strlen(undefined_directive_cases[case_index].name);
    size_t deltas[] = {0, name_length / 2, name_length};
    for (size_t delta_index = 0;
         delta_index < sizeof(deltas) / sizeof(deltas[0]);
         delta_index++) {
      for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
        ag_compilation_session_t *analysis_session =
            fresh_session ? ag_compilation_session_create(&target) : session;
        CHECK(analysis_session != NULL,
              "undefined directive macro fresh session");
        CHECK(analyze_named(
                  analysis_session, "macro-definition.c",
                  macro_definition_forms_source,
                  (size_t)(operand - macro_definition_forms_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "undefined directive macro analysis");
        CHECK(!snapshot.partial && snapshot.diagnostic_count == 0 &&
                  !hover_symbol(&snapshot) &&
                  !find_symbol(
                      &snapshot, undefined_directive_cases[case_index].name,
                      AG_LANGUAGE_SYMBOL_MACRO) &&
                  (!undefined_directive_cases[case_index].later_object ||
                   !find_symbol(
                       &snapshot,
                       undefined_directive_cases[case_index].later_object,
                       AG_LANGUAGE_SYMBOL_OBJECT)),
              "undefined directive macro snapshot");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }

  const char *inactive_definition = strstr(
      macro_definition_forms_source, "#define BRANCH_MACRO 10");
  inactive_definition = inactive_definition
                            ? strstr(inactive_definition, "BRANCH_MACRO")
                            : NULL;
  CHECK(inactive_definition && analyze_named(
            session, "macro-definition.c", macro_definition_forms_source,
            (size_t)(inactive_definition - macro_definition_forms_source) + 2,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "inactive macro definition analysis");
  CHECK(find_symbol(
            &snapshot, "BRANCH_MACRO", AG_LANGUAGE_SYMBOL_MACRO) == NULL &&
            (!hover_symbol(&snapshot) ||
             hover_symbol(&snapshot)->kind != AG_LANGUAGE_SYMBOL_MACRO) &&
            find_diagnostic(&snapshot, "E3016") == NULL,
        "inactive macro definition omitted");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *pragma_identifier = strstr(
      macro_definition_forms_source, "macro_definition_boundary");
  CHECK(pragma_identifier && analyze_named(
            session, "macro-definition.c", macro_definition_forms_source,
            (size_t)(pragma_identifier - macro_definition_forms_source) + 2,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "non-define directive analysis");
  CHECK(!hover_symbol(&snapshot) &&
            find_diagnostic(&snapshot, "E3016") == NULL,
        "non-define directive has no macro hover");
  ag_language_analysis_snapshot_dispose(&snapshot);

  struct {
    const char *fragment;
    const char *name;
    const char *replacement;
    const char *later_object;
  } conditional_cases[] = {
      {"#ifdef SIMPLE_MACRO\nint conditional_ifdef_value",
       "SIMPLE_MACRO", "1", "conditional_ifdef_value"},
      {"#ifndef SIMPLE_MACRO\nint conditional_ifndef_hidden_value",
       "SIMPLE_MACRO", "1", "conditional_ifndef_hidden_value"},
      {"#ifndef /* condition gap */ SIMPLE_MACRO",
       "SIMPLE_MACRO", "1", "conditional_ifndef_comment_hidden_value"},
      {"#ifndef \\\n  SIMPLE_MACRO", "SIMPLE_MACRO", "1",
       "conditional_ifndef_spliced_hidden_value"},
      {"#if SIMPLE_MACRO\nint conditional_direct_value",
       "SIMPLE_MACRO", "1", "conditional_direct_value"},
      {"#if defined(SIMPLE_MACRO)", "SIMPLE_MACRO", "1",
       "conditional_defined_call_value"},
      {"#if defined SIMPLE_MACRO", "SIMPLE_MACRO", "1",
       "conditional_defined_space_value"},
      {"#if /* condition gap */ SIMPLE_MACRO", "SIMPLE_MACRO", "1",
       "conditional_comment_value"},
      {"#if \\\n  SIMPLE_MACRO", "SIMPLE_MACRO", "1",
       "conditional_spliced_value"},
      {"#elif SIMPLE_MACRO\nint conditional_elif_value",
       "SIMPLE_MACRO", "1", "conditional_elif_value"},
      {"#elif CONDITIONAL_FALSE_MACRO\n"
       "int conditional_elif_false_hidden_value",
       "CONDITIONAL_FALSE_MACRO", "0",
       "conditional_elif_false_hidden_value"},
      {"#elif defined(SIMPLE_MACRO)\n"
       "int conditional_elif_defined_value",
       "SIMPLE_MACRO", "1", "conditional_elif_defined_value"},
      {"#elif /* condition gap */ SIMPLE_MACRO",
       "SIMPLE_MACRO", "1", "conditional_elif_comment_value"},
      {"#elif \\\n  SIMPLE_MACRO", "SIMPLE_MACRO", "1",
       "conditional_elif_spliced_value"},
      {"#if 0\n#if 1\n"
       "int conditional_elif_nested_first_hidden;\n"
       "#endif\n#elif SIMPLE_MACRO",
       "SIMPLE_MACRO", "1", "conditional_elif_nested_value"},
      {"#i\\\nf 0\n"
       "int conditional_elif_split_opener_first_hidden;\n"
       "#elif CONDITIONAL_FALSE_MACRO",
       "CONDITIONAL_FALSE_MACRO", "0",
       "conditional_elif_split_opener_hidden_value"},
      {"# /* opener gap */ if 0\n"
       "int conditional_elif_comment_opener_first_hidden;\n"
       "#elif CONDITIONAL_FALSE_MACRO",
       "CONDITIONAL_FALSE_MACRO", "0",
       "conditional_elif_comment_opener_hidden_value"},
      {"# \\\r\nif 0\n"
       "int conditional_elif_spliced_opener_first_hidden;\n"
       "#elif CONDITIONAL_FALSE_MACRO",
       "CONDITIONAL_FALSE_MACRO", "0",
       "conditional_elif_spliced_opener_hidden_value"},
      {"#if 0\n#if 1\n"
       "int conditional_elif_split_endif_first_hidden;\n"
       "#end\\\nif\n#elif CONDITIONAL_FALSE_MACRO",
       "CONDITIONAL_FALSE_MACRO", "0",
       "conditional_elif_split_endif_hidden_value"},
      {"#if 0\n"
       "int conditional_elif_split_current_first_hidden;\n"
       "#el\\\nif CONDITIONAL_FALSE_MACRO",
       "CONDITIONAL_FALSE_MACRO", "0",
       "conditional_elif_split_current_hidden_value"},
      {"#if CONDITIONAL_FALSE_MACRO\nint conditional_false_hidden_value",
       "CONDITIONAL_FALSE_MACRO", "0",
       "conditional_false_hidden_value"},
      {"static int conditional_block(void) {\n#if SIMPLE_MACRO",
       "SIMPLE_MACRO", "1", NULL},
      {"static int conditional_false_block(void) {\n"
       "#if CONDITIONAL_FALSE_MACRO",
       "CONDITIONAL_FALSE_MACRO", "0", NULL},
      {"static int conditional_elif_false_block(void) {\n"
       "#if 0\n"
       "  return 1;\n"
       "#elif CONDITIONAL_FALSE_MACRO",
       "CONDITIONAL_FALSE_MACRO", "0", NULL},
  };
  for (size_t case_index = 0;
       case_index < sizeof(conditional_cases) /
                        sizeof(conditional_cases[0]);
       case_index++) {
    int uses_logical_line_source = strstr(
        conditional_logical_line_source,
        conditional_cases[case_index].fragment) != NULL;
    const char *case_source = uses_logical_line_source
                                  ? conditional_logical_line_source
                                  : macro_definition_forms_source;
    const char *case_source_name = uses_logical_line_source
                                       ? "conditional-logical-lines.c"
                                       : "macro-definition.c";
    const char *fragment = strstr(
        case_source, conditional_cases[case_index].fragment);
    const char *operand = fragment
                              ? strstr(fragment,
                                       conditional_cases[case_index].name)
                              : NULL;
    const char *definition_fragment =
        strcmp(conditional_cases[case_index].name,
               "CONDITIONAL_FALSE_MACRO") == 0
            ? "#define CONDITIONAL_FALSE_MACRO 0"
            : "#define SIMPLE_MACRO 1";
    const char *definition = strstr(
        case_source, definition_fragment);
    definition = definition
                     ? strstr(definition,
                              conditional_cases[case_index].name)
                     : NULL;
    CHECK(fragment && operand && definition,
          "conditional directive macro anchors");
    size_t name_length = strlen(conditional_cases[case_index].name);
    size_t deltas[] = {0, name_length / 2, name_length};
    for (size_t delta_index = 0;
         delta_index < sizeof(deltas) / sizeof(deltas[0]);
         delta_index++) {
      for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
        ag_compilation_session_t *analysis_session =
            fresh_session ? ag_compilation_session_create(&target) : session;
        CHECK(analysis_session != NULL,
              "conditional directive macro fresh session");
        CHECK(analyze_named(
                  analysis_session, case_source_name, case_source,
                  (size_t)(operand - case_source) +
                      deltas[delta_index],
                  (header_bundle_t){0}, defaults, &snapshot, &error),
              "conditional directive macro analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        int snapshot_matches =
            macro_definition_snapshot_matches(
                &snapshot, conditional_cases[case_index].name,
                conditional_cases[case_index].replacement, 0) &&
            hover->declaration.source_name &&
            strcmp(hover->declaration.source_name,
                   case_source_name) == 0 &&
            hover->declaration.start.offset ==
                (int)(definition - case_source) &&
            hover->declaration.end.offset ==
                (int)(definition - case_source) +
                    (int)name_length &&
            (!conditional_cases[case_index].later_object ||
             !find_symbol(
                 &snapshot, conditional_cases[case_index].later_object,
                 AG_LANGUAGE_SYMBOL_OBJECT));
        if (!snapshot_matches)
          fprintf(
              stderr,
              "conditional directive case=%zu delta=%zu fresh=%d "
              "hover=%s partial=%d diagnostics=%d decl=%d..%d "
              "expected=%td..%td later=%d\n",
              case_index, deltas[delta_index], fresh_session,
              hover && hover->name ? hover->name : "<null>",
              snapshot.partial, snapshot.diagnostic_count,
              hover ? hover->declaration.start.offset : -1,
              hover ? hover->declaration.end.offset : -1,
              definition - case_source,
              definition - case_source +
                  (ptrdiff_t)name_length,
              conditional_cases[case_index].later_object &&
                  find_symbol(
                      &snapshot,
                      conditional_cases[case_index].later_object,
                      AG_LANGUAGE_SYMBOL_OBJECT) != NULL);
        CHECK(snapshot_matches,
              "conditional directive macro snapshot");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }

  const char *trailing_sources[] = {
      "#define BEFORE_INCOMPLETE 1\nint unfinished(",
      "#define BEFORE_ERROR 2\nint broken = ;\n",
  };
  const char *trailing_names[] = {
      "BEFORE_INCOMPLETE", "BEFORE_ERROR",
  };
  const char *trailing_replacements[] = {"1", "2"};
  for (size_t i = 0; i < 2; i++) {
    const char *definition = strstr(trailing_sources[i], trailing_names[i]);
    CHECK(definition && analyze_named(
              session, "macro-trailing.c", trailing_sources[i],
              (size_t)(definition - trailing_sources[i]) + 2,
              (header_bundle_t){0}, defaults, &snapshot, &error),
          "macro definition before invalid trailing source");
    CHECK(macro_definition_snapshot_matches(
              &snapshot, trailing_names[i], trailing_replacements[i], 0),
          "macro definition ignores invalid trailing source");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }

  const char *invalid_macro =
      "#define INVALID_MACRO(value) ## value\nint after_invalid;\n";
  const char *invalid_name = strstr(invalid_macro, "INVALID_MACRO");
  int invalid_ok = invalid_name && analyze_named(
      session, "invalid-macro.c", invalid_macro,
      (size_t)(invalid_name - invalid_macro) + 2,
      (header_bundle_t){0}, defaults, &snapshot, &error);
  if (!((invalid_ok && snapshot.partial && snapshot.diagnostic_count > 0) ||
        (!invalid_ok && error.status == AG_LANGUAGE_ANALYSIS_FAILED)))
    fprintf(stderr,
            "invalid macro result: ok=%d partial=%d diagnostics=%d "
            "status=%d code=%s\n",
            invalid_ok, snapshot.partial, snapshot.diagnostic_count,
            error.status, error.code);
  CHECK((invalid_ok && snapshot.partial && snapshot.diagnostic_count > 0) ||
            (!invalid_ok && error.status == AG_LANGUAGE_ANALYSIS_FAILED),
        "invalid macro definition diagnostic preserved");
  ag_language_analysis_snapshot_dispose(&snapshot);

  size_t snake_length = 0;
  char *snake_source = read_fixture_source(
      "test/fixtures/language_analysis/macro_definition_snake.txt",
      &snake_length);
  CHECK(snake_source && snake_length == strlen(snake_source),
        "snake macro definition fixture");
  const char *game_paths[] = {"game.h"};
  const char *game_headers[] = {macro_definition_game_header};
  header_bundle_t game_bundle = make_bundle(game_paths, game_headers, 1);
  struct {
    const char *name;
    const char *replacement;
    const char *documentation;
    const char *comment;
  } snake_macros[] = {
      {"BOARD_COLUMNS", "( GAME_SCREEN_WIDTH / CELL_SIZE )",
       "盤面の横方向のマス数です。", "/// 盤面の横方向のマス数です。"},
      {"BOARD_ROWS", "( ( GAME_SCREEN_HEIGHT - BOARD_TOP ) / CELL_SIZE )",
       "盤面の縦方向のマス数です。", "/// 盤面の縦方向のマス数です。"},
      {"MAX_SNAKE_LENGTH", "( BOARD_COLUMNS * BOARD_ROWS )",
       "盤面に収まるヘビの最大の長さです。",
       "/// 盤面に収まるヘビの最大の長さです。"},
  };
  for (size_t i = 0;
       i < sizeof(snake_macros) / sizeof(snake_macros[0]); i++) {
    const char *definition = strstr(snake_source, snake_macros[i].name);
    const char *use = last_occurrence(snake_source, snake_macros[i].name);
    const char *comment = strstr(snake_source, snake_macros[i].comment);
    CHECK(definition && use && comment, "snake macro definition anchors");
    size_t deltas[] = {
        0, strlen(snake_macros[i].name) / 2,
        strlen(snake_macros[i].name),
    };
    for (size_t delta = 0;
         delta < sizeof(deltas) / sizeof(deltas[0]); delta++) {
      CHECK(analyze_named(
                session, "snake.c", snake_source,
                (size_t)(definition - snake_source) + deltas[delta],
                game_bundle, defaults, &snapshot, &error),
            "snake macro definition analysis");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      const ag_language_symbol_t *completion = find_symbol(
          &snapshot, snake_macros[i].name, AG_LANGUAGE_SYMBOL_MACRO);
      CHECK(macro_definition_snapshot_matches(
                &snapshot, snake_macros[i].name,
                snake_macros[i].replacement, 0) &&
                hover->declaration.start.offset ==
                    (int)(definition - snake_source) &&
                hover->declaration.end.offset ==
                    (int)(definition - snake_source) +
                        (int)strlen(snake_macros[i].name) &&
                check_documentation_symbol(
                    hover, snake_macros[i].documentation, "snake.c",
                    (size_t)(comment - snake_source),
                    (size_t)(comment - snake_source) +
                        strlen(snake_macros[i].comment)) &&
                check_documentation_symbol(
                    completion, snake_macros[i].documentation, "snake.c",
                    (size_t)(comment - snake_source),
                    (size_t)(comment - snake_source) +
                        strlen(snake_macros[i].comment)),
            "snake macro definition fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
    CHECK(analyze_named(
              session, "snake.c", snake_source,
              (size_t)(use - snake_source) + strlen(snake_macros[i].name) / 2,
              game_bundle, defaults, &snapshot, &error),
          "snake macro use analysis");
    CHECK(macro_definition_snapshot_matches(
              &snapshot, snake_macros[i].name,
              snake_macros[i].replacement, 0) &&
              check_documentation_symbol(
                  hover_symbol(&snapshot), snake_macros[i].documentation,
                  "snake.c", (size_t)(comment - snake_source),
                  (size_t)(comment - snake_source) +
                      strlen(snake_macros[i].comment)),
          "snake macro use fields");
    ag_language_analysis_snapshot_dispose(&snapshot);

    ag_compilation_session_t *fresh =
        ag_compilation_session_create(&target);
    CHECK(fresh && analyze_named(
              fresh, "snake.c", snake_source,
              (size_t)(definition - snake_source) +
                  strlen(snake_macros[i].name) / 2,
              game_bundle, defaults, &snapshot, &error),
          "fresh snake macro definition analysis");
    CHECK(macro_definition_snapshot_matches(
              &snapshot, snake_macros[i].name,
              snake_macros[i].replacement, 0),
          "fresh snake macro definition fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
    ag_compilation_session_destroy(fresh);
  }
  free(game_bundle.bytes);
  free(snake_source);

  const char *header_definition = strstr(
      macro_definition_header_source, "HEADER_DEFINITION");
  CHECK(header_definition && analyze_named(
            session, "macro-definition.h", macro_definition_header_source,
            (size_t)(header_definition -
                     macro_definition_header_source) + 3,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "virtual header macro definition analysis");
  CHECK(macro_definition_snapshot_matches(
            &snapshot, "HEADER_DEFINITION", "( ( value ) + 2 )", 1) &&
            strcmp(hover_symbol(&snapshot)->declaration.source_name,
                   "macro-definition.h") == 0,
        "virtual header macro definition fields");
  ag_language_analysis_snapshot_dispose(&snapshot);

  ag_language_project_index_t *project =
      ag_language_project_index_create();
  CHECK(project != NULL, "macro definition project");
  const char *project_docs[] = {
      "project definition v1", "project definition v2",
  };
  const char *project_replacements[] = {"21", "22"};
  for (size_t revision = 0; revision < 2; revision++) {
    CHECK(update_single_source_project(
              session, project, (unsigned int)revision + 1,
              macro_definition_project_sources[revision],
              (header_bundle_t){0}, defaults,
              &error),
          "macro definition project update");
    const char *definition = strstr(
        macro_definition_project_sources[revision], "PROJECT_DEFINITION");
    const char *comment = strstr(
        macro_definition_project_sources[revision], "///");
    const char *comment_end = strchr(comment, '\n');
    CHECK(definition && comment && comment_end && analyze_project_named(
              session, project, "main.c",
              macro_definition_project_sources[revision],
              (size_t)(definition -
                       macro_definition_project_sources[revision]) + 4,
              (header_bundle_t){0}, defaults, &snapshot, &error),
          "macro definition project analysis");
    CHECK(macro_definition_snapshot_matches(
              &snapshot, "PROJECT_DEFINITION",
              project_replacements[revision], 0) &&
              check_documentation_symbol(
                  hover_symbol(&snapshot), project_docs[revision], "main.c",
                  (size_t)(comment -
                           macro_definition_project_sources[revision]),
                  (size_t)(comment_end -
                           macro_definition_project_sources[revision])),
          "macro definition project revision fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  ag_language_project_index_destroy(project);

  const char *limit_source =
      "#define LIMIT_DEFINITION 1\nint unfinished(";
  const char *limit_name = strstr(limit_source, "LIMIT_DEFINITION");
  ag_language_analysis_limits_t tiny = defaults;
  tiny.max_source_bytes = strlen(limit_source);
  CHECK(limit_name && analyze_named(
            session, "macro-definition-limit.c", limit_source,
            (size_t)(limit_name - limit_source) + 2,
            (header_bundle_t){0}, tiny, &snapshot, &error),
        "macro definition exact source limit");
  size_t macro_snapshot_bytes = snapshot.allocated_bytes;
  ag_language_analysis_snapshot_dispose(&snapshot);
  tiny.max_source_bytes = strlen(limit_source) - 1;
  CHECK(!analyze_named(
            session, "macro-definition-limit.c", limit_source,
            (size_t)(limit_name - limit_source) + 2,
            (header_bundle_t){0}, tiny, &snapshot, &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code, "AGC_LIMIT_MAX_SOURCE_BYTES") == 0,
        "macro definition source limit rejection");
  tiny = defaults;
  tiny.max_snapshot_bytes = macro_snapshot_bytes - 1;
  CHECK(!analyze_named(
            session, "macro-definition-limit.c", limit_source,
            (size_t)(limit_name - limit_source) + 2,
            (header_bundle_t){0}, tiny, &snapshot, &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code,
                   "AGC_LIMIT_MAX_ANALYSIS_SNAPSHOT_BYTES") == 0,
        "macro definition snapshot limit rejection");
  CHECK(analyze_named(
            session, "macro-definition-limit.c", "#define SAFE_MACRO 1\n",
            strlen("#define SAFE"), (header_bundle_t){0}, defaults,
            &snapshot, &error) &&
            macro_definition_snapshot_matches(
                &snapshot, "SAFE_MACRO", "1", 0),
        "macro definition session reusable after limits");
  ag_language_analysis_snapshot_dispose(&snapshot);

  ag_compilation_session_destroy(session);
  return 0;
}

static int test_enum_documentation_analysis(ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "enum documentation session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};

  size_t snake_length = 0;
  char *snake_source = read_fixture_source(
      "test/fixtures/language_analysis/macro_definition_snake.txt",
      &snake_length);
  const char *game_paths[] = {"game.h"};
  const char *game_headers[] = {macro_definition_game_header};
  header_bundle_t game_bundle = make_bundle(game_paths, game_headers, 1);
  CHECK(snake_source && snake_length == strlen(snake_source) &&
            game_bundle.bytes,
        "enum documentation snake fixture");
  struct {
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *documentation;
    const char *comment;
  } snake_symbols[] = {
      {"Direction", AG_LANGUAGE_SYMBOL_TAG,
       "ヘビが進む方向を表します。",
       "/// ヘビが進む方向を表します。"},
      {"DIRECTION_LEFT", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "左へ進む方向です。", "/// 左へ進む方向です。"},
      {"DIRECTION_RIGHT", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, "", NULL},
      {"DIRECTION_UP", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "上へ進む方向です。", "/** 上へ進む方向です。 */"},
      {"DIRECTION_DOWN", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "下へ進む方向です。", "/// 下へ進む方向です。"},
  };
  for (size_t i = 0;
       i < sizeof(snake_symbols) / sizeof(snake_symbols[0]); i++) {
    const char *declaration = strstr(snake_source, snake_symbols[i].name);
    const char *use = last_occurrence(snake_source, snake_symbols[i].name);
    const char *comment = snake_symbols[i].comment
                              ? strstr(snake_source, snake_symbols[i].comment)
                              : NULL;
    CHECK(declaration && use && analyze_named(
              session, "snake.c", snake_source,
              (size_t)(use - snake_source) + strlen(snake_symbols[i].name) / 2,
              game_bundle, defaults, &snapshot, &error),
          "snake enum documentation use analysis");
    const ag_language_symbol_t *hover = hover_symbol(&snapshot);
    const ag_language_symbol_t *completion = find_symbol(
        &snapshot, snake_symbols[i].name, snake_symbols[i].kind);
    if (snake_symbols[i].documentation[0]) {
      CHECK(hover && completion &&
                check_documentation_symbol(
                    hover, snake_symbols[i].documentation, "snake.c",
                    (size_t)(comment - snake_source),
                    (size_t)(comment - snake_source) +
                        strlen(snake_symbols[i].comment)) &&
                check_documentation_symbol(
                    completion, snake_symbols[i].documentation, "snake.c",
                    (size_t)(comment - snake_source),
                    (size_t)(comment - snake_source) +
                        strlen(snake_symbols[i].comment)),
            "snake enum documentation fields");
    } else {
      CHECK(hover && completion && hover->documentation &&
                hover->documentation[0] == '\0' &&
                !hover->has_documentation_range &&
                completion->documentation &&
                completion->documentation[0] == '\0' &&
                !completion->has_documentation_range,
            "snake undocumented enum constant does not inherit");
    }
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  free(game_bundle.bytes);
  free(snake_source);

  const char *scope_source = enum_documentation_scope_source;
  const char *inner_anchor = strstr(scope_source, "inner enum");
  const char *inner_tag_use = inner_anchor
                                  ? strstr(inner_anchor,
                                           "enum ScopedDirection value")
                                  : NULL;
  const char *inner_value_use = inner_tag_use
                                    ? strstr(inner_tag_use,
                                             "SCOPED_DIRECTION_VALUE")
                                    : NULL;
  const char *outer_tag_use = last_occurrence(
      scope_source, "ScopedDirection");
  const char *outer_value_use = last_occurrence(
      scope_source, "SCOPED_DIRECTION_VALUE");
  struct {
    const char *cursor;
    const char *name;
    const char *documentation;
  } scope_cases[] = {
      {inner_tag_use ? strstr(inner_tag_use, "ScopedDirection") : NULL,
       "ScopedDirection", "inner enum"},
      {inner_value_use, "SCOPED_DIRECTION_VALUE", "inner value"},
      {outer_tag_use, "ScopedDirection", "outer enum"},
      {outer_value_use, "SCOPED_DIRECTION_VALUE", "outer value"},
  };
  for (size_t i = 0;
       i < sizeof(scope_cases) / sizeof(scope_cases[0]); i++) {
    CHECK(scope_cases[i].cursor && analyze_named(
              session, "enum-scope.c", scope_source,
              (size_t)(scope_cases[i].cursor - scope_source) + 2,
              (header_bundle_t){0}, defaults, &snapshot, &error),
          "nested enum documentation analysis");
    CHECK(hover_symbol(&snapshot) &&
              strcmp(hover_symbol(&snapshot)->name,
                     scope_cases[i].name) == 0 &&
              strcmp(hover_symbol(&snapshot)->documentation,
                     scope_cases[i].documentation) == 0,
          "nested enum documentation scope isolation");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }

  const char *header_paths[] = {"enum-doc.h"};
  const char *header_tag_use = last_occurrence(
      enum_documentation_header_main, "HeaderDirection");
  const char *header_value_use = last_occurrence(
      enum_documentation_header_main, "HEADER_DIRECTION_VALUE");
  for (size_t revision = 0; revision < 3; revision++) {
    const char *header_sources[] = {
        enum_documentation_header_revisions[revision]};
    header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
    const char *expected_tag = revision == 0 ? "header enum v1"
                               : revision == 1 ? "header enum v2" : "";
    const char *expected_value = revision == 0 ? "header constant v1"
                                 : revision == 1
                                       ? "header constant v2" : "";
    CHECK(bundle.bytes && header_tag_use && analyze_named(
              session, "enum-header-main.c",
              enum_documentation_header_main,
              (size_t)(header_tag_use - enum_documentation_header_main) + 2,
              bundle, defaults, &snapshot, &error),
          "virtual header enum tag documentation analysis");
    CHECK(hover_symbol(&snapshot) &&
              strcmp(hover_symbol(&snapshot)->documentation,
                     expected_tag) == 0 &&
              (expected_tag[0]
                   ? hover_symbol(&snapshot)->has_documentation_range &&
                         strcmp(hover_symbol(&snapshot)
                                    ->documentation_range.source_name,
                                "enum-doc.h") == 0
                   : !hover_symbol(&snapshot)->has_documentation_range),
          "virtual header enum tag revision fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
    CHECK(header_value_use && analyze_named(
              session, "enum-header-main.c",
              enum_documentation_header_main,
              (size_t)(header_value_use - enum_documentation_header_main) + 2,
              bundle, defaults, &snapshot, &error),
          "virtual header enum constant documentation analysis");
    CHECK(hover_symbol(&snapshot) &&
              strcmp(hover_symbol(&snapshot)->documentation,
                     expected_value) == 0 &&
              (expected_value[0]
                   ? hover_symbol(&snapshot)->has_documentation_range &&
                         strcmp(hover_symbol(&snapshot)
                                    ->documentation_range.source_name,
                                "enum-doc.h") == 0
                   : !hover_symbol(&snapshot)->has_documentation_range),
          "virtual header enum constant revision fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
    free(bundle.bytes);
  }

  ag_language_project_index_t *project =
      ag_language_project_index_create();
  CHECK(project != NULL, "enum documentation project index");
  for (size_t revision = 0; revision < 3; revision++) {
    const char *source = enum_documentation_project_revisions[revision];
    CHECK(update_single_source_project(
              session, project, (unsigned int)revision + 1, source,
              (header_bundle_t){0}, defaults, &error),
          "enum documentation project update");
    const char *tag_use = last_occurrence(source, "ProjectDirection");
    const char *value_use = last_occurrence(
        source, "PROJECT_DIRECTION_VALUE");
    const char *expected_tag = revision == 0 ? "project enum v1"
                               : revision == 1 ? "project enum v2" : "";
    const char *expected_value = revision == 0 ? "project constant v1"
                                 : revision == 1
                                       ? "project constant v2" : "";
    CHECK(tag_use && analyze_project_named(
              session, project, "main.c", source,
              (size_t)(tag_use - source) + 2, (header_bundle_t){0},
              defaults, &snapshot, &error),
          "enum documentation project tag analysis");
    CHECK(hover_symbol(&snapshot) &&
              strcmp(hover_symbol(&snapshot)->documentation,
                     expected_tag) == 0,
          "enum documentation project tag revision fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
    CHECK(value_use && analyze_project_named(
              session, project, "main.c", source,
              (size_t)(value_use - source) + 2, (header_bundle_t){0},
              defaults, &snapshot, &error),
          "enum documentation project constant analysis");
    CHECK(hover_symbol(&snapshot) &&
              strcmp(hover_symbol(&snapshot)->documentation,
                     expected_value) == 0,
          "enum documentation project constant revision fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  ag_language_project_index_destroy(project);

  const char *limited_source =
      "/** 12345678901234 */\nenum E { V };\n";
  ag_language_analysis_limits_t tiny = defaults;
  tiny.max_string_bytes = 13;
  CHECK(!analyze_named(
            session, "enum-limit.c", limited_source,
            (size_t)(strstr(limited_source, "E") - limited_source),
            (header_bundle_t){0}, tiny, &snapshot, &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code, "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES") == 0 &&
            error.max == 13 && error.actual == 14,
        "enum documentation string limit");
  const char *entry_limit_source =
      "/** first */\nenum A { A_VALUE };\n"
      "/** second */\nenum B { B_VALUE };\n";
  tiny = defaults;
  tiny.max_symbols = 1;
  CHECK(!analyze_named(
            session, "enum-entry-limit.c", entry_limit_source,
            strlen(entry_limit_source), (header_bundle_t){0}, tiny,
            &snapshot, &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code, "AGC_LIMIT_MAX_ANALYSIS_SYMBOLS") == 0,
        "enum documentation entry limit");
  const char *plain_snapshot_source = "enum S { S_VALUE };\n";
  const char *documented_snapshot_source =
      "/** bounded enum doc */\nenum S { S_VALUE };\n";
  CHECK(analyze_named(
            session, "enum-snapshot.c", plain_snapshot_source,
            strlen(plain_snapshot_source), (header_bundle_t){0}, defaults,
            &snapshot, &error),
        "plain enum documentation snapshot sizing");
  size_t plain_snapshot_bytes = snapshot.allocated_bytes;
  ag_language_analysis_snapshot_dispose(&snapshot);
  CHECK(analyze_named(
            session, "enum-snapshot.c", documented_snapshot_source,
            strlen(documented_snapshot_source), (header_bundle_t){0},
            defaults, &snapshot, &error),
        "documented enum snapshot sizing");
  size_t documented_snapshot_bytes = snapshot.allocated_bytes;
  ag_language_analysis_snapshot_dispose(&snapshot);
  CHECK(documented_snapshot_bytes > plain_snapshot_bytes,
        "enum documentation contributes to snapshot limit");
  tiny = defaults;
  tiny.max_snapshot_bytes = documented_snapshot_bytes - 1;
  CHECK(analyze_named(
            session, "enum-snapshot.c", plain_snapshot_source,
            strlen(plain_snapshot_source), (header_bundle_t){0}, tiny,
            &snapshot, &error),
        "plain enum snapshot within documentation boundary");
  ag_language_analysis_snapshot_dispose(&snapshot);
  CHECK(!analyze_named(
            session, "enum-snapshot.c", documented_snapshot_source,
            strlen(documented_snapshot_source), (header_bundle_t){0}, tiny,
            &snapshot, &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code,
                   "AGC_LIMIT_MAX_ANALYSIS_SNAPSHOT_BYTES") == 0,
        "documented enum snapshot limit");
  CHECK(analyze_named(
            session, "enum-limit.c", "enum Safe { SAFE_VALUE };\n",
            strlen("enum Safe { SAFE_VALUE };\n"),
            (header_bundle_t){0}, defaults, &snapshot, &error) &&
            find_symbol(&snapshot, "Safe", AG_LANGUAGE_SYMBOL_TAG),
        "enum documentation session reusable after limits");
  ag_language_analysis_snapshot_dispose(&snapshot);

  ag_compilation_session_destroy(session);
  return 0;
}

static int test_documentation_analysis(ag_target_info_t target) {
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "documentation session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};

  struct {
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *documentation;
    const char *comment;
    const char *macro_replacement;
    int macro_parameter_count;
    const char *first_macro_parameter;
  } documented[] = {
      {"enemy_x", AG_LANGUAGE_SYMBOL_OBJECT, "敵の現在位置",
       "/** 敵の現在位置 */", NULL, 0, NULL},
      {"walk_frame", AG_LANGUAGE_SYMBOL_FUNCTION,
       "歩行中の画像番号を返す\nalternateが0以外なら第一フレーム",
       "/// 歩行中の画像番号を返す\n"
       "/// alternateが0以外なら第一フレーム", NULL, 0, NULL},
      {"qualified_value", AG_LANGUAGE_SYMBOL_OBJECT,
       "読み取り専用の値\n\n日本語の段落を維持する",
       "/**\n"
       " * 読み取り専用の値\n"
       " *\n"
       " * 日本語の段落を維持する\n"
       " */", NULL, 0, NULL},
      {"left_value", AG_LANGUAGE_SYMBOL_OBJECT, "左右の座標",
       "/** 左右の座標 */", NULL, 0, NULL},
      {"right_value", AG_LANGUAGE_SYMBOL_OBJECT, "左右の座標",
       "/** 左右の座標 */", NULL, 0, NULL},
      {"external_value", AG_LANGUAGE_SYMBOL_OBJECT, "外部オブジェクト",
       "/** 外部オブジェクト */", NULL, 0, NULL},
      {"prototype_only", AG_LANGUAGE_SYMBOL_FUNCTION, "prototype only",
       "/** prototype only */", NULL, 0, NULL},
      {"definition_only", AG_LANGUAGE_SYMBOL_FUNCTION, "definition only",
       "/** definition only */", NULL, 0, NULL},
      {"documented_both", AG_LANGUAGE_SYMBOL_FUNCTION, "prototype wins",
       "/** prototype wins */", NULL, 0, NULL},
      {"fallback_definition", AG_LANGUAGE_SYMBOL_FUNCTION,
       "definition fallback", "/** definition fallback */", NULL, 0, NULL},
      {"DocumentedDirection", AG_LANGUAGE_SYMBOL_TAG,
       "ヘビが進む方向を表します。",
       "/// ヘビが進む方向を表します。", NULL, 0, NULL},
      {"DOCUMENTED_DIRECTION_LEFT", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "左へ進む方向です。", "/// 左へ進む方向です。", NULL, 0, NULL},
      {"DOCUMENTED_DIRECTION_UP", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "上へ進む方向です。\n\n明示値を使用します。",
       "/**\n"
       "   * 上へ進む方向です。\n"
       "   *\n"
       "   * 明示値を使用します。\n"
       "   */", NULL, 0, NULL},
      {"DOCUMENTED_DIRECTION_DOWN", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "下へ進む方向です。\nCRLFでも関連付けます。",
       "/// 下へ進む方向です。\r\n"
       "\t/// CRLFでも関連付けます。", NULL, 0, NULL},
      {"TagOnlyDirection", AG_LANGUAGE_SYMBOL_TAG,
       "tagだけの説明です。", "/// tagだけの説明です。", NULL, 0, NULL},
      {"CONSTANT_ONLY_DIRECTION", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "constantだけの説明です。", "/// constantだけの説明です。",
       NULL, 0, NULL},
      {"ANONYMOUS_DIRECTION", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       "anonymous constantの説明です。",
       "/** anonymous constantの説明です。 */", NULL, 0, NULL},
      {"first_only", AG_LANGUAGE_SYMBOL_OBJECT, "最初の宣言だけ",
       "/** 最初の宣言だけ */", NULL, 0, NULL},
      {"crlf_value", AG_LANGUAGE_SYMBOL_OBJECT, "CRLFの説明\n二行目",
       "/**\r\n\t * CRLFの説明\r\n\t * 二行目\r\n\t */", NULL, 0, NULL},
      {"local_value", AG_LANGUAGE_SYMBOL_OBJECT, "local object",
       "/** local object */", NULL, 0, NULL},
      {"PLAYER_SIZE", AG_LANGUAGE_SYMBOL_MACRO,
       "四角形の一辺の長さ（ピクセル）です。\n"
       "プレイヤーの描画に使用します。",
       "/// 四角形の一辺の長さ（ピクセル）です。\r\n"
       "\t/// プレイヤーの描画に使用します。",
       "12", 0, NULL},
      {"DOUBLE", AG_LANGUAGE_SYMBOL_MACRO,
       "値を二倍にします。\n\n引数は一度だけ評価してください。",
       "/**\n"
       " * 値を二倍にします。\n"
       " *\n"
       " * 引数は一度だけ評価してください。\n"
       " */",
       "( ( value ) * 2 )", 1, "value"},
      {"DOCUMENTED_LINE_OBJECT", AG_LANGUAGE_SYMBOL_MACRO,
       "継続object macro", "/** 継続object macro */",
       "( 1 + 2 )", 0, NULL},
      {"DOCUMENTED_LINE_FUNCTION", AG_LANGUAGE_SYMBOL_MACRO,
       "継続function macro", "/** 継続function macro */",
       "( ( value ) + 1 )", 1, "value"},
      {"REDEFINED_DOC", AG_LANGUAGE_SYMBOL_MACRO,
       "新しいmacro説明", "/** 新しいmacro説明 */", "2", 0, NULL},
  };
  for (size_t i = 0; i < sizeof(documented) / sizeof(documented[0]); i++) {
    const char *use = last_occurrence(
        documentation_hover_source, documented[i].name);
    const char *comment = strstr(
        documentation_hover_source, documented[i].comment);
    CHECK(use && comment, "documentation fixture anchors");
    CHECK(analyze_named(
              session, "documentation.c", documentation_hover_source,
              (size_t)(use - documentation_hover_source) +
                  strlen(documented[i].name) / 2,
              (header_bundle_t){0}, defaults, &snapshot, &error),
          "documented symbol analysis");
    const ag_language_symbol_t *hover = hover_symbol(&snapshot);
    const ag_language_symbol_t *symbol = find_symbol(
        &snapshot, documented[i].name, documented[i].kind);
    CHECK(hover && strcmp(hover->name, documented[i].name) == 0 &&
              check_documentation_symbol(
                  hover, documented[i].documentation, "documentation.c",
                  (size_t)(comment - documentation_hover_source),
                  (size_t)(comment - documentation_hover_source) +
                      strlen(documented[i].comment)) &&
              check_documentation_symbol(
                  symbol, documented[i].documentation, "documentation.c",
                  (size_t)(comment - documentation_hover_source),
                  (size_t)(comment - documentation_hover_source) +
                      strlen(documented[i].comment)) &&
              (documented[i].kind != AG_LANGUAGE_SYMBOL_MACRO ||
               (symbol->macro_replacement &&
                strcmp(symbol->macro_replacement,
                       documented[i].macro_replacement) == 0 &&
                symbol->macro_parameter_count ==
                    documented[i].macro_parameter_count &&
                (documented[i].macro_parameter_count == 0 ||
                 (symbol->macro_parameters &&
                  strcmp(symbol->macro_parameters[0],
                         documented[i].first_macro_parameter) == 0)))),
          "documented symbol fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }

  const char *stable_names[] = {
      "enemy_x", "walk_frame", "DocumentedDirection",
      "DOCUMENTED_DIRECTION_LEFT", "DOCUMENTED_DIRECTION_UP",
      "DOCUMENTED_DIRECTION_DOWN", "TagOnlyDirection",
      "CONSTANT_ONLY_DIRECTION", "ANONYMOUS_DIRECTION",
      "PLAYER_SIZE", "DOUBLE"};
  for (size_t name_index = 0;
       name_index < sizeof(stable_names) / sizeof(stable_names[0]);
       name_index++) {
    const char *declaration = strstr(
        documentation_hover_source, stable_names[name_index]);
    const char *use = last_occurrence(
        documentation_hover_source, stable_names[name_index]);
    size_t deltas[] = {
        0, strlen(stable_names[name_index]) / 2,
        strlen(stable_names[name_index]),
    };
    for (int fresh = 0; fresh < 2; fresh++) {
      for (int occurrence = 0; occurrence < 2; occurrence++) {
        for (size_t delta = 0;
             delta < sizeof(deltas) / sizeof(deltas[0]); delta++) {
          ag_compilation_session_t *analysis_session = session;
          if (fresh) {
            analysis_session = ag_compilation_session_create(&target);
            CHECK(analysis_session != NULL,
                  "fresh documentation hover session");
          }
          const char *position = occurrence == 0 ? declaration : use;
          CHECK(analyze_named(
                    analysis_session, "documentation.c",
                    documentation_hover_source,
                    (size_t)(position - documentation_hover_source) +
                        deltas[delta],
                    (header_bundle_t){0}, defaults, &snapshot, &error),
                "stable documentation hover analysis");
          const ag_language_symbol_t *hover = hover_symbol(&snapshot);
          CHECK(hover && strcmp(hover->name, stable_names[name_index]) == 0 &&
                    hover->documentation && hover->documentation[0],
                "stable documentation hover fields");
          ag_language_analysis_snapshot_dispose(&snapshot);
          if (fresh) ag_compilation_session_destroy(analysis_session);
        }
      }
    }
  }

  const char *undocumented[] = {
      "blank_gap",       "directive_gap", "directive_continuation_gap",
      "declaration_after",
      "ordinary_block", "ordinary_line", "comment_text",
      "string_after",   "comment_character", "character_after",
      "DOCUMENTED_DIRECTION_RIGHT",
      "ConstantOnlyDirection", "TAG_ONLY_LEFT", "TAG_ONLY_RIGHT",
      "ANONYMOUS_DIRECTION_UNDOCUMENTED", "BlankGapDirection",
      "OrdinaryGapDirection", "DirectiveGapDirection",
      "BLANK_DOC_MACRO", "ORDINARY_GAP_MACRO",
      "CONDITIONAL_GAP_MACRO", "PRAGMA_GAP_MACRO",
      "documentation_main",
  };
  for (size_t i = 0;
       i < sizeof(undocumented) / sizeof(undocumented[0]); i++) {
    const char *declaration = strstr(
        documentation_hover_source, undocumented[i]);
    CHECK(declaration != NULL, "undocumented fixture anchor");
    CHECK(analyze_named(
              session, "documentation.c", documentation_hover_source,
              (size_t)(declaration - documentation_hover_source) + 1,
              (header_bundle_t){0}, defaults, &snapshot, &error),
          "undocumented symbol analysis");
    const ag_language_symbol_t *hover = hover_symbol(&snapshot);
    CHECK(hover && strcmp(hover->name, undocumented[i]) == 0 &&
              hover->documentation && hover->documentation[0] == '\0' &&
              !hover->has_documentation_range,
          "undocumented symbol fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  CHECK(analyze_named(
            session, "documentation.c", documentation_hover_source,
            strlen(documentation_hover_source), (header_bundle_t){0},
            defaults, &snapshot, &error),
        "inactive macro documentation analysis");
  CHECK(find_symbol(
            &snapshot, "INACTIVE_DOCUMENTATION",
            AG_LANGUAGE_SYMBOL_MACRO) == NULL,
        "inactive macro documentation omitted");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *macro_header_paths[] = {"macro-doc.h", "empty.h"};
  const char *macro_headers[] = {
      "/** include boundary */\n"
      "#include \"empty.h\"\n"
      "#define INCLUDE_GAP_MACRO 4\n"
      "/// header macro v1\n"
      "#define HEADER_DOC(value) ((value) + 1)\n",
      "/** include boundary */\n"
      "#include \"empty.h\"\n"
      "#define INCLUDE_GAP_MACRO 4\n"
      "/** header macro v2 */\n"
      "#define HEADER_DOC(value) ((value) + 2)\n",
      "/** include boundary */\n"
      "#include \"empty.h\"\n"
      "#define INCLUDE_GAP_MACRO 4\n"
      "#define HEADER_DOC(value) ((value) + 3)\n",
  };
  const char *macro_header_main =
      "#include \"macro-doc.h\"\n"
      "int macro_header_main(void) { return HEADER_DOC(INCLUDE_GAP_MACRO); }\n";
  const char *macro_header_use = strstr(macro_header_main, "HEADER_DOC");
  for (size_t revision = 0; revision < 3; revision++) {
    const char *macro_header_sources[] = {macro_headers[revision], ""};
    header_bundle_t bundle = make_bundle(
        macro_header_paths, macro_header_sources, 2);
    CHECK(bundle.bytes && macro_header_use && analyze_named(
              session, "macro-header-main.c", macro_header_main,
              (size_t)(macro_header_use - macro_header_main) + 3,
              bundle, defaults, &snapshot, &error),
          "virtual header macro documentation analysis");
    const ag_language_symbol_t *header_macro = find_symbol(
        &snapshot, "HEADER_DOC", AG_LANGUAGE_SYMBOL_MACRO);
    const ag_language_symbol_t *include_gap = find_symbol(
        &snapshot, "INCLUDE_GAP_MACRO", AG_LANGUAGE_SYMBOL_MACRO);
    const char *expected = revision == 0 ? "header macro v1"
                           : revision == 1 ? "header macro v2" : "";
    const char *comment_text = revision == 0 ? "/// header macro v1"
                               : revision == 1
                                     ? "/** header macro v2 */" : NULL;
    const char *comment = comment_text
                              ? strstr(macro_headers[revision], comment_text)
                              : NULL;
    CHECK(header_macro &&
              check_documentation_symbol(
                  header_macro, expected, "macro-doc.h",
                  comment ? (size_t)(comment - macro_headers[revision]) : 0,
                  comment ? (size_t)(comment - macro_headers[revision]) +
                                strlen(comment_text)
                          : 0) &&
              header_macro->macro_parameter_count == 1 &&
              strcmp(header_macro->macro_parameters[0], "value") == 0 &&
              include_gap && include_gap->documentation &&
              include_gap->documentation[0] == '\0' &&
              !include_gap->has_documentation_range,
          "virtual header macro documentation fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
    free(bundle.bytes);
  }

  const char *macro_project_sources[] = {
      "/** project macro v1 */\n"
      "#define PROJECT_DOC 10\n"
      "int macro_project_main(void) { return PROJECT_DOC; }\n",
      "/** project macro v2 */\n"
      "#define PROJECT_DOC 20\n"
      "int macro_project_main(void) { return PROJECT_DOC; }\n",
      ("#define PROJECT_DOC 30\n"
       "int macro_project_main(void) { return PROJECT_DOC; }\n"),
  };
  ag_language_project_index_t *macro_project =
      ag_language_project_index_create();
  CHECK(macro_project != NULL, "macro documentation project index");
  for (unsigned int revision = 1; revision <= 3; revision++) {
    const char *source = macro_project_sources[revision - 1];
    ag_language_project_source_t project_source = {
        "macro-project.c", source, strlen(source)};
    CHECK(ag_language_project_index_update(
              session, macro_project,
              &(ag_language_project_update_request_t){
                  .revision = revision,
                  .sources = &project_source,
                  .source_count = 1,
                  .limits = defaults,
              },
              &error),
          "macro documentation project update");
    const char *definition = strstr(source, "PROJECT_DOC");
    const char *use = definition
                          ? strstr(definition + strlen("PROJECT_DOC"),
                                   "PROJECT_DOC")
                          : NULL;
    CHECK(use && analyze_project_named(
              session, macro_project, "macro-project.c", source,
              (size_t)(use - source) + 3, (header_bundle_t){0},
              defaults, &snapshot, &error),
          "macro documentation project analysis");
    const ag_language_symbol_t *project_macro = find_symbol(
        &snapshot, "PROJECT_DOC", AG_LANGUAGE_SYMBOL_MACRO);
    const char *expected = revision == 1 ? "project macro v1"
                           : revision == 2 ? "project macro v2" : "";
    const char *comment_text = revision == 1 ? "/** project macro v1 */"
                               : revision == 2
                                     ? "/** project macro v2 */" : NULL;
    const char *comment = comment_text ? strstr(source, comment_text) : NULL;
    CHECK(project_macro &&
              check_documentation_symbol(
                  project_macro, expected, "macro-project.c",
                  comment ? (size_t)(comment - source) : 0,
                  comment ? (size_t)(comment - source) +
                                strlen(comment_text)
                          : 0),
          "macro documentation project revision fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  ag_language_project_index_destroy(macro_project);

  const char *header_paths[] = {"player.h"};
  const char *header_with_documentation =
      "/** header prototype */\nvoid update_player(void);\n";
  const char *header_without_documentation =
      "void update_player(void);\n";
  const char *definition_v1 =
      "#include \"player.h\"\n"
      "/** definition v1 */\nvoid update_player(void) {}\n";
  const char *definition_v2 =
      "#include \"player.h\"\n"
      "/** definition v2 */\nvoid update_player(void) {}\n";
  const char *call_source =
      "#include \"player.h\"\n"
      "void update(void) { update_player(); }\n";
  ag_language_project_index_t *project =
      ag_language_project_index_create();
  CHECK(project != NULL, "documentation project index");
  for (unsigned int revision = 1; revision <= 4; revision++) {
    const char *header = revision == 1 ? header_with_documentation
                                       : header_without_documentation;
    const char *header_sources[] = {header};
    header_bundle_t bundle = make_bundle(header_paths, header_sources, 1);
    const char *definition = revision <= 2 ? definition_v1 : definition_v2;
    ag_language_project_source_t sources[] = {
        {"player.c", definition, strlen(definition)},
        {"main.c", call_source, strlen(call_source)},
    };
    ag_language_project_update_request_t update = {
        .revision = revision,
        .sources = sources,
        .source_count = revision == 4 ? 1 : 2,
        .virtual_header_bundle = bundle.bytes,
        .virtual_header_bundle_length = bundle.length,
        .max_header_files = 32,
        .max_header_file_bytes = 1024 * 1024,
        .max_header_total_bytes = 4 * 1024 * 1024,
        .max_include_depth = 16,
        .limits = defaults,
    };
    if (revision == 4) sources[0] = sources[1];
    CHECK(ag_language_project_index_update(
              session, project, &update, &error),
          "documentation project update");
    const char *call = last_occurrence(call_source, "update_player");
    CHECK(analyze_project_named(
              session, project, "main.c", call_source,
              (size_t)(call - call_source) + 3, bundle, defaults,
              &snapshot, &error),
          "documentation project call analysis");
    const ag_language_symbol_t *hover = hover_symbol(&snapshot);
    const char *expected = revision == 1   ? "header prototype"
                           : revision == 2 ? "definition v1"
                           : revision == 3 ? "definition v2"
                                           : "";
    const char *expected_source = revision == 1 ? "player.h" : "player.c";
    CHECK(hover && strcmp(hover->name, "update_player") == 0 &&
              strcmp(hover->documentation, expected) == 0 &&
              (expected[0] ? hover->has_documentation_range &&
                                 strcmp(hover->documentation_range.source_name,
                                        expected_source) == 0
                           : !hover->has_documentation_range),
          "documentation project revision fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
    free(bundle.bytes);
  }
  const char *visible_header_sources[] = {
      "/** project prototype */\nvoid update_player(void);\n",
  };
  header_bundle_t visible_bundle = make_bundle(
      header_paths, visible_header_sources, 1);
  const char *visible_call_source =
      "/** visible prototype */\nvoid update_player(void);\n"
      "void update(void) { update_player(); }\n";
  ag_language_project_source_t visible_sources[] = {
      {"player.c", definition_v1, strlen(definition_v1)},
      {"visible.c", visible_call_source, strlen(visible_call_source)},
  };
  CHECK(ag_language_project_index_update(
            session, project,
            &(ag_language_project_update_request_t){
                .revision = 5,
                .sources = visible_sources,
                .source_count = 2,
                .virtual_header_bundle = visible_bundle.bytes,
                .virtual_header_bundle_length = visible_bundle.length,
                .max_header_files = 32,
                .max_header_file_bytes = 1024 * 1024,
                .max_header_total_bytes = 4 * 1024 * 1024,
                .max_include_depth = 16,
                .limits = defaults,
            },
            &error),
        "visible prototype documentation project update");
  const char *visible_call = last_occurrence(
      visible_call_source, "update_player");
  CHECK(analyze_project_named(
            session, project, "visible.c", visible_call_source,
            (size_t)(visible_call - visible_call_source) + 3,
            visible_bundle, defaults, &snapshot, &error),
        "visible prototype documentation analysis");
  CHECK(hover_symbol(&snapshot) &&
            strcmp(hover_symbol(&snapshot)->documentation,
                   "visible prototype") == 0 &&
            strcmp(hover_symbol(&snapshot)->documentation_range.source_name,
                   "visible.c") == 0,
        "visible prototype documentation wins");
  ag_language_analysis_snapshot_dispose(&snapshot);
  free(visible_bundle.bytes);
  ag_language_project_index_destroy(project);

  const char *limited_source = "/** 12345678901234 */\nint x;\n";
  ag_language_analysis_limits_t tiny = defaults;
  tiny.max_string_bytes = 13;
  CHECK(!analyze_named(
            session, "d.c", limited_source,
            strlen(limited_source), (header_bundle_t){0}, tiny,
            &snapshot, &error),
        "documentation string limit rejected");
  CHECK(error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code, "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES") == 0 &&
            strcmp(error.limit, "maxAnalysisStringBytes") == 0 &&
            error.max == 13 && error.actual == 14,
        "documentation string limit fields");
  CHECK(analyze_named(
            session, "d.c", "int x;", 6,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "documentation session reusable after string limit");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *limited_macro_source =
      "/** 12345678901234 */\n"
      "#define LIMITED_MACRO 1\n";
  const char *limited_macro_use = strstr(
      limited_macro_source, "LIMITED_MACRO");
  tiny = defaults;
  tiny.max_string_bytes = 13;
  CHECK(limited_macro_use && !analyze_named(
            session, "macro-limit.c", limited_macro_source,
            (size_t)(limited_macro_use - limited_macro_source) + 2,
            (header_bundle_t){0}, tiny, &snapshot, &error),
        "macro documentation string limit rejected");
  CHECK(error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code, "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES") == 0 &&
            strcmp(error.limit, "maxAnalysisStringBytes") == 0 &&
            error.max == 13 && error.actual == 14,
        "macro documentation string limit fields");
  const char *macro_entry_limit_source =
      "/** first */\n#define FIRST_DOC 1\n"
      "/** second */\n#define SECOND_DOC 2\n"
      "int macro_entry_limit_main(void) { return FIRST_DOC + SECOND_DOC; }\n";
  tiny = defaults;
  tiny.max_symbols = 1;
  CHECK(!analyze_named(
            session, "macro-entry-limit.c", macro_entry_limit_source,
            strlen(macro_entry_limit_source), (header_bundle_t){0}, tiny,
            &snapshot, &error),
        "macro documentation entry limit rejected");
  CHECK(error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code, "AGC_LIMIT_MAX_ANALYSIS_SYMBOLS") == 0 &&
            strcmp(error.limit, "maxAnalysisSymbols") == 0 &&
            error.max == 1 && error.actual == 2,
        "macro documentation entry limit fields");
  CHECK(analyze_named(
            session, "macro-limit.c", "#define OK 1\n", 13,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "macro documentation session reusable after limits");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *snapshot_plain = "int bounded;\n";
  const char *snapshot_documented = "/** bounded doc */\nint bounded;\n";
  CHECK(analyze_named(
            session, "documentation-snapshot.c", snapshot_plain,
            strlen(snapshot_plain), (header_bundle_t){0}, defaults,
            &snapshot, &error),
        "plain documentation snapshot sizing");
  size_t plain_snapshot_bytes = snapshot.allocated_bytes;
  ag_language_analysis_snapshot_dispose(&snapshot);
  CHECK(analyze_named(
            session, "documentation-snapshot.c", snapshot_documented,
            strlen(snapshot_documented), (header_bundle_t){0}, defaults,
            &snapshot, &error),
        "documented snapshot sizing");
  size_t documented_snapshot_bytes = snapshot.allocated_bytes;
  ag_language_analysis_snapshot_dispose(&snapshot);
  CHECK(documented_snapshot_bytes > plain_snapshot_bytes,
        "documentation contributes to snapshot limit");
  tiny = defaults;
  tiny.max_snapshot_bytes = documented_snapshot_bytes - 1;
  CHECK(analyze_named(
            session, "documentation-snapshot.c", snapshot_plain,
            strlen(snapshot_plain), (header_bundle_t){0}, tiny,
            &snapshot, &error),
        "plain snapshot within documentation boundary");
  ag_language_analysis_snapshot_dispose(&snapshot);
  CHECK(!analyze_named(
            session, "documentation-snapshot.c", snapshot_documented,
            strlen(snapshot_documented), (header_bundle_t){0}, tiny,
            &snapshot, &error),
        "documented snapshot limit rejected");
  CHECK(error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code,
                   "AGC_LIMIT_MAX_ANALYSIS_SNAPSHOT_BYTES") == 0 &&
            strcmp(error.limit, "maxAnalysisSnapshotBytes") == 0,
        "documentation snapshot limit fields");

  const char *snapshot_plain_macro =
      "#define BOUNDED_MACRO 1\n"
      "int bounded_macro_main(void) { return BOUNDED_MACRO; }\n";
  const char *snapshot_documented_macro =
      "/** bounded macro doc */\n"
      "#define BOUNDED_MACRO 1\n"
      "int bounded_macro_main(void) { return BOUNDED_MACRO; }\n";
  CHECK(analyze_named(
            session, "macro-snapshot.c", snapshot_plain_macro,
            strlen(snapshot_plain_macro), (header_bundle_t){0}, defaults,
            &snapshot, &error),
        "plain macro documentation snapshot sizing");
  size_t plain_macro_snapshot_bytes = snapshot.allocated_bytes;
  ag_language_analysis_snapshot_dispose(&snapshot);
  CHECK(analyze_named(
            session, "macro-snapshot.c", snapshot_documented_macro,
            strlen(snapshot_documented_macro), (header_bundle_t){0},
            defaults, &snapshot, &error),
        "documented macro snapshot sizing");
  size_t documented_macro_snapshot_bytes = snapshot.allocated_bytes;
  ag_language_analysis_snapshot_dispose(&snapshot);
  CHECK(documented_macro_snapshot_bytes > plain_macro_snapshot_bytes,
        "macro documentation contributes to snapshot limit");
  tiny = defaults;
  tiny.max_snapshot_bytes = documented_macro_snapshot_bytes - 1;
  CHECK(analyze_named(
            session, "macro-snapshot.c", snapshot_plain_macro,
            strlen(snapshot_plain_macro), (header_bundle_t){0}, tiny,
            &snapshot, &error),
        "plain macro snapshot within documentation boundary");
  ag_language_analysis_snapshot_dispose(&snapshot);
  CHECK(!analyze_named(
            session, "macro-snapshot.c", snapshot_documented_macro,
            strlen(snapshot_documented_macro), (header_bundle_t){0}, tiny,
            &snapshot, &error),
        "documented macro snapshot limit rejected");
  CHECK(error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.code,
                   "AGC_LIMIT_MAX_ANALYSIS_SNAPSHOT_BYTES") == 0 &&
            strcmp(error.limit, "maxAnalysisSnapshotBytes") == 0,
        "macro documentation snapshot limit fields");

  const char *immutable_source = "/** immutable */\nint immutable_value;\n";
  CHECK(analyze_named(
            session, "immutable.c", immutable_source,
            strlen(immutable_source), (header_bundle_t){0}, defaults,
            &snapshot, &error),
        "documentation immutable snapshot");
  const ag_language_symbol_t *immutable = find_symbol(
      &snapshot, "immutable_value", AG_LANGUAGE_SYMBOL_OBJECT);
  CHECK(immutable && strcmp(immutable->documentation, "immutable") == 0,
        "documentation immutable initial value");
  ag_language_analysis_snapshot_t second = {0};
  CHECK(analyze_named(
            session, "immutable.c", "int replacement;", 16,
            (header_bundle_t){0}, defaults, &second, &error),
        "documentation immutable second analysis");
  CHECK(strcmp(immutable->documentation, "immutable") == 0 &&
            strcmp(immutable->documentation_range.source_name,
                   "immutable.c") == 0,
        "documentation survives reused session");
  ag_language_analysis_snapshot_dispose(&second);
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(session);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--parity-json") == 0)
    return print_parity_snapshot();
  if (argc == 2 && strcmp(argv[1], "--enum-parity-json") == 0)
    return print_enum_parity_snapshot();
  if (argc == 2 && strcmp(argv[1], "--include-only-parity-json") == 0)
    return print_include_only_parity_snapshot();
  if (argc == 2 && strcmp(argv[1], "--object-parity-json") == 0)
    return print_object_parity_snapshot();
  if (argc == 2 &&
      strcmp(argv[1], "--function-definition-parity-json") == 0)
    return print_function_definition_parity_snapshot();
  if (argc == 2 &&
      strcmp(argv[1], "--project-function-parity-json") == 0)
    return print_project_function_parity_snapshot();
  if (argc == 2 &&
      strcmp(argv[1], "--project-header-guard-parity-json") == 0)
    return print_project_header_guard_parity_snapshot(0);
  if (argc == 2 &&
      strcmp(argv[1], "--project-header-guard-error-parity-json") == 0)
    return print_project_header_guard_parity_snapshot(1);
  if (argc == 3 &&
      strcmp(argv[1], "--for-control-hover-parity-json") == 0)
    return print_for_control_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--conditional-hover-parity-json") == 0)
    return print_conditional_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--generic-hover-parity-json") == 0)
    return print_generic_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--function-declarator-hover-parity-json") == 0)
    return print_function_declarator_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--documentation-hover-parity-json") == 0)
    return print_documentation_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--documentation-project-parity-json") == 0)
    return print_documentation_project_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--macro-documentation-header-parity-json") == 0)
    return print_macro_documentation_header_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--macro-documentation-project-parity-json") == 0)
    return print_macro_documentation_project_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--macro-definition-parity-json") == 0)
    return print_macro_definition_forms_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--conditional-logical-line-parity-json") == 0)
    return print_conditional_logical_line_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--inline-tag-object-parity-json") == 0)
    return print_inline_tag_object_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--for-init-declaration-parity-json") == 0)
    return print_for_init_declaration_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--prototype-parameter-bound-parity-json") == 0)
    return print_prototype_parameter_bound_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--block-static-assert-parity-json") == 0)
    return print_block_static_assert_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--do-body-hover-parity-json") == 0)
    return print_do_body_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--offsetof-type-hover-parity-json") == 0)
    return print_offsetof_type_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--initializer-operand-hover-parity-json") == 0)
    return print_initializer_operand_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--direct-aggregate-operand-hover-parity-json") == 0)
    return print_direct_aggregate_operand_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1],
             "--simple-remaining-call-argument-hover-parity-json") == 0)
    return print_simple_remaining_call_argument_hover_parity_snapshot(
        argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--cast-operand-hover-parity-json") == 0)
    return print_cast_operand_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1],
             "--sizeof-expression-operand-hover-parity-json") == 0)
    return print_sizeof_expression_operand_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1],
             "--statement-keyword-operand-hover-parity-json") == 0)
    return print_statement_keyword_operand_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1],
             "--statement-call-operand-hover-parity-json") == 0)
    return print_statement_call_operand_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1],
             "--case-expression-operand-hover-parity-json") == 0)
    return print_case_expression_operand_hover_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1],
             "--enum-initializer-operand-hover-parity-json") == 0)
    return print_enum_initializer_operand_hover_parity_snapshot(argv[2]);
  if (argc == 4 &&
      strcmp(argv[1],
             "--incomplete-enum-initializer-hover-parity-json") == 0)
    return print_incomplete_enum_initializer_hover_parity_snapshot(
        argv[2], argv[3]);
  if (argc == 4 &&
      strcmp(argv[1],
             "--incomplete-enum-header-hover-parity-json") == 0)
    return print_incomplete_enum_header_hover_parity_snapshot(
        argv[2], argv[3]);
  if (argc == 5 &&
      strcmp(argv[1],
             "--incomplete-enum-header-revision-parity-json") == 0)
    return print_incomplete_enum_header_revision_parity_snapshot(
        argv[2], argv[3], argv[4]);
  if (argc == 5 &&
      strcmp(argv[1],
             "--project-enum-macro-revision-parity-json") == 0)
    return print_project_enum_macro_revision_parity_snapshot(
        argv[2], argv[3], argv[4]);
  if (argc == 7 &&
      strcmp(argv[1],
             "--enum-two-argument-call-parity-json") == 0)
    return print_enum_two_argument_call_parity_snapshot(
        argv[2], argv[3], argv[4], argv[5], argv[6]);
  if ((argc == 5 || argc == 6) &&
      strcmp(argv[1],
             "--enum-three-argument-call-parity-json") == 0)
    return print_enum_three_argument_call_parity_snapshot(
        argv[2], argv[3], argv[4], argc == 6 ? argv[5] : NULL);
  if (argc == 3 &&
      strcmp(argv[1],
             "--initializer-designator-operand-hover-parity-json") == 0)
    return print_initializer_designator_operand_hover_parity_snapshot(
        argv[2]);
  if (argc == 3 &&
      strcmp(argv[1],
             "--compound-literal-designator-operand-hover-parity-json") == 0)
    return print_compound_literal_designator_operand_hover_parity_snapshot(
        argv[2]);
  if (argc == 3 &&
      strcmp(argv[1],
             "--type-name-array-bound-operand-hover-parity-json") == 0)
    return print_type_name_array_bound_operand_hover_parity_snapshot(
        argv[2]);
  if (argc == 3 &&
      strcmp(argv[1],
             "--declarator-array-bound-operand-hover-parity-json") == 0)
    return print_declarator_array_bound_operand_hover_parity_snapshot(
        argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--cast-operand-project-parity-json") == 0)
    return print_cast_operand_project_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--macro-definition-snake-parity-json") == 0)
    return print_macro_definition_snake_parity_snapshot(argv[2]);
  if (argc == 2 &&
      strcmp(argv[1], "--macro-definition-header-parity-json") == 0)
    return print_macro_definition_header_parity_snapshot();
  if (argc == 3 &&
      strcmp(argv[1], "--macro-definition-project-parity-json") == 0)
    return print_macro_definition_project_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--enum-documentation-header-parity-json") == 0)
    return print_enum_documentation_header_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--enum-documentation-scope-parity-json") == 0)
    return print_enum_documentation_scope_parity_snapshot(argv[2]);
  if (argc == 3 &&
      strcmp(argv[1], "--enum-documentation-project-parity-json") == 0)
    return print_enum_documentation_project_parity_snapshot(argv[2]);
  if (argc == 2 &&
      strcmp(argv[1], "--project-failure-recovery-parity-json") == 0)
    return test_project_failure_recovery(1);
  if (test_project_failure_recovery(0) != 0) return 1;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};

  CHECK(test_cast_operand_hover(target) == 0,
        "cast operand hover scenarios");
  CHECK(test_sizeof_expression_operand_hover(target) == 0,
        "sizeof expression operand hover scenarios");
  CHECK(test_statement_keyword_operand_hover(target) == 0,
        "statement keyword operand hover scenarios");
  CHECK(test_statement_call_operand_hover(target) == 0,
        "statement call operand hover scenarios");
  CHECK(test_case_expression_operand_hover(target) == 0,
        "case expression operand hover scenarios");
  CHECK(test_enum_initializer_operand_hover(target) == 0,
        "enum initializer operand hover scenarios");
  CHECK(test_incomplete_enum_initializer_hover(target) == 0,
        "incomplete enum initializer hover scenarios");
  CHECK(test_incomplete_enum_header_hover(target) == 0,
        "incomplete enum header hover scenarios");
  CHECK(test_incomplete_enum_header_revisions(target) == 0,
        "incomplete enum header revision scenarios");
  CHECK(test_incomplete_enum_header_rename_transitions(target) == 0,
        "incomplete enum header rename transition scenarios");
  CHECK(test_incomplete_enum_header_kind_transitions(target) == 0,
        "incomplete enum header kind transition scenarios");
  CHECK(test_incomplete_enum_header_macro_shape_transitions(target) == 0,
        "incomplete enum header macro shape transition scenarios");
  CHECK(test_incomplete_enum_header_namespace_collision(target) == 0,
        "incomplete enum header namespace collision scenarios");
  CHECK(test_incomplete_enum_header_namespace_revisions(target) == 0,
        "incomplete enum header namespace revision scenarios");
  CHECK(test_project_enum_macro_revision_order(target) == 0,
        "project enum macro revision order scenarios");
  CHECK(test_enum_two_argument_call_cursor(target) == 0,
        "enum two argument call cursor scenarios");
  CHECK(test_enum_three_argument_call_cursor(target) == 0,
        "enum three argument call cursor scenarios");
  CHECK(test_initializer_designator_operand_hover(target) == 0,
        "initializer designator operand hover scenarios");
  CHECK(test_compound_literal_designator_operand_hover(target) == 0,
        "compound literal designator operand hover scenarios");
  CHECK(test_type_name_array_bound_operand_hover(target) == 0,
        "type-name array bound operand hover scenarios");
  CHECK(test_declarator_array_bound_operand_hover(target) == 0,
        "declarator array bound operand hover scenarios");
  CHECK(test_inline_tag_object_hover(target) == 0,
        "inline tag object hover scenarios");
  CHECK(test_for_init_declaration_hover(target) == 0,
        "for init declaration hover scenarios");
  CHECK(test_prototype_parameter_bound_hover(target) == 0,
        "prototype parameter bound hover scenarios");
  CHECK(test_block_static_assert_hover(target) == 0,
        "block static assert hover scenarios");
  CHECK(test_do_body_hover(target) == 0,
        "do body hover scenarios");
  CHECK(test_offsetof_type_hover(target) == 0,
        "offsetof type hover scenarios");
  CHECK(test_initializer_operand_hover(target) == 0,
        "initializer operand hover scenarios");
  CHECK(test_direct_aggregate_operand_hover(target) == 0,
        "direct aggregate operand hover scenarios");
  CHECK(test_simple_remaining_call_argument_hover(target) == 0,
        "simple remaining call argument hover scenarios");
  CHECK(test_macro_definition_hover(target) == 0,
        "macro definition hover scenarios");
  CHECK(test_enum_documentation_analysis(target) == 0,
        "enum documentation analysis scenarios");
  CHECK(test_documentation_analysis(target) == 0,
        "documentation analysis scenarios");

  const char *game_paths[] = {"game.h"};
  const char *game_sources[] = {
      "#define GAME_SCREEN_WIDTH 320\nvoid screen_clear(int color);\n"};
  header_bundle_t game = make_bundle(game_paths, game_sources, 1);
  const char *source =
      "#include <game.h>\nint main(void) { int local = 1; screen_";
  CHECK(analyze(session, source, strlen(source), game, defaults,
                &snapshot, &error), "virtual header analysis");
  CHECK(find_symbol(&snapshot, "GAME_SCREEN_WIDTH", AG_LANGUAGE_SYMBOL_MACRO),
        "header macro");
  CHECK(find_symbol(&snapshot, "screen_clear", AG_LANGUAGE_SYMBOL_FUNCTION),
        "header function");
  CHECK(find_symbol(&snapshot, "local", AG_LANGUAGE_SYMBOL_OBJECT),
        "local object");
  CHECK(snapshot.partial, "incomplete source is partial");
  ag_language_analysis_snapshot_dispose(&snapshot);
  free(game.bytes);

  const char *hover_paths[] = {"symbols.h"};
  const char *hover_sources[] = {
      "#define HEADER_LIMIT 7\n"
      "typedef unsigned long HeaderSize;\n"
      "extern int header_object;\n"
      "int header_function(int value);\n"};
  header_bundle_t hover_bundle = make_bundle(
      hover_paths, hover_sources, 1);
  source = "#include <symbols.h>\n"
           "int main(void) { return header_function(header_object) + "
           "HEADER_LIMIT + (int)sizeof(HeaderSize); }\n";
  const char *function_use = strstr(source, "header_function");
  size_t function_offsets[] = {
      (size_t)(function_use - source),
      (size_t)(function_use - source) + 7,
      (size_t)(function_use - source) + strlen("header_function"),
  };
  for (size_t i = 0; i < sizeof(function_offsets) / sizeof(function_offsets[0]);
       i++) {
    CHECK(analyze(session, source, function_offsets[i], hover_bundle, defaults,
                  &snapshot, &error), "virtual header function hover");
    const ag_language_symbol_t *hover = hover_symbol(&snapshot);
    CHECK(hover && hover->kind == AG_LANGUAGE_SYMBOL_FUNCTION &&
              strcmp(hover->name, "header_function") == 0 &&
              strcmp(hover->signature, "int (int)") == 0 &&
              strcmp(hover->declaration.source_name, "symbols.h") == 0,
          "virtual header function hover fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  struct {
    const char *name;
    ag_language_symbol_kind_t kind;
  } header_hover_cases[] = {
      {"header_object", AG_LANGUAGE_SYMBOL_OBJECT},
      {"HeaderSize", AG_LANGUAGE_SYMBOL_TYPEDEF},
      {"HEADER_LIMIT", AG_LANGUAGE_SYMBOL_MACRO},
  };
  for (size_t i = 0;
       i < sizeof(header_hover_cases) / sizeof(header_hover_cases[0]); i++) {
    const char *use = strstr(source, header_hover_cases[i].name);
    size_t cursor = (size_t)(use - source) + strlen(header_hover_cases[i].name);
    CHECK(analyze(session, source, cursor, hover_bundle, defaults,
                  &snapshot, &error), "virtual header symbol hover");
    const ag_language_symbol_t *hover = hover_symbol(&snapshot);
    CHECK(hover && hover->kind == header_hover_cases[i].kind &&
              strcmp(hover->name, header_hover_cases[i].name) == 0 &&
              strcmp(hover->declaration.source_name, "symbols.h") == 0,
          "virtual header symbol hover fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  free(hover_bundle.bytes);

  const char *starter_paths[] = {"game.h"};
  const char *starter_headers[] = {
      "#define GAME_SCREEN_WIDTH 640\n"
      "#define GAME_SCREEN_HEIGHT 360\n"};
  header_bundle_t starter_bundle = make_bundle(
      starter_paths, starter_headers, 1);
  const char *starter_source =
      "#include <game.h>\n"
      "enum { PLAYER_SIZE = 12 };\n"
      "static int player_x;\n"
      "static int player_y;\n"
      "static void update(void) {\n"
      "  if (player_x > GAME_SCREEN_WIDTH - PLAYER_SIZE) {\n"
      "    player_x = GAME_SCREEN_WIDTH - PLAYER_SIZE;\n"
      "  }\n"
      "  if (player_y > GAME_SCREEN_HEIGHT - PLAYER_SIZE) {\n"
      "    player_y = GAME_SCREEN_HEIGHT - PLAYER_SIZE;\n"
      "  }\n"
      "}\n";
  struct {
    const char *name;
    const char *replacement;
  } starter_macros[] = {
      {"GAME_SCREEN_WIDTH", "640"},
      {"GAME_SCREEN_HEIGHT", "360"},
  };
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t macro_index = 0;
         macro_index < sizeof(starter_macros) / sizeof(starter_macros[0]);
         macro_index++) {
      const char *use = strstr(starter_source, starter_macros[macro_index].name);
      size_t name_len = strlen(starter_macros[macro_index].name);
      size_t cursor_deltas[] = {0, name_len / 2, name_len};
      for (size_t cursor_index = 0;
           cursor_index <
               sizeof(cursor_deltas) / sizeof(cursor_deltas[0]);
           cursor_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh starter macro hover session");
        }
        size_t cursor =
            (size_t)(use - starter_source) + cursor_deltas[cursor_index];
        CHECK(analyze(
                  analysis_session, starter_source, cursor,
                  starter_bundle, defaults, &snapshot, &error),
              "starter condition macro hover");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        const ag_language_symbol_t *completion = find_symbol(
            &snapshot, starter_macros[macro_index].name,
            AG_LANGUAGE_SYMBOL_MACRO);
        CHECK(hover && completion &&
                  hover->kind == AG_LANGUAGE_SYMBOL_MACRO &&
                  strcmp(hover->name, starter_macros[macro_index].name) == 0 &&
                  strcmp(hover->macro_replacement,
                         starter_macros[macro_index].replacement) == 0 &&
                  strcmp(completion->macro_replacement,
                         starter_macros[macro_index].replacement) == 0 &&
                  strcmp(hover->declaration.source_name, "game.h") == 0,
              "starter condition macro hover fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }
  free(starter_bundle.bytes);

  const char *first_for = strstr(
      for_control_hover_source, "for (\n#define LOOP_SEED");
  const char *init_outer = strstr(first_for, "outer");
  const char *nested_init_outer = strstr(
      init_outer + strlen("outer"), "outer");
  const char *condition_outer = strstr(
      nested_init_outer + strlen("outer"), "outer");
  const char *for_enum_use = strstr(condition_outer, "ENEMY_COUNT");
  const char *for_macro_use = strstr(for_enum_use, "LOOP_LIMIT");
  const char *update_outer = strstr(for_macro_use, "outer");
  const char *comma_update_outer = strstr(
      update_outer + strlen("outer"), "outer");
  const char *empty_for_body_outer = strstr(
      comma_update_outer + strlen("outer"), "outer");
  const char *empty_update_for = strstr(
      empty_for_body_outer + strlen("outer"), "for (outer");
  const char *empty_update_init_outer = strstr(empty_update_for, "outer");
  const char *empty_update_condition_outer = strstr(
      empty_update_init_outer + strlen("outer"), "outer");
  const char *inner_for = strstr(empty_update_condition_outer, "for (int inner");
  const char *inner_declaration = strstr(inner_for, "inner");
  const char *inner_condition = strstr(
      inner_declaration + strlen("inner"), "inner");
  const char *inner_enum_use = strstr(inner_condition, "ENEMY_COUNT");
  const char *inner_update = strstr(inner_enum_use, "inner");
  const char *nested_for = strstr(inner_update, "for (int nested");
  const char *nested_condition_inner = strstr(nested_for, "inner");
  const char *nested_body_outer = strstr(nested_condition_inner, "outer");
  const char *nested_body_inner = strstr(nested_body_outer, "inner");
  const char *after_loop_outer = strstr(
      nested_body_inner + strlen("inner"), "outer");
  const char *after_loop_macro = strstr(after_loop_outer, "LOOP_LIMIT");
  const char *outer_declaration = strstr(
      for_control_hover_source, "outer");
  CHECK(first_for && init_outer && nested_init_outer && condition_outer &&
            for_enum_use && for_macro_use && update_outer &&
            comma_update_outer && empty_for_body_outer && empty_update_for &&
            empty_update_init_outer && empty_update_condition_outer &&
            inner_for && inner_declaration && inner_condition &&
            inner_enum_use && inner_update && nested_for &&
            nested_condition_inner && nested_body_outer && nested_body_inner &&
            after_loop_outer && after_loop_macro && outer_declaration,
        "for-control hover source anchors");

  ag_language_analysis_snapshot_t outer_baseline_snapshot = {0};
  CHECK(analyze_named(
            session, "for-control.c", for_control_hover_source,
            (size_t)(outer_declaration - for_control_hover_source) + 2,
            (header_bundle_t){0}, defaults, &outer_baseline_snapshot, &error),
        "for-control outer hover baseline");
  const ag_language_symbol_t *outer_baseline_hover =
      hover_symbol(&outer_baseline_snapshot);
  CHECK(outer_baseline_hover &&
            outer_baseline_hover->kind == AG_LANGUAGE_SYMBOL_OBJECT &&
            strcmp(outer_baseline_hover->name, "outer") == 0 &&
            strcmp(outer_baseline_hover->type, "int") == 0 &&
            !outer_baseline_snapshot.partial &&
            outer_baseline_snapshot.diagnostic_count == 0,
        "for-control outer hover baseline fields");
  CHECK(analyze_named(
            session, "for-control.c", for_control_hover_source,
            (size_t)(after_loop_outer - for_control_hover_source),
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "post-for object hover");
  CHECK(same_object_hover(
            hover_symbol(&snapshot), outer_baseline_hover) &&
            !snapshot.partial && snapshot.diagnostic_count == 0 &&
            !find_symbol(&snapshot, "inner", AG_LANGUAGE_SYMBOL_OBJECT),
        "for-init object leaves scope after loop");
  ag_language_analysis_snapshot_dispose(&snapshot);
  const char *outer_uses[] = {
      init_outer,
      nested_init_outer,
      condition_outer,
      update_outer,
      comma_update_outer,
      empty_for_body_outer,
      empty_update_init_outer,
      empty_update_condition_outer,
      nested_body_outer,
      after_loop_outer,
  };
  size_t object_cursor_deltas_for[] = {0, 2, strlen("outer")};
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t use_index = 0;
         use_index < sizeof(outer_uses) / sizeof(outer_uses[0]);
         use_index++) {
      for (size_t cursor_index = 0;
           cursor_index < sizeof(object_cursor_deltas_for) /
                              sizeof(object_cursor_deltas_for[0]);
           cursor_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh for-control object hover session");
        }
        size_t cursor =
            (size_t)(outer_uses[use_index] - for_control_hover_source) +
            object_cursor_deltas_for[cursor_index];
        CHECK(analyze_named(
                  analysis_session, "for-control.c",
                  for_control_hover_source, cursor, (header_bundle_t){0},
                  defaults, &snapshot, &error),
              "for-control object hover");
        CHECK(same_object_hover(
                  hover_symbol(&snapshot), outer_baseline_hover) &&
                  !snapshot.partial && snapshot.diagnostic_count == 0,
              "for-control object hover fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }

  ag_language_analysis_snapshot_t inner_baseline_snapshot = {0};
  CHECK(analyze_named(
            session, "for-control.c", for_control_hover_source,
            (size_t)(nested_body_inner - for_control_hover_source),
            (header_bundle_t){0}, defaults, &inner_baseline_snapshot, &error),
        "for-init object hover baseline");
  const ag_language_symbol_t *inner_baseline_hover =
      hover_symbol(&inner_baseline_snapshot);
  const char *inner_uses[] = {
      inner_condition,
      inner_update,
      nested_condition_inner,
      nested_body_inner,
  };
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t use_index = 0;
         use_index < sizeof(inner_uses) / sizeof(inner_uses[0]);
         use_index++) {
      for (size_t cursor_index = 0;
           cursor_index < sizeof(object_cursor_deltas_for) /
                              sizeof(object_cursor_deltas_for[0]);
           cursor_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh for-init object hover session");
        }
        size_t cursor =
            (size_t)(inner_uses[use_index] - for_control_hover_source) +
            object_cursor_deltas_for[cursor_index];
        CHECK(analyze_named(
                  analysis_session, "for-control.c",
                  for_control_hover_source, cursor, (header_bundle_t){0},
                  defaults, &snapshot, &error),
              "for-init object hover");
        CHECK(same_object_hover(
                  hover_symbol(&snapshot), inner_baseline_hover) &&
                  !snapshot.partial && snapshot.diagnostic_count == 0,
              "for-init object hover fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }

  const char *enum_declaration = strstr(
      for_control_hover_source, "ENEMY_COUNT");
  ag_language_analysis_snapshot_t enum_baseline_snapshot = {0};
  CHECK(analyze_named(
            session, "for-control.c", for_control_hover_source,
            (size_t)(enum_declaration - for_control_hover_source) + 2,
            (header_bundle_t){0}, defaults, &enum_baseline_snapshot, &error),
        "for-control enum hover baseline");
  const ag_language_symbol_t *enum_baseline_hover =
      hover_symbol(&enum_baseline_snapshot);
  const char *enum_uses[] = {for_enum_use, inner_enum_use};
  size_t enum_cursor_deltas_for[] = {
      0, strlen("ENEMY_COUNT") / 2, strlen("ENEMY_COUNT"),
  };
  for (size_t use_index = 0;
       use_index < sizeof(enum_uses) / sizeof(enum_uses[0]); use_index++) {
    for (size_t cursor_index = 0;
         cursor_index < sizeof(enum_cursor_deltas_for) /
                            sizeof(enum_cursor_deltas_for[0]);
         cursor_index++) {
      size_t cursor =
          (size_t)(enum_uses[use_index] - for_control_hover_source) +
          enum_cursor_deltas_for[cursor_index];
      CHECK(analyze_named(
                session, "for-control.c", for_control_hover_source, cursor,
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "for-control enum hover");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      CHECK(hover && enum_baseline_hover &&
                hover->kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT &&
                strcmp(hover->constant_value, "8") == 0 &&
                same_range(
                    &hover->declaration,
                    &enum_baseline_hover->declaration) &&
                !snapshot.partial && snapshot.diagnostic_count == 0,
            "for-control enum hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  ag_language_analysis_snapshot_t macro_baseline_snapshot = {0};
  CHECK(analyze_named(
            session, "for-control.c", for_control_hover_source,
            (size_t)(after_loop_macro - for_control_hover_source),
            (header_bundle_t){0}, defaults, &macro_baseline_snapshot, &error),
        "for-control macro hover baseline");
  const ag_language_symbol_t *macro_baseline_hover =
      hover_symbol(&macro_baseline_snapshot);
  CHECK(macro_baseline_hover && !macro_baseline_snapshot.partial &&
            macro_baseline_snapshot.diagnostic_count == 0,
        "for-control macro hover baseline fields");
  size_t macro_cursor_deltas_for[] = {
      0, strlen("LOOP_LIMIT") / 2, strlen("LOOP_LIMIT"),
  };
  for (size_t cursor_index = 0;
       cursor_index < sizeof(macro_cursor_deltas_for) /
                          sizeof(macro_cursor_deltas_for[0]);
       cursor_index++) {
    size_t cursor = (size_t)(for_macro_use - for_control_hover_source) +
                    macro_cursor_deltas_for[cursor_index];
    CHECK(analyze_named(
              session, "for-control.c", for_control_hover_source, cursor,
              (header_bundle_t){0}, defaults, &snapshot, &error),
          "for-control macro hover");
    const ag_language_symbol_t *hover = hover_symbol(&snapshot);
    CHECK(hover && macro_baseline_hover &&
              hover->kind == AG_LANGUAGE_SYMBOL_MACRO &&
              strcmp(hover->name, "LOOP_LIMIT") == 0 &&
              strcmp(hover->macro_replacement, "8") == 0 &&
              same_range(
                  &hover->declaration,
                  &macro_baseline_hover->declaration) &&
              !snapshot.partial && snapshot.diagnostic_count == 0,
          "for-control macro hover fields");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  ag_language_analysis_snapshot_dispose(&macro_baseline_snapshot);
  ag_language_analysis_snapshot_dispose(&enum_baseline_snapshot);
  ag_language_analysis_snapshot_dispose(&inner_baseline_snapshot);
  ag_language_analysis_snapshot_dispose(&outer_baseline_snapshot);

  const char *conditional_macro_declaration = strstr(
      conditional_hover_source, "CHOICE_MACRO");
  const char *conditional_first_declaration = strstr(
      conditional_hover_source, "FIRST");
  const char *conditional_second_declaration = strstr(
      conditional_hover_source, "SECOND");
  const char *conditional_third_declaration = strstr(
      conditional_hover_source, "THIRD");
  const char *conditional_alternate_declaration = strstr(
      conditional_hover_source, "alternate,");
  const char *conditional_other_declaration = strstr(
      conditional_alternate_declaration, "other)");
  const char *conditional_local_declaration = strstr(
      conditional_hover_source, "local = 4");
  const char *conditional_direct = strstr(
      conditional_hover_source, "choose(alternate ? FIRST : SECOND");
  const char *conditional_direct_alternate = strstr(
      conditional_direct, "alternate");
  const char *conditional_direct_first = strstr(
      conditional_direct_alternate, "FIRST");
  const char *conditional_direct_second = strstr(
      conditional_direct_first, "SECOND");
  const char *conditional_direct_other = strstr(
      conditional_direct_second, "other ?");
  const char *conditional_parenthesized = strstr(
      conditional_direct_other, "local += (alternate");
  const char *conditional_parenthesized_alternate = strstr(
      conditional_parenthesized, "alternate");
  const char *conditional_subscript = strstr(
      conditional_parenthesized_alternate, "values[alternate");
  const char *conditional_subscript_alternate = strstr(
      conditional_subscript, "alternate");
  const char *conditional_comma = strstr(
      conditional_subscript_alternate,
      "local += alternate ? (choose(local, FIRST), SECOND)");
  const char *conditional_comma_alternate = strstr(
      conditional_comma, "alternate");
  const char *conditional_comma_local = strstr(
      conditional_comma_alternate, "local");
  const char *conditional_comma_first = strstr(
      conditional_comma_local, "FIRST");
  const char *conditional_comma_second = strstr(
      conditional_comma_first, "SECOND");
  const char *conditional_comma_false_local = strstr(
      conditional_comma_second, "local");
  const char *conditional_comma_third = strstr(
      conditional_comma_false_local, "THIRD");
  const char *conditional_nested_true = strstr(
      conditional_comma_third,
      "local += alternate ? other ? FIRST : SECOND : THIRD");
  const char *conditional_nested_true_alternate = strstr(
      conditional_nested_true, "alternate");
  const char *conditional_nested_true_other = strstr(
      conditional_nested_true_alternate, "other");
  const char *conditional_nested_true_first = strstr(
      conditional_nested_true_other, "FIRST");
  const char *conditional_nested_true_second = strstr(
      conditional_nested_true_first, "SECOND");
  const char *conditional_nested_true_third = strstr(
      conditional_nested_true_second, "THIRD");
  const char *conditional_nested_false = strstr(
      conditional_nested_true_third,
      "local += alternate ? FIRST : other ? SECOND : THIRD");
  const char *conditional_nested_false_alternate = strstr(
      conditional_nested_false, "alternate");
  const char *conditional_nested_false_first = strstr(
      conditional_nested_false_alternate, "FIRST");
  const char *conditional_nested_false_other = strstr(
      conditional_nested_false_first, "other");
  const char *conditional_nested_false_second = strstr(
      conditional_nested_false_other, "SECOND");
  const char *conditional_nested_false_third = strstr(
      conditional_nested_false_second, "THIRD");
  const char *conditional_return = strstr(
      conditional_nested_false_third,
      "return alternate ? other ? FIRST : SECOND");
  const char *conditional_return_alternate = strstr(
      conditional_return, "alternate");
  const char *conditional_return_other = strstr(
      conditional_return_alternate, "other");
  const char *conditional_return_first = strstr(
      conditional_return_other, "FIRST");
  const char *conditional_return_second = strstr(
      conditional_return_first, "SECOND");
  const char *conditional_return_local = strstr(
      conditional_return_second, "local ?");
  const char *conditional_return_macro = strstr(
      conditional_return_local, "CHOICE_MACRO");
  const char *conditional_return_third = strstr(
      conditional_return_macro, "THIRD");
  CHECK(conditional_macro_declaration && conditional_first_declaration &&
            conditional_second_declaration && conditional_third_declaration &&
            conditional_alternate_declaration &&
            conditional_other_declaration && conditional_local_declaration &&
            conditional_direct && conditional_direct_alternate &&
            conditional_direct_first && conditional_direct_second &&
            conditional_direct_other && conditional_parenthesized &&
            conditional_parenthesized_alternate && conditional_subscript &&
            conditional_subscript_alternate && conditional_comma &&
            conditional_comma_alternate && conditional_comma_local &&
            conditional_comma_first && conditional_comma_second &&
            conditional_comma_false_local && conditional_comma_third &&
            conditional_nested_true && conditional_nested_true_alternate &&
            conditional_nested_true_other && conditional_nested_true_first &&
            conditional_nested_true_second && conditional_nested_true_third &&
            conditional_nested_false && conditional_nested_false_alternate &&
            conditional_nested_false_first && conditional_nested_false_other &&
            conditional_nested_false_second && conditional_nested_false_third &&
            conditional_return && conditional_return_alternate &&
            conditional_return_other && conditional_return_first &&
            conditional_return_second && conditional_return_local &&
            conditional_return_macro && conditional_return_third,
        "conditional hover source anchors");
  struct {
    const char *use;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration;
    const char *constant_value;
    const char *macro_replacement;
    int check_all_positions;
  } conditional_cases[] = {
      {conditional_direct_alternate, "alternate", AG_LANGUAGE_SYMBOL_PARAMETER,
       conditional_alternate_declaration, "", "", 1},
      {conditional_direct_first, "FIRST", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       conditional_first_declaration, "1", "", 1},
      {conditional_direct_second, "SECOND", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       conditional_second_declaration, "2", "", 1},
      {conditional_direct_other, "other", AG_LANGUAGE_SYMBOL_PARAMETER,
       conditional_other_declaration, "", "", 0},
      {conditional_parenthesized_alternate, "alternate",
       AG_LANGUAGE_SYMBOL_PARAMETER, conditional_alternate_declaration,
       "", "", 0},
      {conditional_subscript_alternate, "alternate",
       AG_LANGUAGE_SYMBOL_PARAMETER, conditional_alternate_declaration,
       "", "", 0},
      {conditional_comma_alternate, "alternate", AG_LANGUAGE_SYMBOL_PARAMETER,
       conditional_alternate_declaration, "", "", 0},
      {conditional_comma_local, "local", AG_LANGUAGE_SYMBOL_OBJECT,
       conditional_local_declaration, "", "", 0},
      {conditional_comma_first, "FIRST", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       conditional_first_declaration, "1", "", 0},
      {conditional_comma_second, "SECOND", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       conditional_second_declaration, "2", "", 0},
      {conditional_comma_false_local, "local", AG_LANGUAGE_SYMBOL_OBJECT,
       conditional_local_declaration, "", "", 0},
      {conditional_comma_third, "THIRD", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       conditional_third_declaration, "3", "", 0},
      {conditional_nested_true_alternate, "alternate",
       AG_LANGUAGE_SYMBOL_PARAMETER, conditional_alternate_declaration,
       "", "", 0},
      {conditional_nested_true_other, "other", AG_LANGUAGE_SYMBOL_PARAMETER,
       conditional_other_declaration, "", "", 0},
      {conditional_nested_true_first, "FIRST", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       conditional_first_declaration, "1", "", 0},
      {conditional_nested_true_second, "SECOND",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, conditional_second_declaration,
       "2", "", 0},
      {conditional_nested_true_third, "THIRD", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       conditional_third_declaration, "3", "", 0},
      {conditional_nested_false_alternate, "alternate",
       AG_LANGUAGE_SYMBOL_PARAMETER, conditional_alternate_declaration,
       "", "", 0},
      {conditional_nested_false_first, "FIRST",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, conditional_first_declaration,
       "1", "", 0},
      {conditional_nested_false_other, "other", AG_LANGUAGE_SYMBOL_PARAMETER,
       conditional_other_declaration, "", "", 0},
      {conditional_nested_false_second, "SECOND",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, conditional_second_declaration,
       "2", "", 0},
      {conditional_nested_false_third, "THIRD",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, conditional_third_declaration,
       "3", "", 0},
      {conditional_return_alternate, "alternate", AG_LANGUAGE_SYMBOL_PARAMETER,
       conditional_alternate_declaration, "", "", 0},
      {conditional_return_other, "other", AG_LANGUAGE_SYMBOL_PARAMETER,
       conditional_other_declaration, "", "", 0},
      {conditional_return_first, "FIRST", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       conditional_first_declaration, "1", "", 0},
      {conditional_return_second, "SECOND", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       conditional_second_declaration, "2", "", 0},
      {conditional_return_local, "local", AG_LANGUAGE_SYMBOL_OBJECT,
       conditional_local_declaration, "", "", 0},
      {conditional_return_macro, "CHOICE_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       conditional_macro_declaration, "", "7", 0},
      {conditional_return_third, "THIRD", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       conditional_third_declaration, "3", "", 0},
  };
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(conditional_cases) /
                          sizeof(conditional_cases[0]);
         case_index++) {
      size_t name_length = strlen(conditional_cases[case_index].name);
      size_t cursor_deltas[] = {
          conditional_cases[case_index].check_all_positions
              ? 0 : name_length / 2,
          name_length / 2,
          name_length,
      };
      size_t cursor_count =
          conditional_cases[case_index].check_all_positions ? 3 : 1;
      for (size_t cursor_index = 0; cursor_index < cursor_count;
           cursor_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL,
                "fresh conditional hover session");
        }
        size_t cursor =
            (size_t)(conditional_cases[case_index].use -
                     conditional_hover_source) +
            cursor_deltas[cursor_index];
        CHECK(analyze_named(
                  analysis_session, "conditional.c",
                  conditional_hover_source, cursor, (header_bundle_t){0},
                  defaults, &snapshot, &error),
              "conditional hover analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        CHECK(hover && hover->kind == conditional_cases[case_index].kind &&
                  strcmp(hover->name, conditional_cases[case_index].name) == 0 &&
                  ((hover->kind != AG_LANGUAGE_SYMBOL_PARAMETER &&
                    hover->kind != AG_LANGUAGE_SYMBOL_OBJECT) ||
                   strcmp(hover->type, "int") == 0) &&
                  (hover->kind != AG_LANGUAGE_SYMBOL_ENUM_CONSTANT ||
                   strcmp(hover->constant_value,
                          conditional_cases[case_index].constant_value) == 0) &&
                  (hover->kind != AG_LANGUAGE_SYMBOL_MACRO ||
                   strcmp(hover->macro_replacement,
                          conditional_cases[case_index].macro_replacement) == 0) &&
                  strcmp(hover->declaration.source_name, "conditional.c") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(conditional_cases[case_index].declaration -
                            conditional_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(conditional_cases[case_index].declaration -
                            conditional_hover_source + name_length) &&
                  !snapshot.partial && snapshot.diagnostic_count == 0,
              "conditional hover fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }

  const char *generic_macro_declaration = strstr(
      generic_hover_source, "GENERIC_MACRO");
  const char *generic_call_macro_declaration = strstr(
      generic_hover_source, "GENERIC_CALL");
  const char *generic_invoked_object_declaration = strstr(
      generic_hover_source, "GENERIC_INVOKED");
  const char *generic_invoked_macro_declaration =
      generic_invoked_object_declaration
          ? strstr(generic_invoked_object_declaration +
                       strlen("GENERIC_INVOKED"),
                   "GENERIC_INVOKED")
          : NULL;
  const char *generic_trigraph_prefix_declaration = strstr(
      generic_invoked_macro_declaration, "GENERIC_TRIGRAPH_PREFIX");
  const char *generic_after_translation_macro_declaration = strstr(
      generic_trigraph_prefix_declaration, "GENERIC_AFTER_TRANSLATION");
  const char *generic_after_translation_object_declaration = strstr(
      generic_after_translation_macro_declaration,
      "generic_after_translation");
  const char *generic_typedef_declaration = strstr(
      generic_hover_source, "GenericScore");
  const char *generic_struct_declaration = strstr(
      generic_hover_source, "GenericPlayer");
  const char *generic_union_declaration = strstr(
      generic_hover_source, "GenericPayload");
  const char *generic_enum_tag_declaration = strstr(
      generic_hover_source, "GenericState");
  const char *generic_enum_declaration = strstr(
      generic_hover_source, "GENERIC_MODE");
  const char *generic_object_declaration = strstr(
      generic_hover_source, "generic_value");
  const char *generic_function_declaration = strstr(
      generic_hover_source, "generic_identity");
  const char *generic_comment_invocation = strstr(
      generic_function_declaration, "generic_comment_invocation");
  const char *generic_comment_macro_use = strstr(
      generic_comment_invocation, "GENERIC_INVOKED");
  const char *generic_lf_invocation = strstr(
      generic_comment_macro_use, "generic_lf_invocation");
  const char *generic_lf_macro_use = strstr(
      generic_lf_invocation, "GENERIC_INVOKED");
  const char *generic_crlf_invocation = strstr(
      generic_lf_macro_use, "generic_crlf_invocation");
  const char *generic_crlf_macro_use = strstr(
      generic_crlf_invocation, "GENERIC_INVOKED");
  const char *generic_trigraph_lf_invocation = strstr(
      generic_crlf_macro_use, "generic_trigraph_lf_invocation");
  const char *generic_trigraph_lf_macro_use = strstr(
      generic_trigraph_lf_invocation, "GENERIC_INVOKED");
  const char *generic_trigraph_crlf_invocation = strstr(
      generic_trigraph_lf_macro_use, "generic_trigraph_crlf_invocation");
  const char *generic_trigraph_crlf_macro_use = strstr(
      generic_trigraph_crlf_invocation, "GENERIC_INVOKED");
  const char *generic_first_control = strstr(
      generic_function_declaration, "_Generic(generic_value");
  const char *generic_first_object_use = strstr(
      generic_first_control, "generic_value");
  const char *generic_call_control = strstr(
      generic_first_object_use, "_Generic(generic_identity");
  const char *generic_function_use = strstr(
      generic_call_control, "generic_identity");
  const char *generic_argument_use = strstr(
      generic_function_use + strlen("generic_identity"), "generic_value");
  const char *generic_enum_control = strstr(
      generic_argument_use, "_Generic(GENERIC_MODE");
  const char *generic_enum_use = strstr(
      generic_enum_control, "GENERIC_MODE");
  const char *generic_macro_control = strstr(
      generic_enum_use, "_Generic(GENERIC_MACRO");
  const char *generic_macro_use = strstr(
      generic_macro_control, "GENERIC_MACRO");
  const char *generic_call_macro_control = strstr(
      generic_macro_use, "_Generic(GENERIC_CALL()");
  const char *generic_call_macro_use = strstr(
      generic_call_macro_control, "GENERIC_CALL");
  const char *generic_trigraph_prefix_use = strstr(
      generic_call_macro_use, "GENERIC_TRIGRAPH_PREFIX");
  const char *generic_after_translation_macro_use = strstr(
      generic_trigraph_prefix_use, "GENERIC_AFTER_TRANSLATION");
  const char *generic_after_translation_object_use = strstr(
      generic_after_translation_macro_use, "generic_after_translation");
  const char *generic_typedef_control = strstr(
      generic_after_translation_object_use,
      "_Generic(generic_value, GenericScore");
  const char *generic_typedef_use = strstr(
      generic_typedef_control, "GenericScore");
  const char *generic_atomic_query = strstr(
      generic_typedef_use + strlen("GenericScore"),
      "_Atomic /* type */ (GenericScore");
  const char *generic_atomic_typedef_use = strstr(
      generic_atomic_query, "GenericScore");
  const char *generic_alignof = strstr(
      generic_atomic_typedef_use + strlen("GenericScore"),
      "_Alignof /* query */ (const GenericScore");
  const char *generic_alignof_typedef_use = strstr(
      generic_alignof, "GenericScore");
  const char *generic_alignof_bound = strstr(
      generic_alignof_typedef_use, "_Alignof(int [1 + GENERIC_MODE");
  const char *generic_alignof_bound_use = strstr(
      generic_alignof_bound, "GENERIC_MODE");
  const char *generic_struct_query = strstr(
      generic_alignof_bound_use,
      "sizeof /* query */ (const struct /* tag */ GenericPlayer");
  const char *generic_struct_use = strstr(
      generic_struct_query, "GenericPlayer");
  const char *generic_union_query = strstr(
      generic_struct_use, "_Alignof(union /* tag */ GenericPayload");
  const char *generic_union_use = strstr(
      generic_union_query, "GenericPayload");
  const char *generic_enum_tag_query = strstr(
      generic_union_use, "sizeof(enum /* tag */ GenericState");
  const char *generic_enum_tag_use = strstr(
      generic_enum_tag_query, "GenericState");
  const char *generic_struct_literal = strstr(
      generic_enum_tag_use,
      "generic_score((struct /* literal */ GenericPlayer)");
  const char *generic_struct_literal_use = strstr(
      generic_struct_literal, "GenericPlayer");
  const char *generic_enum_cast = strstr(
      generic_struct_literal_use,
      "(enum /* cast */ GenericState)");
  const char *generic_enum_cast_use = strstr(
      generic_enum_cast, "GenericState");
  const char *generic_pointer_cast = strstr(
      generic_enum_cast_use,
      "(const struct /* pointer cast */ GenericPlayer * const)0");
  const char *generic_pointer_cast_use = strstr(
      generic_pointer_cast, "GenericPlayer");
  const char *generic_pointer_chain = strstr(
      generic_pointer_cast_use,
      "(const struct /* pointer chain */ GenericPlayer * /* inner */ const * restrict)0");
  const char *generic_pointer_chain_use = strstr(
      generic_pointer_chain, "GenericPlayer");
  const char *generic_tag_association = strstr(
      generic_pointer_chain_use,
      "struct /* association */ GenericPlayer");
  const char *generic_tag_association_use = strstr(
      generic_tag_association, "GenericPlayer");
  const char *generic_pointer_association = strstr(
      generic_tag_association_use,
      "struct /* pointer association */ GenericPlayer * const");
  const char *generic_pointer_association_use = strstr(
      generic_pointer_association, "GenericPlayer");
  const char *generic_default_first_association = strstr(
      generic_pointer_association_use,
      "struct /* default first association */ GenericPlayer *");
  const char *generic_default_first_association_use = strstr(
      generic_default_first_association, "GenericPlayer");
  const char *generic_array_pointer_association = strstr(
      generic_default_first_association_use,
      "struct /* array pointer association */ GenericPlayer (*)[2]");
  const char *generic_array_pointer_association_use = strstr(
      generic_array_pointer_association, "GenericPlayer");
  const char *generic_array_pointer_cast = strstr(
      generic_array_pointer_association_use,
      "struct /* array pointer cast */ GenericPlayer (*)[2]");
  const char *generic_array_pointer_cast_use = strstr(
      generic_array_pointer_cast, "GenericPlayer");
  const char *generic_quoted_array_bound = strstr(
      generic_array_pointer_cast_use,
      "struct /* quoted array bound */ GenericPlayer (*)[sizeof(\")\")]");
  const char *generic_quoted_array_bound_use = strstr(
      generic_quoted_array_bound, "GenericPlayer");
  const char *generic_function_pointer_cast = strstr(
      generic_quoted_array_bound_use,
      "struct /* function pointer cast */ GenericPlayer (*)(void)");
  const char *generic_function_pointer_cast_use = strstr(
      generic_function_pointer_cast, "GenericPlayer");
  const char *generic_array_literal = strstr(
      generic_function_pointer_cast_use,
      "struct /* array literal */ GenericPlayer [2]");
  const char *generic_array_literal_use = strstr(
      generic_array_literal, "GenericPlayer");
  const char *generic_value_association = strstr(
      generic_array_literal_use, "int: generic_value");
  const char *generic_association_value_use = strstr(
      generic_value_association, "generic_value");
  CHECK(generic_macro_declaration && generic_call_macro_declaration &&
            generic_invoked_object_declaration &&
            generic_invoked_macro_declaration &&
            generic_trigraph_prefix_declaration &&
            generic_after_translation_macro_declaration &&
            generic_after_translation_object_declaration &&
            generic_typedef_declaration &&
            generic_struct_declaration && generic_union_declaration &&
            generic_enum_tag_declaration &&
            generic_enum_declaration && generic_object_declaration &&
            generic_function_declaration && generic_comment_invocation &&
            generic_comment_macro_use && generic_lf_invocation &&
            generic_lf_macro_use && generic_crlf_invocation &&
            generic_crlf_macro_use && generic_trigraph_lf_invocation &&
            generic_trigraph_lf_macro_use &&
            generic_trigraph_crlf_invocation &&
            generic_trigraph_crlf_macro_use && generic_first_control &&
            generic_first_object_use && generic_call_control &&
            generic_function_use && generic_argument_use &&
            generic_enum_control && generic_enum_use &&
            generic_macro_control && generic_macro_use &&
            generic_call_macro_control && generic_call_macro_use &&
            generic_trigraph_prefix_use &&
            generic_after_translation_macro_use &&
            generic_after_translation_object_use &&
            generic_typedef_control && generic_typedef_use &&
            generic_atomic_query && generic_atomic_typedef_use &&
            generic_alignof && generic_alignof_typedef_use &&
            generic_alignof_bound && generic_alignof_bound_use &&
            generic_struct_query && generic_struct_use &&
            generic_union_query && generic_union_use &&
            generic_enum_tag_query && generic_enum_tag_use &&
            generic_struct_literal && generic_struct_literal_use &&
            generic_enum_cast && generic_enum_cast_use &&
            generic_pointer_cast && generic_pointer_cast_use &&
            generic_pointer_chain && generic_pointer_chain_use &&
            generic_tag_association && generic_tag_association_use &&
            generic_pointer_association && generic_pointer_association_use &&
            generic_default_first_association &&
            generic_default_first_association_use &&
            generic_array_pointer_association &&
            generic_array_pointer_association_use &&
            generic_array_pointer_cast && generic_array_pointer_cast_use &&
            generic_quoted_array_bound && generic_quoted_array_bound_use &&
            generic_function_pointer_cast &&
            generic_function_pointer_cast_use &&
            generic_array_literal && generic_array_literal_use &&
            generic_value_association && generic_association_value_use,
        "generic hover source anchors");
  struct {
    const char *use;
    const char *name;
    ag_language_symbol_kind_t kind;
    const char *declaration;
    const char *constant_value;
    const char *macro_replacement;
  } generic_cases[] = {
      {generic_first_object_use, "generic_value", AG_LANGUAGE_SYMBOL_OBJECT,
       generic_object_declaration, "", ""},
      {generic_function_use, "generic_identity", AG_LANGUAGE_SYMBOL_FUNCTION,
       generic_function_declaration, "", ""},
      {generic_argument_use, "generic_value", AG_LANGUAGE_SYMBOL_OBJECT,
       generic_object_declaration, "", ""},
      {generic_enum_use, "GENERIC_MODE", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
       generic_enum_declaration, "3", ""},
      {generic_macro_use, "GENERIC_MACRO", AG_LANGUAGE_SYMBOL_MACRO,
       generic_macro_declaration, "", "9"},
      {generic_call_macro_use, "GENERIC_CALL", AG_LANGUAGE_SYMBOL_MACRO,
       generic_call_macro_declaration, "", "6"},
      {generic_trigraph_prefix_use, "GENERIC_TRIGRAPH_PREFIX",
       AG_LANGUAGE_SYMBOL_MACRO, generic_trigraph_prefix_declaration, "", "11"},
      {generic_after_translation_macro_use, "GENERIC_AFTER_TRANSLATION",
       AG_LANGUAGE_SYMBOL_MACRO, generic_after_translation_macro_declaration,
       "", "13"},
      {generic_after_translation_object_use, "generic_after_translation",
       AG_LANGUAGE_SYMBOL_OBJECT, generic_after_translation_object_declaration,
       "", ""},
      {generic_comment_macro_use, "GENERIC_INVOKED", AG_LANGUAGE_SYMBOL_MACRO,
       generic_invoked_macro_declaration, "", "7"},
      {generic_lf_macro_use, "GENERIC_INVOKED", AG_LANGUAGE_SYMBOL_MACRO,
       generic_invoked_macro_declaration, "", "7"},
      {generic_crlf_macro_use, "GENERIC_INVOKED", AG_LANGUAGE_SYMBOL_MACRO,
       generic_invoked_macro_declaration, "", "7"},
      {generic_trigraph_lf_macro_use, "GENERIC_INVOKED",
       AG_LANGUAGE_SYMBOL_MACRO, generic_invoked_macro_declaration, "", "7"},
      {generic_trigraph_crlf_macro_use, "GENERIC_INVOKED",
       AG_LANGUAGE_SYMBOL_MACRO, generic_invoked_macro_declaration, "", "7"},
      {generic_typedef_use, "GenericScore", AG_LANGUAGE_SYMBOL_TYPEDEF,
       generic_typedef_declaration, "", ""},
      {generic_atomic_typedef_use, "GenericScore",
       AG_LANGUAGE_SYMBOL_TYPEDEF, generic_typedef_declaration, "", ""},
      {generic_alignof_typedef_use, "GenericScore", AG_LANGUAGE_SYMBOL_TYPEDEF,
       generic_typedef_declaration, "", ""},
      {generic_alignof_bound_use, "GENERIC_MODE",
       AG_LANGUAGE_SYMBOL_ENUM_CONSTANT, generic_enum_declaration, "3", ""},
      {generic_struct_use, "GenericPlayer", AG_LANGUAGE_SYMBOL_TAG,
       generic_struct_declaration, "", ""},
      {generic_union_use, "GenericPayload", AG_LANGUAGE_SYMBOL_TAG,
       generic_union_declaration, "", ""},
      {generic_enum_tag_use, "GenericState", AG_LANGUAGE_SYMBOL_TAG,
       generic_enum_tag_declaration, "", ""},
      {generic_struct_literal_use, "GenericPlayer", AG_LANGUAGE_SYMBOL_TAG,
       generic_struct_declaration, "", ""},
      {generic_enum_cast_use, "GenericState", AG_LANGUAGE_SYMBOL_TAG,
       generic_enum_tag_declaration, "", ""},
      {generic_pointer_cast_use, "GenericPlayer", AG_LANGUAGE_SYMBOL_TAG,
       generic_struct_declaration, "", ""},
      {generic_pointer_chain_use, "GenericPlayer", AG_LANGUAGE_SYMBOL_TAG,
       generic_struct_declaration, "", ""},
      {generic_tag_association_use, "GenericPlayer", AG_LANGUAGE_SYMBOL_TAG,
       generic_struct_declaration, "", ""},
      {generic_pointer_association_use, "GenericPlayer",
       AG_LANGUAGE_SYMBOL_TAG, generic_struct_declaration, "", ""},
      {generic_default_first_association_use, "GenericPlayer",
       AG_LANGUAGE_SYMBOL_TAG, generic_struct_declaration, "", ""},
      {generic_array_pointer_association_use, "GenericPlayer",
       AG_LANGUAGE_SYMBOL_TAG, generic_struct_declaration, "", ""},
      {generic_array_pointer_cast_use, "GenericPlayer",
       AG_LANGUAGE_SYMBOL_TAG, generic_struct_declaration, "", ""},
      {generic_quoted_array_bound_use, "GenericPlayer",
       AG_LANGUAGE_SYMBOL_TAG, generic_struct_declaration, "", ""},
      {generic_function_pointer_cast_use, "GenericPlayer",
       AG_LANGUAGE_SYMBOL_TAG, generic_struct_declaration, "", ""},
      {generic_array_literal_use, "GenericPlayer", AG_LANGUAGE_SYMBOL_TAG,
       generic_struct_declaration, "", ""},
      {generic_association_value_use, "generic_value",
       AG_LANGUAGE_SYMBOL_OBJECT, generic_object_declaration, "", ""},
  };
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(generic_cases) / sizeof(generic_cases[0]);
         case_index++) {
      size_t name_length = strlen(generic_cases[case_index].name);
      size_t cursor_deltas[] = {0, name_length / 2, name_length};
      for (size_t cursor_index = 0;
           cursor_index < sizeof(cursor_deltas) / sizeof(cursor_deltas[0]);
           cursor_index++) {
        ag_compilation_session_t *analysis_session = session;
        if (fresh_session) {
          analysis_session = ag_compilation_session_create(&target);
          CHECK(analysis_session != NULL, "fresh generic hover session");
        }
        size_t cursor =
            (size_t)(generic_cases[case_index].use - generic_hover_source) +
            cursor_deltas[cursor_index];
        CHECK(analyze_named(
                  analysis_session, "generic.c", generic_hover_source,
                  cursor, (header_bundle_t){0}, defaults, &snapshot, &error),
              "generic hover analysis");
        const ag_language_symbol_t *hover = hover_symbol(&snapshot);
        CHECK(hover && hover->kind == generic_cases[case_index].kind &&
                  strcmp(hover->name, generic_cases[case_index].name) == 0 &&
                  (hover->kind != AG_LANGUAGE_SYMBOL_ENUM_CONSTANT ||
                   strcmp(hover->constant_value,
                          generic_cases[case_index].constant_value) == 0) &&
                  (hover->kind != AG_LANGUAGE_SYMBOL_MACRO ||
                   strcmp(hover->macro_replacement,
                          generic_cases[case_index].macro_replacement) == 0) &&
                  strcmp(hover->declaration.source_name, "generic.c") == 0 &&
                  hover->declaration.start.offset ==
                      (int)(generic_cases[case_index].declaration -
                            generic_hover_source) &&
                  hover->declaration.end.offset ==
                      (int)(generic_cases[case_index].declaration -
                            generic_hover_source + name_length) &&
                  !snapshot.partial && snapshot.diagnostic_count == 0,
              "generic hover fields");
        ag_language_analysis_snapshot_dispose(&snapshot);
        if (fresh_session)
          ag_compilation_session_destroy(analysis_session);
      }
    }
  }

  const char *spliced_identifier_names[] = {
      "generic_literal_lf_value",
      "generic_literal_crlf_value",
      "generic_trigraph_lf_value",
      "generic_trigraph_crlf_value",
  };
  const char *spliced_identifier_spellings[] = {
      "generic_literal_lf_\\\nvalue",
      "generic_literal_crlf_\\\r\nvalue",
      "generic_trigraph_lf_?" "?/\nvalue",
      "generic_trigraph_crlf_?" "?/\r\nvalue",
  };
  for (int fresh_session = 0; fresh_session < 2; fresh_session++) {
    for (size_t case_index = 0;
         case_index < sizeof(spliced_identifier_names) /
                          sizeof(spliced_identifier_names[0]);
         case_index++) {
      const char *spelling = spliced_identifier_spellings[case_index];
      size_t spelling_length = strlen(spelling);
      const char *declaration = strstr(generic_hover_source, spelling);
      const char *use = declaration
                            ? strstr(declaration + spelling_length, spelling)
                            : NULL;
      const char *splice = strchr(spelling, '\\');
      if (!splice) splice = strstr(spelling, "?" "?/");
      const char *newline = splice ? strchr(splice, '\n') : NULL;
      CHECK(declaration && use && splice && newline,
            "spliced identifier anchors");
      size_t prefix_length = (size_t)(splice - spelling);
      size_t splice_length = (size_t)(newline - splice) + 1;
      size_t cursor_deltas[] = {
          0,
          prefix_length / 2,
          prefix_length,
          prefix_length + splice_length / 2,
          prefix_length + splice_length,
          spelling_length,
      };
      const char *locations[] = {declaration, use};
      for (size_t location_index = 0;
           location_index < sizeof(locations) / sizeof(locations[0]);
           location_index++) {
        for (size_t cursor_index = 0;
             cursor_index < sizeof(cursor_deltas) /
                                sizeof(cursor_deltas[0]);
             cursor_index++) {
          ag_compilation_session_t *analysis_session = session;
          if (fresh_session) {
            analysis_session = ag_compilation_session_create(&target);
            CHECK(analysis_session != NULL,
                  "fresh spliced identifier session");
          }
          CHECK(analyze_named(
                    analysis_session, "generic.c", generic_hover_source,
                    (size_t)(locations[location_index] -
                             generic_hover_source) +
                        cursor_deltas[cursor_index],
                    (header_bundle_t){0}, defaults, &snapshot, &error),
                "spliced identifier hover analysis");
          const ag_language_symbol_t *hover = hover_symbol(&snapshot);
          CHECK(hover && hover->kind == AG_LANGUAGE_SYMBOL_OBJECT &&
                    strcmp(hover->name,
                           spliced_identifier_names[case_index]) == 0 &&
                    strcmp(hover->declaration.source_name, "generic.c") == 0 &&
                    hover->declaration.start.offset ==
                        (int)(declaration - generic_hover_source) &&
                    hover->declaration.end.offset ==
                        (int)(declaration - generic_hover_source +
                              spelling_length) &&
                    hover->declaration.start.column == 5 &&
                    hover->declaration.end.line ==
                        hover->declaration.start.line + 1 &&
                    hover->declaration.end.column == 6 &&
                    !snapshot.partial && snapshot.diagnostic_count == 0,
                "spliced identifier hover fields");
          ag_language_analysis_snapshot_dispose(&snapshot);
          if (fresh_session)
            ag_compilation_session_destroy(analysis_session);
        }
      }
    }
  }

  const char *trigraph_disabled_source =
      "const char *text = \"?" "?=\";\n"
      "int trigraph_disabled_value;\n"
      "int main(void) { return _Generic(trigraph_disabled_value, int: 1, default: 0); }\n";
  const char *trigraph_disabled_declaration = strstr(
      trigraph_disabled_source, "trigraph_disabled_value");
  const char *trigraph_disabled_use = trigraph_disabled_declaration
                                          ? strstr(
                                                trigraph_disabled_declaration +
                                                    strlen("trigraph_disabled_value"),
                                                "trigraph_disabled_value")
                                          : NULL;
  ag_compilation_session_t *trigraph_disabled_session =
      ag_compilation_session_create(&target);
  CHECK(trigraph_disabled_session && trigraph_disabled_declaration &&
            trigraph_disabled_use,
        "trigraph-disabled analysis anchors");
  tk_ctx_set_enable_trigraphs(
      ag_compilation_session_tokenizer(trigraph_disabled_session), false);
  CHECK(analyze_named(
            trigraph_disabled_session, "trigraph-disabled.c",
            trigraph_disabled_source,
            (size_t)(trigraph_disabled_use - trigraph_disabled_source) + 3,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "trigraph-disabled analysis");
  const ag_language_symbol_t *trigraph_disabled_hover =
      hover_symbol(&snapshot);
  CHECK(trigraph_disabled_hover &&
            trigraph_disabled_hover->kind == AG_LANGUAGE_SYMBOL_OBJECT &&
            trigraph_disabled_hover->declaration.start.offset ==
                (int)(trigraph_disabled_declaration -
                      trigraph_disabled_source) &&
            trigraph_disabled_hover->declaration.start.line == 2 &&
            trigraph_disabled_hover->declaration.start.column == 5 &&
            !snapshot.partial && snapshot.diagnostic_count == 0,
        "trigraph-disabled source range remains unnormalized");
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(trigraph_disabled_session);

  const char *enum_source =
      "enum {\n"
      "  PLAYER_ZERO,\n"
      "  PLAYER_SIZE = 12,\n"
      "  PLAYER_SPEED = 2,\n"
      "  PLAYER_NEXT,\n"
      "  PLAYER_EXPR = PLAYER_SIZE + 5\n"
      "};\n"
      "int main(void) {\n"
      "  return PLAYER_ZERO + PLAYER_SIZE + PLAYER_SPEED + "
      "PLAYER_NEXT + PLAYER_EXPR;\n"
      "}\n";
  struct {
    const char *name;
    const char *value;
    int check_all_positions;
  } enum_cases[] = {
      {"PLAYER_ZERO", "0", 0},
      {"PLAYER_SIZE", "12", 1},
      {"PLAYER_SPEED", "2", 1},
      {"PLAYER_NEXT", "3", 0},
      {"PLAYER_EXPR", "17", 0},
  };
  const char *enum_use_region = strstr(enum_source, "return ");
  for (size_t case_index = 0;
       case_index < sizeof(enum_cases) / sizeof(enum_cases[0]); case_index++) {
    const char *declaration = strstr(enum_source, enum_cases[case_index].name);
    const char *use = strstr(enum_use_region, enum_cases[case_index].name);
    size_t name_length = strlen(enum_cases[case_index].name);
    ag_language_analysis_snapshot_t use_snapshot = {0};
    CHECK(analyze(
              session, enum_source, (size_t)(use - enum_source) + name_length,
              (header_bundle_t){0}, defaults, &use_snapshot, &error),
          "enum use hover");
    const ag_language_symbol_t *use_hover = hover_symbol(&use_snapshot);
    CHECK(use_hover &&
              use_hover->kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT &&
              use_hover->initializer_state ==
                  AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT &&
              strcmp(use_hover->constant_value,
                     enum_cases[case_index].value) == 0,
          "enum use hover fields");
    size_t cursor_deltas[] = {
        0, name_length / 2, name_length,
    };
    size_t cursor_count = enum_cases[case_index].check_all_positions ? 3 : 1;
    for (size_t cursor_index = 0; cursor_index < cursor_count; cursor_index++) {
      size_t cursor = (size_t)(declaration - enum_source) +
                      cursor_deltas[cursor_index];
      CHECK(analyze(
                session, enum_source, cursor, (header_bundle_t){0},
                defaults, &snapshot, &error),
            "enum declaration hover");
      const ag_language_symbol_t *declaration_hover = hover_symbol(&snapshot);
      CHECK(declaration_hover &&
                declaration_hover->kind ==
                    AG_LANGUAGE_SYMBOL_ENUM_CONSTANT &&
                strcmp(declaration_hover->name,
                       enum_cases[case_index].name) == 0 &&
                declaration_hover->initializer_state ==
                    AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT &&
                strcmp(declaration_hover->constant_value,
                       enum_cases[case_index].value) == 0 &&
                strcmp(declaration_hover->signature,
                       use_hover->signature) == 0 &&
                strcmp(declaration_hover->type, use_hover->type) == 0 &&
                same_range(
                    &declaration_hover->declaration,
                    &use_hover->declaration),
            "enum declaration and use hover parity");
      CHECK(!snapshot.partial && snapshot.diagnostic_count == 0,
            "complete enum declaration hover is not partial");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
    ag_language_analysis_snapshot_dispose(&use_snapshot);
  }
  source =
      "int local_enum_value(void) {\n"
      "  enum { LOCAL_ENUM_VALUE = 9 };\n"
      "  return LOCAL_ENUM_VALUE;\n"
      "}\n";
  const char *local_enum_declaration = strstr(source, "LOCAL_ENUM_VALUE");
  CHECK(analyze(
            session, source,
            (size_t)(local_enum_declaration - source) + 5,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "block-scope enum declaration hover");
  const ag_language_symbol_t *local_enum_hover = hover_symbol(&snapshot);
  CHECK(local_enum_hover &&
            local_enum_hover->kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT &&
            strcmp(local_enum_hover->name, "LOCAL_ENUM_VALUE") == 0 &&
            strcmp(local_enum_hover->constant_value, "9") == 0 &&
            !snapshot.partial,
        "block-scope enum recovery closes outer scope");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *object_paths[] = {"game.h"};
  const char *object_headers[] = {"int game_running(void);\n"};
  header_bundle_t object_bundle = make_bundle(
      object_paths, object_headers, 1);
  const char *object_source =
      "#include <game.h>\n"
      "static int player_x;\n"
      "\n"
      "int main(void) {\n"
      "  while (game_running()) {\n"
      "    player_x++;\n"
      "  }\n"
      "  return 0;\n"
      "}\n";
  const char *object_declaration = strstr(object_source, "player_x");
  const char *object_use = strstr(
      object_declaration + strlen("player_x"), "player_x");
  size_t object_name_length = strlen("player_x");
  size_t object_cursor_deltas[] = {
      0, 1, object_name_length / 2, object_name_length,
  };
  ag_language_analysis_snapshot_t object_use_snapshot = {0};
  CHECK(analyze(
            session, object_source,
            (size_t)(object_use - object_source) + object_name_length,
            object_bundle, defaults, &object_use_snapshot, &error),
        "object use hover baseline");
  const ag_language_symbol_t *object_use_hover =
      hover_symbol(&object_use_snapshot);
  CHECK(object_use_hover &&
            object_use_hover->kind == AG_LANGUAGE_SYMBOL_OBJECT &&
            strcmp(object_use_hover->name, "player_x") == 0 &&
            strcmp(object_use_hover->type, "int") == 0 &&
            strcmp(object_use_hover->signature,
                   "static int player_x") == 0 &&
            object_use_hover->initializer_state ==
                AG_LANGUAGE_INITIALIZER_ZERO,
        "object use hover baseline fields");
  for (size_t cursor_index = 0;
       cursor_index <
           sizeof(object_cursor_deltas) / sizeof(object_cursor_deltas[0]);
       cursor_index++) {
    size_t declaration_cursor =
        (size_t)(object_declaration - object_source) +
        object_cursor_deltas[cursor_index];
    CHECK(analyze(
              session, object_source, declaration_cursor, object_bundle,
              defaults, &snapshot, &error),
          "object declaration hover");
    CHECK(same_object_hover(
              hover_symbol(&snapshot), object_use_hover),
          "object declaration and use hover parity");
    CHECK(!snapshot.partial && snapshot.diagnostic_count == 0,
          "complete object declaration hover is not partial");
    ag_language_analysis_snapshot_dispose(&snapshot);

    size_t use_cursor =
        (size_t)(object_use - object_source) +
        object_cursor_deltas[cursor_index];
    CHECK(analyze(
              session, object_source, use_cursor, object_bundle, defaults,
              &snapshot, &error),
          "object use hover after declaration hover");
    CHECK(same_object_hover(
              hover_symbol(&snapshot), object_use_hover),
          "object declaration/use alternating analysis");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  ag_language_analysis_snapshot_dispose(&object_use_snapshot);
  free(object_bundle.bytes);

  const char *function_paths[] = {"game.h"};
  const char *function_headers[] = {""};
  header_bundle_t function_bundle = make_bundle(
      function_paths, function_headers, 1);
  const char *function_definition_source =
      "#include <game.h>\n"
      "static void move_and_draw(void) {}\n"
      "int main(void) { move_and_draw(); return 0; }\n";
  const char *function_definition = strstr(
      function_definition_source, "move_and_draw");
  const char *move_function_use = strstr(
      function_definition + strlen("move_and_draw"),
      "move_and_draw");
  size_t function_name_length = strlen("move_and_draw");
  size_t function_cursor_deltas[] = {
      0, 1, function_name_length / 2, function_name_length,
  };
  ag_language_analysis_snapshot_t function_use_snapshot = {0};
  CHECK(analyze(
            session, function_definition_source,
            (size_t)(move_function_use - function_definition_source) +
                function_name_length,
            function_bundle, defaults, &function_use_snapshot, &error),
        "function use hover baseline");
  const ag_language_symbol_t *function_use_hover =
      hover_symbol(&function_use_snapshot);
  CHECK(function_use_hover &&
            function_use_hover->kind == AG_LANGUAGE_SYMBOL_FUNCTION &&
            strcmp(function_use_hover->name, "move_and_draw") == 0 &&
            strcmp(function_use_hover->return_type, "void") == 0 &&
            strcmp(function_use_hover->storage_class, "static") == 0 &&
            function_use_hover->has_function_prototype &&
            !function_use_hover->is_variadic &&
            function_use_hover->parameter_count == 0 &&
            function_use_hover->declaration.start.offset ==
                (int)(function_definition -
                      function_definition_source) &&
            function_use_hover->has_definition &&
            function_use_hover->definition.start.offset ==
                (int)(function_definition -
                      function_definition_source),
        "function use hover baseline fields");
  for (size_t cursor_index = 0;
       cursor_index <
           sizeof(function_cursor_deltas) /
               sizeof(function_cursor_deltas[0]);
       cursor_index++) {
    size_t definition_cursor =
        (size_t)(function_definition - function_definition_source) +
        function_cursor_deltas[cursor_index];
    CHECK(analyze(
              session, function_definition_source, definition_cursor,
              function_bundle, defaults, &snapshot, &error),
          "function definition hover");
    CHECK(same_function_hover(
              hover_symbol(&snapshot), function_use_hover),
          "function definition and use hover parity");
    CHECK(!snapshot.partial && snapshot.diagnostic_count == 0,
          "complete function definition hover is not partial");
    ag_language_analysis_snapshot_dispose(&snapshot);

    size_t use_cursor =
        (size_t)(move_function_use - function_definition_source) +
        function_cursor_deltas[cursor_index];
    CHECK(analyze(
              session, function_definition_source, use_cursor,
              function_bundle, defaults, &snapshot, &error),
          "function use hover after definition hover");
    CHECK(same_function_hover(
              hover_symbol(&snapshot), function_use_hover),
          "function definition/use alternating analysis");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  ag_language_analysis_snapshot_dispose(&function_use_snapshot);

  struct {
    const char *name;
    const char *return_type;
    int parameter_count;
    int has_prototype;
    int has_definition;
  } function_declarator_cases[] = {
      {"old_sum", "int", 2, 0, 1},
      {"old_apply", "int", 2, 0, 1},
      {"old_member", "int", 1, 0, 1},
      {"old_array", "int", 1, 0, 1},
      {"first", "int", 0, 1, 0},
      {"second", "int", 0, 1, 0},
      {"third", "int", 0, 1, 0},
      {"takes_scalar", "int", 1, 1, 0},
      {"parameter_prototype", "int", 3, 1, 0},
      {"unicode_parameter", "int", 1, 1, 1},
      {"parenthesized", "int", 1, 1, 1},
      {"factory", "int (*)(int)", 0, 1, 1},
      {"seeded_factory", "int (*)(int)", 1, 1, 1},
  };
  for (size_t case_index = 0;
       case_index < sizeof(function_declarator_cases) /
                        sizeof(function_declarator_cases[0]);
       case_index++) {
    const char *name = function_declarator_cases[case_index].name;
    const char *declaration = strstr(
        function_declarator_hover_source, name);
    size_t name_length = strlen(name);
    size_t cursor_deltas[] = {0, name_length / 2, name_length};
    CHECK(declaration != NULL, "function declarator hover anchor");
    for (size_t cursor_index = 0;
         cursor_index < sizeof(cursor_deltas) / sizeof(cursor_deltas[0]);
         cursor_index++) {
      CHECK(analyze_named(
                session, "function-declarator.c",
                function_declarator_hover_source,
                (size_t)(declaration - function_declarator_hover_source) +
                    cursor_deltas[cursor_index],
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "function declarator hover analysis");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      CHECK(hover && hover->kind == AG_LANGUAGE_SYMBOL_FUNCTION &&
                strcmp(hover->name, name) == 0 &&
                strcmp(hover->return_type,
                       function_declarator_cases[case_index].return_type) ==
                    0 &&
                hover->parameter_count ==
                    function_declarator_cases[case_index].parameter_count &&
                hover->has_function_prototype ==
                    function_declarator_cases[case_index].has_prototype &&
                hover->has_definition ==
                    function_declarator_cases[case_index].has_definition &&
                hover->declaration.start.offset ==
                    (int)(declaration - function_declarator_hover_source) &&
                (!hover->has_definition ||
                 hover->definition.start.offset ==
                     (int)(declaration -
                           function_declarator_hover_source)) &&
                !snapshot.partial && snapshot.diagnostic_count == 0,
            "function declarator hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  struct {
    const char *anchor;
    const char *name;
    const char *type;
    const char *declaration_anchor;
  } function_parameter_cases[] = {
      {"increment(int value", "value", "int", "increment(int value"},
      {"old_sum(left", "left", "int", "int left, right;"},
      {"int callback(int);", "callback", "int (*)(int)",
       "int callback(int);"},
      {"register int value;", "value", "int", "register int value;"},
      {"old_member(value)", "value", "struct LocalValue", "} value;"},
      {"old_array(values)", "values", "int *", "int values[';'];"},
      {"*pointer,", "pointer", "const int *", "*pointer,"},
      {"proto_callback(int)", "proto_callback", "int (*)(int)",
       "proto_callback(int)"},
      {"unicode_parameter(int 値", "値", "int",
       "unicode_parameter(int 値"},
      {"parenthesized)(int value", "value", "int",
       "parenthesized)(int value"},
      {"(int seed))", "seed", "int", "(int seed))"},
  };
  for (size_t case_index = 0;
       case_index < sizeof(function_parameter_cases) /
                        sizeof(function_parameter_cases[0]);
       case_index++) {
    const char *anchor = strstr(
        function_declarator_hover_source,
        function_parameter_cases[case_index].anchor);
    const char *name = anchor
                           ? strstr(
                                 anchor,
                                 function_parameter_cases[case_index].name)
                           : NULL;
    const char *declaration_anchor = strstr(
        function_declarator_hover_source,
        function_parameter_cases[case_index].declaration_anchor);
    const char *declaration = declaration_anchor
                                  ? strstr(
                                        declaration_anchor,
                                        function_parameter_cases[case_index]
                                            .name)
                                  : NULL;
    size_t name_length = strlen(function_parameter_cases[case_index].name);
    size_t cursor_deltas[] = {0, name_length / 2, name_length};
    CHECK(anchor && name && declaration,
          "function parameter hover anchors");
    for (size_t cursor_index = 0;
         cursor_index < sizeof(cursor_deltas) / sizeof(cursor_deltas[0]);
         cursor_index++) {
      CHECK(analyze_named(
                session, "function-declarator.c",
                function_declarator_hover_source,
                (size_t)(name - function_declarator_hover_source) +
                    cursor_deltas[cursor_index],
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "function parameter hover analysis");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      CHECK(hover && hover->kind == AG_LANGUAGE_SYMBOL_PARAMETER &&
                strcmp(hover->name,
                       function_parameter_cases[case_index].name) == 0 &&
                strcmp(hover->type,
                       function_parameter_cases[case_index].type) == 0 &&
                strcmp(hover->signature, "") == 0 &&
                hover->scope_depth == 1 && !hover->has_definition &&
                hover->declaration.start.offset ==
                    (int)(declaration -
                          function_declarator_hover_source) &&
                hover->declaration.end.offset ==
                    (int)(declaration -
                          function_declarator_hover_source + name_length) &&
                !snapshot.partial && snapshot.diagnostic_count == 0,
            "function parameter hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  struct {
    const char *name;
    const char *type;
    int scope_depth;
  } function_parameter_tag_cases[] = {
      {"PrototypeRecord", "struct PrototypeRecord", 1},
      {"NestedPayload", "union NestedPayload", 2},
      {"PrototypeState", "enum PrototypeState", 1},
      {"NestedState", "enum NestedState", 2},
  };
  for (size_t case_index = 0;
       case_index < sizeof(function_parameter_tag_cases) /
                        sizeof(function_parameter_tag_cases[0]);
       case_index++) {
    const char *name = strstr(
        function_declarator_hover_source,
        function_parameter_tag_cases[case_index].name);
    size_t name_length = strlen(
        function_parameter_tag_cases[case_index].name);
    size_t cursor_deltas[] = {0, name_length / 2, name_length};
    CHECK(name, "function prototype-scope tag hover anchor");
    for (size_t cursor_index = 0;
         cursor_index < sizeof(cursor_deltas) / sizeof(cursor_deltas[0]);
         cursor_index++) {
      CHECK(analyze_named(
                session, "function-declarator.c",
                function_declarator_hover_source,
                (size_t)(name - function_declarator_hover_source) +
                    cursor_deltas[cursor_index],
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "function prototype-scope tag hover analysis");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      CHECK(hover && hover->kind == AG_LANGUAGE_SYMBOL_TAG &&
                hover->name_space == AG_LANGUAGE_NAMESPACE_TAG &&
                strcmp(hover->name,
                       function_parameter_tag_cases[case_index].name) == 0 &&
                strcmp(hover->type,
                       function_parameter_tag_cases[case_index].type) == 0 &&
                strcmp(hover->signature, "") == 0 &&
                strcmp(hover->storage_class, "") == 0 &&
                hover->scope_depth ==
                    function_parameter_tag_cases[case_index].scope_depth &&
                !hover->has_definition &&
                hover->declaration.start.offset ==
                    (int)(name - function_declarator_hover_source) &&
                hover->declaration.end.offset ==
                    (int)(name - function_declarator_hover_source +
                          name_length) &&
                !snapshot.partial && snapshot.diagnostic_count == 0,
            "function prototype-scope tag hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  struct {
    const char *anchor;
    const char *name;
    const char *type;
    int scope_depth;
  } aggregate_member_cases[] = {
      {"FileRecord { const int *member;", "member", "const int *", 1},
      {"unsigned bits : 3;", "bits", "unsigned int", 1},
      {"int values[4];", "values", "int [4]", 1},
      {"(*callback_member)(int);", "callback_member", "int (*)(int)", 1},
      {"FileUnion { long member;", "member", "long", 1},
      {"PrototypeRecord { int member; }", "member", "int", 2},
      {"NestedPayload { int member; }", "member", "int", 3},
      {"MacroRecord { int active_member_macro;", "active_member_macro",
       "int", 1},
      {"int future_member_macro;", "future_member_macro", "int", 1},
  };
  for (size_t case_index = 0;
       case_index < sizeof(aggregate_member_cases) /
                        sizeof(aggregate_member_cases[0]);
       case_index++) {
    const char *anchor = strstr(
        function_declarator_hover_source,
        aggregate_member_cases[case_index].anchor);
    const char *name =
        anchor ? strstr(anchor, aggregate_member_cases[case_index].name) : NULL;
    size_t name_length = strlen(aggregate_member_cases[case_index].name);
    size_t cursor_deltas[] = {0, name_length / 2, name_length};
    CHECK(name, "aggregate member hover anchor");
    for (size_t cursor_index = 0;
         cursor_index < sizeof(cursor_deltas) / sizeof(cursor_deltas[0]);
         cursor_index++) {
      CHECK(analyze_named(
                session, "function-declarator.c",
                function_declarator_hover_source,
                (size_t)(name - function_declarator_hover_source) +
                    cursor_deltas[cursor_index],
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "aggregate member hover analysis");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      CHECK(hover && hover->kind == AG_LANGUAGE_SYMBOL_MEMBER &&
                hover->name_space == AG_LANGUAGE_NAMESPACE_MEMBER &&
                strcmp(hover->name,
                       aggregate_member_cases[case_index].name) == 0 &&
                strcmp(hover->type,
                       aggregate_member_cases[case_index].type) == 0 &&
                strcmp(hover->signature, "") == 0 &&
                strcmp(hover->storage_class, "member") == 0 &&
                hover->initializer_state == AG_LANGUAGE_INITIALIZER_NONE &&
                hover->scope_depth ==
                    aggregate_member_cases[case_index].scope_depth &&
                !hover->has_definition &&
                hover->declaration.start.offset ==
                    (int)(name - function_declarator_hover_source) &&
                hover->declaration.end.offset ==
                    (int)(name - function_declarator_hover_source +
                          name_length) &&
                !snapshot.partial && snapshot.diagnostic_count == 0,
            "aggregate member hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  struct {
    const char *anchor;
    const char *name;
    const char *declaration_anchor;
    const char *type;
  } aggregate_member_use_cases[] = {
      {"return value.member;", "member", "LocalValue { int member; }", "int"},
      {"record->member", "member", "FileRecord { const int *member;",
       "const int *"},
      {"(int)value.member;", "member", "FileUnion { long member; }", "long"},
      {"return value.active_member_macro;", "active_member_macro",
       "MacroRecord { int active_member_macro;", "int"},
  };
  for (size_t case_index = 0;
       case_index < sizeof(aggregate_member_use_cases) /
                        sizeof(aggregate_member_use_cases[0]);
       case_index++) {
    const char *anchor = strstr(
        function_declarator_hover_source,
        aggregate_member_use_cases[case_index].anchor);
    const char *name = anchor
                           ? strstr(anchor,
                                    aggregate_member_use_cases[case_index].name)
                           : NULL;
    const char *declaration_anchor = strstr(
        function_declarator_hover_source,
        aggregate_member_use_cases[case_index].declaration_anchor);
    const char *declaration = declaration_anchor
                                  ? strstr(
                                        declaration_anchor,
                                        aggregate_member_use_cases[case_index].name)
                                  : NULL;
    size_t name_length = strlen(
        aggregate_member_use_cases[case_index].name);
    size_t cursor_deltas[] = {0, name_length / 2, name_length};
    CHECK(name && declaration, "aggregate member use hover anchors");
    for (size_t cursor_index = 0;
         cursor_index < sizeof(cursor_deltas) / sizeof(cursor_deltas[0]);
         cursor_index++) {
      CHECK(analyze_named(
                session, "function-declarator.c",
                function_declarator_hover_source,
                (size_t)(name - function_declarator_hover_source) +
                    cursor_deltas[cursor_index],
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "aggregate member use hover analysis");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      CHECK(hover && hover->kind == AG_LANGUAGE_SYMBOL_MEMBER &&
                hover->name_space == AG_LANGUAGE_NAMESPACE_MEMBER &&
                strcmp(hover->name,
                       aggregate_member_use_cases[case_index].name) == 0 &&
                strcmp(hover->type,
                       aggregate_member_use_cases[case_index].type) == 0 &&
                strcmp(hover->signature, "") == 0 &&
                strcmp(hover->storage_class, "member") == 0 &&
                hover->initializer_state == AG_LANGUAGE_INITIALIZER_NONE &&
                hover->scope_depth == 2 && !hover->has_definition &&
                hover->declaration.start.offset ==
                    (int)(declaration - function_declarator_hover_source) &&
                hover->declaration.end.offset ==
                    (int)(declaration - function_declarator_hover_source +
                          name_length) &&
                !snapshot.partial && snapshot.diagnostic_count == 0,
            "aggregate member use hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  struct {
    const char *anchor;
    const char *name;
    const char *declaration_anchor;
    const char *type;
  } aggregate_member_designator_cases[] = {
      {"initializer_object = { .shared = 1", "shared",
       "InitializerInt { int shared; }", "int"},
      {"initializer_array[] = { [1].shared = 2", "shared",
       "InitializerLong { long shared; }", "long"},
      {"initializer_nested = { .inner.nested = 3", "inner",
       "InitializerOuter { struct InitializerInner inner; }",
       "struct InitializerInner"},
      {".nested = 3", "nested",
       "InitializerInner { unsigned short nested; }", "unsigned short"},
      {"InitializerInt, shared)", "shared",
       "InitializerInt { int shared; }", "int"},
      {"InitializerOuter, inner.nested)", "inner",
       "InitializerOuter { struct InitializerInner inner; }",
       "struct InitializerInner"},
      {"InitializerOuter, inner.nested)", "nested",
       "InitializerInner { unsigned short nested; }", "unsigned short"},
      {"local = { .shared = 4", "shared",
       "InitializerLong { long shared; }", "long"},
      {"InitializerInt){ .shared = 5", "shared",
       "InitializerInt { int shared; }", "int"},
  };
  for (size_t case_index = 0;
       case_index < sizeof(aggregate_member_designator_cases) /
                        sizeof(aggregate_member_designator_cases[0]);
       case_index++) {
    const char *anchor = strstr(
        function_declarator_hover_source,
        aggregate_member_designator_cases[case_index].anchor);
    const char *name = anchor
                           ? strstr(
                                 anchor,
                                 aggregate_member_designator_cases[case_index]
                                     .name)
                           : NULL;
    const char *declaration_anchor = strstr(
        function_declarator_hover_source,
        aggregate_member_designator_cases[case_index].declaration_anchor);
    const char *declaration = declaration_anchor
                                  ? strstr(
                                        declaration_anchor,
                                        aggregate_member_designator_cases[
                                            case_index]
                                            .name)
                                  : NULL;
    size_t name_length = strlen(
        aggregate_member_designator_cases[case_index].name);
    size_t cursor_deltas[] = {0, name_length / 2, name_length};
    CHECK(name && declaration, "aggregate member designator hover anchors");
    for (size_t cursor_index = 0;
         cursor_index < sizeof(cursor_deltas) / sizeof(cursor_deltas[0]);
         cursor_index++) {
      CHECK(analyze_named(
                session, "function-declarator.c",
                function_declarator_hover_source,
                (size_t)(name - function_declarator_hover_source) +
                    cursor_deltas[cursor_index],
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "aggregate member designator hover analysis");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      CHECK(hover && hover->kind == AG_LANGUAGE_SYMBOL_MEMBER &&
                hover->name_space == AG_LANGUAGE_NAMESPACE_MEMBER &&
                strcmp(
                    hover->name,
                    aggregate_member_designator_cases[case_index].name) == 0 &&
                strcmp(
                    hover->type,
                    aggregate_member_designator_cases[case_index].type) == 0 &&
                strcmp(hover->signature, "") == 0 &&
                strcmp(hover->storage_class, "member") == 0 &&
                hover->initializer_state == AG_LANGUAGE_INITIALIZER_NONE &&
                !hover->has_definition &&
                hover->declaration.start.offset ==
                    (int)(declaration - function_declarator_hover_source) &&
                hover->declaration.end.offset ==
                    (int)(declaration - function_declarator_hover_source +
                          name_length) &&
                !snapshot.partial && snapshot.diagnostic_count == 0,
            "aggregate member designator hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  struct {
    const char *anchor;
    const char *declaration_anchor;
    int object_visible;
  } label_hover_cases[] = {
      {"goto /* forward */ shared_label;",
       "shared_label:\n  return value;", 1},
      {"shared_label:\n  return value;",
       "shared_label:\n  return value;", 0},
      {"int backward_label(int value) {\nshared_label:",
       "int backward_label(int value) {\nshared_label:", 0},
      {"goto /* backward */ shared_label;",
       "int backward_label(int value) {\nshared_label:", 1},
  };
  for (size_t case_index = 0;
       case_index < sizeof(label_hover_cases) /
                        sizeof(label_hover_cases[0]);
       case_index++) {
    const char *anchor = strstr(
        function_declarator_hover_source,
        label_hover_cases[case_index].anchor);
    const char *name = anchor ? strstr(anchor, "shared_label") : NULL;
    const char *declaration_anchor = strstr(
        function_declarator_hover_source,
        label_hover_cases[case_index].declaration_anchor);
    const char *declaration = declaration_anchor
                                  ? strstr(declaration_anchor, "shared_label")
                                  : NULL;
    size_t name_length = strlen("shared_label");
    size_t cursor_deltas[] = {0, name_length / 2, name_length};
    CHECK(name && declaration, "label hover anchors");
    for (size_t cursor_index = 0;
         cursor_index < sizeof(cursor_deltas) / sizeof(cursor_deltas[0]);
         cursor_index++) {
      CHECK(analyze_named(
                session, "function-declarator.c",
                function_declarator_hover_source,
                (size_t)(name - function_declarator_hover_source) +
                    cursor_deltas[cursor_index],
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "label hover analysis");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      const ag_language_symbol_t *same_named_object = find_symbol(
          &snapshot, "shared_label", AG_LANGUAGE_SYMBOL_OBJECT);
      CHECK(hover && hover->kind == AG_LANGUAGE_SYMBOL_LABEL &&
                hover->name_space == AG_LANGUAGE_NAMESPACE_LABEL &&
                strcmp(hover->name, "shared_label") == 0 &&
                strcmp(hover->type, "") == 0 &&
                strcmp(hover->signature, "shared_label:") == 0 &&
                strcmp(hover->storage_class, "") == 0 &&
                hover->initializer_state == AG_LANGUAGE_INITIALIZER_NONE &&
                hover->scope_depth == 1 && !hover->has_definition &&
                hover->declaration.start.offset ==
                    (int)(declaration - function_declarator_hover_source) &&
                hover->declaration.end.offset ==
                    (int)(declaration - function_declarator_hover_source +
                          name_length) &&
                (label_hover_cases[case_index].object_visible
                     ? same_named_object != NULL
                     : same_named_object == NULL) &&
                !snapshot.partial && snapshot.diagnostic_count == 0,
            "label hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  struct {
    const char *name;
    const char *value;
    int scope_depth;
  } function_parameter_enum_cases[] = {
      {"PROTOTYPE_READY", "3", 1},
      {"PROTOTYPE_BUSY", "4", 1},
      {"NESTED_READY", "7", 2},
  };
  for (size_t case_index = 0;
       case_index < sizeof(function_parameter_enum_cases) /
                        sizeof(function_parameter_enum_cases[0]);
       case_index++) {
    const char *name = strstr(
        function_declarator_hover_source,
        function_parameter_enum_cases[case_index].name);
    size_t name_length = strlen(
        function_parameter_enum_cases[case_index].name);
    size_t cursor_deltas[] = {0, name_length / 2, name_length};
    CHECK(name, "function prototype-scope enum hover anchor");
    for (size_t cursor_index = 0;
         cursor_index < sizeof(cursor_deltas) / sizeof(cursor_deltas[0]);
         cursor_index++) {
      CHECK(analyze_named(
                session, "function-declarator.c",
                function_declarator_hover_source,
                (size_t)(name - function_declarator_hover_source) +
                    cursor_deltas[cursor_index],
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "function prototype-scope enum hover analysis");
      const ag_language_symbol_t *hover = hover_symbol(&snapshot);
      CHECK(hover &&
                hover->kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT &&
                hover->name_space == AG_LANGUAGE_NAMESPACE_ORDINARY &&
                strcmp(hover->name,
                       function_parameter_enum_cases[case_index].name) == 0 &&
                strcmp(hover->type, "int") == 0 &&
                strcmp(hover->signature, "") == 0 &&
                strcmp(hover->storage_class, "") == 0 &&
                hover->initializer_state ==
                    AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT &&
                strcmp(hover->constant_value,
                       function_parameter_enum_cases[case_index].value) == 0 &&
                hover->scope_depth ==
                    function_parameter_enum_cases[case_index].scope_depth &&
                !hover->has_definition &&
                hover->declaration.start.offset ==
                    (int)(name - function_declarator_hover_source) &&
                hover->declaration.end.offset ==
                    (int)(name - function_declarator_hover_source +
                          name_length) &&
                !snapshot.partial && snapshot.diagnostic_count == 0,
            "function prototype-scope enum hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  const char *function_forms_source =
      "extern int declared_only(int value);\n"
      "static inline int defined_after(int value);\n"
      "static inline int defined_after(int value) { return value + 1; }\n"
      "int main(void) { return declared_only(1) + defined_after(2); }\n";
  const char *declared_only = strstr(
      function_forms_source, "declared_only");
  const char *defined_prototype = strstr(
      function_forms_source, "defined_after");
  const char *defined_definition = strstr(
      defined_prototype + strlen("defined_after"), "defined_after");
  const char *defined_use = strstr(
      defined_definition + strlen("defined_after"), "defined_after");
  CHECK(analyze(
            session, function_forms_source,
            (size_t)(declared_only - function_forms_source) + 1,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "function prototype-only hover");
  const ag_language_symbol_t *prototype_hover = hover_symbol(&snapshot);
  CHECK(prototype_hover &&
            prototype_hover->kind == AG_LANGUAGE_SYMBOL_FUNCTION &&
            strcmp(prototype_hover->name, "declared_only") == 0 &&
            strcmp(prototype_hover->return_type, "int") == 0 &&
            strcmp(prototype_hover->storage_class, "extern") == 0 &&
            prototype_hover->has_function_prototype &&
            prototype_hover->parameter_count == 1 &&
            prototype_hover->declaration.start.offset ==
                (int)(declared_only - function_forms_source) &&
            !prototype_hover->has_definition &&
            prototype_hover->definition_candidate_count == 0 &&
            !snapshot.partial && snapshot.diagnostic_count == 0,
        "function prototype-only fields");
  ag_language_analysis_snapshot_dispose(&snapshot);

  CHECK(analyze(
            session, function_forms_source,
            (size_t)(defined_prototype - function_forms_source) + 1,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "function prototype before definition hover");
  CHECK(hover_symbol(&snapshot) &&
            hover_symbol(&snapshot)->declaration.start.offset ==
                (int)(defined_prototype - function_forms_source) &&
            strcmp(hover_symbol(&snapshot)->storage_class, "static") == 0 &&
            !snapshot.partial && snapshot.diagnostic_count == 0,
        "function prototype before definition range");
  ag_language_analysis_snapshot_dispose(&snapshot);

  CHECK(analyze(
            session, function_forms_source,
            (size_t)(defined_definition - function_forms_source) + 1,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "static inline function definition hover");
  const ag_language_symbol_t *definition_hover = hover_symbol(&snapshot);
  CHECK(definition_hover &&
            definition_hover->kind == AG_LANGUAGE_SYMBOL_FUNCTION &&
            strcmp(definition_hover->name, "defined_after") == 0 &&
            strcmp(definition_hover->return_type, "int") == 0 &&
            strcmp(definition_hover->storage_class, "static") == 0 &&
            definition_hover->has_function_prototype &&
            definition_hover->parameter_count == 1 &&
            definition_hover->declaration.start.offset ==
                (int)(defined_prototype - function_forms_source) &&
            definition_hover->has_definition &&
            definition_hover->definition.start.offset ==
                (int)(defined_definition - function_forms_source) &&
            !snapshot.partial && snapshot.diagnostic_count == 0,
        "static inline function definition fields");
  ag_language_analysis_snapshot_dispose(&snapshot);

  CHECK(analyze(
            session, function_forms_source,
            (size_t)(defined_use - function_forms_source) +
                strlen("defined_after"),
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "function use after prototype and definition");
  CHECK(hover_symbol(&snapshot) &&
            hover_symbol(&snapshot)->declaration.start.offset ==
                (int)(defined_prototype - function_forms_source) &&
            hover_symbol(&snapshot)->has_definition &&
            hover_symbol(&snapshot)->definition.start.offset ==
                (int)(defined_definition - function_forms_source) &&
            strcmp(hover_symbol(&snapshot)->storage_class, "static") == 0 &&
            !snapshot.partial && snapshot.diagnostic_count == 0,
        "function use resolves to definition range");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *function_storage_source =
      "static void internal_function(void);\n"
      "static void internal_function(void) {}\n"
      "extern void external_function(void);\n"
      "void external_function(void) {}\n"
      "void default_function(void) {}\n"
      "int main(void) {\n"
      "  internal_function();\n"
      "  external_function();\n"
      "  default_function();\n"
      "  return 0;\n"
      "}\n";
  struct {
    const char *name;
    const char *storage_class;
    int occurrence_count;
  } function_storage_cases[] = {
      {"internal_function", "static", 3},
      {"external_function", "extern", 3},
      {"default_function", "", 2},
  };
  for (size_t case_index = 0;
       case_index <
           sizeof(function_storage_cases) /
               sizeof(function_storage_cases[0]);
       case_index++) {
    const char *occurrence = function_storage_source;
    for (int occurrence_index = 0;
         occurrence_index <
             function_storage_cases[case_index].occurrence_count;
         occurrence_index++) {
      occurrence = strstr(
          occurrence, function_storage_cases[case_index].name);
      CHECK(occurrence != NULL, "function storage occurrence");
      CHECK(analyze(
                session, function_storage_source,
                (size_t)(occurrence - function_storage_source) + 1,
                (header_bundle_t){0}, defaults, &snapshot, &error),
            "function storage hover");
      const ag_language_symbol_t *storage_hover =
          hover_symbol(&snapshot);
      CHECK(storage_hover &&
                storage_hover->kind == AG_LANGUAGE_SYMBOL_FUNCTION &&
                strcmp(
                    storage_hover->name,
                    function_storage_cases[case_index].name) == 0 &&
                strcmp(
                    storage_hover->storage_class,
                    function_storage_cases[case_index].storage_class) == 0 &&
                !snapshot.partial && snapshot.diagnostic_count == 0,
            "function storage hover fields");
      ag_language_analysis_snapshot_dispose(&snapshot);
      occurrence += strlen(function_storage_cases[case_index].name);
    }
  }

  source =
      "void duplicate_definition(void) {}\n"
      "void duplicate_definition(void) {}\n";
  const char *first_duplicate_definition = strstr(
      source, "duplicate_definition");
  const char *second_duplicate_definition = strstr(
      first_duplicate_definition + 1, "duplicate_definition");
  ag_compilation_session_t *duplicate_session =
      ag_compilation_session_create(&target);
  CHECK(duplicate_session != NULL, "duplicate definition session");
  CHECK(analyze(
            duplicate_session, source,
            (size_t)(second_duplicate_definition - source) + 2,
            (header_bundle_t){0}, defaults, &snapshot, &error),
        "duplicate function definition analysis");
  CHECK(hover_symbol(&snapshot) &&
            hover_symbol(&snapshot)->has_definition &&
            hover_symbol(&snapshot)->definition.start.offset ==
                (int)(first_duplicate_definition - source) &&
            hover_symbol(&snapshot)->definition_candidate_count == 1 &&
            find_diagnostic(&snapshot, "E3064"),
        "invalid duplicate does not replace the first definition range");
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_compilation_session_destroy(duplicate_session);

  const char *project_header_paths[] = {"player.h"};
  const char *project_header_sources[] = {
      "void move_and_draw(void);\n"
      "void declared_only_project(void);\n"
      "void local_only(void);\n",
  };
  header_bundle_t project_bundle = make_bundle(
      project_header_paths, project_header_sources, 1);
  const char *player_source =
      "#include \"player.h\"\n/* プレイヤー */\n"
      "void move_and_draw(void) {}\n";
  const char *main_project_source =
      "#include \"player.h\"\n\n"
      "int main(void) {\n"
      "  move_and_draw();\n"
      "  declared_only_project();\n"
      "  local_only();\n"
      "  return 0;\n"
      "}\n";
  const char *static_a_source =
      "static void local_only(void) {}\n";
  const char *static_b_source =
      "static void local_only(void) {}\n";
  ag_language_project_source_t project_sources[] = {
      {"player.c", player_source, 0},
      {"main.c", main_project_source, 0},
      {"static_a.c", static_a_source, 0},
      {"static_b.c", static_b_source, 0},
  };
  for (size_t source_index = 0;
       source_index < sizeof(project_sources) / sizeof(project_sources[0]);
       source_index++)
    project_sources[source_index].source_length =
        strlen(project_sources[source_index].source);
  ag_language_project_index_t *project =
      ag_language_project_index_create();
  CHECK(project != NULL, "project language index");
  CHECK(ag_language_project_index_update(
            session, project,
            &(ag_language_project_update_request_t){
                .revision = 1,
                .sources = project_sources,
                .source_count = 4,
                .virtual_header_bundle = project_bundle.bytes,
                .virtual_header_bundle_length = project_bundle.length,
                .max_header_files = 32,
                .max_header_file_bytes = 1024 * 1024,
                .max_header_total_bytes = 4 * 1024 * 1024,
                .max_include_depth = 16,
                .limits = defaults,
            },
            &error),
        "build project language index");
  CHECK(ag_language_project_index_revision(project) == 1,
        "project language revision");
  const char *project_declaration = strstr(
      project_header_sources[0], "move_and_draw");
  const char *project_definition = strstr(
      player_source, "move_and_draw");
  const char *project_use = strstr(
      main_project_source, "move_and_draw");
  struct {
    const char *source_name;
    const char *source;
    const char *identifier;
  } project_hover_sources[] = {
      {"main.c", main_project_source, project_use},
      {"player.h", project_header_sources[0], project_declaration},
      {"player.c", player_source, project_definition},
  };
  size_t project_cursor_deltas[] = {
      0, strlen("move_and_draw") / 2, strlen("move_and_draw"),
  };
  for (size_t source_index = 0;
       source_index < sizeof(project_hover_sources) /
                          sizeof(project_hover_sources[0]);
       source_index++) {
    for (size_t cursor_index = 0;
         cursor_index < sizeof(project_cursor_deltas) /
                            sizeof(project_cursor_deltas[0]);
         cursor_index++) {
      CHECK(analyze_project_named(
                session, project,
                project_hover_sources[source_index].source_name,
                project_hover_sources[source_index].source,
                (size_t)(project_hover_sources[source_index].identifier -
                         project_hover_sources[source_index].source) +
                    project_cursor_deltas[cursor_index],
                project_bundle, defaults, &snapshot, &error),
            "project function hover");
      const ag_language_symbol_t *project_hover =
          hover_symbol(&snapshot);
      CHECK(project_hover &&
                strcmp(project_hover->name, "move_and_draw") == 0 &&
                strcmp(project_hover->declaration.source_name,
                       "player.h") == 0 &&
                project_hover->declaration.start.offset ==
                    (int)(project_declaration -
                          project_header_sources[0]) &&
                project_hover->has_definition &&
                !project_hover->definition_conflict &&
                project_hover->definition_candidate_count == 1 &&
                strcmp(project_hover->definition.source_name,
                       "player.c") == 0 &&
                project_hover->definition.start.offset ==
                    (int)(project_definition - player_source) &&
                project_hover->definition.end.offset ==
                    (int)(project_definition - player_source) +
                        (int)strlen("move_and_draw"),
            "project function declaration and definition ranges");
      ag_language_analysis_snapshot_dispose(&snapshot);
    }
  }

  const char *declared_only_use = strstr(
      main_project_source, "declared_only_project");
  CHECK(analyze_project_named(
            session, project, "main.c", main_project_source,
            (size_t)(declared_only_use - main_project_source) + 2,
            project_bundle, defaults, &snapshot, &error),
        "project declaration-only hover");
  CHECK(hover_symbol(&snapshot) &&
            strcmp(hover_symbol(&snapshot)->name,
                   "declared_only_project") == 0 &&
            !hover_symbol(&snapshot)->has_definition &&
            !hover_symbol(&snapshot)->definition_conflict &&
            hover_symbol(&snapshot)->definition_candidate_count == 0,
        "project declaration-only has null definition");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *local_only_use = strstr(
      main_project_source, "local_only");
  CHECK(analyze_project_named(
            session, project, "main.c", main_project_source,
            (size_t)(local_only_use - main_project_source) + 2,
            project_bundle, defaults, &snapshot, &error),
        "project static isolation hover");
  CHECK(hover_symbol(&snapshot) &&
            strcmp(hover_symbol(&snapshot)->name, "local_only") == 0 &&
            !hover_symbol(&snapshot)->has_definition &&
            hover_symbol(&snapshot)->definition_candidate_count == 0,
        "project static definitions remain translation-unit local");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *moved_player_source =
      "#include \"player.h\"\n\n\n\n"
      "void move_and_draw(void) {}\n";
  project_sources[0].source = moved_player_source;
  project_sources[0].source_length = strlen(moved_player_source);
  CHECK(ag_language_project_index_update(
            session, project,
            &(ag_language_project_update_request_t){
                .revision = 2,
                .sources = project_sources,
                .source_count = 4,
                .virtual_header_bundle = project_bundle.bytes,
                .virtual_header_bundle_length = project_bundle.length,
                .limits = defaults,
            },
            &error),
        "update project language revision");
  CHECK(analyze_project_named(
            session, project, "main.c", main_project_source,
            (size_t)(project_use - main_project_source) + 2,
            project_bundle, defaults, &snapshot, &error),
        "project moved definition hover");
  CHECK(hover_symbol(&snapshot) && hover_symbol(&snapshot)->has_definition &&
            hover_symbol(&snapshot)->definition.start.offset ==
                (int)(strstr(moved_player_source, "move_and_draw") -
                      moved_player_source),
        "project revision invalidates moved definition range");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *removed_player_source =
      "#include \"player.h\"\n";
  project_sources[0].source = removed_player_source;
  project_sources[0].source_length = strlen(removed_player_source);
  CHECK(ag_language_project_index_update(
            session, project,
            &(ag_language_project_update_request_t){
                .revision = 2,
                .sources = project_sources,
                .source_count = 4,
                .virtual_header_bundle = project_bundle.bytes,
                .virtual_header_bundle_length = project_bundle.length,
                .limits = defaults,
            },
            &error),
        "reuse unchanged project revision");
  CHECK(analyze_project_named(
            session, project, "main.c", main_project_source,
            (size_t)(project_use - main_project_source) + 2,
            project_bundle, defaults, &snapshot, &error),
        "cached project definition hover");
  CHECK(hover_symbol(&snapshot) && hover_symbol(&snapshot)->has_definition &&
            hover_symbol(&snapshot)->definition.start.offset ==
                (int)(strstr(moved_player_source, "move_and_draw") -
                      moved_player_source),
        "unchanged revision reuses bounded project index");
  ag_language_analysis_snapshot_dispose(&snapshot);

  CHECK(ag_language_project_index_update(
            session, project,
            &(ag_language_project_update_request_t){
                .revision = 3,
                .sources = project_sources,
                .source_count = 4,
                .virtual_header_bundle = project_bundle.bytes,
                .virtual_header_bundle_length = project_bundle.length,
                .limits = defaults,
            },
            &error),
        "remove project definition");
  CHECK(analyze_project_named(
            session, project, "main.c", main_project_source,
            (size_t)(project_use - main_project_source) + 2,
            project_bundle, defaults, &snapshot, &error),
        "removed project definition hover");
  CHECK(hover_symbol(&snapshot) &&
            !hover_symbol(&snapshot)->has_definition &&
            hover_symbol(&snapshot)->definition_candidate_count == 0,
        "removed project definition does not leave stale range");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *duplicate_definition_a =
      "#include \"player.h\"\n"
      "void move_and_draw(void) {}\n";
  const char *duplicate_definition_b =
      "#include \"player.h\"\n\n"
      "void move_and_draw(void) {}\n";
  project_sources[0] = (ag_language_project_source_t){
      "player.c", duplicate_definition_a,
      strlen(duplicate_definition_a)};
  project_sources[3] = (ag_language_project_source_t){
      "duplicate.c", duplicate_definition_b,
      strlen(duplicate_definition_b)};
  CHECK(ag_language_project_index_update(
            session, project,
            &(ag_language_project_update_request_t){
                .revision = 4,
                .sources = project_sources,
                .source_count = 4,
                .virtual_header_bundle = project_bundle.bytes,
                .virtual_header_bundle_length = project_bundle.length,
                .limits = defaults,
            },
            &error),
        "index duplicate project definitions");
  CHECK(analyze_project_named(
            session, project, "main.c", main_project_source,
            (size_t)(project_use - main_project_source) + 2,
            project_bundle, defaults, &snapshot, &error),
        "duplicate project definition hover");
  CHECK(hover_symbol(&snapshot) &&
            !hover_symbol(&snapshot)->has_definition &&
            hover_symbol(&snapshot)->definition_conflict &&
            hover_symbol(&snapshot)->definition_candidate_count == 2,
        "duplicate project definitions return explicit candidates");
  ag_language_analysis_snapshot_dispose(&snapshot);

  ag_language_analysis_limits_t one_source_limit = defaults;
  one_source_limit.max_sources = 1;
  CHECK(!ag_language_project_index_update(
            session, project,
            &(ag_language_project_update_request_t){
                .revision = 5,
                .sources = project_sources,
                .source_count = 4,
                .limits = one_source_limit,
            },
            &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.limit, "maxSources") == 0,
        "project source count limit");
  const char *first_index_source = "void first_indexed(void) {}\n";
  const char *second_index_source = "void second_indexed(void) {}\n";
  ag_language_project_source_t symbol_limit_sources[] = {
      {"first.c", first_index_source, strlen(first_index_source)},
      {"second.c", second_index_source, strlen(second_index_source)},
  };
  ag_language_analysis_limits_t one_symbol_limit = defaults;
  one_symbol_limit.max_symbols = 1;
  CHECK(!ag_language_project_index_update(
            session, project,
            &(ag_language_project_update_request_t){
                .revision = 6,
                .sources = symbol_limit_sources,
                .source_count = 2,
                .limits = one_symbol_limit,
            },
            &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.limit, "maxAnalysisSymbols") == 0,
        "project symbol index limit");
  ag_language_project_source_t duplicate_name_sources[] = {
      {"same.c", first_index_source, strlen(first_index_source)},
      {"same.c", second_index_source, strlen(second_index_source)},
  };
  CHECK(!ag_language_project_index_update(
            session, project,
            &(ag_language_project_update_request_t){
                .revision = 7,
                .sources = duplicate_name_sources,
                .source_count = 2,
                .limits = defaults,
            },
            &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_INVALID_REQUEST &&
            strcmp(error.code,
                   "AGC_LANGUAGE_ANALYSIS_DUPLICATE_PROJECT_SOURCE") == 0,
        "duplicate project source names are rejected");
  ag_language_project_index_destroy(project);
  free(project_bundle.bytes);
  free(function_bundle.bytes);

  const char *guard_header_paths[] = {"move.h", "other.h"};
  const char *guard_header_sources[] = {
      project_guard_move_header, project_guard_other_header,
  };
  header_bundle_t guard_bundle = make_bundle(
      guard_header_paths, guard_header_sources, 2);
  ag_language_project_index_t *guard_project =
      ag_language_project_index_create();
  CHECK(guard_project != NULL, "guarded header project index");
  CHECK(update_guard_project(
            session, guard_project, 34, project_guard_move_source,
            guard_bundle, defaults, &error),
        "build guarded header project index");
  const char *guard_declaration = strstr(
      project_guard_move_header, "move_and_draw");
  const char *guard_definition = strstr(
      project_guard_move_source, "move_and_draw");
  size_t guard_cursor_deltas[] = {
      0, 1, strlen("move_and_draw") / 2, strlen("move_and_draw"),
  };
  for (size_t cursor_index = 0;
       cursor_index < sizeof(guard_cursor_deltas) /
                          sizeof(guard_cursor_deltas[0]);
       cursor_index++) {
    CHECK(analyze_project_named(
              session, guard_project, "move.h",
              project_guard_move_header,
              (size_t)(guard_declaration - project_guard_move_header) +
                  guard_cursor_deltas[cursor_index],
              guard_bundle, defaults, &snapshot, &error),
          "guarded header project hover");
    const ag_language_symbol_t *guard_hover = hover_symbol(&snapshot);
    CHECK(guard_hover &&
              strcmp(guard_hover->name, "move_and_draw") == 0 &&
              strcmp(guard_hover->declaration.source_name, "move.h") == 0 &&
              guard_hover->declaration.start.offset ==
                  (int)(guard_declaration - project_guard_move_header) &&
              guard_hover->has_definition &&
              strcmp(guard_hover->definition.source_name, "move.c") == 0 &&
              guard_hover->definition.start.offset ==
                  (int)(guard_definition - project_guard_move_source) &&
              !guard_hover->definition_conflict &&
              guard_hover->definition_candidate_count == 1 &&
              !snapshot.partial && snapshot.diagnostic_count == 0,
          "guarded header declaration and definition ranges");
    ag_language_analysis_snapshot_dispose(&snapshot);
  }

  const char *guard_main_use = strstr(
      project_guard_main_source, "move_and_draw");
  CHECK(analyze_project_named(
            session, guard_project, "main.c", project_guard_main_source,
            (size_t)(guard_main_use - project_guard_main_source) + 1,
            guard_bundle, defaults, &snapshot, &error),
        "guard macro isolation across project translation units");
  CHECK(hover_symbol(&snapshot) &&
            strcmp(hover_symbol(&snapshot)->declaration.source_name,
                   "move.h") == 0 &&
            hover_symbol(&snapshot)->has_definition,
        "project translation units retain independent guard state");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *other_declaration = strstr(
      project_guard_other_header, "other_action");
  CHECK(analyze_project_named(
            session, guard_project, "other.h", project_guard_other_header,
            (size_t)(other_declaration - project_guard_other_header) + 1,
            guard_bundle, defaults, &snapshot, &error),
        "distinct guarded header hover");
  CHECK(hover_symbol(&snapshot) &&
            strcmp(hover_symbol(&snapshot)->name, "other_action") == 0 &&
            strcmp(hover_symbol(&snapshot)->declaration.source_name,
                   "other.h") == 0 &&
            hover_symbol(&snapshot)->has_definition &&
            strcmp(hover_symbol(&snapshot)->definition.source_name,
                   "other.c") == 0 &&
            !snapshot.partial && snapshot.diagnostic_count == 0,
        "distinct guarded headers keep separate macro state");
  ag_language_analysis_snapshot_dispose(&snapshot);

  CHECK(update_guard_project(
            session, guard_project, 34,
            project_guard_moved_move_source,
            guard_bundle, defaults, &error),
        "reuse guarded project revision");
  CHECK(analyze_project_named(
            session, guard_project, "move.h", project_guard_move_header,
            (size_t)(guard_declaration - project_guard_move_header) + 1,
            guard_bundle, defaults, &snapshot, &error),
        "cached guarded header hover");
  CHECK(hover_symbol(&snapshot) &&
            hover_symbol(&snapshot)->definition.start.offset ==
                (int)(guard_definition - project_guard_move_source),
        "same revision preserves guarded project index");
  ag_language_analysis_snapshot_dispose(&snapshot);
  CHECK(update_guard_project(
            session, guard_project, 35,
            project_guard_moved_move_source,
            guard_bundle, defaults, &error),
        "rebuild guarded project revision");
  CHECK(analyze_project_named(
            session, guard_project, "move.h", project_guard_move_header,
            (size_t)(guard_declaration - project_guard_move_header) + 1,
            guard_bundle, defaults, &snapshot, &error),
        "rebuilt guarded header hover");
  CHECK(hover_symbol(&snapshot) &&
            hover_symbol(&snapshot)->definition.start.offset ==
                (int)(strstr(project_guard_moved_move_source,
                             "move_and_draw") -
                      project_guard_moved_move_source),
        "new revision rebuilds guarded project index");
  ag_language_analysis_snapshot_dispose(&snapshot);

  const char *unterminated_declaration = strstr(
      project_guard_unterminated_header, "move_and_draw");
  CHECK(analyze_project_named(
            session, guard_project, "move.h",
            project_guard_unterminated_header,
            (size_t)(unterminated_declaration -
                     project_guard_unterminated_header) + 1,
            guard_bundle, defaults, &snapshot, &error),
        "unterminated guarded header analysis");
  CHECK(snapshot.partial && find_diagnostic(&snapshot, "E1053"),
        "unterminated guarded header retains E1053");
  ag_language_analysis_snapshot_dispose(&snapshot);
  ag_language_project_index_destroy(guard_project);
  free(guard_bundle.bytes);

  struct {
    const char *label;
    const char *source;
    const char *name;
    ag_language_initializer_state_t initializer_state;
    const char *constant_value;
    const char *signature;
    int reparse_signature;
  } object_declaration_cases[] = {
      {
          "explicit file-scope object",
          "static int explicit_value = 42;\n"
          "int main(void) { return explicit_value; }\n",
          "explicit_value",
          AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT,
          "42",
          "static int explicit_value",
          0,
      },
      {
          "non-static file-scope object",
          "int global_value;\n"
          "int main(void) { return global_value; }\n",
          "global_value",
          AG_LANGUAGE_INITIALIZER_ZERO,
          "",
          "int global_value",
          0,
      },
      {
          "block-scope local object",
          "int main(void) { int local_value = 3; return local_value; }\n",
          "local_value",
          AG_LANGUAGE_INITIALIZER_RUNTIME,
          "",
          "int local_value",
          0,
      },
      {
          "first object in multi-declarator declaration",
          "int first_value = 1, second_value = 2;\n"
          "int main(void) { return first_value + second_value; }\n",
          "first_value",
          AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT,
          "1",
          "int first_value",
          0,
      },
      {
          "second object in multi-declarator declaration",
          "int first_value = 1, second_value = 2;\n"
          "int main(void) { return first_value + second_value; }\n",
          "second_value",
          AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT,
          "2",
          "int second_value",
          0,
      },
      {
          "block-scope static object",
          "int main(void) { static int local_static; return local_static; }\n",
          "local_static",
          AG_LANGUAGE_INITIALIZER_ZERO,
          "",
          "static int local_static",
          0,
      },
      {
          "block-scope extern object",
          "int main(void) { extern int local_extern; return local_extern; }\n",
          "local_extern",
          AG_LANGUAGE_INITIALIZER_NONE,
          "",
          "extern int local_extern",
          0,
      },
      {
          "block-scope register object",
          "int main(void) { register int local_register = 1; "
          "return local_register; }\n",
          "local_register",
          AG_LANGUAGE_INITIALIZER_RUNTIME,
          "",
          "register int local_register",
          0,
      },
      {
          "qualified pointer object",
          "const int *score_pointer;\n"
          "int main(void) { return score_pointer != 0; }\n",
          "score_pointer",
          AG_LANGUAGE_INITIALIZER_ZERO,
          "",
          "const int *score_pointer",
          1,
      },
      {
          "volatile object",
          "volatile int volatile_score;\n"
          "int main(void) { return volatile_score; }\n",
          "volatile_score",
          AG_LANGUAGE_INITIALIZER_ZERO,
          "",
          "volatile int volatile_score",
          0,
      },
      {
          "array object",
          "int scores[4];\n"
          "int main(void) { return scores[0]; }\n",
          "scores",
          AG_LANGUAGE_INITIALIZER_ZERO,
          "",
          "int scores[4]",
          1,
      },
      {
          "function pointer object",
          "int (*callback)(int);\n"
          "int main(void) { return callback ? callback(1) : 0; }\n",
          "callback",
          AG_LANGUAGE_INITIALIZER_ZERO,
          "",
          "int (*callback)(int)",
          1,
      },
      {
          "typedef-based object",
          "typedef int Score;\n"
          "Score typed_score;\n"
          "int main(void) { return typed_score; }\n",
          "typed_score",
          AG_LANGUAGE_INITIALIZER_ZERO,
          "",
          "int typed_score",
          1,
      },
  };
  for (size_t case_index = 0;
       case_index <
           sizeof(object_declaration_cases) /
               sizeof(object_declaration_cases[0]);
       case_index++) {
    const char *case_source = object_declaration_cases[case_index].source;
    const char *case_name = object_declaration_cases[case_index].name;
    const char *case_declaration = strstr(case_source, case_name);
    const char *case_use = strstr(
        case_declaration + strlen(case_name), case_name);
    ag_language_analysis_snapshot_t case_use_snapshot = {0};
    CHECK(analyze(
              session, case_source,
              (size_t)(case_use - case_source) + strlen(case_name),
              (header_bundle_t){0}, defaults, &case_use_snapshot, &error),
          object_declaration_cases[case_index].label);
    CHECK(analyze(
              session, case_source,
              (size_t)(case_declaration - case_source) +
                  strlen(case_name) / 2,
              (header_bundle_t){0}, defaults, &snapshot, &error),
          object_declaration_cases[case_index].label);
    const ag_language_symbol_t *case_hover = hover_symbol(&snapshot);
    CHECK(same_object_display(
              case_hover, hover_symbol(&case_use_snapshot)),
          "object declaration form matches use hover");
    CHECK(case_hover &&
              case_hover->initializer_state ==
                  object_declaration_cases[case_index].initializer_state &&
              strcmp(
                  case_hover->constant_value,
                  object_declaration_cases[case_index].constant_value) == 0 &&
              strcmp(
                  case_hover->signature,
                  object_declaration_cases[case_index].signature) == 0 &&
              !snapshot.partial && snapshot.diagnostic_count == 0,
          "object declaration form initializer and diagnostics");
    if (object_declaration_cases[case_index].reparse_signature) {
      char replay_source[256];
      int replay_length = snprintf(
          replay_source, sizeof(replay_source), "%s;\n",
          case_hover->signature);
      CHECK(replay_length > 0 &&
                (size_t)replay_length < sizeof(replay_source),
            "object signature replay source");
      const char *replay_name = strstr(replay_source, case_name);
      ag_language_analysis_snapshot_t replay_snapshot = {0};
      CHECK(replay_name &&
                analyze(
                    session, replay_source,
                    (size_t)(replay_name - replay_source) +
                        strlen(case_name) / 2,
                    (header_bundle_t){0}, defaults,
                    &replay_snapshot, &error),
            "object signature reparses as C declaration");
      const ag_language_symbol_t *replay_hover =
          hover_symbol(&replay_snapshot);
      CHECK(replay_hover &&
                replay_hover->kind == AG_LANGUAGE_SYMBOL_OBJECT &&
                strcmp(replay_hover->name, case_name) == 0 &&
                strcmp(
                    replay_hover->signature,
                    object_declaration_cases[case_index].signature) == 0 &&
                !replay_snapshot.partial &&
                replay_snapshot.diagnostic_count == 0,
            "reparsed object signature preserves declaration");
      ag_language_analysis_snapshot_dispose(&replay_snapshot);
    }
    ag_language_analysis_snapshot_dispose(&snapshot);
    ag_language_analysis_snapshot_dispose(&case_use_snapshot);
  }

  const char *analysis_game_paths[] = {"game.h"};
  const char *analysis_game_headers[] = {
      "#define GAME_SCREEN_WIDTH 640\n"
      "int game_running(void);\n"};
  header_bundle_t analysis_game = make_bundle(
      analysis_game_paths, analysis_game_headers, 1);
  struct {
    const char *label;
    const char *source;
  } empty_source_cases[] = {
      {"empty", ""},
      {"whitespace", "\n"},
      {"comment", "/* comment only */\n"},
      {"define", "#define LOCAL_VALUE 1\n"},
      {"include", "#include <game.h>\n"},
      {"include-and-declaration",
       "#include <game.h>\n\nint value;\n"},
  };
  for (size_t case_index = 0;
       case_index <
           sizeof(empty_source_cases) / sizeof(empty_source_cases[0]);
       case_index++) {
    const char *empty_source = empty_source_cases[case_index].source;
    CHECK(analyze_named(
              session, "aab/a.c", empty_source, strlen(empty_source),
              analysis_game, defaults, &snapshot, &error),
          empty_source_cases[case_index].label);
    CHECK(snapshot.diagnostic_count == 0 && !snapshot.partial,
          "declaration-free source is complete");
    if (strcmp(empty_source_cases[case_index].label, "define") == 0)
      CHECK(find_symbol(
                &snapshot, "LOCAL_VALUE", AG_LANGUAGE_SYMBOL_MACRO),
            "define-only source completion");
    if (strcmp(empty_source_cases[case_index].label, "include") == 0) {
      const ag_language_symbol_t *screen_width = find_symbol(
          &snapshot, "GAME_SCREEN_WIDTH", AG_LANGUAGE_SYMBOL_MACRO);
      CHECK(screen_width &&
                strcmp(screen_width->macro_replacement, "640") == 0 &&
                find_symbol(
                    &snapshot, "game_running",
                    AG_LANGUAGE_SYMBOL_FUNCTION),
            "include-only virtual header completion");
      CHECK(snapshot.dependency_count == 1 &&
                strcmp(snapshot.dependencies[0], "game.h") == 0,
            "include-only analysis dependencies");
    }
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  source = "value;";
  CHECK(analyze_named(
            session, "aab/a.c", source, strlen(source), analysis_game,
            defaults, &snapshot, &error),
        "real implicit-int declaration");
  const ag_language_diagnostic_t *implicit_int =
      find_diagnostic(&snapshot, "E3088");
  CHECK(implicit_int && snapshot.partial &&
            implicit_int->range.start.offset == 0 &&
            implicit_int->range.end.offset == 5,
        "real implicit-int token retains E3088");
  ag_language_analysis_snapshot_dispose(&snapshot);
  source = "int";
  CHECK(analyze_named(
            session, "aab/a.c", source, strlen(source), analysis_game,
            defaults, &snapshot, &error),
        "incomplete declaration keyword");
  const ag_language_diagnostic_t *partial_identifier =
      find_diagnostic(&snapshot, "AGC_PARTIAL_IDENTIFIER");
  CHECK(snapshot.partial && partial_identifier &&
            partial_identifier->range.start.offset == 0 &&
            partial_identifier->range.end.offset == 3,
        "incomplete declaration remains structured and partial");
  ag_language_analysis_snapshot_dispose(&snapshot);
  source = "#define INCOMPLETE_CALL() 1\n"
           "int main(void) { return INCOMPLETE_CALL(";
  const char *incomplete_macro = last_occurrence(
      source, "INCOMPLETE_CALL(");
  CHECK(incomplete_macro &&
            analyze_named(
                session, "incomplete-macro.c", source,
                (size_t)(incomplete_macro - source) +
                    strlen("INCOMPLETE_CALL") / 2,
                analysis_game, defaults, &snapshot, &error),
        "incomplete macro invocation");
  CHECK(snapshot.partial && snapshot.diagnostic_count > 0,
        "incomplete macro invocation stays partial");
  ag_language_analysis_snapshot_dispose(&snapshot);
  free(analysis_game.bytes);

  const char *stdio_paths[] = {"stdio.h"};
  const char *stdio_sources[] = {"int printf(const char *format, ...);\n"};
  header_bundle_t stdio = make_bundle(stdio_paths, stdio_sources, 1);
  source = "#include <stdio.h>\nint main(void) { pri";
  CHECK(analyze(session, source, strlen(source), stdio, defaults,
                &snapshot, &error), "stdio analysis");
  CHECK(find_symbol(&snapshot, "printf", AG_LANGUAGE_SYMBOL_FUNCTION),
        "stdio included");
  ag_language_analysis_snapshot_dispose(&snapshot);
  source = "int main(void) { pri";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "no stdio analysis");
  CHECK(!find_symbol(&snapshot, "printf", AG_LANGUAGE_SYMBOL_FUNCTION),
        "stdio not included");
  ag_language_analysis_snapshot_dispose(&snapshot);
  free(stdio.bytes);

  const char *indirect_paths[] = {
      "project.h", "string.h", "unused.h", "false.h"};
  const char *indirect_sources[] = {
      "#pragma once\n#include <string.h>\n",
      "unsigned long strlen(const char *s);\n",
      "int unused_header_symbol;\n",
      "int false_header_symbol;\n"};
  header_bundle_t indirect = make_bundle(indirect_paths, indirect_sources, 4);
  source = "#if 0\n#include <false.h>\n#endif\n"
           "#include <project.h>\n#include <project.h>\n"
           "int main(void) { str";
  CHECK(analyze(session, source, strlen(source), indirect, defaults,
                &snapshot, &error), "indirect include");
  CHECK(find_symbol(&snapshot, "strlen", AG_LANGUAGE_SYMBOL_FUNCTION),
        "indirect symbol");
  CHECK(ag_compilation_session_virtual_header_dependency_count(session) == 2 &&
            strcmp(ag_compilation_session_virtual_header_dependency_name_at(
                       session, 0),
                   "project.h") == 0 &&
            strcmp(ag_compilation_session_virtual_header_dependency_name_at(
                       session, 1),
                   "string.h") == 0,
        "native virtual header dependencies");
  ag_language_analysis_snapshot_dispose(&snapshot);
  free(indirect.bytes);

  source = "int fn(int parameter) { int local; loc";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "parameter and local");
  CHECK(find_symbol(&snapshot, "parameter", AG_LANGUAGE_SYMBOL_PARAMETER),
        "parameter kind");
  CHECK(find_symbol(&snapshot, "local", AG_LANGUAGE_SYMBOL_OBJECT),
        "local visible");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "int finished(void) { int hidden; return 0; } int global;";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "translation unit lookup");
  CHECK(find_symbol(&snapshot, "global", AG_LANGUAGE_SYMBOL_OBJECT),
        "global visible outside function");
  CHECK(!find_symbol(&snapshot, "hidden", AG_LANGUAGE_SYMBOL_OBJECT),
        "function local hidden outside function");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "int value; int fn(void) { int value; { int value; val";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "shadowing");
  const ag_language_symbol_t *value =
      find_symbol(&snapshot, "value", AG_LANGUAGE_SYMBOL_OBJECT);
  CHECK(value && value->scope_depth >= 2, "inner object shadows outer objects");
  const char *inner_value = strstr(strstr(source, "int fn"), "int value");
  inner_value = strstr(inner_value + 1, "int value") + strlen("int ");
  CHECK(value->declaration.start.offset == (int)(inner_value - source),
        "shadow resolves to inner declaration range");
  int value_count = 0;
  for (int i = 0; i < snapshot.completion_item_count; i++)
    if (strcmp(snapshot.completion_items[i].name, "value") == 0) value_count++;
  CHECK(value_count == 1, "shadowed duplicate omitted");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "typedef unsigned long Size; enum Mode { MODE_A = 4 };\n"
           "#define APPLY(x) ((x) + 1)\nint fn(void) { AP";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "symbol kinds");
  CHECK(find_symbol(&snapshot, "Size", AG_LANGUAGE_SYMBOL_TYPEDEF), "typedef");
  const ag_language_symbol_t *mode =
      find_symbol(&snapshot, "MODE_A", AG_LANGUAGE_SYMBOL_ENUM_CONSTANT);
  CHECK(mode && strcmp(mode->constant_value, "4") == 0, "enum constant value");
  const ag_language_symbol_t *apply =
      find_symbol(&snapshot, "APPLY", AG_LANGUAGE_SYMBOL_MACRO);
  CHECK(apply && apply->macro_is_function_like &&
            apply->macro_parameter_count == 1,
        "function-like macro");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "#define REMOVED 1\n#undef REMOVED\n"
           "#if 0\n#define DISABLED 2\n#else\n#define ENABLED 3\n#endif\n"
           "#undef ENABLED\n#define ENABLED 4\n"
           "int fn(void) { EN";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "active macro state");
  CHECK(!find_symbol(&snapshot, "REMOVED", AG_LANGUAGE_SYMBOL_MACRO),
        "undefined macro omitted");
  CHECK(!find_symbol(&snapshot, "DISABLED", AG_LANGUAGE_SYMBOL_MACRO),
        "inactive conditional macro omitted");
  const ag_language_symbol_t *enabled =
      find_symbol(&snapshot, "ENABLED", AG_LANGUAGE_SYMBOL_MACRO);
  const char *last_enabled = strstr(source, "#define ENABLED 4") +
                             strlen("#define ");
  CHECK(enabled && strcmp(enabled->macro_replacement, "4") == 0 &&
            enabled->declaration.start.offset ==
                (int)(last_enabled - source),
        "active redefined macro and declaration range");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "int before_error; int main(void) { bef\n"
           "this is invalid syntax after the cursor";
  size_t before_error_cursor = (size_t)(strstr(source, "bef\n") - source) + 3;
  CHECK(analyze(session, source, before_error_cursor, (header_bundle_t){0},
                defaults, &snapshot, &error), "later syntax error");
  CHECK(find_symbol(&snapshot, "before_error", AG_LANGUAGE_SYMBOL_OBJECT),
        "symbol before later syntax error retained");
  CHECK(snapshot.partial, "later syntax error request is partial at cursor");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "int before_semantic_error; int fn(void) { int local; "
           "missing_name = 1; loc";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "semantic error partial analysis");
  CHECK(snapshot.partial && snapshot.diagnostic_count > 0,
        "semantic error returns structured partial diagnostic");
  CHECK(find_symbol(&snapshot, "before_semantic_error",
                    AG_LANGUAGE_SYMBOL_OBJECT),
        "global before semantic error retained");
  CHECK(find_symbol(&snapshot, "local", AG_LANGUAGE_SYMBOL_OBJECT),
        "local before semantic error retained");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "#include <not-registered.h>\nint unreachable;";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "missing virtual header partial analysis");
  CHECK(snapshot.partial && snapshot.diagnostic_count > 0,
        "missing virtual header captured without process exit");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "static int player_x; static int answer = 42; "
           "int fn(void) { int x; int runtime = x; runtime";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "initializer states");
  const ag_language_symbol_t *player =
      find_symbol(&snapshot, "player_x", AG_LANGUAGE_SYMBOL_OBJECT);
  const ag_language_symbol_t *automatic =
      find_symbol(&snapshot, "x", AG_LANGUAGE_SYMBOL_OBJECT);
  const ag_language_symbol_t *answer =
      find_symbol(&snapshot, "answer", AG_LANGUAGE_SYMBOL_OBJECT);
  const ag_language_symbol_t *runtime =
      find_symbol(&snapshot, "runtime", AG_LANGUAGE_SYMBOL_OBJECT);
  CHECK(player && player->initializer_state == AG_LANGUAGE_INITIALIZER_ZERO,
        "static zero initialization");
  CHECK(automatic && automatic->initializer_state ==
                         AG_LANGUAGE_INITIALIZER_INDETERMINATE,
        "automatic indeterminate initialization");
  CHECK(answer && answer->initializer_state ==
                      AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT &&
            strcmp(answer->constant_value, "42") == 0 &&
            answer->has_initializer_range &&
            answer->initializer_range.start.offset ==
                (int)(strstr(source, "42") - source),
        "constant initializer value and range");
  CHECK(runtime && runtime->initializer_state == AG_LANGUAGE_INITIALIZER_RUNTIME &&
            runtime->has_initializer_range,
        "runtime initializer range");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "int sum(int left, unsigned long right, ...);\nint fn(void) { sum";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "function hover");
  const ag_language_symbol_t *sum =
      find_symbol(&snapshot, "sum", AG_LANGUAGE_SYMBOL_FUNCTION);
  CHECK(sum && sum->parameter_count == 2 && sum->is_variadic &&
            strcmp(sum->return_type, "int") == 0,
        "structured function signature");
  CHECK(snapshot.hover_index >= 0 &&
            strcmp(snapshot.completion_items[snapshot.hover_index].name,
                   "sum") == 0,
        "hover resolution");
  CHECK(strcmp(sum->parameters[0].name, "left") == 0 &&
            strcmp(sum->parameters[1].name, "right") == 0,
        "function parameter names");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "const int *pointee_const; int * const pointer_const = 0; "
           "int (*callback)(int); int (*row)[3];";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "complete declarator type display");
  const ag_language_symbol_t *pointee_const =
      find_symbol(&snapshot, "pointee_const", AG_LANGUAGE_SYMBOL_OBJECT);
  const ag_language_symbol_t *pointer_const =
      find_symbol(&snapshot, "pointer_const", AG_LANGUAGE_SYMBOL_OBJECT);
  const ag_language_symbol_t *callback =
      find_symbol(&snapshot, "callback", AG_LANGUAGE_SYMBOL_OBJECT);
  const ag_language_symbol_t *row =
      find_symbol(&snapshot, "row", AG_LANGUAGE_SYMBOL_OBJECT);
  CHECK(pointee_const && strcmp(pointee_const->type, "const int *") == 0,
        "pointee qualifier display");
  CHECK(pointer_const && strcmp(pointer_const->type, "int * const") == 0,
        "pointer qualifier display");
  CHECK(callback && strcmp(callback->type, "int (*)(int)") == 0,
        "function pointer precedence display");
  CHECK(row && strcmp(row->type, "int (*)[3]") == 0,
        "pointer to array precedence display");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "struct Player { int score; }; int fn(void) { struct Player p; p.sc";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "member completion");
  CHECK(find_symbol(&snapshot, "score", AG_LANGUAGE_SYMBOL_MEMBER),
        "member symbol");
  CHECK(find_symbol(&snapshot, "Player", AG_LANGUAGE_SYMBOL_TAG),
        "record tag symbol");
  ag_language_analysis_snapshot_dispose(&snapshot);

  source = "/* 日本語 */ int player; int fn(void) { pla";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "utf8 range");
  player = find_symbol(&snapshot, "player", AG_LANGUAGE_SYMBOL_OBJECT);
  CHECK(player && player->declaration.start.offset ==
                      (int)(strstr(source, "player") - source),
        "utf8 byte offset");
  char *saved_name = strdup(player->name);
  CHECK(saved_name != NULL, "snapshot saved name");
  ag_language_analysis_snapshot_t second = {0};
  CHECK(analyze(session, "int other;", strlen("int other;"),
                (header_bundle_t){0}, defaults, &second, &error),
        "second immutable analysis");
  CHECK(strcmp(saved_name, player->name) == 0, "first snapshot immutable");
  free(saved_name);
  ag_language_analysis_snapshot_dispose(&second);
  ag_language_analysis_snapshot_dispose(&snapshot);

  ag_language_analysis_limits_t tiny = defaults;
  tiny.max_symbols = 1;
  source = "int first; int second;";
  CHECK(!analyze(session, source, strlen(source), (header_bundle_t){0}, tiny,
                 &snapshot, &error), "symbol limit rejected");
  CHECK(error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.limit, "maxAnalysisSymbols") == 0,
        "symbol limit structure");

  tiny = defaults;
  tiny.max_snapshot_bytes = 64;
  source = "int snapshot_limit_symbol;";
  CHECK(!analyze(session, source, strlen(source), (header_bundle_t){0}, tiny,
                 &snapshot, &error), "snapshot limit rejected");
  CHECK(error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.limit, "maxAnalysisSnapshotBytes") == 0,
        "snapshot limit structure");

  tiny = defaults;
  tiny.max_source_bytes = 4;
  source = "int source_limit;";
  CHECK(!analyze(session, source, strlen(source), (header_bundle_t){0}, tiny,
                 &snapshot, &error), "source byte limit rejected");
  CHECK(error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.limit, "maxSourceBytes") == 0 &&
            error.actual == strlen(source),
        "source byte limit structure");

  const char *one_path[] = {"one.h"};
  const char *one_source[] = {"int from_header;\n"};
  header_bundle_t one_header = make_bundle(one_path, one_source, 1);
  tiny = defaults;
  tiny.max_sources = 1;
  source = "#include <one.h>\n";
  CHECK(!analyze(session, source, strlen(source), one_header, tiny,
                 &snapshot, &error), "source count limit rejected");
  CHECK(error.status == AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT &&
            strcmp(error.limit, "maxSources") == 0 && error.actual == 2,
        "source count limit structure");
  free(one_header.bytes);

  ag_language_analysis_request_t malformed = {
      .source_name = "main.c", .source = "int x;", .source_length = 6,
      .cursor_source_name = "missing.c", .cursor_byte_offset = 7,
      .limits = defaults};
  CHECK(!ag_language_analyze_source(session, &malformed, &snapshot, &error) &&
            error.status == AG_LANGUAGE_ANALYSIS_INVALID_REQUEST,
        "malformed request");

  source = "int stable(void) { return 0; }";
  CHECK(analyze(session, source, strlen(source), (header_bundle_t){0}, defaults,
                &snapshot, &error), "complete source analysis");
  int json_len = ag_language_analysis_snapshot_write_json(&snapshot, NULL, 0);
  CHECK(json_len > 0, "json size");
  char *json = malloc((size_t)json_len + 1);
  CHECK(json && ag_language_analysis_snapshot_write_json(
                    &snapshot, json, (size_t)json_len + 1) == json_len,
        "json snapshot");
  CHECK(strstr(json, "\"completionItems\"") != NULL, "json schema");
  free(json);
  ag_language_analysis_snapshot_dispose(&snapshot);

  ag_compilation_session_destroy(session);
  puts("language analysis tests passed (67 scenarios)");
  return 0;
}
