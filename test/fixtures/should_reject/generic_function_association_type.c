/* A function type is not an object type and cannot be an association. */
int reject_generic_function(void) {
  return _Generic(1, int(void): 1, default: 2);
}
