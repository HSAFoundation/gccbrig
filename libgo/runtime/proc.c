// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "config.h"

#ifdef HAVE_DL_ITERATE_PHDR
#include <link.h>
#endif

#include "runtime.h"
#include "arch.h"
#include "defs.h"
#include "malloc.h"
#include "go-type.h"

#ifdef USING_SPLIT_STACK

/* FIXME: These are not declared anywhere.  */

extern void __splitstack_getcontext(void *context[10]);

extern void __splitstack_setcontext(void *context[10]);

extern void *__splitstack_makecontext(size_t, void *context[10], size_t *);

extern void * __splitstack_resetcontext(void *context[10], size_t *);

extern void *__splitstack_find(void *, void *, size_t *, void **, void **,
			       void **);

extern void __splitstack_block_signals (int *, int *);

extern void __splitstack_block_signals_context (void *context[10], int *,
						int *);

#endif

#ifndef PTHREAD_STACK_MIN
# define PTHREAD_STACK_MIN 8192
#endif

#if defined(USING_SPLIT_STACK) && defined(LINKER_SUPPORTS_SPLIT_STACK)
# define StackMin PTHREAD_STACK_MIN
#else
# define StackMin ((sizeof(char *) < 8) ? 2 * 1024 * 1024 : 4 * 1024 * 1024)
#endif

uintptr runtime_stacks_sys;

static void gtraceback(G*);

#ifdef __rtems__
#define __thread
#endif

static __thread G *g;

#ifndef SETCONTEXT_CLOBBERS_TLS

static inline void
initcontext(void)
{
}

static inline void
fixcontext(ucontext_t *c __attribute__ ((unused)))
{
}

#else

# if defined(__x86_64__) && defined(__sun__)

// x86_64 Solaris 10 and 11 have a bug: setcontext switches the %fs
// register to that of the thread which called getcontext.  The effect
// is that the address of all __thread variables changes.  This bug
// also affects pthread_self() and pthread_getspecific.  We work
// around it by clobbering the context field directly to keep %fs the
// same.

static __thread greg_t fs;

static inline void
initcontext(void)
{
	ucontext_t c;

	getcontext(&c);
	fs = c.uc_mcontext.gregs[REG_FSBASE];
}

static inline void
fixcontext(ucontext_t* c)
{
	c->uc_mcontext.gregs[REG_FSBASE] = fs;
}

# elif defined(__NetBSD__)

// NetBSD has a bug: setcontext clobbers tlsbase, we need to save
// and restore it ourselves.

static __thread __greg_t tlsbase;

static inline void
initcontext(void)
{
	ucontext_t c;

	getcontext(&c);
	tlsbase = c.uc_mcontext._mc_tlsbase;
}

static inline void
fixcontext(ucontext_t* c)
{
	c->uc_mcontext._mc_tlsbase = tlsbase;
}

# elif defined(__sparc__)

static inline void
initcontext(void)
{
}

static inline void
fixcontext(ucontext_t *c)
{
	/* ??? Using 
	     register unsigned long thread __asm__("%g7");
	     c->uc_mcontext.gregs[REG_G7] = thread;
	   results in
	     error: variable ‘thread’ might be clobbered by \
		‘longjmp’ or ‘vfork’ [-Werror=clobbered]
	   which ought to be false, as %g7 is a fixed register.  */

	if (sizeof (c->uc_mcontext.gregs[REG_G7]) == 8)
		asm ("stx %%g7, %0" : "=m"(c->uc_mcontext.gregs[REG_G7]));
	else
		asm ("st %%g7, %0" : "=m"(c->uc_mcontext.gregs[REG_G7]));
}

# else

#  error unknown case for SETCONTEXT_CLOBBERS_TLS

# endif

#endif

// ucontext_arg returns a properly aligned ucontext_t value.  On some
// systems a ucontext_t value must be aligned to a 16-byte boundary.
// The g structure that has fields of type ucontext_t is defined in
// Go, and Go has no simple way to align a field to such a boundary.
// So we make the field larger in runtime2.go and pick an appropriate
// offset within the field here.
static ucontext_t*
ucontext_arg(void** go_ucontext)
{
	uintptr_t p = (uintptr_t)go_ucontext;
	size_t align = __alignof__(ucontext_t);
	if(align > 16) {
		// We only ensured space for up to a 16 byte alignment
		// in libgo/go/runtime/runtime2.go.
		runtime_throw("required alignment of ucontext_t too large");
	}
	p = (p + align - 1) &~ (uintptr_t)(align - 1);
	return (ucontext_t*)p;
}

// We can not always refer to the TLS variables directly.  The
// compiler will call tls_get_addr to get the address of the variable,
// and it may hold it in a register across a call to schedule.  When
// we get back from the call we may be running in a different thread,
// in which case the register now points to the TLS variable for a
// different thread.  We use non-inlinable functions to avoid this
// when necessary.

G* runtime_g(void) __attribute__ ((noinline, no_split_stack));

G*
runtime_g(void)
{
	return g;
}

M* runtime_m(void) __attribute__ ((noinline, no_split_stack));

M*
runtime_m(void)
{
	if(g == nil)
		return nil;
	return g->m;
}

// Set g.
void
runtime_setg(G* gp)
{
	g = gp;
}

// Start a new thread.
static void
runtime_newosproc(M *mp)
{
	pthread_attr_t attr;
	sigset_t clear, old;
	pthread_t tid;
	int ret;

	if(pthread_attr_init(&attr) != 0)
		runtime_throw("pthread_attr_init");
	if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0)
		runtime_throw("pthread_attr_setdetachstate");

	// Block signals during pthread_create so that the new thread
	// starts with signals disabled.  It will enable them in minit.
	sigfillset(&clear);

#ifdef SIGTRAP
	// Blocking SIGTRAP reportedly breaks gdb on Alpha GNU/Linux.
	sigdelset(&clear, SIGTRAP);
#endif

	sigemptyset(&old);
	pthread_sigmask(SIG_BLOCK, &clear, &old);
	ret = pthread_create(&tid, &attr, runtime_mstart, mp);
	pthread_sigmask(SIG_SETMASK, &old, nil);

	if (ret != 0)
		runtime_throw("pthread_create");
}

// First function run by a new goroutine.  This replaces gogocall.
static void
kickoff(void)
{
	void (*fn)(void*);
	void *param;

	if(g->traceback != nil)
		gtraceback(g);

	fn = (void (*)(void*))(g->entry);
	param = g->param;
	g->param = nil;
	fn(param);
	runtime_goexit1();
}

// Switch context to a different goroutine.  This is like longjmp.
void runtime_gogo(G*) __attribute__ ((noinline));
void
runtime_gogo(G* newg)
{
#ifdef USING_SPLIT_STACK
	__splitstack_setcontext(&newg->stackcontext[0]);
#endif
	g = newg;
	newg->fromgogo = true;
	fixcontext(ucontext_arg(&newg->context[0]));
	setcontext(ucontext_arg(&newg->context[0]));
	runtime_throw("gogo setcontext returned");
}

// Save context and call fn passing g as a parameter.  This is like
// setjmp.  Because getcontext always returns 0, unlike setjmp, we use
// g->fromgogo as a code.  It will be true if we got here via
// setcontext.  g == nil the first time this is called in a new m.
void runtime_mcall(void (*)(G*)) __attribute__ ((noinline));
void
runtime_mcall(void (*pfn)(G*))
{
	M *mp;
	G *gp;
#ifndef USING_SPLIT_STACK
	void *afterregs;
#endif

	// Ensure that all registers are on the stack for the garbage
	// collector.
	__builtin_unwind_init();

	gp = g;
	mp = gp->m;
	if(gp == mp->g0)
		runtime_throw("runtime: mcall called on m->g0 stack");

	if(gp != nil) {

#ifdef USING_SPLIT_STACK
		__splitstack_getcontext(&g->stackcontext[0]);
#else
		// We have to point to an address on the stack that is
		// below the saved registers.
		gp->gcnextsp = &afterregs;
#endif
		gp->fromgogo = false;
		getcontext(ucontext_arg(&gp->context[0]));

		// When we return from getcontext, we may be running
		// in a new thread.  That means that g may have
		// changed.  It is a global variables so we will
		// reload it, but the address of g may be cached in
		// our local stack frame, and that address may be
		// wrong.  Call the function to reload the value for
		// this thread.
		gp = runtime_g();
		mp = gp->m;

		if(gp->traceback != nil)
			gtraceback(gp);
	}
	if (gp == nil || !gp->fromgogo) {
#ifdef USING_SPLIT_STACK
		__splitstack_setcontext(&mp->g0->stackcontext[0]);
#endif
		mp->g0->entry = (byte*)pfn;
		mp->g0->param = gp;

		// It's OK to set g directly here because this case
		// can not occur if we got here via a setcontext to
		// the getcontext call just above.
		g = mp->g0;

		fixcontext(ucontext_arg(&mp->g0->context[0]));
		setcontext(ucontext_arg(&mp->g0->context[0]));
		runtime_throw("runtime: mcall function returned");
	}
}

// Goroutine scheduler
// The scheduler's job is to distribute ready-to-run goroutines over worker threads.
//
// The main concepts are:
// G - goroutine.
// M - worker thread, or machine.
// P - processor, a resource that is required to execute Go code.
//     M must have an associated P to execute Go code, however it can be
//     blocked or in a syscall w/o an associated P.
//
// Design doc at http://golang.org/s/go11sched.

enum
{
	// Number of goroutine ids to grab from runtime_sched->goidgen to local per-P cache at once.
	// 16 seems to provide enough amortization, but other than that it's mostly arbitrary number.
	GoidCacheBatch = 16,
};

extern Sched* runtime_getsched() __asm__ (GOSYM_PREFIX "runtime.getsched");

Sched*	runtime_sched;
int32	runtime_gomaxprocs;
uint32	runtime_needextram = 1;
M	runtime_m0;
G	runtime_g0;	// idle goroutine for m0
G*	runtime_lastg;
M*	runtime_allm;
P**	runtime_allp;
M*	runtime_extram;
int8*	runtime_goos;
int32	runtime_ncpu;
bool	runtime_precisestack;
static int32	newprocs;

static	Lock allglock;	// the following vars are protected by this lock or by stoptheworld
G**	runtime_allg;
uintptr runtime_allglen;
static	uintptr allgcap;

bool	runtime_isarchive;

void* runtime_mstart(void*);
static void runqput(P*, G*);
static G* runqget(P*);
static bool runqputslow(P*, G*, uint32, uint32);
static G* runqsteal(P*, P*);
static void mput(M*);
static M* mget(void);
static void mcommoninit(M*);
static void schedule(void);
static void procresize(int32);
static void acquirep(P*);
static P* releasep(void);
static void newm(void(*)(void), P*);
static void stopm(void);
static void startm(P*, bool);
static void handoffp(P*);
static void wakep(void);
static void stoplockedm(void);
static void startlockedm(G*);
static void sysmon(void);
static uint32 retake(int64);
static void incidlelocked(int32);
static void checkdead(void);
static void exitsyscall0(G*);
static void park0(G*);
static void goexit0(G*);
static void gfput(P*, G*);
static G* gfget(P*);
static void gfpurge(P*);
static void globrunqput(G*);
static void globrunqputbatch(G*, G*, int32);
static G* globrunqget(P*, int32);
static P* pidleget(void);
static void pidleput(P*);
static void injectglist(G*);
static bool preemptall(void);
static bool exitsyscallfast(void);
static void allgadd(G*);

bool runtime_isstarted;

// The bootstrap sequence is:
//
//	call osinit
//	call schedinit
//	make & queue new G
//	call runtime_mstart
//
// The new G calls runtime_main.
void
runtime_schedinit(void)
{
	M *m;
	int32 n, procs;
	String s;
	const byte *p;
	Eface i;

	runtime_sched = runtime_getsched();

	m = &runtime_m0;
	g = &runtime_g0;
	m->g0 = g;
	m->curg = g;
	g->m = m;

	initcontext();

	runtime_sched->maxmcount = 10000;
	runtime_precisestack = 0;

	// runtime_symtabinit();
	runtime_mallocinit();
	mcommoninit(m);
	
	// Initialize the itable value for newErrorCString,
	// so that the next time it gets called, possibly
	// in a fault during a garbage collection, it will not
	// need to allocated memory.
	runtime_newErrorCString(0, &i);
	
	// Initialize the cached gotraceback value, since
	// gotraceback calls getenv, which mallocs on Plan 9.
	runtime_gotraceback(nil);

	runtime_goargs();
	runtime_goenvs();
	runtime_parsedebugvars();

	runtime_sched->lastpoll = runtime_nanotime();
	procs = 1;
	s = runtime_getenv("GOMAXPROCS");
	p = s.str;
	if(p != nil && (n = runtime_atoi(p, s.len)) > 0) {
		if(n > _MaxGomaxprocs)
			n = _MaxGomaxprocs;
		procs = n;
	}
	runtime_allp = runtime_malloc((_MaxGomaxprocs+1)*sizeof(runtime_allp[0]));
	procresize(procs);

	// Can not enable GC until all roots are registered.
	// mstats()->enablegc = 1;
}

extern void main_init(void) __asm__ (GOSYM_PREFIX "__go_init_main");
extern void main_main(void) __asm__ (GOSYM_PREFIX "main.main");

// Used to determine the field alignment.

struct field_align
{
  char c;
  Hchan *p;
};

// main_init_done is a signal used by cgocallbackg that initialization
// has been completed.  It is made before _cgo_notify_runtime_init_done,
// so all cgo calls can rely on it existing.  When main_init is
// complete, it is closed, meaning cgocallbackg can reliably receive
// from it.
Hchan *runtime_main_init_done;

