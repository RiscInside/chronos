#include <test_helpers.h>

// This is a pretty straightforward test just to make sure that basic functions meant for testing work correctly.

int main() {
	// Assert that event counter is 0 and set it to 1
	chronos_check_and_advance_evcnt(0);
	// Wait for 100 ms
	chronos_spin_for_ms(100);
	// Assert that event counter is 1 and set it to 2
	chronos_check_and_advance_evcnt(1);
	// Wait for another 100 ms
	chronos_spin_for_ms(200);
	// Assert that event counter is 2 and set it to 3
	chronos_check_and_advance_evcnt(2);
}
