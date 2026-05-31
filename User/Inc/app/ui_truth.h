#ifndef APP_UI_TRUTH_H
#define APP_UI_TRUTH_H

#ifdef __cplusplus
extern "C" {
#endif

/* UI truth contract:
 * - UI is read-only with respect to timing truth.
 * - UI must consume clock/sync state via ClockEngine APIs.
 * - UI must never mutate transport phase, lock state, or sync lifecycle.
 */

#define UI_TRUTH_READ_ONLY 1U

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_TRUTH_H */
