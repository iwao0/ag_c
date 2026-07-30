// An unspecified declaration cannot match a prototype parameter that changes
// under the default argument promotions.

typedef int callback_t();

int transform_value();

static callback_t *callback = transform_value;

int main(void) {
  return callback(1.25f);
}
