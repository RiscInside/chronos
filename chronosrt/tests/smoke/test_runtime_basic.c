#include <chronosrt/chronosrt.h>
#include <chronosrt/cpuset.h>
#include <timeline.h>

int main() {
	struct chronosrt_cfg cfg;
	cfg.cpuset = chronosrt_cpuset_create_with_one_set(0); // only give one cpu
	cfg.max_vruntime_diff = 500000000;                    // allow for max 100 milliseconds mismatch
	cfg.ns_min_timeslice = 500000000;                     // reschedule every 100 milliseconds

	chronosrt_instantiate_runtime(&cfg);

	timeline_spin_for(10000);

	chronosrt_detach();
}
