#include "request.h"
#include "server_thread.h"
#include "common.h"

static void
file_data_free(struct file_data *data);

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
	int in_use;
	struct cache_entry *next;
} CacheEntry;

typedef struct lru_ele {
	CacheEntry *entry;
	struct lru_ele *next;
} LRUEntry;

typedef struct lru {
	LRUEntry *head;
	LRUEntry *tail;
	int size;

	//pthread_mutex_t lock_lru;
} LRUList;

typedef struct cache {
	CacheEntry *table;
	LRUList *LRU;
	
	long size;
	long capacity;
	long max_cache_size;

	int rc;
	pthread_cond_t cv_rc;

	pthread_mutex_t lock;

} Cache;

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
};

//	=================	LRU Functions	=================	//

LRUEntry* find_in_LRU(LRUList *LRU, CacheEntry *target) {
	//pthread_mutex_lock(&LRU->lock_lru);
	LRUEntry* current = LRU->head;
	while (current != NULL) {
		if (current->entry == target)
			break;
		current = current->next;
	}
	//pthread_mutex_unlock(&LRU->lock_lru);
	return current;
}

LRUEntry* find_in_LRU_with_prev(LRUList *LRU, CacheEntry *target, LRUEntry **prev) {
	//pthread_mutex_lock(&LRU->lock_lru);
	*prev = NULL;
	LRUEntry* current = LRU->head;
	while (current != NULL) {
		if (current->entry == target)
			break;
		*prev = current;
		current = current->next;
	}
	//pthread_mutex_unlock(&LRU->lock_lru);
	return current;
}

// Appends given node to queue
void add_to_LRU(LRUList *LRU, CacheEntry *entry) {
	//pthread_mutex_lock(&LRU->lock_lru);
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
	//pthread_mutex_unlock(&LRU->lock_lru);
}

// Moves the head of the queue to the end
void move_head_to_end(LRUList *LRU) {
	//pthread_mutex_lock(&LRU->lock_lru);
	if (LRU->size <= 1) {
		//pthread_mutex_unlock(&LRU->lock_lru);
		return;
	}
	LRUEntry *old_head = LRU->head;
	LRU->head = LRU->head->next;
	LRU->tail->next = old_head;
	LRU->tail = old_head;
	old_head->next = NULL;
	//pthread_mutex_unlock(&LRU->lock_lru);
}

void move_node_to_end(LRUList *LRU, CacheEntry *entry) {
	//pthread_mutex_lock(&LRU->lock_lru);
	if (LRU->size <= 1)
		return;
	LRUEntry *prev;
	LRUEntry *node = find_in_LRU_with_prev(LRU, entry, &prev);
	if (!node) {
		//pthread_mutex_unlock(&LRU->lock_lru);
		return;
	}
	if (node == LRU->head)
		move_head_to_end(LRU);
	else if (node != LRU->tail) {
		prev->next = node->next;
		node->next = NULL;
		LRU->tail->next = node;
		LRU->tail = node;
	}
	//pthread_mutex_unlock(&LRU->lock_lru);
}

// Removes and returns and the head of the queue
LRUEntry* pop_LRU(LRUList *LRU) {
	//pthread_mutex_lock(&LRU->lock_lru);
	LRUEntry* old_head = LRU->head;
	LRU->head = LRU->head->next;
	old_head->next = NULL;
	LRU->size--;	// Update queue size
	//pthread_mutex_unlock(&LRU->lock_lru);
	return old_head;
}

