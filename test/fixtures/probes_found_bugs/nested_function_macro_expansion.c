/*
 * Function-like macro arguments are repeatedly prescanned while composing
 * replacement lists.  Hidesets are only consulted for identifiers, so
 * attaching them to every punctuation and numeric token wastes enough arena
 * space to make a cold Wasm self-host compile pathologically slow.
 */
#define TERNARY_1(value) (1 ? (value) : 0)
#define TERNARY_2(value) TERNARY_1(TERNARY_1(value))
#define TERNARY_4(value) TERNARY_2(TERNARY_2(value))
#define TERNARY_8(value) TERNARY_4(TERNARY_4(value))
#define TERNARY_16(value) TERNARY_8(TERNARY_8(value))
#define TERNARY_32(value) TERNARY_16(TERNARY_16(value))

_Static_assert(
    TERNARY_32(1),
    "nested function-like macro expansion");

int main(void) {
  return 0;
}
