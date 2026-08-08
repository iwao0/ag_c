/*
 * Every bundled public header must compose in one translation unit when the
 * initial include order is reversed and the headers are then included again.
 */
#include <wctype.h>
#include <wchar.h>
#include <unistd.h>
#include <uchar.h>
#include <time.h>
#include <tgmath.h>
#include <string.h>
#include <stdnoreturn.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdalign.h>
#include <signal.h>
#include <setjmp.h>
#include <math.h>
#include <locale.h>
#include <limits.h>
#include <iso646.h>
#include <inttypes.h>
#include <float.h>
#include <fenv.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <complex.h>
#include <assert.h>

#include <assert.h>
#include <complex.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fenv.h>
#include <float.h>
#include <inttypes.h>
#include <iso646.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <tgmath.h>
#include <time.h>
#include <uchar.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

_Static_assert(sizeof(size_t) == 8, "size_t width");
_Static_assert(sizeof(ptrdiff_t) == 8, "ptrdiff_t width");
_Static_assert(sizeof(intmax_t) == 8, "intmax_t width");
_Static_assert(sizeof(char16_t) == 2, "char16_t width");
_Static_assert(sizeof(char32_t) == 4, "char32_t width");
_Static_assert(sizeof(wchar_t) == 4, "wchar_t width");
_Static_assert(sizeof(wint_t) == 4, "wint_t width");
_Static_assert(sizeof(sig_atomic_t) == 4, "sig_atomic_t width");
_Static_assert(sizeof(atomic_int) == sizeof(int), "atomic_int width");
_Static_assert(alignof(max_align_t) >= alignof(double),
               "max_align_t alignment");

#ifdef __wasm32__
_Static_assert(sizeof(void *) == 4, "Wasm pointer width");
_Static_assert(sizeof(fexcept_t) == 8, "Wasm fexcept_t width");
_Static_assert(sizeof(jmp_buf) == 384, "Wasm jmp_buf storage");
_Static_assert(sizeof(mbstate_t) == 32, "Wasm mbstate_t storage");
#else
_Static_assert(sizeof(void *) == 8, "native pointer width");
_Static_assert(sizeof(fexcept_t) == 2, "native fexcept_t width");
_Static_assert(sizeof(jmp_buf) == 192, "native jmp_buf storage");
_Static_assert(sizeof(mbstate_t) == 128, "native mbstate_t storage");
#endif

static FILE *stream_value;
static struct lconv *locale_value;
static fenv_t environment_value;
static jmp_buf jump_value;
static mbstate_t multibyte_value;
static struct tm calendar_value;
static wctype_t descriptor_value;
static off_t offset_value;

int main(void) {
  assert(stream_value == NULL);
  assert(locale_value == NULL);
  assert(offset_value == 0);
  assert(descriptor_value == 0);
  assert(calendar_value.tm_year == 0);
  assert(((unsigned char *)&environment_value)[0] == 0);
  assert(((unsigned char *)&jump_value)[0] == 0);
  assert(((unsigned char *)&multibyte_value)[0] == 0);
  return 0;
}
