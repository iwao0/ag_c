/* C11 forbids this low control code point in a universal character name. */
int main(void) { return "\U0000001F"[0]; }
