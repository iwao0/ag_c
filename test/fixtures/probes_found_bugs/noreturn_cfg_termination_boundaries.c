/*
 * A closing brace is unreachable when every path that reaches the final
 * control-flow construct ends in a direct call declared with _Noreturn.
 * These paths must not produce W3005, and a _Noreturn case must not produce
 * the switch fallthrough warning W3017.
 */

static _Noreturn void stop_a(void) {
    for (;;) {
    }
}

static _Noreturn void stop_b(void) {
    for (;;) {
    }
}

static int choose_if(int value) {
    if (value == 42) {
        return 42;
    }
    if (value) {
        stop_a();
    } else {
        stop_b();
    }
}

static int choose_switch(int value) {
    if (value == 42) {
        return 42;
    }
    switch (value) {
        case 0:
            stop_a();
        default:
            stop_b();
    }
}

static int choose_conditional(int value) {
    if (value == 42) {
        return 42;
    }
    value ? stop_a() : stop_b();
}

static int choose_switch_expressions(int value) {
    if (value == 42) {
        return 42;
    }
    switch (value) {
        case 0:
            value += 0, stop_a();
        case 1:
            value ? stop_a() : stop_b();
        default:
            (void)stop_b();
    }
}

int main(void) {
    if (choose_if(42) != 42) return 1;
    if (choose_switch(42) != 42) return 2;
    if (choose_conditional(42) != 42) return 3;
    if (choose_switch_expressions(42) != 42) return 4;
    return 0;
}
