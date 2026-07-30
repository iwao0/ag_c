// Cross-TU function signatures may use an enumeration or its compatible
// signed/unsigned integer type at the corresponding nested type position.
// Expected with the companion TU: exit=42.

enum signed_result {
  SIGNED_RESULT_NEGATIVE = -1,
  SIGNED_RESULT_VALUE = 19
};

enum unsigned_result {
  UNSIGNED_RESULT_ZERO = 0,
  UNSIGNED_RESULT_VALUE = 23
};

typedef enum signed_result signed_callback_t(void);
typedef enum unsigned_result unsigned_callback_t(void);

enum signed_result make_signed_result(void);
enum unsigned_result make_unsigned_result(void);
int accept_signed(enum signed_result value);
int accept_unsigned_pointer(enum unsigned_result *value);
int call_signed(signed_callback_t *callback);

static signed_callback_t *signed_callback = make_signed_result;
static unsigned_callback_t *unsigned_callback = make_unsigned_result;

int main(void) {
  enum unsigned_result unsigned_value = UNSIGNED_RESULT_VALUE;
  return signed_callback() +
         unsigned_callback() +
         accept_signed(SIGNED_RESULT_VALUE) +
         accept_unsigned_pointer(&unsigned_value) +
         call_signed(make_signed_result);
}
