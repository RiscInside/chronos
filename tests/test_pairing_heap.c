#include <pairing_heap.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ELEMS 10

int rand_comp(void const *a, void const *b) {
	(void)a;
	(void)b;
	return rand() % 2 == 0 ? -1 : 1;
}

int main() {
	time_t seed = time(NULL);
	fprintf(stderr, "using seed: %llu\n", (unsigned long long)seed);
	srand(seed);
	// Create an array of heap nodes with keys from 0 to ELEMS and shuffle the array using qsort
	pairing_heap_node_t *elems = malloc(ELEMS * sizeof(pairing_heap_node_t));
	for (int i = 0; i < ELEMS; ++i) {
		elems[i].key = i;
	}
	qsort(elems, ELEMS, sizeof(pairing_heap_node_t), rand_comp);
	// Add nodes to the heap one by one
	pairing_heap_t heap = NULL;
	for (int i = 0; i < ELEMS; ++i) {
		pairing_heap_insert(&heap, elems + i);
	}
	// Check that nodes are dequeued in the order we expect
	for (int i = 0; i < ELEMS; ++i) {
		pairing_heap_node_t *node = pairing_heap_del_min(&heap);
		if (node->key != i) {
			fprintf(stderr, "got %zu on iteration %d\n", node->key, i);
			return EXIT_FAILURE;
		}
	}
	// Check that heap is now empty
	if (pairing_heap_get_min(&heap) != NULL) {
		fprintf(stderr, "heap is not empty\n");
		return EXIT_FAILURE;
	}
	free(elems);
}
