// An unspecified declaration cannot match a variadic prototype definition.

typedef int callback_t();

int variadic_target();

static callback_t *callback = variadic_target;

int main(void) {
  return callback(40, 2);
}
