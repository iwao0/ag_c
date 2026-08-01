/* An enumerator introduced in an old-style parameter declaration is not visible in the body. */
int old_style(value)
enum HeaderEnum { HEADER_ENUM_VALUE = 1 } value;
{
  return HEADER_ENUM_VALUE;
}
