#ifndef APP_APP_ENCODER_MATH_H
#define APP_APP_ENCODER_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int8_t AppEncoderMath_TransitionDelta(uint8_t previous_state, uint8_t current_state);
int8_t AppEncoderMath_AccumulateTransition(int8_t transition_accum, int8_t transition_delta);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_ENCODER_MATH_H */