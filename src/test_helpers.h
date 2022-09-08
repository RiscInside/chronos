#ifndef __CHRONOS_TEST_HELPERS_H__
#define __CHRONOS_TEST_HELPERS_H__

#include <stddef.h>

void chronos_test_init_runtime(int *argc, char **argv);
void chronos_spin_for_ns(size_t ns);
void chronos_spin_for_us(size_t us);
void chronos_spin_for_ms(size_t ms);
void chronos_check_and_advance_evcnt(size_t ev);

#endif
