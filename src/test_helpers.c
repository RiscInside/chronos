#include <chronos/chronosrt.h>
#include <ctype.h>
#include <log.h>
#include <ns.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <test_helpers.h>

// CPU list component. Binary equivalent of X[-Y[:Z]] strings in CPU list, where X is min_cpu_inclusive, Y + 1 is
// max_cpu_exclusive, and Z is stride
struct chronos_cpulist_component {
	size_t min_cpu_inclusive;
	size_t max_cpu_exclusive;
	size_t stride;
};

static void chronos_skip_spaces(char **str) {
	while (isspace(**str)) {
		(*str)++;
	}
}

// TODO: This is probably good enough for tests, but it can certainly be better
static bool chronos_get_cpulist_component(char **str, char *full_cpumask, struct chronos_cpulist_component *component) {
	char *head = *str;
	if (*head == ',') {
		head++;
	}
	if (*head == '\0') {
		return false;
	}
	char *old_head = head;
	long first_cpu = strtol(head, &head, 10);
	if (head == old_head || first_cpu < 0) {
		dprintf(2, "error: invalid cpulist `%s`: expected non-negative integer at position %zu\n", full_cpumask,
		        (old_head - full_cpumask) + 1);
		exit(EXIT_FAILURE);
	}
	component->min_cpu_inclusive = (size_t)first_cpu;
	chronos_skip_spaces(&head);
	if (*head == ',' || *head == '\0') {
		component->max_cpu_exclusive = component->min_cpu_inclusive + 1;
		component->stride = 1;
		*str = head;
		return true;
	} else if (*head != '-') {
		dprintf(2, "error: invalid cpulist `%s`: unexpected character at position %zu\n", full_cpumask,
		        (head - full_cpumask) + 1);
		exit(EXIT_FAILURE);
	}
	head++;
	chronos_skip_spaces(&head);
	old_head = head;
	long last_cpu = strtol(head, &head, 10);
	if (head == old_head || last_cpu < 0) {
		dprintf(2, "error: invalid cpulist `%s`: expected non-negative integer at position %zu\n", full_cpumask,
		        (old_head - full_cpumask) + 1);
		exit(EXIT_FAILURE);
	}
	component->max_cpu_exclusive = ((size_t)last_cpu) + 1;
	if (component->max_cpu_exclusive <= component->min_cpu_inclusive) {
		dprintf(2, "error: invalid cpulist `%s`: invalid range %zu-%zu\n", full_cpumask, component->min_cpu_inclusive,
		        component->max_cpu_exclusive - 1);
		exit(EXIT_FAILURE);
	}
	chronos_skip_spaces(&head);
	if (*head == ',' || *head == '\0') {
		component->stride = 1;
		*str = head;
		return true;
	} else if (*head != ':') {
		dprintf(2, "error: invalid cpulist `%s`: unexpected character at position %zu\n", full_cpumask,
		        (head - full_cpumask) + 1);
		exit(EXIT_FAILURE);
	}
	head++;
	chronos_skip_spaces(&head);
	old_head = head;
	long stride = strtol(head, &head, 10);
	if (head == old_head || stride < 1) {
		dprintf(2, "error: invalid cpulist `%s`: expected positive integer at position %zu\n", full_cpumask,
		        (old_head - full_cpumask) + 1);
		exit(EXIT_FAILURE);
	}
	component->stride = (size_t)stride;
	if (*head == ',' || *head == '\0') {
		return true;
	}
	dprintf(2, "error: invalid cpulist `%s`: unexpected character at position %zu\n", full_cpumask,
	        (head - full_cpumask) + 1);
	exit(EXIT_FAILURE);
}

