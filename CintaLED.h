#ifndef CINTA_LED_H
#define CINTA_LED_H



#include <WS2812FX.h>
#include "CustomEffects.h"
#include "config.h"

#ifndef TIMER_MS
#define TIMER_MS ((int)REPISA_CINTA_AUTO_MODE_MS)
#endif

#include <Preferences.h>

struct value_t {
    bool b_val;
    int i_val;
    float f_val;
    const char *s_val;  // Usamos puntero a cadena C
} ;

class CintaLED {

public:
value_t valueT;
  const uint32_t* colores = nullptr;
  int n_colores;
  WS2812FX *ws2812fx;
  CintaLED(int LPIN,int NLEDS,std::function<void(String, const value_t &)> callback = nullptr,int tipo_led=NEO_GRB);

  char* strdup_P(const char* progmem_str);
  void iniciar(bool encender=false,int Brillo=255,int Color=0x007BFF,String Efectos="Static");
  void procesar();
  void encender();
  void apagar();
  uint16_t rgbToHue(uint8_t r, uint8_t g, uint8_t b);
  uint32_t color(int pos=-1,int modo=-1);
  uint32_t color(int r, int g, int b,int modo=-1);
  uint32_t color(uint32_t color,int modo=-1);
  int buscar_modo(const char* NombreModo);
  int modoSig();
  int modoAnt();
  int modo(int pos=-1,bool automatico=false);
  int modo(const char * Modo);
  int brilloMas(int brillo);
  int  brillo(int brill);
  /** Pin ADC (≥0) o REPISA_MIC_PIN_I2S (ESP32, INMP441 por I²S legacy). -1 desactiva. */
  void setPinSonidoAnalogo(int pin);
  /** Si true, en procesar() el brillo sigue el nivel del micrófono (modo “música”). */
  void setReaccionSonido(bool activo);
  bool getReaccionSonido() const { return reaccion_sonido; }
  int modo_act=FX_MODE_STATIC;
  const char** modos;
  unsigned int n_modos=0;
private:
  int LED_PIN;
  int NUM_LEDS;

  std::function<void(String, const value_t &)> onEstadoActualizado;
  unsigned long last_change = 0;
  unsigned long now = 0;
  int color_act = -1;
  bool modo_auto = false;

  int sound_ao_pin = -1;
  bool reaccion_sonido = false;
  unsigned long last_sound_ms = 0;
  float sonido_baseline = 0;
  float sonido_envelope = 0;
  void aplicarBrilloPorSonido();
/**/
};

#endif
