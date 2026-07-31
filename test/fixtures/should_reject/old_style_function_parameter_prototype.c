/* Function parameter adjustment preserves the nested callback prototype. */
int apply(int (*callback)(long), int value);

int apply(callback, value)
int callback(int);
int value;
{
  return callback(value);
}
