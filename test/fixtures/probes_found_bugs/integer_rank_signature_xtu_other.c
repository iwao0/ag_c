typedef signed char (*signed_char_rank_callback_t)(signed char);
typedef short (*short_rank_callback_t)(short);
typedef int (*int_rank_callback_t)(int);

signed char transform_signed_char_rank(signed char value) {
  return (signed char)(value + 7);
}

short transform_short_rank(short value) {
  return (short)(value + 1234);
}

int transform_int_rank(int value) {
  return value + 123456;
}

signed_char_rank_callback_t global_signed_char_rank_callback =
    transform_signed_char_rank;
short_rank_callback_t global_short_rank_callback = transform_short_rank;
int_rank_callback_t global_int_rank_callback = transform_int_rank;
