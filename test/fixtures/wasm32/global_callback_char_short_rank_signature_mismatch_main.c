typedef short (*integer_rank_callback_t)(short);

extern integer_rank_callback_t global_integer_rank_callback;

int main(void) {
  return global_integer_rank_callback((short)7) == 12 ? 0 : 1;
}
