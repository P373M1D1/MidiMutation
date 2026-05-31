#ifndef APP_APP_DISPATCH_H
#define APP_APP_DISPATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	uint32_t calls;
	uint32_t budget_hits;
	uint8_t max_events_per_call;
} AppDispatchDiagnostics_t;

/**
 * Worst-case execution time (µs) observed per handler bucket in the dispatch
 * loop. All fields are lifetime maxes and are never reset, so they accumulate
 * the worst event ever seen for each handler class.
 */
typedef struct
{
	uint32_t timer_max_us;      /* AppTimerEvents_HandleEvent   */
	uint32_t tempo_max_us;      /* AppTempo_HandleEvent         */
	uint32_t ui_max_us;         /* AppUiEvents_HandleEvent      */
	uint32_t button_max_us;     /* AppButtonEvents_HandleEvent  */
	uint32_t activation_max_us; /* AppActivation_HandleEvent    */
	uint32_t save_max_us;       /* AppSaveService_HandleEvent   */
} AppDispatchHandlerTimings_t;

/* Drains queued events until the queue is empty; returns the number of events handled. */
uint8_t AppDispatch_ProcessPendingEvents(void);
void AppDispatch_GetDiagnostics(AppDispatchDiagnostics_t *diagnostics);
/* Returns lifetime worst-case per-handler dispatch timings. */
void AppDispatch_GetHandlerTimings(AppDispatchHandlerTimings_t *timings);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_DISPATCH_H */