#ifndef DISPLAY_COMPOSE_H
#define DISPLAY_COMPOSE_H

#include <stdint.h>
#include "fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

void DisplayCompose_Clear(uint16_t clip_width,
                          uint16_t clip_height,
                          uint16_t colour);
void DisplayCompose_FillRect(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             uint16_t w,
                             uint16_t h,
                             uint16_t colour);
void DisplayCompose_Char32(uint16_t clip_width,
                           uint16_t clip_height,
                           uint16_t x,
                           uint16_t y,
                           char ch,
                           FontDef32 font,
                           uint16_t colour,
                           uint16_t background);
void DisplayCompose_String32(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             const char *text,
                             FontDef32 font,
                             uint16_t colour,
                             uint16_t background);
void DisplayCompose_String16(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             const char *text,
                             FontDef font,
                             uint16_t colour,
                             uint16_t background);
void DisplayCompose_Blit(uint16_t x,
                         uint16_t y,
                         uint16_t width,
                         uint16_t height);

#ifdef __cplusplus
}
#endif

#endif