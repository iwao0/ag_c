/* Plain char and signed char remain distinct canonical types. */
int function(char);
int function(signed char value) { return value; }
int main(void) { return 0; }
