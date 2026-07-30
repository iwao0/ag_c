// Cross-TU unprototyped function address and default argument promotions.
// Expected with unprototyped_funcptr_xtu_other.c: exit=42

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

typedef int callback_t();

int xtu_check();

static callback_t *global_callback = xtu_check;

int main(void) {
  signed char byte_value = -7;
  float float_value = 13.25f;
  struct small small_value = {23};
  struct wide wide_value = {29, 31};
  return global_callback(
      byte_value, float_value, small_value, wide_value);
}
