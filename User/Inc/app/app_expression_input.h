#ifndef APP_APP_EXPRESSION_INPUT_H
#define APP_APP_EXPRESSION_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tuning knob for analog pedal jitter suppression before residual accumulation.
 * Increase to reduce idle drift; decrease for more sensitivity at very slow moves. */
#ifndef APP_EXPRESSION_INPUT_NOISE_THRESHOLD
#define APP_EXPRESSION_INPUT_NOISE_THRESHOLD 10
#endif

/* Endpoint dead zones in normalized 12-bit units (0..4095) after calibration.
 * Values inside these margins are clamped to 0 or 4095 to suppress rest jitter. */
#ifndef APP_EXPRESSION_INPUT_HEEL_DEAD_ZONE
#define APP_EXPRESSION_INPUT_HEEL_DEAD_ZONE 24
#endif

#ifndef APP_EXPRESSION_INPUT_TOE_DEAD_ZONE
#define APP_EXPRESSION_INPUT_TOE_DEAD_ZONE 24
#endif

void AppExpressionInput_Init(void);
void AppExpressionInput_ProcessPending(void);
void AppExpressionInput_PublishRawSample(uint16_t raw_sample);
uint8_t AppExpressionInput_TryGetLatestRawSample(uint16_t *raw_sample);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_EXPRESSION_INPUT_H */