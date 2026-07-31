/* An alignment specifier cannot be used in a typedef declaration. */
typedef _Alignas(16) int aligned_int;
int main(void) { return 0; }
