// The order of headers below is important, as bpf_helpers.h depends on vmlinux.h being included
// clang-format off
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
// clang-format on
#include <trace_switch.h>

// How many threads can this BPF probe handle?
#define MAX_THREADS 16384

// How many events can ring buffer hold
#define MAX_EVENTS 16384

SEC("tracepoint/sched/sched_switch") // Called on every context switch
int tracepoint__sched__sched_switch(struct trace_event_raw_sched_switch *ctx) {
	(void)ctx;
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
