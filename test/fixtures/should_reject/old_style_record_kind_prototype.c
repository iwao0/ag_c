/* A struct parameter conflicts with an otherwise similar union parameter. */
struct record { int value; };
union choice { int value; };

int read_value(struct record value);

int read_value(value)
union choice value;
{
  return value.value;
}
