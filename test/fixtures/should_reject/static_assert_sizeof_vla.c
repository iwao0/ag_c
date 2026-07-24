/* sizeof a VLA is evaluated at run time and is not an integer constant. */
void reject_sizeof_vla_in_static_assert(int count) {
  int values[count];
  _Static_assert(sizeof(values) > 0, "VLA size is not constant");
}
