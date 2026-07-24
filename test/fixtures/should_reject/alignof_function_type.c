/* _Alignof cannot be applied to a function type. */
_Static_assert(_Alignof(int(void)) > 0, "function alignment");
