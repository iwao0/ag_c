/* A parameter cannot use the predefined function-name identifier. */
int function(int __func__) {
  return 0;
}

int main(void) {
  return function(3);
}
