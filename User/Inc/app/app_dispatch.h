#ifndef APP_APP_DISPATCH_H
#define APP_APP_DISPATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppDispatch_ProcessPendingEvents(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_DISPATCH_H */