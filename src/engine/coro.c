#include "coro.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(__x86_64__) || defined(__aarch64__)
/* Callee-saved register context switch. ctx[0] = saved stack pointer. */
typedef struct { void *sp; } Ctx;
extern void swiv_ctx_switch(Ctx *from, Ctx *to);
#if defined(__x86_64__)
__asm__(
".text\n.globl swiv_ctx_switch\n.type swiv_ctx_switch,@function\nswiv_ctx_switch:\n"
"  pushq %rbp\n  pushq %rbx\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
"  movq %rsp, (%rdi)\n  movq (%rsi), %rsp\n"
"  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rbx\n  popq %rbp\n  ret\n");
#else /* aarch64 */
__asm__(
".text\n.globl swiv_ctx_switch\n.type swiv_ctx_switch,%function\nswiv_ctx_switch:\n"
"  sub sp, sp, #176\n"
"  stp x19, x20, [sp, #0]\n  stp x21, x22, [sp, #16]\n  stp x23, x24, [sp, #32]\n  stp x25, x26, [sp, #48]\n"
"  stp x27, x28, [sp, #64]\n  stp x29, x30, [sp, #80]\n"
"  stp d8, d9, [sp, #96]\n  stp d10, d11, [sp, #112]\n  stp d12, d13, [sp, #128]\n  stp d14, d15, [sp, #144]\n"
"  mov x2, sp\n  str x2, [x0]\n  ldr x2, [x1]\n  mov sp, x2\n"
"  ldp x19, x20, [sp, #0]\n  ldp x21, x22, [sp, #16]\n  ldp x23, x24, [sp, #32]\n  ldp x25, x26, [sp, #48]\n"
"  ldp x27, x28, [sp, #64]\n  ldp x29, x30, [sp, #80]\n"
"  ldp d8, d9, [sp, #96]\n  ldp d10, d11, [sp, #112]\n  ldp d12, d13, [sp, #128]\n  ldp d14, d15, [sp, #144]\n"
"  add sp, sp, #176\n  ret\n");
#endif

struct Coro { Ctx ctx, caller; CoroFn fn; void *arg; void *stack; size_t size; int finished; };
static __thread Coro *current;

static void coro_trampoline(void) {
    Coro *c = current;
    c->fn(c->arg);
    c->finished = 1;
    swiv_ctx_switch(&c->ctx, &c->caller);   /* never returns */
    abort();
}
#if defined(__x86_64__)
static void coro_boot(void) { coro_trampoline(); }
#endif

Coro *coro_new(CoroFn fn, void *arg, size_t stack_bytes) {
    Coro *c = calloc(1, sizeof *c);
    c->fn = fn; c->arg = arg; c->size = stack_bytes < 16384 ? 16384 : stack_bytes;
    c->stack = malloc(c->size);
    uintptr_t top = ((uintptr_t)c->stack + c->size) & ~(uintptr_t)15;
#if defined(__x86_64__)
    /* stack layout for swiv_ctx_switch: r15 r14 r13 r12 rbx rbp ret */
    uint64_t *sp = (uint64_t *)top;
    *--sp = 0;                          /* alignment: after ret, rsp % 16 == 8 as at function entry */
    *--sp = (uint64_t)coro_boot;        /* return address */
    for (int i = 0; i < 6; i++) *--sp = 0;
    c->ctx.sp = sp;
#else
    uint64_t *sp = (uint64_t *)(top - 176);
    memset(sp, 0, 176);
    sp[11] = (uint64_t)coro_trampoline; /* x30 (lr) slot at +88 */
    c->ctx.sp = sp;
#endif
    return c;
}
int coro_resume(Coro *c) {
    if (c->finished) return 0;
    Coro *prev = current; current = c;
    swiv_ctx_switch(&c->caller, &c->ctx);
    current = prev;
    return !c->finished;
}
void coro_yield(void) { Coro *c = current; swiv_ctx_switch(&c->ctx, &c->caller); }
#else
#include <ucontext.h>
struct Coro { ucontext_t ctx, caller; CoroFn fn; void *arg; void *stack; size_t size; int finished; };
static __thread Coro *current;
static void tramp(void) { Coro *c = current; c->fn(c->arg); c->finished = 1; }
Coro *coro_new(CoroFn fn, void *arg, size_t stack_bytes) {
    Coro *c = calloc(1, sizeof *c); c->fn = fn; c->arg = arg; c->size = stack_bytes < 32768 ? 32768 : stack_bytes;
    c->stack = malloc(c->size); getcontext(&c->ctx); c->ctx.uc_stack.ss_sp = c->stack; c->ctx.uc_stack.ss_size = c->size;
    c->ctx.uc_link = &c->caller; makecontext(&c->ctx, tramp, 0); return c;
}
int coro_resume(Coro *c) { if (c->finished) return 0; Coro *p = current; current = c; swapcontext(&c->caller, &c->ctx); current = p; return !c->finished; }
void coro_yield(void) { Coro *c = current; swapcontext(&c->ctx, &c->caller); }
#endif
void coro_free(Coro *c) { if (c) { free(c->stack); free(c); } }
int coro_finished(const Coro *c) { return c->finished; }
Coro *coro_current(void) { return current; }
