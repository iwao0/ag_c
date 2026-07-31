/* An unsigned-compatible enum parameter conflicts with a signed prototype. */
enum code { CODE_ZERO = 0, CODE_VALUE = 42 };

int enum_identity(int value);

int enum_identity(value)
enum code value;
{
  return value;
}
