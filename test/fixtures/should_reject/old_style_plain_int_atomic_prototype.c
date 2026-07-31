/* A plain int old-style parameter conflicts with an atomic int prototype. */
int atomic_value(_Atomic int value);

int atomic_value(value)
int value;
{
  return value;
}
