#ifndef APP_APP_BOARD_INIT_H
#define APP_APP_BOARD_INIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppBoard_InitStartupPeripherals(void);
uint8_t AppBoard_MetronomePwmIsAvailable(void);
uint8_t AppBoard_MetronomePwmStart(uint16_t frequency_hz, uint8_t volume);
void AppBoard_MetronomePwmStop(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_BOARD_INIT_H */