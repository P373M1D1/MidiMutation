#ifndef APP_APP_RUNTIME_H
#define APP_APP_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	uint32_t dispatch_samples;
	uint32_t dispatch_over_1000us;
	uint32_t dispatch_over_5000us;
	uint32_t dispatch_max_us;
} AppRuntimePressureCounters_t;

/* Runs one foreground pass that pumps the queue and services low-priority work. */
void AppRuntime_ServiceForeground(void);
/* Emits runtime queue and pump diagnostics. */
void AppRuntime_DiagnosticService(void);
/* Returns cumulative dispatch-pressure counters for adaptive diagnostics control. */
void AppRuntime_GetPressureCounters(AppRuntimePressureCounters_t *counters);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_RUNTIME_H */