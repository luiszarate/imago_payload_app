# Manual del programador — Imago Payload Control

Guía para quien desarrolla o mantiene Imago Payload Control: cómo está hecha la aplicación,
cómo se compila y se prueba en cada plataforma, y cómo se agrega una función sin romper la
paridad entre apps. Los textos de la interfaz se citan en inglés, tal como aparecen en pantalla.

> **Mantenimiento de este manual.** Todo cambio de código que modifique el comportamiento, la
> API del core, la compilación o las herramientas debe actualizar este manual y el
> [manual de usuario](manual-usuario.md) en el mismo commit (regla en [`CLAUDE.md`](../CLAUDE.md)).

## Contenido

1. [Qué es y para qué sirve](#1-qué-es-y-para-qué-sirve)
2. [Arquitectura](#2-arquitectura)
3. [Estructura del repositorio](#3-estructura-del-repositorio)
4. [Compilar y ejecutar](#4-compilar-y-ejecutar)
5. [El core en C++](#5-el-core-en-c)
6. [Protocolo de la Orus L](#6-protocolo-de-la-orus-l)
7. [Las apps](#7-las-apps)
8. [Funciones, de punta a punta](#8-funciones-de-punta-a-punta)
9. [Herramientas](#9-herramientas)
10. [Pruebas e integración continua](#10-pruebas-e-integración-continua)
11. [Cómo hacer un cambio](#11-cómo-hacer-un-cambio)
12. [Licencias](#12-licencias)
13. [Pendientes y puntos sin verificar](#13-pendientes-y-puntos-sin-verificar)

---

## 1. Qué es y para qué sirve

Imago Payload Control opera el payload **Gremsy Orus L** (cámara EO con zoom hasta 300×,
cámara térmica IR, telémetro láser, tracker y detección por IA, sobre un gimbal de 3 ejes)
desde iPad/iPhone, teléfonos y tablets Android, y PCs con Windows o Linux. Reproduce las
funciones de *Payload Plus* (la app Android de Gremsy) con dos prioridades: la **mínima
latencia** que pueda agregar la app (video y mando del gimbal) y la **confiabilidad** en campo.

- La app de **iOS/iPadOS es la referencia**: lo que hace es lo que deben hacer las demás.
- Android y escritorio se mantienen a la par; sus diferencias se registran en
  [`docs/pending-android-desktop.md`](pending-android-desktop.md).
- Documento de diseño (español):
  https://claude.ai/code/artifact/3bef4f57-a422-4fd7-a8d1-2f8c278a3712

## 2. Arquitectura

```
                 ┌───────────────────────── Orus L ─────────────────────────┐
                 │  payload manager (comp 26)   cámara (100-105)   gimbal (154) │
                 └──────────┬──────────────────────────┬────────────────────┘
               MAVLink v2 / UDP 14566            RTSP + RTP (H.264/H.265)
             (directo o reenviado por Herelink)  (de la Orus L o de Herelink)
                            │                          │
┌───────────────────────────┴──────────────────────────┴──────────────────────────┐
│ core/ (C++20, API en C)                                                          │
│  mavlink/  enlace UDP, tlog          rtsp/ + rtp/  cliente RTSP, depacketizer     │
│  payload/  protocolo Orus L, modelo de estado, comandos con ACK y reintentos     │
│  video/    receptor, medición de retraso (lag), suavizado                         │
│  session/  imago_session: hilos, eventos, API C                                   │
└───────────┬───────────────────────────────┬─────────────────────────────────────┘
   estado (copia) + eventos + comandos     cuadros comprimidos (sin decodificar)
            │                               │
┌───────────┴──────────┬────────────────────┴───────────┬──────────────────────────┐
│ apple/  SwiftUI      │ android/  Compose + JNI         │ desktop/  Qt 6 / QML     │
│ VideoToolbox → AVSampleBufferDisplayLayer │ AMediaCodec → SurfaceView │ FFmpeg → QVideoSink │
└──────────────────────┴─────────────────────────────────┴──────────────────────────┘
```

Decisiones (ver [`docs/adr/`](adr/)):

- **ADR 0001**: protocolo, transporte de video y estado del payload viven en un core portable
  con API en C. Cada plataforma escribe solo su UI, la decodificación por hardware y el
  dibujado. El core **nunca decodifica**: entrega unidades de acceso H.264/H.265.
- **ADR 0002**: escritorio en Qt Quick (LGPLv3, enlace dinámico) con FFmpeg/libavcodec para
  decodificar en Windows y Linux; macOS usa la app SwiftUI.
- No se usa el código del PayloadSdk de Gremsy (GPLv3, incompatible con una app propietaria):
  solo como referencia del protocolo. Tampoco FFmpeg/live555 para RTSP.

### Hilos

| Hilo | Dónde | Qué hace |
| --- | --- | --- |
| Red | core, `Session::networkThread_` | Recibe MAVLink, servicios periódicos (heartbeat, intervalos, lecturas de parámetros, reintentos), repite tasas del gimbal, keep-awake. En Windows mantiene `timeBeginPeriod(1)` mientras corre |
| Video | core, `VideoReceiver` | RTSP/RTP, arma cada cuadro y llama al callback de video **en este hilo** |
| tlog | core, `TlogWriter` | Escribe el `.tlog` a disco, nunca en la ruta de control |
| Llamante | apps | Los comandos se codifican y envían **en el hilo que los llama** (joystick, mando) sin pasar por el hilo principal |
| Principal | apps | Lee el estado a 30 Hz (`imago_session_get_state`) y pinta la UI; recibe eventos |

Reglas de las rutas críticas: el video se entrega sin colas (cada cuadro sale cuando llega
su último paquete), y los comandos de control salen en el momento en que se emiten. Nada en
esas rutas espera al hilo principal.

## 3. Estructura del repositorio

| Ruta | Contenido |
| --- | --- |
| `core/include/imago/imago_core.h` | API pública en C (única interfaz de las apps con el core) |
| `core/src/` | `mavlink/`, `payload/`, `rtsp/`, `rtp/`, `video/`, `session/`, `platform/` (sockets, hilos, reloj), `util/` |
| `core/tests/` | Pruebas doctest (unitarias e integración contra `orus-sim`) |
| `core/third_party/` | Cabeceras MAVLink C (MIT, dialecto common) y doctest |
| `tools/orus-sim/` | Simulador de la Orus L (MAVLink + RTSP) |
| `tools/imago-cli/` | Cliente de terminal de la API C |
| `tools/tlog-bursts.py` | Análisis de ráfagas y pérdidas en un `.tlog` |
| `Package.swift` | Expone el core a Swift como módulo `ImagoCore` |
| `apple/` | App SwiftUI (iOS 17+, iPadOS); proyecto generado con XcodeGen (`apple/project.yml`) |
| `android/` | App Kotlin + Jetpack Compose (Android 7.0+, API 24), JNI al core |
| `desktop/` | App Qt 6 / QML para Windows 10/11 x64 y Linux |
| `docs/adr/` | Decisiones de arquitectura |
| `docs/protocol/orus-l.md` | Notas del protocolo de la Orus L (fuente de cada valor) |
| `docs/pending-android-desktop.md` | Diferencias de Android y escritorio contra iOS, pruebas de campo pendientes |
| `docs/manual-programador.md`, `docs/manual-usuario.md` | Estos manuales |
| `.github/workflows/ci.yml` | CI: core, iOS, Android, escritorio Linux y Windows |

## 4. Compilar y ejecutar

### Core y herramientas (Linux, macOS, Windows)

Requisitos: CMake 3.22+, compilador C++20 (GCC 11+, Clang 14+, Apple Clang 15+, MSVC 2022+).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Con sanitizers: `cmake -S . -B build-asan -DIMAGO_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug`
(ASan + UBSan). Para TSan existe `build-tsan` con la misma idea. Una prueba concreta:
`build/core/tests/imago_core_tests -tc="*isotherm*"`.

### iOS / iPadOS (Mac con Xcode)

```sh
brew install xcodegen
cd apple && xcodegen            # genera ImagoPayloadControl.xcodeproj (no se versiona)
open ImagoPayloadControl.xcodeproj
```

Elegir el equipo de firma en *Signing & Capabilities* y ejecutar en el dispositivo. Volver a
correr `xcodegen` cuando se agregan o quitan archivos. La primera conexión pide el permiso de
**Red local**; sin él todo falla con "Network is unreachable". Sin hardware: correr `orus-sim`
en la Mac y usar el perfil "orus-sim on this Mac" en el simulador de iOS. Desde Linux la única
comprobación de compilación es el job de CI "iOS app (build, unsigned)".

### Android

Requisitos: JDK 17+, SDK con plataforma 37, NDK 29.0.14206865, CMake 3.31.6.

```sh
cd android
./gradlew assembleDebug testDebugUnitTest lintDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

`compileSdk` 37 (lo exige Compose 1.12), `targetSdk` 36 a propósito (Android 17 agrega un
permiso de red local para apps que apuntan a 37), `minSdk` 24. Firma de release: ver
[`android/README.md`](../android/README.md). El perfil "orus-sim (emulator host)" usa
10.0.2.2 para llegar al simulador desde el emulador.

### Escritorio (Qt 6)

Linux:

```sh
sudo apt install libavcodec-dev libavutil-dev libsdl2-dev libgl1-mesa-dev
cmake -S desktop -B desktop/build -DCMAKE_PREFIX_PATH=~/Qt/6.10.3/gcc_64
cmake --build desktop/build -j && ctest --test-dir desktop/build
./desktop/build/ImagoPayloadControl
```

Windows: Visual Studio 2022+, Qt para MSVC 2022 x64 y vcpkg (FFmpeg LGPL solo libavcodec, y
SDL2, versiones fijadas en `desktop/vcpkg.json`); comandos completos en
[`desktop/README.md`](../desktop/README.md). FFmpeg en Linux sale de pkg-config: **nunca**
enlazar el libavcodec que Qt trae para su backend multimedia (no trae cabeceras).

Opciones de línea de comandos del escritorio (las usa la prueba de humo del CI): `--profile
<nombre>`, `--connect`, `--open <página>`, `--delay <ms>`, `--screenshot <archivo.png>`, `--view <eo|ir|ir-eo|…>` (cambia la vista al
conectar; `--view ir` muestra la barra térmica); ver
`desktop/README.md` para la lista vigente. Variables: `IMAGO_HWDEC=0` (decodificación por
software), `IMAGO_FFMPEG_LOG=1`.

## 5. El core en C++

### Reglas

1. Nada de UI ni código de plataforma dentro de `core/`; sockets, hilos y reloj pasan por
   `core/src/platform/`.
2. La API pública es C puro: sin excepciones ni tipos C++.
3. Las rutas críticas no bloquean.
4. Toda lógica de protocolo vive en el core, nunca en Swift, Kotlin o QML: así se prueba una
   vez y vale para las tres apps.

### API (`imago_core.h`)

| Grupo | Funciones | Notas |
| --- | --- | --- |
| Configuración | `imago_config_init`, `imago_config` | Modo directo (`payload_ip`) o escucha (`NULL`, típico de Herelink), RTSP, protocolo de gimbal, guardia de retraso, keep-awake, suavizado |
| Sesión | `imago_session_create/start/stop/destroy`, `imago_session_get_state` | El estado es una copia consistente; leerlo a la frecuencia de pantalla |
| Grabación | `imago_session_start_recording/stop_recording` | `.tlog` de todo el MAVLink entrante y saliente + métricas de la app |
| Video | `imago_video_start/stop`, `imago_report_frame_presented`, `imago_now_ns` | Las apps reportan decodificado/mostrado para el HUD |
| Gimbal | `imago_gimbal_set_rate/set_angle/set_mode/set_protocol/take_control` | Tasa repetida a `gimbal_rate_hz` mientras no sea cero |
| Cámara | `imago_camera_set_mode/take_photo/set_recording/zoom/zoom_to/zoom_target/zoom_range/focus/autofocus/set_view/set_param/read_param`, `imago_zoom_levels` | `read_param` encola y espacia lecturas (40 ms) |
| IR | `imago_ir_palette_name/palette_colors`, `imago_ir_ffc`, `imago_ir_set_ffc_mode`, `imago_ir_set_isotherm` | Ver §8.6 |
| Tracking | `imago_tracker_set_function`, `imago_tracking_set_mode/start_at/point_at/stop` | Coordenadas normalizadas 0..1 |
| Otros | `imago_lrf_set_mode`, `imago_stream_set_bitrate/resolution` | |

Cada comando con acuse devuelve un `request_id` (> 0) que vuelve en un evento
`IMAGO_EVENT_COMMAND_RESULT` o `IMAGO_EVENT_PARAM_RESULT` (`result` 0 = aceptado, -1 = sin
respuesta tras los reintentos, otro = código MAVLink). Las apps registran una etiqueta por
`request_id` y muestran un aviso si falla.

**Eventos** (`imago_event_type`): `LINK_UP/DOWN`, `VIDEO_STARTED/STOPPED/ERROR`,
`VIDEO_RESYNC`, `COMMAND_RESULT`, `PARAM_RESULT`, `STREAM_URI`, `STATUS_TEXT`, `CAMERA_PARAM`
(valor leído o aceptado), `FOREIGN_COMMAND` (alguien más comanda el payload),
`ZOOM_CHANGED_EXTERNALLY`, `TRACKING_IGNORED`. Los callbacks corren en hilos del core: deben
volver rápido y no detener ni destruir la sesión.

**Estado** (`imago_state`): enlace y métricas MAVLink, identidad del payload, gimbal (roll,
pitch, yaw contra la nariz y contra el norte, *pan* acumulado ±320°, modo, control del gimbal
manager), rumbo del vehículo, cámara (vista, modo, zoom EO e IR, FOV, grabación,
almacenamiento), LRF, tracking y detecciones IA, IR (temperaturas mín./máx./media, tipo de
sensor, paleta, modo FFC), y video (fps, bitrate, pérdidas, jitter, huecos entre cuadros,
latencias, retraso, resyncs).

### Módulos internos

| Módulo | Responsabilidad |
| --- | --- |
| `mavlink/mavlink_link` | Socket UDP, modo directo/escucha, aprendizaje del peer, contadores, keep-awake |
| `mavlink/tlog_writer` | Grabación `.tlog` en su propio hilo |
| `payload/orus_protocol` | Funciones **puras**: constructores de mensajes y decodificadores (parámetros, índices de datos, detecciones, tablas de zoom y de paletas IR) |
| `payload/payload_model` | Estado del payload a partir de los mensajes recibidos; caducidad de datos |
| `payload/command_manager` | Comandos con ACK: emparejado por ID de comando o de parámetro, reintentos con timeout adaptativo (2 × RTT + 100 ms) |
| `rtsp/`, `rtp/` | Cliente RTSP propio (UDP con caída a TCP), SDP, depacketizer H.264/H.265 |
| `video/video_receiver`, `lag_tracker` | Entrega de cuadros, medición de retraso, reinicio de sesión, suavizado |
| `session/` | `Session` (orquesta todo), `capi.cpp` (frontera C, nunca deja escapar excepciones) |

## 6. Protocolo de la Orus L

Resumen; la referencia completa, con la fuente de cada dato y lo que falta verificar, está en
[`docs/protocol/orus-l.md`](protocol/orus-l.md).

- MAVLink v2 por UDP. El payload escucha en 14566. Con Herelink hay dos caminos: a la IP de la
  Orus L por el puente Ethernet (preferido, RTT ~130 ms) o el reenvío de Herelink a UDP 14550
  de los clientes Wi-Fi (RTT ~235 ms).
- Video RTSP, normalmente `rtsp://192.168.43.1:8554/fpv_stream` (Herelink).
- Ajustes de cámara con `PARAM_EXT_SET`/`PARAM_EXT_REQUEST_READ` (valores uint32), p. ej.
  `C_SOURCE` (vista), `C_T_PALETTE`, `C_V_ZM_MODE`.
- Comandos propios de Gremsy: `MAV_CMD_USER_4` (31013) al componente 26, `param1` = grupo,
  `param2` = función (tracking, FFC, ajustes de stream…).
- Valores en flujo: `MAV_CMD_SET_MESSAGE_INTERVAL` al componente 26 con `p1` = índice (no ID
  de mensaje); llegan como `DEBUG(ind, value)` (zoom, LRF, caja de tracking, vista, tipo y
  paleta IR, modo FFC, temperaturas IR…).
- Detecciones IA: `V2_EXTENSION` tipo `0x4001`.

## 7. Las apps

Las tres apps tienen la misma forma; los nombres de archivo se corresponden:

| Pieza | iOS (`apple/ImagoPayloadControl/`) | Android (`android/app/src/main/`) | Escritorio (`desktop/`) |
| --- | --- | --- | --- |
| Envoltura de la sesión | `Core/PayloadSession.swift` | `java/.../core/PayloadSession.kt` + `cpp/jni_core.cpp` | `src/core/PayloadSession.cpp` |
| Estado por tema | `Core/PayloadStatus.swift` | `core/PayloadStatus.kt` + `cpp/state_writer.cpp` | `src/core/PayloadStatus.{h,cpp}` |
| Catálogo de ajustes | `Core/CameraSettings.swift` | `core/CameraSettings.kt` | `src/core/CameraSettings.cpp` |
| Perfiles de conexión | `Core/ConnectionProfile.swift` | `core/ConnectionProfile.kt` | `src/core/ConnectionProfile.cpp` |
| Modelo (estado + acciones) | `App/AppModel*.swift` | `app/AppModel.kt` | `src/app/AppModel.{h,cpp}` (singleton QML `App`) |
| Video | `Video/VideoPipeline.swift` (VideoToolbox) | `cpp/video_decoder.cpp` (AMediaCodec) | `src/video/VideoDecoder.cpp` (FFmpeg) |
| Mandos | `Control/` (GameController) | `control/` | `src/control/` (SDL2) |
| Pantallas | `UI/` | `ui/` | `qml/Operator`, `qml/Pages` |

Patrones comunes:

- **Sondeo a 30 Hz**: el modelo copia el estado del core y asigna cada tema (gimbal, cámara,
  tracking, IR…) solo si cambió, para que la UI vuelva a pintar únicamente lo afectado.
- **Etiquetas pendientes**: cada comando registra `request_id → etiqueta`; al llegar el
  resultado, un fallo se muestra como aviso ("Palette was rejected (code 4)").
- **`cameraValues`**: diccionario `param_id → valor` alimentado por `IMAGO_EVENT_CAMERA_PARAM`
  (lecturas y valores aceptados). La pantalla de ajustes y la barra térmica leen de ahí.
- **Mandos**: las tasas del gimbal van directo del hilo del mando a la sesión, sin pasar por
  el hilo principal.

Particularidades:

- **iOS**: `@Observable` + SwiftUI; NAL en formato AVCC; `AVSampleBufferDisplayLayer` muestra
  cada cuadro al decodificarlo o lo descarta.
- **Android**: los cuadros nunca llegan a Java: en el hilo de video del core cada cuadro entra
  directo a un buffer de `AMediaCodec` (Annex B) y un hilo de salida pinta en un `SurfaceView`.
  El estado cruza JNI como un buffer serializado: el orden de campos de `state_writer.cpp` y de
  `PayloadSnapshot`/`PayloadStatus.kt` es un contrato; al cambiarlo se regenera el fixture con
  `android/tools/make_state_fixture.sh`. `NetworkBinder` ata el proceso a la red que llega al
  payload y mantiene un candado de Wi-Fi de baja latencia.
- **Escritorio**: FFmpeg en modo low-delay con hwaccel (D3D11VA/DXVA2 en Windows,
  VA-API/CUDA/VDPAU en Linux) y copia a memoria hacia `QVideoSink`. SDL2 para gamepads y HOTAS
  (entradas con nombres "pad:leftx", "joy:axis3"). F11 pantalla completa, rueda del ratón para
  zoom.

Las diferencias deliberadas entre plataformas (gestos, textos, hápticos…) están en la sección 2
de [`pending-android-desktop.md`](pending-android-desktop.md).

## 8. Funciones, de punta a punta

### 8.1 Video y latencia

- El core entrega cada cuadro al llegar su último paquete (sin jitter buffer). El decodificador
  de la plataforma lo toma de inmediato y la capa de pantalla lo muestra o lo descarta.
- **Guardia de retraso**: el core compara la llegada de cada cuadro contra su timestamp RTP;
  si durante 2 s todos llegan más tarde que el límite del perfil (500 ms por defecto), reinicia
  la sesión RTSP (`IMAGO_EVENT_VIDEO_RESYNC`), lo que vacía colas ajenas (Herelink, radio,
  Wi-Fi). La app marca el video como atrasado.
- **Suavizado** (`video_smoothing_ms`, 0/50/100/200 ms por perfil): reparte en su ritmo los
  cuadros que llegan en ráfaga, a cambio de hasta ese retraso (`display_delay_ns`).
- **Keep Wi-Fi awake**: un datagrama de 1 byte cada 33 ms al puerto discard del peer MAVLink
  para que el Wi-Fi de la tablet no dormite (ráfagas de ~410 ms vistas en un iPad).
- **Lugar del video (iOS)**: la cámara dibuja su OSD en el borde izquierdo de la imagen, bajo
  el riel de cámara. Con `AppModel.videoClearOfCameraRail` (opción "Keep the picture clear of
  the camera rail", `UserDefaults` `ui.videoClearOfCameraRail`, activada por defecto)
  `OperatorView` mide el borde derecho del riel en coordenadas de pantalla (`EdgeReader`,
  `.global`) y aplica ese margen izquierdo a `VideoView` y a `VideoOverlay` antes de
  `ignoresSafeArea()`. Los dos calculan el rectángulo de la imagen (aspect fit) en el mismo
  marco, así que los toques, las cajas de IA y la retícula siguen alineados. Android y
  escritorio aún no lo tienen.

### 8.2 Gimbal

- Tres protocolos por perfil: `GIMBAL_DEVICE_SET_ATTITUDE` (como el SDK de Gremsy),
  `DO_MOUNT_CONTROL`, o el **gimbal manager** del autopiloto PX4 (la app toma el control
  primario al conectar y lo devuelve al desconectar; con él, "Center" es un setpoint 0/0).
- El *pan* sigue el yaw contra la nariz a través de ±180° para mostrar cuánto falta para el
  tope de ±320°; aviso desde 290°.
- Joystick y sticks: velocidad, curva y zona muerta configurables; la velocidad baja al hacer
  zoom.

### 8.3 Zoom EO

El slider usa los niveles propios de la cámara (`C_V_ZM_CB_LV`/`C_V_ZM_SR_LV`, tablas en
`orus_protocol.cpp`), paceados a uno cada 150 ms con repetición final. Los botones hacen zoom
continuo (un toque dura al menos 300 ms). El core avisa si el zoom se mueve sin que la app lo
pida.

### 8.4 Tracking, Eagle Eye y detección IA

Tocar el video envía un punto normalizado; dentro de una caja de IA se envía esa caja. El
core verifica 1.5 s después de un "stop" o "punto" aceptado si el tracker obedeció
(`IMAGO_EVENT_TRACKING_IGNORED`). Un punto cuenta como obedecido si el tracker recién arrancó,
si la caja quedó cerca del punto o si el gimbal giró 2° o más (con un punto lejano el gimbal
trae el objetivo al centro antes de la verificación); prueba con `Options::centreTarget` del
simulador. La detección IA se activa con `TRACK_MODE` = 1 y las cajas
llegan por `V2_EXTENSION`.

### 8.5 Diagnóstico

HUD de red (bitrate, fps, pérdidas, jitter, huecos entre cuadros, latencias, RTT, comandos de
otros) y grabación `.tlog` (abrible con QGroundControl, MAVExplorer o pymavlink; incluye una vez
por segundo las métricas de la app como `NAMED_VALUE_FLOAT`).

### 8.6 Barra térmica (IR)

Visible mientras la vista es **IR** o **IR + EO**. Recorrido completo:

| Capa | Pieza |
| --- | --- |
| Core, estado | `ir_temp_min/max/mean_c` (índices 25-27), `ir_camera_type` (21), `ir_palette` (22), `ir_ffc_mode` (23), `ir_zoom_level` (1); intervalos pedidos en `kDataRates` (`session.cpp`) |
| Core, comandos | `imago_ir_ffc` (`USER_4` 2/7), `imago_ir_set_ffc_mode` (2/6), `imago_ir_set_isotherm`. La app de iOS no usa FFC desde 2026-10-16 (la cámara lo hace sola y el operador casi nunca lo necesita); siguen en el core, en `imago-cli` (`ffc`, `ffcmode`), en Android y en escritorio |
| Core, datos | `imago_ir_palette_name/colors`: nombres de Gremsy por núcleo térmico (F1/G1) en `orus_protocol.cpp`. La Orus L tiene el G1 (prueba de campo 2026-10-16; colores G1 tomados de sus imágenes). Qué valor de `ir_camera_type` es F1 no se conoce: `isF1()` devuelve siempre falso, así que todos los tipos usan G1 (el HUD muestra el tipo) |
| App, modelo | iOS `App/AppModel+Thermal.swift`: al entrar en vista IR lee `C_T_PALETTE`, `C_T_ZOOM`, `C_T_ISO_M`, `C_T_G`, `C_T_RG0_TMP`, `C_T_RG1_TMP`; copia a `cameraValues` los cambios de paleta y zoom que reporta el flujo de datos; recuerda la última banda de isoterma |
| App, UI | iOS `UI/ThermalBar.swift` (barra, escala de temperatura, panel de isoterma) y `UI/OperatorView.swift` (posición abajo a la derecha: desde el borde derecho del indicador de actitud, que queda al centro, hasta el borde de la pantalla, o hasta el riel del gimbal si este baja hasta la barra, medido con `EdgeReader`; ver `thermalBarInsets`). `ThermalBar` elige con `ViewThatFits` la variante que cabe: paleta con nombre → solo la muestra → zoom, FFC e isoterma en un menú "IR" → sin escala. Android y escritorio aún usan el diseño anterior (entre los rieles, actitud a la derecha); ver `pending-android-desktop.md` |
| Android | `app/AppModel.kt` (sección "Thermal bar"), `ui/ThermalBar.kt`, `ui/OperatorScreen.kt`; JNI `irPaletteName/irPaletteColors/irFFC/irSetFFCMode/irSetIsotherm` en `cpp/jni_core.cpp`; campos IR en `state_writer.cpp` ↔ `PayloadStatus.kt` |
| Escritorio | Lógica probada en `src/core/Thermal.{h,cpp}` (`tests/test_logic.cpp`), textos de avisos en `src/core/PayloadMessages.{h,cpp}`, QML `qml/Operator/ThermalBar.qml`, `TemperatureScale.qml`, `PaletteStrip.qml`, `IsothermPanel.qml` |

**Isoterma.** Gremsy documenta la ganancia (`C_T_G`) y la isoterma (`C_T_ISO_*`,
`C_T_RG<n>_*`) solo para su núcleo térmico F1. La app la ofrece únicamente si la cámara
responde a la lectura de `C_T_ISO_M`. `imago_ir_set_isotherm` escribe cada ajuste una sola vez
(unidad Celsius, regiones 0 y 2–5 apagadas, umbrales `RG0_TMP` = mínimo y `RG1_TMP` = máximo,
región 1 lineal de negro a rojo y por último `C_T_ISO_M` = 1), de modo que un reintento tardío
no puede deshacer un ajuste posterior; por encima de 150 °C también pone ganancia baja. No
existe ajuste documentado para fijar el rango mín./máx. de la paleta: la isoterma es la forma
de resaltar una banda.

`orus-sim` simula paleta, zoom IR, FFC, ganancia e isoterma; `--no-isotherm` simula un núcleo
sin esos ajustes (no responde lecturas y rechaza escrituras).

## 9. Herramientas

### orus-sim

Simulador de la Orus L: MAVLink en UDP 14566 y RTSP en 8554.

```sh
./build/tools/orus-sim/orus-sim --video tools/orus-sim/testdata/bars-720p30.h264
```

| Opción | Efecto |
| --- | --- |
| `--port N`, `--rtsp-port N` | Puertos |
| `--video clip.h264`, `--fps N` | Video real (sin `--video` los cuadros son sintéticos y no decodificables) |
| `--push IP:PUERTO` | Envía primero, como el reenvío de Herelink |
| `--heading N` | Rumbo del vehículo que reporta el gimbal |
| `--autopilot` | Autopiloto tipo PX4 con gimbal manager |
| `--lag MS`, `--lag-every S`, `--lag-catch-up` | Simula video que se atrasa |
| `--burst N` | Cuadros en ráfagas, como un Wi-Fi que dormita |
| `--no-isotherm` | Núcleo IR sin ganancia ni isoterma |
| `--no-ack`, `--verbose` | Sin acuses; registro detallado |

### imago-cli

Cliente de terminal que usa solo la API C. Sirve con el simulador y con una Orus L real:
`imago-cli 192.168.144.240` (la Orus L detrás de Herelink; `192.168.55.1` es solo la IP por defecto del SDK), `imago-cli - --rtsp rtsp://192.168.43.1:8554/fpv_stream` (escucha),
`--tlog out.tlog`, `--dump out.h264`. Comandos interactivos: `rate`, `angle`, `mode`, `proto`,
`photo`, `rec`, `stop`, `zoom`, `zoomto`, `zoomrange`, `zoomsweep`, `view`, `track`, `eagle`,
`untrack`, `detect`, `lrf`, `param <ID> <valor>`, `get <ID>`, `ffc`, `ffcmode <0|1>`, `quit` (lista completa al inicio
de `tools/imago-cli/main.cpp`). El estado incluye una línea `ir` (tipo de sensor, paleta con
su nombre, zoom IR, modo FFC y temperaturas mín./media/máx.) y el pan del gimbal. Con
`--every 30` el estado sale cada 30 s y no estorba al escribir. Para saber si un payload tiene
isoterma: `get C_T_ISO_M` (si no hay respuesta, ese núcleo no la tiene; nuestra Orus L no la
tiene, 2026-10-16).

### tlog-bursts.py

`tools/tlog-bursts.py logs/*.tlog` resume huecos, retenciones largas (> 250 ms), pérdidas por
emisor y retraso, separando segundos en reposo y con actividad, más las métricas propias de la
app grabadas en el `.tlog`.

## 10. Pruebas e integración continua

- **Core**: `core/tests/` (doctest). Las pruebas de integración levantan `orus-sim` en un
  puerto libre y una sesión real contra él (`Fixture` en `test_session.cpp`). Correr también
  con ASan/UBSan y TSan antes de cambios de concurrencia.
- **Android**: pruebas JVM en `android/app/src/test/` (`LogicTest`, `PayloadSnapshotTest` con
  el fixture del estado) y lint.
- **Escritorio**: `desktop/tests/test_logic.cpp` (doctest) y prueba de humo con captura de
  pantalla contra `orus-sim`.
- **CI** (`.github/workflows/ci.yml`, en cada push): Core Linux GCC; Core ASan+UBSan; Core macOS
  + paquete Swift (macOS e iOS); app iOS sin firmar (dispositivo y simulador); Android (APK,
  pruebas, lint; artefacto `imago-payload-control-debug-apk`); escritorio Linux (pruebas, humo,
  capturas); Windows MSVC (core, escritorio, `windeployqt`, humo; artefacto
  `imago-payload-control-windows-x64`). El estado de los jobs se puede leer sin autenticación:
  `https://api.github.com/repos/luiszarate/imago_payload_app/actions/runs?head_sha=<sha>`.

## 11. Cómo hacer un cambio

1. **Protocolo primero en el core.** Si la función habla con la Orus L, va en
   `orus_protocol` (constructores puros) + `Session` + `capi.cpp` + `imago_core.h`, con su
   simulación en `orus-sim` y una prueba en `core/tests/`. Documentar el dato en
   `docs/protocol/orus-l.md` con su fuente, marcando *(unverified)* lo no comprobado en hardware.
2. **iOS después**, como referencia de comportamiento y de UI.
3. **Paridad.** Mientras no se pida explícitamente portar a Android y escritorio, todo cambio en
   iOS se anota en la sección 1 de `docs/pending-android-desktop.md` (comportamiento iOS,
   archivos iOS, qué falta en Android y en escritorio). Al portar, se quitan de ahí las filas
   resueltas; las diferencias deliberadas van a la sección 2.
4. **Manuales.** Actualizar este manual y el de usuario en el mismo commit.
5. **Compilar y probar** lo que se pueda localmente; el resto lo confirma el CI. Hacer `git
   fetch` antes de cada push: el repositorio recibe commits en paralelo.
6. **Textos de UI** en inglés; documentación del equipo en español; comentarios de código en
   inglés, explicando el porqué (hallazgos de campo con fecha).

## 12. Licencias

- El código de la app es propietario (Imago Aerospace).
- **PayloadSdk de Gremsy: GPLv3. No copiar su código**; solo describir el protocolo con
  palabras propias.
- MAVLink C headers: MIT. doctest: MIT. Fuentes Space Grotesk y Manrope: SIL OFL.
- Escritorio: Qt y FFmpeg bajo LGPLv3 con enlace dinámico (sin módulos solo-GPL, FFmpeg sin
  partes GPL); el paquete de Windows incluye los avisos en `licenses/`. SDL2: zlib.

## 13. Pendientes y puntos sin verificar

- Pruebas de campo de Android y escritorio contra una Orus L real (lista en
  `pending-android-desktop.md`, sección 4).
- Qué valor de `ir_camera_type` reporta el núcleo F1 (la Orus L tiene el G1). La Orus L
  probada no tiene ganancia ni isoterma por MAVLink (2026-10-16, `docs/protocol/orus-l.md`);
  su página web sí configura una isoterma de un umbral.
- Un segundo componente en el rango de cámara (100) aparece por Herelink y puede desviar los
  comandos de cámara; falta un tlog para saber quién es.
- Tabla de zoom combinado sobre el índice 10, flags de yaw en modo lock, `RC_MODE` 1: ver
  `docs/protocol/orus-l.md`.
- Grabación MP4 local en el dispositivo y ajustes térmicos radiométricos (TIFF, raw IR): no
  implementados.
