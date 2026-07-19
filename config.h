#pragma once

/**
 * Repisa Hot Wheels — hardware, tiempos y constantes de compilación.
 * Solo macros / constantes; sin funciones ni variables con enlace.
 */

/* ---------- Firmware, OTA y parámetros RainMaker (nombres en la app) ---------- */
#define REPISA_FW_VERSION "0.2.0"
#define REPISA_FW_JSON_URL "https://raw.githubusercontent.com/WilmarC20/RepisaHotWheels/main/firmware.json"

#define REPISA_RM_PARAM_VERIFICAR "ComprobarGit"
#define REPISA_RM_PARAM_INFO "InfoActualizacion"
#define REPISA_RM_PARAM_VERSION_LOCAL "VersionFirmware"

/**
 * Reloj 6×8 (modo Reloj): 1 = texto fijo "12:34" (calibración glifos); 0 = hora local (getLocalTime tras NTP).
 */
#ifndef REPISA_RELOJ_FIJO_12_34
#define REPISA_RELOJ_FIJO_12_34 0
#endif

/* ---------- Micrófono INMP441 (misma lógica que el sketch de referencia) ---------- */
#ifndef REPISA_MIC_SERIAL_PLOTTER
#define REPISA_MIC_SERIAL_PLOTTER 1
#endif

#ifndef REPISA_MIC_PLOT_ONCE_PER_BUFFER
#define REPISA_MIC_PLOT_ONCE_PER_BUFFER 1
#endif

#ifndef REPISA_MIC_THRESHOLD
#define REPISA_MIC_THRESHOLD 1200
#endif

/**
 * INMP441: crudo 32b I²S → misma escala en tarea de mic, VU, plotter y FFT
 * (evita mezclar (>>8)-8388608 con `>>14`, que desincronizaba span / soundLevel / bins).
 */
#ifndef REPISA_I2S_PCM_FROM_RAW
#define REPISA_I2S_PCM_FROM_RAW(x) ((int32_t)(x) >> 14)
#endif

/**
 * Tras `REPISA_I2S_PCM_FROM_RAW`: |muestra| en [FLOOR, CEILING) pasa el antiglitch
 * (misma unidad que el monitor `span` y el bloque FFT).
 */
#ifndef REPISA_MIC_SIGNAL_FLOOR
#define REPISA_MIC_SIGNAL_FLOOR 0
#endif
#ifndef REPISA_MIC_SIGNAL_CEILING
#define REPISA_MIC_SIGNAL_CEILING 131072
#endif

/**
 * Efectos espectro (I²S): `span` = max−min del bloque de 64 en unidades PCM14;
 * el umbral baja mucho al unificar el decode. `soundLevel` = media de |PCM14| por buffer I²S.
 */
#ifndef REPISA_MIC_BLOCK_SPAN_FLOOR
#define REPISA_MIC_BLOCK_SPAN_FLOOR 1000.0f
#endif
#ifndef REPISA_MIC_BLOCK_SPAN_SCALE
#define REPISA_MIC_BLOCK_SPAN_SCALE 4.0f
#endif
#ifndef REPISA_MIC_SOUNDLEVEL_MIN
#define REPISA_MIC_SOUNDLEVEL_MIN 2
#endif

/**
 * Cuánto suma la SensibilidadMic (RainMaker) a los umbrales I²S cuando bajas el slider:
 * menos sensibilidad → más margen (menos parpadeo en silencio).
 */
#ifndef REPISA_MIC_SPAN_EXTRA_FROM_SENS
#define REPISA_MIC_SPAN_EXTRA_FROM_SENS 88.0f
#endif
#ifndef REPISA_MIC_LEVEL_EXTRA_FROM_SENS
#define REPISA_MIC_LEVEL_EXTRA_FROM_SENS 175
#endif

/**
 * No usar solo `soundLevel` para abrir el FFT (I²S): el offset DC sube la media de |PCM|
 * con span casi nulo → barras bajas por fuga en bins 1–2. Se exige también span_acústico.
 */
#ifndef REPISA_MIC_SPAN_FOR_LEVEL_GATE_MIN
#define REPISA_MIC_SPAN_FOR_LEVEL_GATE_MIN 750.0f
#endif
#ifndef REPISA_MIC_SPAN_FOR_LEVEL_GATE_EXTRA
#define REPISA_MIC_SPAN_FOR_LEVEL_GATE_EXTRA 36.0f
#endif
/**
 * Junto con nivel: span mínimo = max(fracción de `span_gate`, fracción del piso REPISA_MIC_BLOCK_SPAN_FLOOR).
 * Con audio real trace 4 del plotter suele ser >1000; en silencio suele quedar muy por debajo — no usar solo 0.15×gate.
 */
