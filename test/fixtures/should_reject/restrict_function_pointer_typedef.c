// A typedef does not hide the C11 6.7.3p2 constraint on the pointed-to type.
typedef int function_type(void);
typedef function_type *restrict callback_type;

int main(void) {
  callback_type callback = 0;
  return callback != 0;
}
