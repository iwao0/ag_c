// Paired with unprototyped_parameter_categories_xtu_main.c.

#ifndef AGC_UNPROTOTYPED_PARAMETER_CATEGORIES_XTU_TYPES
#define AGC_UNPROTOTYPED_PARAMETER_CATEGORIES_XTU_TYPES
struct pair {
  int left;
  int right;
};
#endif

typedef int binary_t(int, int);

int check_pointer(int *value) {
  return *value;
}

int check_complex(float _Complex value) {
  return (int)(__real__ value + __imag__ value);
}

int check_aggregate(struct pair value) {
  return value.left + value.right;
}

int check_function_pointer(binary_t *callback) {
  return callback(-20, 20);
}
