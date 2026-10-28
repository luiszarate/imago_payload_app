# Tuning del gimbal de la Orus L con gTune

Objetivo: que el gimbal responda más rápido a los comandos de la app sin perder
estabilización. Guía para quien hace el tuning, en tierra, con la Orus L montada en la aeronave.

## Por qué

En las pruebas en tierra del 2026-10-27 (`docs/pruebas/centrado-en-tierra.md`) el gimbal giró a
**~25 % de la tasa pedida**: `rate -20 0` dio 4.7–5.5°/s y `rate -30 0` dio 7.8°/s (zoom 1x),
igual directo que por el gimbal manager del PX4. En *follow*, tras un reset, el pitch baja solo
a ~0.3°/s hasta −16°. Nada de esto lo decide la app: lo deciden ajustes del gimbal que se
cambian con gTune y con la página web de la Orus L. Lo que dice Gremsy de cada uno está en
"Referencia de parámetros", al final.

## Hipótesis

| | Hipótesis | Paso que la prueba |
|---|---|---|
| H1 | "Autospeed" (página web de la Orus L) reduce la velocidad, quizá con el zoom | 3 y 4 |
| H2 | La velocidad máxima de control (`RC_SPD_TILT` / `RC_SPD_PAN`) limita o escala nuestras tasas | 5 |
| H3 | El suavizado de control (`RC_LPF_TILT` / `RC_LPF_PAN`) retrasa el arranque y la parada | 6 |
| H4 | La deriva del pitch en *follow* viene del follow de tilt (`FLW_SP_TILT`, *Follow Tilt*) | 8 |

## Reglas

- **Un solo cambio a la vez**, medir después de cada uno y anotarlo en el registro.
- **Exportar el perfil antes de tocar nada** (gTune → PROFILES → Export Gimbal Profile, un
  `.xml`) y guardarlo con la fecha. Si algo sale mal, se vuelve con Import.
- **No tocar**: Gain (Expert, 120), `ENABLE_ADRC`, la calibración de motor ni la de
  acelerómetro. Gremsy pide una contraseña para las dos calibraciones y advierte que una
  calibración mal hecha puede dañar los motores.
- Parar y deshacer el último cambio si aparece zumbido, vibración rápida, balanceo lento, o si
  los motores se calientan.

## Preparación

1. **Aeronave en tierra, sin hélices**, desarmada, nivelada, con el PX4, la Orus L y el Herelink
   encendidos. El tuning vale para la carga y la estructura con que se hace: hacerlo con la
   Orus L montada en la aeronave, no en el banco.
2. Espacio libre alrededor del gimbal: recorre pitch +40° a −120° y pan ±320°.
3. **Cerrar la app en el iPad y en la tableta Android**: usan la identidad 1/193, la misma que
   `imago-cli`, y mezclarían sus comandos con los de la prueba. No tocar el gimbal ni las ruedas
   en el control del Herelink.
4. gTune Desktop **v1.4.9.7** (la que pide Gremsy para la Orus L). Conexión: *Select your
   Connection*, IP de la Orus L `192.168.144.240`, puerto **14564**, *Search* (gTune no la
   detecta solo). *(Sin verificar: si gTune llega por el Wi-Fi del Herelink o hace falta cable
   Ethernet.)*
5. Página web de la Orus L: `http://192.168.144.240:8000` → **GIMBAL** → "Gimbal Control –
   Autospeed". Los cambios de esa sección se aplican al pulsar **UPDATE**.
6. Mac en la red del Herelink, `imago-cli` compilado. Para medir, **cerrar gTune** (no se sabe
   si manda algo al gimbal mientras está conectado) y arrancar:

   ```sh
   cmake --build build -j --target imago-cli
   mkdir -p logs
   ./build/tools/imago-cli/imago-cli - --tlog logs/tuning-$(date +%Y%m%d-%H%M).tlog --every 1
   ```

   Esperar `link=UP` e `ids cam=101 gimbal=154`. Si la aeronave vuela con el PX4 como gimbal
   manager, medir igual que en vuelo: `proto 3`, `take`, y esperar
   `control=this app (1/193)`.

