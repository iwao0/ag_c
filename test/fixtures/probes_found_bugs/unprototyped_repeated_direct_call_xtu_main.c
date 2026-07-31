// Repeated direct calls through one unprototyped symbol may use different
// source types when their default-promoted Wasm ABI signatures are identical.
// Expected with the companion TU: exit=42.

#ifndef AGC_UNPROTOTYPED_REPEATED_DIRECT_CALL_XTU_TYPES
#define AGC_UNPROTOTYPED_REPEATED_DIRECT_CALL_XTU_TYPES
struct pair {
  int left;
  int right;
};
#endif

int check_promoted_values();

static int (*taken_address)() = check_promoted_values;

int main(void) {
  signed char first_integer = 3;
  float first_floating = 4.0f;
  struct pair first_pair = {5, 9};
  struct pair second_pair = {8, 6};
  int first;
  int second;

  first = check_promoted_values(
      first_integer, first_floating, first_pair);
  second = check_promoted_values(3, 4.0, second_pair);
  if (taken_address == 0)
    return 1;
  return first + second;
}
