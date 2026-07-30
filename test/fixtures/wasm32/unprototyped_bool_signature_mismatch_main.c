// An unspecified declaration cannot match a _Bool prototype parameter.
// Default argument promotions pass int while both Wasm signatures use i32.

typedef int callback_t();

int transform_bool();

static callback_t *callback = transform_bool;

int main(void) {
  return callback(1);
}