// The chan bool type, for runtime_main_init_done.

extern const struct __go_type_descriptor bool_type_descriptor
  __asm__ (GOSYM_PREFIX "__go_tdn_bool");

static struct __go_channel_type chan_bool_type_descriptor =
  {
    /* __common */
    {
      /* __code */
      GO_CHAN,
      /* __align */
      __alignof (Hchan *),
      /* __field_align */
      offsetof (struct field_align, p) - 1,
      /* __size */
      sizeof (Hchan *),
      /* __hash */
      0, /* This value doesn't matter.  */
      /* __hashfn */
      NULL,
      /* __equalfn */
      NULL,
      /* __gc */
      NULL, /* This value doesn't matter */
      /* __reflection */
      NULL, /* This value doesn't matter */
      /* __uncommon */
      NULL,
      /* __pointer_to_this */
      NULL
    },
    /* __element_type */
    &bool_type_descriptor,
    /* __dir */
    CHANNEL_BOTH_DIR
  };

extern Hchan *makechan (ChanType *, int64)
  __asm__ (GOSYM_PREFIX "runtime.makechan");
extern void closechan(Hchan *) __asm__ (GOSYM_PREFIX "runtime.closechan");

static void
initDone(void *arg __attribute__ ((unused))) {
	runtime_unlockOSThread();
};

// The main goroutine.
// Note: C frames in general are not copyable during stack growth, for two reasons:
//   1) We don't know where in a frame to find pointers to other stack locations.
//   2) There's no guarantee that globals or heap values do not point into the frame.
//
// The C frame for runtime.main is copyable, because:
//   1) There are no pointers to other stack locations in the frame
//      (d.fn points at a global, d.link is nil, d.argp is -1).
//   2) The only pointer into this frame is from the defer chain,
//      which is explicitly handled during stack copying.
void
runtime_main(void* dummy __attribute__((unused)))
{
	Defer d;
	_Bool frame;
	
	newm(sysmon, nil);

	// Lock the main goroutine onto this, the main OS thread,
	// during initialization.  Most programs won't care, but a few
	// do require certain calls to be made by the main thread.
	// Those can arrange for main.main to run in the main thread
	// by calling runtime.LockOSThread during initialization
	// to preserve the lock.
	runtime_lockOSThread();
	
	// Defer unlock so that runtime.Goexit during init does the unlock too.
	d.pfn = (uintptr)(void*)initDone;
	d.link = g->_defer;
	d.arg = (void*)-1;
	d._panic = g->_panic;
	d.retaddr = 0;
	d.makefunccanrecover = 0;
	d.frame = &frame;
	d.special = true;
	g->_defer = &d;

	if(g->m != &runtime_m0)
		runtime_throw("runtime_main not on m0");
	__go_go(runtime_MHeap_Scavenger, nil);

	runtime_main_init_done = makechan(&chan_bool_type_descriptor, 0);

	_cgo_notify_runtime_init_done();

	main_init();

	closechan(runtime_main_init_done);

	if(g->_defer != &d || (void*)d.pfn != initDone)
		runtime_throw("runtime: bad defer entry after init");
	g->_defer = d.link;
	runtime_unlockOSThread();

	// For gccgo we have to wait until after main is initialized
	// to enable GC, because initializing main registers the GC
	// roots.
	mstats()->enablegc = 1;

	if(runtime_isarchive) {
		// This is not a complete program, but is instead a
		// library built using -buildmode=c-archive or
		// c-shared.  Now that we are initialized, there is
		// nothing further to do.
		return;
	}

	main_main();

	// Make racy client program work: if panicking on
	// another goroutine at the same time as main returns,
	// let the other goroutine finish printing the panic trace.
	// Once it does, it will exit. See issue 3934.
	if(runtime_panicking())
		runtime_park(nil, nil, "panicwait");

	runtime_exit(0);
	for(;;)
		*(int32*)0 = 0;
}

void
runtime_tracebackothers(G * volatile me)
{
	G * volatile gp;
	Traceback tb;
	int32 traceback;
	Slice slice;
	volatile uintptr i;

	tb.gp = me;
	traceback = runtime_gotraceback(nil);
	
	// Show the current goroutine first, if we haven't already.
	if((gp = g->m->curg) != nil && gp != me) {
		runtime_printf("\n");
		runtime_goroutineheader(gp);
		gp->traceback = &tb;

#ifdef USING_SPLIT_STACK
		__splitstack_getcontext(&me->stackcontext[0]);
#endif
		getcontext(ucontext_arg(&me->context[0]));

		if(gp->traceback != nil) {
		  runtime_gogo(gp);
		}

		slice.__values = &tb.locbuf[0];
		slice.__count = tb.c;
		slice.__capacity = tb.c;
		runtime_printtrace(slice, nil);
		runtime_printcreatedby(gp);
	}

	runtime_lock(&allglock);
	for(i = 0; i < runtime_allglen; i++) {
		gp = runtime_allg[i];
		if(gp == me || gp == g->m->curg || gp->atomicstatus == _Gdead)
			continue;
		if(gp->issystem && traceback < 2)
			continue;
		runtime_printf("\n");
		runtime_goroutineheader(gp);

		// Our only mechanism for doing a stack trace is
		// _Unwind_Backtrace.  And that only works for the
		// current thread, not for other random goroutines.
		// So we need to switch context to the goroutine, get
		// the backtrace, and then switch back.

		// This means that if g is running or in a syscall, we
		// can't reliably print a stack trace.  FIXME.

		if(gp->atomicstatus == _Grunning) {
			runtime_printf("\tgoroutine running on other thread; stack unavailable\n");
			runtime_printcreatedby(gp);
		} else if(gp->atomicstatus == _Gsyscall) {
			runtime_printf("\tgoroutine in C code; stack unavailable\n");
			runtime_printcreatedby(gp);
		} else {
			gp->traceback = &tb;

#ifdef USING_SPLIT_STACK
			__splitstack_getcontext(&me->stackcontext[0]);
#endif
			getcontext(ucontext_arg(&me->context[0]));

			if(gp->traceback != nil) {
				runtime_gogo(gp);
			}

			slice.__values = &tb.locbuf[0];
			slice.__count = tb.c;
			slice.__capacity = tb.c;
			runtime_printtrace(slice, nil);
			runtime_printcreatedby(gp);
		}
	}
	runtime_unlock(&allglock);
}

static void
checkmcount(void)
{
	// sched lock is held
	if(runtime_sched->mcount > runtime_sched->maxmcount) {
		runtime_printf("runtime: program exceeds %d-thread limit\n", runtime_sched->maxmcount);
		runtime_throw("thread exhaustion");
	}
}

// Do a stack trace of gp, and then restore the context to
// gp->dotraceback.

static void
gtraceback(G* gp)
{
	Traceback* traceback;

	traceback = gp->traceback;
	gp->traceback = nil;
	if(gp->m != nil)
		runtime_throw("gtraceback: m is not nil");
	gp->m = traceback->gp->m;
	traceback->c = runtime_callers(1, traceback->locbuf,
		sizeof traceback->locbuf / sizeof traceback->locbuf[0], false);
	gp->m = nil;
	runtime_gogo(traceback->gp);
}

static void
mcommoninit(M *mp)
{
	// If there is no mcache runtime_callers() will crash,
	// and we are most likely in sysmon thread so the stack is senseless anyway.
	if(g->m->mcache)
		runtime_callers(1, mp->createstack, nelem(mp->createstack), false);

	mp->fastrand = 0x49f6428aUL + mp->id + runtime_cputicks();

	runtime_lock(&runtime_sched->lock);
	mp->id = runtime_sched->mcount++;
	checkmcount();
	runtime_mpreinit(mp);

	// Add to runtime_allm so garbage collector doesn't free m
	// when it is just in a register or thread-local storage.
	mp->alllink = runtime_allm;
	// runtime_NumCgoCall() iterates over allm w/o schedlock,
	// so we need to publish it safely.
	runtime_atomicstorep(&runtime_allm, mp);
	runtime_unlock(&runtime_sched->lock);
}

// Mark gp ready to run.
void
runtime_ready(G *gp)
{
	// Mark runnable.
	g->m->locks++;  // disable preemption because it can be holding p in a local var
	if(gp->atomicstatus != _Gwaiting) {
		runtime_printf("goroutine %D has status %d\n", gp->goid, gp->atomicstatus);
		runtime_throw("bad g->atomicstatus in ready");
	}
	gp->atomicstatus = _Grunnable;
	runqput((P*)g->m->p, gp);
	if(runtime_atomicload(&runtime_sched->npidle) != 0 && runtime_atomicload(&runtime_sched->nmspinning) == 0)  // TODO: fast atomic
		wakep();
	g->m->locks--;
}

void goready(G*, int) __asm__ (GOSYM_PREFIX "runtime.goready");

void
goready(G* gp, int traceskip __attribute__ ((unused)))
{
	runtime_ready(gp);
}

int32
runtime_gcprocs(void)
{
	int32 n;

	// Figure out how many CPUs to use during GC.
	// Limited by gomaxprocs, number of actual CPUs, and MaxGcproc.
	runtime_lock(&runtime_sched->lock);
	n = runtime_gomaxprocs;
	if(n > runtime_ncpu)
		n = runtime_ncpu > 0 ? runtime_ncpu : 1;
	if(n > MaxGcproc)
		n = MaxGcproc;
	if(n > runtime_sched->nmidle+1) // one M is currently running
		n = runtime_sched->nmidle+1;
	runtime_unlock(&runtime_sched->lock);
	return n;
}

static bool
needaddgcproc(void)
{
	int32 n;

	runtime_lock(&runtime_sched->lock);
	n = runtime_gomaxprocs;
	if(n > runtime_ncpu)
		n = runtime_ncpu;
	if(n > MaxGcproc)
		n = MaxGcproc;
	n -= runtime_sched->nmidle+1; // one M is currently running
	runtime_unlock(&runtime_sched->lock);
	return n > 0;
}

void
runtime_helpgc(int32 nproc)
{
	M *mp;
	int32 n, pos;

	runtime_lock(&runtime_sched->lock);
	pos = 0;
	for(n = 1; n < nproc; n++) {  // one M is currently running
		if(runtime_allp[pos]->mcache == g->m->mcache)
			pos++;
		mp = mget();
		if(mp == nil)
			runtime_throw("runtime_gcprocs inconsistency");
		mp->helpgc = n;
		mp->mcache = runtime_allp[pos]->mcache;
		pos++;
		runtime_notewakeup(&mp->park);
	}
	runtime_unlock(&runtime_sched->lock);
}

// Similar to stoptheworld but best-effort and can be called several times.
// There is no reverse operation, used during crashing.
// This function must not lock any mutexes.
void
runtime_freezetheworld(void)
{
	int32 i;

	if(runtime_gomaxprocs == 1)
		return;
	// stopwait and preemption requests can be lost
	// due to races with concurrently executing threads,
	// so try several times
	for(i = 0; i < 5; i++) {
		// this should tell the scheduler to not start any new goroutines
		runtime_sched->stopwait = 0x7fffffff;
		runtime_atomicstore((uint32*)&runtime_sched->gcwaiting, 1);
		// this should stop running goroutines
		if(!preemptall())
			break;  // no running goroutines
		runtime_usleep(1000);
	}
	// to be sure
	runtime_usleep(1000);
	preemptall();
	runtime_usleep(1000);
}

void
runtime_stopTheWorldWithSema(void)
{
	int32 i;
	uint32 s;
	P *p;
	bool wait;

	runtime_lock(&runtime_sched->lock);
	runtime_sched->stopwait = runtime_gomaxprocs;
	runtime_atomicstore((uint32*)&runtime_sched->gcwaiting, 1);
	preemptall();
	// stop current P
	((P*)g->m->p)->status = _Pgcstop;
	runtime_sched->stopwait--;
	// try to retake all P's in _Psyscall status
	for(i = 0; i < runtime_gomaxprocs; i++) {
		p = runtime_allp[i];
		s = p->status;
		if(s == _Psyscall && runtime_cas(&p->status, s, _Pgcstop))
			runtime_sched->stopwait--;
	}
	// stop idle P's
	while((p = pidleget()) != nil) {
		p->status = _Pgcstop;
		runtime_sched->stopwait--;
	}
	wait = runtime_sched->stopwait > 0;
	runtime_unlock(&runtime_sched->lock);

	// wait for remaining P's to stop voluntarily
	if(wait) {
		runtime_notesleep(&runtime_sched->stopnote);
		runtime_noteclear(&runtime_sched->stopnote);
	}
	if(runtime_sched->stopwait)
		runtime_throw("stoptheworld: not stopped");
	for(i = 0; i < runtime_gomaxprocs; i++) {
		p = runtime_allp[i];
		if(p->status != _Pgcstop)
			runtime_throw("stoptheworld: not stopped");
	}
}

static void
mhelpgc(void)
{
	g->m->helpgc = -1;
}

