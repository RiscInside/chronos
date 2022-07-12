#pragma once

// "pairing_heap.h" - simple intrusive pairing heap implementation

#include <stddef.h>

// Pairing heap node
typedef struct pairing_heap_node {
	// First heap in the child list
	struct pairing_heap_node *first;
	// Next heap in the list
	struct pairing_heap_node *next;
	// Node key
	size_t key;
} pairing_heap_node_t;

// Pairing heap type itself
typedef pairing_heap_node_t *pairing_heap_t;

// Pairing heap default value
#define PAIRING_HEAP_EMPTY NULL

// Get pointer to the minimum node
inline static pairing_heap_node_t *pairing_heap_get_min(pairing_heap_t const *heap) {
	return *heap;
}

// Meld two pairing heaps
inline static pairing_heap_node_t *_pairing_heap_meld(pairing_heap_node_t *node1, pairing_heap_node_t *node2) {
	if (!node1) {
		return node2;
	} else if (!node2) {
		return node1;
	} else if (node1->key < node2->key) {
		// Add node2 as node1's first child
		node1->next = NULL;
		node2->next = node1->first;
		node1->first = node2;
		return node1;
	}
	// Add node1 as node2's first child
	node2->next = NULL;
	node1->next = node2->first;
	node2->first = node1;
	return node2;
}

// Insert node in the pairing heap
inline static void pairing_heap_insert(pairing_heap_t *heap, pairing_heap_node_t *node) {
	node->first = NULL;
	node->next = NULL;
	*heap = _pairing_heap_meld(*heap, node);
}

// Merge subheap pairs
inline static pairing_heap_node_t *_pairing_heap_merge_pairs(pairing_heap_node_t *list_head) {
	if (list_head == NULL) {
		return NULL;
	} else if (list_head->next == NULL) {
		return list_head;
	}
	pairing_heap_node_t *next_pair = list_head->next->next;
	return _pairing_heap_meld(_pairing_heap_meld(list_head, list_head->next), _pairing_heap_merge_pairs(next_pair));
}

// Remove minimum node from the heap
inline static pairing_heap_node_t *pairing_heap_del_min(pairing_heap_t *heap) {
	if (!*heap) {
		return NULL;
	}
	pairing_heap_node_t *const res = *heap;
	*heap = _pairing_heap_merge_pairs(res->first);
	return res;
}
