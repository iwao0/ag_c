/*
 * A nonzero constant controlling expression has no edge to the closing brace
 * unless a reachable break/goto creates one.  Exercise the constant forms
 * that Typed HIR preserves as distinct expression shapes.
 */

static _Noreturn void stop_a(void) {
    for (;;) {
    }
}

static _Noreturn void stop_b(void) {
    for (;;) {
    }
}

static int while_integer(int value) {
    if (value == 42) return 42;
    while (1) {
    }
}

static int while_long(int value) {
    if (value == 42) return 42;
    while (1L) {
    }
}

static int while_floating(int value) {
    if (value == 42) return 42;
    while (1.0) {
    }
}

static int do_floating(int value) {
    if (value == 42) return 42;
    do {
    } while (1.0);
}

static int while_cast(int value) {
    if (value == 42) return 42;
    while ((double)1) {
    }
}

static int while_arithmetic(int value) {
    if (value == 42) return 42;
    while (1 + 1) {
    }
}

static int while_unary(int value) {
    if (value == 42) return 42;
    while (!0) {
    }
}

static int while_float_compare(int value) {
    if (value == 42) return 42;
    while (1.0 == 1.0) {
    }
}

static int while_ternary(int value) {
    if (value == 42) return 42;
    while (1 ? 2 : 0) {
    }
}

static int while_short_circuit(int value) {
    if (value == 42) return 42;
    while (1 || value) {
    }
}

static int constant_if_noreturn(int value) {
    if (value == 42) return 42;
    if (1) {
        stop_a();
    }
}

static int goto_all_noreturn(int value) {
    if (value == 42) return 42;
    if (value) goto first;
    goto second;
first:
    stop_a();
second:
    stop_b();
}

static int switch_break_stays_in_loop(int value) {
    if (value == 42) return 42;
    while (1) {
        switch (value) {
            case 0:
                break;
            default:
                break;
        }
    }
}

static int inner_loop_break_stays_in_outer(int value) {
    if (value == 42) return 42;
    for (;;) {
        do {
            if (value) {
                break;
            }
        } while (0);
    }
}

static int continue_stays_in_loop(int value) {
    if (value == 42) return 42;
    while (1) {
        if (value) {
            continue;
        }
    }
}

static int label_continue_stays_in_loop(int value) {
    if (value == 42) return 42;
    while (1) {
        if (value) {
            goto inside;
        }
inside:
        continue;
    }
}

int main(void) {
    if (while_integer(42) != 42) return 1;
    if (while_long(42) != 42) return 2;
    if (while_floating(42) != 42) return 3;
    if (do_floating(42) != 42) return 4;
    if (while_cast(42) != 42) return 5;
    if (while_arithmetic(42) != 42) return 6;
    if (while_unary(42) != 42) return 7;
    if (while_float_compare(42) != 42) return 8;
    if (while_ternary(42) != 42) return 9;
    if (while_short_circuit(42) != 42) return 10;
    if (constant_if_noreturn(42) != 42) return 11;
    if (goto_all_noreturn(42) != 42) return 12;
    if (switch_break_stays_in_loop(42) != 42) return 13;
    if (inner_loop_break_stays_in_outer(42) != 42) return 14;
    if (continue_stays_in_loop(42) != 42) return 15;
    if (label_continue_stays_in_loop(42) != 42) return 16;
    return 0;
}
