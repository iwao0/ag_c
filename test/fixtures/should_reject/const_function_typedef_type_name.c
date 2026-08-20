/* Type-name resolution rejects a pointer to a const-qualified function. */
typedef int FunctionType(void);

int main(void) {
  return sizeof(const FunctionType *) != sizeof(FunctionType *);
}
