/* A variably modified typedef cannot be redefined in the same scope. */
int function(int count) {
  typedef int Values[count];
  typedef int Values[count];
  return 0;
}
