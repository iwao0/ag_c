/* An old-style parameter declaration may use only register storage. */
int old_style(value)
static int value;
{
  return value;
}
