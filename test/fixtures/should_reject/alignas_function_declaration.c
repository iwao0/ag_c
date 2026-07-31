/* An alignment specifier cannot be applied to a function declaration. */
_Alignas(16) int function(void);
int main(void) { return 0; }
