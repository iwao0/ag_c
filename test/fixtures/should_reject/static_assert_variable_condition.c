/* A static assertion condition cannot depend on an object value. */
int value = 1;
_Static_assert(value, "variable condition");
int main(void) { return 0; }
