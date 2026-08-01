/* Converting __func__ to a mutable character pointer discards const. */
int main(void) {
  char *name = __func__;
  return name[0];
}
