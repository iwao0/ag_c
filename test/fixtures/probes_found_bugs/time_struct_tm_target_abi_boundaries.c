/*
 * Preserve the target-specific struct tm ABI used by the native libc and the
 * standalone Wasm runtime.  wchar.h may forward-declare struct tm before
 * time.h completes it.
 */
#include <assert.h>
#include <stddef.h>
#include <wchar.h>
#include <time.h>

_Static_assert(offsetof(struct tm, tm_sec) == 0, "tm_sec offset");
_Static_assert(offsetof(struct tm, tm_min) == 4, "tm_min offset");
_Static_assert(offsetof(struct tm, tm_hour) == 8, "tm_hour offset");
_Static_assert(offsetof(struct tm, tm_mday) == 12, "tm_mday offset");
_Static_assert(offsetof(struct tm, tm_mon) == 16, "tm_mon offset");
_Static_assert(offsetof(struct tm, tm_year) == 20, "tm_year offset");
_Static_assert(offsetof(struct tm, tm_wday) == 24, "tm_wday offset");
_Static_assert(offsetof(struct tm, tm_yday) == 28, "tm_yday offset");
_Static_assert(offsetof(struct tm, tm_isdst) == 32, "tm_isdst offset");

#ifdef __wasm32__
_Static_assert(sizeof(struct tm) == 36, "Wasm struct tm size");
_Static_assert(_Alignof(struct tm) == 4, "Wasm struct tm alignment");
#else
_Static_assert(offsetof(struct tm, tm_gmtoff) == 40, "native tm_gmtoff offset");
_Static_assert(offsetof(struct tm, tm_zone) == 48, "native tm_zone offset");
_Static_assert(sizeof(struct tm) == 56, "native struct tm size");
_Static_assert(_Alignof(struct tm) == 8, "native struct tm alignment");
_Static_assert(_Generic(((struct tm *)0)->tm_gmtoff,
                        long: 1, default: 0),
               "native tm_gmtoff type");
_Static_assert(_Generic(((struct tm *)0)->tm_zone,
                        char *: 1, default: 0),
               "native tm_zone type");
#endif

static struct tm *(*gmtime_signature)(const time_t *) = gmtime;
static struct tm *(*localtime_signature)(const time_t *) = localtime;
static time_t (*mktime_signature)(struct tm *) = mktime;
static size_t (*strftime_signature)(
    char *, size_t, const char *, const struct tm *) = strftime;
static size_t (*wcsftime_signature)(
    wchar_t *, size_t, const wchar_t *, const struct tm *) = wcsftime;

struct guarded_tm {
  unsigned int before;
  struct tm value;
  unsigned int after;
};

int main(void) {
  const unsigned int before = 0x13579bdfU;
  const unsigned int after = 0x2468ace0U;
  time_t epoch = 0;
  struct tm *utc = gmtime_signature(&epoch);
  struct tm utc_value;
  struct tm *local;
  struct guarded_tm guarded;
  char narrow[32];
  wchar_t wide[32];

  assert(utc != NULL);
  assert(utc->tm_sec == 0 && utc->tm_min == 0 && utc->tm_hour == 0);
  assert(utc->tm_mday == 1 && utc->tm_mon == 0 && utc->tm_year == 70);
  assert(utc->tm_wday == 4 && utc->tm_yday == 0);
  utc_value = *utc;
  local = localtime_signature(&epoch);
  assert(local != NULL);

  guarded.before = before;
  guarded.value = utc_value;
  guarded.value.tm_sec = 61;
  guarded.value.tm_min = -1;
  guarded.value.tm_isdst = -1;
  guarded.after = after;
  (void)mktime_signature(&guarded.value);
  assert(guarded.before == before);
  assert(guarded.after == after);
  assert(guarded.value.tm_sec >= 0 && guarded.value.tm_sec <= 60);
  assert(guarded.value.tm_min >= 0 && guarded.value.tm_min <= 59);
  assert(guarded.value.tm_hour >= 0 && guarded.value.tm_hour <= 23);
  assert(guarded.value.tm_mon >= 0 && guarded.value.tm_mon <= 11);
  assert(guarded.value.tm_wday >= 0 && guarded.value.tm_wday <= 6);
  assert(guarded.value.tm_yday >= 0 && guarded.value.tm_yday <= 365);

  assert(strftime_signature(
             narrow, sizeof(narrow), "%Y-%m-%d %H:%M:%S", &utc_value) == 19);
  assert(wcsftime_signature(
             wide, sizeof(wide) / sizeof(wide[0]),
             L"%Y-%m-%d %H:%M:%S", &utc_value) == 19);
  assert(narrow[0] == '1' && narrow[19] == '\0');
  assert(wide[0] == L'1' && wide[19] == L'\0');
  return 0;
}
