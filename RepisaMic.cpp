#if defined(ARDUINO_ARCH_ESP32)

#include "Arduino.h"
#include "RepisaMic.h"

#include <cstring>
#include <driver/i2s.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#if defined(CONFIG_IDF_TARGET_ESP32C6)
#define REPISA_MIC_IS_C6_BUILD 1
#else
#define REPISA_MIC_IS_C6_BUILD 0
#endif

// =====================================================
// CONFIG
// =====================================================

#define I2S_PORT I2S_NUM_0

// =====================================================
// VARIABLES
// =====================================================

int32_t sBuffer[256];

volatile int soundLevel = 0;
volatile int soundSpan = 0;
volatile int level_original = 0;

static volatile int s_threshold_cached = REPISA_MIC_THRESHOLD;

static bool s_driver_ok = false;

static TaskHandle_t s_mic_task_handle = nullptr;
static SemaphoreHandle_t s_mic_mtx = nullptr;

static int32_t s_shared[256];
static volatile size_t s_shared_bytes = 0;

static constexpr uint32_t kMicTaskStackWords = 4096;

static constexpr UBaseType_t kMicTaskPrio = 1;

#if REPISA_MIC_SERIAL_PLOTTER && REPISA_MIC_PLOT_ONCE_PER_BUFFER
static volatile uint32_t s_plot_generation = 0;
static uint32_t s_last_plot_generation = 0;
#endif

// =====================================================
// CALCULO NIVEL
// =====================================================

static void repisa_mic_compute_level_exact(
    const int32_t *buf,
    int samples,
    int thresh,
    int *out_level,
    int *out_levelORI
) {

    *out_level = 0;
    *out_levelORI = 0;

    if (!buf || samples <= 0) {
        return;
    }

    long levelSum = 0;

    for (int i = 0; i < samples; i++) {

        // ESTA ES LA PARTE IMPORTANTE
        // EXACTAMENTE IGUAL AL SKETCH QUE SI FUNCIONA
        int32_t sample = REPISA_I2S_PCM_FROM_RAW(buf[i]);

        levelSum += abs(sample);
    }

    int level = levelSum / samples;

    // threshold opcional
    level -= thresh;

    if (level < 0) {
        level = 0;
    }

    *out_level = level;
    *out_levelORI = level;
}

// =====================================================
// TASK AUDIO
// =====================================================

