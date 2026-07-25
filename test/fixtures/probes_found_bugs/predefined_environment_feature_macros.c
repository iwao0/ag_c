#include <assert.h>

#ifndef __STDC__
#error "__STDC__ must be defined"
#endif

#ifndef __STDC_VERSION__
#error "__STDC_VERSION__ must be defined"
#endif

#ifndef __STDC_HOSTED__
#error "__STDC_HOSTED__ must be defined"
#endif

#ifndef __STDC_NO_THREADS__
#error "__STDC_NO_THREADS__ must describe the missing threads.h feature"
#endif

#ifndef __STDC_UTF_16__
#error "__STDC_UTF_16__ must describe char16_t as UTF-16"
#endif

#ifndef __STDC_UTF_32__
#error "__STDC_UTF_32__ must describe char32_t as UTF-32"
#endif

#ifdef __STDC_NO_ATOMICS__
#error "stdatomic.h is supported"
#endif

#ifdef __STDC_NO_COMPLEX__
#error "complex arithmetic is supported"
#endif

#ifdef __STDC_NO_VLA__
#error "variable length arrays are supported"
#endif

int main(void) {
  assert(__STDC__ == 1);
  assert(__STDC_VERSION__ >= 201112L);
  assert(__STDC_HOSTED__ == 1);
  assert(__STDC_NO_THREADS__ == 1);
  assert(__STDC_UTF_16__ == 1);
  assert(__STDC_UTF_32__ == 1);
  assert(sizeof(u"\U0001F642") / sizeof(u"\U0001F642"[0]) == 3);
  assert(u"\U0001F642"[0] == 0xd83d);
  assert(u"\U0001F642"[1] == 0xde42);
  assert(U"\U0001F642"[0] == 0x1f642);
  return 0;
}
