#ifndef APP_APP_ENCODER_SAMPLER_H
#define APP_APP_ENCODER_SAMPLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_ENCODER_SAMPLER_ENCODER1 = 0U,
    APP_ENCODER_SAMPLER_ENCODER2 = 1U,
    APP_ENCODER_SAMPLER_ENCODER3 = 2U,
} AppEncoderSamplerId_t;

void AppEncoderSampler_Init(void);
void AppEncoderSampler_SampleInterrupt(void);
void AppEncoderSampler_TakePendingMotion(AppEncoderSamplerId_t encoder_id, int8_t *pending_delta);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_ENCODER_SAMPLER_H */
