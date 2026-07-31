/* _Noreturn cannot be attached to a function-pointer member. */
struct holder {
  _Noreturn int (*callback)(void);
};

int main(void) {
  return 0;
}
