/* The predefined function name is a const character array. */
int main(void) {
  __func__[0] = 'x';
  return 0;
}
