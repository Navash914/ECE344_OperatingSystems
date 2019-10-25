#include "request.h"
#include "server_thread.h"
#include "common.h"

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
};

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
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
	/* read file, 
	 * fills data->file_buf with the file contents,
	 * data->file_size with file size. */
	ret = request_readfile(rq);
	if (ret == 0) { /* couldn't read file */
		goto out;
	}
	/* send file to client */
	request_sendfile(rq);
out:
	request_destroy(rq);
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
		// TODO in Lab 5
	}

	/* Lab 4: create queue of max_request size when max_requests > 0 */

	/* Lab 5: init server cache and limit its size to max_cache_size */

	/* Lab 4: create worker threads when nr_threads > 0 */

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
