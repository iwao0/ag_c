/* An alignment specifier cannot be applied to a function definition. */
_Alignas(16) int function(void) { return 0; }
int main(void) { return function(); }
