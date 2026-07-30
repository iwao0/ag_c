typedef struct {
  int prefix;
  int payload[];
} anonymous_flexible_function_signature_t;

anonymous_flexible_function_signature_t
transform_anonymous_flexible(
    anonymous_flexible_function_signature_t value) {
  value.prefix += 25;
  return value;
}

int consume_anonymous_flexible(
    anonymous_flexible_function_signature_t value) {
  return value.prefix;
}