// Parse cpuset from string
chronos_cpuset_t chronos_parse_cpuset(char *str) {
	char *current_head = str;
	// Determine cpuset size
	size_t cpus = 0;
	struct chronos_cpulist_component component;
	while (chronos_get_cpulist_component(&current_head, str, &component)) {
		cpus = component.max_cpu_exclusive > cpus ? component.max_cpu_exclusive : cpus;
	}
	chronos_cpuset_t result = chronos_cpuset_create_empty(cpus);
	// Add selected CPUs to the CPU set
	current_head = str;
	size_t cpus_set = 0;
	while (chronos_get_cpulist_component(&current_head, str, &component)) {
		for (size_t i = component.min_cpu_inclusive; i < component.max_cpu_exclusive; i += component.stride) {
			if (!chronos_cpuset_is_cpu_included(result, i)) {
				cpus_set++;
				chronos_cpuset_include_cpu(result, i);
			}
		}
	}
	if (cpus_set < 2) {
		dprintf(2, "error: invalud cpulist `%s`: need at least two CPUs to be set", str);
		exit(EXIT_FAILURE);
	}
	return result;
}

// Parse command-line option specifying duration.
// argc and argv are to be passed as to int main()
// current_pos is a cursor that will be shifted by 1 if option is accepted
// name is an option name, abbrev is an optional shorter abbreviation
// Duration in nanoseconds is stored in ns_out
static bool chronos_parse_time_cmdline_arg(int argc, char **argv, int *current_pos, const char *name,
                                           const char *abbrev, bool *guard, size_t *ns_out) {
	chronos_assert(*current_pos < argc);
	// Check if argument is applicable
	if (strcmp(argv[*current_pos], name) && (abbrev == NULL || strcmp(argv[*current_pos], abbrev))) {
		return false;
	}
	// Check that duration parameter has been passed on the command line
	if (*current_pos + 1 >= argc) {
		dprintf(2, "error: invalid %s usage: duration parameter expected for %s\n", name, name);
		exit(EXIT_FAILURE);
	}
	// Check that this command line argument has not been supplied before
	if (*guard) {
		dprintf(2, "error: duplicate %s option\n", name);
		exit(EXIT_FAILURE);
	}
	*guard = true;    // Makes subsequent duplicate checks fail
	(*current_pos)++; // Move on from the argument, second increment is done by the parsing loop
	char *time_string = argv[*current_pos];
	char *time_string_unit;
	long value = strtol(time_string, &time_string_unit, 10);
	if (value < 0) {
		goto arg_fail;
	}
	if (*time_string_unit == '\0' || !strcmp(time_string_unit, "ns")) {
		// Nanoseconds
		*ns_out = (size_t)value;
	} else if (!strcmp(time_string_unit, "us")) {
		// Microseconds
		*ns_out = 1000 * (size_t)value;
	} else if (!strcmp(time_string_unit, "ms")) {
		// Milliseconds
		*ns_out = 1000000 * (size_t)value;
	} else if (!strcmp(time_string_unit, "s")) {
		// Seconds
		*ns_out = 100000000 * (size_t)value;
	} else {
		// Unrecognised unit
		goto arg_fail;
	}
	return true;
arg_fail:
	dprintf(2, "error: expected duration (e.g. 10040234, 103ms, or 1us) as an argument for %s\n", name);
	exit(EXIT_FAILURE);
}