void
runtime_startTheWorldWithSema(void)
{
	P *p, *p1;
	M *mp;
	G *gp;
	bool add;

	g->m->locks++;  // disable preemption because it can be holding p in a local var
	gp = runtime_netpoll(false);  // non-blocking
	injectglist(gp);
	add = needaddgcproc();
	runtime_lock(&runtime_sched->lock);
	if(newprocs) {
		procresize(newprocs);
		newprocs = 0;
	} else
		procresize(runtime_gomaxprocs);
	runtime_sched->gcwaiting = 0;

	p1 = nil;
	while((p = pidleget()) != nil) {
		// procresize() puts p's with work at the beginning of the list.
		// Once we reach a p without a run queue, the rest don't have one either.
		if(p->runqhead == p->runqtail) {
			pidleput(p);
			break;
		}
		p->m = (uintptr)mget();
		p->link = (uintptr)p1;
		p1 = p;
	}
	if(runtime_sched->sysmonwait) {
		runtime_sched->sysmonwait = false;
		runtime_notewakeup(&runtime_sched->sysmonnote);
	}
	runtime_unlock(&runtime_sched->lock);

	while(p1) {
		p = p1;
		p1 = (P*)p1->link;
		if(p->m) {
			mp = (M*)p->m;
			p->m = 0;
			if(mp->nextp)
				runtime_throw("startTheWorldWithSema: inconsistent mp->nextp");
			mp->nextp = (uintptr)p;
			runtime_notewakeup(&mp->park);
		} else {
			// Start M to run P.  Do not start another M below.
			newm(nil, p);
			add = false;
		}
	}

	if(add) {
		// If GC could have used another helper proc, start one now,
		// in the hope that it will be available next time.
		// It would have been even better to start it before the collection,
		// but doing so requires allocating memory, so it's tricky to
		// coordinate.  This lazy approach works out in practice:
		// we don't mind if the first couple gc rounds don't have quite
		// the maximum number of procs.
		newm(mhelpgc, nil);
	}
	g->m->locks--;
}

// Called to start an M.
void*
runtime_mstart(void* mp)
{
	M *m;

	m = (M*)mp;
	g = m->g0;
	g->m = m;

	initcontext();

	g->entry = nil;
	g->param = nil;

	// Record top of stack for use by mcall.
	// Once we call schedule we're never coming back,
	// so other calls can reuse this stack space.
#ifdef USING_SPLIT_STACK
	__splitstack_getcontext(&g->stackcontext[0]);
#else
	g->gcinitialsp = &mp;
	// Setting gcstacksize to 0 is a marker meaning that gcinitialsp
	// is the top of the stack, not the bottom.
	g->gcstacksize = 0;
	g->gcnextsp = &mp;
#endif
	getcontext(ucontext_arg(&g->context[0]));

	if(g->entry != nil) {
		// Got here from mcall.
		void (*pfn)(G*) = (void (*)(G*))g->entry;
		G* gp = (G*)g->param;
		pfn(gp);
		*(int*)0x21 = 0x21;
	}
	runtime_minit();

#ifdef USING_SPLIT_STACK
	{
		int dont_block_signals = 0;
		__splitstack_block_signals(&dont_block_signals, nil);
	}
#endif

	// Install signal handlers; after minit so that minit can
	// prepare the thread to be able to handle the signals.
	if(m == &runtime_m0) {
		if(runtime_iscgo && !runtime_cgoHasExtraM) {
			runtime_cgoHasExtraM = true;
			runtime_newextram();
			runtime_needextram = 0;
		}
		runtime_initsig(false);
	}
	
	if(m->mstartfn)
		((void (*)(void))m->mstartfn)();

	if(m->helpgc) {
		m->helpgc = 0;
		stopm();
	} else if(m != &runtime_m0) {
		acquirep((P*)m->nextp);
		m->nextp = 0;
	}
	schedule();

	// TODO(brainman): This point is never reached, because scheduler
	// does not release os threads at the moment. But once this path
	// is enabled, we must remove our seh here.

	return nil;
}

typedef struct CgoThreadStart CgoThreadStart;
struct CgoThreadStart
{
	M *m;
	G *g;
	uintptr *tls;
	void (*fn)(void);
};

// Allocate a new m unassociated with any thread.
// Can use p for allocation context if needed.
M*
runtime_allocm(P *p, int32 stacksize, byte** ret_g0_stack, uintptr* ret_g0_stacksize)
{
	M *mp;

	g->m->locks++;  // disable GC because it can be called from sysmon
	if(g->m->p == 0)
		acquirep(p);  // temporarily borrow p for mallocs in this function
#if 0
	if(mtype == nil) {
		Eface e;
		runtime_gc_m_ptr(&e);
		mtype = ((const PtrType*)e.__type_descriptor)->__element_type;
	}
#endif

	mp = runtime_mal(sizeof *mp);
	mcommoninit(mp);
	mp->g0 = runtime_malg(stacksize, ret_g0_stack, ret_g0_stacksize);
	mp->g0->m = mp;

	if(p == (P*)g->m->p)
		releasep();
	g->m->locks--;

	return mp;
}

static G*
allocg(void)
{
	G *gp;
	// static Type *gtype;
	
	// if(gtype == nil) {
	// 	Eface e;
	// 	runtime_gc_g_ptr(&e);
	// 	gtype = ((PtrType*)e.__type_descriptor)->__element_type;
	// }
	// gp = runtime_cnew(gtype);
	gp = runtime_malloc(sizeof(G));
	return gp;
}

static M* lockextra(bool nilokay);
static void unlockextra(M*);

// needm is called when a cgo callback happens on a
// thread without an m (a thread not created by Go).
// In this case, needm is expected to find an m to use
// and return with m, g initialized correctly.
// Since m and g are not set now (likely nil, but see below)
// needm is limited in what routines it can call. In particular
// it can only call nosplit functions (textflag 7) and cannot
// do any scheduling that requires an m.
//
// In order to avoid needing heavy lifting here, we adopt
// the following strategy: there is a stack of available m's
// that can be stolen. Using compare-and-swap
// to pop from the stack has ABA races, so we simulate
// a lock by doing an exchange (via casp) to steal the stack
// head and replace the top pointer with MLOCKED (1).
// This serves as a simple spin lock that we can use even
// without an m. The thread that locks the stack in this way
// unlocks the stack by storing a valid stack head pointer.
//
// In order to make sure that there is always an m structure
// available to be stolen, we maintain the invariant that there
// is always one more than needed. At the beginning of the
// program (if cgo is in use) the list is seeded with a single m.
// If needm finds that it has taken the last m off the list, its job
// is - once it has installed its own m so that it can do things like
// allocate memory - to create a spare m and put it on the list.
//
// Each of these extra m's also has a g0 and a curg that are
// pressed into service as the scheduling stack and current
// goroutine for the duration of the cgo callback.
//
// When the callback is done with the m, it calls dropm to
// put the m back on the list.
//
// Unlike the gc toolchain, we start running on curg, since we are
// just going to return and let the caller continue.
void
runtime_needm(void)
{
	M *mp;

	if(runtime_needextram) {
		// Can happen if C/C++ code calls Go from a global ctor.
		// Can not throw, because scheduler is not initialized yet.
		int rv __attribute__((unused));
		rv = runtime_write(2, "fatal error: cgo callback before cgo call\n",
			sizeof("fatal error: cgo callback before cgo call\n")-1);
		runtime_exit(1);
	}

	// Lock extra list, take head, unlock popped list.
	// nilokay=false is safe here because of the invariant above,
	// that the extra list always contains or will soon contain
	// at least one m.
	mp = lockextra(false);

	// Set needextram when we've just emptied the list,
	// so that the eventual call into cgocallbackg will
	// allocate a new m for the extra list. We delay the
	// allocation until then so that it can be done
	// after exitsyscall makes sure it is okay to be
	// running at all (that is, there's no garbage collection
	// running right now).
	mp->needextram = mp->schedlink == 0;
	unlockextra((M*)mp->schedlink);

	// Install g (= m->curg).
	runtime_setg(mp->curg);

	// Initialize g's context as in mstart.
	initcontext();
	g->atomicstatus = _Gsyscall;
	g->entry = nil;
	g->param = nil;
#ifdef USING_SPLIT_STACK
	__splitstack_getcontext(&g->stackcontext[0]);
#else
	g->gcinitialsp = &mp;
	g->gcstack = nil;
	g->gcstacksize = 0;
	g->gcnextsp = &mp;
#endif
	getcontext(ucontext_arg(&g->context[0]));

	if(g->entry != nil) {
		// Got here from mcall.
		void (*pfn)(G*) = (void (*)(G*))g->entry;
		G* gp = (G*)g->param;
		pfn(gp);
		*(int*)0x22 = 0x22;
	}

	// Initialize this thread to use the m.
	runtime_minit();

#ifdef USING_SPLIT_STACK
	{
		int dont_block_signals = 0;
		__splitstack_block_signals(&dont_block_signals, nil);
	}
#endif
}

// newextram allocates an m and puts it on the extra list.
// It is called with a working local m, so that it can do things
// like call schedlock and allocate.
void
runtime_newextram(void)
{
	M *mp, *mnext;
	G *gp;
	byte *g0_sp, *sp;
	uintptr g0_spsize, spsize;
	ucontext_t *uc;

	// Create extra goroutine locked to extra m.
	// The goroutine is the context in which the cgo callback will run.
	// The sched.pc will never be returned to, but setting it to
	// runtime.goexit makes clear to the traceback routines where
	// the goroutine stack ends.
	mp = runtime_allocm(nil, StackMin, &g0_sp, &g0_spsize);
	gp = runtime_malg(StackMin, &sp, &spsize);
	gp->atomicstatus = _Gdead;
	gp->m = mp;
	mp->curg = gp;
	mp->locked = _LockInternal;
	mp->lockedg = gp;
	gp->lockedm = mp;
	gp->goid = runtime_xadd64(&runtime_sched->goidgen, 1);
	// put on allg for garbage collector
	allgadd(gp);

	// The context for gp will be set up in runtime_needm.  But
	// here we need to set up the context for g0.
	uc = ucontext_arg(&mp->g0->context[0]);
	getcontext(uc);
	uc->uc_stack.ss_sp = g0_sp;
	uc->uc_stack.ss_size = (size_t)g0_spsize;
	makecontext(uc, kickoff, 0);

	// Add m to the extra list.
	mnext = lockextra(true);
	mp->schedlink = (uintptr)mnext;
	unlockextra(mp);
}

// dropm is called when a cgo callback has called needm but is now
// done with the callback and returning back into the non-Go thread.
// It puts the current m back onto the extra list.
//
// The main expense here is the call to signalstack to release the
// m's signal stack, and then the call to needm on the next callback
// from this thread. It is tempting to try to save the m for next time,
// which would eliminate both these costs, but there might not be
// a next time: the current thread (which Go does not control) might exit.
// If we saved the m for that thread, there would be an m leak each time
// such a thread exited. Instead, we acquire and release an m on each
// call. These should typically not be scheduling operations, just a few
// atomics, so the cost should be small.
//
// TODO(rsc): An alternative would be to allocate a dummy pthread per-thread
// variable using pthread_key_create. Unlike the pthread keys we already use
// on OS X, this dummy key would never be read by Go code. It would exist
// only so that we could register at thread-exit-time destructor.
// That destructor would put the m back onto the extra list.
// This is purely a performance optimization. The current version,
// in which dropm happens on each cgo call, is still correct too.
// We may have to keep the current version on systems with cgo
// but without pthreads, like Windows.
void
runtime_dropm(void)
{
	M *mp, *mnext;

	// Undo whatever initialization minit did during needm.
	runtime_unminit();

	// Clear m and g, and return m to the extra list.
	// After the call to setg we can only call nosplit functions.
	mp = g->m;
	runtime_setg(nil);

	mp->curg->atomicstatus = _Gdead;
	mp->curg->gcstack = nil;
	mp->curg->gcnextsp = nil;

	mnext = lockextra(true);
	mp->schedlink = (uintptr)mnext;
	unlockextra(mp);
}

#define MLOCKED ((M*)1)

// lockextra locks the extra list and returns the list head.
// The caller must unlock the list by storing a new list head
// to runtime.extram. If nilokay is true, then lockextra will
// return a nil list head if that's what it finds. If nilokay is false,
// lockextra will keep waiting until the list head is no longer nil.
static M*
lockextra(bool nilokay)
{
	M *mp;
	void (*yield)(void);

	for(;;) {
		mp = runtime_atomicloadp(&runtime_extram);
		if(mp == MLOCKED) {
			yield = runtime_osyield;
			yield();
			continue;
		}
		if(mp == nil && !nilokay) {
			runtime_usleep(1);
			continue;
		}
		if(!runtime_casp(&runtime_extram, mp, MLOCKED)) {
			yield = runtime_osyield;
			yield();
			continue;
		}
		break;
	}
	return mp;
}

static void
unlockextra(M *mp)
{
	runtime_atomicstorep(&runtime_extram, mp);
}

static int32
countextra()
{
	M *mp, *mc;
	int32 c;

	for(;;) {
		mp = runtime_atomicloadp(&runtime_extram);
		if(mp == MLOCKED) {
			runtime_osyield();
			continue;
		}
		if(!runtime_casp(&runtime_extram, mp, MLOCKED)) {
			runtime_osyield();
			continue;
		}
		c = 0;
		for(mc = mp; mc != nil; mc = (M*)mc->schedlink)
			c++;
		runtime_atomicstorep(&runtime_extram, mp);
		return c;
	}
}

// Create a new m.  It will start off with a call to fn, or else the scheduler.
static void
newm(void(*fn)(void), P *p)
{
	M *mp;

	mp = runtime_allocm(p, -1, nil, nil);
	mp->nextp = (uintptr)p;
	mp->mstartfn = (uintptr)(void*)fn;

	runtime_newosproc(mp);
}

