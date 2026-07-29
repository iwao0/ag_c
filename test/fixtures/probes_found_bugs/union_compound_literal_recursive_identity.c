/*
 * Union compound literals have one automatic object per active execution of
 * the enclosing block. Re-entering the same occurrence reinitializes that
 * object's selected member, while recursive frames remain independent.
 */
#include <assert.h>

#define FRAME_COUNT 24

struct Payload {
  int depth;
  int round;
  int values[2];
};

union FrameValue {
  struct Payload payload;
  long words[3];
};

static union FrameValue *active_frames[FRAME_COUNT];
static int initializer_effects;

static int initialized_value(int depth, int round, int slot) {
  initializer_effects++;
  if (slot == 0)
    return depth;
  if (slot == 1)
    return round;
  return depth * 100 + round * 10 + slot;
}

static void check_frame(const union FrameValue *frame, int depth) {
  assert(frame != 0);
  assert(frame->payload.depth == depth);
  assert(frame->payload.round == 1);
  assert(frame->payload.values[0] == depth * 100 + 12);
  assert(frame->payload.values[1] == depth * 100 + 13);
}

static void visit_frame(int depth) {
  int round = 0;
  union FrameValue *first = 0;
  union FrameValue *current = 0;

repeat_literal:
  current = &(union FrameValue){
      .payload = {
          initialized_value(depth, round, 0),
          initialized_value(depth, round, 1),
          {
              initialized_value(depth, round, 2),
              initialized_value(depth, round, 3),
          },
      },
  };
  if (round == 0) {
    first = current;
    current->words[0] = -1;
    current->words[1] = -2;
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
  assert(initializer_effects == FRAME_COUNT * 2 * 4);
  for (int depth = 0; depth < FRAME_COUNT; depth++)
    assert(active_frames[depth] == 0);
  return 0;
}
