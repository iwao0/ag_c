/* A generic selection needs a compatible association or a default. */
int main(void) { return _Generic(1.0, int: 1); }
