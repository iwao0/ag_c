/* A function-pointer typedef cannot be restrict-qualified by a prefix. */
typedef int (*function_pointer)(void);

int main(void) {
  restrict function_pointer callback = 0;
  return callback != 0;
}
