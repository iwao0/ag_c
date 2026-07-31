/* A float old-style parameter is promoted and conflicts with a float prototype. */
double old_style(float);

double old_style(value)
float value;
{
  return value;
}
