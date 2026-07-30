typedef struct {
  int prefix;
  unsigned int payload[];
} anonymous_flexible_callback_member_type_t;

typedef anonymous_flexible_callback_member_type_t
anonymous_flexible_member_type_callback_t(
    anonymous_flexible_callback_member_type_t value);

int invoke_anonymous_flexible_callback_member_type(
    anonymous_flexible_member_type_callback_t *callback) {
  anonymous_flexible_callback_member_type_t value = {
      .prefix = 42,
  };
  return callback(value).prefix;
}
