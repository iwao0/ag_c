/* A typedef cannot redeclare the predefined function-name identifier. */
int main(void) {
  typedef int __func__;
  return 0;
}
