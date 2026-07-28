/*
 * Preserve the complete C11 <signal.h> macro and type surface without
 * delivering process-terminating signals.
 */
#include <assert.h>
#include <signal.h>
#include <signal.h>

typedef void (*handler_type)(int);

#define IS_INT(expression) _Generic((expression), int: 1, default: 0)
#define IS_HANDLER(expression) \
  _Generic((expression), handler_type: 1, default: 0)

#if SIGABRT != 6 || SIGFPE != 8 || SIGILL != 4 || SIGINT != 2 || \
    SIGSEGV != 11 || SIGTERM != 15
#error "unexpected target signal numbers"
#endif

_Static_assert(IS_INT(SIGABRT), "SIGABRT type");
_Static_assert(IS_INT(SIGFPE), "SIGFPE type");
_Static_assert(IS_INT(SIGILL), "SIGILL type");
_Static_assert(IS_INT(SIGINT), "SIGINT type");
_Static_assert(IS_INT(SIGSEGV), "SIGSEGV type");
_Static_assert(IS_INT(SIGTERM), "SIGTERM type");
_Static_assert(_Generic((sig_atomic_t)0, int: 1, default: 0),
               "sig_atomic_t target identity");
_Static_assert(sizeof(sig_atomic_t) == sizeof(int), "sig_atomic_t width");
_Static_assert((sig_atomic_t)-1 < 0, "sig_atomic_t is signed");
_Static_assert(IS_HANDLER(SIG_DFL), "SIG_DFL type");
_Static_assert(IS_HANDLER(SIG_IGN), "SIG_IGN type");
_Static_assert(IS_HANDLER(SIG_ERR), "SIG_ERR type");

enum {
  signal_number_sum =
      SIGABRT + SIGFPE + SIGILL + SIGINT + SIGSEGV + SIGTERM
};

static handler_type sentinels[] = {
    SIG_DFL,
    SIG_IGN,
    SIG_ERR,
};

int main(void) {
  handler_type (*install_handler)(int, handler_type) = signal;
  int (*send_signal)(int) = raise;

  assert(signal_number_sum == 46);
  assert(SIGABRT > 0 && SIGFPE > 0 && SIGILL > 0);
  assert(SIGINT > 0 && SIGSEGV > 0 && SIGTERM > 0);
  assert(sentinels[0] == SIG_DFL);
  assert(sentinels[1] == SIG_IGN);
  assert(sentinels[2] == SIG_ERR);
  assert(sentinels[0] != sentinels[1]);
  assert(sentinels[0] != sentinels[2]);
  assert(sentinels[1] != sentinels[2]);
  assert(install_handler != 0);
  assert(send_signal != 0);
  return 0;
}