static void repisaMicAudioTask(void * /*pv*/) {

    for (;;) {

        if (!s_driver_ok) {

            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t bytes_read = 0;

        esp_err_t result = i2s_read(
            I2S_PORT,
            sBuffer,
            sizeof(sBuffer),
            &bytes_read,
            portMAX_DELAY
        );

        if (result != ESP_OK) {

            vTaskDelay(1);
            continue;
        }

        if (bytes_read == 0) {

            vTaskDelay(1);
            continue;
        }

        int samples = bytes_read / sizeof(int32_t);

        long level = 0;
        int32_t s_min = 0;
        int32_t s_max = 0;

        /* Mismo N que el bloque FFT (64): compara `soundSpan` con span_f / REPISA_MIC_BLOCK_SPAN_FLOOR. */
        const int span_n = (samples < 64) ? samples : 64;

        for (int i = 0; i < samples; i++) {

            int32_t sample = REPISA_I2S_PCM_FROM_RAW(sBuffer[i]);

            level += abs(sample);

            if (i < span_n) {
                if (i == 0) {
                    s_min = s_max = sample;
                } else {
                    if (sample < s_min) {
                        s_min = sample;
                    }
                    if (sample > s_max) {
                        s_max = sample;
                    }
                }
            }
        }

        level /= samples;

        soundLevel = (int)level;
        level_original = (int)level;
        soundSpan = (span_n > 0) ? (int)(s_max - s_min) : 0;

#if REPISA_MIC_SERIAL_PLOTTER && REPISA_MIC_PLOT_ONCE_PER_BUFFER
        ++s_plot_generation;
#endif

        if (s_mic_mtx != nullptr &&
            xSemaphoreTake(s_mic_mtx, 0) == pdTRUE) {

            size_t copy_n =
                (bytes_read < sizeof(s_shared))
                    ? bytes_read
                    : sizeof(s_shared);

            memcpy(s_shared, sBuffer, copy_n);

            s_shared_bytes = copy_n;

            xSemaphoreGive(s_mic_mtx);
        }

        vTaskDelay(1);
    }
}

// =====================================================
// STOP TASK
// =====================================================

static void repisaMicStopTask() {

    if (s_mic_task_handle != nullptr) {

        TaskHandle_t h = s_mic_task_handle;

        s_mic_task_handle = nullptr;

        vTaskDelete(h);
    }
}

// =====================================================
// HW DEINIT
// =====================================================

static void repisa_hw_deinit() {

    i2s_driver_uninstall(I2S_PORT);
}

// =====================================================
// HW INIT
// =====================================================

static esp_err_t repisa_hw_init(
    int pinDin,
    int pinBclk,
    int pinWs
) {

    i2s_config_t i2s_config = {

        .mode =
            (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),

        .sample_rate = 16000,

        .bits_per_sample =
            I2S_BITS_PER_SAMPLE_32BIT,

        .channel_format =
            I2S_CHANNEL_FMT_ONLY_LEFT,

        // IMPORTANTE:
        // ESTE ES EL QUE FUNCIONA EN EL C6
        .communication_format =
            I2S_COMM_FORMAT_STAND_I2S,

        .intr_alloc_flags =
            ESP_INTR_FLAG_LEVEL1,

        .dma_buf_count = 8,

        // EXACTAMENTE IGUAL AL SKETCH FUNCIONAL
        .dma_buf_len = 128,

        .use_apll = false,

        .tx_desc_auto_clear = false,

        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {

        .bck_io_num = pinBclk,

        .ws_io_num = pinWs,

        .data_out_num = I2S_PIN_NO_CHANGE,

        .data_in_num = pinDin
    };

    esp_err_t err;

    err = i2s_driver_install(
        I2S_PORT,
        &i2s_config,
        0,
        NULL
    );

    if (err != ESP_OK) {

        Serial.print("[MIC] install error: ");
        Serial.println(err);

        return err;
    }

    err = i2s_set_pin(
        I2S_PORT,
        &pin_config
    );

    if (err != ESP_OK) {

        Serial.print("[MIC] pin error: ");
        Serial.println(err);

        i2s_driver_uninstall(I2S_PORT);

        return err;
    }

    err = i2s_start(I2S_PORT);

    if (err != ESP_OK) {

        Serial.print("[MIC] start error: ");
        Serial.println(err);

        i2s_driver_uninstall(I2S_PORT);

        return err;
    }

    i2s_zero_dma_buffer(I2S_PORT);

    Serial.println("[MIC] INMP441 iniciado");

    return ESP_OK;
}

// =====================================================
// DEINIT
// =====================================================

void repisaMicDeinit() {

    repisaMicStopTask();

    if (s_driver_ok) {

        repisa_hw_deinit();

        s_driver_ok = false;
    }

    s_shared_bytes = 0;

    soundLevel = 0;
    soundSpan = 0;

#if REPISA_MIC_SERIAL_PLOTTER && REPISA_MIC_PLOT_ONCE_PER_BUFFER
    s_plot_generation = 0;
    s_last_plot_generation = 0;
#endif
}

// =====================================================
// INIT
// =====================================================

void repisaMicInitInmp441(
    int pinDin,
    int pinBclk,
    int pinWs
) {

    repisaMicDeinit();

    if (s_mic_mtx == nullptr) {

        s_mic_mtx = xSemaphoreCreateMutex();
    }

    s_threshold_cached = REPISA_MIC_THRESHOLD;

    Serial.printf(
        "[RepisaMic] INMP441 init DIN=%d BCLK=%d WS=%d\n",
        pinDin,
        pinBclk,
        pinWs
    );

    esp_err_t err =
        repisa_hw_init(
            pinDin,
            pinBclk,
            pinWs
        );

    if (err != ESP_OK) {

        Serial.printf(
            "[RepisaMic] init fallo: %s (%d)\n",
            esp_err_to_name(err),
            (int)err
        );

        return;
    }

    s_driver_ok = true;

#if REPISA_MIC_IS_C6_BUILD

    BaseType_t ok =
        xTaskCreate(
            repisaMicAudioTask,
            "inmp441_i2s",
            kMicTaskStackWords,
            nullptr,
            kMicTaskPrio,
            &s_mic_task_handle
        );

#else

    BaseType_t ok =
        xTaskCreatePinnedToCore(
            repisaMicAudioTask,
            "inmp441_i2s",
            kMicTaskStackWords,
            nullptr,
            kMicTaskPrio,
            &s_mic_task_handle,
            0
        );

#endif

    if (ok != pdPASS) {

        Serial.println(
            "[RepisaMic] ERROR creando tarea"
        );

        repisa_hw_deinit();

        s_driver_ok = false;

        return;
    }
}

// =====================================================
// READY
// =====================================================

bool repisaMicIsReady() {

    return s_driver_ok;
}

// =====================================================
// THRESHOLD
// =====================================================

void repisaMicSetThreshold(int threshold) {

    if (threshold < 0) {
        threshold = 0;
    }

    if (threshold > 500000) {
        threshold = 500000;
    }

    s_threshold_cached = threshold;
}

int repisaMicGetThreshold() {

    return s_threshold_cached;
}

// =====================================================
// READ BLOCK
// =====================================================

bool repisaMicReadBlockInt32(
    int32_t *dst,
    int count
) {

    if (!dst ||
        count <= 0 ||
        !repisaMicIsReady() ||
        s_mic_mtx == nullptr) {

        return false;
    }

    if (count > 64) {
        count = 64;
    }

    size_t want =
        (size_t)count * sizeof(int32_t);

    if (xSemaphoreTake(s_mic_mtx, 0) != pdTRUE) {

        return false;
    }

    if (s_shared_bytes < want) {

        xSemaphoreGive(s_mic_mtx);

        return false;
    }

    memcpy(dst, s_shared, want);

    xSemaphoreGive(s_mic_mtx);

    return true;
}

// =====================================================
// SERIAL PLOTTER
// =====================================================

void repisaMicSerialPlotterTick() {

#if REPISA_MIC_SERIAL_PLOTTER

    if (!repisaMicIsReady()) {

        Serial.println(-1);

        return;
    }

#if REPISA_MIC_PLOT_ONCE_PER_BUFFER

    uint32_t gen = s_plot_generation;

    if (gen == s_last_plot_generation) {
        return;
    }

    s_last_plot_generation = gen;

#endif

    /* Serial Plotter: 4 trazas — cero, techo ref, |PCM| medio, span bloque (max−min). */
  /*  Serial.print("piso:0,techo:5000,soundLevel:");
    Serial.print(soundLevel);
    Serial.print(",soundSpan:");
    Serial.println(soundSpan);*/
#endif
}

#endif