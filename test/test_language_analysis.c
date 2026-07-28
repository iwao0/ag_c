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
            function_use_hover->has_function_prototype &&
            !function_use_hover->is_variadic &&
            function_use_hover->parameter_count == 0 &&
            function_use_hover->declaration.start.offset ==
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
            prototype_hover->has_function_prototype &&
            prototype_hover->parameter_count == 1 &&
            prototype_hover->declaration.start.offset ==
                (int)(declared_only - function_forms_source) &&
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
            definition_hover->has_function_prototype &&
            definition_hover->parameter_count == 1 &&
            definition_hover->declaration.start.offset ==
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
                (int)(defined_definition - function_forms_source) &&
            !snapshot.partial && snapshot.diagnostic_count == 0,
        "function use resolves to definition range");
  ag_language_analysis_snapshot_dispose(&snapshot);
  free(function_bundle.bytes);

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
  puts("language analysis tests passed (32 scenarios)");
  return 0;
}
