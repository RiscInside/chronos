#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <chronos.h>
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
	chronos_rt_submit(params->rt, params->vruntime);
	timeline_event_happened(&ev_cnt, 1);
	printf("other thread up\n");

	// wrooom
	chronos_set_tdf(10.0);

	timeline_spin_for(10000);
	timeline_event_happened(&ev_cnt, 2);

	chronos_rt_detatch();
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

	void *rt = chronos_rt_init(2, &set, 1000000);
	printf("runtime up\n");
	timeline_event_happened(&ev_cnt, 0);

	timeline_spin_for(2000);

	struct params params;
	params.rt = rt;
	params.vruntime = chronos_get_vruntime();

	if (pthread_create(&other_thread, NULL, other_thread_func, &rt) != 0) {
		fprintf(stderr, "failed to spawn other thread\n");
		return EXIT_FAILURE;
	}

	timeline_spin_for(5000);
	timeline_event_happened(&ev_cnt, 3);

	pthread_join(other_thread, NULL);

	chronos_rt_detatch();
	printf("runtime down\n");
}
