/* A typedef cannot preserve a const-qualified function type. */
typedef int FunctionType(void);
typedef const FunctionType QualifiedFunctionType;

int main(void) { return 0; }
