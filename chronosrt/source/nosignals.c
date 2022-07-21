#include <fail.h>
#include <nosignals.h>
#include <signal.h>
#include <stddef.h>

void chronosrt_disable_signals() {
	sigset_t mask;
	CHRONOSRT_ASSERT_FALSE(sigfillset(&mask));
	CHRONOSRT_ASSERT_FALSE(sigprocmask(SIG_SETMASK, &mask, NULL));
}