// Parse command-line arguments passed to the test and initialize chronos runtime based on them, rewriting arguments in
// the processs (so that test can only see test-specific arguments)
void chronos_test_init_runtime(int *argc_out, char **argv) {
	int argc = *argc_out;

	// Set default values for Chronos runtime config
	struct chronosrt_config cfg;
	cfg.min_local_timeslice = 1000000;                 // 1ms in virtual time
	cfg.suspend_lag_allowance = 20000000;              // 20 ms in virtual time
	cfg.resume_lag_allowance = 10000000;               // 10 ms in virtual time
	cfg.barriers_deadline = 1000000000;                // 1 second in real time
	cfg.reschedule_penalty = 0;                        // no rescheduling penalty
	cfg.available_cpus = chronos_cpuset_create_raw(2); // use two first CPUs
	cfg.runtime_mode = FULL;                           // actually run tests under framework
	chronos_cpuset_include_cpu(cfg.available_cpus, 0);
	chronos_cpuset_include_cpu(cfg.available_cpus, 1);

	// Parse command line arguments
	int new_argc = 1; // New value of argc to be returned to the caller
	bool cpulist_option_set = false;
	bool min_local_timeslice_option_set = false;
	bool suspend_lag_allowance_option_set = false;
	bool resume_lag_allowance_option_set = false;
	bool reschedule_penalty_option_set = false;
	bool barriers_deadline_option_set = false;
	bool framework_mode_set = false;

	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--cpulist") || !strcmp(argv[i], "-C")) {
			// List of CPUs that framework can use
			if (cpulist_option_set) {
				dprintf(2, "error: duplicate --cpulist arguments are not allowed\n");
				exit(EXIT_FAILURE);
			}
			cpulist_option_set = true;
			++i;
			if (i >= argc) {
				dprintf(2, "error: invalid --cpulist usage: cpulist argument required (e.g. --cpulist 0-1)\n");
				exit(EXIT_FAILURE);
			}
			chronos_cpuset_destroy(cfg.available_cpus);
			cfg.available_cpus = chronos_parse_cpuset(argv[i]);
			continue;
		} else if (!strcmp(argv[i], "--nop-mode") || !strcmp(argv[i], "-N")) {
			// NOP mode, no framework operations.
			if (framework_mode_set) {
				dprintf(2, "error: framework mode has been already set\n");
				exit(EXIT_FAILURE);
			}
			framework_mode_set = true;
			cfg.runtime_mode = NOP;
			continue;
		} else if (chronos_parse_time_cmdline_arg(argc, argv, &i, "--min_local_timeslice", "-L",
		                                          &min_local_timeslice_option_set, &cfg.min_local_timeslice)) {
			// Minimum local timeslice, scheduler won't switch threads in the framework more often than that
		} else if (chronos_parse_time_cmdline_arg(argc, argv, &i, "--suspend_lag_allowance", "-S",
		                                          &suspend_lag_allowance_option_set, &cfg.suspend_lag_allowance)) {
			// Suspend lag allowance, aka how far ahead can vCPUs get in virtual time before getting suspended by the
			// scheduler
		} else if (chronos_parse_time_cmdline_arg(argc, argv, &i, "--resume_lag_allowance", "-R",
		                                          &resume_lag_allowance_option_set, &cfg.resume_lag_allowance)) {
			// Resume lag allowance, aka how close vCPU has to be to the global simulation time in order to be unfreezed
			// after suspend
		} else if (chronos_parse_time_cmdline_arg(argc, argv, &i, "--reschedule_penalty", "-P",
		                                          &reschedule_penalty_option_set, &cfg.reschedule_penalty)) {
			// Reschedule penalty, aka how much nanoseconds should preemption take on average
		} else if (chronos_parse_time_cmdline_arg(argc, argv, &i, "--barriers_deadline", "-B",
		                                          &barriers_deadline_option_set, &cfg.barriers_deadline)) {
			// Barriers deadline, aka what is the max time code can spend under a barrier
		} else {
			// Test-specific argument
			argv[new_argc++] = argv[i];
		}
	}

	// Instantiate runtime with given options
	void *main_tcb = malloc(chronosrt_tcb_size);
	chronos_assert(main_tcb);
	chronosrt_instantiate(&cfg, main_tcb);
	*argc_out = new_argc;
}

// CPU time waste functions. These are perfect for emulating workload that is meant to take a given period of time //

// Spin for a given number of nanoseconds
void chronos_spin_for_ns(size_t ns) {
	size_t start = chronos_ns_get_clock(CLOCK_THREAD_CPUTIME_ID);
	while (chronos_ns_get_clock(CLOCK_THREAD_CPUTIME_ID) - start < ns) {
		asm volatile("pause");
	}
}

// Spin for a given number of microseconds
void chronos_spin_for_us(size_t us) {
	chronos_spin_for_ns(US_TO_NS(us));
}

// Spin for a given number of milliseconds
void chronos_spin_for_ms(size_t ms) {
	chronos_spin_for_ns(MS_TO_NS(ms));
}

// Advance event counter. This can be used to things that should be happening in test are happening in the correct order
void chronos_check_and_advance_evcnt(size_t ev) {
	static atomic_size_t evcnt = 0;
	size_t evcnt_current = atomic_fetch_add(&evcnt, 1);
	if (evcnt_current != ev) {
		chronos_raw_log("Event %zu out of order (expected %zu)", ev, evcnt_current);
		abort();
	}
}
