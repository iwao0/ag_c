// Decimal rounding must not lose the last digit when a rounded binary
// floating-point value lies just below its intended decimal representation.
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  char fixed[16];
  int fixed_length =
      snprintf(fixed, sizeof(fixed), "%06.1f", -2.34);
  assert(fixed_length == 6);
  assert(strcmp(fixed, "-002.3") == 0);

  char scientific[16];
  int scientific_length =
      snprintf(scientific, sizeof(scientific), "%+.1e", 12.0);
  assert(scientific_length == 8);
  assert(strcmp(scientific, "+1.2e+01") == 0);

  char hexadecimal[16];
  int hexadecimal_length =
      snprintf(hexadecimal, sizeof(hexadecimal), "%08.0a", 1.0);
  assert(hexadecimal_length == 8);
  assert(strcmp(hexadecimal, "0x001p+0") == 0);
  return 0;
}
