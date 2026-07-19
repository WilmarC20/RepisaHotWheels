#include "CustomEffects.h"
#include "Matrix6x8.h"
#include "config.h"
#include <arduinoFFT.h>
#include <cmath>
#include <string.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_random.h>
#include <time.h>
#include "RepisaMic.h"
#define REPISA_MIC_INPUT_ACTIVE(pp) ((pp) >= 0 || (pp) == REPISA_MIC_PIN_I2S)
#else
#define REPISA_MIC_INPUT_ACTIVE(pp) ((pp) >= 0)
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Bases (más bajo = más sensible); escalado por setMicSensitivityPct(). */
static constexpr int kMicDeadbandBase = 20;
static constexpr int kMicSilenceSpanBase = 38;

WS2812FX* CustomEffects::_fx = nullptr;
int CustomEffects::mic_pin = -1;
uint8_t CustomEffects::mic_sens_pct = 62;

uint8_t CustomEffects::modeJump3;
uint8_t CustomEffects::modeJump7;
uint8_t CustomEffects::modeFade3;
uint8_t CustomEffects::modeFade7;
uint8_t CustomEffects::modeSpectrum;
uint8_t CustomEffects::modeSoundBright;
uint8_t CustomEffects::modeSoundHue;
uint8_t CustomEffects::modeImpact;
uint8_t CustomEffects::modeHotWheels;

// ================= INIT =================

void CustomEffects::begin(WS2812FX* fx) {
    _fx = fx;

    /* Mismos nombres que antes en CintaLED para la lista / RainMaker. */
    modeJump7 = _fx->setCustomMode(F("Reloj"), jump7);
    modeFade3 = _fx->setCustomMode(F("HotWheelsNitro"), hotWheelsNitro);
    modeFade7 = _fx->setCustomMode(F("TurboBoost"), turboBoost);
    modeSpectrum = _fx->setCustomMode(F("Spectrum6x8"), spectrumVertical);
    modeSoundBright = _fx->setCustomMode(F("AudioFire"), audioFire);
    modeSoundHue = _fx->setCustomMode(F("SonidoColor"), vuMeterFull);
    modeHotWheels = _fx->setCustomMode(F("HotWheels"), hotWheelsMarquee);
    modeImpact = _fx->setCustomMode(F("Impacto"), impactShow);
    modeJump3 = _fx->setCustomMode(F("Jump3"), jump3);
    
}

void CustomEffects::setMicPin(int pin) {
    mic_pin = pin;
}

void CustomEffects::setMicSensitivityPct(uint8_t pct0_100) {
    if (pct0_100 > 100u) {
        pct0_100 = 100u;
    }
    mic_sens_pct = pct0_100;
}

uint8_t CustomEffects::getMicSensitivityPct() {
    return mic_sens_pct;
}

int CustomEffects::micEffectiveDeadband() {
    const float scale = 0.35f + (mic_sens_pct / 100.f) * 1.9f;
    return (int)constrain((float)kMicDeadbandBase / scale, 8.f, 90.f);
}

int CustomEffects::micEffectiveSilenceSpan() {
    const float scale = 0.35f + (mic_sens_pct / 100.f) * 1.9f;
    return (int)constrain((float)kMicSilenceSpanBase / scale, 18.f, 180.f);
}

float CustomEffects::micBrightnessEnvGain() {
    return 1.38f + (mic_sens_pct / 100.f) * 2.15f;
}

/** 0..1 desde SensibilidadMic (AudioFire / VU SonidoColor / Impacto). */
static inline float micSensNorm() {
    return (float)CustomEffects::getMicSensitivityPct() / 100.f;
}

#if defined(ARDUINO_ARCH_ESP32)
/** Solo I²S: evita ratio mpeak/env ~ 1 en AudioFire y SonidoColor (Spectrum usa ratios por banda). */
static inline float micGlobalVuNormStretch(int mp) {
    return (mp == REPISA_MIC_PIN_I2S) ? REPISA_MIC_GLOBALVU_NORM_STRETCH_I2S : 1.f;
}
#else
static inline float micGlobalVuNormStretch(int) {
    return 1.f;
}
#endif

/** Amplifica |muestra−DC| tras deadband: sube mucho con sensibilidad alta. */
static inline float micEnvDriveScale() {
    return 0.88f + micSensNorm() * 0.85f;
}

/** Coeficiente de retención del envolvente (1−ataque): más bajo = más reacción al instante. */
static inline float micEnvSmoothKeep() {
    return 0.64f - micSensNorm() * 0.30f;
}

static inline uint8_t af_qsub8(uint8_t a, uint8_t b) {
    return (b >= a) ? 0 : (uint8_t)(a - b);
}

static inline uint8_t af_qadd8(uint8_t a, uint8_t b) {
    const uint16_t s = (uint16_t)a + (uint16_t)b;
    return (s > 255u) ? 255u : (uint8_t)s;
}

/** Paleta tipo FastLED HeatColor: negro→rojo→naranja/amarillo→blanco (chispas). */
static uint32_t af_heatColor(WS2812FX* fx, uint8_t heat) {
    if (heat < 64) {
        return fx->Color((uint8_t)(heat << 2), 0, 0);
    }
    heat -= 64;
    if (heat < 64) {
        return fx->Color(255, (uint8_t)(heat << 2), 0);
    }
    heat -= 64;
    if (heat < 64) {
        return fx->Color(255, 255, (uint8_t)(heat << 2));
    }
    return fx->Color(255, 255, 255);
}

static inline uint8_t af_rand8() {
#if defined(ARDUINO_ARCH_ESP32)
    return (uint8_t)(esp_random() & 0xFFu);
#else
    return (uint8_t)random(0, 256);
#endif
}

// ================= GETTERS =================

uint8_t CustomEffects::getJump3() { return modeJump3; }
uint8_t CustomEffects::getJump7() { return modeJump7; }
uint8_t CustomEffects::getFade3() { return modeFade3; }
uint8_t CustomEffects::getFade7() { return modeFade7; }
uint8_t CustomEffects::getSpectrum6x8() { return modeSpectrum; }
uint8_t CustomEffects::getSoundBright6x8() { return modeSoundBright; }
uint8_t CustomEffects::getSoundHue6x8() { return modeSoundHue; }
uint8_t CustomEffects::getImpact6x8() { return modeImpact; }
uint8_t CustomEffects::getHotWheels6x8() { return modeHotWheels; }

static uint8_t glyph4x8(char c, uint8_t col) {
    if (col >= 4) {
        return 0;
    }
    switch (c) {
        /* 4x8: bit 0 = fila superior del glifo, bit 7 = inferior. */
        case 'A': { static const uint8_t g[4] = {0xFE, 0x09, 0x09, 0xFE}; return g[col]; }
        case 'D': { static const uint8_t g[4] = {0xFF, 0x81, 0x81, 0x7E}; return g[col]; }
        case 'E': { static const uint8_t g[4] = {0xFF, 0x99, 0x99, 0x81}; return g[col]; }
        case 'G': { static const uint8_t g[4] = {0x7E, 0x81, 0x91, 0x71}; return g[col]; }
        case 'H': { static const uint8_t g[4] = {0xFF, 0x18, 0x18, 0xFF}; return g[col]; }
        case 'I': { static const uint8_t g[4] = {0x81, 0x81, 0xFF, 0x81}; return g[col]; }
        case 'J': { static const uint8_t g[4] = {0x40, 0x80, 0x80, 0x7F}; return g[col]; }
        case 'L': { static const uint8_t g[4] = {0xFF, 0x80, 0x80, 0x80}; return g[col]; }
        case 'N': { static const uint8_t g[4] = {0xFF, 0x06, 0x18, 0xFF}; return g[col]; }
        case 'O': { static const uint8_t g[4] = {0x7E, 0x81, 0x81, 0x7E}; return g[col]; }
        case 'R': { static const uint8_t g[4] = {0xFF, 0x09, 0x19, 0xE6}; return g[col]; }
        case 'S': { static const uint8_t g[4] = {0x86, 0x99, 0x99, 0x61}; return g[col]; }
        case 'T': { static const uint8_t g[4] = {0x01, 0x01, 0xFF, 0x01}; return g[col]; }
        case 'W': { static const uint8_t g[4] = {0xFF, 0xC0, 0x80, 0xFF}; return g[col]; }
        default: return 0;
    }
}