// Stops execution of the current m until new work is available.
// Returns with acquired P.
static void
stopm(void)
{
	M* m;

	m = g->m;
	if(m->locks)
		runtime_throw("stopm holding locks");
	if(m->p)
		runtime_throw("stopm holding p");
	if(m->spinning) {
		m->spinning = false;
		runtime_xadd(&runtime_sched->nmspinning, -1);
	}

retry:
	runtime_lock(&runtime_sched->lock);
	mput(m);
	runtime_unlock(&runtime_sched->lock);
	runtime_notesleep(&m->park);
	m = g->m;
	runtime_noteclear(&m->park);
	if(m->helpgc) {
		runtime_gchelper();
		m->helpgc = 0;
		m->mcache = nil;
		goto retry;
	}
	acquirep((P*)m->nextp);
	m->nextp = 0;
}

static void
mspinning(void)
{
	g->m->spinning = true;
}

// Schedules some M to run the p (creates an M if necessary).
// If p==nil, tries to get an idle P, if no idle P's does nothing.
static void
startm(P *p, bool spinning)
{
	M *mp;
	void (*fn)(void);

	runtime_lock(&runtime_sched->lock);
	if(p == nil) {
		p = pidleget();
		if(p == nil) {
			runtime_unlock(&runtime_sched->lock);
			if(spinning)
				runtime_xadd(&runtime_sched->nmspinning, -1);
			return;
		}
	}
	mp = mget();
	runtime_unlock(&runtime_sched->lock);
	if(mp == nil) {
		fn = nil;
		if(spinning)
			fn = mspinning;
		newm(fn, p);
		return;
	}
	if(mp->spinning)
		runtime_throw("startm: m is spinning");
	if(mp->nextp)
		runtime_throw("startm: m has p");
	mp->spinning = spinning;
	mp->nextp = (uintptr)p;
	runtime_notewakeup(&mp->park);
}

// Hands off P from syscall or locked M.
static void
handoffp(P *p)
{
	// if it has local work, start it straight away
	if(p->runqhead != p->runqtail || runtime_sched->runqsize) {
		startm(p, false);
		return;
	}
	// no local work, check that there are no spinning/idle M's,
	// otherwise our help is not required
	if(runtime_atomicload(&runtime_sched->nmspinning) + runtime_atomicload(&runtime_sched->npidle) == 0 &&  // TODO: fast atomic
		runtime_cas(&runtime_sched->nmspinning, 0, 1)) {
		startm(p, true);
		return;
	}
	runtime_lock(&runtime_sched->lock);
	if(runtime_sched->gcwaiting) {
		p->status = _Pgcstop;
		if(--runtime_sched->stopwait == 0)
			runtime_notewakeup(&runtime_sched->stopnote);
		runtime_unlock(&runtime_sched->lock);
		return;
	}
	if(runtime_sched->runqsize) {
		runtime_unlock(&runtime_sched->lock);
		startm(p, false);
		return;
	}
	// If this is the last running P and nobody is polling network,
	// need to wakeup another M to poll network.
	if(runtime_sched->npidle == (uint32)runtime_gomaxprocs-1 && runtime_atomicload64(&runtime_sched->lastpoll) != 0) {
		runtime_unlock(&runtime_sched->lock);
		startm(p, false);
		return;
	}
	pidleput(p);
	runtime_unlock(&runtime_sched->lock);
}

// Tries to add one more P to execute G's.
// Called when a G is made runnable (newproc, ready).
static void
wakep(void)
{
	// be conservative about spinning threads
	if(!runtime_cas(&runtime_sched->nmspinning, 0, 1))
		return;
	startm(nil, true);
}

// Stops execution of the current m that is locked to a g until the g is runnable again.
// Returns with acquired P.
static void
stoplockedm(void)
{
	M *m;
	P *p;

	m = g->m;
	if(m->lockedg == nil || m->lockedg->lockedm != m)
		runtime_throw("stoplockedm: inconsistent locking");
	if(m->p) {
		// Schedule another M to run this p.
		p = releasep();
		handoffp(p);
	}
	incidlelocked(1);
	// Wait until another thread schedules lockedg again.
	runtime_notesleep(&m->park);
	m = g->m;
	runtime_noteclear(&m->park);
	if(m->lockedg->atomicstatus != _Grunnable)
		runtime_throw("stoplockedm: not runnable");
	acquirep((P*)m->nextp);
	m->nextp = 0;
}

// Schedules the locked m to run the locked gp.
static void
startlockedm(G *gp)
{
	M *mp;
	P *p;

	mp = gp->lockedm;
	if(mp == g->m)
		runtime_throw("startlockedm: locked to me");
	if(mp->nextp)
		runtime_throw("startlockedm: m has p");
	// directly handoff current P to the locked m
	incidlelocked(-1);
	p = releasep();
	mp->nextp = (uintptr)p;
	runtime_notewakeup(&mp->park);
	stopm();
}

// Stops the current m for stoptheworld.
// Returns when the world is restarted.
static void
gcstopm(void)
{
	P *p;

	if(!runtime_sched->gcwaiting)
		runtime_throw("gcstopm: not waiting for gc");
	if(g->m->spinning) {
		g->m->spinning = false;
		runtime_xadd(&runtime_sched->nmspinning, -1);
	}
	p = releasep();
	runtime_lock(&runtime_sched->lock);
	p->status = _Pgcstop;
	if(--runtime_sched->stopwait == 0)
		runtime_notewakeup(&runtime_sched->stopnote);
	runtime_unlock(&runtime_sched->lock);
	stopm();
}

// Schedules gp to run on the current M.
// Never returns.
static void
execute(G *gp)
{
	int32 hz;

	if(gp->atomicstatus != _Grunnable) {
		runtime_printf("execute: bad g status %d\n", gp->atomicstatus);
		runtime_throw("execute: bad g status");
	}
	gp->atomicstatus = _Grunning;
	gp->waitsince = 0;
	((P*)g->m->p)->schedtick++;
	g->m->curg = gp;
	gp->m = g->m;

	// Check whether the profiler needs to be turned on or off.
	hz = runtime_sched->profilehz;
	if(g->m->profilehz != hz)
		runtime_resetcpuprofiler(hz);

	runtime_gogo(gp);
}

// Finds a runnable goroutine to execute.
// Tries to steal from other P's, get g from global queue, poll network.
static G*
findrunnable(void)
{
	G *gp;
	P *p;
	int32 i;

top:
	if(runtime_sched->gcwaiting) {
		gcstopm();
		goto top;
	}
	if(runtime_fingwait && runtime_fingwake && (gp = runtime_wakefing()) != nil)
		runtime_ready(gp);
	// local runq
	gp = runqget((P*)g->m->p);
	if(gp)
		return gp;
	// global runq
	if(runtime_sched->runqsize) {
		runtime_lock(&runtime_sched->lock);
		gp = globrunqget((P*)g->m->p, 0);
		runtime_unlock(&runtime_sched->lock);
		if(gp)
			return gp;
	}
	// poll network
	gp = runtime_netpoll(false);  // non-blocking
	if(gp) {
		injectglist((G*)gp->schedlink);
		gp->atomicstatus = _Grunnable;
		return gp;
	}
	// If number of spinning M's >= number of busy P's, block.
	// This is necessary to prevent excessive CPU consumption
	// when GOMAXPROCS>>1 but the program parallelism is low.
	if(!g->m->spinning && 2 * runtime_atomicload(&runtime_sched->nmspinning) >= runtime_gomaxprocs - runtime_atomicload(&runtime_sched->npidle))  // TODO: fast atomic
		goto stop;
	if(!g->m->spinning) {
		g->m->spinning = true;
		runtime_xadd(&runtime_sched->nmspinning, 1);
	}
	// random steal from other P's
	for(i = 0; i < 2*runtime_gomaxprocs; i++) {
		if(runtime_sched->gcwaiting)
			goto top;
		p = runtime_allp[runtime_fastrand1()%runtime_gomaxprocs];
		if(p == (P*)g->m->p)
			gp = runqget(p);
		else
			gp = runqsteal((P*)g->m->p, p);
		if(gp)
			return gp;
	}
stop:
	// return P and block
	runtime_lock(&runtime_sched->lock);
	if(runtime_sched->gcwaiting) {
		runtime_unlock(&runtime_sched->lock);
		goto top;
	}
	if(runtime_sched->runqsize) {
		gp = globrunqget((P*)g->m->p, 0);
		runtime_unlock(&runtime_sched->lock);
		return gp;
	}
	p = releasep();
	pidleput(p);
	runtime_unlock(&runtime_sched->lock);
	if(g->m->spinning) {
		g->m->spinning = false;
		runtime_xadd(&runtime_sched->nmspinning, -1);
	}
	// check all runqueues once again
	for(i = 0; i < runtime_gomaxprocs; i++) {
		p = runtime_allp[i];
		if(p && p->runqhead != p->runqtail) {
			runtime_lock(&runtime_sched->lock);
			p = pidleget();
			runtime_unlock(&runtime_sched->lock);
			if(p) {
				acquirep(p);
				goto top;
			}
			break;
		}
	}
	// poll network
	if(runtime_xchg64(&runtime_sched->lastpoll, 0) != 0) {
		if(g->m->p)
			runtime_throw("findrunnable: netpoll with p");
		if(g->m->spinning)
			runtime_throw("findrunnable: netpoll with spinning");
		gp = runtime_netpoll(true);  // block until new work is available
		runtime_atomicstore64(&runtime_sched->lastpoll, runtime_nanotime());
		if(gp) {
			runtime_lock(&runtime_sched->lock);
			p = pidleget();
			runtime_unlock(&runtime_sched->lock);
			if(p) {
				acquirep(p);
				injectglist((G*)gp->schedlink);
				gp->atomicstatus = _Grunnable;
				return gp;
			}
			injectglist(gp);
		}
	}
	stopm();
	goto top;
}

static void
resetspinning(void)
{
	int32 nmspinning;

	if(g->m->spinning) {
		g->m->spinning = false;
		nmspinning = runtime_xadd(&runtime_sched->nmspinning, -1);
		if(nmspinning < 0)
			runtime_throw("findrunnable: negative nmspinning");
	} else
		nmspinning = runtime_atomicload(&runtime_sched->nmspinning);

	// M wakeup policy is deliberately somewhat conservative (see nmspinning handling),
	// so see if we need to wakeup another P here.
	if (nmspinning == 0 && runtime_atomicload(&runtime_sched->npidle) > 0)
		wakep();
}

// Injects the list of runnable G's into the scheduler.
// Can run concurrently with GC.
static void
injectglist(G *glist)
{
	int32 n;
	G *gp;

	if(glist == nil)
		return;
	runtime_lock(&runtime_sched->lock);
	for(n = 0; glist; n++) {
		gp = glist;
		glist = (G*)gp->schedlink;
		gp->atomicstatus = _Grunnable;
		globrunqput(gp);
	}
	runtime_unlock(&runtime_sched->lock);

	for(; n && runtime_sched->npidle; n--)
		startm(nil, false);
}

// One round of scheduler: find a runnable goroutine and execute it.
// Never returns.
static void
schedule(void)
{
	G *gp;
	uint32 tick;

	if(g->m->locks)
		runtime_throw("schedule: holding locks");

top:
	if(runtime_sched->gcwaiting) {
		gcstopm();
		goto top;
	}

	gp = nil;
	// Check the global runnable queue once in a while to ensure fairness.
	// Otherwise two goroutines can completely occupy the local runqueue
	// by constantly respawning each other.
	tick = ((P*)g->m->p)->schedtick;
	// This is a fancy way to say tick%61==0,
	// it uses 2 MUL instructions instead of a single DIV and so is faster on modern processors.
	if(tick - (((uint64)tick*0x4325c53fu)>>36)*61 == 0 && runtime_sched->runqsize > 0) {
		runtime_lock(&runtime_sched->lock);
		gp = globrunqget((P*)g->m->p, 1);
		runtime_unlock(&runtime_sched->lock);
		if(gp)
			resetspinning();
	}
	if(gp == nil) {
		gp = runqget((P*)g->m->p);
		if(gp && g->m->spinning)
			runtime_throw("schedule: spinning with local work");
	}
	if(gp == nil) {
		gp = findrunnable();  // blocks until work is available
		resetspinning();
	}

	if(gp->lockedm) {
		// Hands off own p to the locked m,
		// then blocks waiting for a new p.
		startlockedm(gp);
		goto top;
	}

	execute(gp);
}

// Puts the current goroutine into a waiting state and calls unlockf.
// If unlockf returns false, the goroutine is resumed.
void
runtime_park(bool(*unlockf)(G*, void*), void *lock, const char *reason)
{
	if(g->atomicstatus != _Grunning)
		runtime_throw("bad g status");
	g->m->waitlock = lock;
	g->m->waitunlockf = unlockf;
	g->waitreason = runtime_gostringnocopy((const byte*)reason);
	runtime_mcall(park0);
}

void gopark(FuncVal *, void *, String, byte, int)
  __asm__ ("runtime.gopark");

void
gopark(FuncVal *unlockf, void *lock, String reason,
       byte traceEv __attribute__ ((unused)),
       int traceskip __attribute__ ((unused)))
{
	if(g->atomicstatus != _Grunning)
		runtime_throw("bad g status");
	g->m->waitlock = lock;
	g->m->waitunlockf = unlockf == nil ? nil : (void*)unlockf->fn;
	g->waitreason = reason;
	runtime_mcall(park0);
}

static bool
parkunlock(G *gp, void *lock)
{
	USED(gp);
	runtime_unlock(lock);
	return true;
}

// Puts the current goroutine into a waiting state and unlocks the lock.
// The goroutine can be made runnable again by calling runtime_ready(gp).
void
runtime_parkunlock(Lock *lock, const char *reason)
{
	runtime_park(parkunlock, lock, reason);
}

