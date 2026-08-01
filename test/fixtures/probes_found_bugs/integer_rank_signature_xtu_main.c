// Canonical cross-TU signatures must preserve integer rank separately from
// target width, including callback types stored in external data objects.
typedef signed char (*signed_char_rank_callback_t)(signed char);
typedef short (*short_rank_callback_t)(short);
typedef int (*int_rank_callback_t)(int);

signed char transform_signed_char_rank(signed char value);
short transform_short_rank(short value);
int transform_int_rank(int value);

extern signed_char_rank_callback_t global_signed_char_rank_callback;
extern short_rank_callback_t global_short_rank_callback;
extern int_rank_callback_t global_int_rank_callback;

_Static_assert(
    _Generic(global_signed_char_rank_callback,
             signed_char_rank_callback_t: 1, default: 0),
    "signed char callback rank must survive declaration lowering");
_Static_assert(
    _Generic(global_short_rank_callback,
             short_rank_callback_t: 1, default: 0),
    "short callback rank must survive declaration lowering");
_Static_assert(
    _Generic(global_int_rank_callback,
             int_rank_callback_t: 1, default: 0),
    "int callback rank must survive declaration lowering");

int main(void) {
  if (transform_signed_char_rank((signed char)-100) != -93) return 1;
  if (transform_short_rank((short)-20000) != -18766) return 2;
  if (transform_int_rank(-2000000000) != -1999876544) return 3;
  if (global_signed_char_rank_callback((signed char)40) != 47) return 4;
  if (global_short_rank_callback((short)30000) != 31234) return 5;
  if (global_int_rank_callback(2000000000) != 2000123456) return 6;
  return 0;
}
