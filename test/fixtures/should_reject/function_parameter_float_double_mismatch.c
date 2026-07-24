/* Prototype parameter types are compared before default promotions. */
int function(float);
int function(double value) { return value != 0; }
int main(void) { return 0; }
