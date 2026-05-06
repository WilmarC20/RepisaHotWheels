#include "HardwareSerial.h"
#include <Arduino.h>
#include "IRControlLite.h"
#include "driver/rmt_rx.h"

extern void IRControlLite_inicio(int valor);
extern void IRControlLite_presionado(int valor);
extern void IRControlLite_solto(int valor);

#define BUFFER_SIZE 1024

// RMT
rmt_channel_handle_t rx_channel = NULL;
rmt_receive_config_t rx_config;
uint8_t raw_buffer[BUFFER_SIZE];

volatile bool data_ready = false;
size_t received_size = 0;

// Estado (igual que antes)
bool boton_presionado = false;

// Callback RMT
bool rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_ctx) {
  received_size = edata->num_symbols * sizeof(rmt_symbol_word_t);
  data_ready = true;
  return false;
}

uint32_t decodeNEC(rmt_symbol_word_t *symbols, int count) {

  // NEC válido suele tener entre 32 y 36 símbolos
  if (count < 30) return 0;

  // 🔥 Validar cabecera (9ms HIGH + 4.5ms LOW)
  if (symbols[0].duration0 < 8000 || symbols[0].duration1 < 4000) {
    return 0;
  }

  uint32_t data = 0;

  // 🔥 Leer bits (más tolerante)
  int bits = 0;

  for (int i = 1; i < count && bits < 32; i++) {

    uint32_t low = symbols[i].duration1;

    if (low > 1000) {
      data |= (1UL << bits);
    }

    bits++;
  }

  // 🔥 Validar que sí leímos suficientes bits
  if (bits < 32) return 0;

  // 🔥 Extraer comando
  uint8_t command = (data >> 16) & 0xFF;
  uint8_t inv_command = (data >> 24) & 0xFF;

  // 🔥 Validación NEC (comando y su inverso)
  if ((command ^ inv_command) != 0xFF) {
    return 0;
  }
  
  //command = (command>=27?command-27:command+5);
  //return command>=24?command-4:command;
  return command+1;
}

/* NEC repeat frame: cabecera 9ms + 2.25ms y un burst corto final.
 * No trae comando nuevo; indica "sigo presionando el último botón". */
static bool isNECRepeat(rmt_symbol_word_t *symbols, int count) {
  if (count < 2) return false;
  const uint32_t h0 = symbols[0].duration0;
  const uint32_t h1 = symbols[0].duration1;
  if (h0 < 8000 || h0 > 10000) return false;
  if (h1 < 1800 || h1 > 3000) return false;
  return true;
}

// -------------------------

IRControlLite::IRControlLite(int pin) : pin(pin) {}

void IRControlLite::iniciar() {

  pinMode(pin, INPUT);

  // Config RMT
  rmt_rx_channel_config_t rx_chan_config = {
      .gpio_num = (gpio_num_t)pin,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 1000000,
      .mem_block_symbols = 64,
      .flags = {
          .invert_in = false,
          .with_dma = false,
      }
  };

  ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_channel));

  rmt_rx_event_callbacks_t cbs = {
      .on_recv_done = rx_done_callback,
  };
  ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, NULL));

  rx_config.signal_range_min_ns = 1000;
  rx_config.signal_range_max_ns = 10000000;

  ESP_ERROR_CHECK(rmt_enable(rx_channel));

  // iniciar recepción
  ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_buffer, BUFFER_SIZE, &rx_config));
}

// -------------------------

uint32_t IRControlLite::procesar() {

  uint32_t retorno = -1;

  if (data_ready) {

    rmt_symbol_word_t *symbols = (rmt_symbol_word_t *)raw_buffer;
    int num = received_size / sizeof(rmt_symbol_word_t);

    uint32_t comando = decodeNEC(symbols, num);
    const bool repeatFrame = (comando == 0) && isNECRepeat(symbols, num);

    if (comando != 0 || (repeatFrame && boton_presionado)) {

      if (millis() - ultimoTiempo > t_pulsacion) {

        if (comando != 0) {
          retorno = comando - 1;
          ultimoCodigo = retorno;
        } else {
          // Frame de repetición NEC: conservar el último comando válido.
          retorno = ultimoCodigo;
        }

        if (!boton_presionado) {
          //Serial.println("++++++++++++++++++++++++++++++++++++++");
          //Serial.print("Inicio Pulsacion ");
          IRControlLite_inicio(retorno);
        } else {
          IRControlLite_presionado(retorno);
          //Serial.print("Repite Pulsacion ");
        }

//        Serial.println((int)retorno);

        boton_presionado = true;
        ultimoTiempo = millis();
      }

      ultimoTiempoGral = millis();
    }

    data_ready = false;

    delay(1); // evita watchdog

    // reiniciar recepción
    ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_buffer, BUFFER_SIZE, &rx_config));
  }

  else if (boton_presionado && millis() - ultimoTiempoGral > 200) {

    boton_presionado = false;
    IRControlLite_solto(ultimoCodigo);

    /*Serial.print("-----------------------Solto Boton ");
    Serial.print((int)ultimoCodigo);
    Serial.println(" --------------------------------");*/
  }

  return retorno;
}

// -------------------------

uint32_t IRControlLite::getUltimoComando() {
  return ultimoCodigo;
}