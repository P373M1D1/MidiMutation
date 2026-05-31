#include "app/app_encoder_math.h"

int8_t AppEncoderMath_TransitionDelta(uint8_t previous_state, uint8_t current_state)
{
    static const int8_t transition_delta[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };

    return transition_delta[(previous_state << 2U) | current_state];
}

int8_t AppEncoderMath_AccumulateTransition(int8_t transition_accum, int8_t transition_delta)
{
    if (transition_delta == 0)
        return transition_accum;

    if (((transition_accum > 0) && (transition_delta < 0))
     || ((transition_accum < 0) && (transition_delta > 0)))
    {
        return transition_delta;
    }

    return (int8_t)(transition_accum + transition_delta);
}