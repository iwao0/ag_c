/* A universal character name cannot exceed U+10FFFF. */
int main(void) { return "\U00110000"[0]; }
