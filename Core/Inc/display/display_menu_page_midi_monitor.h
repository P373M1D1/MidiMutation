#ifndef DISPLAY_MENU_PAGE_MIDI_MONITOR_H
#define DISPLAY_MENU_PAGE_MIDI_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Display_DrawMenuMidiMonitor(void);
void Display_MenuMidiMonitorEnter(void);
void Display_MenuMidiMonitorTogglePause(void);
void Display_MenuMidiMonitorClear(void);
void Display_MenuMidiMonitorScroll(int8_t delta);
void Display_MenuMidiMonitorService(void);
uint8_t Display_MenuMidiMonitorIsActive(void);
uint8_t Display_MenuMidiMonitorIsPaused(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_MIDI_MONITOR_H */