static uint8_t glyphClockFlipRows(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

/*
 * =========================================================
 * ASCII -> columnas binarias 4x8
 * =========================================================
 */

static bool glyphPixelOn(char c) {
    return (c == '#' || c == 'X' || c == '*');
}

static void buildGlyph4x8(
    const char* rows[8],
    uint8_t outCols[4]
) {

    /* Cada rows[i] debe ser exactamente 4 caracteres ASCII (p. ej. '#' no UTF-8 multibyte). */

    for (uint8_t x = 0; x < 4; x++) {
        outCols[x] = 0;
    }

    for (uint8_t row = 0; row < 8; row++) {

        /* row 0 = línea superior del arte ASCII */
        uint8_t fy = (uint8_t)(7 - row);

        for (uint8_t col = 0; col < 4; col++) {

            char ch = rows[row][col];

            if (glyphPixelOn(ch)) {
                outCols[col] |= (1u << fy);
            }
        }
    }
}

/*
 * Convierte automáticamente a formato legacy
 * (bit0 arriba -> necesita flip)
 */
static void buildGlyph4x8Legacy(
    const char* rows[8],
    uint8_t outCols[4]
) {

    buildGlyph4x8(rows, outCols);

    for (uint8_t i = 0; i < 4; i++) {
        outCols[i] = glyphClockFlipRows(outCols[i]);
    }
}

/*
 * buildGlyph4x8 = bits con y=0 abajo en columna.
 * Legacy aplica flip: en jump7 usar índice (7-y) salvo '4' y '5' (directos).
 */
static inline uint8_t glyphClockBitForMatrixY(char ch, uint8_t matrixY) {
    if (ch == '4' || ch == '5') {
        return matrixY;
    }
    return (uint8_t)(7u - matrixY);
}

/*
 * =========================================================
 * GLYPHS RELOJ
 * =========================================================
 */

static uint8_t glyphClock4x8(char c, uint8_t col) {

    if (col >= 4) {
        return 0;
    }

    uint8_t g[4];

    switch (c) {

        case '0':
        {
            const char* rows[8] = {
                " ## ",
                "#  #",
                "#  #",
                "#  #",
                "#  #",
                "#  #",
                "#  #",
                " ## "
            };

            buildGlyph4x8Legacy(rows, g);
            return g[col];
        }

        case '1':
        {
            const char* rows[8] = {
                "  # ",
                " ## ",
                "  # ",
                "  # ",
                "  # ",
                "  # ",
                "  # ",
                " ###"
            };

            buildGlyph4x8Legacy(rows, g);
            return g[col];
        }

        case '2':
        {
            const char* rows[8] = {
                " ## ",
                "#  #",
                "   #",
                "  # ",
                " #  ",
                "#   ",
                "#   ",
                "####"
            };

            buildGlyph4x8Legacy(rows, g);
            return g[col];
        }

        case '3':
        {
            const char* rows[8] = {
                " ## ",
                "#  #",
                "   #",
                " ## ",
                "   #",
                "   #",
                "#  #",
                " ## "
            };

            buildGlyph4x8Legacy(rows, g);
            return g[col];
        }

        case '4':
        {
            const char* rows[8] = {
                "#  #",
                "#  #",
                "#  #",
                "####",
                "   #",
                "   #",
                "   #",
                "   #"
            };

            buildGlyph4x8(rows, g);
            return g[col];
        }

        case '5':
        {
            const char* rows[8] = {
                "####",
                "#   ",
                "#   ",
                "####",
                "   #",
                "   #",
                "   #",
                "####"
            };

            buildGlyph4x8(rows, g);
            return g[col];
        }

        case '6':
        {
            const char* rows[8] = {
                "####",
                "#   ",
                "#   ",
                "#   ",
                "####",
                "#  #",
                "#  #",
                "####"
            };

            buildGlyph4x8Legacy(rows, g);
            return g[col];
        }

        case '7':
        {
            const char* rows[8] = {
                "####",
                "   #",
                "   #",
                "  # ",
                "  # ",
                " #  ",
                " #  ",
                " #  "
            };

            buildGlyph4x8Legacy(rows, g);
            return g[col];
        }

        case '8':
        {
            const char* rows[8] = {
                " ## ",
                "#  #",
                "#  #",
                " ## ",
                "#  #",
                "#  #",
                "#  #",
                " ## "
            };

            buildGlyph4x8Legacy(rows, g);
            return g[col];
        }

        case '9':
        {
            const char* rows[8] = {
                " ## ",
                "#  #",
                "#  #",
                " ###",
                "   #",
                "   #",
                "#  #",
                " ## "
            };

            buildGlyph4x8Legacy(rows, g);
            return g[col];
        }

        case ':':
        {
            const char* rows[8] = {
                "    ",
                " ## ",
                " ## ",
                "    ",
                "    ",
                " ## ",
                " ## ",
                "    "
            };

            buildGlyph4x8Legacy(rows, g);
            return g[col];
        }

        case '-':
        {
            const char* rows[8] = {
                "    ",
                "    ",
                "    ",
                "####",
                "####",
                "    ",
                "    ",
                "    "
            };

            buildGlyph4x8Legacy(rows, g);
            return g[col];
        }

        default:
            return 0;
    }
}
uint16_t CustomEffects::hotWheelsMarquee() {
    if (!_fx) {
        return 50;
    }
    const uint16_t stripLen = _fx->getLength();
    static constexpr char kMsg[] = " SERGIO ALEJANDRO HOTWHEELS";
    static constexpr int kChars = (int)sizeof(kMsg) - 1;
    static constexpr int kCharStep = 6; // 4 columnas + 3 espacios para separar letras
    static constexpr int kTotalCols = kChars * kCharStep + Matrix6x8::W;
    static int scrollCol = 0;

    static constexpr uint16_t kWordHue[4] = {
        (uint16_t)(65535UL * 208UL / 360UL),  // SERGIO: azul
        (uint16_t)(65535UL * 132UL / 360UL),  // ALEJANDRO: verde
        //(uint16_t)(65535UL * 26UL / 360UL),   // HOTWHEELS: naranja
        (uint16_t)(65535UL * 0UL / 360UL),   // rojo
        (uint16_t)(65535UL * 60UL / 360UL),  // amarillo
    };

    auto wordIndexFromChar = [&](int ci) -> int {
        if (ci < 0 || ci >= kChars) return -1;
        if (kMsg[ci] == ' ') return -1;
        if (ci <= 7) return 0;     // SERGIO
        if (ci <= 16) return 1;    // ALEJANDRO
        if (ci <= 20) return 2;    // HOT
        return 3;                  // WHEELS
    };

    for (uint8_t x = 0; x < Matrix6x8::W; x++) {
        const int worldCol = scrollCol + x;
        const int charIdx = worldCol / kCharStep;
        const int inChar = worldCol % kCharStep;
        uint8_t bits = 0;
        int wordIdx = -1;
        if (charIdx >= 0 && charIdx < kChars && inChar < 4) {
            bits = glyph4x8(kMsg[charIdx], (uint8_t)inChar);
            wordIdx = wordIndexFromChar(charIdx);
        }
        for (uint8_t y = 0; y < Matrix6x8::H; y++) {
            const uint16_t idx = Matrix6x8::xyToIndex(x, y);
            uint32_t colr = 0;
            /* Glifo 4x8 directo: bit 0 arriba, bit 7 abajo; sin estiramiento (no duplica filas). */
            const uint8_t fy = (uint8_t)(7 - y);
            if (wordIdx >= 0 && (bits & (1u << fy))) {
                const uint16_t baseHue = kWordHue[wordIdx];
                const uint16_t edgeHue = (uint16_t)(baseHue + (uint16_t)(65535UL * 18UL / 360UL));
                const uint16_t hue = (fy >= 8) ? edgeHue : baseHue;
                const uint8_t bri = (fy >= 8) ? 255 : (uint8_t)(210 + fy * 4);
                colr = _fx->ColorHSV(hue, 255, bri);
            }
            if (idx < stripLen) {
                _fx->setPixelColor(idx, colr);
            } 
        }
    }
    for (uint16_t i = Matrix6x8::NUM; i < stripLen; i++) {
        _fx->setPixelColor(i, 0);
    }

    static uint8_t stepDiv = 0;
    stepDiv++;
    if (stepDiv >= 2) {
        stepDiv = 0;
        scrollCol++;
        if (scrollCol >= kTotalCols) {
            scrollCol = 0;
        }
    }
    return 130;
}

// ================= EFECTOS =================

// 🔴 JUMP3
uint16_t CustomEffects::jump3() {
    if (!_fx) {
        return 50;
    }
    WS2812FX::Segment* seg = _fx->getSegment();
    if (!seg) {
        return 50;
    }
    static uint8_t step = 0;

    uint32_t colores[] = {
        0xFF0000,
        0x00FF00,
        0x0000FF
    };

    for (uint16_t i = seg->start; i <= seg->stop; i++) {
        _fx->setPixelColor(i, colores[step]);
    }

    step = (step + 1) % 3;

    return seg->speed;
}

// 🕐 Reloj HH:MM — scroll; glifos solo en glyphClock4x8 (mismo criterio que letras HotWheels); azul; ciclo corto
uint16_t CustomEffects::jump7() {
    if (!_fx) {
        return 50;
    }
    WS2812FX::Segment* seg = _fx->getSegment();
    if (!seg) {
        return 50;
    }
    const uint16_t stripLen = _fx->getLength();
    const uint16_t pin0 = (uint16_t)seg->start;

    static char kMsg[16] = " --:--";
#if !REPISA_RELOJ_FIJO_12_34
    static unsigned long lastRefresh = 0;
#endif

#if REPISA_RELOJ_FIJO_12_34
    snprintf(kMsg, sizeof(kMsg), "%02d:%02d", 12, 34);
#else
    if (lastRefresh == 0 || millis() - lastRefresh >= 500) {
        lastRefresh = millis();
#if defined(ARDUINO_ARCH_ESP32)
        struct tm ti;
        if (getLocalTime(&ti)) {
            snprintf(kMsg, sizeof(kMsg), " %02d:%02d", ti.tm_hour, ti.tm_min);
        } else {
            snprintf(kMsg, sizeof(kMsg), " --:--");
        }
#else
        snprintf(kMsg, sizeof(kMsg), " --:--");
#endif
    }
#endif

    static constexpr int kCharStep = 6;
    static int scrollCol = 0;
    static uint8_t stepDiv = 0;

    const int kChars = (int)strlen(kMsg);
    const int kTotalCols = kChars * kCharStep + 3;

    const uint32_t fg = _fx->Color(55, 160, 255);

    for (uint8_t x = 0; x < Matrix6x8::W; x++) {
        const int worldCol = scrollCol + x;
        const int charIdx = worldCol / kCharStep;
        const int inChar = worldCol % kCharStep;
        uint8_t bits = 0;
        if (charIdx >= 0 && charIdx < kChars && inChar < 4) {
            bits = glyphClock4x8(kMsg[charIdx], (uint8_t)inChar);
        }
        for (uint8_t y = 0; y < Matrix6x8::H; y++) {
            const uint16_t idx = Matrix6x8::xyToIndex(x, y);
            const uint8_t rowBit = (charIdx >= 0 && charIdx < kChars && inChar < 4)
                ? glyphClockBitForMatrixY(kMsg[charIdx], y)
                : (uint8_t)0;
            uint32_t colr = 0;
            if (bits & (1u << rowBit)) {
                colr = fg;
            }
            const uint16_t outIdx = (uint16_t)(pin0 + idx);
            if (outIdx <= (uint16_t)seg->stop && outIdx < stripLen) {
                _fx->setPixelColor(outIdx, colr);
            }
        }
    }
    const uint16_t tailStart = (uint16_t)(pin0 + Matrix6x8::NUM);
    for (uint16_t i = tailStart; i <= (uint16_t)seg->stop && i < stripLen; i++) {
        _fx->setPixelColor(i, 0);
    }

    stepDiv++;
    if (stepDiv >= 2) {
        stepDiv = 0;
        scrollCol++;
        if (scrollCol >= kTotalCols) {
            scrollCol = 0;
        }
    }

    return seg->speed > 0 ? seg->speed : 130;
}

/*
 * Espectrograma / AudioFire / VU: 64 muestras (potencia de 2), FFT Cooley–Tukey
 * vía arduinoFFT (Gestor de librerías → "arduinoFFT" de kosme, versión ≥ 2.0).
 */
static constexpr int kSpecSamples = 64;
static constexpr int kSpecBatch = 64;

/** Buffer + forma de onda para FFT (ADC en 12b; I²S como float tras mismo decode que el nivel). */
static uint16_t g_mic_fft_adc[kSpecSamples];
static float g_mic_wave[kSpecSamples];
#if defined(ARDUINO_ARCH_ESP32)
static int32_t g_mic_i32[kSpecSamples];
#endif
static uint16_t g_mic_fft_pos = 0;
static float g_mic_fft_peak_env = 0.08f;

static float g_fft_real[kSpecSamples];
static float g_fft_imag[kSpecSamples];
/** Fs nominal INMP441 @ 16 kHz (I²S); con ADC analógico la ráfaga sigue siendo analogRead. */
static ArduinoFFT<float> g_mic_fft_engine(
    g_fft_real, g_fft_imag, (uint16_t)kSpecSamples, 16000.0f);

/** 8 bandas en bins 1..31 (FFT 64 pt, sin DC); luego se mapean a 6 columnas de matriz. */
static const uint8_t kFftBand8Lo[8] = {1, 5, 9, 13, 17, 21, 25, 29};
static const uint8_t kFftBand8Hi[8] = {4, 8, 12, 16, 20, 24, 28, 31};

/** Margen extra al umbral ADC: requiere más variación en el bloque de muestras para abrir el FFT. */
static constexpr int kMicSpanSilenceExtra = 42;

/** Suelo mínimo del AGC (evita div/0); un floor alto aplastaba los ratios y las barras no subían. */
static constexpr float kMicAgcEnvFloor = 4e-5f;

/**
 * ADC + arduinoFFT + AGC (mismos mags[6] que antes para el espectro 6×8).
 * @return false si aún no hay kSpecSamples muestras (el caller debe `return 4`).
 */
static bool mic_fft_collect_and_compute(int mic_pin, float mags[6], float *out_mmax) {
#if defined(ARDUINO_ARCH_ESP32)
    if (mic_pin == REPISA_MIC_PIN_I2S) {
        if (!repisaMicIsReady() || !repisaMicReadBlockInt32(g_mic_i32, kSpecSamples)) {
            return false;
        }
        for (int i = 0; i < kSpecSamples; i++) {
            const int32_t s = REPISA_I2S_PCM_FROM_RAW(g_mic_i32[i]);
            const int32_t a = abs(s);
            if (a >= REPISA_MIC_SIGNAL_FLOOR && a < REPISA_MIC_SIGNAL_CEILING) {
                g_mic_wave[i] = (float)s;
            } else {
                g_mic_wave[i] = 0.f;
            }
        }
    } else
#endif
    {
        for (int k = 0; k < kSpecBatch && g_mic_fft_pos < (uint16_t)kSpecSamples; k++) {
            g_mic_fft_adc[g_mic_fft_pos++] = (uint16_t)analogRead(mic_pin);
        }
        if (g_mic_fft_pos < (uint16_t)kSpecSamples) {
            return false;
        }
        g_mic_fft_pos = 0;
        for (int i = 0; i < kSpecSamples; i++) {
            g_mic_wave[i] = (float)g_mic_fft_adc[i];
        }
    }

    float mean = 0;
    float wf_min = g_mic_wave[0];
    float wf_max = g_mic_wave[0];
    for (int i = 0; i < kSpecSamples; i++) {
        const float v = g_mic_wave[i];
        mean += v;
        if (v < wf_min) {
            wf_min = v;
        }
        if (v > wf_max) {
            wf_max = v;
        }
    }
    mean /= (float)kSpecSamples;
    const float span_f = wf_max - wf_min;

    const int span_cut =
        CustomEffects::micEffectiveSilenceSpan() + kMicSpanSilenceExtra +
        (int)((1.f - micSensNorm()) * 24.f);

    bool fft_silence;
    float span_gate = (float)span_cut;

#if defined(ARDUINO_ARCH_ESP32)
    /** SensibilidadMic RainMaker 0..1; `rm_noise_reject` alto = menos sensibilidad. */
    const float rm_mic_sens = micSensNorm();
    const float rm_noise_reject = 1.f - rm_mic_sens;
    int i2s_level_gate = REPISA_MIC_SOUNDLEVEL_MIN;

    if (mic_pin == REPISA_MIC_PIN_I2S) {
        const float span_floor_dyn =
            REPISA_MIC_BLOCK_SPAN_FLOOR +
            rm_noise_reject * REPISA_MIC_SPAN_EXTRA_FROM_SENS;
        const float span_scale_dyn =
            REPISA_MIC_BLOCK_SPAN_SCALE * (0.42f + 0.58f * rm_mic_sens);
        span_gate = fmaxf(span_floor_dyn, (float)span_cut * span_scale_dyn);
        i2s_level_gate =
            REPISA_MIC_SOUNDLEVEL_MIN +
            (int)(rm_noise_reject * (float)REPISA_MIC_LEVEL_EXTRA_FROM_SENS + 0.5f);
        /**
         * No abrir FFT solo con nivel alto y span casi nulo (DC/offset I²S → barras 1–2 en silencio).
         * Si `span_for_level` es demasiado bajo (p. ej. 0.15×gate), un silencio ruidoso (~150–300) pasaba.
         * Calibra con el plotter: audio real trace 4 > ~1000 → exige aquí ~0.38–0.45× ese orden.
         */
        const float span_for_level =
            fmaxf(
                REPISA_MIC_SPAN_FOR_LEVEL_GATE_MIN,
                fmaxf(
                    span_gate * REPISA_MIC_SPAN_FOR_LEVEL_FRAC_GATE,
                    REPISA_MIC_BLOCK_SPAN_FLOOR * REPISA_MIC_SPAN_FOR_LEVEL_FRAC_BLOCK_FLOOR)) +
            rm_noise_reject * REPISA_MIC_SPAN_FOR_LEVEL_GATE_EXTRA;
        const bool span_ok = span_f >= span_gate;
        const bool level_and_span_ok =
            ((float)soundLevel > (float)i2s_level_gate) && (span_f >= span_for_level);
        fft_silence = !(span_ok || level_and_span_ok);
    } else {
        fft_silence = span_f < span_gate;
    }
#else
    fft_silence = span_f < span_gate;
#endif

    if (fft_silence) {
        for (int c = 0; c < 6; c++) {
            mags[c] = 0.0f;
        }
        g_mic_fft_peak_env = g_mic_fft_peak_env * 0.90f;
        if (g_mic_fft_peak_env < 1e-5f) {
            g_mic_fft_peak_env = 1e-5f;
        }
        if (out_mmax) {
            *out_mmax = 0.f;
        }
    } else {
#if defined(ARDUINO_ARCH_ESP32)
        /* I²S: escala REPISA_MIC_INV_FFT_SCALE_I2S en config.h (PCM14). */
        const float inv_fft_scale =
            (mic_pin == REPISA_MIC_PIN_I2S) ? REPISA_MIC_INV_FFT_SCALE_I2S : 4096.f;
#else
        const float inv_fft_scale = 4096.f;
#endif
        for (int i = 0; i < kSpecSamples; i++) {
            g_fft_real[i] = (g_mic_wave[i] - mean) / inv_fft_scale;
            g_fft_imag[i] = 0.f;
        }
        g_mic_fft_engine.windowing(FFTWindow::Hann, FFTDirection::Forward, false);
        g_mic_fft_engine.compute(FFTDirection::Forward);
        g_mic_fft_engine.complexToMagnitude();

#if defined(ARDUINO_ARCH_ESP32)
        if (mic_pin == REPISA_MIC_PIN_I2S) {
            /* Residuo DC en bin 0 y fuga fuerte en 1–2 (silencio → dos primeras columnas iluminadas). */
            g_fft_real[0] = 0.f;
            g_fft_real[1] *= REPISA_MIC_I2S_LOWBIN_ATTENUATION;
            g_fft_real[2] *= (REPISA_MIC_I2S_LOWBIN_ATTENUATION + 0.33f);
        }
#endif

        float b8[8];
        for (int b = 0; b < 8; b++) {
            float acc = 0.f;
            const unsigned w = (unsigned)(kFftBand8Hi[b] - kFftBand8Lo[b] + 1);
            for (unsigned k = kFftBand8Lo[b]; k <= kFftBand8Hi[b]; k++) {
                acc += g_fft_real[k];
            }
            b8[b] = acc / (float)w;
        }
        for (int c = 0; c < 5; c++) {
            mags[c] = b8[c];
        }
        mags[5] = (b8[5] + b8[6] + b8[7]) / 3.f;

        static const float kBandGain[6] = {0.55f, 0.78f, 1.00f, 1.20f, 1.45f, 1.75f};
        for (int c = 0; c < 6; c++) {
            mags[c] *= kBandGain[c];
        }
        float raw6[6];
        float raw_peak = 0.f;
        for (int c = 0; c < 6; c++) {
            raw6[c] = mags[c];
            if (raw6[c] > raw_peak) {
                raw_peak = raw6[c];
            }
        }
        const float kSpecLap = 0.13f;
        for (int c = 0; c < 6; c++) {
            float side = 0.f;
            int cnt = 0;
            if (c > 0) {
                side += raw6[c - 1];
                cnt++;
            }
            if (c < 5) {
                side += raw6[c + 1];
                cnt++;
            }
            const float neigh = cnt > 0 ? side / (float)cnt : 0.f;
            mags[c] = fmaxf(0.f, raw6[c] - kSpecLap * neigh);
        }

        /* Pico por columna (misma escala que las barras). Antes se usaba max(bin FFT) → norm
         * enorme frente a mags[] (promedios por banda) y ratio casi nulo con música real. */
        float mpeak = 0.f;
        for (int c = 0; c < 6; c++) {
            if (mags[c] > mpeak) {
                mpeak = mags[c];
            }
        }

        /* Ruido de fondo suele ser “plano” (todas las bandas parecidas); la música tiene más contraste. */
        float mean6 = 0.f;
        for (int c = 0; c < 6; c++) {
            mean6 += mags[c];
        }
        mean6 /= 6.f;
        const float band_snr = mpeak / (mean6 + 1e-7f);

        float mag_min = mags[0];
        for (int c = 1; c < 6; c++) {
            if (mags[c] < mag_min) {
                mag_min = mags[c];
            }
        }
        const float band_contrast = mpeak / (mag_min + 1e-7f);

        const float env_ref = fmaxf(g_mic_fft_peak_env, kMicAgcEnvFloor);
        const float mpeak_rel = mpeak / env_ref;

        const bool quiet_flat =
            (band_snr < 1.22f && mpeak_rel < 0.13f &&
             mpeak < fmaxf(env_ref * 0.095f, 1.6e-3f));
        const bool quiet_even =
            (band_contrast < 1.36f && mpeak_rel < 0.17f &&
             mpeak < fmaxf(env_ref * 0.10f, 2.3e-3f));
        const bool quiet_weak_vs_env =
            (mpeak_rel < 0.058f && mpeak < env_ref * 0.072f);
        const bool quiet_abs =
            (mpeak < 2.0e-4f && raw_peak < 4.5e-4f);
#if defined(ARDUINO_ARCH_ESP32)
        /* ADC: mismos tests relativos de siempre. I²S: mismos + patrón “solo graves/fuga” en silencio eléctrico. */
        const bool adc_quiet =
            (mic_pin != REPISA_MIC_PIN_I2S) &&
            (quiet_flat || quiet_even || quiet_weak_vs_env || quiet_abs);
        const bool i2s_quiet =
            (mic_pin == REPISA_MIC_PIN_I2S) &&
            ((quiet_flat || quiet_even || quiet_weak_vs_env || quiet_abs) ||
             ((span_f < span_gate * 1.55f) && ((mags[0] + mags[1]) > 1.85f * (mags[2] + mags[3]) + 1e-7f) &&
              (band_snr < 1.42f)));
        const bool spectral_quiet = adc_quiet || i2s_quiet;
#else
        const bool spectral_quiet =
            quiet_flat || quiet_even || quiet_weak_vs_env || quiet_abs;
#endif

        if (spectral_quiet) {
            for (int c = 0; c < 6; c++) {
                mags[c] = 0.f;
            }
            g_mic_fft_peak_env = g_mic_fft_peak_env * 0.90f;
            if (g_mic_fft_peak_env < 1e-5f) {
                g_mic_fft_peak_env = 1e-5f;
            }
            if (out_mmax) {
                *out_mmax = 0.f;
            }
        } else {
            const float mmax = mpeak;
            if (mmax > g_mic_fft_peak_env * 1.04f) {
                g_mic_fft_peak_env = g_mic_fft_peak_env * 0.62f + mmax * 0.38f;
            } else {
                g_mic_fft_peak_env = g_mic_fft_peak_env * 0.91f + mmax * 0.09f;
            }
            if (g_mic_fft_peak_env < kMicAgcEnvFloor) {
                g_mic_fft_peak_env = kMicAgcEnvFloor;
            }
            if (out_mmax) {
                *out_mmax = mmax;
            }
        }
    }
    return true;
}

static uint32_t turbo_mix_rgb(float t, uint32_t a, uint32_t b) {
    if (t <= 0.f) {
        return a;
    }
    if (t >= 1.f) {
        return b;
    }
    const uint8_t ra = (uint8_t)((a >> 16) & 0xFFu);
    const uint8_t ga = (uint8_t)((a >> 8) & 0xFFu);
    const uint8_t ba = (uint8_t)(a & 0xFFu);
    const uint8_t rb = (uint8_t)((b >> 16) & 0xFFu);
    const uint8_t gb = (uint8_t)((b >> 8) & 0xFFu);
    const uint8_t bb = (uint8_t)(b & 0xFFu);
    return ((uint32_t)(uint8_t)((float)ra + t * ((float)rb - (float)ra)) << 16) |
        ((uint32_t)(uint8_t)((float)ga + t * ((float)gb - (float)ga)) << 8) |
        (uint32_t)(uint8_t)((float)ba + t * ((float)bb - (float)ba));
}

static uint32_t turbo_rgb_energy(uint32_t c) {
    return (uint32_t)((c >> 16) & 0xFFu) + (uint32_t)((c >> 8) & 0xFFu) + (uint32_t)(c & 0xFFu);
}

/**
 * TurboBoost — pista Hot Wheels + estelas de nitro por banda FFT:
 * base en toda la matriz; cada disparo llena la columna con gradiente (amarillo punta → naranja → brasas);
 * meta fila 7: destello blanco + nitro cian un frame.
 */
uint16_t CustomEffects::turboBoost() {
    if (!_fx) {
        return 50;
    }
    WS2812FX::Segment* seg = _fx->getSegment();
    if (!seg) {
        return 50;
    }
    const uint16_t stripLen = _fx->getLength();

    static float proj_y[Matrix6x8::W];
    static uint8_t flash_frm[Matrix6x8::W];
    static float prev_ratio[Matrix6x8::W];
    static uint8_t turbo_inited = 0;

    if (!turbo_inited) {
        for (uint8_t c = 0; c < Matrix6x8::W; c++) {
            proj_y[c] = -1.f;
            flash_frm[c] = 0;
            prev_ratio[c] = 0.f;
        }
        turbo_inited = 1;
    }

    static constexpr float kBarPow = 0.74f;
    static constexpr float kRatioCap = 1.55f;
    static constexpr float kThrFire = 0.34f;
    static constexpr float kThrArm = 0.22f;
    static constexpr float kShotSpeed = 1.75f;
    static constexpr uint8_t kFlashFrames = 7;

    const uint32_t kYellowTip = ((uint32_t)255 << 16) | ((uint32_t)230 << 8) | 45;
    const uint32_t kHwOrange = ((uint32_t)255 << 16) | ((uint32_t)68 << 8) | 0;
    const uint32_t kRedCoals = ((uint32_t)110 << 16) | ((uint32_t)12 << 8) | 0;

    const float sens = micSensNorm();
    const float thr_on = kThrFire - sens * 0.07f;
    const float thr_off = kThrArm - sens * 0.06f;

    float track_heat = 0.f;

    if (!REPISA_MIC_INPUT_ACTIVE(mic_pin)) {
        const uint32_t tm = millis();
        for (uint8_t col = 0; col < Matrix6x8::W; col++) {
            const float ph = (float)tm * 0.0031f + (float)col * 1.31f;
            const float raw = 0.38f + 0.62f * (0.5f + 0.5f * sinf(ph + sinf(ph * 0.37f) * 0.4f));
            const float shaped = powf(raw, kBarPow);
            const bool armed = prev_ratio[col] <= thr_on;
            if (shaped > thr_on && armed && proj_y[col] < 0.f && flash_frm[col] == 0) {
                proj_y[col] = 0.f;
            }
            prev_ratio[col] = (shaped < thr_off) ? 0.f : shaped;
        }
        track_heat = 0.45f + 0.55f * (0.5f + 0.5f * sinf((float)tm * 0.0024f));
    } else {
        float mags[6];
        float mmax_out = 0.f;
        if (!mic_fft_collect_and_compute(mic_pin, mags, &mmax_out)) {
            return 4;
        }
        const float env_agc = g_mic_fft_peak_env;
        float mpeak_b = 0.f;
        float acc = 0.f;
        for (int c = 0; c < 6; c++) {
            if (mags[c] > mpeak_b) {
                mpeak_b = mags[c];
            }
            acc += mags[c];
        }
        const float norm_v =
            fmaxf(fmaxf(env_agc, mpeak_b * 0.90f), kMicAgcEnvFloor) * micGlobalVuNormStretch(mic_pin);
        track_heat = fminf(1.f, (acc / 6.f) / fmaxf(norm_v, 1e-7f) * 0.85f);

        for (uint8_t col = 0; col < Matrix6x8::W; col++) {
            float ratio = norm_v > 1e-8f ? (mags[col] / norm_v) : 0.f;
            if (ratio < 0.f) {
                ratio = 0.f;
            }
            if (ratio > kRatioCap) {
                ratio = kRatioCap;
            }
            const float shaped = powf(ratio, kBarPow);
            const bool armed = prev_ratio[col] <= thr_on;
            if (shaped > thr_on && armed && proj_y[col] < 0.f && flash_frm[col] == 0) {
                proj_y[col] = 0.f;
            }
            prev_ratio[col] = (shaped < thr_off) ? 0.f : shaped;
        }
    }

    const int y_top = (int)Matrix6x8::H - 1;

    for (uint8_t col = 0; col < Matrix6x8::W; col++) {
        if (flash_frm[col] > 0) {
            continue;
        }
        if (proj_y[col] >= 0.f) {
            proj_y[col] += kShotSpeed;
            if (proj_y[col] >= (float)y_top - 0.25f) {
                proj_y[col] = -1.f;
                flash_frm[col] = kFlashFrames;
            }
        }
    }

    uint32_t pix[Matrix6x8::NUM];
    const float hf = fminf(1.f, track_heat * 1.15f);
    for (uint8_t x = 0; x < Matrix6x8::W; x++) {
        for (uint8_t y = 0; y < Matrix6x8::H; y++) {
            const float horizon = (float)y / fmaxf(1.f, (float)Matrix6x8::H - 1.f);
            const uint8_t rb = (uint8_t)(10.f + hf * 48.f * (1.f - horizon * 0.55f));
            const uint8_t gb = (uint8_t)(3.f + hf * 18.f * (1.f - horizon * 0.4f));
            const uint8_t bb = (uint8_t)(2.f + hf * 10.f);
            pix[Matrix6x8::xyToIndex(x, y)] = _fx->Color(rb, gb, bb);
        }
    }

    for (uint8_t x = 0; x < Matrix6x8::W; x++) {
        if (flash_frm[x] > 0) {
            uint32_t hit = ((uint32_t)255 << 16) | ((uint32_t)255 << 8) | 255;
            if (flash_frm[x] == kFlashFrames) {
                hit = ((uint32_t)160 << 16) | ((uint32_t)255 << 8) | 255;
            }
            const uint16_t idm = Matrix6x8::xyToIndex(x, (uint8_t)y_top);
            if (idm < Matrix6x8::NUM) {
                const uint32_t prev = pix[idm];
                pix[idm] = turbo_rgb_energy(hit) > turbo_rgb_energy(prev) ? hit : prev;
            }
            flash_frm[x]--;
        } else if (proj_y[x] >= 0.f) {
            const float hy = proj_y[x];
            for (uint8_t y = 0; y < Matrix6x8::H; y++) {
                const float d = hy - (float)y;
                if (d < -0.55f) {
                    continue;
                }
                const float span = fmaxf(hy + 0.7f, 1.2f);
                float along = 1.f - d / span;
                if (along < 0.f) {
                    along = 0.f;
                }
                if (along > 1.f) {
                    along = 1.f;
                }
                const float fall = expf(-fmaxf(0.f, d) * 0.34f);
                uint32_t band = kYellowTip;
                if (along < 0.25f) {
                    band = kYellowTip;
                } else if (along < 0.55f) {
                    band = turbo_mix_rgb((along - 0.25f) / 0.30f, kYellowTip, kHwOrange);
                } else {
                    band = turbo_mix_rgb((along - 0.55f) / 0.45f, kHwOrange, kRedCoals);
                }
                const float tip_boost = (d < 1.1f) ? 1.18f : 1.f;
                float bri_f = fall * tip_boost * (0.42f + 0.58f * (1.f - along * 0.65f));
                if (bri_f > 1.f) {
                    bri_f = 1.f;
                }
                const uint8_t rr =
                    (uint8_t)((float)((band >> 16) & 0xFFu) * bri_f);
                const uint8_t gg =
                    (uint8_t)((float)((band >> 8) & 0xFFu) * bri_f);
                const uint8_t bb = (uint8_t)((float)(band & 0xFFu) * bri_f);
                const uint32_t sc = _fx->Color(rr, gg, bb);
                const uint16_t idm = Matrix6x8::xyToIndex(x, y);
                if (idm >= Matrix6x8::NUM) {
                    continue;
                }
                const uint32_t prev = pix[idm];
                pix[idm] = turbo_rgb_energy(sc) > turbo_rgb_energy(prev) ? sc : prev;
            }
        }
    }

    for (uint16_t i = 0; i < Matrix6x8::NUM && i < stripLen; i++) {
        _fx->setPixelColor(i, pix[i]);
    }

    for (uint16_t i = Matrix6x8::NUM; i < stripLen; i++) {
        _fx->setPixelColor(i, 0);
    }

    return 8;
}

/**
 * Cuadro concéntrico desde el centro geométrico (distancia Chebyshev): el bloque crece con los graves;
 * brillo y tono (HSV) siguen golpes y balance medio/agudo.
 */
uint16_t CustomEffects::hotWheelsNitro() {
    if (!_fx) {
        return 50;
    }
    WS2812FX::Segment* seg = _fx->getSegment();
    if (!seg) {
        return 50;
    }
    const uint16_t stripLen = _fx->getLength();
    const uint32_t now_ms = millis();

    static float rad_smooth = 0.f;
    static float beat_flash = 0.f;
    static float beat_env = 0.f;
    static float hue_kick = 0.f;

    static constexpr float kBarPow = 0.74f;
    static constexpr float kRatioCap = 1.55f;
    static constexpr float kBassPow = 0.52f;
    static constexpr float kAttack = 0.92f;
    static constexpr float kDecay = 0.52f;

    /* Centro exacto entre LEDs: (2.5, 3.5) en 6×8. */
    const float cx = 0.5f * (float)((int)Matrix6x8::W - 1);
    const float cy = 0.5f * (float)((int)Matrix6x8::H - 1);
    /* Radio máximo Chebyshev desde el centro hasta una esquina = max(cx, cy). */
    const float d_max = fmaxf(cx, cy);

    float radius_target = 0.f;
    float level_drive = 0.f;
    float bass_drive = 0.f;
    float color_ratio = 0.5f;

    if (!REPISA_MIC_INPUT_ACTIVE(mic_pin)) {
        const float ph = (float)now_ms * 0.0028f;
        bass_drive = 0.38f + 0.62f * (0.5f + 0.5f * sinf(ph));
        radius_target = bass_drive * d_max * 1.05f;
        if (radius_target > d_max) {
            radius_target = d_max;
        }
        level_drive = 0.42f + 0.35f * (0.5f + 0.5f * sinf(ph * 0.65f));
        color_ratio = 0.5f + 0.5f * sinf(ph * 1.15f + 0.7f);
        /* Pulso tipo beat en demo. */
        const float pk = powf(fmaxf(0.f, sinf(ph * 5.8f)), 14.f);
        beat_flash = fmaxf(beat_flash * 0.86f, pk * 1.15f);
        if (pk > 0.55f) {
            hue_kick = fminf(1.f, hue_kick + 0.55f);
        }
    } else {
        float mags[6];
        float mmax_out = 0.f;
        if (!mic_fft_collect_and_compute(mic_pin, mags, &mmax_out)) {
            return 4;
        }
        const float env_agc = g_mic_fft_peak_env;
        float mpeak_b = 0.f;
        for (int c = 0; c < 6; c++) {
            if (mags[c] > mpeak_b) {
                mpeak_b = mags[c];
            }
        }
        const float norm_v =
            fmaxf(fmaxf(env_agc, mpeak_b * 0.90f), kMicAgcEnvFloor) * micGlobalVuNormStretch(mic_pin);

        float lr = norm_v > 1e-8f ? (mmax_out / norm_v) : 0.f;
        if (lr < 0.f) {
            lr = 0.f;
        }
        if (lr > kRatioCap) {
            lr = kRatioCap;
        }
        level_drive = powf(lr, kBarPow);

        float bass_peak = 0.f;
        float bass_sum = 0.f;
        for (int c = 0; c < 3; c++) {
            float ratio = norm_v > 1e-8f ? (mags[c] / norm_v) : 0.f;
            if (ratio < 0.f) {
                ratio = 0.f;
            }
            if (ratio > kRatioCap) {
                ratio = kRatioCap;
            }
            const float shaped = powf(ratio, kBassPow);
            bass_sum += shaped;
            if (shaped > bass_peak) {
                bass_peak = shaped;
            }
        }
        bass_sum /= 3.f;
        bass_drive = fmaxf(bass_sum * 1.08f, bass_peak * 1.25f);
        radius_target = bass_drive * d_max * 1.12f;
        if (radius_target > d_max) {
            radius_target = d_max;
        }

        /* Color según medios + agudos (columnas 3–5 del espectro 6 bandas). */
        float mid_hi = 0.f;
        for (int c = 3; c < 6; c++) {
            float ratio = norm_v > 1e-8f ? (mags[c] / norm_v) : 0.f;
            if (ratio < 0.f) {
                ratio = 0.f;
            }
            if (ratio > kRatioCap) {
                ratio = kRatioCap;
            }
            mid_hi += powf(ratio, 0.62f);
        }
        mid_hi /= 3.f;
        color_ratio = fminf(1.f, mid_hi * 1.35f);

        /* Detección simple de beat: pico del instantáneo respecto a envolvente lenta. */
        const float inst = mmax_out;
        beat_env = beat_env * 0.93f + inst * 0.07f;
        const float rel = beat_env > 1e-9f ? inst / beat_env : 0.f;
        const float floor_inst = fmaxf(kMicAgcEnvFloor * 120.f, norm_v * 2.5e-4f);
        if (rel > 1.42f && inst > floor_inst * 3.f) {
            beat_flash = fminf(1.f, beat_flash + 0.48f);
            hue_kick = fminf(1.f, hue_kick + 0.42f);
        }
        beat_flash *= 0.84f;
    }

    hue_kick *= 0.90f;

    if (radius_target > rad_smooth) {
        rad_smooth += (radius_target - rad_smooth) * kAttack;
    } else {
        rad_smooth -= (rad_smooth - radius_target) * kDecay;
    }
    if (rad_smooth < 0.f) {
        rad_smooth = 0.f;
    }
    if (rad_smooth > d_max) {
        rad_smooth = d_max;
    }

    const uint32_t hue_spin = (uint32_t)(now_ms / 4U) & 0xFFFFU;
    const uint16_t hue =
        (uint16_t)(hue_spin + (uint16_t)(color_ratio * 15000.f) + (uint16_t)(hue_kick * 11000.f) +
                   (uint16_t)(beat_flash * 7500.f));

    for (uint8_t x = 0; x < Matrix6x8::W; x++) {
        for (uint8_t y = 0; y < Matrix6x8::H; y++) {
            const uint16_t idx = Matrix6x8::xyToIndex(x, y);
            if (idx >= stripLen) {
                continue;
            }
            const float dx = fabsf((float)x - cx);
            const float dy = fabsf((float)y - cy);
            const float d = fmaxf(dx, dy);

            uint32_t colr = 0;
            if (d <= rad_smooth + 0.02f && rad_smooth > 0.02f) {
                const float edge = rad_smooth > 1e-4f ? d / rad_smooth : 0.f;
                const float center_boost = (1.f - edge) * 0.28f;
                const int bri = (int)(22.f + beat_flash * 218.f + level_drive * 95.f + center_boost * 120.f);
                const uint8_t vb = (uint8_t)constrain(bri, 0, 255);
                colr = _fx->ColorHSV(hue, 255, vb);
            }

            _fx->setPixelColor(idx, colr);
        }
    }

    for (uint16_t i = Matrix6x8::NUM; i < stripLen; i++) {
        _fx->setPixelColor(i, 0);
    }

    return 18;
}

// Espectrograma vertical 6×8 (serpentina Matrix6x8). 6 columnas desde FFT 64 + 8 bandas.
uint16_t CustomEffects::spectrumVertical() {
    if (!_fx) {
        return 50;
    }
    WS2812FX::Segment* seg = _fx->getSegment();
    if (!seg) {
        return 50;
    }

    const uint16_t stripLen = _fx->getLength();

    if (!REPISA_MIC_INPUT_ACTIVE(mic_pin)) {
        for (uint16_t i = 0; i < stripLen && i < Matrix6x8::NUM; i++) {
            _fx->setPixelColor(i, 0x080808);
        }
        return seg->speed;
    }

    float mags[6];
    float mmax_unused = 0.f;
    if (!mic_fft_collect_and_compute(mic_pin, mags, &mmax_unused)) {
        return 4;
    }
    const float env_agc = g_mic_fft_peak_env;
    float mpeak_frame = 0.f;
    for (int c = 0; c < 6; c++) {
        if (mags[c] > mpeak_frame) {
            mpeak_frame = mags[c];
        }
    }
    const float norm_vis = fmaxf(fmaxf(env_agc, mpeak_frame * 0.90f), kMicAgcEnvFloor);

    /* === "Torre de acrílico": gradiente vertical fijo por fila ===
     * Filas 0-2 (graves): Azul → Cian.
     * Filas 3-5 (medios): Verde → Amarillo.
     * Filas 6-7 (picos):  Naranja → Rojo. */
    static const uint16_t kRowHue[8] = {
        (uint16_t)(65535UL * 240UL / 360UL),   // 0: azul
        (uint16_t)(65535UL * 210UL / 360UL),   // 1: azul-cian
        (uint16_t)(65535UL * 180UL / 360UL),   // 2: cian
        (uint16_t)(65535UL * 120UL / 360UL),   // 3: verde
        (uint16_t)(65535UL *  90UL / 360UL),   // 4: verde-amarillo
        (uint16_t)(65535UL *  60UL / 360UL),   // 5: amarillo
        (uint16_t)(65535UL *  30UL / 360UL),   // 6: naranja
        (uint16_t)(0),                          // 7: rojo
    };

    /* Estado por columna: barra suavizada, pico flotante con física, velocidad y hold timer + EMA del nivel. */
    static float bar_pos[Matrix6x8::W] = {0};
    static float peakPos[Matrix6x8::W] = {0};
    static float peakVelocity[Matrix6x8::W] = {0};
    static uint32_t holdTimer[Matrix6x8::W] = {0};
    static float ema_level[Matrix6x8::W] = {0};

    /* Suavizado de entrada (EMA) para transiciones limpias a ~60 fps. α alto = más reactivo. */
    static constexpr float kEmaAlpha = 0.54f;
    /* Mapeo del ratio a altura: exponente más bajo = más altura útil con ratio < 1. */
    static constexpr float kBarPow = 0.74f;
    static constexpr float kRatioCap = 1.55f;
    /* Dinámica de la barra principal. */
    static constexpr float kAttack = 0.82f;     // subida proporcional al gap
    static constexpr float kDecay = 0.16f;      // bajada proporcional
    static constexpr float kMaxFall = 0.48f;    // amortiguación: caída máxima por frame
    /* Física del pico flotante con gravedad. */
    static constexpr uint32_t kHoldTime = 250UL;
    static constexpr float kGravity = 0.05f;
    static constexpr float kMaxPeakVel = 1.2f;

    const uint32_t now_ms = millis();

    for (uint8_t col = 0; col < Matrix6x8::W; col++) {
        /* Nivel objetivo: ratio normalizado por AGC, con curva y mapeo a alturas 0..H. */
        float ratio = norm_vis > 1e-8f ? (mags[col] / norm_vis) : 0.f;
        if (ratio < 0.f) ratio = 0.f;
        if (ratio > kRatioCap) ratio = kRatioCap;
        const float target = powf(ratio, kBarPow) * (float)Matrix6x8::H;

        /* EMA: out = (1-α)·prev + α·target. Suaviza saltos del FFT antes del render. */
        ema_level[col] = ema_level[col] * (1.f - kEmaAlpha) + target * kEmaAlpha;
        float lvl = ema_level[col];
        if (lvl < 0.f) lvl = 0.f;
        if (lvl > (float)Matrix6x8::H) lvl = (float)Matrix6x8::H;

        /* Barra principal: ataque rápido al subir, caída amortiguada al bajar. */
        float bp = bar_pos[col];
        if (lvl > bp) {
            bp += (lvl - bp) * kAttack;
        } else {
            float fall = (bp - lvl) * kDecay;
            if (fall > kMaxFall) fall = kMaxFall;   // amortiguación dura: no más de kMaxFall por frame
            bp -= fall;
        }
        if (bp < 0.f) bp = 0.f;
        if (bp > (float)Matrix6x8::H) bp = (float)Matrix6x8::H;
        bar_pos[col] = bp;

        /* Pico flotante: sube de inmediato al nuevo nivel, mantiene 250 ms y luego cae con gravedad. */
        if (lvl > peakPos[col]) {
            peakPos[col] = lvl;
            peakVelocity[col] = 0.f;
            holdTimer[col] = now_ms;
        } else if ((uint32_t)(now_ms - holdTimer[col]) >= kHoldTime) {
            peakVelocity[col] += kGravity;                                  // v_n = v_{n-1} + g
            if (peakVelocity[col] > kMaxPeakVel) peakVelocity[col] = kMaxPeakVel;
            peakPos[col] -= peakVelocity[col];                              // pos_n = pos_{n-1} - v_n
        }
        /* El pico nunca puede quedar por debajo de la barra principal. */
        if (peakPos[col] < bp) {
            peakPos[col] = bp;
            peakVelocity[col] = 0.f;
        }
        if (peakPos[col] < 0.f) peakPos[col] = 0.f;
        if (peakPos[col] > (float)Matrix6x8::H) peakPos[col] = (float)Matrix6x8::H;
    }

    /* Renderizado: barra coloreada por fila + LED blanco de contraste para el pico. */
    for (uint8_t col = 0; col < Matrix6x8::W; col++) {
        int bar_h = (int)(bar_pos[col] + 0.5f);
        if (bar_h < 0) bar_h = 0;
        if (bar_h > (int)Matrix6x8::H) bar_h = (int)Matrix6x8::H;

        int peak_led = (int)(peakPos[col] + 0.5f);
        if (peak_led < 0) peak_led = 0;
        if (peak_led >= (int)Matrix6x8::H) peak_led = (int)Matrix6x8::H - 1;

        for (uint8_t row = 0; row < Matrix6x8::H; row++) {
            const uint16_t idx = Matrix6x8::xyToIndex(col, row);
            uint32_t colr = 0;
            if ((int)row < bar_h) {
                /* Brillo ligeramente mayor en la zona alta (rojo/naranja) para “quemar” como acrílico. */
                const uint8_t bri = (row >= 6) ? 240 : 215;
                colr = _fx->ColorHSV(kRowHue[row], 255, bri);
            } else if ((int)row == peak_led && peak_led >= bar_h && bar_h < (int)Matrix6x8::H) {
                colr = _fx->ColorHSV(0, 255, 255); // blanco puro para destacar
            }
            if (idx < stripLen) {
                _fx->setPixelColor(idx, colr);
            }
        }
    }
    for (uint16_t i = Matrix6x8::NUM; i < stripLen; i++) {
        _fx->setPixelColor(i, 0);
    }

    /* Debe ser >= tiempo real del FFT + show; si es menor, WS2812FX dispara el modo cada loop() y mata WiFi/IR/voz. */
    return 18;
}

uint16_t CustomEffects::audioFire() {
    if (!_fx) {
        return 50;
    }
    WS2812FX::Segment* seg = _fx->getSegment();
    if (!seg) {
        return 50;
    }
    const uint16_t stripLen = _fx->getLength();

    static uint8_t heat[Matrix6x8::W][Matrix6x8::H];
    static uint8_t heat_inited = 0;
    static float af_bass_sm = 0.f;
    static float af_level_sm = 0.f;

    if (!heat_inited) {
        memset(heat, 0, sizeof(heat));
        heat_inited = 1;
    }

    const float p = micSensNorm();
    const uint32_t tm = millis();

    /* Audio: mismo ADC+FFT+AGC que spectrumVertical (`mic_fft_collect_and_compute`). */
    float bass_drive = 0.f;
    float level_drive = 0.f;

    if (!REPISA_MIC_INPUT_ACTIVE(mic_pin)) {
        /* Sin mic: demo suave para ver el fuego */
        bass_drive = 0.35f + 0.25f * sinf((float)tm * 0.0031f);
        level_drive = 0.45f + 0.15f * sinf((float)tm * 0.0022f);
    } else {
        float mags[6];
        float mmax_out = 0.f;
        if (!mic_fft_collect_and_compute(mic_pin, mags, &mmax_out)) {
            return 4;
        }
        const float env_agc = g_mic_fft_peak_env;
        float mpeak_b = 0.f;
        for (int c = 0; c < 6; c++) {
            if (mags[c] > mpeak_b) {
                mpeak_b = mags[c];
            }
        }
        const float norm_v =
            fmaxf(fmaxf(env_agc, mpeak_b * 0.90f), kMicAgcEnvFloor) * micGlobalVuNormStretch(mic_pin);
        static constexpr float kBarPow = 0.74f;
        static constexpr float kRatioCap = 1.55f;
        /* Bajos = promedio de las 3 bandas graves con el mismo mapeo que las barras del espectro. */
        float bass_mix = 0.f;
        for (int c = 0; c < 3; c++) {
            float ratio = norm_v > 1e-8f ? (mags[c] / norm_v) : 0.f;
            if (ratio < 0.f) {
                ratio = 0.f;
            }
            if (ratio > kRatioCap) {
                ratio = kRatioCap;
            }
            bass_mix += powf(ratio, kBarPow);
        }
        bass_drive = bass_mix / 3.f;
        float lr = norm_v > 1e-8f ? (mmax_out / norm_v) : 0.f;
        if (lr < 0.f) {
            lr = 0.f;
        }
        if (lr > kRatioCap) {
            lr = kRatioCap;
        }
        level_drive = powf(lr, kBarPow);
        /* −50 % sensibilidad respecto al cálculo FFT (Spectrum6x8 sin cambios). */
        bass_drive *= 1.f;
        level_drive *= 0.3f;
    }

    /* Mismo α que el espectrograma para suavizar drive (kEmaAlpha = 0.42). */
    const float ema_b = 0.42f;
    af_bass_sm = af_bass_sm * (1.f - ema_b) + bass_drive * ema_b;
    af_level_sm = af_level_sm * (1.f - ema_b) + level_drive * ema_b;

    /* Enfriamiento: un poco más en filas altas cuando hay mucho nivel (evita saturar todo en blanco). */
    const uint8_t cool_hi =
        (uint8_t)constrain(8.f + (1.f - af_level_sm) * 18.f + af_level_sm * 12.f, 5.f, 30.f);
    const uint8_t cool_lo = (uint8_t)constrain(4.f + (1.f - af_level_sm) * 10.f + af_level_sm * 4.f, 2.f, 18.f);

    uint8_t prev[Matrix6x8::W][Matrix6x8::H];
    memcpy(prev, heat, sizeof(prev));

    for (uint8_t x = 0; x < Matrix6x8::W; x++) {
        for (uint8_t y = 0; y < Matrix6x8::H; y++) {
            const uint8_t cmax = (y >= 5) ? cool_hi : cool_lo;
            prev[x][y] = af_qsub8(prev[x][y], (uint8_t)(af_rand8() % (unsigned)(cmax + 1U)));
        }
    }

    /* Ignición solo en la base: bajos → más chispas, pero sin sumar tanto que sature toda la matriz. */
    const int spark_prob = (int)constrain(6.f + af_bass_sm * (120.f + p * 70.f), 0.f, 220.f);
    const uint8_t spark_gain = (uint8_t)constrain(28.f + af_bass_sm * (95.f + p * 45.f), 22.f, 115.f);
    for (uint8_t x = 0; x < Matrix6x8::W; x++) {
        if ((int)(af_rand8()) < spark_prob) {
            const uint8_t add = (uint8_t)(spark_gain / 2 + (af_rand8() % (unsigned)(spark_gain / 2 + 1U)));
            prev[x][0] = af_qadd8(prev[x][0], add);
        }
    }

    /* Subida + difusión lateral (más suave que antes: evita que una fila caliente blanquee toda la de arriba). */
    for (uint8_t y = 1; y < Matrix6x8::H; y++) {
        for (uint8_t x = 0; x < Matrix6x8::W; x++) {
            const uint16_t c = prev[x][y - 1];
            const uint16_t l = (x > 0) ? prev[x - 1][y - 1] : c;
            const uint16_t r = (x < Matrix6x8::W - 1) ? prev[x + 1][y - 1] : c;
            const uint32_t sum = (uint32_t)c * 2u + (uint32_t)l + (uint32_t)r + (uint32_t)prev[x][y];
            uint32_t v = (sum * 42u) >> 8;
            if (v > 255u) {
                v = 255u;
            }
            int vv = (int)v + (int)(af_rand8() & 5u) - 2;
            heat[x][y] = (uint8_t)constrain(vv, 0, 255);
        }
    }
    for (uint8_t x = 0; x < Matrix6x8::W; x++) {
        int h0 = (int)prev[x][0] + (int)(af_rand8() & 5u) - 2;
        heat[x][0] = (uint8_t)constrain(h0, 0, 255);
    }

    /* El nivel global ya modula chispas y enfriamiento; no sumar calor en bloque (eso saturaba en blanco). */
    for (uint8_t x = 0; x < Matrix6x8::W; x++) {
        for (uint8_t y = 0; y < Matrix6x8::H; y++) {
            if (heat[x][y] > 218) {
                heat[x][y] = af_qsub8(heat[x][y], (uint8_t)(3u + (af_rand8() & 11u)));
            }
        }
    }

    /* Color + mapeo Matrix6x8 (y=0 abajo). */
    for (uint8_t x = 0; x < Matrix6x8::W; x++) {
        for (uint8_t y = 0; y < Matrix6x8::H; y++) {
            const uint16_t idx = Matrix6x8::xyToIndex(x, y);
            if (idx < stripLen) {
                _fx->setPixelColor(idx, af_heatColor(_fx, heat[x][y]));
            }
        }
    }
    for (uint16_t i = Matrix6x8::NUM; i < stripLen; ++i) {
        _fx->setPixelColor(i, 0);
    }

    return 14;
}

/** VU clásico: verde (y=0 abajo) → amarillo → rojo (y=7 arriba), en grados 0..120 del círculo HSV. */
static inline uint16_t vu_meter_row_hue(uint8_t y) {
    const float hue_deg = 120.f * (1.f - (float)y / 7.f);
    return (uint16_t)(65535.f * hue_deg / 360.f);
}

uint16_t CustomEffects::vuMeterFull() {
    if (!_fx) {
        return 50;
    }
    WS2812FX::Segment* seg = _fx->getSegment();
    if (!seg) {
        return 50;
    }
    const uint16_t stripLen = _fx->getLength();
    const float p = micSensNorm();
    const uint32_t now_ms = millis();

    static float disp = 0.f;
    /** Solo dibujo: cae más lento que `disp` en bajadas bruscas (anti-strobe); zona entre ambos = “fantasma” tenue. */
    static float disp_soft = 0.f;
    static float peak_val = 0.f;
    static uint32_t peak_last_rise_ms = 0;

    static constexpr float kBarPow = 0.74f;
    static constexpr float kRatioCap = 1.55f;
    static constexpr uint32_t kPeakHoldMs = 125UL;

    float target = 0.f;

    const float disp_aggressive = disp;

    if (!REPISA_MIC_INPUT_ACTIVE(mic_pin)) {
        /* Demo sin mic: barrido tipo VU para validar gradiente y pico. */
        const float ph = (float)now_ms * 0.0045f;
        const float w = 0.5f + 0.5f * sinf(ph);
        target = w * (float)Matrix6x8::H * (0.72f + 0.28f * sinf(ph * 0.37f));
    } else {
        float mags[6];
        float mmax_out = 0.f;
        if (!mic_fft_collect_and_compute(mic_pin, mags, &mmax_out)) {
            return 4;
        }
        const float norm = g_mic_fft_peak_env;
        const float norm_vu =
            fmaxf(fmaxf(norm, mmax_out * 0.92f), kMicAgcEnvFloor) * micGlobalVuNormStretch(mic_pin);
        float lr = norm_vu > 1e-8f ? (mmax_out / norm_vu) : 0.f;
        if (lr < 0.f) {
            lr = 0.f;
        }
        if (lr > kRatioCap) {
            lr = kRatioCap;
        }
        const float level_drive = powf(lr, kBarPow);
        /* Suelo anti-ruido (MAX9814 / AGC ya más limpio; suelo más bajo = más recorrido del VU). */
        const float noise_floor = 0.022f + (1.f - p) * 0.055f;
        if (level_drive <= noise_floor) {
            target = 0.f;
        } else {
            const float head = 1.f - noise_floor;
            target = ((level_drive - noise_floor) / head) * (float)Matrix6x8::H;
        }
        if (target > (float)Matrix6x8::H) {
            target = (float)Matrix6x8::H;
        }
        /* −50 % sensibilidad VU (Spectrum6x8 sin cambios). */
        target *= 1.f;
    }

    /* Subida: como aguja al máximo instantáneo. Bajada: proporcional al hueco para seguir cambios leves sin “pegarse” a una fila. */
    if (target > disp) {
        disp = target;
    } else {
        const float gap_dn = disp - target;
        const float k_dn = 0.62f + p * 0.28f;
        disp -= gap_dn * k_dn;
        if (disp < target) {
            disp = target;
        }
    }
    if (disp < 0.f) {
        disp = 0.f;
    }

    const float drop_snap = disp_aggressive - disp;
    const bool brutal_drop = drop_snap > 1.05f;
    if (disp >= disp_soft - 1e-4f) {
        disp_soft = disp;
    } else if (brutal_drop) {
        const float k_ghost = 0.17f + p * 0.07f;
        disp_soft += (disp - disp_soft) * k_ghost;
    } else {
        disp_soft = disp;
    }
    if (disp_soft < 0.f) {
        disp_soft = 0.f;
    }
    if (disp_soft > (float)Matrix6x8::H) {
        disp_soft = (float)Matrix6x8::H;
    }

    /* Pico: tras breve hold baja más rápido hacia el nivel mostrado. */
    if (disp >= peak_val - 1e-3f) {
        peak_val = disp;
        peak_last_rise_ms = now_ms;
    } else if ((uint32_t)(now_ms - peak_last_rise_ms) >= kPeakHoldMs) {
        const float gap_p = peak_val - disp;
        const float pfall = (0.14f + (1.f - p) * 0.11f) * (1.65f + p * 0.35f);
        peak_val = fmaxf(disp, peak_val - fmaxf(gap_p * 0.55f, pfall));
    }

    const int peak_row = (peak_val > disp + 0.07f)
        ? (int)fminf(floorf(peak_val - 1e-4f), (float)(Matrix6x8::H - 1))
        : -1;

    for (uint8_t x = 0; x < Matrix6x8::W; x++) {
        for (uint8_t y = 0; y < Matrix6x8::H; y++) {
            const uint16_t idx = Matrix6x8::xyToIndex(x, y);
            if (idx >= stripLen) {
                continue;
            }
            const float lit_hard = disp - (float)y;
            const float lit_soft = disp_soft - (float)y;
            uint32_t colr = 0;
            float frac = 0.f;
            bool ghost_tail = false;
            if (lit_hard > 0.f) {
                frac = (lit_hard >= 1.f) ? 1.f : lit_hard;
            } else if (lit_soft > 0.f) {
                frac = (lit_soft >= 1.f) ? 1.f : lit_soft;
                ghost_tail = true;
            }
            if (frac > 0.f) {
                float bri_f = frac * 255.f;
                if (ghost_tail) {
                    bri_f *= 0.34f + p * 0.20f;
                }
                uint8_t bri = (uint8_t)(bri_f + 0.5f);
                if (bri < 1 && bri_f > 0.f) {
                    bri = 1;
                }
                const uint16_t hue = vu_meter_row_hue(y);
                colr = _fx->ColorHSV(hue, 255, bri);
            }
            if (peak_row >= 0 && (int)y == peak_row) {
                const uint16_t hue = vu_meter_row_hue((uint8_t)peak_row);
                colr = _fx->ColorHSV(hue, 32, 255);
            }
            _fx->setPixelColor(idx, colr);
        }
    }
    for (uint16_t i = Matrix6x8::NUM; i < stripLen; ++i) {
        _fx->setPixelColor(i, 0);
    }

    return 14;
}


uint16_t CustomEffects::impactShow() {
    if (!_fx) {
        return 50;
    }
    WS2812FX::Segment* seg = _fx->getSegment();
    if (!seg) {
        return 50;
    }
    const uint16_t stripLen = _fx->getLength();
    const uint32_t t = millis();
    const float cx0 = ((float)Matrix6x8::W - 1.f) * 0.5f;
    const float cy0 = ((float)Matrix6x8::H - 1.f) * 0.5f;

    static float dc = 2048.f;
    static float env = 0;
    static float prev = 0;
    static uint8_t pulse = 0;
    static int imp_s_last = -1;
    /** 0..1 suavizado: mismo origen que AudioFire/VU (`level_drive` tras FFT + AGC). */
    static float imp_vu_sm = 0.f;

    const float p = micSensNorm();
    const float drive_geom = 35.f + 35.f * sinf((float)t * 0.0021f);

    float vu_lin = 0.f;
    if (REPISA_MIC_INPUT_ACTIVE(mic_pin)) {
        float mags[6];
        float mmax_out = 0.f;
        if (!mic_fft_collect_and_compute(mic_pin, mags, &mmax_out)) {
            return 4;
        }
        const float norm = g_mic_fft_peak_env;
        const float norm_vu =
            fmaxf(fmaxf(norm, mmax_out * 0.92f), kMicAgcEnvFloor) * micGlobalVuNormStretch(mic_pin);
        static constexpr float kBarPowVu = 0.74f;
        static constexpr float kRatioCapVu = 1.55f;
        float lr = norm_vu > 1e-8f ? (mmax_out / norm_vu) : 0.f;
        if (lr < 0.f) {
            lr = 0.f;
        }
        if (lr > kRatioCapVu) {
            lr = kRatioCapVu;
        }
        const float level_drive = powf(lr, kBarPowVu);
        const float noise_floor = 0.022f + (1.f - p) * 0.055f;
        if (level_drive > noise_floor) {
            const float head = 1.f - noise_floor;
            vu_lin = (level_drive - noise_floor) / head;
        } else {
            vu_lin = 0.f;
        }
        if (vu_lin > 1.18f) {
            vu_lin = 1.18f;
        }
    } else {
        const float ph = (float)t * 0.0045f;
        vu_lin = 0.48f + 0.42f * sinf(ph);
    }

    /* Mismo α que AudioFire (`af_bass_sm` / `af_level_sm`, ema_b = 0.42f). */
    static constexpr float kImpactVuEma = 0.42f;
    imp_vu_sm = imp_vu_sm * (1.f - kImpactVuEma) + vu_lin * kImpactVuEma;

    if (REPISA_MIC_INPUT_ACTIVE(mic_pin)) {
#if defined(ARDUINO_ARCH_ESP32)
        const int s = (mic_pin == REPISA_MIC_PIN_I2S) ? (int)soundLevel : analogRead(mic_pin);
#else
        const int s = analogRead(mic_pin);
#endif
        int dj_step = 0;
#if defined(ARDUINO_ARCH_ESP32)
        if (mic_pin == REPISA_MIC_PIN_I2S && imp_s_last >= 0) {
            dj_step = (s > imp_s_last) ? (s - imp_s_last) : (imp_s_last - s);
        }
#endif
        dc = dc * 0.90f + (float)s * 0.10f;
        const float d_trans = fabsf((float)s - dc);
        float dead = (float)micEffectiveDeadband() * (0.78f - 0.28f * p);
#if defined(ARDUINO_ARCH_ESP32)
        if (mic_pin == REPISA_MIC_PIN_I2S) {
            dead *= REPISA_MIC_IMPACT_DEAD_MULT_I2S * 0.30f;
        }
#endif
        float d_t = 0.f;
        if (d_trans >= dead) {
            d_t = (d_trans - dead) * (1.05f + p * 1.35f);
        }

        float d = d_t;
#if defined(ARDUINO_ARCH_ESP32)
        /**
         * I²S: `soundLevel` es media de |PCM|; `dc` rápido → |s−dc| ≈ 0 con música sostenida.
         * Nivel sobre base lenta + `soundSpan` pasan por un dead más bajo y se suman al transitorio.
         */
        if (mic_pin == REPISA_MIC_PIN_I2S) {
            static float dc_slow = -1.f;
            if (dc_slow < 0.f) {
                dc_slow = (float)s;
            } else {
                dc_slow = dc_slow * 0.991f + (float)s * 0.009f;
            }
            const float sustain = fmaxf(0.f, (float)s - dc_slow);
            const float span_f = (float)soundSpan;
            /* Nivel sostenido (sustain + span nivel); ritmo fuerte solo vía stomp/dj_step (sin Δspan por buffer). */
            const float music_raw =
                sustain * (0.85f + p * 1.15f) + span_f * (0.0019f + p * 0.0016f);
            const float m_dead = dead * 0.14f + 4.f;
            const float music_net =
                (music_raw > m_dead) ? (music_raw - m_dead) * (1.45f + p * 0.85f) : 0.f;
            d = d_t + music_net;
            /* Solo |ΔsoundLevel| fuerte = ritmo; sin derivada de span (ruido I²S → titileo constante). */
            const float stomp_thr = 14.f + (1.f - p) * 38.f;
            const float stomp = fmaxf(0.f, (float)dj_step - stomp_thr);
            d += stomp * (0.95f + p * 0.75f);
        }
#endif
        if (mic_pin == REPISA_MIC_PIN_I2S) {
            const float ek_att = 0.42f - p * 0.10f;
            const float ek_dec = 0.82f - p * 0.16f;
            if (d > env) {
                env = env * ek_att + d * (1.f - ek_att);
            } else {
                env = env * ek_dec + d * (1.f - ek_dec);
            }
        } else {
            const float ek = 0.58f - p * 0.22f;
            env = env * ek + d * (1.f - ek);
        }
#if defined(ARDUINO_ARCH_ESP32)
        if (mic_pin == REPISA_MIC_PIN_I2S) {
            const float edge_i2s =
                fmaxf((4.f + (1.f - p) * 14.f) * 0.35f, env * (0.14f + p * 0.10f) + 2.f);
            const float floorv_i2s =
                fmaxf((5.f + (1.f - p) * 20.f) * 0.30f, env * (0.20f + p * 0.12f) + 3.f);
            const int dj_need = (int)(10.f + (1.f - p) * 38.f);
            const bool snap_hit = (dj_step >= dj_need);
            const bool snap_rel = (imp_s_last > 120) && (dj_step * 25 > imp_s_last);
            const bool env_hit = (env - prev > edge_i2s) && (env > floorv_i2s);
            if (snap_hit || snap_rel || env_hit) {
                pulse = 36;
            }
            imp_s_last = s;
        } else {
            const float edge = 4.f + (1.f - p) * 14.f;
            const float floorv = 5.f + (1.f - p) * 20.f;
            if ((env - prev > edge) && (env > floorv)) {
                pulse = 28;
            }
        }
#else
        {
            const float edge = 4.f + (1.f - p) * 14.f;
            const float floorv = 5.f + (1.f - p) * 20.f;
            if ((env - prev > edge) && (env > floorv)) {
                pulse = 28;
            }
        }
#endif
        prev = prev * (0.74f - p * 0.10f) + env * (0.26f + p * 0.10f);
    }

    /* Patrón geométrico / matiz fijo; V y S desde `imp_vu_sm` (FFT+AGC como AudioFire / SonidoColor). */
    const float phase = (float)t * 0.0042f;
    for (uint8_t col = 0; col < Matrix6x8::W; col++) {
        for (uint8_t row = 0; row < Matrix6x8::H; row++) {
            const uint16_t idx = Matrix6x8::xyToIndex(col, row);
            if (idx >= stripLen) {
                continue;
            }
            const float fx = (float)col - cx0;
            const float fy = (float)row - cy0;
            const float dist = sqrtf(fx * fx + fy * fy);
            const uint16_t hue = (uint16_t)(
                (int32_t)(sinf(phase + dist * 0.75f + (float)col * 0.35f) * 10000.f)
                + (int32_t)t * 3 + (int32_t)col * 3200 + (int32_t)row * 1800);
            const int base_geo =
                (int)(68.f + sinf(dist * 0.9f - phase * 6.f) * 50.f + drive_geom * 0.34f);
            /* `imp_vu_sm` ≈ 0..1 (igual cadena que VU: level_drive + noise_floor + EMA 0.42). */
            const float vu_boost = imp_vu_sm * 128.f + (pulse > 0 ? 34.f : 0.f);
            const int bri_boost = (int)vu_boost;
            const uint8_t sat =
                (uint8_t)constrain(52 + (int)(imp_vu_sm * 205.f), 48, 255);
            uint32_t c;
            if (pulse > 0) {
                const uint8_t sat_p =
                    (uint8_t)constrain((int)pulse * 5 + 55, 55, 220);
                c = _fx->ColorHSV(hue, sat_p, 255);
            } else {
                int val = base_geo + bri_boost;
                if (val < 36) {
                    val = 36;
                }
                if (val > 255) {
                    val = 255;
                }
                c = _fx->ColorHSV(hue, sat, (uint8_t)val);
            }
            _fx->setPixelColor(idx, c);
        }
    }
    if (pulse > 0) {
        pulse--;
    }
    for (uint16_t i = Matrix6x8::NUM; i < stripLen; ++i) {
        _fx->setPixelColor(i, 0);
    }
    return 10;
}