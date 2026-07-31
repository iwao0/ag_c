// Paired with unprototyped_void_parameter_xtu_main.c.

#ifndef AGC_UNPROTOTYPED_VOID_PARAMETER_XTU_TYPES
#define AGC_UNPROTOTYPED_VOID_PARAMETER_XTU_TYPES
struct pair {
  int left;
  int right;
};
#endif

int recorded_call_count;
int recorded_total;

void record_promoted_values(
    int integer_value, double floating_value, struct pair pair_value) {
  recorded_call_count++;
  recorded_total += integer_value + (int)(floating_value * 2.0) +
                    pair_value.left + pair_value.right;
}
