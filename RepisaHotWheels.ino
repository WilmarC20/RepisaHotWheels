// ESP RainMaker + CintaLED + IRControlLite (provisión como ejemplo que te funciona en tu core)
#include "config.h"
#include "RMaker.h"
#include "WiFi.h"
#include "WiFiProv.h"
#include "IRControlLite.h"
#include "CintaLED.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "RepisaMic.h"
#endif
#include <Preferences.h>
#include <esp_event.h>
#include <esp_system.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_common_events.h>
#include <esp_rmaker_standard_types.h>
#include <cstdlib>
#include <cstring>
#include <HTTPClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** Versión: ver `REPISA_FW_VERSION` en config.h. */

extern Device *luz_led;

static volatile bool g_repisa_git_check_busy = false;

static bool repisaJsonExtractString(const String &json, const char *key, String &out) {
   const String pat = String("\"") + key + "\"";
   const int keyPos = json.indexOf(pat);
   if (keyPos < 0) {
      return false;
   }
   const int colon = json.indexOf(':', keyPos + pat.length());
   if (colon < 0) {
      return false;
   }
   const int q1 = json.indexOf('"', colon);
   if (q1 < 0) {
      return false;
   }
   const int q2 = json.indexOf('"', q1 + 1);
   if (q2 < 0) {
      return false;
   }
   out = json.substring(q1 + 1, q2);
   return true;
}

static void repisaGitCheckTask(void * /*pvParameters*/) {
   char line[256];

   if (luz_led == nullptr) {
      g_repisa_git_check_busy = false;
      vTaskDelete(nullptr);
      return;
   }

   if (WiFi.status() != WL_CONNECTED) {
      snprintf(line, sizeof(line), "Sin WiFi (estado %d)", (int)WiFi.status());
      line[sizeof(line) - 1] = '\0';
      luz_led->updateAndReportParam(REPISA_RM_PARAM_INFO, line);
      g_repisa_git_check_busy = false;
      vTaskDelete(nullptr);
      return;
   }

   HTTPClient http;
   http.setTimeout(20000);
   http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
   http.addHeader("User-Agent", "RepisaHotWheels/1");
   http.begin(REPISA_FW_JSON_URL);
   const int code = http.GET();
   if (code != HTTP_CODE_OK) {
      snprintf(line, sizeof(line), "HTTP %d al leer JSON", code);
      line[sizeof(line) - 1] = '\0';
      luz_led->updateAndReportParam(REPISA_RM_PARAM_INFO, line);
      http.end();
      g_repisa_git_check_busy = false;
      vTaskDelete(nullptr);
      return;
   }

   const String payload = http.getString();
   http.end();

   String srvVer;
   String url;
   String desc;
   String fecha;
   if (!repisaJsonExtractString(payload, "version", srvVer) || !repisaJsonExtractString(payload, "url", url)) {
      luz_led->updateAndReportParam(REPISA_RM_PARAM_INFO, "JSON: falta version o url");
      g_repisa_git_check_busy = false;
      vTaskDelete(nullptr);
      return;
   }
   (void)url; /* reservado para futura descarga OTA por URL */
   repisaJsonExtractString(payload, "description", desc);
   repisaJsonExtractString(payload, "date", fecha);
   if (desc.length() > 72) {
      desc = desc.substring(0, 72);
   }

   const bool hayNueva = (srvVer != String(REPISA_FW_VERSION));
   const char *estado = hayNueva ? "Hay nueva" : "Al dia";

   if (fecha.length() > 0 && desc.length() > 0) {
      snprintf(line, sizeof(line), "%s | Srv:%s Loc:%s | %s | %s", estado, srvVer.c_str(), REPISA_FW_VERSION, fecha.c_str(),
               desc.c_str());
   } else if (desc.length() > 0) {
      snprintf(line, sizeof(line), "%s | Srv:%s Loc:%s | %s", estado, srvVer.c_str(), REPISA_FW_VERSION, desc.c_str());
   } else {
      snprintf(line, sizeof(line), "%s | Srv:%s Loc:%s | URL ok", estado, srvVer.c_str(), REPISA_FW_VERSION);
   }
   line[sizeof(line) - 1] = '\0';
   luz_led->updateAndReportParam(REPISA_RM_PARAM_INFO, line);

   g_repisa_git_check_busy = false;
   vTaskDelete(nullptr);
}

/* No está en los headers públicos; misma ruta que usa el broker MQTT al aplicar parámetros remotos. */
extern "C" esp_err_t esp_rmaker_handle_set_params(char *data, size_t data_len, esp_rmaker_req_src_t src);

static constexpr const char *kRepisaRmPrefsNs = "repisa_rm";
/** Tras publicar una vez las escenas por defecto en RainMaker, se guarda en NVS para no duplicar al reconectar. */
static constexpr const char *kRepisaRmPrefScenesSeed = "scenes_seed";

static void repisaLimpiarFlagEscenasRainMakerSeed();

/** Saturación: `REPISA_COLOR_SATURATION_BYTE` en config.h. */

