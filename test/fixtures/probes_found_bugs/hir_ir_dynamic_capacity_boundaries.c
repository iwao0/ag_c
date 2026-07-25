#include <assert.h>

#define APPLY_1(prefix, apply) \
  apply(prefix##0) apply(prefix##1)
#define APPLY_2(prefix, apply) \
  APPLY_1(prefix##0, apply) APPLY_1(prefix##1, apply)
#define APPLY_3(prefix, apply) \
  APPLY_2(prefix##0, apply) APPLY_2(prefix##1, apply)
#define APPLY_4(prefix, apply) \
  APPLY_3(prefix##0, apply) APPLY_3(prefix##1, apply)
#define APPLY_5(prefix, apply) \
  APPLY_4(prefix##0, apply) APPLY_4(prefix##1, apply)
#define APPLY_6(prefix, apply) \
  APPLY_5(prefix##0, apply) APPLY_5(prefix##1, apply)
#define APPLY_7(prefix, apply) \
  APPLY_6(prefix##0, apply) APPLY_6(prefix##1, apply)
#define APPLY_8(prefix, apply) \
  APPLY_7(prefix##0, apply) APPLY_7(prefix##1, apply)
#define APPLY_9(prefix, apply) \
  APPLY_8(prefix##0, apply) APPLY_8(prefix##1, apply)

static int nested_loop_capacity(void) {
  int result = 0;
#define ONE_LOOP for (int once = 0; once < 1; ++once)
  ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP
  ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP
  ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP
  ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP
  ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP ONE_LOOP
    ++result;
#undef ONE_LOOP
  return result;
}

static int nested_switch_capacity(void) {
  int result = 0;
#define OPEN_SWITCH switch (0) { case 0:
  OPEN_SWITCH OPEN_SWITCH OPEN_SWITCH OPEN_SWITCH OPEN_SWITCH
  OPEN_SWITCH OPEN_SWITCH OPEN_SWITCH OPEN_SWITCH OPEN_SWITCH
  OPEN_SWITCH OPEN_SWITCH OPEN_SWITCH OPEN_SWITCH OPEN_SWITCH
  OPEN_SWITCH OPEN_SWITCH
    ++result;
  } } } } } } } } } } } } } } } } }
#undef OPEN_SWITCH
  return result;
}

static int label_capacity(void) {
  int result = 0;
#define EMIT_LABEL(name) name: ++result;
  goto label_0000000;
  APPLY_7(label_, EMIT_LABEL)
  EMIT_LABEL(label_extra)
#undef EMIT_LABEL
  return result;
}

static int local_capacity(void) {
  int result = 0;
#define DECLARE_LOCAL(name) int name = 1;
  APPLY_9(local_, DECLARE_LOCAL)
  DECLARE_LOCAL(local_extra)
#undef DECLARE_LOCAL
#define LOCAL_POINTER(name) &name,
  int *locals[] = {
      APPLY_9(local_, LOCAL_POINTER)
      &local_extra,
  };
#undef LOCAL_POINTER
  for (int index = 0; index < 513; ++index)
    result += *locals[index];
  return result;
}

int main(void) {
  assert(nested_loop_capacity() == 1);
  assert(nested_switch_capacity() == 1);
  assert(label_capacity() == 129);
  assert(local_capacity() == 513);
  return 0;
}
