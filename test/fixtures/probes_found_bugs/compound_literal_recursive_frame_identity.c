/*
 * One block-scope compound literal object belongs to each active execution of
 * its enclosing block. Re-entering the occurrence with goto in one frame
 * reuses that frame's object, while recursive executions must have distinct
 * simultaneously-live objects.
 */
#include <assert.h>

#define FRAME_COUNT 24

struct Frame {
  int depth;
  int round;
  int values[3];
};

static struct Frame *active_frames[FRAME_COUNT];
static int initializer_effects;

static int initialized_value(int depth, int round, int slot) {
  initializer_effects++;
  return depth * 100 + round * 10 + slot;
}

static void check_frame(const struct Frame *frame, int depth) {
  assert(frame != 0);
  assert(frame->depth == depth);
  assert(frame->round == 1);
  assert(frame->values[0] == depth * 100 + 10);
  assert(frame->values[1] == depth * 100 + 11);
  assert(frame->values[2] == depth * 100 + 12);
}

static void visit_frame(int depth) {
  int round = 0;
  struct Frame *first = 0;
  struct Frame *current = 0;

repeat_literal:
  current = &(struct Frame){
      depth,
      round,
      {
          initialized_value(depth, round, 0),
          initialized_value(depth, round, 1),
          initialized_value(depth, round, 2),
      },
  };
  if (round == 0) {
    first = current;
    current->depth = -1;
    current->values[0] = -1;
    round = 1;
    goto repeat_literal;
  }

  assert(current == first);
  check_frame(current, depth);

  for (int ancestor = 0; ancestor < depth; ancestor++) {
    assert(current != active_frames[ancestor]);
    check_frame(active_frames[ancestor], ancestor);
  }
  active_frames[depth] = current;

  if (depth + 1 < FRAME_COUNT)
    visit_frame(depth + 1);

  check_frame(current, depth);
  for (int ancestor = 0; ancestor < depth; ancestor++)
    check_frame(active_frames[ancestor], ancestor);
  active_frames[depth] = 0;
}

int main(void) {
  visit_frame(0);
  assert(initializer_effects == FRAME_COUNT * 2 * 3);
  for (int depth = 0; depth < FRAME_COUNT; depth++)
    assert(active_frames[depth] == 0);
  return 0;
}
