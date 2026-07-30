// An unspecified declaration cannot match a prototype parameter whose narrow
// integer type changes under the default argument promotions. Both sides use
// i32 in Wasm, so the linker must preserve the C-level distinction.

typedef int callback_t();

int transform_narrow();

static callback_t *callback = transform_narrow;

int main(void) {
  return callback(42);
}
