#include "type_display.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "semantic/type_identity.h"

typedef struct {
  char *out;
  size_t capacity;
  size_t length;
  int failed;
} type_writer_t;

typedef struct {
  char *bytes;
  size_t length;
  size_t capacity;
  int prefix_binding;
} declarator_fragment_t;

static void write_bytes(type_writer_t *writer, const char *text, size_t len) {
  if (!writer || writer->failed || !text || len > (size_t)INT_MAX ||
      writer->length > (size_t)INT_MAX - len) {
    if (writer) writer->failed = 1;
    return;
  }
  if (writer->out && writer->capacity > 0 &&
      writer->length < writer->capacity - 1) {
    size_t writable = writer->capacity - 1 - writer->length;
    if (writable > len) writable = len;
    memcpy(writer->out + writer->length, text, writable);
  }
  writer->length += len;
}

static void write_literal(type_writer_t *writer, const char *text) {
  write_bytes(writer, text, strlen(text));
}

static int fragment_reserve(declarator_fragment_t *fragment, size_t needed) {
  if (!fragment || needed > (size_t)INT_MAX) return 0;
  if (needed <= fragment->capacity) return 1;
  size_t capacity = fragment->capacity ? fragment->capacity : 32;
  while (capacity < needed) {
    if (capacity > (size_t)INT_MAX / 2) {
      capacity = needed;
      break;
    }
    capacity *= 2;
  }
  char *next = realloc(fragment->bytes, capacity);
  if (!next) return 0;
  fragment->bytes = next;
  fragment->capacity = capacity;
  return 1;
}

static int fragment_init(declarator_fragment_t *fragment) {
  if (!fragment || !fragment_reserve(fragment, 2)) return 0;
  fragment->bytes[0] = '@';
  fragment->bytes[1] = '\0';
  fragment->length = 1;
  return 1;
}

static int fragment_append_n(declarator_fragment_t *fragment,
                             const char *text, size_t length) {
  if (!fragment || !text || length > (size_t)INT_MAX ||
      fragment->length > (size_t)INT_MAX - length ||
      !fragment_reserve(fragment, fragment->length + length + 1))
    return 0;
  memcpy(fragment->bytes + fragment->length, text, length);
  fragment->length += length;
  fragment->bytes[fragment->length] = '\0';
  return 1;
}

static int fragment_append(declarator_fragment_t *fragment,
                           const char *text) {
  return fragment_append_n(fragment, text, strlen(text));
}

static int fragment_prepend(declarator_fragment_t *fragment,
                            const char *text) {
  size_t length = strlen(text);
  if (!fragment || length > (size_t)INT_MAX ||
      fragment->length > (size_t)INT_MAX - length ||
      !fragment_reserve(fragment, fragment->length + length + 1))
    return 0;
  memmove(fragment->bytes + length, fragment->bytes, fragment->length + 1);
  memcpy(fragment->bytes, text, length);
  fragment->length += length;
  return 1;
}

static int fragment_wrap_for_postfix(declarator_fragment_t *fragment) {
  if (!fragment || !fragment->prefix_binding) return 1;
  if (!fragment_prepend(fragment, "(") || !fragment_append(fragment, ")"))
    return 0;
  fragment->prefix_binding = 0;
  return 1;
}

static int fragment_append_number(declarator_fragment_t *fragment, int value) {
  char digits[16];
  int count = 0;
  unsigned int remaining = value > 0 ? (unsigned int)value : 0;
  do {
    digits[count++] = (char)('0' + remaining % 10u);
    remaining /= 10u;
  } while (remaining && count < (int)sizeof(digits));
  while (count > 0)
    if (!fragment_append_n(fragment, &digits[--count], 1)) return 0;
  return 1;
}

