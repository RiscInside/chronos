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

// TODO: I have no idea where am I meant to get these constants from, so I am just going to go ahead and hardcode them
// Values are obtained from /sys/kernel/debug/tracing/events/sched/sched_switch/format (mount debugfs before peeking)
#define TASK_S_FLAG 0x01 // "S" in /proc/[pid]/stat, "Sleeping in an interruptible wait"
#define TASK_D_FLAG 0x02 // "D" in /proc/[pid]/stat, "Waiting in uninterruptible disk sleep"
#define TASK_T_FLAG 0x04 // "T" in /proc/[pid]/stat, "Stopped (on a signal) or (before Linux 2.6.33) trace stopped"
#define TASK_t_FLAG 0x08 // "t" in /proc/[pid]/stat, "Tracing stop"
#define TASK_X_FLAG 0x10 // "X" in /proc/[pid]/stat, "Dead"
#define TASK_Z_FLAG 0x20 // "Z" in /proc/[pid]/stat, "Zombie"

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
	struct chronos_switch_event event = {0};
	event.vcpu = vcpu_index;
	event.ty = CHRONOS_SWITCH_EV_EXIT;
	event.ts = bpf_ktime_get_ns();
	event.cpu = bpf_get_smp_processor_id();
	if (bpf_ringbuf_output(&ev_ringbuffer, &event, sizeof(struct chronos_switch_event), BPF_RB_NO_WAKEUP) < 0) {
		bpf_printk("chronos/BPF: Failed to submit exit notification (vcpu: %zu)", vcpu_index);
	}
}

// Report suspension event to the chronos runtime
static void chronos_trace_switch_report_suspend(chronos_tcb_addr_t userspace_tcb_addr) {
	struct chronos_switch_event event = {0};
	event.tcb_addr = userspace_tcb_addr;
	event.ty = CHRONOS_SWITCH_EV_SUSPEND;
	event.ts = bpf_ktime_get_ns();
	event.cpu = bpf_get_smp_processor_id();
	if (bpf_ringbuf_output(&ev_ringbuffer, &event, sizeof(struct chronos_switch_event), BPF_RB_NO_WAKEUP) < 0) {
		bpf_printk("chronos/BPF: Failed to submit exit notification (tcb_addr: %zu)", userspace_tcb_addr);
	}
}
// Report resumption event to the chronos runtime
static void chronos_trace_switch_report_resume(chronos_tcb_addr_t userspace_tcb_addr) {
	struct chronos_switch_event event = {0};
	event.tcb_addr = userspace_tcb_addr;
	event.ty = CHRONOS_SWITCH_EV_RESUME;
	event.ts = bpf_ktime_get_ns();
	event.cpu = bpf_get_smp_processor_id();
	if (bpf_ringbuf_output(&ev_ringbuffer, &event, sizeof(struct chronos_switch_event), BPF_RB_NO_WAKEUP) < 0) {
		bpf_printk("chronos/BPF: Failed to submit resume notification (tcb_addr: %zu)", userspace_tcb_addr);
	}
}

SEC("tracepoint/sched/sched_switch") // Called on every context switch
int tracepoint__sched__sched_switch(struct trace_event_raw_sched_switch *ctx) {
	// Check prev_state and next_pid to figure out what kind of event are we dealing with
	if (ctx->prev_state & (TASK_Z_FLAG | TASK_X_FLAG)) {
		// Thread termination event (prev_pid is a zombie or a dead process)
		// Get core this process is meant to run on. This step also filters threads that do not belong to the framework
		pid_t key = ctx->prev_pid;
		chronos_vcpu_index_t *vcpu_index_ptr = bpf_map_lookup_elem(&on_exit_mapped_to_vcpu, &key);
		if (!vcpu_index_ptr) {
			// Thread does not belong to the framework
			return 0;
		}
		// Report exit event to the userspace
		chronos_trace_switch_report_exit(*vcpu_index_ptr);
		return 0;
	} else if (ctx->prev_state & (TASK_S_FLAG | TASK_D_FLAG | TASK_T_FLAG | TASK_t_FLAG)) {
		(void)chronos_trace_switch_report_suspend;
		// Suspension event
		// Get thread's TCB userspace address
		pid_t key = ctx->prev_pid;
		chronos_tcb_addr_t *tcb_addr_ptr = bpf_map_lookup_elem(&tcb_ptrs, &key);
		if (!tcb_addr_ptr) {
			// Thread does not belong to the framework
			return 0;
		}
		// Report suspension event to the userspace
		chronos_trace_switch_report_suspend(*tcb_addr_ptr);
	}
	// Other events are irrelevant to us
	return 0;
}

SEC("tracepoint/sched/sched_waking") // Called on every wakeup (in the waker context)
int tracepoint__sched__sched_waking(struct trace_event_raw_sched_wakeup_template *ctx) {
	pid_t key = ctx->pid;
	// Get thread's TCB userspace address
	chronos_tcb_addr_t *tcb_addr_ptr = bpf_map_lookup_elem(&tcb_ptrs, &key);
	if (!tcb_addr_ptr) {
		// Thread does not belong to the framework
		return 0;
	}
	// Report resumption event to the userspace
	chronos_trace_switch_report_resume(*tcb_addr_ptr);
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
