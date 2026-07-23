// Type-name binding must preserve `restrict` written before a typedef name.
typedef int (*function_pointer)(void);

int main(void) {
  return (restrict function_pointer)0 != 0;
}