/** La nube puede enviar entero, float o cadena (p. ej. Google Home / rutinas). */
static int repisaIntFromCloudVal(const param_val_t &v, int fallback) {
   if (v.type == RMAKER_VAL_TYPE_INTEGER) {
      return v.val.i;
   }
   if (v.type == RMAKER_VAL_TYPE_FLOAT) {
      return (int)((double)v.val.f + 0.5);
   }
   if (v.type == RMAKER_VAL_TYPE_STRING && v.val.s) {
      char *end = nullptr;
      const long n = strtol(v.val.s, &end, 10);
      if (end != v.val.s) {
         return (int)n;
      }
   }
   return fallback;
}

static bool repisaIsBrightnessParam(const char *name) {
   return name && (strcmp(name, "Brillo") == 0 || strcmp(name, "Brightness") == 0);
}

/** Nombre visible o tipo estándar esp.param.brightness (asistentes / Matter). */
static bool repisaParamIsBrightness(Param *param) {
   if (!param) {
      return false;
   }
   if (repisaIsBrightnessParam(param->getParamName())) {
      return true;
   }
   const param_handle_t *ph = param->getParamHandle();
   if (!ph) {
      return false;
   }
   char *ty = esp_rmaker_param_get_type(ph);
   return ty && strcmp(ty, ESP_RMAKER_PARAM_BRIGHTNESS) == 0;
}

static bool repisaIsPowerParam(const char *name) {
   return name && (strcmp(name, "Encender") == 0 || strcmp(name, "Power") == 0);
}

#if CONFIG_IDF_TARGET_ESP32S2
#define RMAKER_USE_SOFTAP_PROVISION 1
#else
#define RMAKER_USE_SOFTAP_PROVISION 0
#endif

#if !RMAKER_USE_SOFTAP_PROVISION
/* Tu ejemplo usa FREE_BTDM en ESP32 clásico; en C3/C6/S3 usar FREE_BLE. */
#if CONFIG_IDF_TARGET_ESP32
#define RMAKER_PROV_BLE_HANDLER NETWORK_PROV_SCHEME_HANDLER_FREE_BTDM
#else
#define RMAKER_PROV_BLE_HANDLER NETWORK_PROV_SCHEME_HANDLER_FREE_BLE
#endif
#endif

const char *service_name = REPISA_PROVISION_SERVICE_NAME;
const char *pop = REPISA_PROVISION_POP;

Preferences *prefs = nullptr;

/* `prefs` se usa desde loop(), el callback de RainMaker y el event loop de esp_event
 * (tareas distintas); sin mutex, un begin() puede pisar el handle de otro en curso.
 * Se crea en setup() antes de que exista cualquier otra tarea. */
static SemaphoreHandle_t s_prefs_mutex = nullptr;
static void prefsLock() {
   if (s_prefs_mutex == nullptr) {
      s_prefs_mutex = xSemaphoreCreateMutex();
   }
   xSemaphoreTake(s_prefs_mutex, portMAX_DELAY);
   if (prefs == nullptr) {
      prefs = new Preferences();
   }
}
static void prefsUnlock() {
   xSemaphoreGive(s_prefs_mutex);
}

IRControlLite *irControl = nullptr;
CintaLED *cinta = nullptr;

volatile bool conectado_rainmaker = false;

/* IR no puede arrancar durante provisión (rompe BLE/WiFi). Fuera de eso, IR es independiente de WiFi/RainMaker. */
volatile bool g_provision_en_curso = false;
volatile uint32_t g_setup_done_ms = 0;
volatile uint32_t g_prov_inicio_ms = 0;

struct Config {
   int Brillo;
   uint32_t Color;
   String Efectos;
   bool Encender;
   int SensMic;
} config;

Device *luz_led = nullptr; /* definición; declaración extern arriba para repisaGitCheckTask */

struct AccionesIR {
   const char *titulo;
   void (*inicio)();
   void (*presionado)();
   void (*solto)();
};

const AccionesIR AccionIR[] PROGMEM = {
   {"Brillo+", [] { cinta->brilloMas(+20); }, [] { cinta->brilloMas(10); }, nullptr},
   {"Brillo-", [] { cinta->brilloMas(-20); }, [] { cinta->brilloMas(-10); }, nullptr},
   {"ON", [] { cinta->setReaccionSonido(false);cinta->encender(); }, nullptr, nullptr},
   {"OFF", [] { cinta->setReaccionSonido(false);cinta->apagar(); }, nullptr, nullptr},

   {"R", [] { cinta->setReaccionSonido(false);cinta->color(255, 0, 0, FX_MODE_STATIC); }, nullptr, nullptr},
   {"G", [] { cinta->setReaccionSonido(false);cinta->color(0, 255, 0, FX_MODE_STATIC); }, nullptr, nullptr},
   {"B", [] { cinta->setReaccionSonido(false);cinta->color(0, 0, 255, FX_MODE_STATIC); }, nullptr, nullptr},
   {"BLANCO", [] { cinta->setReaccionSonido(false);cinta->color(255, 255, 255, FX_MODE_STATIC); }, nullptr, nullptr},

   {"NARANJA", [] { cinta->setReaccionSonido(false);cinta->color(255, 165, 0, FX_MODE_STATIC); }, nullptr, nullptr},
   {"AMARILLO", [] { cinta->setReaccionSonido(false);cinta->color(255, 255, 0, FX_MODE_STATIC); }, nullptr, nullptr},
   {"CYAN", [] { cinta->setReaccionSonido(false);cinta->color(0, 255, 255, FX_MODE_STATIC); }, nullptr, nullptr},
   {"PURPURA", [] { cinta->setReaccionSonido(false);cinta->color(128, 0, 128, FX_MODE_STATIC); }, nullptr, nullptr},

   {"JUMP3", [] { cinta->setReaccionSonido(true);cinta->modo(CustomEffects::getHotWheels6x8()); }, nullptr, nullptr},
   {"RELOJ", [] { cinta->setReaccionSonido(true);cinta->modo(CustomEffects::getJump7()); }, nullptr, nullptr},
   {"FADE3", [] { cinta->setReaccionSonido(false);cinta->modo(CustomEffects::getFade3()); }, nullptr, nullptr},
   {"FADE7", [] { cinta->setReaccionSonido(true);cinta->modo(CustomEffects::getFade7()); }, nullptr, nullptr},

   {"M1", [] { cinta->setReaccionSonido(false); cinta->modo(CustomEffects::getSpectrum6x8()); cinta->encender(); }, nullptr, nullptr},
   {"M2", [] { cinta->setReaccionSonido(false); cinta->modo(CustomEffects::getSoundBright6x8()); cinta->encender(); }, nullptr, nullptr},
   {"M3", [] { cinta->setReaccionSonido(false); cinta->modo(CustomEffects::getSoundHue6x8()); cinta->encender(); }, nullptr, nullptr},
   {"M4", [] { cinta->setReaccionSonido(false); cinta->modo(CustomEffects::getImpact6x8()); cinta->encender(); }, nullptr, nullptr},
   {"M5", [] { cinta->modo(-1, true); }, nullptr, nullptr},

};

