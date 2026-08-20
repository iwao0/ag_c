/* A pointer cannot hide a const qualifier on its function pointee. */
typedef int FunctionType(void);

int consume(const FunctionType *callback);

int main(void) { return 0; }