static int fragment_prepend_pointer_qualifiers(
    declarator_fragment_t *fragment, psx_type_qualifiers_t qualifiers) {
  char prefix[64] = "*";
  if (qualifiers & PSX_TYPE_QUALIFIER_CONST) strcat(prefix, " const");
  if (qualifiers & PSX_TYPE_QUALIFIER_VOLATILE) strcat(prefix, " volatile");
  if (qualifiers & PSX_TYPE_QUALIFIER_ATOMIC) strcat(prefix, " _Atomic");
  if (qualifiers & PSX_TYPE_QUALIFIER_RESTRICT) strcat(prefix, " restrict");
  if (qualifiers != PSX_TYPE_QUALIFIER_NONE) strcat(prefix, " ");
  if (!fragment_prepend(fragment, prefix)) return 0;
  fragment->prefix_binding = 1;
  return 1;
}

static void write_qualifiers(type_writer_t *writer,
                             psx_type_qualifiers_t qualifiers) {
  if (qualifiers & PSX_TYPE_QUALIFIER_CONST) write_literal(writer, "const ");
  if (qualifiers & PSX_TYPE_QUALIFIER_VOLATILE)
    write_literal(writer, "volatile ");
  if (qualifiers & PSX_TYPE_QUALIFIER_ATOMIC) write_literal(writer, "_Atomic ");
  if (qualifiers & PSX_TYPE_QUALIFIER_RESTRICT)
    write_literal(writer, "restrict ");
}

static void write_terminal_type(type_writer_t *writer,
                                const psx_type_shape_t *shape,
                                psx_type_qualifiers_t qualifiers) {
  write_qualifiers(writer, qualifiers);
  switch (shape->kind) {
    case PSX_TYPE_VOID: write_literal(writer, "void"); return;
    case PSX_TYPE_BOOL: write_literal(writer, "_Bool"); return;
    case PSX_TYPE_INTEGER:
      if (shape->integer_kind == PSX_INTEGER_KIND_ENUM) {
        write_literal(writer, "enum");
        if (shape->enum_tag_length > 0) {
          write_literal(writer, " ");
          write_bytes(writer, shape->enum_tag_name,
                      (size_t)shape->enum_tag_length);
        }
        return;
      }
      if (shape->is_unsigned) write_literal(writer, "unsigned ");
      switch (shape->integer_kind) {
        case PSX_INTEGER_KIND_CHAR: write_literal(writer, "char"); return;
        case PSX_INTEGER_KIND_SHORT: write_literal(writer, "short"); return;
        case PSX_INTEGER_KIND_LONG: write_literal(writer, "long"); return;
        case PSX_INTEGER_KIND_LONG_LONG:
          write_literal(writer, "long long"); return;
        default: write_literal(writer, "int"); return;
      }
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      if (shape->floating_kind == PSX_FLOATING_KIND_FLOAT)
        write_literal(writer, "float");
      else if (shape->floating_kind == PSX_FLOATING_KIND_LONG_DOUBLE)
        write_literal(writer, "long double");
      else
        write_literal(writer, "double");
      if (shape->kind == PSX_TYPE_COMPLEX) write_literal(writer, " _Complex");
      return;
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      write_literal(writer,
                    shape->kind == PSX_TYPE_STRUCT ? "struct" : "union");
      if (shape->record_tag_length > 0) {
        write_literal(writer, " ");
        write_bytes(writer, shape->record_tag_name,
                    (size_t)shape->record_tag_length);
      }
      return;
    default:
      writer->failed = 1;
      return;
  }
}

