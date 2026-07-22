typedef int integer;

static integer inline ordered_specifiers(void) { return 3; }
static inline inline int repeated_inline(void) { return 5; }
static inline int (parenthesized_identifier)(void) { return 7; }

static int pointed_value = 11;
static inline int *returns_pointer(void) { return &pointed_value; }

static int callback_target(void) { return 13; }
static inline int (*returns_callback(void))(void) { return callback_target; }

static inline int redeclared_function(void);
static int redeclared_function(void) { return 17; }

static _Noreturn void (never_returns)(void) { for (;;) {} }
static _Noreturn _Noreturn void repeated_noreturn(void) { for (;;) {} }

static inline int block_target(void) { return 19; }

int main(void) {
  inline int block_target(void);
  _Noreturn void never_returns(void);
  _Noreturn void repeated_noreturn(void);

  int sum = ordered_specifiers() + repeated_inline();
  sum += parenthesized_identifier();
  sum += *returns_pointer();
  sum += returns_callback()();
  sum += redeclared_function();
  sum += block_target();
  return sum == 75 ? 0 : 1;
}