void goparkunlock(Lock *, String, byte, int)
  __asm__ (GOSYM_PREFIX "runtime.goparkunlock");

void
goparkunlock(Lock *lock, String reason, byte traceEv __attribute__ ((unused)),
	     int traceskip __attribute__ ((unused)))
{
	if(g->atomicstatus != _Grunning)
		runtime_throw("bad g status");
	g->m->waitlock = lock;
	g->m->waitunlockf = parkunlock;
	g->waitreason = reason;
	runtime_mcall(park0);
}

// runtime_park continuation on g0.
static void
park0(G *gp)
{
	M *m;
	bool ok;

	m = g->m;
	gp->atomicstatus = _Gwaiting;
	gp->m = nil;
	m->curg = nil;
	if(m->waitunlockf) {
		ok = ((bool (*)(G*, void*))m->waitunlockf)(gp, m->waitlock);
		m->waitunlockf = nil;
		m->waitlock = nil;
		if(!ok) {
			gp->atomicstatus = _Grunnable;
			execute(gp);  // Schedule it back, never returns.
		}
	}
	if(m->lockedg) {
		stoplockedm();
		execute(gp);  // Never returns.
	}
	schedule();
}

// Scheduler yield.
void
runtime_gosched(void)
{
	if(g->atomicstatus != _Grunning)
		runtime_throw("bad g status");
	runtime_mcall(runtime_gosched0);
}

// runtime_gosched continuation on g0.
void
runtime_gosched0(G *gp)
{
	M *m;

	m = g->m;
	gp->atomicstatus = _Grunnable;
	gp->m = nil;
	m->curg = nil;
	runtime_lock(&runtime_sched->lock);
	globrunqput(gp);
	runtime_unlock(&runtime_sched->lock);
	if(m->lockedg) {
		stoplockedm();
		execute(gp);  // Never returns.
	}
	schedule();
}

// Finishes execution of the current goroutine.
// Need to mark it as nosplit, because it runs with sp > stackbase (as runtime_lessstack).
// Since it does not return it does not matter.  But if it is preempted
// at the split stack check, GC will complain about inconsistent sp.
void runtime_goexit1(void) __attribute__ ((noinline));
void
runtime_goexit1(void)
{
	if(g->atomicstatus != _Grunning)
		runtime_throw("bad g status");
	runtime_mcall(goexit0);
}

// runtime_goexit1 continuation on g0.
static void
goexit0(G *gp)
{
	M *m;

	m = g->m;
	gp->atomicstatus = _Gdead;
	gp->entry = nil;
	gp->m = nil;
	gp->lockedm = nil;
	gp->paniconfault = 0;
	gp->_defer = nil; // should be true already but just in case.
	gp->_panic = nil; // non-nil for Goexit during panic. points at stack-allocated data.
	gp->writebuf.__values = nil;
	gp->writebuf.__count = 0;
	gp->writebuf.__capacity = 0;
	gp->waitreason = runtime_gostringnocopy(nil);
	gp->param = nil;
	m->curg = nil;
	m->lockedg = nil;
	if(m->locked & ~_LockExternal) {
		runtime_printf("invalid m->locked = %d\n", m->locked);
		runtime_throw("internal lockOSThread error");
	}	
	m->locked = 0;
	gfput((P*)m->p, gp);
	schedule();
}

// The goroutine g is about to enter a system call.
// Record that it's not using the cpu anymore.
// This is called only from the go syscall library and cgocall,
// not from the low-level system calls used by the runtime.
//
// Entersyscall cannot split the stack: the runtime_gosave must
// make g->sched refer to the caller's stack segment, because
// entersyscall is going to return immediately after.

void runtime_entersyscall(int32) __attribute__ ((no_split_stack));
static void doentersyscall(uintptr, uintptr)
  __attribute__ ((no_split_stack, noinline));

void
runtime_entersyscall(int32 dummy __attribute__ ((unused)))
{
	// Save the registers in the g structure so that any pointers
	// held in registers will be seen by the garbage collector.
	getcontext(ucontext_arg(&g->gcregs[0]));

	// Do the work in a separate function, so that this function
	// doesn't save any registers on its own stack.  If this
	// function does save any registers, we might store the wrong
	// value in the call to getcontext.
	//
	// FIXME: This assumes that we do not need to save any
	// callee-saved registers to access the TLS variable g.  We
	// don't want to put the ucontext_t on the stack because it is
	// large and we can not split the stack here.
	doentersyscall((uintptr)runtime_getcallerpc(&dummy),
		       (uintptr)runtime_getcallersp(&dummy));
}

static void
doentersyscall(uintptr pc, uintptr sp)
{
	// Disable preemption because during this function g is in _Gsyscall status,
	// but can have inconsistent g->sched, do not let GC observe it.
	g->m->locks++;

	// Leave SP around for GC and traceback.
#ifdef USING_SPLIT_STACK
	{
	  size_t gcstacksize;
	  g->gcstack = __splitstack_find(nil, nil, &gcstacksize,
					 &g->gcnextsegment, &g->gcnextsp,
					 &g->gcinitialsp);
	  g->gcstacksize = (uintptr)gcstacksize;
	}
#else
	{
		void *v;

		g->gcnextsp = (byte *) &v;
	}
#endif

	g->syscallsp = sp;
	g->syscallpc = pc;

	g->atomicstatus = _Gsyscall;

	if(runtime_atomicload(&runtime_sched->sysmonwait)) {  // TODO: fast atomic
		runtime_lock(&runtime_sched->lock);
		if(runtime_atomicload(&runtime_sched->sysmonwait)) {
			runtime_atomicstore(&runtime_sched->sysmonwait, 0);
			runtime_notewakeup(&runtime_sched->sysmonnote);
		}
		runtime_unlock(&runtime_sched->lock);
	}

	g->m->mcache = nil;
	((P*)(g->m->p))->m = 0;
	runtime_atomicstore(&((P*)g->m->p)->status, _Psyscall);
	if(runtime_atomicload(&runtime_sched->gcwaiting)) {
		runtime_lock(&runtime_sched->lock);
		if (runtime_sched->stopwait > 0 && runtime_cas(&((P*)g->m->p)->status, _Psyscall, _Pgcstop)) {
			if(--runtime_sched->stopwait == 0)
				runtime_notewakeup(&runtime_sched->stopnote);
		}
		runtime_unlock(&runtime_sched->lock);
	}

	g->m->locks--;
}

// The same as runtime_entersyscall(), but with a hint that the syscall is blocking.
void
runtime_entersyscallblock(int32 dummy __attribute__ ((unused)))
{
	P *p;

	g->m->locks++;  // see comment in entersyscall

	// Leave SP around for GC and traceback.
#ifdef USING_SPLIT_STACK
	{
	  size_t gcstacksize;
	  g->gcstack = __splitstack_find(nil, nil, &gcstacksize,
					 &g->gcnextsegment, &g->gcnextsp,
					 &g->gcinitialsp);
	  g->gcstacksize = (uintptr)gcstacksize;
	}
#else
	g->gcnextsp = (byte *) &p;
#endif

	// Save the registers in the g structure so that any pointers
	// held in registers will be seen by the garbage collector.
	getcontext(ucontext_arg(&g->gcregs[0]));

	g->syscallpc = (uintptr)runtime_getcallerpc(&dummy);
	g->syscallsp = (uintptr)runtime_getcallersp(&dummy);

	g->atomicstatus = _Gsyscall;

	p = releasep();
	handoffp(p);
	if(g->isbackground)  // do not consider blocked scavenger for deadlock detection
		incidlelocked(1);

	g->m->locks--;
}

// The goroutine g exited its system call.
// Arrange for it to run on a cpu again.
// This is called only from the go syscall library, not
// from the low-level system calls used by the runtime.
void
runtime_exitsyscall(int32 dummy __attribute__ ((unused)))
{
	G *gp;

	gp = g;
	gp->m->locks++;  // see comment in entersyscall

	if(gp->isbackground)  // do not consider blocked scavenger for deadlock detection
		incidlelocked(-1);

	gp->waitsince = 0;
	if(exitsyscallfast()) {
		// There's a cpu for us, so we can run.
		((P*)gp->m->p)->syscalltick++;
		gp->atomicstatus = _Grunning;
		// Garbage collector isn't running (since we are),
		// so okay to clear gcstack and gcsp.
#ifdef USING_SPLIT_STACK
		gp->gcstack = nil;
#endif
		gp->gcnextsp = nil;
		runtime_memclr(&gp->gcregs[0], sizeof gp->gcregs);
		gp->syscallsp = 0;
		gp->m->locks--;
		return;
	}

	gp->m->locks--;

	// Call the scheduler.
	runtime_mcall(exitsyscall0);

	// Scheduler returned, so we're allowed to run now.
	// Delete the gcstack information that we left for
	// the garbage collector during the system call.
	// Must wait until now because until gosched returns
	// we don't know for sure that the garbage collector
	// is not running.
#ifdef USING_SPLIT_STACK
	gp->gcstack = nil;
#endif
	gp->gcnextsp = nil;
	runtime_memclr(&gp->gcregs[0], sizeof gp->gcregs);

	gp->syscallsp = 0;

	// Note that this gp->m might be different than the earlier
	// gp->m after returning from runtime_mcall.
	((P*)gp->m->p)->syscalltick++;
}

static bool
exitsyscallfast(void)
{
	G *gp;
	P *p;

	gp = g;

	// Freezetheworld sets stopwait but does not retake P's.
	if(runtime_sched->stopwait) {
		gp->m->p = 0;
		return false;
	}

	// Try to re-acquire the last P.
	if(gp->m->p && ((P*)gp->m->p)->status == _Psyscall && runtime_cas(&((P*)gp->m->p)->status, _Psyscall, _Prunning)) {
		// There's a cpu for us, so we can run.
		gp->m->mcache = ((P*)gp->m->p)->mcache;
		((P*)gp->m->p)->m = (uintptr)gp->m;
		return true;
	}
	// Try to get any other idle P.
	gp->m->p = 0;
	if(runtime_sched->pidle) {
		runtime_lock(&runtime_sched->lock);
		p = pidleget();
		if(p && runtime_atomicload(&runtime_sched->sysmonwait)) {
			runtime_atomicstore(&runtime_sched->sysmonwait, 0);
			runtime_notewakeup(&runtime_sched->sysmonnote);
		}
		runtime_unlock(&runtime_sched->lock);
		if(p) {
			acquirep(p);
			return true;
		}
	}
	return false;
}

// runtime_exitsyscall slow path on g0.
// Failed to acquire P, enqueue gp as runnable.
static void
exitsyscall0(G *gp)
{
	M *m;
	P *p;

	m = g->m;
	gp->atomicstatus = _Grunnable;
	gp->m = nil;
	m->curg = nil;
	runtime_lock(&runtime_sched->lock);
	p = pidleget();
	if(p == nil)
		globrunqput(gp);
	else if(runtime_atomicload(&runtime_sched->sysmonwait)) {
		runtime_atomicstore(&runtime_sched->sysmonwait, 0);
		runtime_notewakeup(&runtime_sched->sysmonnote);
	}
	runtime_unlock(&runtime_sched->lock);
	if(p) {
		acquirep(p);
		execute(gp);  // Never returns.
	}
	if(m->lockedg) {
		// Wait until another thread schedules gp and so m again.
		stoplockedm();
		execute(gp);  // Never returns.
	}
	stopm();
	schedule();  // Never returns.
}

void syscall_entersyscall(void)
  __asm__(GOSYM_PREFIX "syscall.Entersyscall");

void syscall_entersyscall(void) __attribute__ ((no_split_stack));

void
syscall_entersyscall()
{
  runtime_entersyscall(0);
}

void syscall_exitsyscall(void)
  __asm__(GOSYM_PREFIX "syscall.Exitsyscall");

void syscall_exitsyscall(void) __attribute__ ((no_split_stack));

void
syscall_exitsyscall()
{
  runtime_exitsyscall(0);
}

// Called from syscall package before fork.
void syscall_runtime_BeforeFork(void)
  __asm__(GOSYM_PREFIX "syscall.runtime_BeforeFork");
void
syscall_runtime_BeforeFork(void)
{
	// Fork can hang if preempted with signals frequently enough (see issue 5517).
	// Ensure that we stay on the same M where we disable profiling.
	runtime_m()->locks++;
	if(runtime_m()->profilehz != 0)
		runtime_resetcpuprofiler(0);
}

// Called from syscall package after fork in parent.
void syscall_runtime_AfterFork(void)
  __asm__(GOSYM_PREFIX "syscall.runtime_AfterFork");
void
syscall_runtime_AfterFork(void)
{
	int32 hz;

	hz = runtime_sched->profilehz;
	if(hz != 0)
		runtime_resetcpuprofiler(hz);
	runtime_m()->locks--;
}

