/* A _Bool old-style parameter is promoted and conflicts with a _Bool prototype. */
int boolean_value(_Bool value);

int boolean_value(value)
_Bool value;
{
  return value;
}
