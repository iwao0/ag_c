#include "type_signature.h"

#include <limits.h>
#include <string.h>

#include "target_info.h"

typedef struct {
  char *out;
  size_t capacity;
  size_t length;
  int failed;
} signature_writer_t;

typedef struct signature_path_t signature_path_t;
struct signature_path_t {
  psx_type_id_t type_id;
  const signature_path_t *parent;
};

static int path_contains(
    const signature_path_t *path, psx_type_id_t type_id) {
  for (; path; path = path->parent) {
    if (path->type_id == type_id) return 1;
  }
  return 0;
}

static void write_bytes(
    signature_writer_t *writer, const char *bytes, size_t length) {
  if (!writer || writer->failed || !bytes) return;
  if (length > (size_t)INT_MAX ||
      writer->length > (size_t)INT_MAX - length) {
    writer->failed = 1;
    return;
  }
  if (writer->out && writer->capacity > 0 &&
      writer->length < writer->capacity - 1) {
    size_t writable = writer->capacity - 1 - writer->length;
    if (writable > length) writable = length;
    memcpy(writer->out + writer->length, bytes, writable);
  }
  writer->length += length;
}

static void write_literal(signature_writer_t *writer, const char *text) {
  write_bytes(writer, text, strlen(text));
}

static void write_unsigned(
    signature_writer_t *writer, unsigned int value) {
  char digits[16];
  int count = 0;
  do {
    digits[count++] = (char)('0' + value % 10u);
    value /= 10u;
  } while (value != 0 && count < (int)sizeof(digits));
  while (count > 0) write_bytes(writer, &digits[--count], 1);
}

static int integer_rank(const psx_type_shape_t *shape) {
  if (!shape || shape->kind != PSX_TYPE_INTEGER) return 0;
  if (shape->is_plain_char) return 1;
  switch (shape->integer_kind) {
    case PSX_INTEGER_KIND_CHAR: return 1;
    case PSX_INTEGER_KIND_SHORT: return 2;
    case PSX_INTEGER_KIND_INT:
    case PSX_INTEGER_KIND_ENUM: return 3;
    case PSX_INTEGER_KIND_LONG: return 4;
    case PSX_INTEGER_KIND_LONG_LONG: return 5;
    default: return 0;
  }
}

static ag_target_scalar_kind_t integer_target_kind(int rank) {
  if (rank >= 5) return AG_TARGET_SCALAR_LONG_LONG;
  if (rank == 4) return AG_TARGET_SCALAR_LONG;
  if (rank == 2) return AG_TARGET_SCALAR_SHORT;
  if (rank == 1) return AG_TARGET_SCALAR_CHAR;
  return AG_TARGET_SCALAR_INT;
}

static ag_target_scalar_kind_t floating_target_kind(
    psx_floating_kind_t kind, int is_complex) {
  if (kind == PSX_FLOATING_KIND_LONG_DOUBLE)
    return is_complex ? AG_TARGET_SCALAR_LONG_DOUBLE_COMPLEX
                      : AG_TARGET_SCALAR_LONG_DOUBLE;
  if (kind == PSX_FLOATING_KIND_FLOAT)
    return is_complex ? AG_TARGET_SCALAR_FLOAT_COMPLEX
                      : AG_TARGET_SCALAR_FLOAT;
  return is_complex ? AG_TARGET_SCALAR_DOUBLE_COMPLEX
                    : AG_TARGET_SCALAR_DOUBLE;
}

