/* inline cannot be attached to a function-pointer member. */
struct holder {
  inline int (*callback)(void);
};

int main(void) {
  return 0;
}
