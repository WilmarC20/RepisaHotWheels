#include "CustomEffects.h"
#include "Matrix6x8.h"
#include <arduinoFFT.h>
#include <cmath>
#include <string.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_random.h>
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
    modeJump3 = _fx->setCustomMode(F("Jump3"), jump3);
    modeJump7 = _fx->setCustomMode(F("Jump7"), jump7);
    modeFade3 = _fx->setCustomMode(F("Fade3"), fade3);
    //modeFade7 = _fx->setCustomMode(F("Fade7"), fade7);
    modeSpectrum = _fx->setCustomMode(F("Spectrum6x8"), spectrumVertical);
    modeSoundBright = _fx->setCustomMode(F("AudioFire"), audioFire);
    modeSoundHue = _fx->setCustomMode(F("SonidoColor"), vuMeterFull);
    modeHotWheels = _fx->setCustomMode(F("HotWheels"), hotWheelsMarquee);
    modeImpact = _fx->setCustomMode(F("Impacto"), impactShow);
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

// 🌈 JUMP7
uint16_t CustomEffects::jump7() {
    if (!_fx) {
        return 50;
    }
    WS2812FX::Segment* seg = _fx->getSegment();
    if (!seg) {
        return 50;
    }

    static uint8_t step = 0;
    static unsigned long last = 0;

    uint32_t colores[] = {
        0xFF0000,
        0x00FF00,
        0x0000FF,
        0xFFFF00,
        0x00FFFF,
        0xFF00FF,
        0xFFFFFF
    };

    if (millis() - last > seg->speed) {
        step = (step + 1) % 7;
        last = millis();
    }

    for (uint16_t i = seg->start; i <= seg->stop; i++) {
        _fx->setPixelColor(i, colores[step]);
    }

    return seg->speed; 
}

// 🌫️ FADE3
uint16_t CustomEffects::fade3() {
    if (!_fx) {
        return 50;
    }
    WS2812FX::Segment* seg = _fx->getSegment();
    if (!seg) {
        return 50;
    }

    static float t = 0;
    static uint8_t index = 0;
    static unsigned long last = 0;

    uint32_t colores[] = {
        0xFF0000,
        0x00FF00,
        0x0000FF
    };

    if (millis() - last > 20) {
        t += 0.02;

        if (t >= 1.0) {
            t = 0;
            index = (index + 1) % 3;
        }

        last = millis();
    }

    uint32_t c1 = colores[index];
    uint32_t c2 = colores[(index + 1) % 3];

    uint8_t r = ((c1 >> 16) & 0xFF) + (((c2 >> 16) & 0xFF) - ((c1 >> 16) & 0xFF)) * t;
    uint8_t g = ((c1 >> 8) & 0xFF) + (((c2 >> 8) & 0xFF) - ((c1 >> 8) & 0xFF)) * t;
    uint8_t b = (c1 & 0xFF) + ((c2 & 0xFF) - (c1 & 0xFF)) * t;

    uint32_t color = (r << 16) | (g << 8) | b;

    for (uint16_t i = seg->start; i <= seg->stop; i++) {
        _fx->setPixelColor(i, color);
    }

    return seg->speed; 
}

// 🌈 FADE7
uint16_t CustomEffects::fade7() {
    if (!_fx) {
        return 50;
    }
    WS2812FX::Segment* seg = _fx->getSegment();
    if (!seg) {
        return 50;
    }

    static float t = 0;
    static uint8_t index = 0;
    static unsigned long last = 0;

    uint32_t colores[] = {
        0xFF0000,
        0xFF7F00,
        0xFFFF00,
        0x00FF00,
        0x0000FF,
        0x4B0082,
        0x9400D3
    };

    if (millis() - last > 20) {
        t += 0.02;

        if (t >= 1.0) {
            t = 0;
            index = (index + 1) % 7;
        }

        last = millis();
    }

    uint32_t c1 = colores[index];
    uint32_t c2 = colores[(index + 1) % 7];

    uint8_t r = ((c1 >> 16) & 0xFF) + (((c2 >> 16) & 0xFF) - ((c1 >> 16) & 0xFF)) * t;
    uint8_t g = ((c1 >> 8) & 0xFF) + (((c2 >> 8) & 0xFF) - ((c1 >> 8) & 0xFF)) * t;
    uint8_t b = (c1 & 0xFF) + ((c2 & 0xFF) - (c1 & 0xFF)) * t;

    uint32_t color = (r << 16) | (g << 8) | b;

    for (uint16_t i = seg->start; i <= seg->stop; i++) {
        _fx->setPixelColor(i, color);
    }

    return seg->speed; 
}

/*
 * Espectrograma / AudioFire / VU: 64 muestras (potencia de 2), FFT Cooley–Tukey
 * vía arduinoFFT (Gestor de librerías → "arduinoFFT" de kosme, versión ≥ 2.0).
 */
static constexpr int kSpecSamples = 64;
static constexpr int kSpecBatch = 64;

/** Buffer + AGC compartidos: misma lectura/FFT que `spectrumVertical` (para AudioFire y espectro). */
static uint16_t g_mic_fft_adc[kSpecSamples];
static uint16_t g_mic_fft_pos = 0;
static float g_mic_fft_peak_env = 0.08f;

