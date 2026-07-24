/* signed int and unsigned int are incompatible parameter types. */
int function(int);
int function(unsigned int value) { return (int)value; }
int main(void) { return 0; }
