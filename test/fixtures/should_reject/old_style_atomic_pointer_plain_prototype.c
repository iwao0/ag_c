/* An atomic pointer parameter conflicts with a plain pointer prototype. */
int atomic_value(int *value);

int atomic_value(value)
int * _Atomic value;
{
  return *value;
}
