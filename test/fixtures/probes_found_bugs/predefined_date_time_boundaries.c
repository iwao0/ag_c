#include <assert.h>

#if !defined(__DATE__) || !defined(__TIME__)
#error date and time predefined macros must be available
#endif

static const char compile_date[] = __DATE__;
static const char compile_date_again[] = __DATE__;
static const char compile_time[] = __TIME__;
static const char compile_time_again[] = __TIME__;
static const char build_stamp[] = "built " __DATE__ " " __TIME__;

_Static_assert(sizeof(compile_date) == 12, "__DATE__ length");
_Static_assert(sizeof(compile_time) == 9, "__TIME__ length");
_Static_assert(sizeof(build_stamp) == 27, "concatenated build stamp length");

static int is_digit(char value) {
  return value >= '0' && value <= '9';
}

static int is_upper(char value) {
  return value >= 'A' && value <= 'Z';
}

static int is_lower(char value) {
  return value >= 'a' && value <= 'z';
}

static int same_text(const char *left, const char *right) {
  while (*left || *right) {
    if (*left != *right) return 0;
    left++;
    right++;
  }
  return 1;
}

int main(void) {
  assert(is_upper(compile_date[0]));
  assert(is_lower(compile_date[1]));
  assert(is_lower(compile_date[2]));
  assert(compile_date[3] == ' ');
  assert(compile_date[4] == ' ' || is_digit(compile_date[4]));
  assert(is_digit(compile_date[5]));
  assert(compile_date[6] == ' ');
  assert(is_digit(compile_date[7]));
  assert(is_digit(compile_date[8]));
  assert(is_digit(compile_date[9]));
  assert(is_digit(compile_date[10]));

  assert(is_digit(compile_time[0]));
  assert(is_digit(compile_time[1]));
  assert(compile_time[2] == ':');
  assert(is_digit(compile_time[3]));
  assert(is_digit(compile_time[4]));
  assert(compile_time[5] == ':');
  assert(is_digit(compile_time[6]));
  assert(is_digit(compile_time[7]));
  assert((compile_time[0] - '0') * 10 + compile_time[1] - '0' < 24);
  assert((compile_time[3] - '0') * 10 + compile_time[4] - '0' < 60);
  assert((compile_time[6] - '0') * 10 + compile_time[7] - '0' < 60);

  assert(same_text(compile_date, compile_date_again));
  assert(same_text(compile_time, compile_time_again));
  assert(build_stamp[0] == 'b');
  assert(build_stamp[5] == ' ');
  return 0;
}
