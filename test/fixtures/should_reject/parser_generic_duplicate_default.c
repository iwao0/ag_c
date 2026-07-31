/* A generic association list can contain at most one default. */
int main(void) { return _Generic(1, default: 1, default: 2); }
