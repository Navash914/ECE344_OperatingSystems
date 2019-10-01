#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stdbool.h>
#include "thread.h"
#include "interrupt.h"

// Global array to check which thread ids are taken
bool Tid_taken[THREAD_MAX_THREADS] = {false};

// Linked list for holding thread information structs
struct thread_list {
	struct thread *thread;
	struct thread_list *next;
};

// Enum for checking thread status
typedef enum {
	THREAD_RUNNING,
	THREAD_READY,
	THREAD_BLOCKED,
	THREAD_EXITED
} THREAD_STATUS;

/* This is the wait queue structure */
struct wait_queue {
	/* ... Fill this in Lab 3 ... */
};

// This is the ready queue structure
struct ready_queue {
	int size;	// Size of the ready queue
	struct thread_list *head;	// Start of the ready queue
								// The head of the ready queue 
								// is also the currently running thread
	struct thread_list *tail;	// End of the ready queue
} rq;

/* This is the thread control block */
struct thread {
	Tid id;						// Id of the thread
	THREAD_STATUS status;		// Status of the thread
	ucontext_t context;			// Thread context
	void *stack;				// Pointer to memory allocated for thread stack
};

// Thread stub function that all threads enter when they are first run
void
thread_stub(void (*thread_main)(void *), void *arg)
{
	rq.head->thread->status = THREAD_RUNNING;	// Set thread to running
	thread_main(arg); // call thread_main() function with arg
	thread_exit();		// Exit thread when execution is done
}

// Initialize
void
thread_init(void)
{
	// Custom create the first thread (the currently running one)
	struct thread *th = (struct thread *) malloc(sizeof(struct thread));
	th->id = 0;
	th->status = THREAD_RUNNING;

	// Add thread to the ready queue
	rq.head = (struct thread_list *) malloc(sizeof(struct thread_list));
	rq.head->thread = th;
	rq.head->next = NULL;
	rq.tail = rq.head;
	rq.size = 1;
	Tid_taken[0] = true;
}

// Returns id of currently running thread
Tid
thread_id()
{
	return rq.head->thread->id;
}

// Create a new thread
Tid
thread_create(void (*fn) (void *), void *parg)
{
	// Find an available thread id
	Tid id = -1;
	for (int i=0; i<THREAD_MAX_THREADS; ++i) {
		if (!Tid_taken[i]) {
			// Found an available thread id
			id = i;
			break;
		}
	}
	if (id < 0)
		return THREAD_NOMORE;	// No thread ids available
	
	struct thread *th = (struct thread *) malloc(sizeof(struct thread));
	if (th == NULL)
		return THREAD_NOMEMORY;	// No memory available for thread structure
	th->stack = malloc(THREAD_MIN_STACK);
	if (th->stack == NULL) {
		// No memory available for thread stack
		free(th);	// Free the previously allocated memory for thread structure
		return THREAD_NOMEMORY;
	}

	// Pack the thread info structure
	th->id = id;
	th->status = THREAD_READY;
	getcontext(&th->context);			// Copy the current context as the context of the new thread
	const int STACK_ALIGN_OFFSET = -8; // Offset by this amount to align frame to 16 bytes

	// Point stack pointer register in context to newly allocated stack memory
	th->context.uc_mcontext.gregs[15] = (unsigned long) th->stack + (unsigned long) THREAD_MIN_STACK + STACK_ALIGN_OFFSET;
	// Assign first argument register to thread's main function
	th->context.uc_mcontext.gregs[8] = (unsigned long) fn;
	// Assign second argument register to thread's main function argument
	th->context.uc_mcontext.gregs[9] = (unsigned long) parg;
	// Assign the pc of the thread to point to the thread_stub function
	th->context.uc_mcontext.gregs[16] = (unsigned long) thread_stub;

	// Add newly created thread to the end of the ready queue
	rq.tail->next = (struct thread_list *) malloc(sizeof(struct thread_list));
	rq.tail = rq.tail->next;
	rq.tail->thread = th;
	rq.tail->next = NULL;
	rq.size++;

	// Thread id of newly created thread is now taken
	Tid_taken[id] = true;
	return id;
}

