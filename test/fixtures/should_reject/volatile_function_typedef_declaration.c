/* A function type itself cannot be volatile-qualified through a typedef. */
typedef int FunctionType(void);

volatile FunctionType function;

int main(void) { return 0; }
