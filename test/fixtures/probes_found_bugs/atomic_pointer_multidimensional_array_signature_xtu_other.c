static int plain_values[2] = {20, 19};
static _Atomic(int) atomic_values[2] = {21, 22};

_Atomic(int *) atomic_pointer_matrix[1][2] = {
    {plain_values, plain_values + 1}};
_Atomic(int) *atomic_pointee_matrix[1][2] = {
    {atomic_values, atomic_values + 1}};
