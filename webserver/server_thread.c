#include "request.h"
#include "server_thread.h"
#include "common.h"

// Hash Function for hash table
// Hash function used is djb2, obtained from
// http://www.cse.yorku.ca/~oz/hash.html
unsigned long
hash(char *str)
{
	unsigned long hash = 5381;
	int c;

	while ((c = *str++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}

typedef struct cache_entry {
	struct file_data *data;
	struct cache_entry *next;
} CacheEntry;

typedef struct cache {
	CacheEntry *table;
	long size;
	long capacity;
	long max_cache_size;
} Cache;

typedef struct lru_ele {
	CacheEntry *entry;
	struct lru_ele *next;
} LRUEntry;

typedef struct lru {
	LRUEntry *head;
	LRUEntry *tail;
	int size;
} LRUList;

struct request_buffer {
	int* requests;
	int in;
	int out;
	int max_size;
};

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;

	pthread_t* threads;
	struct request_buffer buffer;

	pthread_cond_t cv_full;
	pthread_cond_t cv_empty;
	pthread_mutex_t lock;

	Cache *cache;
	LRUList *LRU;
};

//	=================	LRU Functions	=================	//

LRUEntry* find_in_LRU(LRUList *LRU, CacheEntry *target) {
	LRUEntry* current = LRU->head;
	while (current != NULL) {
		if (current->entry == target)
			break;
		current = current->next;
	}
	return current;
}

LRUEntry* find_in_LRU_with_prev(LRUList *LRU, CacheEntry *target, CacheEntry **prev) {
	*prev = NULL;
	LRUEntry* current = LRU->head;
	while (current != NULL) {
		if (current->entry == target)
			break;
		*prev = current;
		current = current->next;
	}
	return current;
}

// Appends given node to queue
void add_to_LRU(LRUList *LRU, CacheEntry *entry) {
	LRUEntry *node = (LRUEntry *) malloc(sizeof(LRUEntry));
	assert(node);
	node->entry = entry;
	node->next = NULL;
	if (LRU->head == NULL) {
		// LRU is currently empty
		LRU->head = node;
		LRU->tail = node;
	} else {
		LRU->tail->next = node;	// Append to end of queue
		LRU->tail = LRU->tail->next;	// Move tail to new end of queue
	}
	LRU->size++;	// Update queue size
}

// Moves the head of the queue to the end
void move_head_to_end(LRUList *LRU) {
	if (LRU->size <= 1)
		return;
	LRUEntry *old_head = LRU->head;
	LRU->head = LRU->head->next;
	LRU->tail->next = old_head;
	LRU->tail = old_head;
	old_head->next = NULL;
}

void move_node_to_end(LRUList *LRU, CacheEntry *entry) {
	if (LRU->size <= 1)
		return;
	LRUEntry *prev;
	LRUEntry *node = find_in_LRU_with_prev(LRU, entry, &prev);
	if (!node)
		return;
	if (node == LRU->head)
		move_head_to_end(LRU);
	else if (node != LRU->tail) {
		prev->next = node->next;
		node->next = NULL;
		LRU->tail->next = node;
		LRU->tail = node;
	}
}

// Removes and returns and the head of the queue
LRUEntry* pop_LRU(LRUList *LRU) {
	LRUEntry* old_head = LRU->head;
	LRU->head = LRU->head->next;
	old_head->next = NULL;
	LRU->size--;	// Update queue size
	return old_head;
}

// Removes target node from queue
void remove_from_LRU(LRUList *LRU, CacheEntry *target) {
	if (LRU->size == 1) {
		// This is the only node in the queue
		free(LRU->head);
		LRU->head = NULL;
		LRU->tail = NULL;
		LRU->size = 0;
		return;
	}

	LRUEntry *current = LRU->head;
	LRUEntry *prev = NULL;
	while (current != NULL) {
		if (current->entry == target)
			break;
		prev = current;
		current = current->next;
	}

	if (current) {
		if (!prev)
			LRU->head = current->next;
		else
			prev->next = current->next;
		current->next = NULL;
		if (current == LRU->tail)
			LRU->tail = prev;

		free(current);
		LRU->size--;
	}
}

// Frees all nodes in the given queue
void clear_LRU(LRUList *LRU) {
	if (LRU == NULL)
		return;
	while (LRU->head != NULL) {
		LRUEntry *next = LRU->head->next;
		free(LRU->head);
		LRU->head = next;
	}
	LRU->tail = NULL;
	LRU->size = 0;
}

//	=================	End of LRU Functions		=================	//


// ======================== Hashtable Operations ========================

struct file_data *cache_lookup(Cache *cache, char *filename) {
	// TODO: Apply mutex
	unsigned long key = hash(filename) % cache->capacity;
	CacheEntry *entry = &cache->table[key];
	while (entry->next != NULL) {
		entry = entry->next;
		if (!strcmp(entry->data->file_name, filename)) {
			// TODO: Update LRU List
			return entry->data;		// Cache hit
		}
	}
	return NULL;	// Cache miss
}

int cache_insert(Cache *cache, struct file_data *file) {
	// TODO: Apply mutex
	// TODO: Check for space limitations
	// Assume doesn't exist in cache already
	unsigned long key = hash(file->file_name) % cache->capacity;
	CacheEntry *entry = &cache->table[key];
	while (entry->next != NULL) {
		entry = entry->next;
	}

	entry->next = (CacheEntry *) malloc(sizeof(CacheEntry));
	entry = entry->next;
	entry->data = file;
	entry->next = NULL;
	cache->size += file->file_size;

	// TODO: Update LRU List
	return 1;
}

void cache_evict(Cache *cache, unsigned long amount_to_evict) {
	// TODO: Implement this function
}

// ======================== End of Hashtable Operations ========================

/* static functions */

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
	int ret, cache_inserted = 0;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}

	// Check for cache hit
	struct file_data *cache_value = cache_lookup(sv->cache, data->file_name);
	if (cache_value) {
		file_data_free(data);
		request_set_data(rq, cache_value);
	} else {
		/* read file, 
		* fills data->file_buf with the file contents,
		* data->file_size with file size. */
		ret = request_readfile(rq);
		if (ret == 0) { /* couldn't read file */
			goto out;
		} else {
			// Add file to cache
			cache_inserted = cache_insert(sv->cache, data);
		}
	}

	
	/* send file to client */
	request_sendfile(rq);
