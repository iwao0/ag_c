/* A signed-compatible enum parameter conflicts with an unsigned prototype. */
enum code { CODE_NEGATIVE = -1, CODE_VALUE = 41 };

int enum_identity(unsigned int value);

int enum_identity(value)
enum code value;
{
  return value;
}
