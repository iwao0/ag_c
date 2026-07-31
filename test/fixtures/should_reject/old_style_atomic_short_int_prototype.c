/* Atomic short does not undergo the plain integer default promotion. */
int atomic_value(int value);

int atomic_value(value)
_Atomic short value;
{
  return value;
}
