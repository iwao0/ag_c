typedef struct {
  int prefix;
  int payload[];
} anonymous_flexible_function_member_type_t;

anonymous_flexible_function_member_type_t
transform_anonymous_flexible_member_type(
    anonymous_flexible_function_member_type_t value);

int main(void) {
  anonymous_flexible_function_member_type_t value = {
      .prefix = 42,
  };
  return transform_anonymous_flexible_member_type(value).prefix;
}
