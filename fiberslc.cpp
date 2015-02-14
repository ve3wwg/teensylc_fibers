///////////////////////////////////////////////////////////////////////
// fiberslc.cpp -- Implementation of asm fibers code
// Date: Fri Feb 13 21:00:39 2015  (C) Warren W. Gay VE3WWG 
///////////////////////////////////////////////////////////////////////

#include <stdint.h>
#include <fiberslc.h>

extern "C" {
	static void fiber_start();
	extern void yield();
}

static volatile uint32_t headroom = 1024;	// Headroom of main() or last fiber created
static volatile uint32_t stackroot = 0;		// Address of the stack root for last fiber (else 0)

//////////////////////////////////////////////////////////////////////
// Configure how much stack to allocate to the main() thread
//////////////////////////////////////////////////////////////////////

void
fiber_set_main(uint32_t stack_size) {

	if ( !stackroot ) 			// If not too late..
		headroom = stack_size;		// Set the main program's stack size
}

//////////////////////////////////////////////////////////////////////
// This routine is the fiber's launch pad routine
//////////////////////////////////////////////////////////////////////

static void
fiber_start() {
	fiber_t *fiber;

	asm("mov %[result], r7\n" : [result] "=r" (fiber));		// r7 points to fiber initially

	fiber->state = FiberExecuting;

	asm("mov r0, %[value]\n" : : [value] "r" (fiber->arg));		// Supply void *arg to fiber call
	asm("mov r1, %[value]\n" : : [value] "r" (fiber->funcptr));	// r1 now holds the function ptr to call

	asm("push {r4-r7}\n");
	asm("push {r3}\n");
	asm("mov r4,r8\n");
	asm("mov r5,r9\n");
        asm("push {r4,r5}\n");			// Push r8,r9

        asm("blx  r1\n");                       // func(arg) call

	asm("pop {r4,r5}\n");
	asm("mov r8,r4\n");
	asm("mov r9,r5\n");
	asm("pop {r3}\n");
        asm("pop  {r4-r7}\n");

        fiber->state = FiberReturned;           // Fiber has returned from its function

        for (;;) {
                ::yield();
        }
}

//////////////////////////////////////////////////////////////////////
// Set up a fiber to execute (but don't launch it)
//////////////////////////////////////////////////////////////////////

void
fiber_create(volatile fiber_t *fiber,uint32_t stack_size,fiber_func_t func,void *arg) {

	asm("push {r0,r1,r2,r3}\n");
	asm("stmia r0!,{r4,r5,r6,r7}\n");       // Save lower regs
	asm("mov r1,r8\n");
	asm("mov r2,r9\n");
	asm("mov r3,sl\n");
	asm("stmia r0!,{r1,r2,r3}\n");		// Save r8,r9 & sl
	asm("mov r1,fp\n");
	asm("mov r3,lr\n");
	asm("stmia r0!,{r1,r2,r3}\n");		// Save fp,(placeholder for sp) & lr
	asm("pop {r0,r1,r2,r3}\n");		// Restore regs
	
	fiber->stack_size = stack_size;		// In r1
	fiber->funcptr 	= func;			// In r2
	fiber->arg 	= arg;			// In r3
	fiber->r7	= (uint32_t)fiber;	// Overwrite r12 with fiber ptr

	if ( !stackroot )
		asm("mov %[result],sp\n" : [result] "=r" (stackroot));

	stackroot -= stack_size;		// This is the new root of the stack
	headroom = stack_size;			// Save this stack size for creation of the next fiber

	fiber->sp = stackroot;			// Save as Fiber's sp
	fiber->lr = (void *) fiber_start;	// Fiber startup code
	fiber->state = FiberCreated;		// Set state of this fiber
	fiber->initial_sp = stackroot;		// Save sp for restart()
}

//////////////////////////////////////////////////////////////////////
// Swap one fiber context for another
//////////////////////////////////////////////////////////////////////

void
fiber_swap(volatile fiber_t *nextfibe,volatile fiber_t *prevfibe) {
	
	asm("stmia r1!,{r4-r7}\n");	// Save r4,r5,r6,r7
	asm("mov r2,r8\n");
	asm("mov r3,r9\n");
	asm("mov r4,sl\n");
	asm("stmia r1!,{r2-r4}\n");	// Save r8,r9,sl
	asm("mov r2,fp\n");
	asm("mov r3,sp\n");
	asm("mov r4,lr\n");
	asm("stmia r1!,{r2-r4}\n");	// Save fp,sp,lr

	asm("add r0,#16");		// r0 = &nextfibe->r8
	asm("ldmia r0!,{r2-r4}\n");	// Load values for r8,r9,sl
	asm("mov r8,r2\n");
	asm("mov r9,r3\n");
	asm("mov sl,r4\n");
	asm("ldmia r0!,{r2-r4}\n");	// Load values for fp,sp,lr
	asm("mov fp,r2\n");
	asm("mov sp,r3\n");
	asm("mov lr,r4\n");
	asm("sub r0,#40\n");		// r0 = nextfibe
	asm("ldmia r0!,{r4-r7}\n");	// Restore r4-r7
	// asm("bx lr\n");						// Return to where we left off in this fiber
}

//////////////////////////////////////////////////////////////////////
// Restart a terminated fiber
//////////////////////////////////////////////////////////////////////

void
fiber_restart(volatile fiber_t *fiber,fiber_func_t func,void *arg) {

	fiber->r7	= (uint32_t) fiber;				// Tell fiber_start() where the fiber_t struct is
	fiber->funcptr	= func;						// New fiber routine
	fiber->arg	= arg;						// New arg value
	fiber->state	= FiberCreated;					// Fiber is being launched
	fiber->lr	= (void *)fiber_start;				// Address of next instruction
	fiber->sp	= fiber->initial_sp;				// Reset sp
}

// End fiberslc.cpp

