#ifndef AG_C_ATOMIC_AGGREGATE_CALLBACK_XTU_H
#define AG_C_ATOMIC_AGGREGATE_CALLBACK_XTU_H

struct words3 {
  unsigned int a;
  unsigned int b;
  unsigned int c;
};

typedef struct words3 callback_t(_Atomic(struct words3));

struct words3 rotate_words(_Atomic(struct words3));

#endif
