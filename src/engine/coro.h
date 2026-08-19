/* coro.h -- minimal stackful coroutines (x86-64 SysV, AArch64; ucontext fallback).
 * Each game object runs its behaviour script on its own small stack, exactly
 * like the Sales Curve kernel's per-object task stacks. */
#ifndef SWIV_CORO_H
#define SWIV_CORO_H
#include <stddef.h>
typedef struct Coro Coro;
typedef void (*CoroFn)(void *arg);
Coro *coro_new(CoroFn fn, void *arg, size_t stack_bytes);   /* created suspended */
void  coro_free(Coro *c);
int   coro_resume(Coro *c);      /* run until it yields or finishes; returns 0 when finished */
void  coro_yield(void);          /* from inside a coroutine */
int   coro_finished(const Coro *c);
Coro *coro_current(void);
#endif
