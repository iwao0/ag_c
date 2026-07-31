/* An atomic int old-style parameter conflicts with a plain int prototype. */
int atomic_value(int value);

int atomic_value(value)
_Atomic int value;
{
  return value;
}
