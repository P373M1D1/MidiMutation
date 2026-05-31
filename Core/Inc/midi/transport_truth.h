#ifndef TRANSPORT_TRUTH_H
#define TRANSPORT_TRUTH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Transport truth contract:
 * - Sync lifecycle and transport phase mutation are internal-only concerns.
 * - Only MIDI core timing modules may include midi_transport_internal.h.
 * - Cross-domain modules must consume clock state through ClockEngine.
 */

#define TRANSPORT_TRUTH_INTERNAL_ONLY 1U

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_TRUTH_H */
