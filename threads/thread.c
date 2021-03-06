#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stdbool.h>
#include "thread.h"
#include "interrupt.h"

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

// This is the thread queue structure
struct wait_queue {
	int size;	// Size of the thread queue
	struct thread_list *head;	// Start of the queue
								// The head of the ready queue 
								// is also the currently running thread
	struct thread_list *tail;	// End of the queue
} rq, eq;	// rq is the ready queue. eq is the exit queue.

typedef struct wait_queue thread_queue;

struct wait_queue *thread_wait_queues[THREAD_MAX_THREADS] = { NULL };

/* This is the thread control block */
struct thread {
	Tid id;						// Id of the thread
	THREAD_STATUS status;		// Status of the thread
	ucontext_t context;			// Thread context
	void *stack;				// Pointer to memory allocated for thread stack
};

// Global array that stores pointers to threads based on their thread id.
// If a thread id is available, the pointer is NULL
// Otherwise, the pointer points to the thread struct for that thread
struct thread *threads[THREAD_MAX_THREADS] = { NULL };

//	=================	Helper Functions	=================	//

// Find thread with given tid in queue
struct thread_list* find_in_queue(thread_queue *q, int tid) {
	struct thread_list* current = q->head;
	while (current != NULL) {
		if (current->thread->id == tid)
			break;
		current = current->next;
	}
	return current;
}

// Find thread with given tid in queue
// Also return the previous node in prev
struct thread_list* find_in_queue_with_prev(thread_queue *q, int tid, struct thread_list **prev) {
	if (q->head->thread->id == tid) {
		*prev = NULL;
		return q->head;
	}

	struct thread_list* current = q->head;

	while (current->next != NULL) {
		if (current->next->thread->id == tid)
			break;
		current = current->next;
	}
	*prev = current;
	return current->next;
}

// Appends given node to queue
void append_node_to_queue(thread_queue *q, struct thread_list *node) {
	if (q->head == NULL) {
		// Queue is currently empty
		q->head = node;
		q->tail = node;
	} else {
		q->tail->next = node;	// Append to end of queue
		q->tail = q->tail->next;	// Move tail to new end of queue
	}
	q->size++;	// Update queue size
}

// Creates new node for thread and appends to queue
bool append_to_queue(thread_queue *q, struct thread *th) {
	struct thread_list *new_node = (struct thread_list *) malloc(sizeof(struct thread_list));
	if (new_node == NULL)
		return false;
	new_node->thread = th;
	new_node->next = NULL;
	append_node_to_queue(q, new_node);
	return true;
}

// Moves the head of the queue to the end
void move_head_to_end(thread_queue *q) {
	if (q->size <= 1)
		return;
	struct thread_list *old_head = q->head;
	q->head = q->head->next;
	q->tail->next = old_head;
	q->tail = old_head;
	old_head->next = NULL;
}

// Moves the target node to the head of the queue
void move_to_head(thread_queue *q, struct thread_list *target, struct thread_list *prev) {
	if (q->size <= 1)
		return;
	if (target == q->head)
		return;
	struct thread_list *old_head = q->head;
	q->head = target;
	prev->next = target->next;
	target->next = old_head;
	if (q->tail == target)
		q->tail = prev;
}

// Removes and returns and the head of the queue
struct thread_list* pop_queue(thread_queue *q) {
	struct thread_list* old_head = q->head;
	q->head = q->head->next;
	old_head->next = NULL;
	q->size--;	// Update queue size
	return old_head;
}

// Removes target node from queue
// This does not free memory for the node
void remove_from_queue(thread_queue *q, struct thread_list *target, struct thread_list *prev) {
	if (q->size == 1) {
		// This is the only node in the queue
		q->head = NULL;
		q->tail = NULL;
	} else if (target == q->head) {
		// Removing head
		q->head = q->head->next;
		target->next = NULL;
	} else {
		prev->next = target->next;
		target->next = NULL;
		if (target == q->tail) {
			// Update new tail
			q->tail = prev;
		}
	}
	q->size--;	// Update queue size
}