## Medición (repetir después de cada cambio)

Siempre en *lock* (`mode 1`), zoom 1x (`zoomto 1`), con 5 s de gimbal quieto entre órdenes.

| Medida | Comandos en `imago-cli` | Qué anotar |
|---|---|---|
| A. Pitch −20°/s | `rate -20 0`, a los **3 s** `rate 0 0` | Pitch antes y después (línea `gimbal p=…`). Tasa medida = diferencia ÷ 3 |
| B. Pitch +20°/s | `rate 20 0`, 3 s, `rate 0 0` | Igual (vuelve al punto de partida) |
| C. Pan 30°/s | `rate 0 30`, 3 s, `rate 0 0` | Yaw antes y después; tasa = diferencia ÷ 3 |
| D. Pan −30°/s | `rate 0 -30`, 3 s, `rate 0 0` | Igual |
| E. Retraso | Las mismas órdenes, mirando el gimbal | ¿Arranca y para en seco o tarda? ¿Se pasa y vuelve? |

Con la tasa de A–D se sabe cuánto de lo pedido entrega (hoy ~25 %). La hora exacta de cada
orden y la actitud dos veces por segundo quedan en el `.tlog`:

```sh
python3 tools/tlog-gimbal.py logs/tuning-*.tlog
```

En esa lista, el tiempo entre una orden `RATE` y el primer cambio de pitch es el retraso de
arranque (resolución 0.5 s).

## Pasos

| Paso | Dónde | Qué hacer | Medir / ver |
|---|---|---|---|
| 1. Respaldo | gTune → PROFILES | Export Gimbal Profile; anotar firmware (INFO) y los valores actuales de todas las pestañas en el registro | — |
| 2. Línea base | `imago-cli` | Medidas A–E sin cambiar nada | Debería repetir el ~25 % del 2026-10-27 |
| 3. Autospeed y zoom (H1) | `imago-cli` | Anotar si Autospeed está activo; `zoomto 10`, medidas A y C, `zoomto 1` | Si a 10x es más lento que a 1x, la velocidad depende del zoom |
| 4. Autospeed apagado (H1) | Web → GIMBAL | Autospeed = Disable, **UPDATE**; repetir A–E (y A, C a 10x) | ¿Sube la tasa? Si no cambia nada, dejarlo como estaba |
| 5. Velocidad de control (H2) | gTune → Controls | Anotar Speed de Tilt y Pan (`RC_SPD_*`). Subirlos un 50 % (p. ej. 30 → 45); repetir A–E | ¿La tasa sube en la misma proporción? Entonces escala nuestras tasas |
| 6. Suavizado de control (H3) | gTune → Controls | Anotar Smooth de Tilt y Pan (`RC_LPF_*`). Bajarlos a la mitad; repetir E | Arranque y parada más secos. Si el video se ve brusco, subir de nuevo poco a poco |
| 7. Estabilización | gTune → SETTINGS → STIFFNESS | **AUTO TUNE** 2–3 veces, con la aeronave quieta. Comparar Stiffness con lo anotado | El gimbal mantiene la cámara firme al golpear suave la aeronave, sin zumbido |
| 7b. Stiffness a mano (si el Auto tune no convence) | gTune → Stiffness | Por eje, en orden Tilt, Roll, Pan: subir 5–10 hasta notar oscilación al tocar la cámara, bajar 5. Debe quedar Tilt < Roll < Pan | Sin oscilación al mover la aeronave en cualquier orientación |
| 8. Follow (H4) | gTune → Follow | Anotar Follow Tilt/Pan, Speed, Smooth y Window. Si *Follow Tilt* está activo, probar con `mode 2`: `mode 4` (reset) y mirar si el pitch baja solo; luego apagar Follow Tilt y repetir | ¿Desaparece la deriva a −16°? |
| 8b. Follow más vivo | gTune → Follow | Speed de Pan más alto, Smooth más bajo, Window entre 1 y 2 (nunca 0: produce temblor) | Al girar la aeronave, el pan la sigue con menos retraso y sin temblar |
| 9. Persistencia | Aeronave | Apagar y encender la Orus L; repetir A y C | Los valores siguen; si no, gTune no los guardó |
| 10. Cierre | gTune → PROFILES | Exportar el perfil final con la fecha | — |