#ifndef REPISA_MIC_SPAN_FOR_LEVEL_FRAC_GATE
#define REPISA_MIC_SPAN_FOR_LEVEL_FRAC_GATE 0.44f
#endif
#ifndef REPISA_MIC_SPAN_FOR_LEVEL_FRAC_BLOCK_FLOOR
#define REPISA_MIC_SPAN_FOR_LEVEL_FRAC_BLOCK_FLOOR 0.38f
#endif

/** Tras magnitud FFT: atenua bins 1–2 (fuga DC/Hann); 1 = sin cambio. */
#ifndef REPISA_MIC_I2S_LOWBIN_ATTENUATION
#define REPISA_MIC_I2S_LOWBIN_ATTENUATION 0.42f
#endif

/** Entrada al FFT: (muestra−media)/SCALE. Bajar = barras más altas (misma escala PCM14). */
#ifndef REPISA_MIC_INV_FFT_SCALE_I2S
#define REPISA_MIC_INV_FFT_SCALE_I2S 380.0f
#endif

/**
 * AudioFire / SonidoColor (VU por pico global): en I²S el envolvente AGC sigue tan de cerca a `mmax_out`
 * que el ratio queda ~1 y todo se ve “al máximo”. >1 estira el denominador (más headroom dinámico).
 */
#ifndef REPISA_MIC_GLOBALVU_NORM_STRETCH_I2S
#define REPISA_MIC_GLOBALVU_NORM_STRETCH_I2S 1.52f
#endif

/** Impacto: deadband en unidades `soundLevel` I²S es proporcionalmente pequeño sin este factor. */
#ifndef REPISA_MIC_IMPACT_DEAD_MULT_I2S
#define REPISA_MIC_IMPACT_DEAD_MULT_I2S 7.0f
#endif
#ifndef REPISA_MIC_IMPACT_ENV_GAIN_I2S
#define REPISA_MIC_IMPACT_ENV_GAIN_I2S 0.58f
#endif

/* ---------- Provisión RainMaker (solo texto; el modo BLE/SoftAP se define en el .ino por dependencias SDK) ----------
 * Credenciales en secrets.h (fuera de git). Primera vez: copia secrets.example.h como secrets.h. */
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Falta secrets.h: copia secrets.example.h como secrets.h y define tus credenciales de provision"
#endif

/* ---------- Cinta LED WS2812 (data + número de LEDs; orden NEO_* va en el constructor en el .ino) ---------- */
#define REPISA_LED_DATA_PIN 4
#define REPISA_LED_COUNT 48

/** Cambio automático de modo en la cinta (ms); antes TIMER_MS en CintaLED.h */
#define REPISA_CINTA_AUTO_MODE_MS 5000U

/* ---------- Entradas digitales / IR ---------- */
#define REPISA_IR_RECEIVE_PIN 14
#define REPISA_BOOT_BUTTON_PIN 0

/**
 * ADC para micrófono analógico (p. ej. MAX9814). Solo ESP32.
 */
#if defined(ARDUINO_ARCH_ESP32)
#if CONFIG_IDF_TARGET_ESP32C6
#define REPISA_SOUND_SENSOR_AO_PIN 3
#elif CONFIG_IDF_TARGET_ESP32S3
#define REPISA_SOUND_SENSOR_AO_PIN 8
#else
#define REPISA_SOUND_SENSOR_AO_PIN 34
#endif
#endif

/* ---------- INMP441 I²S (ESP32) ---------- */
#if defined(ARDUINO_ARCH_ESP32)
#if CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32S3
#define REPISA_I2S_PIN_WS 2//naranja
#define REPISA_I2S_PIN_SD 6//verde
#define REPISA_I2S_PIN_SCK 7//azul
#else
/* En ESP32 clasico los GPIO 6-11 van a la flash SPI interna: usarlos brickea el arranque. */
#error "Define pines I2S seguros para este chip (evita GPIO 6-11 en ESP32 clasico)"
#endif
#endif

/* ---------- Tiempos y límites ---------- */
/** Debounce de guardado en NVS: espera este tiempo sin cambios antes de escribir flash
 * (evita una escritura por cada paso al arrastrar un slider en la app). */
#define REPISA_NVS_SAVE_DEBOUNCE_MS 2000U

/** Mantener pulsado BOOT para reset por firmware (si se usa en otro flujo). */
#define REPISA_RESET_HOLD_MS 5000U

/**
 * Reinicio de fábrica RainMaker sin PC: ciclos rápidos de alimentación,
 * y tiempo máximo encendido para cancelar el contador.
 */
#define REPISA_FACTORY_RESET_POWER_CYCLES 6U
#define REPISA_FACTORY_CANCEL_STABLE_MS 45000UL

/** Saturación HSV fija en presets de escena (0–255). */
#define REPISA_COLOR_SATURATION_BYTE 255
