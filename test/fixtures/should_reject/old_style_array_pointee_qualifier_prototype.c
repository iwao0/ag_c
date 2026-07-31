/* Array adjustment preserves a const element qualifier against the prototype. */
int first_value(int *values);

int first_value(values)
const int values[];
{
  return values[0];
}
