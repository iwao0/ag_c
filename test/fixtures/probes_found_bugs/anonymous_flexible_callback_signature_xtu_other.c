typedef struct {
  int prefix;
  int payload[];
} anonymous_flexible_callback_signature_t;

typedef anonymous_flexible_callback_signature_t
anonymous_flexible_callback_t(
    anonymous_flexible_callback_signature_t value);

int invoke_anonymous_flexible_callback(
    anonymous_flexible_callback_t *callback,
    anonymous_flexible_callback_signature_t value) {
  return callback(value).prefix;
}