Filtros (gTune → Stiffness → Gyro filter / Output filter): **no cambiarlos para ganar
respuesta**. Solo si después del paso 7 queda una oscilación: vibración rápida → subir el
Output filter; balanceo lento → bajarlo. Un filtro más alto quita ruido pero agrega retraso.

## Análisis y entrega

Enviar a desarrollo el `.tlog`, el registro de abajo, el perfil `.xml` inicial y el final. Con
eso se registra en `docs/protocol/orus-l.md` qué ajuste explica el 25 % y con qué valores queda
nuestra Orus L.

| Resultado | Qué significa |
|---|---|
| H1 sí (Autospeed baja la tasa, o el zoom la cambia) | Decidir con el operador si se quiere velocidad según zoom: es útil a 30x, molesta a 1x |
| H2 sí (la tasa sigue a `RC_SPD_*`) | La Orus L escala nuestras tasas con ese máximo: fijar un valor y anotar la escala para la app |
| H2 no y H1 no | El 25 % viene de otro lado (firmware, `RC_MODE`): consultar a Gremsy con el `.tlog` |
| H3 sí | Dejar el suavizado más bajo que no se note brusco en video |
| H4 sí | Follow Tilt apagado si el operador no lo quiere |

## Registro de cambios

| Fecha y hora | Dónde | Parámetro | Antes | Después | Tasa A / C (°/s) | Notas |
|---|---|---|---|---|---|---|
| | | (línea base) | | | | |
| | | | | | | |

## Referencia de parámetros

Nombres como aparecen en gTune, el ID del firmware (el que viaja por MAVLink al componente
154) y el nombre en el perfil `.xml` de gTune.

**Estabilización** (SETTINGS → Basic / Expert)

| En gTune | ID firmware | En el `.xml` | Qué hace | Para más respuesta |
|---|---|---|---|---|
| Stiffness Tilt/Roll/Pan | `STIFF_TILT`, `STIFF_ROLL`, `STIFF_PAN` | `PITCH_P`, `ROLL_P`, `YAW_P` | Cuánto corrige el gimbal para mantener la cámara | Lo más alto sin oscilar |
| Hold strength (Expert) | `PWR_TILT`, `PWR_ROLL`, `PWR_PAN` | `PITCH_POWER`, `ROLL_POWER`, `YAW_POWER` | Potencia mínima del motor con Auto power, o fija sin él. Por defecto 40/40/40 en gimbals H y T | +10 % con carga pesada; más potencia, más calor |
| Auto power adjustment (Expert) | — | — | Potencia entre Hold strength y el máximo según el error de ángulo | Dejar activo |
| Gain (Expert) | — | — | "Ganancia del sistema"; gSDK: qué tan rápido vuelve cada eje a la posición pedida. Por defecto 120 | No tocar |
| Gyro filter | `GYRO_LPF` | `GYRO_LPF` | Filtro del giroscopio | No tocar para ganar respuesta |
| Output filter | `FILTER_OUT` | `YAW_I` | Filtro de la salida a los motores | Vibración rápida → subir; balanceo lento → bajar |
| AUTO TUNE | — | — | Busca el Stiffness solo | 2–3 veces con la aeronave quieta |
| — | `MT_ELEC_PWR` | — | Potencia eléctrica del motor, 50–100 % (no documentado) | Solo leer: debería estar en 100 |
| — | `ENABLE_ADRC` | `DIR_MOTOR_PITCH` | Control de rechazo de perturbaciones (no documentado) | No tocar |

**Control** (pestaña Controls)

