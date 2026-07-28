#ifndef _SETJMP_H
#define _SETJMP_H

#ifdef __wasm32__
/* Opaque storage retained for the standalone Wasm setjmp stub contract. */
typedef long jmp_buf[48];
#else
/* Apple arm64 libSystem uses the SDK's 48-int, 192-byte save area. */
typedef int jmp_buf[48];
#endif

int setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif
