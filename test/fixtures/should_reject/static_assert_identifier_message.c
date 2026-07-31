/* A static assertion message must be a string literal, not an identifier. */
const char message[] = "message";
_Static_assert(1, message);
int main(void) { return 0; }
