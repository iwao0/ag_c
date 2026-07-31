// Taking an unprototyped address before a direct call must preserve the
// wildcard metadata while the call ABI promotes float to double.

typedef int callback_t();

int transform_address_direct();

static callback_t *taken_address = transform_address_direct;

int main(void) {
  if (taken_address == 0)
    return 1;
  return transform_address_direct(1.25f);
}