| ID firmware | Qué hace | Para más respuesta |
|---|---|---|
| `RC_SPD_TILT`, `RC_SPD_PAN`, `RC_SPD_ROLL` | Velocidad máxima en °/s. gSDK la usa como tope de las tasas por MAVLink | Subir |
| `RC_LPF_TILT`, `RC_LPF_PAN`, `RC_LPF_ROLL` | Suavizado de los comandos: más alto, más suave y con más retraso | Bajar |
| `CTRL_ANGLE_MODE` | Órdenes por ángulo: 0 camino más corto, 1 directo | 0 |
| `RC_LIM_MIN/MAX_TILT`, `_PAN`, `_ROLL` | Límites de recorrido (`TRAVEL_MIN_PIT`… en el `.xml`) | No cambian la velocidad |
| `RC_TRIM_TILT`, `RC_TRIM_ROLL`, `RC_REVERSE_AXIS` | Trims y sentido de cada eje | No cambian la velocidad |

**Follow** (pestaña Follow)

| ID firmware | En el `.xml` | Qué hace | Recomendación |
|---|---|---|---|
| `FLW_SP_TILT`, `FLW_SP_PAN` | `PITCH_FOLLOW`, `YAW_FOLLOW` | Velocidad con que sigue a la aeronave | Subir para seguir antes |
| `FLW_LPF_TILT`, `FLW_LPF_PAN` | `PITCH_FILTER`, `YAW_FILTER` | Suavizado del follow | Bajar para menos retraso |
| `FLW_WD_TILT`, `FLW_WD_PAN` | `TILT_WINDOW`, `PAN_WINDOW` | Zona muerta en grados: dentro no se mueve | 1–5; 0 produce temblor |

**Orus L, fuera de gTune**

| Dónde | Ajuste | Qué hace |
|---|---|---|
| Web → GIMBAL | Gimbal Control – Autospeed | Enable: la velocidad se ajusta según la entrada. Disable: velocidad constante |
| Cámara (`PARAM_EXT`) | `RC_MODE` | 0 Gremsy, 1 Standard (hoy 1). Cómo maneja el RC al gimbal |

## Dudas abiertas

- **Smooth del follow:** la página de gTune dice que más alto es más rápido; la de Follow
  Mode Settings, que más alto es más suave y con más retraso. Medir antes de creer cualquiera.
- **"Speed 0" en follow:** gTune lo recomienda "para respuesta inmediata"; no está claro qué
  hace.
- **Valores por defecto de los filtros:** según la fuente, Gyro/Output son 2/3 (gSDK, S1V3) o
  5/1 (página general de Filter). Anotar los de nuestra Orus L en el paso 1.
- **Gain:** no se sabe con qué ID viaja.

## Fuentes

- Gremsy: [gTune Desktop – How to use](https://docs.gremsy.com/gremsy-app/gtune-desktop/how-to-use),
  [Orus L – gTune Desktop](https://docs.gremsy.com/payloads/orus-l/software-configuration/assistance-software/gtune-desktop),
  [Orus L – web, Gimbal](https://docs.gremsy.com/payloads/orus-l/software-configuration/web-setting-app/gimbal),
  [Stiffness tuning](https://docs.gremsy.com/gimbals/general/gimbal-configuration/gimbal-tunning/stiffness-tunning),
  [Filter](https://docs.gremsy.com/gimbals/general/gimbal-configuration/gimbal-tunning/filter),
  [Follow mode settings](https://docs.gremsy.com/gimbals/general/gimbal-configuration/follow-mode-settings),
  [gTune FAQ](https://docs.gremsy.com/gremsy-app/gtune-desktop/faq).
- IDs de firmware y su relación con el `.xml`: `src/gimbal_interface.*` de
  [gSDK](https://github.com/Gremsy/gSDK) y los ejemplos `gimbal_*` de
  [PayloadSdk](https://github.com/Gremsy/PayloadSdk) (solo leídos, nada copiado).
- Experiencia de otros con Gremsy por MAVLink:
  [hilo de ArduPilot 4.3](https://discuss.ardupilot.org/t/ardupilot-4-3-gremsy-gimbal-improvements-testers-wanted/86479?page=2).
