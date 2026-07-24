/* A pointee qualifier is part of a function parameter's adjusted type. */
int function(const int *);
int function(int *value) { return value != 0; }
int main(void) { return 0; }
