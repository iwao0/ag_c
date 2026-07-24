/* Block-scope _Thread_local requires static or extern (C11 6.7.1p2). */
int main(void) {
  _Thread_local int value;
  return 0;
}
