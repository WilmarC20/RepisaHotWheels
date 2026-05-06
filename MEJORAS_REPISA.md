# Ideas de mejora para la repisa Hot Wheels

Repo: ESP32-C6 Super Mini, matriz 6×8 (48 LEDs WS2812), micrófono MAX9814, IR (RMT), RainMaker.

Las entradas marcadas con **★** son las que suelen dar **alto impacto con bajo esfuerzo** en tu hardware actual.

---

## Hardware / electricidad (lo más importante de revisar)

- **★ Fuente 5 V dedicada** de 3–5 A para la tira: 48 WS2812 a blanco 100 % piden ~2.9 A. Si alimentas por el USB del ESP32 verás resets, glitches y colores raros. **GND común** con el ESP32.
- **★ Capacitor 470–1000 µF** entre 5 V y GND cerca del primer LED.
- **★ Resistencia 330–470 Ω** en serie en la línea de datos hacia DIN.
- **Level shifter** (74AHCT125 / SN74LVC1T45) entre el GPIO del C6 (3.3 V) y DIN de la tira (5 V). A veces funciona sin él, pero falla de forma intermitente si baja tensión o sube temperatura.
- **Micrófono**: en hardware se usa **MAX9814** (AGC). El KY-037 era ruidoso; con MAX9814 suele bastar ajustar **SensMic** en RainMaker y el pin **GAIN** del breakout.
- **TSOP38238 / VS1838B** dedicado para IR si usas un receptor genérico (más alcance y menos ruido).
- **PIR HC-SR501** o sensor de luz **BH1750** opcional para encender automáticamente al pasar o atenuar de noche.

---

## Audio (espectrograma y reacción al sonido)

- **★ FFT real** (p. ej. `ArduinoFFT`) en lugar del DFT manual: libera CPU y permite 8 bandas u opciones más finas.
- **★ Detector de beat / BPM** (energía de banda baja con umbral adaptativo): efectos que pulsen con la batería, no solo con amplitud.
- **Filtro pasa-altos** suave en software si conectas audio analógico desde el celular (menos 50/60 Hz).
- **Calibración de silencio al arrancar** (3–5 s de ruido base) para auto-fijar deadband; menos dependencia solo del slider SensibilidadMic.
- **AGC asimétrico** afinado: aplauso que suba en un frame y silencio que decaiga lento.
- **Ganancia por banda configurable desde RainMaker** (graves / medios / agudos) para ecualizar mic y sala.

---

## Efectos nuevos (matriz 6×8)

- **★ Reloj 6×8** con NTP (ya tienes WiFi): fuente 3×5 con scroll o estática.
- **Plasma**: olas de color con `sin`/`cos` (sin sonido), bueno para valorar la matriz.
- **Fire por columnas**: llamas subiendo, derivado del concepto `fire_flicker`.
- **Confetti reactivo**: por cada golpe fuerte del mic, partículas que caen y se desvanecen.
- **VU horizontal**: una barra que crece de izquierda a derecha con el volumen.
- **Pong / Snake** controlado por IR.
- **Notificaciones**: flash breve al cambiar parámetro RainMaker o un trigger externo.
- **Modo carrera Hot Wheels**: cuenta regresiva 3-2-1 por IR; con foto-sensor en pista, cronómetro de vueltas.

---

## RainMaker / control

- **★ Slider de velocidad** (mapeo a `seg->speed`) y slider de **saturación** o paleta.
- **★ Brillo automático nocturno** según hora (NTP + schedule RainMaker).
- **Selector de paleta**: dropdown (Hot Wheels, Océano, Fuego, Pastel…) como índice + tabla de gradientes.
- **Modo automático** con timer: rotar efectos cada N segundos.
- **Escenas** (nube + app): guía paso a paso en **`ESCENAS_RAINMAKER.md`**. En el dispositivo también está el parámetro **Escena** (Lectura / Fiesta / Sueño).
- **★ Voz**: Alexa / Google con RainMaker: «enciende la repisa», «modo fiesta», «color azul».
- **Modo offline**: si pierde Internet, priorizar IR y no reconectar de forma agresiva (ajustar según necesidad).

