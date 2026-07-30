typedef struct {
  int prefix;
  int payload[];
} anonymous_flexible_callback_member_type_t;

typedef anonymous_flexible_callback_member_type_t
anonymous_flexible_member_type_callback_t(
    anonymous_flexible_callback_member_type_t value);

int invoke_anonymous_flexible_callback_member_type(
    anonymous_flexible_member_type_callback_t *callback);

static anonymous_flexible_callback_member_type_t
identity_anonymous_flexible_member_type(
    anonymous_flexible_callback_member_type_t value) {
  return value;
}

int main(void) {
  return invoke_anonymous_flexible_callback_member_type(
      identity_anonymous_flexible_member_type);
}
