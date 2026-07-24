/* _Alignof requires an object type and cannot be applied to void. */
_Static_assert(_Alignof(void) > 0, "void alignment");
