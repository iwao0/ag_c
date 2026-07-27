/*
 * C11 6.10.1 evaluates signed and unsigned integer preprocessing tokens as
 * intmax_t and uintmax_t.  Usual arithmetic conversions, unary operations,
 * shifts, division, and the conditional operator must retain that signedness.
 */

#if (-1 < 1U)
#error "signed -1 must convert to uintmax_t before comparison"
#endif

#if ((~0U >> 63) != 1)
#error "unsigned complement and right shift must use uintmax_t width"
#endif

#if !((1U - 2) > 0)
#error "unsigned subtraction must wrap in uintmax_t"
#endif

#if ((1U / -1) != 0)
#error "mixed signed/unsigned division must use uintmax_t"
#endif

#if ((0x8000000000000000ULL >> 63) != 1)
#error "unsigned high-bit shift must be logical"
#endif

#if ((0xffffffffffffffffULL / 3) != 6148914691236517205ULL)
#error "uintmax_t division must preserve the full unsigned range"
#endif

#if !((1 ? -1 : 0U) > 0)
#error "conditional result must use the common uintmax_t type"
#endif

#if !((0 ? 0 : -1U) > 0)
#error "selected unsigned conditional branch must remain uintmax_t"
#endif

#if !((1 ? -1 : (0U / 0)) > 0)
#error "unselected branch type must participate without evaluating division"
#endif

int main(void) {
  return 0;
}
