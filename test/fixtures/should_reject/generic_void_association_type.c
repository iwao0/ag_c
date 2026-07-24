/* A generic association must name a complete object type. */
int reject_generic_void(void) {
  return _Generic(1, void: 1, default: 2);
}
