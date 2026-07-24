/* Function redeclarations must preserve the canonical parameter width. */
int function(int);
int function(long value) { return (int)value; }
int main(void) { return 0; }
