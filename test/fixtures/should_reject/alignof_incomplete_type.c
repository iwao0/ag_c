/* _Alignof cannot be applied to an incomplete object type. */
struct incomplete;
_Static_assert(_Alignof(struct incomplete) > 0,
               "incomplete alignment");
