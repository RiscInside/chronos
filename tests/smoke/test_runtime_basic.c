#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <chronosrt.h>
#include <stdio.h>

#include "timeline.h"

int main() {
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(0, &set);
	CPU_SET(1, &set);

	chronosrt_init(2, &set, 1000000);
	printf("runtime up\n");

	timeline_spin_for(1000);

	chronosrt_detatch();
	printf("runtime down\n");
}