static int format_type(type_writer_t *writer,
                       const psx_semantic_type_table_t *types,
                       psx_qual_type_t type, int depth) {
  if (!writer || !types || depth > 64) return 0;
  declarator_fragment_t declarator = {0};
  if (!fragment_init(&declarator)) return 0;
  psx_qual_type_t current = type;
  psx_type_shape_t shape = {0};
  for (;;) {
    if (!psx_semantic_type_table_describe(types, current.type_id, &shape)) {
      free(declarator.bytes);
      return 0;
    }
    if (shape.kind == PSX_TYPE_POINTER) {
      if (!fragment_prepend_pointer_qualifiers(
              &declarator, current.qualifiers)) {
        free(declarator.bytes);
        return 0;
      }
      current = psx_semantic_type_table_base(types, current.type_id);
      continue;
    }
    if (shape.kind == PSX_TYPE_ARRAY) {
      if (!fragment_wrap_for_postfix(&declarator) ||
          !fragment_append(&declarator, "[") ||
          (shape.is_vla && !fragment_append(&declarator, "*")) ||
          (!shape.is_vla && shape.array_len > 0 &&
           !fragment_append_number(&declarator, shape.array_len)) ||
          !fragment_append(&declarator, "]")) {
        free(declarator.bytes);
        return 0;
      }
      current = psx_semantic_type_table_base(types, current.type_id);
      continue;
    }
    if (shape.kind == PSX_TYPE_FUNCTION) {
      if (!fragment_wrap_for_postfix(&declarator) ||
          !fragment_append(&declarator, "(")) {
        free(declarator.bytes);
        return 0;
      }
      if (shape.has_function_prototype && shape.parameter_count == 0 &&
          !shape.is_variadic_function) {
        if (!fragment_append(&declarator, "void")) {
          free(declarator.bytes);
          return 0;
        }
      } else {
        for (int i = 0; i < shape.parameter_count; i++) {
          if (i && !fragment_append(&declarator, ", ")) {
            free(declarator.bytes);
            return 0;
          }
          psx_qual_type_t parameter = psx_semantic_type_table_parameter(
              types, current.type_id, i);
          type_writer_t parameter_writer = {0};
          if (!format_type(&parameter_writer, types, parameter, depth + 1) ||
              parameter_writer.failed ||
              !fragment_reserve(
                  &declarator,
                  declarator.length + parameter_writer.length + 1)) {
            free(declarator.bytes);
            return 0;
          }
          char *parameter_text = malloc(parameter_writer.length + 1);
          if (!parameter_text) {
            free(declarator.bytes);
            return 0;
          }
          parameter_writer = (type_writer_t){
              parameter_text, parameter_writer.length + 1, 0, 0};
          int parameter_ok = format_type(
              &parameter_writer, types, parameter, depth + 1);
          parameter_text[parameter_writer.length] = '\0';
          int appended = parameter_ok && fragment_append_n(
              &declarator, parameter_text, parameter_writer.length);
          free(parameter_text);
          if (!appended) {
            free(declarator.bytes);
            return 0;
          }
        }
        if (shape.is_variadic_function) {
          if ((shape.parameter_count && !fragment_append(&declarator, ", ")) ||
              !fragment_append(&declarator, "...")) {
            free(declarator.bytes);
            return 0;
          }
        }
      }
      if (!fragment_append(&declarator, ")")) {
        free(declarator.bytes);
        return 0;
      }
      current = psx_semantic_type_table_base(types, current.type_id);
      continue;
    }
    break;
  }

  write_terminal_type(writer, &shape, current.qualifiers);
  if (!writer->failed && declarator.length > 1) {
    write_literal(writer, " ");
    for (size_t i = 0; i < declarator.length; i++) {
      if (declarator.bytes[i] == ' ' && i + 1 < declarator.length &&
          declarator.bytes[i + 1] == '@')
        continue;
      if (declarator.bytes[i] != '@')
        write_bytes(writer, declarator.bytes + i, 1);
    }
  }
  free(declarator.bytes);
  return !writer->failed;
}

int ag_format_c_type(const psx_semantic_type_table_t *types,
                     psx_qual_type_t type, char *out, size_t out_size) {
  type_writer_t writer = {out, out_size, 0, 0};
  if (type.type_id == PSX_TYPE_ID_INVALID ||
      !format_type(&writer, types, type, 0))
    writer.failed = 1;
  if (out && out_size > 0) {
    size_t end = writer.length < out_size ? writer.length : out_size - 1;
    out[end] = '\0';
  }
  return writer.failed ? -1 : (int)writer.length;
}
