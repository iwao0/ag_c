/* inline cannot be attached to a function-pointer parameter. */
int caller(inline int (*callback)(void));

int main(void) {
  return 0;
}