out:
	request_destroy(rq);
	if (!cache_inserted)
		file_data_free(data);
}

/* entry point functions */

void add_request(struct server* sv, int connfd) {
	// Acquire lock for mutual exclusion
	pthread_mutex_lock(&sv->lock);

	while ((sv->buffer.in - sv->buffer.out + sv->buffer.max_size) % sv->buffer.max_size == sv->buffer.max_size - 1) {
		// Buffer is full. Wait for buffer space to be free
		pthread_cond_wait(&sv->cv_full, &sv->lock);
	}

	// Add socket info for request in buffer
	sv->buffer.requests[sv->buffer.in] = connfd;

	if (sv->buffer.in == sv->buffer.out) {
		// Buffer was empty but now has requests. Wake up sleeping worker threads
		pthread_cond_broadcast(&sv->cv_empty);
	}

	// Update in
	sv->buffer.in = (sv->buffer.in + 1) % sv->buffer.max_size;

	// Release lock
	pthread_mutex_unlock(&sv->lock);
}

void take_request(struct server* sv) {
	// Acquire lock for mutual exclusion
	pthread_mutex_lock(&sv->lock);
	while (!sv->exiting && sv->buffer.in == sv->buffer.out) {
		// Buffer is empty. Wait for buffer to have requests
		pthread_cond_wait(&sv->cv_empty, &sv->lock);
	}

	// Take request from buffer
	int connfd = sv->buffer.requests[sv->buffer.out];

	if ((sv->buffer.in - sv->buffer.out + sv->buffer.max_size) % sv->buffer.max_size == sv->buffer.max_size - 1) {
		// Buffer was full but is no longer full. Wake up threads sleeping on full
		pthread_cond_broadcast(&sv->cv_full);
	}

	// Update out
	sv->buffer.out = (sv->buffer.out + 1) % sv->buffer.max_size;

	// Release lock
	pthread_mutex_unlock(&sv->lock);

	// Perform request
	if (!sv->exiting)
		do_server_request(sv, connfd);
}

void worker_thread(struct server* sv) {
	while (!sv->exiting) {
		take_request(sv);
	}
	pthread_exit((void*)0);
}

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests + 1;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;
	
	if (sv->nr_threads > 0 && sv->max_requests > 1) {
		sv->buffer.requests = (int*) malloc(max_requests * sizeof(int));
		assert(sv->buffer.requests);
		sv->buffer.in = 0;
		sv->buffer.out = 0;
		sv->buffer.max_size = sv->max_requests;

		sv->threads = (pthread_t*) malloc(nr_threads * sizeof(pthread_t));
		assert(sv->threads);
		for (int i=0; i<nr_threads; ++i) {
			if (pthread_create(&sv->threads[i], NULL, (void * (*)(void *)) worker_thread, sv)) {
				fprintf(stderr, "Error creating thread #%d\n", i);
				exit(1);
			}
		}
		if(pthread_cond_init(&sv->cv_full, NULL)) {
			fprintf(stderr, "Error creating cv_full\n");
			exit(1);
		}
		if(pthread_cond_init(&sv->cv_empty, NULL)) {
			fprintf(stderr, "Error creating cv_empty\n");
			exit(1);
		}
		if(pthread_mutex_init(&sv->lock, NULL)) {
			fprintf(stderr, "Error creating lock\n");
			exit(1);
		}
	}

	if (max_cache_size > 0) {
		sv->cache = (Cache *) malloc(sizeof(Cache));
		assert(sv->cache);
		sv->cache->max_cache_size = max_cache_size;
		sv->cache->capacity = 300;
		sv->cache->table = (CacheEntry *) calloc(sv->cache->capacity * sizeof(CacheEntry));
		assert(sv->cache->table);
		sv->cache->size = 0;

		sv->LRU = (LRUList *) malloc(sizeof(LRUList));
		assert(sv->LRU);
		sv->LRU->head = NULL;
		sv->LRU->tail = NULL;
		sv->LRU->size = 0;
	}

	return sv;
}

void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0 || sv->max_requests <= 1) { /* no worker threads or no buffer */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		add_request(sv, connfd);
	}
}

void
server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	sv->exiting = 1;

	if (sv->max_requests > 1 && sv->nr_threads > 0) {
		// Wake up any sleeping worker threads
		pthread_cond_broadcast(&sv->cv_empty);

		for (int i=0; i<sv->nr_threads; ++i) {
			pthread_join(sv->threads[i], NULL);
		}
	}

	/* make sure to free any allocated resources */
	free(sv->buffer.requests);
	free(sv->threads);
	free(sv);
}
