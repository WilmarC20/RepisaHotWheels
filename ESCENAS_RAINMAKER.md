# Escenas en RainMaker (Lectura, Fiesta, Sueño)

En **ESP RainMaker** las escenas pueden gestionarse desde la **app móvil** o **panel**; el firmware llama a `RMaker.enableScenes()` para exponer el servicio **Scenes**.

### Creación automática (este proyecto)

La primera vez que el nodo **conecta a MQTT** tras un arranque, el sketch **registra solo** las tres escenas *Lectura*, *Fiesta* y *Sueño* en la nube (acción mínima: `Encender` + `Escena`; el preset completo lo aplica el firmware en `applyEscenaPreset`). Se guarda un flag en NVS (`Preferences`, namespace `repisa_rm`, clave `scenes_seed`) para no volver a enviarlas en cada reconexión.

- Tras **reinicio de fábrica RainMaker** (6× alimentación, `RM_FACTORY`, botón largo, etc.) ese flag se **borra** y volverán a publicarse en el próximo MQTT conectado.
- Monitor serie **115200**: comando `RM_SEED_SCENES` + Enter fuerza de nuevo el envío (útil tras borrar escenas a mano en la app).

Si al compilar aparece error de enlace con `esp_rmaker_handle_set_params`, avisa: depende de la versión del core/Arduino-ESP32 que enlacé el símbolo del RainMaker.

Este documento también lista valores **manuales** en la app por si quieres escenas con más parámetros en la acción.

**Dispositivo en la app:** *Repisa* (`esp.device.light`).

---

## Cómo crear una escena (app Espressif RainMaker)

La ruta exacta puede variar según versión de la app; suele ser:

1. Abre **ESP RainMaker** (Android / iOS).
2. Entra a **Scenes** / **Escenas** (icono o menú principal).
3. **Add scene** / **Añadir escena**.
4. Pon **nombre** (recomendado más abajo, útil para Google Home si el asistente expone escenas).
5. **Añadir acción** → elige el dispositivo **Repisa**.
6. Ajusta **todos** los parámetros listados para esa fila de la tabla (o al menos los que la app obligue).
7. **Guardar**.

Repite para las tres escenas.

---

## Valores por escena (copiar a la app)

| Parámetro (nombre en app) | Lectura | Fiesta | Sueño |
|---------------------------|--------:|-------:|------:|
| **Encender**              | Sí (on) | Sí    | Sí    |
| **Escena**                | Lectura | Fiesta | Sueño |
| **Brillo** (0–100)        | 30      | 95     | 12    |
| **Color** (Hue 0–360)    | 32      | 18     | 8     |
| **SensibilidadMic** (0–100) | 38    | 88     | 25    |
| **Efectos** (modo)        | Static | Spectrum6x8 | Breath |

**Notas:**

- La **saturación del color** no es un parámetro en la app: en el firmware va fija al máximo (100 %).
- **Escena** y **Efectos** deben ser coherentes: al activar la escena deberías fijar **Escena** al mismo nombre y **Efectos** al modo de la tabla (la nube enviará varios `write`; el firmware ordenará brillo/color/modo).
- Si la app no permite fijar **Escena** y **Efectos** a la vez, prioriza **Escena** = Lectura / Fiesta / Sueño: ese parámetro aplica el preset completo en el dispositivo.

---

## Nombres recomendados (voz / rutinas)

- `Lectura`
- `Fiesta`
- `Sueño`

En **Google Home**, si RainMaker publica escenas, prueba: *«Ok Google, activa Fiesta»* o el nombre exacto que veas en Home.  
Si no aparece, crea una **rutina** en Google Home que ejecute esa escena o que ajuste el dispositivo **Repisa** (cuando el control fino esté disponible).

---

## Relación con el parámetro *Escena* del firmware

En el sketch, el desplegable **Escena** incluye: Personalizado, Lectura, Fiesta, Sueño.  
Las **escenas de la nube** son otro mecanismo: al **activar** una escena, RainMaker envía los valores de los parámetros al ESP; el efecto final debe ser el mismo que si eligieras **Escena** = Fiesta en la app.

---

## Si no ves “Scenes” en la app

- Comprueba que el firmware tenga `RMaker.enableScenes()` (ya está en `RepisaHotWheels.ino`).
- Vuelve a abrir la app tras reflash / reconexión del nodo.
- Cuenta de **RainMaker público** y app actualizada.
