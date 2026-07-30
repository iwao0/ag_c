// Cross-TU unprototyped function addresses whose prototype definitions use
// parameter categories that are unchanged by the default argument promotions.
// Expected with the companion TU: exit=42.

#ifndef AGC_UNPROTOTYPED_PARAMETER_CATEGORIES_XTU_TYPES
#define AGC_UNPROTOTYPED_PARAMETER_CATEGORIES_XTU_TYPES
struct pair {
  int left;
  int right;
};
#endif

typedef int callback_t();
typedef int binary_t(int, int);

int check_pointer();
int check_complex();
int check_aggregate();
int check_function_pointer();

static callback_t *callbacks[] = {
    check_pointer,
    check_complex,
    check_aggregate,
    check_function_pointer,
};

static int add_values(int left, int right) {
  return left + right;
}

int main(void) {
  int value = 7;
  float _Complex complex_value = {5.0f, 6.0f};
  struct pair pair_value = {9, 15};
  return callbacks[0](&value) +
         callbacks[1](complex_value) +
         callbacks[2](pair_value) +
         callbacks[3](add_values);
}
