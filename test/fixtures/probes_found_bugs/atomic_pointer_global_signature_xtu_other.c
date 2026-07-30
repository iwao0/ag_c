static int plain_value = 20;
static _Atomic(int) atomic_value = 22;

_Atomic(int *) shared_atomic_pointer = &plain_value;
_Atomic(int) *shared_atomic_pointee_pointer = &atomic_value;
