/* A universal character name cannot designate a surrogate code point. */
int main(void) { return "\U0000D800"[0]; }