int numOpcionesControl = sizeof(AccionIR) / sizeof(AccionIR[0]);

void LeerConfig() {
   prefsLock();
   prefs->begin("config", true);
   config.Brillo = 255;
   config.Color = prefs->getInt("Color", 13529899);
   config.Efectos = prefs->getString("Efectos", "Static");
   config.Encender = prefs->getBool("Encender", false);
   config.SensMic = prefs->getInt("SensMic", 62);
   prefs->end();
   prefsUnlock();
}

/* Debounce de NVS: los cambios se acumulan en `config` (bajo el mutex de prefs) y se
 * escriben todos juntos tras REPISA_NVS_SAVE_DEBOUNCE_MS sin cambios nuevos. Antes,
 * arrastrar un slider en la app generaba una escritura de flash por cada valor. */
static volatile bool s_config_pendiente = false;
static uint32_t s_config_ultimo_cambio_ms = 0;

static void marcarConfigPendiente() {
   s_config_ultimo_cambio_ms = millis();
   s_config_pendiente = true;
}

void GuardarConfigInt(String label, int valor) {
   prefsLock();
   if (label == "Color") {
      config.Color = (uint32_t)valor;
   } else if (label == "Brillo") {
      config.Brillo = valor;
   } else if (label == "SensMic") {
      config.SensMic = valor;
   }
   marcarConfigPendiente();
   prefsUnlock();
}
void GuardarConfigStr(String label, String valor) {
   prefsLock();
   if (label == "Efectos") {
      config.Efectos = valor;
   }
   marcarConfigPendiente();
   prefsUnlock();
}
void GuardarConfigBool(String label, bool valor) {
   prefsLock();
   if (label == "Encender") {
      config.Encender = valor;
   }
   marcarConfigPendiente();
   prefsUnlock();
}

/** Escribe YA la config pendiente sin esperar el debounce (usar antes de un reinicio). */
void repisaFlushConfigAhora() {
   if (!s_config_pendiente) {
      return;
   }
   prefsLock();
   s_config_pendiente = false;
   prefs->begin("config", false);
   prefs->putInt("Color", (int)config.Color);
   prefs->putInt("Brillo", config.Brillo);
   prefs->putInt("SensMic", config.SensMic);
   prefs->putString("Efectos", config.Efectos.c_str());
   prefs->putBool("Encender", config.Encender);
   prefs->end();
   prefsUnlock();
}

/** Llamar desde loop(): escribe la config a NVS si hay cambios y ya pasó el debounce. */
void repisaFlushConfigSiPendiente() {
   if (!s_config_pendiente || (millis() - s_config_ultimo_cambio_ms) < REPISA_NVS_SAVE_DEBOUNCE_MS) {
      return;
   }
   repisaFlushConfigAhora();
}

/** Cada encendido “físico” suma 1; si llega a REPISA_FACTORY_RESET_POWER_CYCLES → RMakerFactoryReset.
 * Si la repisa queda encendida más de REPISA_FACTORY_CANCEL_STABLE_MS, el contador vuelve a 0 (cancelar). */
