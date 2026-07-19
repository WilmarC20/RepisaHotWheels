#pragma once

#include "config.h"
#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)

/**
 * No es un GPIO: mic INMP441 por I²S legacy (`driver/i2s.h`).
 * Captura solo en la tarea `inmp441_i2s`.
 * Umbrales / plot: `REPISA_MIC_*` en config.h.
 */
#define REPISA_MIC_PIN_I2S (-2)

/** Media de |PCM| por buffer I²S (misma `REPISA_I2S_PCM_FROM_RAW` que FFT). Escribe la tarea del mic. */
extern volatile int soundLevel;
/** Rango max−min en las primeras 64 muestras PCM14 (misma ventana que FFT) — Serial Plotter. */
extern volatile int soundSpan;

extern int32_t sBuffer[256];

void repisaMicInitInmp441(int pinDin, int pinBclk, int pinWs);
void repisaMicDeinit();

bool repisaMicIsReady();

void repisaMicSetThreshold(int threshold);
int repisaMicGetThreshold();

/** Copia snapshot crudo I²S (mutex try); para FFT — sin conversión 12-bit. */
bool repisaMicReadBlockInt32(int32_t *dst, int count);

void repisaMicSerialPlotterTick();

#else

#define REPISA_MIC_PIN_I2S (-2)
static inline void repisaMicInitInmp441(int, int, int) {}
static inline void repisaMicDeinit() {}
static inline bool repisaMicIsReady() {
   return false;
}
static inline void repisaMicSetThreshold(int) {}
static inline int repisaMicGetThreshold() {
   return REPISA_MIC_THRESHOLD;
}
static inline bool repisaMicReadBlockInt32(int32_t *, int) {
   return false;
}
static inline void repisaMicSerialPlotterTick() {}

#endif
