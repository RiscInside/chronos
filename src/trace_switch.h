#ifndef __CHRONOS_TRACE_SWITCH_H__
#define __CHRONOS_TRACE_SWITCH_H__

// TCB address expressed as an integer
typedef size_t chronos_tcb_addr_t;

// vCPU index
typedef size_t chronos_vcpu_index_t;

// Preemption events in the ring buffer
struct chronos_switch_event {
	// Address of the thread's TCB
	chronos_tcb_addr_t tcb_addr;
	// vCPU TCB belongs to
	chronos_vcpu_index_t vcpu;
	// Timestamp
	size_t ts;
	// CPU id event originates from
	size_t cpu;
	// Event type
	enum {
		// Thread has finished its execution
		CHRONOS_SWITCH_EV_EXIT = 0,
		// Thread is blocked on something/sleeping
		CHRONOS_SWITCH_EV_SUSPEND = 1,
		// Thread has been woken up
		CHRONOS_SWITCH_EV_RESUME = 2,
	} ty;
};

#endif
