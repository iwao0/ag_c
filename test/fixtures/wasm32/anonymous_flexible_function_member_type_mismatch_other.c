typedef struct {
  int prefix;
  unsigned int payload[];
} anonymous_flexible_function_member_type_t;

anonymous_flexible_function_member_type_t
transform_anonymous_flexible_member_type(
    anonymous_flexible_function_member_type_t value) {
  return value;
}