static void repisaEvaluarResetPorCiclosAlimentacion() {
   const esp_reset_reason_t rr = esp_reset_reason();
   const bool esEncendidoPorCorriente =
       (rr == ESP_RST_POWERON || rr == ESP_RST_EXT || rr == ESP_RST_BROWNOUT
#if defined(ESP_RST_USB)
        || rr == ESP_RST_USB
#endif
       );
   if (!esEncendidoPorCorriente) {
      return;
   }
   prefsLock();
   prefs->begin("hw_rst", false);
   unsigned seq = (unsigned)prefs->getUChar("pwrcyc", 0) + 1U;
   if (seq > 250U) {
      seq = 1U;
   }
   prefs->putUChar("pwrcyc", (uint8_t)seq);
   prefs->end();
   prefsUnlock();

   Serial.printf(
       "[Repisa] Ciclo rapido de alimentacion: %u de %u "
       "(reinicio fabrica al llegar; o deja encendida %lu s para cancelar)\n",
       seq,
       (unsigned)REPISA_FACTORY_RESET_POWER_CYCLES,
       (unsigned long)(REPISA_FACTORY_CANCEL_STABLE_MS / 1000UL));

   if (seq >= (unsigned)REPISA_FACTORY_RESET_POWER_CYCLES) {
      prefsLock();
      prefs->begin("hw_rst", false);
      prefs->putUChar("pwrcyc", 0);
      prefs->end();
      prefsUnlock();
      Serial.println("[Repisa] Aplicando reinicio de fabrica RainMaker...");
      Serial.flush();
      delay(800);
      repisaLimpiarFlagEscenasRainMakerSeed();
      RMakerFactoryReset(2);
   }
}

static void repisaCancelarContadorSiEncendidoEstable() {
   static bool hecho = false;
   if (hecho || millis() < REPISA_FACTORY_CANCEL_STABLE_MS) {
      return;
   }
   hecho = true;
   prefsLock();
   prefs->begin("hw_rst", false);
   prefs->putUChar("pwrcyc", 0);
   prefs->end();
   prefsUnlock();
}

void sysProvEvent(arduino_event_t *sys_event) {
   switch (sys_event->event_id) {
   case ARDUINO_EVENT_PROV_START:
      g_provision_en_curso = true;
      if (g_prov_inicio_ms == 0) {
         g_prov_inicio_ms = millis();
      }
#if RMAKER_USE_SOFTAP_PROVISION
      Serial.printf("\nProvisioning Started with name \"%s\" and PoP \"%s\" on SoftAP\n", service_name, pop);
      WiFiProv.printQR(service_name, pop, "softap");
#else
      Serial.printf("\nProvisioning Started with name \"%s\" and PoP \"%s\" on BLE\n", service_name, pop);
      WiFiProv.printQR(service_name, pop, "ble");
#endif
      break;
   case ARDUINO_EVENT_PROV_INIT:
      /* No marcar g_provision_en_curso aquí: en equipos ya provisionados PROV_INIT puede dispararse sin
         PROV_CRED_SUCCESS y el IR quedaría bloqueado para siempre. La sesión real se marca en PROV_START. */
      WiFiProv.disableAutoStop(10000);
      break;
   case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      WiFiProv.endProvision();
      g_provision_en_curso = false;
      g_prov_inicio_ms = 0;
      break;
   case ARDUINO_EVENT_PROV_CRED_FAIL:
      g_provision_en_curso = false;
      g_prov_inicio_ms = 0;
      break;
   case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      /* Ya hay IP en STA: no estamos en el handshake crítico de provisión BLE. */
      g_provision_en_curso = false;
      g_prov_inicio_ms = 0;
      Serial.print("\nConectado a la IP: ");
      Serial.println(IPAddress(sys_event->event_info.got_ip.ip_info.ip.addr));
      conectado_rainmaker = true;
      break;
   case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi STA desconectado");
      conectado_rainmaker = false;
      break;
   default:
      break;
   }
}

/** Payload: servicio "Scenes"; acciones mínimas (Escena + Encender) — el firmware aplica el preset en applyEscenaPreset(). */
static char repisa_json_escenas_rm_por_defecto[] =
    R"({"Scenes":{"Scenes":[{"id":"lectura","operation":"add","name":"Lectura","action":{"Repisa":{"Encender":true,"Escena":"Lectura"}}},{"id":"fiesta","operation":"add","name":"Fiesta","action":{"Repisa":{"Encender":true,"Escena":"Fiesta"}}},{"id":"sueno","operation":"add","name":"Sueño","action":{"Repisa":{"Encender":true,"Escena":"Sueño"}}}]}})";

static void repisaLimpiarFlagEscenasRainMakerSeed() {
   prefsLock();
   prefs->begin(kRepisaRmPrefsNs, false);
   prefs->remove(kRepisaRmPrefScenesSeed);
   prefs->end();
   prefsUnlock();
}

/** Registra en la nube las escenas Lectura / Fiesta / Sueño una sola vez (luego queda en NVS). */
static void repisaIntentarPublicarEscenasRainMakerPorDefecto() {
   prefsLock();
   prefs->begin(kRepisaRmPrefsNs, true);
   const bool ya_enviadas = prefs->getUChar(kRepisaRmPrefScenesSeed, 0) == 1;
   prefs->end();
   prefsUnlock();
   if (ya_enviadas) {
      return;
   }
   const esp_err_t err =
       esp_rmaker_handle_set_params(repisa_json_escenas_rm_por_defecto,
                                    strlen(repisa_json_escenas_rm_por_defecto), ESP_RMAKER_REQ_SRC_LOCAL);
   if (err != ESP_OK) {
      Serial.printf("[Repisa] Escenas RM por defecto: error %d (se reintenta al reconectar MQTT)\n", (int)err);
      return;
   }
   prefsLock();
   prefs->begin(kRepisaRmPrefsNs, false);
   prefs->putUChar(kRepisaRmPrefScenesSeed, 1);
   prefs->end();
   prefsUnlock();
   Serial.println(F("[Repisa] Escenas por defecto (Lectura/Fiesta/Sueño) enviadas a RainMaker."));
}

