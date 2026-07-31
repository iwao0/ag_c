/* _Noreturn cannot be attached to a function-pointer parameter. */
int caller(_Noreturn int (*callback)(void));

int main(void) {
  return 0;
}
