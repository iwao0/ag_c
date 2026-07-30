typedef struct {
  int prefix;
  int payload[];
} anonymous_flexible_callback_signature_t;

typedef anonymous_flexible_callback_signature_t
anonymous_flexible_callback_t(
    anonymous_flexible_callback_signature_t value);

int invoke_anonymous_flexible_callback(
    anonymous_flexible_callback_t *callback,
    anonymous_flexible_callback_signature_t value);

static anonymous_flexible_callback_signature_t
increment_anonymous_flexible(
    anonymous_flexible_callback_signature_t value) {
  value.prefix += 25;
  return value;
}

int main(void) {
  anonymous_flexible_callback_signature_t value = {
      .prefix = 17,
  };
  return invoke_anonymous_flexible_callback(
      increment_anonymous_flexible, value);
}
