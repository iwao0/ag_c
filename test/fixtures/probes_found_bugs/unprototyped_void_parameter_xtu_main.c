// A cross-TU void function address may be materialized before direct and
// indirect calls establish the promoted parameter ABI.
// Expected with the companion TU: exit=42.

#ifndef AGC_UNPROTOTYPED_VOID_PARAMETER_XTU_TYPES
#define AGC_UNPROTOTYPED_VOID_PARAMETER_XTU_TYPES
struct pair {
  int left;
  int right;
};
#endif

void record_promoted_values();

extern int recorded_call_count;
extern int recorded_total;

static void (*taken_address)() = record_promoted_values;

int main(void) {
  signed char first_integer = -3;
  float first_floating = 2.5f;
  struct pair first_pair = {20, 24};
  struct pair second_pair = {10, 21};

  record_promoted_values(
      first_integer, first_floating, first_pair);
  taken_address(7, 1.5f, second_pair);

  return recorded_call_count == 2 && recorded_total == 87
             ? 42
             : 1;
}
