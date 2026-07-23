typedef int function_type(void);

struct callbacks {
  _Atomic function_type callback;
};

int main(void) {
  return 0;
}
