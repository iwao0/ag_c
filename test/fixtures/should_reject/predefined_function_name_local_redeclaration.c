/* A local object cannot redeclare the predefined function-name identifier. */
int main(void) {
  int __func__ = 3;
  return 0;
}
