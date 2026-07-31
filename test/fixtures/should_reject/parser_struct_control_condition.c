/* The controlling expression of if must have scalar type. */
struct condition { int value; };
int main(void) { struct condition condition = {1}; if (condition) return 1; return 0; }
