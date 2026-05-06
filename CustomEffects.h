#ifndef CUSTOM_EFFECTS_H
#define CUSTOM_EFFECTS_H

#include <Arduino.h>
#include <WS2812FX.h>

class CustomEffects {
public:
    static void begin(WS2812FX* fx);

    static uint8_t getJump3();
    static uint8_t getJump7();
    static uint8_t getFade3();
    static uint8_t getFade7();
    static uint8_t getSpectrum6x8();
    static uint8_t getSoundBright6x8();
    static uint8_t getSoundHue6x8();
    static uint8_t getImpact6x8();
    static uint8_t getHotWheels6x8();
    /** Pin ADC del micrófono (MAX9814 OUT, misma señal que reacción al sonido). */
    static void setMicPin(int pin);
    /** 0 = menos sensible, 100 = más (reacción al sonido + espectrograma). */
    static void setMicSensitivityPct(uint8_t pct0_100);
    static uint8_t getMicSensitivityPct();
    static int micEffectiveDeadband();
    static int micEffectiveSilenceSpan();
    static float micBrightnessEnvGain();
    // efectos
    static uint16_t jump3();
    static uint16_t jump7();
    static uint16_t fade3();
    static uint16_t fade7();
    static uint16_t spectrumVertical();
    /** Antes "SonidoBrillo": hoguera Fire2012 + FFT (bajos en la base). */
    static uint16_t audioFire();
    /** Antes relleno por matiz; ahora VU 6×8 con FFT + sensibilidad RainMaker (nombre modo: SonidoColor). */
    static uint16_t vuMeterFull();
    static uint16_t impactShow();
    static uint16_t hotWheelsMarquee();
private:
    static WS2812FX* _fx;
    static int mic_pin;

    static uint8_t modeJump3;
    static uint8_t modeJump7;
    static uint8_t modeFade3;
    static uint8_t modeFade7;
    static uint8_t modeSpectrum;
    static uint8_t modeSoundBright;
    static uint8_t modeSoundHue;
    static uint8_t modeImpact;
    static uint8_t modeHotWheels;

    static uint8_t mic_sens_pct;
};

#endif