static float g_fft_real[kSpecSamples];
static float g_fft_imag[kSpecSamples];
/** Fs nominal (coherencia de bins; el muestreo real sigue siendo la ráfaga de analogRead). */
static ArduinoFFT<float> g_mic_fft_engine(
    g_fft_real, g_fft_imag, (uint16_t)kSpecSamples, 5000.0f);

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
    for (int k = 0; k < kSpecBatch && g_mic_fft_pos < (uint16_t)kSpecSamples; k++) {
        g_mic_fft_adc[g_mic_fft_pos++] = (uint16_t)analogRead(mic_pin);
    }
    if (g_mic_fft_pos < (uint16_t)kSpecSamples) {
        return false;
    }
    g_mic_fft_pos = 0;

    float mean = 0;
    uint16_t raw_min = 65535;
    uint16_t raw_max = 0;
    for (int i = 0; i < kSpecSamples; i++) {
        const uint16_t v = g_mic_fft_adc[i];
        if (v < raw_min) {
            raw_min = v;
        }
        if (v > raw_max) {
            raw_max = v;
        }
        mean += (float)g_mic_fft_adc[i];
    }
    mean /= (float)kSpecSamples;
    const uint16_t span = (raw_max > raw_min) ? (uint16_t)(raw_max - raw_min) : 0;

    const int span_cut =
        CustomEffects::micEffectiveSilenceSpan() + kMicSpanSilenceExtra +
        (int)((1.f - micSensNorm()) * 24.f);
    if ((int)span < span_cut) {
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
        for (int i = 0; i < kSpecSamples; i++) {
            g_fft_real[i] = ((float)g_mic_fft_adc[i] - mean) / 4096.f;
            g_fft_imag[i] = 0.f;
        }
        g_mic_fft_engine.windowing(FFTWindow::Hann, FFTDirection::Forward, false);
        g_mic_fft_engine.compute(FFTDirection::Forward);
        g_mic_fft_engine.complexToMagnitude();

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
        const bool spectral_quiet =
            quiet_flat || quiet_even || quiet_weak_vs_env || quiet_abs;

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

    if (mic_pin < 0) {
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

    if (mic_pin < 0) {
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
        const float norm_v = fmaxf(fmaxf(env_agc, mpeak_b * 0.90f), kMicAgcEnvFloor);
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
    static float peak_val = 0.f;
    static uint32_t peak_last_rise_ms = 0;

    static constexpr float kBarPow = 0.74f;
    static constexpr float kRatioCap = 1.55f;
    static constexpr uint32_t kPeakHoldMs = 200UL;

    float target = 0.f;

    if (mic_pin < 0) {
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
        const float norm_vu = fmaxf(fmaxf(norm, mmax_out * 0.92f), kMicAgcEnvFloor);
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
    }

    /* Ataque instantáneo, caída amortiguada (más lenta con sensibilidad baja = menos “nervioso”). */
    if (target > disp) {
        disp = target;
    } else {
        const float fall = 0.11f + (1.f - p) * 0.10f;
        disp = fmaxf(target, disp - fall);
    }
    if (disp < 0.f) {
        disp = 0.f;
    }

    /* Pico: sube con el nivel; tras 200 ms sin nuevo máximo, cae hacia el nivel actual. */
    if (disp >= peak_val - 1e-3f) {
        peak_val = disp;
        peak_last_rise_ms = now_ms;
    } else if ((uint32_t)(now_ms - peak_last_rise_ms) >= kPeakHoldMs) {
        const float pfall = 0.14f + (1.f - p) * 0.11f;
        peak_val = fmaxf(disp, peak_val - pfall);
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
            const float thr = disp - (float)y;
            uint32_t colr = 0;
            if (thr > 0.f) {
                const float frac = (thr >= 1.f) ? 1.f : thr;
                uint8_t bri = (uint8_t)(frac * 255.f + 0.5f);
                if (bri < 1 && frac > 0.f) {
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

    const float p = micSensNorm();
    float drive = 35.f + 35.f * sinf((float)t * 0.0021f);
    if (mic_pin >= 0) {
        const int s = analogRead(mic_pin);
        dc = dc * 0.90f + (float)s * 0.10f;
        float d = fabsf((float)s - dc);
        const float dead = (float)micEffectiveDeadband() * (0.78f - 0.28f * p);
        if (d < dead) {
            d = 0.f;
        } else {
            d = (d - dead) * (1.05f + p * 1.35f);
        }
        const float ek = 0.70f - p * 0.30f;
        env = env * ek + d * (1.f - ek);
        /* Trigger más sensible: detecta transientes con menos umbral y más duración de pulso. */
        const float edge = 4.f + (1.f - p) * 14.f;
        const float floorv = 5.f + (1.f - p) * 20.f;
        if ((env - prev > edge) && (env > floorv)) {
            pulse = 28;
        }
        prev = prev * (0.74f - p * 0.10f) + env * (0.26f + p * 0.10f);
        drive = 24.f + env * (1.25f + p * 2.25f);
    }

    if (pulse > 0) {
        pulse--;
    }

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
            int val = (int)(94.f + sinf(dist * 0.9f - phase * 6.f) * 56.f + drive * 0.68f);
            if (pulse > 0) {
                val += (int)pulse * 11;
            }
            val = (val < 34) ? 34 : val;
            val = (val > 255) ? 255 : val;
            const uint32_t c = _fx->ColorHSV(hue, 255, (uint8_t)val);
            _fx->setPixelColor(idx, c);
        }
    }
    for (uint16_t i = Matrix6x8::NUM; i < stripLen; ++i) {
        _fx->setPixelColor(i, 0);
    }
    return 10;
}