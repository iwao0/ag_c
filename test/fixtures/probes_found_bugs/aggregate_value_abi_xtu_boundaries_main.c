// Cross-TU aggregate parameter, return, callback, and variadic ABI boundaries.
// Expected with aggregate_value_abi_xtu_boundaries_other.c: exit=42.
#include "test/fixtures/probes_found_bugs/aggregate_value_abi_xtu_boundaries.h"

static int anchors[4] = {10, 20, 30, 40};

static int triple(int value) {
  return value * 3;
}

static int add_five(int value) {
  return value + 5;
}

static int hfa_is(struct xtu_hfa4 value,
                  float first,
                  float second,
                  float third,
                  float fourth) {
  return value.rows[0].first == first &&
         value.rows[0].second == second &&
         value.rows[1].first == third &&
         value.rows[1].second == fourth;
}

static int packet_error(struct xtu_large_packet value,
                        int amount) {
  int row;
  int column;
  int expected = 1;
  if (value.marker != 100ULL + (unsigned long long)amount) return 10;
  if (value.reals[0] != 7.25 + (double)amount) return 11;
  if (value.reals[1] != 8.5 + (double)amount) return 12;
  if (value.objects[0] != &anchors[0]) return 13;
  if (value.objects[1] != &anchors[3]) return 14;
  if (value.callbacks[0] != triple) return 15;
  if (value.callbacks[1] != add_five) return 16;
  if (value.tail != -9 - amount) return 17;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 3; column++) {
      if (value.grid[row][column] != expected + amount) {
        return 20 + row * 3 + column;
      }
      expected++;
    }
  }
  return 0;
}

int main(void) {
  xtu_hfa_callback_t *hfa_callback = xtu_shift_hfa;
  xtu_pointer_pair_callback_t *pointer_callback =
      xtu_apply_pointer_pair;
  xtu_packet_callback_t *packet_callback = xtu_shift_packet;
  xtu_choice_callback_t *choice_callback = xtu_shift_choice;
  struct xtu_hfa4 hfa = {
      .rows = {
          {1.25f, 2.5f},
          {3.75f, 4.0f},
      },
  };
  struct xtu_pointer_pair pair = {
      .object = &anchors[1],
      .callback = triple,
  };
  struct xtu_large_packet packet = {
      .marker = 100ULL,
      .grid = {
          {1, 2, 3},
          {4, 5, 6},
      },
      .reals = {7.25, 8.5},
      .objects = {&anchors[0], &anchors[3]},
      .callbacks = {triple, add_five},
      .tail = -9,
  };
  union xtu_choice choice = {
      .payload = {
          .values = {50, 60},
          .object = &anchors[2],
          .callback = add_five,
      },
  };
  int packet_failure;

  if (packet.tail != -9) return 40;

  hfa = hfa_callback(hfa, 2);
  if (!hfa_is(hfa, 3.25f, 4.5f, 5.75f, 6.0f)) {
    return 1;
  }

  pair = pointer_callback(pair, 4);
  if (anchors[1] != 32 ||
      pair.object != &anchors[2] ||
      pair.callback != triple ||
      pair.callback(5) != 15) {
    return 2;
  }

  packet = packet_callback(packet, 3);
  packet_failure = packet_error(packet, 3);
  if (packet_failure != 0) return packet_failure;
  if (anchors[0] != 19) return 30;
  if (anchors[3] != 48) return 31;

  choice = choice_callback(choice, 4);
  if (choice.payload.values[0] != 54 ||
      choice.payload.values[1] != 68 ||
      choice.payload.object != &anchors[2] ||
      choice.payload.callback != add_five ||
      anchors[2] != 39) {
    return 4;
  }

  if (!xtu_check_variadic(77, hfa, pair, packet, choice)) {
    return 5;
  }
  return 42;
}