// Stop currently running thread and start thread with id want_tid
Tid
thread_yield(Tid want_tid)
{
	struct thread_list *want_thread, *current_thread;
	current_thread = rq.head;

	if (want_tid == THREAD_ANY) {
		// Yield to the next thread in the queue
		if (rq.size == 1)
			return THREAD_NONE;	// There are no more threads in the queue to yield to

		want_thread = rq.head->next;
		want_tid = want_thread->thread->id;

		// Move current thread to end of ready queue and 
		// assign the next thread in the queue as the new head
		rq.head = want_thread;
		rq.tail->next = current_thread;
		rq.tail = current_thread;
		current_thread->next = NULL;
	} else if (want_tid == THREAD_SELF || want_tid == rq.head->thread->id) {
		// Yield to the currently running thread
		// Should be a no-op but yield anyways for debugging purposes
		want_tid = rq.head->thread->id;
		want_thread = rq.head;
	} else {
		// Yield the thread with id want_tid
		// Find the thread in the ready queue
		struct thread_list *current = rq.head;
		while (current->next != NULL) {
			if (want_tid == current->next->thread->id)
				break;	// Found the thread
			current = current->next;
		}
		want_thread = current->next;
		if (want_thread == NULL)
			return THREAD_INVALID;	// Thread with id want_tid does not exist in the ready queue

		// Move current thread to end of ready queue and 
		// assign the wanted thread in the queue as the new head
		rq.head = want_thread;
		rq.tail->next = current_thread;
		rq.tail = current_thread;
		current->next = want_thread->next;
		want_thread->next = current_thread->next;
		if (want_thread->next == NULL)
			want_thread->next = current_thread;
		current_thread->next = NULL;
	}

	volatile bool setcontext_called = false;		// Flag to check if returning from a different thread
	current_thread->thread->status = THREAD_READY;	// Set current thread (which is about to sleep) to ready state
	getcontext(&current_thread->thread->context);	// Save context for current thread

	if (setcontext_called) {
		// Returning to this thread from a different thread
		if (rq.head->thread->status == THREAD_EXITED)
			thread_exit();	// This thread was marked to exit
		else {
			rq.head->thread->status = THREAD_RUNNING;	// Set current thread status to running
			return want_tid;
		}
	}
	setcontext_called = true;
	setcontext(&want_thread->thread->context);	// Switch to context of wanted thread
	
	return want_tid;
}

// Kill and exit out of the currently running thread
void
thread_exit()
{
	Tid_taken[rq.head->thread->id] = false;	// This Tid can be reused

	// Store pointers to memory about to be deallocated
	struct thread_list *current_thread_list = rq.head;
	struct thread *current_thread = current_thread_list->thread;
	void *stack_ptr = current_thread->stack;

	// Move head of ready queue to next available thread
	rq.head = rq.head->next;
	rq.size--;	// One element removed from queue

	// Deallocate memory for this thread
	free(current_thread_list);
	free(current_thread);
	free(stack_ptr);		// !! STACK DEALLOCATED. CAN NO LONGER USE STACK MEMORY !!
	if (rq.head == NULL)
		exit(0);	// This was the last running thread so exit out of the program
	setcontext(&rq.head->thread->context);	// Switch to next available thread
}

// Mark given thread to be killed next time it runs
Tid
thread_kill(Tid tid)
{
	if (tid == rq.head->thread->id)
		return THREAD_INVALID;	// Cannot kill currently running thread

	// Find the requested thread
	struct thread_list *current = rq.head;
	while (current->next != NULL) {
		if (tid == current->next->thread->id)
			break;
		current = current->next;
	}

	struct thread_list *thread2kill = current->next;
	if (thread2kill == NULL)
		return THREAD_INVALID;	// Requested thread does not exist on the ready queue

	// Move killed thread to end of queue
	// This is commented out because it is not required to move
	// the killed thread to the end of queue
	/*if (thread2kill != rq.tail) {
		current->next = thread2kill->next;
		rq.tail->next = thread2kill;
		thread2kill->next = NULL;
	}*/
	
	Tid killed_id = thread2kill->thread->id;

	// Set status of the killed thread to exited
	// The thread will exit next time it runs
	thread2kill->thread->status = THREAD_EXITED;
	return killed_id;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	TBD();

	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	TBD();
	free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	TBD();
	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	TBD();
	return 0;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	TBD();
	return 0;
}

struct lock {
	/* ... Fill this in ... */
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	TBD();

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	TBD();

	free(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

struct cv {
	/* ... Fill this in ... */
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	TBD();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	TBD();

	free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}
