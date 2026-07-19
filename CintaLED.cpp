#include "Arduino.h"
#include "CintaLED.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "RepisaMic.h"
#endif

CintaLED::CintaLED(int LPIN,int NLEDS,std::function<void(String, const value_t &)> callback,int tipo_led)
{
  NUM_LEDS=NLEDS;
  LED_PIN=LPIN;
  if(callback != nullptr)
    onEstadoActualizado = callback;
  else
    onEstadoActualizado=[](String, const value_t &) {};
  ws2812fx = new WS2812FX(NUM_LEDS, LED_PIN, tipo_led/*NEO_BGR NEO_RGB*/ + NEO_KHZ800);//NEO_BGR,NEO_RBG,NEO_RBG,NEO_GRB,NEO_GBR
  /* Los callbacks usan CustomEffects::_fx; debe existir CustomEffects::begin(ws2812fx). No registrar aquí
     con setCustomMode sin begin: _fx queda nullptr y el programa se cae al ejecutar el efecto. */
  CustomEffects::begin(ws2812fx);

  static const uint32_t listaColores[] = 
  {
    RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE,
    0xFFA500, 0x800080, 0x00FF7F, 0xFF1493,
    0x1E90FF, 0xA52A2A, 0x008080, 0xF0E68C,
    0xFFD700, 0x00CED1, 0xDC143C, 0x4B0082
  };
  n_colores=(sizeof(listaColores) / sizeof(listaColores[0]));
  colores = listaColores;

  n_modos = ws2812fx->getModeCount();
  modos = new const char*[n_modos]; 
  for (int i = 0; i < n_modos; i++) 
  {
    const __FlashStringHelper *fsh = ws2812fx->getModeName(i);
    modos[i] = strdup_P(reinterpret_cast<const char *>(fsh));  // duplica desde PROGMEM
  }
}
void CintaLED::iniciar(bool encender,int Brillo,int Color,String Efectos) {
  ws2812fx->init();
  ws2812fx->setBrightness(Brillo);
  ws2812fx->setSpeed(100);
  ws2812fx->setColor(Color);
  ws2812fx->setMode(buscar_modo(Efectos.c_str()));
  if(encender)
    ws2812fx->start();
}

void CintaLED::setPinSonidoAnalogo(int pin) {
   sound_ao_pin = pin;
   CustomEffects::setMicPin(pin);
#if defined(ARDUINO_ARCH_ESP32)
   if (pin == REPISA_MIC_PIN_I2S) {
      /* Nivel = misma métrica que el sketch (soundLevel); sin pseudo-ADC 12 bits. */
      sonido_baseline = 0.f;
      sonido_envelope = 0;
      return;
   }
#endif
   if (pin >= 0) {
#if defined(ARDUINO_ARCH_ESP32)
      analogReadResolution(12);
      analogSetPinAttenuation((uint8_t)pin, ADC_11db);
      /* Pull-down reduce “falso audio” con AO desconectado. En ESP32 clásico 34–39 no hay pull interno. */
      if (pin >= 34 && pin <= 39) {
         pinMode(pin, INPUT);
      } else {
         pinMode(pin, INPUT_PULLDOWN);
      }
#else
      pinMode(pin, INPUT);
#endif
      const int a0 = analogRead(pin);
      sonido_baseline = (float)a0;
      sonido_envelope = 0;
   }
}

void CintaLED::setReaccionSonido(bool activo) {
#if defined(ARDUINO_ARCH_ESP32)
   reaccion_sonido = activo && (sound_ao_pin >= 0 || sound_ao_pin == REPISA_MIC_PIN_I2S);
#else
   reaccion_sonido = activo && sound_ao_pin >= 0;
#endif
   if (sound_ao_pin >= 0) {
      sonido_baseline = (float)analogRead(sound_ao_pin);
   }
#if defined(ARDUINO_ARCH_ESP32)
   else if (sound_ao_pin == REPISA_MIC_PIN_I2S) {
      sonido_baseline = 0.f;
   }
#endif
   sonido_envelope = 0;
   last_sound_ms = 0;
}

void CintaLED::aplicarBrilloPorSonido() {
#if defined(ARDUINO_ARCH_ESP32)
   const bool pin_ok = (sound_ao_pin >= 0) || (sound_ao_pin == REPISA_MIC_PIN_I2S);
#else
   const bool pin_ok = sound_ao_pin >= 0;
#endif
   if (!pin_ok || !reaccion_sonido) {
      return;
   }
   const unsigned long ahora = millis();
   if (ahora - last_sound_ms < 10U) {
      return;
   }
   last_sound_ms = ahora;

#if defined(ARDUINO_ARCH_ESP32)
   const int raw = (sound_ao_pin == REPISA_MIC_PIN_I2S) ? (int)soundLevel : analogRead(sound_ao_pin);
#else
   const int raw = analogRead(sound_ao_pin);
#endif
   if (sonido_baseline <= 1.0f) {
      sonido_baseline = (float)raw;
   } else {
      sonido_baseline = sonido_baseline * 0.985f + (float)raw * 0.015f;
   }
   const float diff = (float)raw - sonido_baseline;
   float dev = diff >= 0.0f ? diff : -diff;
   const int dead = CustomEffects::micEffectiveDeadband();
   if (dev < (float)dead) {
      dev = 0.0f;
      sonido_envelope *= 0.92f;
   }
   sonido_envelope = sonido_envelope * 0.62f + dev * 0.38f;

   const float envGain = CustomEffects::micBrightnessEnvGain();
   const int b = (int)constrain(10.0f + sonido_envelope * envGain, 8.0f, 255.0f);

   ws2812fx->setBrightness((uint8_t)b);
   ws2812fx->service();
   yield();
   /* No llamar onEstadoActualizado aquí: iría a Preferences/RainMaker a cada ráfaga y desgasta flash. */
}

