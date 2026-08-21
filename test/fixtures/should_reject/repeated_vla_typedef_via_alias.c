/* Redeclaring a VLA typedef through its own alias remains invalid. */
int function(int count) {
  typedef int Values[count];
  typedef Values Values;
  return 0;
}