// Frees memory allocated for node
void free_node(struct thread_list *node) {
	if (node == NULL)
		return;
	free(node->thread->stack);
	free(node->thread);
	free(node);
}

// Frees all nodes in the given queue
void clear_queue(thread_queue *q) {
	if (q == NULL)
		return;
	while (q->head != NULL) {
		struct thread_list *next = q->head->next;
		free_node(q->head);
		q->head = next;
	}
	q->tail = NULL;
	q->size = 0;
}

// Changes status of given thread.
// If current status of thread is THREAD_EXITED,
// the status is not changed
void set_thread_status(struct thread *th, THREAD_STATUS status) {
	if (th->status != THREAD_EXITED)
		th->status = status;
}

//	=================	End of Helper Functions		=================	//

// Thread stub function that all threads enter when they are first run
void
thread_stub(void (*thread_main)(void *), void *arg)
{
	interrupts_on();
	if (rq.head->thread->status == THREAD_EXITED)
		thread_exit();	// Thread was killed before it was run
	set_thread_status(rq.head->thread, THREAD_RUNNING);	// Set thread to running
	clear_queue(&eq);	// Clear exit queue
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

	// Initialize the queue sizes
	rq.size = 0;
	eq.size = 0;

	// Add thread to the ready queue
	append_to_queue(&rq, th);
	threads[0] = th;
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
	int enabled = interrupts_off();
	// Find an available thread id
	Tid id = -1;
	for (int i=0; i<THREAD_MAX_THREADS; ++i) {
		if (threads[i] == NULL) {
			// Found an available thread id
			id = i;
			break;
		}
	}
	if (id < 0) {
		interrupts_set(enabled);
		return THREAD_NOMORE;	// No thread ids available
	}
	
	struct thread *th = (struct thread *) malloc(sizeof(struct thread));
	if (th == NULL) {
		interrupts_set(enabled);
		return THREAD_NOMEMORY;	// No memory available for thread structure
	}
	th->stack = malloc(THREAD_MIN_STACK);
	if (th->stack == NULL) {
		// No memory available for thread stack
		free(th);	// Free the previously allocated memory for thread structure
		interrupts_set(enabled);
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
	bool success = append_to_queue(&rq, th);
	if (!success) {
		// No memory for thread node
		// Free previously allocated memory
		free(th->stack);
		free(th);
		interrupts_set(enabled);
		return THREAD_NOMEMORY;
	}

	// Thread id of newly created thread is now taken
	threads[id] = th;

	interrupts_set(enabled);
	return id;
}

// Stop currently running thread and start thread with id want_tid
Tid
thread_yield(Tid want_tid)
{
	int enabled = interrupts_off();
	struct thread_list *want_thread, *current_thread;
	current_thread = rq.head;

	if (want_tid == THREAD_ANY) {
		// Yield to the next thread in the queue
		if (rq.size == 1) {
			interrupts_set(enabled);
			return THREAD_NONE;	// There are no more threads in the queue to yield to
		}

		want_thread = rq.head->next;
		want_tid = want_thread->thread->id;

		// Move current thread to end of ready queue
		move_head_to_end(&rq);
	} else if (want_tid == THREAD_SELF || want_tid == rq.head->thread->id) {
		// Yield to the currently running thread
		// Should be a no-op but yield anyways for debugging purposes
		want_tid = rq.head->thread->id;
		want_thread = rq.head;
	} else {
		// Yield the thread with id want_tid
		// Find the thread in the ready queue
		struct thread_list *prev;
		want_thread = find_in_queue_with_prev(&rq, want_tid, &prev);
		if (want_thread == NULL) {
			interrupts_set(enabled);
			return THREAD_INVALID;	// Thread with id want_tid does not exist in the ready queue
		}

		// Move current thread to end of ready queue and 
		// assign the wanted thread in the queue as the new head
		move_head_to_end(&rq);
		move_to_head(&rq, want_thread, prev);
	}

	volatile bool setcontext_called = false;		// Flag to check if returning from a different thread
	set_thread_status(current_thread->thread, THREAD_READY);	// Set current thread (which is about to yield) to ready state
	getcontext(&current_thread->thread->context);	// Save context for current thread

	if (setcontext_called) {
		// Returning to this thread from a different thread
		if (rq.head->thread->status == THREAD_EXITED)
			thread_exit();	// This thread was marked to exit
		else {
			set_thread_status(rq.head->thread, THREAD_RUNNING);	// Set current thread status to running
			clear_queue(&eq);	// Clear exit queue
			interrupts_set(enabled);
			return want_tid;
		}
	}
	setcontext_called = true;
	setcontext(&want_thread->thread->context);	// Switch to context of wanted thread
	
	interrupts_set(enabled);
	return want_tid;
}

// Kill and exit out of the currently running thread
void
thread_exit()
{
	interrupts_off();
	threads[thread_id()] = NULL;	// This Tid can be reused

	// Wakeup all threads waiting on this thread's exit
	thread_wakeup(thread_wait_queues[thread_id()], 1);

	// Delete the wait queue for this thread
	wait_queue_destroy(thread_wait_queues[thread_id()]);
	thread_wait_queues[thread_id()] = NULL;

	// Pop head of ready queue
	struct thread_list* exited_thread = pop_queue(&rq);

	if (rq.head == NULL) {
		// This is the last running thread.
		// Clear exit queue, any remaining wait queues,
		// deallocate current thread and exit from program
		clear_queue(&eq);

		for (int i=0; i<THREAD_MAX_THREADS; ++i)
			wait_queue_destroy(thread_wait_queues[i]);

		struct thread_list *current_thread_list = exited_thread;
		struct thread *current_thread = current_thread_list->thread;
		void *stack_ptr = current_thread->stack;

		// Deallocate memory for this thread
		free(current_thread_list);
		free(current_thread);
		free(stack_ptr);		// !! STACK DEALLOCATED. CAN NO LONGER USE STACK MEMORY !!
		exit(0);
	}

	// Append exited thread to exit queue
	append_node_to_queue(&eq, exited_thread);

	setcontext(&rq.head->thread->context);	// Switch to next available thread
}

// Mark given thread to be killed next time it runs
Tid
thread_kill(Tid tid)
{
	int enabled = interrupts_off();
	if (tid == thread_id()) {
		interrupts_set(enabled);
		return THREAD_INVALID;	// Cannot kill currently running thread
	}

	if (tid < 0 || tid >= THREAD_MAX_THREADS) {
		interrupts_set(enabled);
		return THREAD_INVALID;	// Invalid tid
	}

	// Get the requested thread
	struct thread *thread2kill = threads[tid];
	if (thread2kill == NULL) {
		interrupts_set(enabled);
		return THREAD_INVALID;	// Requested thread does not exist
	}

	Tid killed_id = thread2kill->id;

	// Set status of the killed thread to exited
	// The thread will exit next time it runs
	set_thread_status(thread2kill, THREAD_EXITED);
	interrupts_set(enabled);
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

	wq->size = 0;
	wq->head = NULL;
	wq->tail = NULL;

	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	clear_queue(wq);
	free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	int enabled = interrupts_off();
	if (queue == NULL) {
		// Invalid Queue
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	if (rq.size == 1) {
		// Currently running thread is the only availabled thread
		interrupts_set(enabled);
		return THREAD_NONE;
	}

	struct thread_list *current_thread = pop_queue(&rq);
	append_node_to_queue(queue, current_thread);

	struct thread_list *new_thread = rq.head;
	int ret_id = new_thread->thread->id;

	volatile bool setcontext_called = false;			// Flag to check if returning from a different thread
	set_thread_status(current_thread->thread, THREAD_BLOCKED);	// Set current thread (which is about to sleep) to blocked state
	getcontext(&current_thread->thread->context);		// Save context for current thread

	if (setcontext_called) {
		// Returning to this thread from a different thread
		if (rq.head->thread->status == THREAD_EXITED)
			thread_exit();	// This thread was marked to exit
		else {
			set_thread_status(rq.head->thread, THREAD_RUNNING);	// Set current thread status to running
			clear_queue(&eq);	// Clear exit queue
			interrupts_set(enabled);
			return ret_id;
		}
	}
	setcontext_called = true;
	setcontext(&new_thread->thread->context);	// Switch to context of wanted thread
	
	interrupts_set(enabled);
	return ret_id;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	int enabled = interrupts_off();
	if (queue == NULL || queue->size == 0) {
		// Invalid or empty queue
		interrupts_set(enabled);
		return 0;
	}

	int count = 0;
	do {
		struct thread_list *node = pop_queue(queue);
		set_thread_status(node->thread, THREAD_READY);
		append_node_to_queue(&rq, node);
		count++;
	} while (all && queue->head != NULL);

	interrupts_set(enabled);
	return count;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	int enabled = interrupts_off();
	
	if (tid < 0 || tid == thread_id() || threads[tid] == NULL) {
		// Invalid tid
		interrupts_set(enabled);
		return THREAD_INVALID;
	}

	int ret_id = tid;
	struct wait_queue *wq = thread_wait_queues[tid];
	if (wq == NULL) {
		thread_wait_queues[tid] = wait_queue_create();
		wq = thread_wait_queues[tid];
	}

	thread_sleep(wq);
	interrupts_set(enabled);
	return ret_id;
}

struct lock {
	bool locked;	// Whether this lock has been acquired
	int thread;		// Id of thread that acquired this lock.
	struct wait_queue *wq;		// Wait queue for this lock
};

struct lock *
lock_create()
{
	int enabled = interrupts_off();
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	lock->locked = false;
	lock->wq = wait_queue_create();

	interrupts_set(enabled);
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	int enabled = interrupts_off();
	assert(lock != NULL);
	assert(!lock->locked);

	wait_queue_destroy(lock->wq);

	free(lock);
	interrupts_set(enabled);
}

void
lock_acquire(struct lock *lock)
{
	int enabled = interrupts_off();
	assert(lock != NULL);

	// Wait until lock is released
	while (lock->locked) {
		thread_sleep(lock->wq);
	}

	// Acquire the lock
	lock->locked = true;
	lock->thread = thread_id();

	interrupts_set(enabled);
}

void
lock_release(struct lock *lock)
{
	int enabled = interrupts_off();
	assert(lock != NULL);
	assert(lock->locked && lock->thread == thread_id());

	// Release the lock
	lock->locked = false;

	// Wake up all threads waiting on the lock
	thread_wakeup(lock->wq, 1);	
	
	interrupts_set(enabled);
}

struct cv {
	int thread;
	struct wait_queue *wq;		// Wait queue for this cv
};

struct cv *
cv_create()
{
	int enabled = interrupts_off();
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	cv->wq = wait_queue_create();

	interrupts_set(enabled);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	int enabled = interrupts_off();
	assert(cv != NULL);
	assert(cv->wq->size == 0);

	wait_queue_destroy(cv->wq);
	free(cv);

	interrupts_set(enabled);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);
	assert(lock->locked && lock->thread == thread_id());

	lock_release(lock);
	thread_sleep(cv->wq);
	lock_acquire(lock);

	interrupts_set(enabled);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);
	assert(lock->locked && lock->thread == thread_id());

	thread_wakeup(cv->wq, 0);
	interrupts_set(enabled);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);
	assert(lock->locked && lock->thread == thread_id());

	thread_wakeup(cv->wq, 1);
	interrupts_set(enabled);
}
