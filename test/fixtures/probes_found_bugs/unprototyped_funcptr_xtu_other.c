// Cross-TU old-style definition paired with
// unprototyped_funcptr_xtu_main.c.

#ifndef AGC_UNPROTOTYPED_FUNCPTR_XTU_TYPES
#define AGC_UNPROTOTYPED_FUNCPTR_XTU_TYPES
struct small {
  int value;
};

struct wide {
  long long left;
  long long right;
};
#endif

int xtu_check(
    byte_value, float_value, small_value, wide_value)
int byte_value;
double float_value;
struct small small_value;
struct wide wide_value;
{
  if (byte_value != -7)
    return 1;
  if (float_value != 13.25)
    return 2;
  if (small_value.value != 23)
    return 3;
  if (wide_value.left != 29 || wide_value.right != 31)
    return 4;
  return 42;
}
