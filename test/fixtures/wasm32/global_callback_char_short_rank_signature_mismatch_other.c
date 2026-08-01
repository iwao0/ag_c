typedef signed char (*integer_rank_callback_t)(signed char);

static signed char add_five(signed char value) {
  return (signed char)(value + 5);
}

integer_rank_callback_t global_integer_rank_callback = add_five;
