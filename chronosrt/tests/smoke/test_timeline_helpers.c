#include "timeline.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

timeline_event_cnt_t ev_cnt = 0;

void thread1() {
	timeline_spin_for(750);
	timeline_event_happened(&ev_cnt, 2);
}

void thread2() {
	timeline_spin_for(250);
	timeline_event_happened(&ev_cnt, 0);
}

void thread3() {
	timeline_spin_for(500);
	timeline_event_happened(&ev_cnt, 1);
}

int main() {
	pthread_t pthread2, pthread3;
	if (pthread_create(&pthread2, NULL, (void *)thread2, NULL) != 0) {
		fprintf(stderr, "failed to spawn thread2\n");
		return EXIT_FAILURE;
	}
	if (pthread_create(&pthread3, NULL, (void *)thread3, NULL) != 0) {
		fprintf(stderr, "failed to spawn thread3\n");
		return EXIT_FAILURE;
	}
	thread1();
	if (pthread_join(pthread2, NULL)) {
		fprintf(stderr, "failed to join thread 2\n");
		return EXIT_FAILURE;
	}
	if (pthread_join(pthread3, NULL)) {
		fprintf(stderr, "failed to join thread 2\n");
		return EXIT_FAILURE;
	}
}