static void repisaOnRainMakerCommonEvent(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
   (void)arg;
   (void)event_data;
   if (event_base != RMAKER_COMMON_EVENT || event_id != RMAKER_MQTT_EVENT_CONNECTED) {
      return;
   }
   repisaIntentarPublicarEscenasRainMakerPorDefecto();
}

int hue_rm = 0;
int valor_rm = 255;

/** Evita marcar Escena=Personalizado mientras aplicamos un preset (evita ecos MQTT). */
static volatile bool g_aplicando_escena = false;

static const char *kEscenaNombres[] = {"Personalizado", "Lectura", "Fiesta", "Sueño"};

/** Aplica preset de escena: modo + brillo (0–100) + color HSV + sensibilidad mic. */
static void applyEscenaPreset(const char *nombre) {
   if (!cinta || !luz_led || !nombre) {
      return;
   }
   if (strcmp(nombre, "Personalizado") == 0) {
      return;
   }

   const char *modo = "Static";
   int brillo_pct = 35;
   int hue = 30;
   int sens = 40;

   if (strcmp(nombre, "Lectura") == 0) {
      modo = "Static";
      brillo_pct = 30;
      hue = 32;
      sens = 38;
   } else if (strcmp(nombre, "Fiesta") == 0) {
      modo = "Spectrum6x8";
      brillo_pct = 95;
      hue = 18;
      sens = 88;
   } else if (strcmp(nombre, "Sueño") == 0) {
      modo = "Breath";
      brillo_pct = 12;
      hue = 8;
      sens = 25;
   } else {
      return;
   }

   g_aplicando_escena = true;

   cinta->setReaccionSonido(false);
   valor_rm = (int)map(constrain(brillo_pct, 0, 100), 0, 100, 0, 255);
   hue_rm = constrain(hue, 0, 360);
   sens = constrain(sens, 0, 100);

   cinta->encender();
   cinta->brillo(valor_rm);
   {
      uint32_t rgb = cinta->ws2812fx->ColorHSV(hue_rm * 182, REPISA_COLOR_SATURATION_BYTE, valor_rm);
      cinta->color(rgb);
   }
   cinta->modo(modo);

   CustomEffects::setMicSensitivityPct((uint8_t)sens);
   GuardarConfigInt(String("SensMic"), sens);

   luz_led->updateAndReportParam("Encender", true);
   luz_led->updateAndReportParam("Brillo", brillo_pct);
   luz_led->updateAndReportParam("Color", hue_rm);
   luz_led->updateAndReportParam("SensibilidadMic", sens);

   {
      const __FlashStringHelper *fsh = cinta->ws2812fx->getModeName(cinta->modo_act);
      char buf[48];
      strncpy_P(buf, reinterpret_cast<const char *>(fsh), sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      luz_led->updateAndReportParam("Efectos", buf);
   }

   g_aplicando_escena = false;
}

void write_callback(Device *device, Param *param, const param_val_t val, void *priv_data, write_ctx_t *ctx) {
   (void)device;
   (void)priv_data;
   (void)ctx;
   const char *param_name = param->getParamName();
   if (!param_name) {
      param_name = "";
   }
   if (strcmp(param_name, "Escena") == 0) {
      const char *s = val.val.s;
      if (s && strcmp(s, "Personalizado") != 0) {
         applyEscenaPreset(s);
      }
      param->updateAndReport(val);
   } else if (strcmp(param_name, "Efectos") == 0) {
      cinta->modo(val.val.s);
      if (!g_aplicando_escena && luz_led) {
         luz_led->updateAndReportParam("Escena", "Personalizado");
      }
      param->updateAndReport(val);
   } else if (repisaIsPowerParam(param_name)) {
      bool state = false;
      if (val.type == RMAKER_VAL_TYPE_BOOLEAN) {
         state = val.val.b;
      } else if (val.type == RMAKER_VAL_TYPE_INTEGER) {
         state = (val.val.i != 0);
      }
      if (state) {
         cinta->encender();
      } else {
         cinta->setReaccionSonido(false);
         cinta->apagar();
      }
      if (!g_aplicando_escena && luz_led) {
         luz_led->updateAndReportParam("Escena", "Personalizado");
      }
      param->updateAndReport(val);
   } else if (repisaParamIsBrightness(param)) {
      cinta->setReaccionSonido(false);
      const int cur_pct = (int)map(constrain(valor_rm, 0, 255), 0, 255, 0, 100);
      int b_pct = repisaIntFromCloudVal(val, cur_pct);
      if (b_pct < 0) {
         b_pct = 0;
      }
      if (b_pct > 100) {
         b_pct = 100;
      }
      valor_rm = map(b_pct, 0, 100, 0, 255);
      cinta->brillo(valor_rm);
      if (!g_aplicando_escena && luz_led) {
         luz_led->updateAndReportParam("Escena", "Personalizado");
      }
      param->updateAndReport(val);
   } else if (strcmp(param_name, "Color") == 0) {
      hue_rm = repisaIntFromCloudVal(val, hue_rm);
      if (hue_rm < 0) {
         hue_rm = 0;
      }
      if (hue_rm > 360) {
         hue_rm = 360;
      }
      uint32_t color = cinta->ws2812fx->ColorHSV(hue_rm * 182, REPISA_COLOR_SATURATION_BYTE, valor_rm);
      /* Igual que los botones de color del IR: forzar Static, si no el efecto activo
       * pisa el color en cada frame y el cambio no se ve. */
      cinta->setReaccionSonido(false);
      cinta->color(color, FX_MODE_STATIC);
      if (!g_aplicando_escena && luz_led) {
         luz_led->updateAndReportParam("Escena", "Personalizado");
      }
      param->updateAndReport(val);
   } else if (strcmp(param_name, "SensibilidadMic") == 0) {
      int s = repisaIntFromCloudVal(val, (int)CustomEffects::getMicSensitivityPct());
      if (s < 0) {
         s = 0;
      }
      if (s > 100) {
         s = 100;
      }
      CustomEffects::setMicSensitivityPct((uint8_t)s);
      GuardarConfigInt(String("SensMic"), s);
      if (!g_aplicando_escena && luz_led) {
         luz_led->updateAndReportParam("Escena", "Personalizado");
      }
      param->updateAndReport(val);
   } else if (strcmp(param_name, REPISA_RM_PARAM_VERIFICAR) == 0) {
      bool disparar = false;
      if (val.type == RMAKER_VAL_TYPE_BOOLEAN) {
         disparar = val.val.b;
      } else if (val.type == RMAKER_VAL_TYPE_INTEGER) {
         disparar = (val.val.i != 0);
      }
      param->updateAndReport(value(false));
      if (disparar && !g_repisa_git_check_busy) {
         g_repisa_git_check_busy = true;
         if (luz_led) {
            luz_led->updateAndReportParam(REPISA_RM_PARAM_INFO, "Comprobando GitHub...");
         }
         const BaseType_t ok =
             xTaskCreatePinnedToCore(repisaGitCheckTask, "repisa_git_chk", 12288, nullptr, 3, nullptr, tskNO_AFFINITY);
         if (ok != pdPASS) {
            g_repisa_git_check_busy = false;
            if (luz_led) {
               luz_led->updateAndReportParam(REPISA_RM_PARAM_INFO, "Error: no se pudo iniciar la comprobacion");
            }
         }
      }
   }
}

void CambioCintaLed(String nombre, const value_t &valor) {
   if (nombre == "Color") {
      GuardarConfigInt(nombre, valor.i_val);
      luz_led->updateAndReportParam(nombre.c_str(), valor.i_val);
   } else if (nombre == "Brillo") {
      /* NVS y WS2812FX en 0–255; RainMaker / voz usan 0–100 en el parámetro estándar de brillo. */
      GuardarConfigInt(nombre, valor.i_val);
      const int pct = (int)map(constrain(valor.i_val, 0, 255), 0, 255, 0, 100);
      luz_led->updateAndReportParam("Brillo", pct);
   } else if (nombre == "Efectos") {
      String StrEfecto = cinta->ws2812fx->getModeName(valor.i_val);
      GuardarConfigStr(nombre, StrEfecto);
      luz_led->updateAndReportParam(nombre.c_str(), StrEfecto.c_str());
   } else if (nombre == "Encender") {
      GuardarConfigBool(nombre, valor.b_val);
      luz_led->updateAndReportParam(nombre.c_str(), valor.b_val);
   }
}

void setup() {
   Serial.begin(115200);
   delay(500);
   /* Crear el mutex de prefs antes de que arranque cualquier otra tarea. */
   s_prefs_mutex = xSemaphoreCreateMutex();
   repisaEvaluarResetPorCiclosAlimentacion();

   Serial.println();
   Serial.println(F("=== Repisa Hot Wheels ==="));
   Serial.println(F("Sin boton ni PC: quita y pon la ALIMENTACION (enchufe/USB) 6 veces seguidas."));
   Serial.println(F("Cada vez que enciende, apagala antes de 45 s; al 6. encendido se resetea sola para"));
   Serial.println(F("volver a vincular RainMaker. Si la dejas mas de 45 s encendida, el contador se borra."));
   LeerConfig();
   CustomEffects::setMicSensitivityPct((uint8_t)constrain(config.SensMic, 0, 100));

   pinMode(REPISA_BOOT_BUTTON_PIN, INPUT_PULLUP);

   Param color_param("Color", ESP_RMAKER_PARAM_HUE, value((int)config.Color), PROP_FLAG_READ | PROP_FLAG_WRITE);
   Param effect_param("Efectos", ESP_RMAKER_PARAM_MODE, esp_rmaker_str(config.Efectos.c_str()), PROP_FLAG_READ | PROP_FLAG_WRITE);
   int brillo_rm = (int)map(constrain(config.Brillo, 0, 255), 0, 255, 0, 100);
   /* Un solo parámetro esp.param.brightness por dispositivo: voz (Google/Alexa) y app lo mapean aquí. */
   Param brightness_param("Brillo", ESP_RMAKER_PARAM_BRIGHTNESS, value(brillo_rm), PROP_FLAG_READ | PROP_FLAG_WRITE);
   const int sens_mic_rm = (int)constrain(config.SensMic, 0, 100);
   /* No usar esp.param.brightness (reservado al brillo real). */
   Param sens_mic_param("SensibilidadMic", ESP_RMAKER_PARAM_RANGE, value(sens_mic_rm), PROP_FLAG_READ | PROP_FLAG_WRITE);
   Param power_param("Encender", ESP_RMAKER_PARAM_POWER, value(config.Encender), PROP_FLAG_READ | PROP_FLAG_WRITE);
   Param scene_param("Escena", ESP_RMAKER_PARAM_MODE, esp_rmaker_str("Personalizado"), PROP_FLAG_READ | PROP_FLAG_WRITE);

   /* Matriz 6×8: REPISA_LED_* en config.h */
   cinta = new CintaLED(REPISA_LED_DATA_PIN, REPISA_LED_COUNT, CambioCintaLed, NEO_RGB);
#if defined(ARDUINO_ARCH_ESP32)
   /* INMP441: driver i2s.h + misma lógica de lectura que tu sketch (RepisaMic.cpp). */
   repisaMicInitInmp441(REPISA_I2S_PIN_SD, REPISA_I2S_PIN_SCK, REPISA_I2S_PIN_WS);
   cinta->setPinSonidoAnalogo(REPISA_MIC_PIN_I2S);
#endif

   Node my_node = RMaker.initNode("Nodo Repisa");

   power_param.addUIType(ESP_RMAKER_UI_TOGGLE);
   brightness_param.addBounds(value(0), value(100), value(1));
   brightness_param.addUIType(ESP_RMAKER_UI_SLIDER);
   sens_mic_param.addBounds(value(0), value(100), value(1));
   sens_mic_param.addUIType(ESP_RMAKER_UI_SLIDER);
   effect_param.addValidStrList(cinta->modos, cinta->n_modos);
   effect_param.addUIType(ESP_RMAKER_UI_DROPDOWN);
   scene_param.addValidStrList(kEscenaNombres, (uint8_t)(sizeof(kEscenaNombres) / sizeof(kEscenaNombres[0])));
   scene_param.addUIType(ESP_RMAKER_UI_DROPDOWN);
   color_param.addBounds(value(0), value(360), value(1));
   color_param.addUIType(ESP_RMAKER_UI_HUE_CIRCLE);

   Param version_firmware_param(REPISA_RM_PARAM_VERSION_LOCAL, "repisa.param.version", esp_rmaker_str(REPISA_FW_VERSION),
                                PROP_FLAG_READ);
   version_firmware_param.addUIType(ESP_RMAKER_UI_TEXT);
   Param info_actualizacion_param(REPISA_RM_PARAM_INFO, "repisa.param.git_info",
                                  esp_rmaker_str("Pulsa ComprobarGit"), PROP_FLAG_READ);
   info_actualizacion_param.addUIType(ESP_RMAKER_UI_TEXT);
   Param verificar_git_param(REPISA_RM_PARAM_VERIFICAR, "repisa.param.git_check", value(false),
                             PROP_FLAG_READ | PROP_FLAG_WRITE);
   /* push-btn-big = tarjona tipo interruptor; trigger = control compacto tipo “acción”. */
   verificar_git_param.addUIType(ESP_RMAKER_UI_TRIGGER);

   luz_led = new Device("Repisa", "esp.device.light", NULL);
   luz_led->addParam(power_param);
   luz_led->addParam(scene_param);
   luz_led->addParam(effect_param);
   luz_led->addParam(brightness_param);
   luz_led->addParam(sens_mic_param);
   luz_led->addParam(color_param);
   luz_led->addParam(version_firmware_param);
   luz_led->addParam(verificar_git_param);
   luz_led->addParam(info_actualizacion_param);
   luz_led->addCb(write_callback);
   my_node.addDevice(*luz_led);

   RMaker.enableOTA(OTA_USING_TOPICS);
   RMaker.enableTZService();
   RMaker.enableSchedule();
   RMaker.enableScenes();

   RMaker.start();

   {
      const esp_err_t erh = esp_event_handler_register(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID,
                                                       repisaOnRainMakerCommonEvent, nullptr);
      if (erh != ESP_OK) {
         Serial.printf("[Repisa] esp_event_handler_register RainMaker: %d\n", (int)erh);
      }
   }

   WiFi.onEvent(sysProvEvent);
#if RMAKER_USE_SOFTAP_PROVISION
   WiFiProv.beginProvision(NETWORK_PROV_SCHEME_SOFTAP, NETWORK_PROV_SCHEME_HANDLER_NONE, NETWORK_PROV_SECURITY_1, pop,
                           service_name, nullptr, nullptr, false);
#else
   WiFiProv.beginProvision(NETWORK_PROV_SCHEME_BLE, RMAKER_PROV_BLE_HANDLER, NETWORK_PROV_SECURITY_1, pop, service_name,
                           nullptr, nullptr, false);
#endif
   

   /* Snapshot bajo mutex: RMaker.start() ya corre y write_callback puede mutar `config`
    * (String no es atómico) desde la tarea de RainMaker. */
   prefsLock();
   const bool cfg_encender = config.Encender;
   const int cfg_brillo = config.Brillo;
   const uint32_t cfg_color = config.Color;
   const String cfg_efectos = config.Efectos;
   prefsUnlock();
   cinta->iniciar(cfg_encender, cfg_brillo, cfg_color, cfg_efectos.c_str());
   /* IR se arranca en loop(): no en provisión activa (IRremote vs BLE/WiFi); sin exigir WiFi conectado. */
   Serial.println("[IR]ANTES IRControlLite listo (independiente de WiFi/RainMaker; fuera de ventana de provisión).");
   irControl = new IRControlLite(REPISA_IR_RECEIVE_PIN);
   irControl->iniciar();
   Serial.println("[IR] IRControlLite listo (independiente de WiFi/RainMaker; fuera de ventana de provisión).");
  
   valor_rm = map(brillo_rm, 0, 100, 0, 255);
   power_param.updateAndReport(value(cfg_encender));
   brightness_param.updateAndReport(value(brillo_rm));
   sens_mic_param.updateAndReport(value((int)CustomEffects::getMicSensitivityPct()));
   color_param.updateAndReport(value((int)cfg_color));
   effect_param.updateAndReport(esp_rmaker_str(cfg_efectos.c_str()));
   scene_param.updateAndReport(esp_rmaker_str("Personalizado"));
   if (luz_led) {
      luz_led->updateAndReportParam(REPISA_RM_PARAM_VERSION_LOCAL, REPISA_FW_VERSION);
      luz_led->updateAndReportParam(REPISA_RM_PARAM_INFO, "Pulsa ComprobarGit");
      luz_led->updateAndReportParam(REPISA_RM_PARAM_VERIFICAR, false);
   }

   g_setup_done_ms = millis();
}

void IRControlLite_inicio(int codigoIR) {
   if (codigoIR < numOpcionesControl && AccionIR[codigoIR].inicio != nullptr) {
      AccionIR[codigoIR].inicio();
   }
}
void IRControlLite_presionado(int codigoIR) {
   if (codigoIR < numOpcionesControl && AccionIR[codigoIR].presionado != nullptr) {
      AccionIR[codigoIR].presionado();
   }
}
void IRControlLite_solto(int codigoIR) {
   if (codigoIR < numOpcionesControl && AccionIR[codigoIR].solto != nullptr) {
      AccionIR[codigoIR].solto();
   }
}

unsigned long tiempoAnterior = 0;
const unsigned long intervalo = 5000;
int wifiRetryCount = 0;

void probarConexion() {
   if (millis() - tiempoAnterior >= intervalo) {
      tiempoAnterior = millis();
      if (conectado_rainmaker) {
         if (WiFi.status() != WL_CONNECTED) {
            wifiRetryCount++;
            Serial.printf("WiFi desconectado. Reintentando conexión #%d...\n", wifiRetryCount + 1);
            WiFi.reconnect();
         } else if (wifiRetryCount > 0) {
            wifiRetryCount = 0;
         }
      }
   }
}


/** Opcional (avanzado): monitor serie 115200 — RM_FACTORY / RM_WIFI + Enter. */
static void procesarComandosSerialReset() {
   static String linea;
   while (Serial.available() > 0) {
      const char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
         linea.trim();
         if (linea.length() > 0) {
            if (linea == "RM_FACTORY") {
               Serial.println("[Repisa] RM_FACTORY: reinicio fabrica RainMaker (2 s)...");
               Serial.flush();
               delay(2000);
               repisaFlushConfigAhora();
               repisaLimpiarFlagEscenasRainMakerSeed();
               RMakerFactoryReset(2);
               return;
            }
            if (linea == "RM_WIFI") {
               Serial.println("[Repisa] RM_WIFI: borrando WiFi (2 s)...");
               Serial.flush();
               delay(2000);
               RMakerWiFiReset(2);
               return;
            }
            if (linea == "RM_SEED_SCENES") {
               repisaLimpiarFlagEscenasRainMakerSeed();
               repisaIntentarPublicarEscenasRainMakerPorDefecto();
               return;
            }
            /* ignorar lineas que no sean comandos */
         }
         linea = "";
         continue;
      }
      if (linea.length() < 48) {
         linea += c;
      } else {
         linea = "";
      }
   }
}

