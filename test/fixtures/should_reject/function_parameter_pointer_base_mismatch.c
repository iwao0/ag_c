/* Pointers to different unqualified object types are incompatible. */
int function(int *);
int function(long *value) { return value != 0; }
int main(void) { return 0; }
