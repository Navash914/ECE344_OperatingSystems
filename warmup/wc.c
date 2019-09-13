#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "wc.h"
#include <string.h>
#include <ctype.h>

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

struct wc_ele {
	char *word;
	int num_occurrences;
	struct wc_ele *next;
};

struct wc {
	struct wc_ele *hash_table;
	long size;
	long capacity;
};

struct wc *
wc_init(char *word_array, long size)
{
	struct wc *wc;

	wc = (struct wc *)malloc(sizeof(struct wc));
	assert(wc);

	wc->hash_table = (struct wc_ele *) calloc(size, sizeof(struct wc_ele));
	wc->capacity = size;
	wc->size = 0;

	char current_word[100];
	char *word_buffer = current_word;
	*word_buffer = '\0';

	for (int i=0; i<size; ++i) {
		char c = word_array[i];
		if (isspace(c)) {
			if (strlen(current_word) == 0)
				continue;
			else {
				// Add word to hash table
				unsigned long hashed_word = hash(current_word) % wc->capacity;
				struct wc_ele *ele = &wc->hash_table[hashed_word];
				int exists = 0;
				while (ele->next != NULL) {
					ele = ele->next;
					if (strcmp(current_word, ele->word) == 0) {
						ele->num_occurrences++;
						exists = 1;
						break;
					}
				}

				if (!exists) {
					ele->next = (struct wc_ele *) malloc(sizeof(struct wc_ele));
					ele->next->word = (char *) malloc((strlen(current_word)+1) * sizeof(char));
					strcpy(ele->next->word, current_word);
					ele->next->num_occurrences = 1;
					ele->next->next = NULL;
					wc->size++;
				}

				word_buffer = current_word;
				*word_buffer = '\0';
			}
		} else {
			*word_buffer++ = c;
			*word_buffer = '\0';
		}
	}

	return wc;
}

void
wc_output(struct wc *wc)
{
	int num_printed = 0;
	for (int i=0; i<wc->capacity; ++i) {
		struct wc_ele *ele = &wc->hash_table[i];
		while (ele->next != NULL) {
			ele = ele->next;
			printf("%s:%d\n", ele->word, ele->num_occurrences);
			num_printed++;
		}
		if (num_printed == wc->size)
			break;
	}
}

void
wc_destroy(struct wc *wc)
{
	for (int i=0; i<wc->capacity; ++i) {
		struct wc_ele *ele = &wc->hash_table[i];
		ele = ele->next;
		while (ele != NULL) {
			struct wc_ele *temp = ele->next;
			free(ele->word);
			free(ele);
			ele = temp;
		}
	}
	free(wc->hash_table);
	free(wc);
}
