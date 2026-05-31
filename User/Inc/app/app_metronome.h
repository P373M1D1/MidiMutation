#ifndef APP_APP_METRONOME_H
#define APP_APP_METRONOME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_METRONOME_SOURCE_INTERNAL = 0,
    APP_METRONOME_SOURCE_EXTERNAL,
} AppMetronomeSource_t;

typedef enum {
    APP_METRONOME_OUTPUT_NONE = 0,
    APP_METRONOME_OUTPUT_DAC_CLICK,
    APP_METRONOME_OUTPUT_PWM_CLICK,
    APP_METRONOME_OUTPUT_GPIO_PULSE,
} AppMetronomeOutput_t;

typedef struct {
    uint8_t enabled;
    uint8_t volume;
    uint8_t beat_in_bar;
    uint8_t beats_per_bar;
    uint8_t pending_click_count;
    uint8_t next_click_accent;
    uint8_t rhythm;
    uint8_t output_available;
    uint16_t normal_pitch_hz;
    uint16_t accent_pitch_hz;
    uint16_t last_click_pitch_hz;
    uint32_t quarter_interval_us;
    AppMetronomeSource_t last_source;
    AppMetronomeOutput_t output;
} AppMetronomeState_t;

void AppMetronome_Init(void);
void AppMetronome_Service(void);
void AppMetronome_DiagnosticService(void);
void AppMetronome_HandleTimingCounterIrq(void);
void AppMetronome_FlagTimingCounterIrq(void);
void AppMetronome_ServiceDeferredTimingWork(void);
void AppMetronome_ResetCycle(void);
void AppMetronome_OnQuarterNote(AppMetronomeSource_t source);
void AppMetronome_OnQuarterNoteAt(AppMetronomeSource_t source, uint32_t anchor_us);
void AppMetronome_OnQuarterNoteAtCount(AppMetronomeSource_t source,
                                       uint32_t anchor_us,
                                       uint32_t quarter_note_count);
void AppMetronome_SetEnabled(uint8_t enabled);
uint8_t AppMetronome_IsEnabled(void);
uint8_t AppMetronome_IsOutputActive(void);
void AppMetronome_SetOutput(AppMetronomeOutput_t output);
AppMetronomeOutput_t AppMetronome_GetOutput(void);
uint8_t AppMetronome_IsOutputAvailable(AppMetronomeOutput_t output);
void AppMetronome_GetState(AppMetronomeState_t *state);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_METRONOME_H */