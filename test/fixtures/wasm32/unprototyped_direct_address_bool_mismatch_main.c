// A direct unprototyped call before address materialization must retain the
// wildcard metadata needed to reject a _Bool prototype definition.

typedef int callback_t();

int transform_direct_address();

int main(void) {
  int result = transform_direct_address(1);
  callback_t *taken_address = transform_direct_address;
  return taken_address != 0 ? result : 1;
}