---

## Conectividad (más allá de RainMaker)

- **★ mDNS** (`repisa.local`): no depender de recordar la IP.
- **Servidor web local** simple para control en LAN sin depender solo de la app RainMaker.
- **MQTT** propio paralelo para **Home Assistant**.
- **WebSocket + WebAudio**: página en el celular que reproduce o analiza audio y manda 6 bandas al ESP (sin pasar audio por la nube).
- **ESP-NOW** entre varias repisas: una master lee el mic y envía bandas; las demás siguen **sincronizadas** sin WiFi pesado.

---

## UX local (sin app / IR)

- **★ Botón BOOT con varias acciones**: 1 toque on/off, 2 toques siguiente modo, hold reset (expandir sobre el comportamiento actual).
- **Encoder rotativo + push** para brillo y modo.
- **OLED 0.96" I2C** para modo activo, sensibilidad, IP y nivel de mic (útil sin Serial).

---

## Calidad de software

- **★ Watchdog** (`esp_task_wdt`) para reinicio si un efecto se cuelga.
- **Refactor** de `CustomEffects.cpp` (archivo grande): separar audio, texto, utilidades de matriz.
- **Logs por UDP / syslog** para depurar sin cable USB.
- **Probar OTA** (RainMaker) antes de cerrar el gabinete físico.
- **Simulador PC** (p. ej. Pygame) para prototipar efectos sin flashear cada cambio menor.

---

## Temático Hot Wheels

- **Sensores IR/láser** inicio y fin de pista para **cronometrar vueltas** en la matriz.
- **Banderazo de salida**: un botón IR dispara banderas + cuenta atrás.
- **Show secuencial**: texto `HotWheels` → espectrograma ~30 s → `Impacto` en bucle.
- **Color por equipo** (rojo/naranja) asignado a botones del IR.

---

## Orden sugerido de implementación

1. Alimentación + capacitor + resistencia + level shifter (estabilidad).
2. **★** Botón BOOT multi-acción + watchdog + mDNS.
3. **★** `ArduinoFFT` + detector de beat (más reactivo).
4. **★** Slider velocidad + selector paleta en RainMaker + voz Alexa/Google.
5. Reloj + modo carrera temático.

---

## Usuario final: volver a vincular RainMaker (sin botón ni PC)

Si la repisa **ya no aparece en la app** pero sigue con WiFi guardada, hace falta un **reinicio de fábrica RainMaker** en el propio dispositivo.

**Método sencillo** (solo enchufe, sin monitor serie ni comandos):

1. Quita y pon la **alimentación** (USB o fuente 5 V) **6 veces seguidas**.
2. En cada ciclo: en cuanto encienda, **apágala antes de 45 segundos** y vuelve a enchufar.
3. Al **sexto** encendido aplicará sola el reset de fábrica y podrás **añadirla otra vez** en RainMaker (QR / provisión BLE).
4. Si en algún momento la dejas **más de 45 s encendida**, el contador se **borra** y tienes que empezar de nuevo (así se evita un reset accidental).

Opcional avanzado: monitor serie 115200, escribir `RM_FACTORY`, `RM_WIFI` o `RM_SEED_SCENES` y Enter (ver `RepisaHotWheels.ino`).

---

## Nota sobre audio del celular y RainMaker

RainMaker **no** transporta audio en vivo de forma práctica (MQTT, límites y latencia). Para usar el celular como fuente:

- **Reproductor dentro de una página web** servida por el ESP + Web Audio + WebSocket (6 floats por frame), o  
- **Cable analógico** del celular al circuito del mic (cualquier app: Spotify, YouTube, etc.), o  
- App nativa **Android** con `AudioPlaybackCapture` hacia el ESP (iOS no expone captura de otras apps), o  
- Placa con **Bluetooth clásico / A2DP** (el ESP32-C6 no tiene BT classic; otro chip sí podría actuar como «bocina» y recibir el mix del sistema).

Ver también la conversación en el proyecto sobre límites del navegador móvil al capturar audio de **otras** apps (Spotify, YouTube app, etc.).
