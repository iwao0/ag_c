/* 'static' inside [] is limited to an outermost parameter array. */
int values[static 3];
int main(void) { return 0; }
