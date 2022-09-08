#include <log.h>
#include <stdlib.h>

#define CHRONOS_ENV_RT_LOG_SWITCH "CHRONOS_ENABLE_RT_LOGGING"
#define CHRONOS_ENV_TEST_LOG_SWITCH "CHRONOS_ENABLE_TEST_LOGGING"

bool chronos_is_rt_logging_enabled(void) {
	static bool queried_env = false;
	static bool should_log = false;
	if (!queried_env) {
		queried_env = true;
		should_log = getenv(CHRONOS_ENV_RT_LOG_SWITCH) != NULL;
	}
	return should_log;
}

bool chronos_is_test_logging_enabled(void) {
	static bool queried_env = false;
	static bool should_log = false;
	if (!queried_env) {
		queried_env = true;
		should_log = getenv(CHRONOS_ENV_TEST_LOG_SWITCH) != NULL;
	}
	return should_log;
}
