/*
 * Preserve every C11 locale category macro as an integer constant expression
 * and exercise category-specific query/set behavior in the C locale.
 */
#include <assert.h>
#include <locale.h>
#include <locale.h>
#include <string.h>

#if LC_ALL != 0 || LC_COLLATE != 1 || LC_CTYPE != 2 || \
    LC_MONETARY != 3 || LC_NUMERIC != 4 || LC_TIME != 5
#error "unexpected target locale category values"
#endif

#define IS_INT(expression) _Generic((expression), int: 1, default: 0)

_Static_assert(IS_INT(LC_ALL), "LC_ALL type");
_Static_assert(IS_INT(LC_COLLATE), "LC_COLLATE type");
_Static_assert(IS_INT(LC_CTYPE), "LC_CTYPE type");
_Static_assert(IS_INT(LC_MONETARY), "LC_MONETARY type");
_Static_assert(IS_INT(LC_NUMERIC), "LC_NUMERIC type");
_Static_assert(IS_INT(LC_TIME), "LC_TIME type");

enum {
  locale_category_sum =
      LC_ALL + LC_COLLATE + LC_CTYPE + LC_MONETARY + LC_NUMERIC + LC_TIME,
};

static int categories[] = {
    LC_ALL,
    LC_COLLATE,
    LC_CTYPE,
    LC_MONETARY,
    LC_NUMERIC,
    LC_TIME,
};

int main(void) {
  char *(*select_locale)(int, const char *) = setlocale;
  unsigned long index;

  assert(locale_category_sum == 15);
  assert(select_locale(LC_ALL, "C") != 0);

  for (index = 0; index < sizeof(categories) / sizeof(categories[0]); ++index) {
    char *queried = select_locale(categories[index], 0);
    char *selected;

    assert(queried != 0);
    assert(strcmp(queried, "C") == 0);
    selected = select_locale(categories[index], "C");
    assert(selected != 0);
    assert(strcmp(selected, "C") == 0);
  }

  assert(select_locale(-1, "C") == 0);
  assert(select_locale(999, "C") == 0);
  return 0;
}
