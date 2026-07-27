#!/usr/bin/env bash

# Strict-C exclusions shared by every c-testsuite runner. Keep these cases in
# one place so Native, Wasm WAT, Wasm object, and linked-object counts agree.
unsupported_reason() {
  case "$1" in
    00095) echo "function pointer to object pointer conversion" ;;
    00144) echo "discarding const qualifier in pointer assignment" ;;
    00206) echo "GNU #pragma push_macro/pop_macro" ;;
    00210) echo "GNU __attribute__ syntax and placement" ;;
    00213) echo "GNU statement expressions" ;;
    00214) echo "GNU statement expressions" ;;
    00216) echo "GNU empty struct / range designator" ;;
    *) return 1 ;;
  esac
}
