// WAT standalone stdlib.h termination function pointer stubs
// Expected: exit=0
#include <stdlib.h>

static void (*global_exit_fn)(int) = exit;

int main(void) {
    void (*exit_fn)(int) = exit;
    void (*quick_exit_fn)(int) = quick_exit;
    void (*_Exit_fn)(int) = _Exit;
    void (*abort_fn)(void) = abort;

    if (!exit_fn) return 1;
    if (!quick_exit_fn) return 2;
    if (!_Exit_fn) return 3;
    if (!abort_fn) return 4;
    if (!global_exit_fn) return 5;

    return 0;
}