// Allocate a new g, with a stack big enough for stacksize bytes.
G*
runtime_malg(int32 stacksize, byte** ret_stack, uintptr* ret_stacksize)
{
	G *newg;

	newg = allocg();
	if(stacksize >= 0) {
#if USING_SPLIT_STACK
		int dont_block_signals = 0;
		size_t ss_stacksize;

		*ret_stack = __splitstack_makecontext(stacksize,
						      &newg->stackcontext[0],
						      &ss_stacksize);
		*ret_stacksize = (uintptr)ss_stacksize;
		__splitstack_block_signals_context(&newg->stackcontext[0],
						   &dont_block_signals, nil);
#else
                // In 64-bit mode, the maximum Go allocation space is
                // 128G.  Our stack size is 4M, which only permits 32K
                // goroutines.  In order to not limit ourselves,
                // allocate the stacks out of separate memory.  In
                // 32-bit mode, the Go allocation space is all of
                // memory anyhow.
		if(sizeof(void*) == 8) {
			void *p = runtime_SysAlloc(stacksize, &mstats()->other_sys);
			if(p == nil)
				runtime_throw("runtime: cannot allocate memory for goroutine stack");
			*ret_stack = (byte*)p;
		} else {
			*ret_stack = runtime_mallocgc(stacksize, 0, FlagNoProfiling|FlagNoGC);
			runtime_xadd(&runtime_stacks_sys, stacksize);
		}
		*ret_stacksize = (uintptr)stacksize;
		newg->gcinitialsp = *ret_stack;
		newg->gcstacksize = (uintptr)stacksize;
#endif
	}
	return newg;
}

G*
__go_go(void (*fn)(void*), void* arg)
{
	byte *sp;
	size_t spsize;
	G *newg;
	P *p;

//runtime_printf("newproc1 %p %p narg=%d nret=%d\n", fn->fn, argp, narg, nret);
	if(fn == nil) {
		g->m->throwing = -1;  // do not dump full stacks
		runtime_throw("go of nil func value");
	}
	g->m->locks++;  // disable preemption because it can be holding p in a local var

	p = (P*)g->m->p;
	if((newg = gfget(p)) != nil) {
#ifdef USING_SPLIT_STACK
		int dont_block_signals = 0;

		sp = __splitstack_resetcontext(&newg->stackcontext[0],
					       &spsize);
		__splitstack_block_signals_context(&newg->stackcontext[0],
						   &dont_block_signals, nil);
#else
		sp = newg->gcinitialsp;
		spsize = newg->gcstacksize;
		if(spsize == 0)
			runtime_throw("bad spsize in __go_go");
		newg->gcnextsp = sp;
#endif
	} else {
		uintptr malsize;

		newg = runtime_malg(StackMin, &sp, &malsize);
		spsize = (size_t)malsize;
		allgadd(newg);
	}

	newg->entry = (byte*)fn;
	newg->param = arg;
	newg->gopc = (uintptr)__builtin_return_address(0);
	newg->atomicstatus = _Grunnable;
	if(p->goidcache == p->goidcacheend) {
		p->goidcache = runtime_xadd64(&runtime_sched->goidgen, GoidCacheBatch);
		p->goidcacheend = p->goidcache + GoidCacheBatch;
	}
	newg->goid = p->goidcache++;

	{
		// Avoid warnings about variables clobbered by
		// longjmp.
		byte * volatile vsp = sp;
		size_t volatile vspsize = spsize;
		G * volatile vnewg = newg;
		ucontext_t * volatile uc;

		uc = ucontext_arg(&vnewg->context[0]);
		getcontext(uc);
		uc->uc_stack.ss_sp = vsp;
		uc->uc_stack.ss_size = vspsize;
		makecontext(uc, kickoff, 0);

		runqput(p, vnewg);

		if(runtime_atomicload(&runtime_sched->npidle) != 0 && runtime_atomicload(&runtime_sched->nmspinning) == 0 && fn != runtime_main)  // TODO: fast atomic
			wakep();
		g->m->locks--;
		return vnewg;
	}
}

static void
allgadd(G *gp)
{
	G **new;
	uintptr cap;

	runtime_lock(&allglock);
	if(runtime_allglen >= allgcap) {
		cap = 4096/sizeof(new[0]);
		if(cap < 2*allgcap)
			cap = 2*allgcap;
		new = runtime_malloc(cap*sizeof(new[0]));
		if(new == nil)
			runtime_throw("runtime: cannot allocate memory");
		if(runtime_allg != nil) {
			runtime_memmove(new, runtime_allg, runtime_allglen*sizeof(new[0]));
			runtime_free(runtime_allg);
		}
		runtime_allg = new;
		allgcap = cap;
	}
	runtime_allg[runtime_allglen++] = gp;
	runtime_unlock(&allglock);
}

// Put on gfree list.
// If local list is too long, transfer a batch to the global list.
static void
gfput(P *p, G *gp)
{
	gp->schedlink = (uintptr)p->gfree;
	p->gfree = gp;
	p->gfreecnt++;
	if(p->gfreecnt >= 64) {
		runtime_lock(&runtime_sched->gflock);
		while(p->gfreecnt >= 32) {
			p->gfreecnt--;
			gp = p->gfree;
			p->gfree = (G*)gp->schedlink;
			gp->schedlink = (uintptr)runtime_sched->gfree;
			runtime_sched->gfree = gp;
		}
		runtime_unlock(&runtime_sched->gflock);
	}
}

// Get from gfree list.
// If local list is empty, grab a batch from global list.
static G*
gfget(P *p)
{
	G *gp;

retry:
	gp = p->gfree;
	if(gp == nil && runtime_sched->gfree) {
		runtime_lock(&runtime_sched->gflock);
		while(p->gfreecnt < 32 && runtime_sched->gfree) {
			p->gfreecnt++;
			gp = runtime_sched->gfree;
			runtime_sched->gfree = (G*)gp->schedlink;
			gp->schedlink = (uintptr)p->gfree;
			p->gfree = gp;
		}
		runtime_unlock(&runtime_sched->gflock);
		goto retry;
	}
	if(gp) {
		p->gfree = (G*)gp->schedlink;
		p->gfreecnt--;
	}
	return gp;
}

// Purge all cached G's from gfree list to the global list.
static void
gfpurge(P *p)
{
	G *gp;

	runtime_lock(&runtime_sched->gflock);
	while(p->gfreecnt) {
		p->gfreecnt--;
		gp = p->gfree;
		p->gfree = (G*)gp->schedlink;
		gp->schedlink = (uintptr)runtime_sched->gfree;
		runtime_sched->gfree = gp;
	}
	runtime_unlock(&runtime_sched->gflock);
}

void
runtime_Breakpoint(void)
{
	runtime_breakpoint();
}

void runtime_Gosched (void) __asm__ (GOSYM_PREFIX "runtime.Gosched");

void
runtime_Gosched(void)
{
	runtime_gosched();
}

// Implementation of runtime.GOMAXPROCS.
// delete when scheduler is even stronger

intgo runtime_GOMAXPROCS(intgo)
  __asm__(GOSYM_PREFIX "runtime.GOMAXPROCS");

intgo
runtime_GOMAXPROCS(intgo n)
{
	intgo ret;

	if(n > _MaxGomaxprocs)
		n = _MaxGomaxprocs;
	runtime_lock(&runtime_sched->lock);
	ret = (intgo)runtime_gomaxprocs;
	if(n <= 0 || n == ret) {
		runtime_unlock(&runtime_sched->lock);
		return ret;
	}
	runtime_unlock(&runtime_sched->lock);

	runtime_acquireWorldsema();
	g->m->gcing = 1;
	runtime_stopTheWorldWithSema();
	newprocs = (int32)n;
	g->m->gcing = 0;
	runtime_releaseWorldsema();
	runtime_startTheWorldWithSema();

	return ret;
}

// lockOSThread is called by runtime.LockOSThread and runtime.lockOSThread below
// after they modify m->locked. Do not allow preemption during this call,
// or else the m might be different in this function than in the caller.
static void
lockOSThread(void)
{
	g->m->lockedg = g;
	g->lockedm = g->m;
}

void	runtime_LockOSThread(void) __asm__ (GOSYM_PREFIX "runtime.LockOSThread");
void
runtime_LockOSThread(void)
{
	g->m->locked |= _LockExternal;
	lockOSThread();
}

void
runtime_lockOSThread(void)
{
	g->m->locked += _LockInternal;
	lockOSThread();
}


// unlockOSThread is called by runtime.UnlockOSThread and runtime.unlockOSThread below
// after they update m->locked. Do not allow preemption during this call,
// or else the m might be in different in this function than in the caller.
static void
unlockOSThread(void)
{
	if(g->m->locked != 0)
		return;
	g->m->lockedg = nil;
	g->lockedm = nil;
}

void	runtime_UnlockOSThread(void) __asm__ (GOSYM_PREFIX "runtime.UnlockOSThread");

void
runtime_UnlockOSThread(void)
{
	g->m->locked &= ~_LockExternal;
	unlockOSThread();
}

void
runtime_unlockOSThread(void)
{
	if(g->m->locked < _LockInternal)
		runtime_throw("runtime: internal error: misuse of lockOSThread/unlockOSThread");
	g->m->locked -= _LockInternal;
	unlockOSThread();
}

bool
runtime_lockedOSThread(void)
{
	return g->lockedm != nil && g->m->lockedg != nil;
}

int32
runtime_gcount(void)
{
	G *gp;
	int32 n, s;
	uintptr i;

	n = 0;
	runtime_lock(&allglock);
	// TODO(dvyukov): runtime.NumGoroutine() is O(N).
	// We do not want to increment/decrement centralized counter in newproc/goexit,
	// just to make runtime.NumGoroutine() faster.
	// Compromise solution is to introduce per-P counters of active goroutines.
	for(i = 0; i < runtime_allglen; i++) {
		gp = runtime_allg[i];
		s = gp->atomicstatus;
		if(s == _Grunnable || s == _Grunning || s == _Gsyscall || s == _Gwaiting)
			n++;
	}
	runtime_unlock(&allglock);
	return n;
}

int32
runtime_mcount(void)
{
	return runtime_sched->mcount;
}

static struct {
	uint32 lock;
	int32 hz;
} prof;

static void System(void) {}
static void GC(void) {}

// Called if we receive a SIGPROF signal.
void
runtime_sigprof()
{
	M *mp = g->m;
	int32 n, i;
	bool traceback;
	uintptr pcbuf[TracebackMaxFrames];
	Location locbuf[TracebackMaxFrames];
	Slice stk;

	if(prof.hz == 0)
		return;

	if(mp == nil)
		return;

	// Profiling runs concurrently with GC, so it must not allocate.
	mp->mallocing++;

	traceback = true;

	if(mp->mcache == nil)
		traceback = false;

	n = 0;

	if(runtime_atomicload(&runtime_in_callers) > 0) {
		// If SIGPROF arrived while already fetching runtime
		// callers we can have trouble on older systems
		// because the unwind library calls dl_iterate_phdr
		// which was not recursive in the past.
		traceback = false;
	}

	if(traceback) {
		n = runtime_callers(0, locbuf, nelem(locbuf), false);
		for(i = 0; i < n; i++)
			pcbuf[i] = locbuf[i].pc;
	}
	if(!traceback || n <= 0) {
		n = 2;
		pcbuf[0] = (uintptr)runtime_getcallerpc(&n);
		if(mp->gcing || mp->helpgc)
			pcbuf[1] = (uintptr)GC;
		else
			pcbuf[1] = (uintptr)System;
	}

	if (prof.hz != 0) {
		stk.__values = &pcbuf[0];
		stk.__count = n;
		stk.__capacity = n;

		// Simple cas-lock to coordinate with setcpuprofilerate.
		while (!runtime_cas(&prof.lock, 0, 1)) {
			runtime_osyield();
		}
		if (prof.hz != 0) {
			runtime_cpuprofAdd(stk);
		}
		runtime_atomicstore(&prof.lock, 0);
	}

	mp->mallocing--;
}

// Arrange to call fn with a traceback hz times a second.
void
runtime_setcpuprofilerate_m(int32 hz)
{
	// Force sane arguments.
	if(hz < 0)
		hz = 0;

	// Disable preemption, otherwise we can be rescheduled to another thread
	// that has profiling enabled.
	g->m->locks++;

	// Stop profiler on this thread so that it is safe to lock prof.
	// if a profiling signal came in while we had prof locked,
	// it would deadlock.
	runtime_resetcpuprofiler(0);

	while (!runtime_cas(&prof.lock, 0, 1)) {
		runtime_osyield();
	}
	prof.hz = hz;
	runtime_atomicstore(&prof.lock, 0);

	runtime_lock(&runtime_sched->lock);
	runtime_sched->profilehz = hz;
	runtime_unlock(&runtime_sched->lock);

	if(hz != 0)
		runtime_resetcpuprofiler(hz);

	g->m->locks--;
}