void CintaLED::procesar() {
  if(ws2812fx->isRunning())
  {
    if (reaccion_sonido) {
       aplicarBrilloPorSonido();
    }
    if(modo_auto)
    {
      now = millis();
      if(now - last_change > TIMER_MS) 
      {
        modo((ws2812fx->getMode() + 1) % ws2812fx->getModeCount(),true);
        Serial.printf("Modo: %d\n",modo_act);
        last_change = now;
      }
    }
    ws2812fx->service();
    yield();
  }
}

void CintaLED::encender()
{
  if(ws2812fx->isRunning())
    return;
  ws2812fx->start();   
  ws2812fx->service();
  onEstadoActualizado("Encender", { .b_val = true });
  
}

void CintaLED::apagar() {
  if(!ws2812fx->isRunning())
    return;
  ws2812fx->stop();      
  ws2812fx->service();   
  onEstadoActualizado("Encender", { .b_val = false });
}

int CintaLED::brilloMas(int brill) 
{
  brill=brillo(ws2812fx->getBrightness()+brill);
  return brill;
}

int CintaLED::brillo(int brill) 
{
  brill = constrain(brill, 1, 255);
  if(abs(ws2812fx->getBrightness()-brill) <= 1 )
    return brill;
  ws2812fx->setBrightness(brill);
  ws2812fx->service();
  encender();
  onEstadoActualizado("Brillo", { .i_val = brill });
  return brill;
}
int CintaLED::buscar_modo(const char* NombreModo)
{
  /* modos[] ya se duplicó desde PROGMEM en el constructor; no volver a strdup_P
     aquí (cada llamada perdía un malloc por modo y fragmentaba el heap). */
  for (int i = 0; i < n_modos; i++)
    if (strcmp(NombreModo, modos[i]) == 0)
      return i;
  return 0;
}
int CintaLED::modo(const char* NombreModo)
{
  modo(buscar_modo(NombreModo));
  encender();
  return ws2812fx->getMode();
}

int CintaLED::modo(int pos,bool automatico)
{
  modo_auto=automatico;
  if(ws2812fx->getMode() == pos)
    return pos;
  Serial.printf("\nModo: %d\n",pos);
  
  /* Incluye el último índice: los modos custom de WS2812FX suelen ir al final (antes pos<n_modos-1 los ignoraba). */
  if (pos >= 0 && pos < n_modos) {
    ws2812fx->setMode(pos);
  }
  ws2812fx->service();
  encender();
  modo_act=ws2812fx->getMode();
  onEstadoActualizado("Efectos", { .i_val = modo_act });
  return modo_act;
}
int CintaLED::modoSig()
{
  int _modo=(modo_act + 1) % n_modos;
  modo(_modo);
  return _modo;
}
int CintaLED::modoAnt()
{
  int _modo=(modo_act==0 ) ?(n_modos-1) : ((modo_act - 1) % n_modos);
  modo(_modo);
  return _modo;
}

uint16_t CintaLED::rgbToHue(uint8_t r, uint8_t g, uint8_t b) 
{
  float rf = r / 255.0;
  float gf = g / 255.0;
  float bf = b / 255.0;

  float maxVal = max(rf, max(gf, bf));
  float minVal = min(rf, min(gf, bf));
  float delta = maxVal - minVal;

  float hue = 0.0;

  if (delta == 0) {
    hue = 0;
  } else if (maxVal == rf) {
    hue = 60 * fmod(((gf - bf) / delta), 6.0);
  } else if (maxVal == gf) {
    hue = 60 * (((bf - rf) / delta) + 2);
  } else if (maxVal == bf) {
    hue = 60 * (((rf - gf) / delta) + 4);
  }

  if (hue < 0) hue += 360;

  return (uint16_t)hue;
}



uint32_t CintaLED::color(int pos,int _modo) 
{
  Serial.println("color(int pos,int _modo)");
  return color(colores[color_act = pos== -1?(color_act + 1) % n_colores:pos],_modo);
}

uint32_t CintaLED::color(int r, int g, int b,int _modo) 
{
  Serial.println("color(int r, int g, int b,int _modo) ");
  return color(ws2812fx->Color(r, g, b),_modo);
}
uint32_t CintaLED::color(uint32_t color,int _modo)
{
    Serial.println("color(uint32_t color,int _modo)");

  /* Solo salir temprano si tampoco hay cambio de modo pendiente: con un efecto
     activo y el mismo color pedido, igual hay que poder pasar a _modo (p. ej. Static). */
  if(color == ws2812fx->getColor() && (_modo == -1 || ws2812fx->getMode() == (uint8_t)_modo))
    return color;
  ws2812fx->setColor(color);
  ws2812fx->service();
  encender();
  if(_modo != -1)
   modo(_modo);
  color=ws2812fx->getColor();
  onEstadoActualizado("Color", { .i_val = rgbToHue((color >> 16) & 0xFF,(color >> 8) & 0xFF,color & 0xFF) });
  return color;

}
char* CintaLED::strdup_P(const char* progmem_str) 
{
  size_t len = strlen_P(progmem_str) + 1;  // +1 para el null terminator
  char* ram_str = (char*)malloc(len);
  if (ram_str) {
    strcpy_P(ram_str, progmem_str);
  }
  return ram_str;
}
