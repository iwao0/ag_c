/* A variably modified type cannot be a generic association type. */
int reject_generic_vla(int count) {
  return _Generic(1, int[count]: 1, default: 2);
}