// Change number of processors.  The world is stopped, sched is locked.
static void
procresize(int32 new)
{
	int32 i, old;
	bool pempty;
	G *gp;
	P *p;
	intgo j;

	old = runtime_gomaxprocs;
	if(old < 0 || old > _MaxGomaxprocs || new <= 0 || new >_MaxGomaxprocs)
		runtime_throw("procresize: invalid arg");
	// initialize new P's
	for(i = 0; i < new; i++) {
		p = runtime_allp[i];
		if(p == nil) {
			p = (P*)runtime_mallocgc(sizeof(*p), 0, FlagNoInvokeGC);
			p->id = i;
			p->status = _Pgcstop;
			p->deferpool.__values = &p->deferpoolbuf[0];
			p->deferpool.__count = 0;
			p->deferpool.__capacity = nelem(p->deferpoolbuf);
			runtime_atomicstorep(&runtime_allp[i], p);
		}
		if(p->mcache == nil) {
			if(old==0 && i==0)
				p->mcache = g->m->mcache;  // bootstrap
			else
				p->mcache = runtime_allocmcache();
		}
	}

	// redistribute runnable G's evenly
	// collect all runnable goroutines in global queue preserving FIFO order
	// FIFO order is required to ensure fairness even during frequent GCs
	// see http://golang.org/issue/7126
	pempty = false;
	while(!pempty) {
		pempty = true;
		for(i = 0; i < old; i++) {
			p = runtime_allp[i];
			if(p->runqhead == p->runqtail)
				continue;
			pempty = false;
			// pop from tail of local queue
			p->runqtail--;
			gp = (G*)p->runq[p->runqtail%nelem(p->runq)];
			// push onto head of global queue
			gp->schedlink = runtime_sched->runqhead;
			runtime_sched->runqhead = (uintptr)gp;
			if(runtime_sched->runqtail == 0)
				runtime_sched->runqtail = (uintptr)gp;
			runtime_sched->runqsize++;
		}
	}
	// fill local queues with at most nelem(p->runq)/2 goroutines
	// start at 1 because current M already executes some G and will acquire allp[0] below,
	// so if we have a spare G we want to put it into allp[1].
	for(i = 1; (uint32)i < (uint32)new * nelem(p->runq)/2 && runtime_sched->runqsize > 0; i++) {
		gp = (G*)runtime_sched->runqhead;
		runtime_sched->runqhead = gp->schedlink;
		if(runtime_sched->runqhead == 0)
			runtime_sched->runqtail = 0;
		runtime_sched->runqsize--;
		runqput(runtime_allp[i%new], gp);
	}

	// free unused P's
	for(i = new; i < old; i++) {
		p = runtime_allp[i];
		for(j = 0; j < p->deferpool.__count; j++) {
			((struct _defer**)p->deferpool.__values)[j] = nil;
		}
		p->deferpool.__count = 0;
		runtime_freemcache(p->mcache);
		p->mcache = nil;
		gfpurge(p);
		p->status = _Pdead;
		// can't free P itself because it can be referenced by an M in syscall
	}

	if(g->m->p)
		((P*)g->m->p)->m = 0;
	g->m->p = 0;
	g->m->mcache = nil;
	p = runtime_allp[0];
	p->m = 0;
	p->status = _Pidle;
	acquirep(p);
	for(i = new-1; i > 0; i--) {
		p = runtime_allp[i];
		p->status = _Pidle;
		pidleput(p);
	}
	runtime_atomicstore((uint32*)&runtime_gomaxprocs, new);
}

// Associate p and the current m.
static void
acquirep(P *p)
{
	M *m;

	m = g->m;
	if(m->p || m->mcache)
		runtime_throw("acquirep: already in go");
	if(p->m || p->status != _Pidle) {
		runtime_printf("acquirep: p->m=%p(%d) p->status=%d\n", p->m, p->m ? ((M*)p->m)->id : 0, p->status);
		runtime_throw("acquirep: invalid p state");
	}
	m->mcache = p->mcache;
	m->p = (uintptr)p;
	p->m = (uintptr)m;
	p->status = _Prunning;
}

// Disassociate p and the current m.
static P*
releasep(void)
{
	M *m;
	P *p;

	m = g->m;
	if(m->p == 0 || m->mcache == nil)
		runtime_throw("releasep: invalid arg");
	p = (P*)m->p;
	if((M*)p->m != m || p->mcache != m->mcache || p->status != _Prunning) {
		runtime_printf("releasep: m=%p m->p=%p p->m=%p m->mcache=%p p->mcache=%p p->status=%d\n",
			m, m->p, p->m, m->mcache, p->mcache, p->status);
		runtime_throw("releasep: invalid p state");
	}
	m->p = 0;
	m->mcache = nil;
	p->m = 0;
	p->status = _Pidle;
	return p;
}

static void
incidlelocked(int32 v)
{
	runtime_lock(&runtime_sched->lock);
	runtime_sched->nmidlelocked += v;
	if(v > 0)
		checkdead();
	runtime_unlock(&runtime_sched->lock);
}

// Check for deadlock situation.
// The check is based on number of running M's, if 0 -> deadlock.
static void
checkdead(void)
{
	G *gp;
	int32 run, grunning, s;
	uintptr i;

	// For -buildmode=c-shared or -buildmode=c-archive it's OK if
	// there are no running goroutines.  The calling program is
	// assumed to be running.
	if(runtime_isarchive) {
		return;
	}

	// -1 for sysmon
	run = runtime_sched->mcount - runtime_sched->nmidle - runtime_sched->nmidlelocked - 1 - countextra();
	if(run > 0)
		return;
	// If we are dying because of a signal caught on an already idle thread,
	// freezetheworld will cause all running threads to block.
	// And runtime will essentially enter into deadlock state,
	// except that there is a thread that will call runtime_exit soon.
	if(runtime_panicking() > 0)
		return;
	if(run < 0) {
		runtime_printf("runtime: checkdead: nmidle=%d nmidlelocked=%d mcount=%d\n",
			runtime_sched->nmidle, runtime_sched->nmidlelocked, runtime_sched->mcount);
		runtime_throw("checkdead: inconsistent counts");
	}
	grunning = 0;
	runtime_lock(&allglock);
	for(i = 0; i < runtime_allglen; i++) {
		gp = runtime_allg[i];
		if(gp->isbackground)
			continue;
		s = gp->atomicstatus;
		if(s == _Gwaiting)
			grunning++;
		else if(s == _Grunnable || s == _Grunning || s == _Gsyscall) {
			runtime_unlock(&allglock);
			runtime_printf("runtime: checkdead: find g %D in status %d\n", gp->goid, s);
			runtime_throw("checkdead: runnable g");
		}
	}
	runtime_unlock(&allglock);
	if(grunning == 0)  // possible if main goroutine calls runtime_Goexit()
		runtime_throw("no goroutines (main called runtime.Goexit) - deadlock!");
	g->m->throwing = -1;  // do not dump full stacks
	runtime_throw("all goroutines are asleep - deadlock!");
}

static void
sysmon(void)
{
	uint32 idle, delay;
	int64 now, lastpoll, lasttrace;
	G *gp;

	lasttrace = 0;
	idle = 0;  // how many cycles in succession we had not wokeup somebody
	delay = 0;
	for(;;) {
		if(idle == 0)  // start with 20us sleep...
			delay = 20;
		else if(idle > 50)  // start doubling the sleep after 1ms...
			delay *= 2;
		if(delay > 10*1000)  // up to 10ms
			delay = 10*1000;
		runtime_usleep(delay);
		if(runtime_debug.schedtrace <= 0 &&
			(runtime_sched->gcwaiting || runtime_atomicload(&runtime_sched->npidle) == (uint32)runtime_gomaxprocs)) {  // TODO: fast atomic
			runtime_lock(&runtime_sched->lock);
			if(runtime_atomicload(&runtime_sched->gcwaiting) || runtime_atomicload(&runtime_sched->npidle) == (uint32)runtime_gomaxprocs) {
				runtime_atomicstore(&runtime_sched->sysmonwait, 1);
				runtime_unlock(&runtime_sched->lock);
				runtime_notesleep(&runtime_sched->sysmonnote);
				runtime_noteclear(&runtime_sched->sysmonnote);
				idle = 0;
				delay = 20;
			} else
				runtime_unlock(&runtime_sched->lock);
		}
		// poll network if not polled for more than 10ms
		lastpoll = runtime_atomicload64(&runtime_sched->lastpoll);
		now = runtime_nanotime();
		if(lastpoll != 0 && lastpoll + 10*1000*1000 < now) {
			runtime_cas64(&runtime_sched->lastpoll, lastpoll, now);
			gp = runtime_netpoll(false);  // non-blocking
			if(gp) {
				// Need to decrement number of idle locked M's
				// (pretending that one more is running) before injectglist.
				// Otherwise it can lead to the following situation:
				// injectglist grabs all P's but before it starts M's to run the P's,
				// another M returns from syscall, finishes running its G,
				// observes that there is no work to do and no other running M's
				// and reports deadlock.
				incidlelocked(-1);
				injectglist(gp);
				incidlelocked(1);
			}
		}
		// retake P's blocked in syscalls
		// and preempt long running G's
		if(retake(now))
			idle = 0;
		else
			idle++;

		if(runtime_debug.schedtrace > 0 && lasttrace + runtime_debug.schedtrace*1000000ll <= now) {
			lasttrace = now;
			runtime_schedtrace(runtime_debug.scheddetail);
		}
	}
}

typedef struct Pdesc Pdesc;
struct Pdesc
{
	uint32	schedtick;
	int64	schedwhen;
	uint32	syscalltick;
	int64	syscallwhen;
};
static Pdesc pdesc[_MaxGomaxprocs];

static uint32
retake(int64 now)
{
	uint32 i, s, n;
	int64 t;
	P *p;
	Pdesc *pd;

	n = 0;
	for(i = 0; i < (uint32)runtime_gomaxprocs; i++) {
		p = runtime_allp[i];
		if(p==nil)
			continue;
		pd = &pdesc[i];
		s = p->status;
		if(s == _Psyscall) {
			// Retake P from syscall if it's there for more than 1 sysmon tick (at least 20us).
			t = p->syscalltick;
			if(pd->syscalltick != t) {
				pd->syscalltick = t;
				pd->syscallwhen = now;
				continue;
			}
			// On the one hand we don't want to retake Ps if there is no other work to do,
			// but on the other hand we want to retake them eventually
			// because they can prevent the sysmon thread from deep sleep.
			if(p->runqhead == p->runqtail &&
				runtime_atomicload(&runtime_sched->nmspinning) + runtime_atomicload(&runtime_sched->npidle) > 0 &&
				pd->syscallwhen + 10*1000*1000 > now)
				continue;
			// Need to decrement number of idle locked M's
			// (pretending that one more is running) before the CAS.
			// Otherwise the M from which we retake can exit the syscall,
			// increment nmidle and report deadlock.
			incidlelocked(-1);
			if(runtime_cas(&p->status, s, _Pidle)) {
				n++;
				handoffp(p);
			}
			incidlelocked(1);
		} else if(s == _Prunning) {
			// Preempt G if it's running for more than 10ms.
			t = p->schedtick;
			if(pd->schedtick != t) {
				pd->schedtick = t;
				pd->schedwhen = now;
				continue;
			}
			if(pd->schedwhen + 10*1000*1000 > now)
				continue;
			// preemptone(p);
		}
	}
	return n;
}

// Tell all goroutines that they have been preempted and they should stop.
// This function is purely best-effort.  It can fail to inform a goroutine if a
// processor just started running it.
// No locks need to be held.
// Returns true if preemption request was issued to at least one goroutine.
static bool
preemptall(void)
{
	return false;
}

void
runtime_schedtrace(bool detailed)
{
	static int64 starttime;
	int64 now;
	int64 id1, id2, id3;
	int32 i, t, h;
	uintptr gi;
	const char *fmt;
	M *mp, *lockedm;
	G *gp, *lockedg;
	P *p;

	now = runtime_nanotime();
	if(starttime == 0)
		starttime = now;

	runtime_lock(&runtime_sched->lock);
	runtime_printf("SCHED %Dms: gomaxprocs=%d idleprocs=%d threads=%d idlethreads=%d runqueue=%d",
		(now-starttime)/1000000, runtime_gomaxprocs, runtime_sched->npidle, runtime_sched->mcount,
		runtime_sched->nmidle, runtime_sched->runqsize);
	if(detailed) {
		runtime_printf(" gcwaiting=%d nmidlelocked=%d nmspinning=%d stopwait=%d sysmonwait=%d\n",
			runtime_sched->gcwaiting, runtime_sched->nmidlelocked, runtime_sched->nmspinning,
			runtime_sched->stopwait, runtime_sched->sysmonwait);
	}
	// We must be careful while reading data from P's, M's and G's.
	// Even if we hold schedlock, most data can be changed concurrently.
	// E.g. (p->m ? p->m->id : -1) can crash if p->m changes from non-nil to nil.
	for(i = 0; i < runtime_gomaxprocs; i++) {
		p = runtime_allp[i];
		if(p == nil)
			continue;
		mp = (M*)p->m;
		h = runtime_atomicload(&p->runqhead);
		t = runtime_atomicload(&p->runqtail);
		if(detailed)
			runtime_printf("  P%d: status=%d schedtick=%d syscalltick=%d m=%d runqsize=%d gfreecnt=%d\n",
				i, p->status, p->schedtick, p->syscalltick, mp ? mp->id : -1, t-h, p->gfreecnt);
		else {
			// In non-detailed mode format lengths of per-P run queues as:
			// [len1 len2 len3 len4]
			fmt = " %d";
			if(runtime_gomaxprocs == 1)
				fmt = " [%d]\n";
			else if(i == 0)
				fmt = " [%d";
			else if(i == runtime_gomaxprocs-1)
				fmt = " %d]\n";
			runtime_printf(fmt, t-h);
		}
	}
	if(!detailed) {
		runtime_unlock(&runtime_sched->lock);
		return;
	}
	for(mp = runtime_allm; mp; mp = mp->alllink) {
		p = (P*)mp->p;
		gp = mp->curg;
		lockedg = mp->lockedg;
		id1 = -1;
		if(p)
			id1 = p->id;
		id2 = -1;
		if(gp)
			id2 = gp->goid;
		id3 = -1;
		if(lockedg)
			id3 = lockedg->goid;
		runtime_printf("  M%d: p=%D curg=%D mallocing=%d throwing=%d gcing=%d"
			" locks=%d dying=%d helpgc=%d spinning=%d blocked=%d lockedg=%D\n",
			mp->id, id1, id2,
			mp->mallocing, mp->throwing, mp->gcing, mp->locks, mp->dying, mp->helpgc,
			mp->spinning, mp->blocked, id3);
	}
	runtime_lock(&allglock);
	for(gi = 0; gi < runtime_allglen; gi++) {
		gp = runtime_allg[gi];
		mp = gp->m;
		lockedm = gp->lockedm;
		runtime_printf("  G%D: status=%d(%S) m=%d lockedm=%d\n",
			gp->goid, gp->atomicstatus, gp->waitreason, mp ? mp->id : -1,
			lockedm ? lockedm->id : -1);
	}
	runtime_unlock(&allglock);
	runtime_unlock(&runtime_sched->lock);
}

