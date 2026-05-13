#ifndef DISPLAY_COMPOSE_HELPERS_H
#define DISPLAY_COMPOSE_HELPERS_H

#include <stdint.h>

#include "fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

void Display_ComposeClear(uint16_t clip_width,
                          uint16_t clip_height,
                          uint16_t colour);
void Display_ComposeFillRect(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             uint16_t w,
                             uint16_t h,
                             uint16_t colour);
void Display_ComposeChar32(uint16_t clip_width,
                           uint16_t clip_height,
                           uint16_t x,
                           uint16_t y,
                           char ch,
                           FontDef32 font,
                           uint16_t colour,
                           uint16_t background);
void Display_ComposeString16(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             const char *text,
                             FontDef font,
                             uint16_t colour,
                             uint16_t background);
void Display_ComposeString32(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             const char *text,
                             FontDef32 font,
                             uint16_t colour,
                             uint16_t background);
void Display_ComposeString32Literal(uint16_t clip_width,
                                    uint16_t clip_height,
                                    uint16_t x,
                                    uint16_t y,
                                    const char *text,
                                    FontDef32 font,
                                    uint16_t colour,
                                    uint16_t background);
void Display_ComposeBlit(uint16_t x,
                         uint16_t y,
                         uint16_t width,
                         uint16_t height);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_COMPOSE_HELPERS_H */