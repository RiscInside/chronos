// The order of headers below is important, as bpf_helpers.h depends on vmlinux.h being included
// clang-format off
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
// clang-format on
#include <trace_switch.h>

// How many threads can this BPF probe handle?
#define MAX_THREADS 16384

// How many events can ring buffer hold
#define MAX_EVENTS_BYTES 16384

// TODO: I have no idea where am I meant to get this constant from, so I am just going to go ahead and hardcode it
#define TASK_ZOMBIE_FLAG 0x20

// Mappings from thread IDs to TCB userspace addresses
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_THREADS);
	__type(key, pid_t);
	__type(value, chronos_tcb_addr_t);
} tcb_ptrs SEC(".maps");

// Mapping from thread IDs to CPUs tasks are assigned to run on
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_THREADS);
	__type(key, pid_t);
	__type(value, chronos_vcpu_index_t);
} on_exit_mapped_to_vcpu SEC(".maps");

// Ringbuffer to notify userspace about exits
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, sizeof(struct chronos_switch_event) * MAX_EVENTS_BYTES);
} ev_ringbuffer SEC(".maps");

// Report exit event to the chronos runtime
static void chronos_trace_switch_report_exit(chronos_vcpu_index_t vcpu_index) {
	// Allocate space for the event record on the ringbuffer
	struct chronos_switch_event *event = bpf_ringbuf_reserve(&ev_ringbuffer, sizeof(struct chronos_switch_event), 0);
	if (!event) {
		// TODO: maybe it would be a good idea to use a separate notification channel to tell the runtime BPF probe has
		// panicked?
		bpf_printk("BPF probe: failed to submit exit notification");
		return;
	}

	// Fill out event record fields
	event->tcb_addr = 0;
	event->vcpu = vcpu_index;
	event->ty = CHRONOS_SWITCH_EV_EXIT;

	// Shoot the event to userspace
	// NOTE: we are using BPF_RB_NO_WAKEUP since scheduler never sleeps
	bpf_ringbuf_submit(event, BPF_RB_NO_WAKEUP);
}

SEC("tracepoint/sched/sched_switch") // Called on every context switch
int tracepoint__sched__sched_switch(struct trace_event_raw_sched_switch *ctx) {
	// Check prev_state and next_pid to figure out what kind of event are we dealing with
	if (!(ctx->prev_state & TASK_ZOMBIE_FLAG)) {
		// Get core this process is meant to run on. This step also filters threads that do not belong to the framework
		chronos_vcpu_index_t *vcpu_index_ptr;
		pid_t key = ctx->prev_pid;
		vcpu_index_ptr = bpf_map_lookup_elem(&on_exit_mapped_to_vcpu, &key);
		if (!vcpu_index_ptr) {
			// Thread does not belong to the framework
			return 0;
		}
		// Report exit event to the userspace
		chronos_trace_switch_report_exit(*vcpu_index_ptr);
		return 0;
	}
	// TODO: No other events relevant to us as of now, but we also need to catch suspensions in the future

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
