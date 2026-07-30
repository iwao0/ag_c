// Definition TU for cross-TU aggregate value ABI boundaries.
#include <stdarg.h>

#include "test/fixtures/probes_found_bugs/aggregate_value_abi_xtu_boundaries.h"

struct xtu_hfa4 xtu_shift_hfa(struct xtu_hfa4 value,
                              int amount) {
  value.rows[0].first += (float)amount;
  value.rows[0].second += (float)amount;
  value.rows[1].first += (float)amount;
  value.rows[1].second += (float)amount;
  return value;
}

struct xtu_pointer_pair xtu_apply_pointer_pair(
    struct xtu_pointer_pair value, int amount) {
  *value.object += value.callback(amount);
  value.object++;
  return value;
}

struct xtu_large_packet xtu_shift_packet(
    struct xtu_large_packet value, int amount) {
  int row;
  int column;
  value.marker += (unsigned long long)amount;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 3; column++) {
      value.grid[row][column] += amount;
    }
  }
  value.reals[0] += (double)amount;
  value.reals[1] += (double)amount;
  *value.objects[0] += value.callbacks[0](amount);
  *value.objects[1] += value.callbacks[1](amount);
  value.tail -= amount;
  return value;
}

union xtu_choice xtu_shift_choice(union xtu_choice value,
                                  int amount) {
  value.payload.values[0] += amount;
  value.payload.values[1] += amount * 2;
  *value.payload.object += value.payload.callback(amount);
  return value;
}

int xtu_check_variadic(int marker, ...) {
  va_list arguments;
  struct xtu_hfa4 hfa;
  struct xtu_pointer_pair pair;
  struct xtu_large_packet packet;
  union xtu_choice choice;
  int row;
  int column;
  int expected = 4;

  va_start(arguments, marker);
  hfa = va_arg(arguments, struct xtu_hfa4);
  pair = va_arg(arguments, struct xtu_pointer_pair);
  packet = va_arg(arguments, struct xtu_large_packet);
  choice = va_arg(arguments, union xtu_choice);
  va_end(arguments);

  if (marker != 77 ||
      hfa.rows[0].first != 3.25f ||
      hfa.rows[0].second != 4.5f ||
      hfa.rows[1].first != 5.75f ||
      hfa.rows[1].second != 6.0f ||
      pair.object != choice.payload.object ||
      *pair.object != 39 ||
      pair.callback(2) != 6 ||
      packet.marker != 103ULL ||
      packet.reals[0] != 10.25 ||
      packet.reals[1] != 11.5 ||
      packet.callbacks[0](2) != 6 ||
      packet.callbacks[1](2) != 7 ||
      packet.tail != -12 ||
      choice.payload.values[0] != 54 ||
      choice.payload.values[1] != 68 ||
      choice.payload.callback(2) != 7) {
    return 0;
  }
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 3; column++) {
      if (packet.grid[row][column] != expected) {
        return 0;
      }
      expected++;
    }
  }
  return 1;
}
