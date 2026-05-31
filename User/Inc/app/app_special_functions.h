#ifndef APP_APP_SPECIAL_FUNCTIONS_H
#define APP_APP_SPECIAL_FUNCTIONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t AppSpecialFunctions_IsActive(void);
uint8_t AppSpecialFunctions_Toggle(void);
void AppSpecialFunctions_Reset(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_SPECIAL_FUNCTIONS_H */