// Paired with incomplete_array_bound_signature_xtu_main.c.

extern int values[3];

int sum_row(int (*row)[3]) {
  return (*row)[0] + (*row)[1] + (*row)[2];
}

int sum_vla_row(int count, int (*row)[3]) {
  if (count != 3) return 0;
  return (*row)[0] + (*row)[1] + (*row)[2];
}

int (*get_row(void))[3] {
  return &values;
}

typedef int complete_row_callback_t(int (*)[3]);

int apply_row(complete_row_callback_t *callback, int (*row)[3]) {
  return callback(row);
}
