/* A variadic callback parameter conflicts with a fixed callback prototype. */
int apply(int (*callback)(int), int value);

int apply(callback, value)
int callback(int, ...);
int value;
{
  return callback(value, 1);
}
