#include "../src/language_analysis.h"
#include "../src/target_info.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  unsigned char *bytes;
  size_t length;
} header_bundle_t;

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

static int analyze(ag_compilation_session_t *session, const char *source,
                   size_t cursor, header_bundle_t bundle,
                   ag_language_analysis_limits_t limits,
                   ag_language_analysis_snapshot_t *snapshot,
                   ag_language_analysis_error_t *error) {
  return ag_language_analyze_source(
      session,
      &(ag_language_analysis_request_t){
          .source_name = "main.c",
          .source = source,
          .source_length = strlen(source),
          .cursor_source_name = "main.c",
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

#define CHECK(condition, label)                                                  \
  do {                                                                           \
    if (!(condition)) {                                                           \
      fprintf(stderr, "language analysis check failed: %s (line %d)\n", label, \
              __LINE__);                                                         \
      return 1;                                                                  \
    }                                                                            \
  } while (0)

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

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--parity-json") == 0)
    return print_parity_snapshot();
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session = ag_compilation_session_create(&target);
  CHECK(session != NULL, "session");
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  ag_language_analysis_snapshot_t snapshot = {0};
  ag_language_analysis_error_t error = {0};

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
           "#define ENABLED 4\n"
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

  source = "int before_error; int fn(void) { bef\n"
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
  puts("language analysis tests passed (28 scenarios)");
  return 0;
}
