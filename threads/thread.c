#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stdbool.h>
#include "thread.h"
#include "interrupt.h"

bool Tid_taken[THREAD_MAX_THREADS] = {false};

struct thread_list {
	struct thread *thread;
	struct thread_list *next;
};

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
	int size;
	struct thread_list *head;
	struct thread_list *tail;
} rq;

/* This is the thread control block */
struct thread {
	Tid id;
	THREAD_STATUS status;
	ucontext_t context;
	void *stack;
	void (*fn) (void *);
	void *parg;
};

void
thread_stub(void (*thread_main)(void *), void *arg)
{
	rq.head->thread->status = THREAD_RUNNING;
	thread_main(arg); // call thread_main() function with arg
	thread_exit();
}

void
thread_init(void)
{
	struct thread *th = (struct thread *) malloc(sizeof(struct thread));
	th->id = 0;
	th->status = THREAD_RUNNING;
	rq.size = 1;
	rq.head = (struct thread_list *) malloc(sizeof(struct thread_list));
	rq.head->thread = th;
	rq.head->next = NULL;
	rq.tail = rq.head;
	Tid_taken[0] = true;
}

Tid
thread_id()
{
	return rq.head->thread->id;
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	Tid id = -1;
	for (int i=0; i<THREAD_MAX_THREADS; ++i) {
		if (!Tid_taken[i]) {
			id = i;
			break;
		}
	}
	if (id < 0)
		return THREAD_NOMORE;
	
	struct thread *th = (struct thread *) malloc(sizeof(struct thread));
	if (th == NULL)
		return THREAD_NOMEMORY;
	th->stack = malloc(THREAD_MIN_STACK);
	if (th->stack == NULL) {
		free(th);
		return THREAD_NOMEMORY;
	}
	th->id = id;
	th->status = THREAD_READY;
	th->fn = fn;
	th->parg = parg;
	getcontext(&th->context);
	const int STACK_ALIGN_OFFSET = -8; // Offset by this amount to align frame to 16 bytes
	th->context.uc_mcontext.gregs[15] = (unsigned long) th->stack + (unsigned long) THREAD_MIN_STACK + STACK_ALIGN_OFFSET;
	th->context.uc_mcontext.gregs[8] = (unsigned long) fn;
	th->context.uc_mcontext.gregs[9] = (unsigned long) parg;
	th->context.uc_mcontext.gregs[16] = (unsigned long) thread_stub;

	rq.tail->next = (struct thread_list *) malloc(sizeof(struct thread_list));
	rq.tail = rq.tail->next;
	rq.tail->thread = th;
	rq.tail->next = NULL;
	rq.size++;

	Tid_taken[id] = true;
	return id;
}

Tid
thread_yield(Tid want_tid)
{
	struct thread_list *want_thread, *current_thread;
	current_thread = rq.head;

	if (want_tid == THREAD_ANY) {
		if (rq.size == 1)
			return THREAD_NONE;
		want_thread = rq.head->next;
		want_tid = want_thread->thread->id;

		rq.head = want_thread;
		rq.tail->next = current_thread;
		rq.tail = current_thread;
		current_thread->next = NULL;
	} else if (want_tid == THREAD_SELF || want_tid == rq.head->thread->id) {
		want_tid = rq.head->thread->id;
		want_thread = rq.head;
	} else {
		struct thread_list *current = rq.head;
		while (current->next != NULL) {
			if (want_tid == current->next->thread->id)
				break;
			current = current->next;
		}
		want_thread = current->next;
		if (want_thread == NULL)
			return THREAD_INVALID;

		/*current->next = want_thread->next;
		rq.tail->next = rq.head;
		rq.tail = rq.head;
		want_thread->next = rq.head->next;
		rq.head->next = NULL;
		rq.head = want_thread;*/
		rq.head = want_thread;
		rq.tail->next = current_thread;
		rq.tail = current_thread;
		current->next = want_thread->next;
		want_thread->next = current_thread->next;
		if (want_thread->next == NULL)
			want_thread->next = current_thread;
		current_thread->next = NULL;
	}

	volatile bool setcontext_called = false;
	current_thread->thread->status = THREAD_READY;
	getcontext(&current_thread->thread->context);

	if (setcontext_called) {
		if (rq.head->thread->status == THREAD_EXITED)
			thread_exit();
		else {
			rq.head->thread->status = THREAD_RUNNING;
			return want_tid;
		}
	}
	setcontext_called = true;
	setcontext(&want_thread->thread->context);
	
	return want_tid;
}

void
thread_exit()
{
	Tid_taken[rq.head->thread->id] = false;
	struct thread_list *current_thread_list = rq.head;
	struct thread *current_thread = current_thread_list->thread;
	void *stack_ptr = current_thread->stack;
	rq.head = rq.head->next;
	rq.size--;
	free(current_thread_list);
	free(current_thread);
	free(stack_ptr);
	if (rq.head == NULL)
		exit(0);
	setcontext(&rq.head->thread->context);
}

Tid
thread_kill(Tid tid)
{
	/*if (tid == rq.head->thread->id)
		return THREAD_INVALID;
	struct thread_list *current = rq.head;
	while (current->next != NULL) {
		if (tid == current->next->thread->id)
			break;
		current = current->next;
	}

	struct thread_list *thread2kill = current->next;
	if (thread2kill == NULL)
		return THREAD_INVALID;

	current->next = thread2kill->next;
	Tid killed_id = thread2kill->thread->id;
	Tid_taken[killed_id] = false;
	free(thread2kill->thread->stack);
	free(thread2kill->thread);
	free(thread2kill);
	rq.size--;

	return killed_id;*/

	if (tid == rq.head->thread->id)
		return THREAD_INVALID;
	struct thread_list *current = rq.head;
	while (current->next != NULL) {
		if (tid == current->next->thread->id)
			break;
		current = current->next;
	}

	struct thread_list *thread2kill = current->next;
	if (thread2kill == NULL)
		return THREAD_INVALID;

	/*if (thread2kill != rq.tail) {
		current->next = thread2kill->next;
		rq.tail->next = thread2kill;
		thread2kill->next = NULL;
	}*/
	//Tid_taken[killed_id] = false;
	Tid killed_id = thread2kill->thread->id;
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
