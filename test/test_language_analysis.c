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
    "  int normal_call = cast_choose(parameter_value) + cast_object;\n"
    "  int grouped = (cast_object + cast_seed) + CAST_OPERAND_MACRO;\n"
    "  int type_size = (int)sizeof(unsigned int) + CAST_OPERAND_MACRO;\n"
    "  int type_align = (int)_Alignof(unsigned int) + CAST_OPERAND_MACRO;\n"
    "  int compound = ((struct CastRecord){ 1 }).value + "
    "CAST_OPERAND_MACRO;\n"
    "  return simple + nested + binary_rhs + argument + conditional +\n"
    "         subscript + typedef_name + (pointer != 0) +\n"
    "         (tag_pointer != 0) + enum_cast +\n"
    "         comment_gap + splice_lf + splice_crlf + nested_cast +\n"
    "         normal_call + grouped + type_size + type_align + compound;\n"
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
    CHECK(hover && hover->kind == non_cast_cases[i].kind &&
              strcmp(hover->name, non_cast_cases[i].name) == 0 &&
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
  puts("language analysis tests passed (43 scenarios)");
  return 0;
}
