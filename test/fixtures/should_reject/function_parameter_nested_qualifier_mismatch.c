/* Nested pointee qualifiers are also part of canonical type identity. */
int function(const int **);
int function(int **value) { return value != 0; }
int main(void) { return 0; }
