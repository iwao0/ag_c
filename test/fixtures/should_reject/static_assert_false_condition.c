/* A static assertion fails when its integer constant expression is zero. */
_Static_assert(0, "failure");
int main(void) { return 0; }