// Put mp on midle list.
// Sched must be locked.
static void
mput(M *mp)
{
	mp->schedlink = runtime_sched->midle;
	runtime_sched->midle = (uintptr)mp;
	runtime_sched->nmidle++;
	checkdead();
}

// Try to get an m from midle list.
// Sched must be locked.
static M*
mget(void)
{
	M *mp;

	if((mp = (M*)runtime_sched->midle) != nil){
		runtime_sched->midle = mp->schedlink;
		runtime_sched->nmidle--;
	}
	return mp;
}

// Put gp on the global runnable queue.
// Sched must be locked.
static void
globrunqput(G *gp)
{
	gp->schedlink = 0;
	if(runtime_sched->runqtail)
		((G*)runtime_sched->runqtail)->schedlink = (uintptr)gp;
	else
		runtime_sched->runqhead = (uintptr)gp;
	runtime_sched->runqtail = (uintptr)gp;
	runtime_sched->runqsize++;
}

// Put a batch of runnable goroutines on the global runnable queue.
// Sched must be locked.
static void
globrunqputbatch(G *ghead, G *gtail, int32 n)
{
	gtail->schedlink = 0;
	if(runtime_sched->runqtail)
		((G*)runtime_sched->runqtail)->schedlink = (uintptr)ghead;
	else
		runtime_sched->runqhead = (uintptr)ghead;
	runtime_sched->runqtail = (uintptr)gtail;
	runtime_sched->runqsize += n;
}

// Try get a batch of G's from the global runnable queue.
// Sched must be locked.
static G*
globrunqget(P *p, int32 max)
{
	G *gp, *gp1;
	int32 n;

	if(runtime_sched->runqsize == 0)
		return nil;
	n = runtime_sched->runqsize/runtime_gomaxprocs+1;
	if(n > runtime_sched->runqsize)
		n = runtime_sched->runqsize;
	if(max > 0 && n > max)
		n = max;
	if((uint32)n > nelem(p->runq)/2)
		n = nelem(p->runq)/2;
	runtime_sched->runqsize -= n;
	if(runtime_sched->runqsize == 0)
		runtime_sched->runqtail = 0;
	gp = (G*)runtime_sched->runqhead;
	runtime_sched->runqhead = gp->schedlink;
	n--;
	while(n--) {
		gp1 = (G*)runtime_sched->runqhead;
		runtime_sched->runqhead = gp1->schedlink;
		runqput(p, gp1);
	}
	return gp;
}

// Put p to on pidle list.
// Sched must be locked.
static void
pidleput(P *p)
{
	p->link = runtime_sched->pidle;
	runtime_sched->pidle = (uintptr)p;
	runtime_xadd(&runtime_sched->npidle, 1);  // TODO: fast atomic
}

// Try get a p from pidle list.
// Sched must be locked.
static P*
pidleget(void)
{
	P *p;

	p = (P*)runtime_sched->pidle;
	if(p) {
		runtime_sched->pidle = p->link;
		runtime_xadd(&runtime_sched->npidle, -1);  // TODO: fast atomic
	}
	return p;
}

// Try to put g on local runnable queue.
// If it's full, put onto global queue.
// Executed only by the owner P.
static void
runqput(P *p, G *gp)
{
	uint32 h, t;

retry:
	h = runtime_atomicload(&p->runqhead);  // load-acquire, synchronize with consumers
	t = p->runqtail;
	if(t - h < nelem(p->runq)) {
		p->runq[t%nelem(p->runq)] = (uintptr)gp;
		runtime_atomicstore(&p->runqtail, t+1);  // store-release, makes the item available for consumption
		return;
	}
	if(runqputslow(p, gp, h, t))
		return;
	// the queue is not full, now the put above must suceed
	goto retry;
}

// Put g and a batch of work from local runnable queue on global queue.
// Executed only by the owner P.
static bool
runqputslow(P *p, G *gp, uint32 h, uint32 t)
{
	G *batch[nelem(p->runq)/2+1];
	uint32 n, i;

	// First, grab a batch from local queue.
	n = t-h;
	n = n/2;
	if(n != nelem(p->runq)/2)
		runtime_throw("runqputslow: queue is not full");
	for(i=0; i<n; i++)
		batch[i] = (G*)p->runq[(h+i)%nelem(p->runq)];
	if(!runtime_cas(&p->runqhead, h, h+n))  // cas-release, commits consume
		return false;
	batch[n] = gp;
	// Link the goroutines.
	for(i=0; i<n; i++)
		batch[i]->schedlink = (uintptr)batch[i+1];
	// Now put the batch on global queue.
	runtime_lock(&runtime_sched->lock);
	globrunqputbatch(batch[0], batch[n], n+1);
	runtime_unlock(&runtime_sched->lock);
	return true;
}

// Get g from local runnable queue.
// Executed only by the owner P.
static G*
runqget(P *p)
{
	G *gp;
	uint32 t, h;

	for(;;) {
		h = runtime_atomicload(&p->runqhead);  // load-acquire, synchronize with other consumers
		t = p->runqtail;
		if(t == h)
			return nil;
		gp = (G*)p->runq[h%nelem(p->runq)];
		if(runtime_cas(&p->runqhead, h, h+1))  // cas-release, commits consume
			return gp;
	}
}

// Grabs a batch of goroutines from local runnable queue.
// batch array must be of size nelem(p->runq)/2. Returns number of grabbed goroutines.
// Can be executed by any P.
static uint32
runqgrab(P *p, G **batch)
{
	uint32 t, h, n, i;

	for(;;) {
		h = runtime_atomicload(&p->runqhead);  // load-acquire, synchronize with other consumers
		t = runtime_atomicload(&p->runqtail);  // load-acquire, synchronize with the producer
		n = t-h;
		n = n - n/2;
		if(n == 0)
			break;
		if(n > nelem(p->runq)/2)  // read inconsistent h and t
			continue;
		for(i=0; i<n; i++)
			batch[i] = (G*)p->runq[(h+i)%nelem(p->runq)];
		if(runtime_cas(&p->runqhead, h, h+n))  // cas-release, commits consume
			break;
	}
	return n;
}

// Steal half of elements from local runnable queue of p2
// and put onto local runnable queue of p.
// Returns one of the stolen elements (or nil if failed).
static G*
runqsteal(P *p, P *p2)
{
	G *gp;
	G *batch[nelem(p->runq)/2];
	uint32 t, h, n, i;

	n = runqgrab(p2, batch);
	if(n == 0)
		return nil;
	n--;
	gp = batch[n];
	if(n == 0)
		return gp;
	h = runtime_atomicload(&p->runqhead);  // load-acquire, synchronize with consumers
	t = p->runqtail;
	if(t - h + n >= nelem(p->runq))
		runtime_throw("runqsteal: runq overflow");
	for(i=0; i<n; i++, t++)
		p->runq[t%nelem(p->runq)] = (uintptr)batch[i];
	runtime_atomicstore(&p->runqtail, t);  // store-release, makes the item available for consumption
	return gp;
}

void runtime_testSchedLocalQueue(void)
  __asm__("runtime.testSchedLocalQueue");

void
runtime_testSchedLocalQueue(void)
{
	P p;
	G gs[nelem(p.runq)];
	int32 i, j;

	runtime_memclr((byte*)&p, sizeof(p));

	for(i = 0; i < (int32)nelem(gs); i++) {
		if(runqget(&p) != nil)
			runtime_throw("runq is not empty initially");
		for(j = 0; j < i; j++)
			runqput(&p, &gs[i]);
		for(j = 0; j < i; j++) {
			if(runqget(&p) != &gs[i]) {
				runtime_printf("bad element at iter %d/%d\n", i, j);
				runtime_throw("bad element");
			}
		}
		if(runqget(&p) != nil)
			runtime_throw("runq is not empty afterwards");
	}
}

void runtime_testSchedLocalQueueSteal(void)
  __asm__("runtime.testSchedLocalQueueSteal");

void
runtime_testSchedLocalQueueSteal(void)
{
	P p1, p2;
	G gs[nelem(p1.runq)], *gp;
	int32 i, j, s;

	runtime_memclr((byte*)&p1, sizeof(p1));
	runtime_memclr((byte*)&p2, sizeof(p2));

	for(i = 0; i < (int32)nelem(gs); i++) {
		for(j = 0; j < i; j++) {
			gs[j].sig = 0;
			runqput(&p1, &gs[j]);
		}
		gp = runqsteal(&p2, &p1);
		s = 0;
		if(gp) {
			s++;
			gp->sig++;
		}
		while((gp = runqget(&p2)) != nil) {
			s++;
			gp->sig++;
		}
		while((gp = runqget(&p1)) != nil)
			gp->sig++;
		for(j = 0; j < i; j++) {
			if(gs[j].sig != 1) {
				runtime_printf("bad element %d(%d) at iter %d\n", j, gs[j].sig, i);
				runtime_throw("bad element");
			}
		}
		if(s != i/2 && s != i/2+1) {
			runtime_printf("bad steal %d, want %d or %d, iter %d\n",
				s, i/2, i/2+1, i);
			runtime_throw("bad steal");
		}
	}
}

intgo
runtime_setmaxthreads(intgo in)
{
	intgo out;

	runtime_lock(&runtime_sched->lock);
	out = (intgo)runtime_sched->maxmcount;
	runtime_sched->maxmcount = (int32)in;
	checkmcount();
	runtime_unlock(&runtime_sched->lock);
	return out;
}

static intgo
procPin()
{
	M *mp;

	mp = runtime_m();
	mp->locks++;
	return (intgo)(((P*)mp->p)->id);
}

static void
procUnpin()
{
	runtime_m()->locks--;
}

intgo sync_runtime_procPin(void)
  __asm__ (GOSYM_PREFIX "sync.runtime_procPin");

intgo
sync_runtime_procPin()
{
	return procPin();
}

void sync_runtime_procUnpin(void)
  __asm__ (GOSYM_PREFIX  "sync.runtime_procUnpin");

void
sync_runtime_procUnpin()
{
	procUnpin();
}

intgo sync_atomic_runtime_procPin(void)
  __asm__ (GOSYM_PREFIX "sync_atomic.runtime_procPin");

intgo
sync_atomic_runtime_procPin()
{
	return procPin();
}

void sync_atomic_runtime_procUnpin(void)
  __asm__ (GOSYM_PREFIX  "sync_atomic.runtime_procUnpin");

void
sync_atomic_runtime_procUnpin()
{
	procUnpin();
}

void
runtime_proc_scan(struct Workbuf** wbufp, void (*enqueue1)(struct Workbuf**, Obj))
{
	enqueue1(wbufp, (Obj){(byte*)&runtime_main_init_done, sizeof runtime_main_init_done, 0});
}

// Return whether we are waiting for a GC.  This gc toolchain uses
// preemption instead.
bool
runtime_gcwaiting(void)
{
	return runtime_sched->gcwaiting;
}

// os_beforeExit is called from os.Exit(0).
//go:linkname os_beforeExit os.runtime_beforeExit

extern void os_beforeExit() __asm__ (GOSYM_PREFIX "os.runtime_beforeExit");

void
os_beforeExit()
{
}

// Active spinning for sync.Mutex.
//go:linkname sync_runtime_canSpin sync.runtime_canSpin

enum
{
	ACTIVE_SPIN = 4,
	ACTIVE_SPIN_CNT = 30,
};

extern _Bool sync_runtime_canSpin(intgo i)
  __asm__ (GOSYM_PREFIX "sync.runtime_canSpin");

_Bool
sync_runtime_canSpin(intgo i)
{
	P *p;

	// sync.Mutex is cooperative, so we are conservative with spinning.
	// Spin only few times and only if running on a multicore machine and
	// GOMAXPROCS>1 and there is at least one other running P and local runq is empty.
	// As opposed to runtime mutex we don't do passive spinning here,
	// because there can be work on global runq on on other Ps.
	if (i >= ACTIVE_SPIN || runtime_ncpu <= 1 || runtime_gomaxprocs <= (int32)(runtime_sched->npidle+runtime_sched->nmspinning)+1) {
		return false;
	}
	p = (P*)g->m->p;
	return p != nil && p->runqhead == p->runqtail;
}

//go:linkname sync_runtime_doSpin sync.runtime_doSpin
//go:nosplit

extern void sync_runtime_doSpin(void)
  __asm__ (GOSYM_PREFIX "sync.runtime_doSpin");

void
sync_runtime_doSpin()
{
	runtime_procyield(ACTIVE_SPIN_CNT);
}

// For Go code to look at variables, until we port proc.go.

extern M** runtime_go_allm(void)
  __asm__ (GOSYM_PREFIX "runtime.allm");

M**
runtime_go_allm()
{
	return &runtime_allm;
}

extern Slice runtime_go_allgs(void)
  __asm__ (GOSYM_PREFIX "runtime.allgs");

Slice
runtime_go_allgs()
{
	Slice s;

	s.__values = runtime_allg;
	s.__count = runtime_allglen;
	s.__capacity = allgcap;
	return s;
}

intgo NumCPU(void) __asm__ (GOSYM_PREFIX "runtime.NumCPU");

intgo
NumCPU()
{
	return (intgo)(runtime_ncpu);
}
