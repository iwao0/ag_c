/* A const-qualified old-style array parameter cannot be reassigned. */
int replace(values)
int values[const _Atomic 3];
{
  values = 0;
  return 0;
}
