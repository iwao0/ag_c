// time.h and locale.h signatures, indirect calls, and failure boundaries.
// Expected: exit=0
#include <locale.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

static time_t (*time_signature)(time_t *) = time;
static clock_t (*clock_signature)(void) = clock;
static double (*difftime_signature)(time_t, time_t) = difftime;
static struct tm *(*gmtime_signature)(const time_t *) = gmtime;
static struct tm *(*localtime_signature)(const time_t *) = localtime;
static time_t (*mktime_signature)(struct tm *) = mktime;
static char *(*asctime_signature)(const struct tm *) = asctime;
static char *(*ctime_signature)(const time_t *) = ctime;
static size_t (*strftime_signature)(
    char *, size_t, const char *, const struct tm *) = strftime;
static int (*timespec_get_signature)(struct timespec *, int) = timespec_get;
static char *(*setlocale_signature)(int, const char *) = setlocale;
static struct lconv *(*localeconv_signature)(void) = localeconv;

int main(void) {
    time_t stored = 0;
    time_t epoch = 0;
    time_t now = time_signature(&stored);
    struct timespec ts = {7, 9};
    char text[32];
    char *locale_name;
    struct lconv *locale_data;

    if (now != stored) return 1;
    (void)clock_signature();
    if (difftime_signature((time_t)10, (time_t)3) != 7.0) return 2;

    struct tm *utc = gmtime_signature(&epoch);
    if (!utc || utc->tm_sec != 0 || utc->tm_min != 0 ||
        utc->tm_hour != 0 || utc->tm_mday != 1 || utc->tm_mon != 0 ||
        utc->tm_year != 70 || utc->tm_wday != 4 || utc->tm_yday != 0)
        return 3;
    if (strcmp(asctime_signature(utc), "Thu Jan  1 00:00:00 1970\n") != 0)
        return 4;
    if (strftime_signature(text, sizeof(text), "%Y-%m-%d %H:%M:%S", utc) != 19 ||
        strcmp(text, "1970-01-01 00:00:00") != 0) return 5;

    struct tm normalized = *utc;
    (void)mktime_signature(&normalized);
    if (normalized.tm_sec != 0 || normalized.tm_min != 0 ||
        normalized.tm_hour != 0 || normalized.tm_mday != 1 ||
        normalized.tm_mon != 0 || normalized.tm_year != 70) return 6;

    struct tm *local = localtime_signature(&epoch);
    if (!local || local->tm_sec < 0 || local->tm_sec > 60 ||
        local->tm_min < 0 || local->tm_min > 59 ||
        local->tm_hour < 0 || local->tm_hour > 23) return 7;
    if (!ctime_signature(&epoch) || ctime_signature(&epoch)[0] == '\0') return 8;
    if (timespec_get_signature(&ts, 0) != 0) return 9;

    locale_name = setlocale_signature(LC_ALL, NULL);
    if (!locale_name || locale_name[0] == '\0') return 10;
    locale_name = setlocale_signature(LC_ALL, "C");
    if (!locale_name || strcmp(locale_name, "C") != 0) return 11;
    if (setlocale_signature(LC_ALL, "ag_c_missing_locale") != NULL) return 12;

    locale_data = localeconv_signature();
    if (!locale_data || !locale_data->decimal_point ||
        strcmp(locale_data->decimal_point, ".") != 0) return 13;
    return 0;
}
