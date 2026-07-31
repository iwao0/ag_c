/* An alignment specifier cannot be applied to a function parameter. */
int function(_Alignas(16) int value) { return value; }
int main(void) { return function(0); }