static void write_type(signature_writer_t *writer,
                       const psx_semantic_type_table_t *types,
                       psx_qual_type_t type, const signature_path_t *path,
                       const ag_data_layout_t *data_layout) {
  psx_type_shape_t shape = {0};
  if (type.type_id == PSX_TYPE_ID_INVALID ||
      path_contains(path, type.type_id) ||
      !psx_semantic_type_table_describe(types, type.type_id, &shape)) {
    writer->failed = 1;
    return;
  }
  signature_path_t current = {type.type_id, path};
  if (type.qualifiers & PSX_TYPE_QUALIFIER_CONST)
    write_literal(writer, "k");
  if (type.qualifiers & PSX_TYPE_QUALIFIER_VOLATILE)
    write_literal(writer, "V");
  if (type.qualifiers & PSX_TYPE_QUALIFIER_ATOMIC)
    write_literal(writer, "A");

  switch (shape.kind) {
    case PSX_TYPE_VOID:
      write_literal(writer, "v");
      return;
    case PSX_TYPE_BOOL:
      write_literal(writer, "b");
      return;
    case PSX_TYPE_INTEGER: {
      int rank = integer_rank(&shape);
      if (rank <= 0) rank = 3;
      unsigned int bits =
          (unsigned int)(ag_data_layout_scalar_size(data_layout,
                                                    integer_target_kind(rank)) *
                         8);
      if (shape.integer_kind == PSX_INTEGER_KIND_ENUM) {
        write_literal(writer, "e{");
        write_unsigned(writer, (unsigned int)(
            shape.enum_tag_length > 0 ? shape.enum_tag_length : 0));
        write_literal(writer, ":");
        if (shape.enum_tag_length > 0)
          write_bytes(writer, shape.enum_tag_name,
                      (size_t)shape.enum_tag_length);
        write_literal(writer, "}");
      } else if (shape.is_plain_char) {
        write_literal(writer, "c");
        write_unsigned(writer, bits);
      } else if (shape.integer_kind == PSX_INTEGER_KIND_LONG_LONG) {
        write_literal(writer, shape.is_unsigned ? "ull" : "ll");
        write_unsigned(writer, bits);
      } else if (shape.integer_kind == PSX_INTEGER_KIND_LONG) {
        write_literal(writer, shape.is_unsigned ? "ul" : "l");
        write_unsigned(writer, bits);
      } else {
        write_literal(writer, shape.is_unsigned ? "u" : "i");
        write_unsigned(writer, bits);
      }
      return;
    }
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      write_literal(writer, shape.kind == PSX_TYPE_FLOAT ? "f" : "x");
      write_unsigned(
          writer,
          (unsigned int)(ag_data_layout_scalar_size(
                             data_layout, floating_target_kind(
                                              shape.floating_kind,
                                              shape.kind == PSX_TYPE_COMPLEX)) *
                         8));
      return;
    case PSX_TYPE_POINTER:
    case PSX_TYPE_ARRAY: {
      psx_qual_type_t base = psx_semantic_type_table_base(
          types, type.type_id);
      if (shape.kind == PSX_TYPE_POINTER) {
        write_literal(writer, "p<");
      } else {
        write_literal(writer, "a");
        write_unsigned(writer, (unsigned int)(
            shape.array_len > 0 ? shape.array_len : 0));
        write_literal(writer, "<");
      }
      write_type(writer, types, base, &current, data_layout);
      write_literal(writer, ">");
      return;
    }
    case PSX_TYPE_FUNCTION: {
      if (shape.parameter_count < 0) {
        writer->failed = 1;
        return;
      }
      write_type(writer, types,
                 psx_semantic_type_table_base(types, type.type_id), &current,
                 data_layout);
      write_literal(writer, "(");
      for (int i = 0; i < shape.parameter_count; i++) {
        if (i > 0) write_literal(writer, ",");
        write_type(writer, types,
                   psx_semantic_type_table_parameter(types, type.type_id, i),
                   &current, data_layout);
      }
      if (shape.is_variadic_function) {
        if (shape.parameter_count > 0) write_literal(writer, ",");
        write_literal(writer, "...");
      }
      write_literal(writer, ")");
      return;
    }
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      write_literal(writer, shape.kind == PSX_TYPE_STRUCT ? "s{" : "u{");
      write_unsigned(writer, (unsigned int)(
          shape.record_tag_length > 0 ? shape.record_tag_length : 0));
      write_literal(writer, ":");
      if (shape.record_tag_length > 0)
        write_bytes(writer, shape.record_tag_name,
                    (size_t)shape.record_tag_length);
      write_literal(writer, "}");
      return;
    default:
      writer->failed = 1;
      return;
  }
}

int psx_format_canonical_type_signature(const psx_semantic_type_table_t *types,
                                        psx_qual_type_t type,
                                        const ag_data_layout_t *data_layout,
                                        char *out, size_t out_size) {
  signature_writer_t writer = {out, out_size, 0, 0};
  if (!types || !ag_data_layout_is_valid(data_layout))
    writer.failed = 1;
  if (!writer.failed)
    write_type(&writer, types, type, NULL, data_layout);
  if (out && out_size > 0) {
    size_t terminator = writer.length < out_size
                            ? writer.length : out_size - 1;
    out[terminator] = '\0';
  }
  if (writer.failed || writer.length > (size_t)INT_MAX) return -1;
  return (int)writer.length;
}
