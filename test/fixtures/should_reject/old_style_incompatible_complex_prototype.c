/* A float complex old-style parameter conflicts with a double complex prototype. */
int has_expected_value(double _Complex value);

int has_expected_value(value)
float _Complex value;
{
  return value == 0.0f;
}
