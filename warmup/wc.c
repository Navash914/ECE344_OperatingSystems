#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "wc.h"

struct wc {
	/* you can define this struct to have whatever fields you want. */
};

struct wc *
wc_init(char *word_array, long size)
{
	struct wc *wc;

	wc = (struct wc *)malloc(sizeof(struct wc));
	assert(wc);

	TBD();

	return wc;
}

void
wc_output(struct wc *wc)
{
	TBD();
}

void
wc_destroy(struct wc *wc)
{
	TBD();
	free(wc);
}
