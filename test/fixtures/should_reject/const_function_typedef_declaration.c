/* A function type itself cannot be const-qualified through a typedef. */
typedef int FunctionType(void);

const FunctionType function;

int main(void) { return 0; }
