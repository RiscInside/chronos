#ifndef __CHRONOS_H_INCLUDED__
#define __CHRONOS_H_INCLUDED__

#include <assert.h>
#include <stddef.h>

typedef struct chronos_pairing_heap_node {
	struct chronos_pairing_heap_node *first;
	struct chronos_pairing_heap_node *next;
	struct chronos_pairing_heap_node *backlink;
	size_t key;
} chronos_pairing_heap_node_t;

typedef chronos_pairing_heap_node_t *chronos_pairing_heap_t;

#define CHRONOS_PAIRING_HEAP_EMPTY NULL

inline static chronos_pairing_heap_node_t *chronos_pairing_heap_get_min(chronos_pairing_heap_t const *heap) {
	return *heap;
}

inline static chronos_pairing_heap_node_t *_chronos_pairing_heap_meld(chronos_pairing_heap_node_t *node1,
                                                                      chronos_pairing_heap_node_t *node2) {
	if (!node1) {
		return node2;
	} else if (!node2) {
		return node1;
	} else if (node1->key <= node2->key) {
		node1->next = NULL;
		node2->next = node1->first;
		if (node1->first != NULL) {
			node1->first->backlink = node2;
		}
		node1->first = node2;
		node2->backlink = node1;
		return node1;
	}
	node2->next = NULL;
	node1->next = node2->first;
	if (node2->first != NULL) {
		node2->first->backlink = node1;
	}
	node2->first = node1;
	node1->backlink = node2;
	return node2;
}

inline static void chronos_pairing_heap_insert(chronos_pairing_heap_t *heap, chronos_pairing_heap_node_t *node) {
	node->first = NULL;
	node->next = NULL;
	node->backlink = NULL;
	*heap = _chronos_pairing_heap_meld(*heap, node);
}

inline static chronos_pairing_heap_node_t *_chronos_pairing_heap_merge_pairs(chronos_pairing_heap_node_t *list_head) {
	if (list_head == NULL) {
		return NULL;
	} else if (list_head->next == NULL) {
		return list_head;
	}
	chronos_pairing_heap_node_t *first_elem = list_head;
	chronos_pairing_heap_node_t *second_elem = list_head->next;
	first_elem->backlink = NULL;
	second_elem->backlink = NULL;
	chronos_pairing_heap_node_t *list_cont = second_elem->next;
	return _chronos_pairing_heap_meld(_chronos_pairing_heap_meld(first_elem, second_elem),
	                                  _chronos_pairing_heap_merge_pairs(list_cont));
}

inline static chronos_pairing_heap_node_t *chronos_pairing_heap_del_min(chronos_pairing_heap_t *heap) {
	if (!*heap) {
		return NULL;
	}
	chronos_pairing_heap_node_t *const res = *heap;
	if (res->first != NULL) {
		res->first->backlink = NULL;
	}
	*heap = _chronos_pairing_heap_merge_pairs(res->first);
	return res;
}

inline static void chronos_pairing_heap_decrease_key(chronos_pairing_heap_t *heap, chronos_pairing_heap_node_t *node,
                                                     size_t new_key) {
	if (node->backlink == NULL) {
		return;
	}
	if (node->backlink->first == node) {
		chronos_pairing_heap_node_t *parent = node->backlink;
		parent->first = node->next;
		if (node->next != NULL) {
			node->next->backlink = parent;
		}
	} else {
		chronos_pairing_heap_node_t *prev_sibling = node->backlink;
		prev_sibling->next = node->next;
		if (node->next != NULL) {
			node->next->backlink = prev_sibling;
		}
	}
	node->key = new_key;
	*heap = _chronos_pairing_heap_meld(node, *heap);
}

inline static void chronos_pairing_heap_del(chronos_pairing_heap_t *heap, chronos_pairing_heap_node_t *node) {
	chronos_pairing_heap_decrease_key(heap, node, 0);
	assert(*heap == node);
	chronos_pairing_heap_del_min(heap);
}

#endif
