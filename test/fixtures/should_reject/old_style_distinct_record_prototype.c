/* Distinct record types remain incompatible in an old-style definition. */
struct left_record { int value; };
struct right_record { int value; };

int read_value(struct left_record value);

int read_value(value)
struct right_record value;
{
  return value.value;
}
