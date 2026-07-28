/*
 * _Noreturn only terminates the path on which its direct call executes.
 * Every function below keeps at least one path to its closing brace, so the
 * compiler must retain W3005.  An indirect call through a function pointer is
 * also not _Noreturn because that property belongs to the declaration, not
 * the function type.
 */

static _Noreturn void stop(void) {
    for (;;) {
    }
}

static int partial_if(int value) {
    if (value) {
        stop();
    }
}

static int partial_conditional(int value) {
    value ? stop() : (void)0;
}

static int partial_short_circuit(int value) {
    value && (stop(), 1);
}

static int indirect_call(void (*callback)(void)) {
    callback();
}

static int while_true_can_break(int value) {
    while (1) {
        if (value) {
            stop();
        }
        break;
    }
}

static int while_false(int value) {
    while (0) {
        stop();
    }
}

static int constant_false_if(int value) {
    if (0) {
        stop();
    }
}

static int goto_out_reaches_end(int value) {
    while (1) {
        if (value) {
            goto out;
        }
    }
out:
    ;
}

static int switch_path_reaches_end(int value) {
    switch (value) {
        case 0:
            while (1) {
            }
        default:
            break;
    }
}

static int nested_outer_break_reaches_end(int value) {
    while (1) {
        while (value) {
            break;
        }
        if (value) {
            break;
        }
    }
}

/*
 * The case body can still reach default when value is zero.  Recognizing the
 * _Noreturn arm of the conditional must not suppress W3017 for that edge.
 */
static int partial_switch(int value) {
    switch (value) {
        case 0:
            value ? stop() : (void)0;
        default:
            return 0;
    }
}

int main(void) {
    return partial_switch(0);
}
