#ifndef __CHRONOS_TRACE_SWITCH_H__
#define __CHRONOS_TRACE_SWITCH_H__

// TCB address expressed as an integer
typedef uintptr_t chronos_tcb_addr_t;

// Preemption events in the ring buffer
struct chronos_switch_event {
	// Address of the thread's TCB
	chronos_tcb_addr_t tcb_addr;
	// Event type
	enum {
		// Thread has finished its execution
		CHRONOS_SWITCH_EV_EXIT = 0,
	};
};

#endif
