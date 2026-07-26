#include <assert.h>

int typed_enum_parameter(enum TypedEnum {
  TYPED_ENUM_VALUE = 5
} value) {
  enum TypedEnum copy = value;
  return copy + TYPED_ENUM_VALUE;
}

int typed_struct_parameter(struct TypedStruct {
  int member;
} value) {
  struct TypedStruct copy = value;
  return copy.member;
}

int typed_nested_struct_parameter(struct TypedOuter {
  struct TypedInner {
    int member;
  } inner;
} value) {
  struct TypedOuter copy = value;
  struct TypedInner inner = copy.inner;
  return inner.member;
}

int typed_forward_tag_parameter(struct HeaderOnly *value) {
  return value == 0;
}

int old_enum_parameter(value)
enum OldEnum {
  OLD_ENUM_VALUE = 7
} value;
{
  enum OldEnum {
    BODY_ENUM_VALUE = 11
  };
  return value + BODY_ENUM_VALUE;
}

int old_shared_enum_parameters(left, right)
enum SharedEnum {
  SHARED_ENUM_ZERO = 0
} left, right;
{
  return left + right;
}

int old_struct_parameters(left, right)
struct OldStruct {
  int member;
} left, right;
{
  struct OldStruct {
    int body_member;
  } local = {3};
  return left.member + right.member + local.body_member;
}

int old_referenced_struct_parameters(left, right)
struct ReferencedStruct {
  int member;
} left;
struct ReferencedStruct right;
{
  return left.member + right.member;
}

int old_matrix_value(rows, columns, values)
int rows;
int columns;
int values[rows][columns];
{
  return values[rows - 1][columns - 1];
}

int main(void) {
  int matrix[2][3] = {
      {1, 2, 3},
      {4, 5, 6},
  };
  assert(typed_enum_parameter(5) == 10);
  assert(typed_forward_tag_parameter(0) == 1);
  assert(old_enum_parameter(7) == 18);
  assert(old_shared_enum_parameters(13, 17) == 30);
  assert(old_matrix_value(2, 3, matrix) == 6);
  return 0;
}
