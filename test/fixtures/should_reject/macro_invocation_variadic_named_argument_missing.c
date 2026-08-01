#define VARIADIC(first, second, ...) ((first) + (second))
int main(void) { return VARIADIC(1); }
