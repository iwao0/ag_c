// Paired with array_bound_signature_mismatch_main.c.

int mismatched_array_bound(int (*row)[3]) {
  return (*row)[0];
}
