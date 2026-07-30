typedef struct {
  int prefix;
  int payload[];
} anonymous_flexible_function_signature_t;

anonymous_flexible_function_signature_t
transform_anonymous_flexible(
    anonymous_flexible_function_signature_t value);
int consume_anonymous_flexible(
    anonymous_flexible_function_signature_t value);

int main(void) {
  anonymous_flexible_function_signature_t value = {
      .prefix = 17,
  };
  anonymous_flexible_function_signature_t transformed =
      transform_anonymous_flexible(value);
  return consume_anonymous_flexible(transformed);
}
