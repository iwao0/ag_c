/*
 * Syntax preflight, Typed HIR construction, and constant evaluation must not
 * consume the host call stack while walking a valid unary expression chain.
 */
#define LOGICAL_NOT_2 ! !
#define LOGICAL_NOT_4 LOGICAL_NOT_2 LOGICAL_NOT_2
#define LOGICAL_NOT_8 LOGICAL_NOT_4 LOGICAL_NOT_4
#define LOGICAL_NOT_16 LOGICAL_NOT_8 LOGICAL_NOT_8
#define LOGICAL_NOT_32 LOGICAL_NOT_16 LOGICAL_NOT_16
#define LOGICAL_NOT_64 LOGICAL_NOT_32 LOGICAL_NOT_32
#define LOGICAL_NOT_128 LOGICAL_NOT_64 LOGICAL_NOT_64
#define LOGICAL_NOT_256 LOGICAL_NOT_128 LOGICAL_NOT_128

_Static_assert(
    LOGICAL_NOT_256 1,
    "deep unary constant expression");

int main(void) {
  return 0;
}
