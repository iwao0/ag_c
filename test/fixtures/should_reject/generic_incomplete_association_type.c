/* An incomplete record cannot be a generic association type. */
struct incomplete_generic_record;

int reject_generic_incomplete(void) {
  return _Generic(
      1, struct incomplete_generic_record: 1, default: 2);
}
