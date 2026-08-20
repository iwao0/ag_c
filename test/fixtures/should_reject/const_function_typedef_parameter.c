/* Parameter adjustment cannot hide a const-qualified function type. */
typedef int FunctionType(void);

int consume(const FunctionType callback);

int main(void) { return 0; }
