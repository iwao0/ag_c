/* Casting a floating expression does not make it an integer constant expression. */
_Static_assert((int)(1.0 + 2.0) == 3, "not immediate operands");
int main(void) { return 0; }
