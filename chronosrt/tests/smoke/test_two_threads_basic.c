#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <chronosrt.h>
#include <pthread.h>
#include <stdio.h>

#include "timeline.h"

timeline_event_cnt_t ev_cnt = 0;

struct params {
	void *rt;
	size_t vruntime;
};

void *other_thread_func(void *arg) {
	struct params *params = (struct params *)arg;
	chronosrt_submit(params->rt, params->vruntime);
	timeline_event_happened(&ev_cnt, 1);
	printf("other thread up\n");

	timeline_spin_for(1000);

	chronosrt_detatch();
	timeline_event_happened(&ev_cnt, 3);
	printf("other thread done\n");
	return NULL;
}

int main() {
	pthread_t other_thread;
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(0, &set);
	CPU_SET(1, &set);
	CPU_SET(2, &set);

	void *rt = chronosrt_init(2, &set, 1000000);
	printf("runtime up\n");
	timeline_event_happened(&ev_cnt, 0);

	timeline_spin_for(200);

	struct params params;
	params.rt = rt;
	params.vruntime = chronosrt_get_vruntime();

	if (pthread_create(&other_thread, NULL, other_thread_func, &rt) != 0) {
		fprintf(stderr, "failed to spawn other thread\n");
		return EXIT_FAILURE;
	}

	timeline_spin_for(500);
	timeline_event_happened(&ev_cnt, 2);

	pthread_join(other_thread, NULL);

	chronosrt_detatch();
	printf("runtime down\n");
}
