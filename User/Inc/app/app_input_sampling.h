#ifndef APP_APP_INPUT_SAMPLING_H
#define APP_APP_INPUT_SAMPLING_H

#ifdef __cplusplus
extern "C" {
#endif

void AppInputSampling_Init(void);
void AppInputSampling_HandleTimerIrq(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_INPUT_SAMPLING_H */