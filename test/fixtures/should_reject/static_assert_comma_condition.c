/* A comma expression is not an integer constant expression here. */
_Static_assert((1, 1), "comma condition");
int main(void) { return 0; }
