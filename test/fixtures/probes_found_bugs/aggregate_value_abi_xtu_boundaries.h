#ifndef AG_C_AGGREGATE_VALUE_ABI_XTU_BOUNDARIES_H
#define AG_C_AGGREGATE_VALUE_ABI_XTU_BOUNDARIES_H

typedef int xtu_scalar_callback_t(int);

struct xtu_float_pair {
  float first;
  float second;
};

struct xtu_hfa4 {
  struct xtu_float_pair rows[2];
};

struct xtu_pointer_pair {
  int *object;
  xtu_scalar_callback_t *callback;
};

struct xtu_large_packet {
  unsigned long long marker;
  int grid[2][3];
  double reals[2];
  int *objects[2];
  xtu_scalar_callback_t *callbacks[2];
  long tail;
};

struct xtu_choice_payload {
  long values[2];
  int *object;
  xtu_scalar_callback_t *callback;
};

union xtu_choice {
  struct xtu_choice_payload payload;
  unsigned char reserve[32];
};

typedef struct xtu_hfa4 xtu_hfa_callback_t(
    struct xtu_hfa4, int);
typedef struct xtu_pointer_pair xtu_pointer_pair_callback_t(
    struct xtu_pointer_pair, int);
typedef struct xtu_large_packet xtu_packet_callback_t(
    struct xtu_large_packet, int);
typedef union xtu_choice xtu_choice_callback_t(
    union xtu_choice, int);

struct xtu_hfa4 xtu_shift_hfa(struct xtu_hfa4 value,
                              int amount);
struct xtu_pointer_pair xtu_apply_pointer_pair(
    struct xtu_pointer_pair value, int amount);
struct xtu_large_packet xtu_shift_packet(
    struct xtu_large_packet value, int amount);
union xtu_choice xtu_shift_choice(union xtu_choice value,
                                  int amount);
int xtu_check_variadic(int marker, ...);

#endif
