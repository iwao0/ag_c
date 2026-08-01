/* A tag introduced in an old-style parameter declaration is not visible in the body. */
int old_style(value)
struct HeaderStruct { int member; } value;
{
  struct HeaderStruct local;
  return 0;
}