// Removes target node from queue
void remove_from_LRU(LRUList *LRU, CacheEntry *target) {
	//pthread_mutex_lock(&LRU->lock_lru);
	if (LRU->size == 1) {
		// This is the only node in the queue
		free(LRU->head);
		LRU->head = NULL;
		LRU->tail = NULL;
		LRU->size = 0;
		//pthread_mutex_unlock(&LRU->lock_lru);
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
	//pthread_mutex_unlock(&LRU->lock_lru);
}

LRUEntry* remove_node_from_LRU(LRUList *LRU, LRUEntry *target, LRUEntry *prev) {
	//pthread_mutex_lock(&LRU->lock_lru);
	LRUEntry *ret;
	if (LRU->size == 1) {
		// This is the only node in the queue
		LRU->head = NULL;
		LRU->tail = NULL;
		ret = NULL;
	} else if (prev == NULL) {
		// Removing head
		LRU->head = LRU->head->next;
		target->next = NULL;
		ret = LRU->head;
	} else {
		prev->next = target->next;
		target->next = NULL;
		if (target == LRU->tail) {
			// Update new tail
			LRU->tail = prev;
		}
		ret = prev->next;
	}
	free(target);
	LRU->size--;	// Update queue size
	//pthread_mutex_unlock(&LRU->lock_lru);
	return ret;
} 

// Frees all nodes in the given queue
void clear_LRU(LRUList *LRU) {
	if (LRU == NULL)
		return;
	//pthread_mutex_lock(&LRU->lock_lru);
	while (LRU->head != NULL) {
		LRUEntry *next = LRU->head->next;
		free(LRU->head);
		LRU->head = next;
	}
	LRU->tail = NULL;
	LRU->size = 0;
	//pthread_mutex_unlock(&LRU->lock_lru);
}

void destroy_LRU(LRUList *LRU) {
	clear_LRU(LRU);
	//pthread_mutex_destroy(&LRU->lock_lru);
	free(LRU);
}

//	=================	End of LRU Functions		=================	//


// ======================== Hashtable Operations ========================

CacheEntry* cache_lookup(Cache *cache, char *filename) {
	pthread_mutex_lock(&cache->lock);
	unsigned long key = hash(filename) % cache->capacity;
	CacheEntry *entry = &cache->table[key];
	int hit = 0;
	while (entry->next != NULL) {
		entry = entry->next;
		if (!strcmp(entry->data->file_name, filename)) {
			hit = 1;
			break;
		}
	}

	CacheEntry *ret;
	if (hit) {
		ret = entry;
		move_node_to_end(cache->LRU, entry);
	} else ret = NULL;

	pthread_mutex_unlock(&cache->lock);
	return ret;
}

int cache_exists(Cache *cache, char *filename) {
	unsigned long key = hash(filename) % cache->capacity;
	CacheEntry *entry = &cache->table[key];
	while (entry->next != NULL) {
		entry = entry->next;
		if (!strcmp(entry->data->file_name, filename)) {
			return 1;
		}
	}

	return 0;
}

void remove_from_cache(Cache *cache, CacheEntry *target) {
	unsigned long key = hash(target->data->file_name) % cache->capacity;
	CacheEntry *entry = &cache->table[key];
	CacheEntry *prev = NULL;
	int hit = 0;
	while (entry->next != NULL) {
		prev = entry;
		entry = entry->next;
		if (entry == target) {
			hit = 1;
			break;
		}
	}

	if (hit) {
		prev->next = target->next;
		target->next = NULL;
		cache->size -= target->data->file_size;
		file_data_free(target->data);
		free(target);
	}
}

unsigned long cache_evict(Cache *cache, unsigned long amount_to_evict) {
	// No need for mutex as this function only called from cache_insert, which already has mutex
	unsigned long evicted_amount = 0;
	LRUEntry *current = cache->LRU->head;
	LRUEntry *prev = NULL;
	while (current != NULL) {
		printf("Use count for %lu: %d\n", (unsigned long) current->entry, current->entry->in_use);
		if (current->entry->in_use > 0) {
			prev = current;
			current = current->next;
		} else {
			CacheEntry *to_destroy = current->entry;
			evicted_amount += current->entry->data->file_size;
			current = remove_node_from_LRU(cache->LRU, current, prev);
			remove_from_cache(cache, to_destroy);
			if (evicted_amount >= amount_to_evict)
				break;
		}
	}
	return evicted_amount;
}

int cache_insert(Cache *cache, struct file_data *file) {
	//pthread_mutex_lock(&cache->lock);

	if (cache_exists(cache, file->file_name)) {
		//pthread_mutex_unlock(&cache->lock);
		return 0;
	}

	if (file->file_size > cache->max_cache_size) {
		// File too large. Cannot cache.
		//pthread_mutex_unlock(&cache->lock);
		return 0;
	}

	if (cache->size + file->file_size > cache->max_cache_size) {
		cache_evict(cache, file->file_size - (cache->max_cache_size - cache->size));
		if (cache->size + file->file_size > cache->max_cache_size) {
			// Couldn't evict enough
			//pthread_mutex_unlock(&cache->lock);
			return 0;
		}
	}
	
	//pthread_mutex_unlock(&cache->lock);
	//return 0;	
	
	unsigned long key = hash(file->file_name) % cache->capacity;
	CacheEntry *entry = &cache->table[key];
	while (entry->next != NULL) {
		entry = entry->next;
	}

	//pthread_mutex_unlock(&cache->lock);
	//return 0;

	entry->next = (CacheEntry *) malloc(sizeof(CacheEntry));
	assert(entry->next);
	entry = entry->next;
	entry->data = file;
	entry->in_use = 0;
	entry->next = NULL;
	cache->size += file->file_size;

	add_to_LRU(cache->LRU, entry);
	//pthread_mutex_unlock(&cache->lock);
	return 1;
}

void cache_clear(Cache *cache) {
	for (int i=0; i<cache->capacity; ++i) {
		CacheEntry *entry = &cache->table[i];
		entry = entry->next;
		while (entry != NULL) {
			CacheEntry *temp = entry->next;
			file_data_free(entry->data);
			free(entry);
			entry = temp;
		}
	}
}

void cache_destroy(Cache *cache) {
	if (cache == NULL)
		return;
	pthread_mutex_lock(&cache->lock);
	cache_clear(cache);
	free(cache->table);

	destroy_LRU(cache->LRU);

	//pthread_mutex_destroy(&cache->lock_entry);
	//pthread_mutex_destroy(&cache->lock_exit);
	//pthread_mutex_destroy(&cache->lock_data);
	//pthread_cond_destroy(&cache->cv_rc);

	free(cache);
	pthread_mutex_unlock(&cache->lock);
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
	CacheEntry *cache_value = NULL;
	if (sv->cache)
		cache_value = cache_lookup(sv->cache, data->file_name);
	if (cache_value != NULL) {
		pthread_mutex_lock(&sv->cache->lock);
		cache_value->in_use++;
		printf("%lu is being used. Use count: %d\n", (unsigned long) cache_value, cache_value->in_use);
		pthread_mutex_unlock(&sv->cache->lock);
		file_data_free(data);
		request_set_data(rq, cache_value->data);
	} else {
		/* read file, 
		* fills data->file_buf with the file contents,
		* data->file_size with file size. */
		ret = request_readfile(rq);
		if (ret == 0) { /* couldn't read file */
			goto out;
		} else {
			// Add file to cache
			pthread_mutex_lock(&sv->cache->lock);
			printf("About to add file to cache\n");
			if (sv->cache)
				cache_inserted = cache_insert(sv->cache, data);
			printf("Cache inserted: %d\n", cache_inserted);
			pthread_mutex_unlock(&sv->cache->lock);
		}
	}

	
	/* send file to client */
	request_sendfile(rq);
	if (cache_value) {
		pthread_mutex_lock(&sv->cache->lock);
		cache_value->in_use--;
		printf("%lu no longer being used. Use count: %d\n", (unsigned long) cache_value, cache_value->in_use);
		assert(cache_value->in_use >= 0);
		pthread_mutex_unlock(&sv->cache->lock);
	}
out:
	request_destroy(rq);
	//if (!cache_inserted)
	//	file_data_free(data);
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
		Cache *cache = sv->cache;

		cache->max_cache_size = max_cache_size;
		cache->capacity = 5000;
		cache->table = (CacheEntry *) calloc(sv->cache->capacity, sizeof(CacheEntry));
		assert(cache->table);
		cache->size = 0;
		pthread_mutex_init(&cache->lock, NULL);
		//pthread_mutex_init(&cache->lock_entry, NULL);
		//pthread_mutex_init(&cache->lock_exit, NULL);
		//pthread_mutex_init(&cache->lock_data, NULL);
		//pthread_cond_init(&cache->cv_rc, NULL);

		cache->LRU = (LRUList *) malloc(sizeof(LRUList));
		assert(cache->LRU);
		LRUList *LRU = cache->LRU;
		LRU->head = NULL;
		LRU->tail = NULL;
		LRU->size = 0;
		//pthread_mutex_init(&LRU->lock_lru, NULL);
	} else sv->cache = NULL;

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
	//if (sv->cache)
	//	cache_destroy(sv->cache);

	//pthread_cond_destroy(&sv->cv_empty);
	//pthread_cond_destroy(&sv->cv_full);
	//pthread_mutex_destroy(&sv->lock);

	free(sv);
}