/** BOOT (GPIO0): sin while/delay — evalúa la duración solo al soltar; no bloquea animaciones ni el plotter. */
static void repisaProcesarBootButtonNoBloqueante() {
   static bool estaba_bajo = false;
   static uint32_t press_start_ms = 0;

   const bool bajo = (digitalRead(REPISA_BOOT_BUTTON_PIN) == LOW);

   if (bajo && !estaba_bajo) {
      press_start_ms = millis();
      estaba_bajo = true;
      return;
   }
   if (!bajo && estaba_bajo) {
      estaba_bajo = false;
      const uint32_t dur = millis() - press_start_ms;
      if (dur < 80U) {
         return;
      }
      if (dur > 10000U) {
         Serial.println(F("Reset to factory."));
         repisaFlushConfigAhora();
         repisaLimpiarFlagEscenasRainMakerSeed();
         RMakerFactoryReset(2);
      } else if (dur > 3000U) {
         Serial.println(F("Reset Wi-Fi."));
         RMakerWiFiReset(2);
      }
   }
}

void loop() {
   repisaCancelarContadorSiEncendidoEstable();
   repisaFlushConfigSiPendiente();
   procesarComandosSerialReset();

#if defined(ARDUINO_ARCH_ESP32) && REPISA_MIC_SERIAL_PLOTTER
   repisaMicSerialPlotterTick(); /* solo imprime soundLevel; no i2s_read */
#endif

   if (irControl) {
      irControl->procesar();
   }

   yield();
   if (cinta) {
      cinta->procesar();
   }

   probarConexion();

   repisaProcesarBootButtonNoBloqueante();

   yield();
}
