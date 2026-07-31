// Paired with unprototyped_repeated_direct_call_xtu_main.c.

#ifndef AGC_UNPROTOTYPED_REPEATED_DIRECT_CALL_XTU_TYPES
#define AGC_UNPROTOTYPED_REPEATED_DIRECT_CALL_XTU_TYPES
struct pair {
  int left;
  int right;
};
#endif

int check_promoted_values(integer_value, floating_value, pair_value)
int integer_value;
double floating_value;
struct pair pair_value;
{
  return integer_value + (int)floating_value +
         pair_value.left + pair_value.right;
}
