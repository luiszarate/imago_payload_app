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
- La app de **Mac** es la misma de iOS por Mac Catalyst (decisión del 2026-10-19): no hay que
  portarle nada; lo exclusivo de Mac va en `App/MacSupport.swift` y tras
  `#if targetEnvironment(macCatalyst)`.
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
| Web | core, `Session::webThread_` (`imago-web`) | Página de ajustes del payload, una petición a la vez |
| Media | core, `MediaManager` (`imago-media`, `imago-download`) | Lista, thumbnails y borrados en uno; descargas en otro, para que una descarga larga no detenga la lista |
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
| `tools/tlog-gimbal.py` | Comandos al gimbal contra su actitud en un `.tlog` |
| `docs/pruebas/` | Procedimientos de pruebas en tierra |
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
comprobación de compilación es el job de CI "iOS and Mac app (build, unsigned)", que compila
para iOS, el simulador y Mac Catalyst.

### macOS (Mac Catalyst)

El mismo target, destino **My Mac (Mac Catalyst)**; macOS 14+ (derivado de iOS 17), Apple silicon
e Intel. Ajustes en `apple/project.yml`: `SUPPORTS_MACCATALYST`, idiom de Mac
(`TARGETED_DEVICE_FAMILY` "1,2,6": controles a tamaño de Mac, sin la escala de 77 % del idiom
iPad), mismo bundle ID que iOS, hardened runtime y `Mac.entitlements` (sandbox, red cliente y
servidor —el modo escucha enlaza un puerto UDP—, USB y Bluetooth para mandos). La fase "Resizable
window on the Mac" quita del Info.plist de Mac `UIRequiresFullScreen` y las orientaciones solo
horizontales, que dejarían la ventana de tamaño fijo. Lo exclusivo de Mac, en
`App/MacSupport.swift`:

- `MacSupport.configureWindow`: tamaño mínimo 1000 × 640 y barra de título sin título.
- `SystemAwake`: `ProcessInfo.beginActivity(.idleDisplaySleepDisabled)` mientras hay conexión
  (en iOS, `isIdleTimerDisabled`).
- `ScrollZoomView`: rueda o desplazamiento de dos dedos sobre el video = zoom continuo, alto
  250 ms después del último paso (como escritorio). Es una vista encima de `VideoOverlay` cuyo
  `hitTest` solo acepta eventos `.scroll`, así que los clics llegan al overlay. El sentido
  (arriba = acercar) está por confirmar en una Mac.
- `MacSupport.showInFinder`: carpeta de registros (botón en el HUD) y `Documents/Exports` de
  los puntos marcados. Con sandbox, `Documents` está en
  `~/Library/Containers/aero.imago.payloadcontrol/Data/Documents`.
- `AppModel.scenePhaseChanged`: en Mac `.background` (ventana minimizada u oculta) no
  desconecta; `.inactive` (otra app al frente) sigue deteniendo gimbal y zoom.
- Menús (`ImagoPayloadControlApp.commands`): Settings… ⌘,; Payload → Connections… ⌘K,
  Recenter gimbal ⌘R, Stop tracking ⌘. (también con teclado en iPad, manteniendo ⌘).

Distribución interna notarizada: `apple/scripts/build-mac.sh` (archivo, exportación Developer
ID, `.dmg` firmado, `notarytool` y `stapler`). Necesita en la Mac el certificado "Developer ID
Application" y un perfil de `notarytool store-credentials`; detalles en la cabecera del script.
Nada de esto se ha probado aún en una Mac: la compilación la valida CI.

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
| Grabación | `imago_session_start_recording/stop_recording`, `imago_session_set_recording_detail` | `.tlog` del MAVLink: `IMAGO_RECORDING_FULL` (todo + métricas de la app, por defecto en el core) o `IMAGO_RECORDING_COMPACT` (ver §8.5) |
| Video | `imago_video_start/stop`, `imago_report_frame_presented`, `imago_now_ns` | Las apps reportan decodificado/mostrado para el HUD |
| Gimbal | `imago_gimbal_set_rate/set_angle/set_mode/set_protocol/take_control` | Tasa repetida a `gimbal_rate_hz` mientras no sea cero |
| Cámara | `imago_camera_set_mode/take_photo/set_recording/zoom/zoom_to/zoom_target/zoom_range/focus/autofocus/set_view/set_param/read_param`, `imago_zoom_levels` | `read_param` encola y espacia lecturas (40 ms) |
| IR | `imago_ir_palette_name/palette_colors`, `imago_ir_ffc`, `imago_ir_set_ffc_mode`, `imago_ir_set_isotherm` | Ver §8.6 |
| Tracking | `imago_tracker_set_function`, `imago_tracking_set_mode/start_at/point_at/stop` | Coordenadas normalizadas 0..1 |
| Web | `imago_web_read_ir_settings/set_isotherm/restart/get_ir_settings` | Ver §8.5c |
| Media | `imago_media_get_state/refresh/set_active/get_files/get_flights/request_thumbnail/copy_thumbnail/download/cancel_downloads/delete/set_storage` | Ver §8.5d |
| Otros | `imago_lrf_set_mode`, `imago_stream_set_bitrate/resolution` | |

Cada comando con acuse devuelve un `request_id` (> 0) que vuelve en un evento
`IMAGO_EVENT_COMMAND_RESULT` o `IMAGO_EVENT_PARAM_RESULT` (`result` 0 = aceptado, -1 = sin
respuesta tras los reintentos, otro = código MAVLink). Las apps registran una etiqueta por
`request_id` y muestran un aviso si falla.

**Eventos** (`imago_event_type`): `LINK_UP/DOWN`, `VIDEO_STARTED/STOPPED/ERROR`,
`VIDEO_RESYNC`, `COMMAND_RESULT`, `PARAM_RESULT`, `STREAM_URI`, `STATUS_TEXT`, `CAMERA_PARAM`
(valor leído o aceptado), `FOREIGN_COMMAND` (alguien más comanda el payload),
`ZOOM_CHANGED_EXTERNALLY`, `TRACKING_IGNORED`, `WEB_RESULT`, `MEDIA_RESULT` (`code` =
`imago_media_op`, `result` incluye `IMAGO_RESULT_CANCELLED`). Los callbacks corren en hilos del core: deben
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
| `payload/command_manager` | Comandos con ACK: emparejado por ID de comando o de parámetro, reintentos con timeout adaptativo (2 × RTT + 100 ms), espera mínima por comando (`minWaitNs`) y cierre por otra evidencia (`complete`) |
| `rtsp/`, `rtp/` | Cliente RTSP propio (UDP con caída a TCP), SDP, depacketizer H.264/H.265 |
| `video/video_receiver`, `lag_tracker` | Entrega de cuadros, medición de retraso, reinicio de sesión, suavizado |
| `web/http_client`, `orus_web`, `orus_media` | HTTP/1.1 propio (streaming, `Range`, `HEAD`, chunked); formularios de la página de ajustes; lista de media, nombres, fechas, miniatura EXIF y vuelos (funciones puras) |
| `session/` | `Session` (orquesta todo), `MediaManager` (archivos del payload), `capi.cpp` (frontera C, nunca deja escapar excepciones) |

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
  marco, así que los toques, las cajas de IA y la retícula siguen alineados. Android:
  `ui/OperatorScreen.kt` mide el borde del riel y aplica `videoInset` a la superficie de video
  y a `VideoOverlay` (preferencia `ui.videoClearOfCameraRail`). Escritorio: `videoInset` en
  `qml/Main.qml` y `AppModel.videoClearOfCameraRail` (QSettings).

### 8.2 Gimbal

- Tres protocolos por perfil: `GIMBAL_DEVICE_SET_ATTITUDE` (como el SDK de Gremsy),
  `DO_MOUNT_CONTROL`, o el **gimbal manager** del autopiloto PX4. Con el manager, las tres
  apps toman el control primario en cuanto oyen el estado y lo vuelven a tomar si queda en
  "nadie" sin que el operador lo haya soltado (cada 5 s como mucho; `keepGimbalControl` en
  `AppModel.swift`, `AppModel.kt` y `desktop/src/app/AppModel.cpp`); si lo tiene una
  estación de tierra (255 o componente 190), avisa y no lo toma. Lo devuelve al desconectar.
- "Center" (`IMAGO_GIMBAL_MODE_RESET`) es siempre `GB_MODE` = 4, el reset propio del payload,
  en los tres protocolos: la Orus L ignora el ángulo 0/0 (el cuaternión identidad) por
  cualquier camino (pruebas del 2026-10-27, `docs/pruebas/centrado-en-tierra.md`). Se manda
  **una sola vez** y lo da por aceptado el propio gimbal: la bandera NEUTRAL de
  `GIMBAL_DEVICE_ATTITUDE_STATUS` (~0.3 s) o una actitud a ≤3° de pitch y ≤5° de yaw de la
  nariz (`Session::checkRecentre`, `CommandManager::complete`), porque el acuse llega ~2 s
  tarde. Los demás `GB_MODE` esperan 3 s antes de reenviarse (`kGimbalModeAckWaitNs`, el
  `minWaitNs` de `CommandManager::add`) y su acuse no entra en el RTT medido.
- Solo cuentan como payload los componentes (26, cámara 100–105, gimbal) del sistema que
  anuncia el *payload manager* (26): la cámara del Herelink (42/100) ya no recibe comandos de
  cámara ni mantiene el enlace (`PayloadModel::isPayloadSender`). Antes de oír al 26 cuenta
  cualquiera; al oírlo se olvidan la cámara y el gimbal aprendidos de otro sistema.
- El *pan* sigue el yaw contra la nariz a través de ±180° para mostrar cuánto falta para el
  tope de ±320°; aviso desde 290°.
- Joystick y sticks: velocidad, curva, zona muerta y compensación por zoom configurables
  (`GimbalInputShaper` en `apple/…/Control/`, `android/…/control/` y `desktop/src/control/`, con
  los mismos campos JSON). La tasa se multiplica por zoom^−`zoomCompensation`: 0 no compensa, 1
  mantiene el paso de la imagen en pantalla, 0.5 (√zoom, lo de antes) es el valor por defecto.
  Los ajustes guardados sin ese campo lo toman por defecto. El 2026-10-28 los operadores
  encontraron la √zoom demasiado lenta con zoom máximo para seguir un blanco.

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
otros) y grabación `.tlog` (abrible con QGroundControl, MAVExplorer o pymavlink). Completa
(`IMAGO_RECORDING_FULL`): todo el MAVLink entrante y saliente y, una vez por segundo, las
métricas de la app como `NAMED_VALUE_FLOAT`. Compacta (`IMAGO_RECORDING_COMPACT`, decisión
del usuario 2026-10-16 para que los registros de todos los días no crezcan): `TlogFilter`
(`core/src/mavlink/tlog_filter.*`) deja fuera lo que no viene de los componentes del payload
(26, cámara, gimbal), guarda completos comandos, respuestas, ajustes y `STATUSTEXT`, y el resto
de flujos una vez por segundo por tipo (mensaje + índice `DEBUG` o comando repetido); sin
métricas de la app. Las tres apps usan la compacta salvo con "Detailed log (link debugging)"
(`detailedLog` en el `AppModel` de cada una; clave `log.detailed` en `UserDefaults`, en las
preferencias de Android y en QSettings), aplicada al crear la sesión y al cambiarla. Prueba: "a compact recording keeps the
payload's side" en `core/tests/test_session.cpp`; en `imago-cli`, `--compact`.

### 8.5c Ajustes por la página web del payload

La Orus L (firmware 3.0.5) tiene ajustes que solo existen en su página web (puerto 8000), entre
ellos la isoterma de su núcleo G1: un umbral, no la banda del F1. La página es HTML con
formularios clásicos, sin autenticación ni API JSON (reporte del 2026-10-16 en
`docs/protocol/orus-l.md`, "Web settings"). El core la maneja como un navegador:

- `core/src/web/http_client.*`: una petición HTTP/1.1 por conexión, con plazo y cancelación.
- `core/src/web/orus_web.*`: lee los valores del formulario (`<input value>`, `<option
  selected>`, tolerante al HTML roto de la página) y arma los cuerpos de "Isotherms Regions"
  (`IsoTherms_Status`, `Threshold`, `Measurement_Range`, `submit=Apply`) y de reinicio
  (`Restart_Service=1` / `Reboot_Service=1` a `/setup-systems`).
- `Session`: un hilo propio (`imago-web`), una petición a la vez (el servidor de la cámara es
  el de desarrollo de Werkzeug), nunca en la ruta de control ni de video. API
  `imago_web_read_ir_settings`, `imago_web_set_isotherm` (guarda y relee),
  `imago_web_restart`, `imago_web_get_ir_settings`; cada una termina con
  `IMAGO_EVENT_WEB_RESULT`. Host: `imago_config.web_host` o la IP del payload; puerto
  `web_port` (0 = 8000).
- orus-sim sirve una página propia con los mismos nombres de campo (`tools/orus-sim/web_server.*`,
  `--web-port`, 8000 por defecto); pruebas "the isotherm through the payload's web settings
  page" y "web settings: form values". `imago-cli`: `web`, `iso <0|1> <C> [0|1]`, `restart
  service|system`.
- iOS: `WebIsothermSection` en `UI/SettingsView.swift` y `WebIsotherm` / `readWebIsotherm` /
  `saveWebIsotherm` en `App/AppModel+Thermal.swift`. Sin botón de reinicio: activar la isoterma
  y cambiar el umbral se ve al guardar (prueba de campo 2026-10-19). `imago_web_restart` sigue
  en el core y en el CLI. El reinicio del servicio VIO (`Restart_Service`) sí está en la app:
  `PayloadServiceSection` (Settings → Camera → Payload, con confirmación) y
  `AppModel.restartPayloadService`, para el tracker que deja de obedecer en sesiones largas
  (2026-10-15 y 2026-10-19); el aviso `TRACKING_IGNORED` lo sugiere. Que lo resuelva está sin
  verificar.
- Android: JNI `webReadIRSettings/webSetIsotherm/webRestart/webIRSettings` y `web_host` en
  `cpp/jni_core.cpp`; `PayloadSession.kt` (evento `WebResult`), `AppModel.kt` (sección "Web
  settings") y `ui/SettingsScreen.kt`. Escritorio: `src/core/PayloadSession.*`,
  `src/app/AppModel.*` (`readWebIsotherm`, `saveWebIsotherm`, `restartPayloadService`), textos
  en `src/core/PayloadMessages.*` y `qml/Pages/SettingsPage.qml`. En las tres, `web_host` es la
  IP del payload del perfil, también en modo escucha.

La página tiene también acciones peligrosas sin protección (`Format_Trigger`, `Factory_Reset`):
el core no las implementa.

### 8.5d Archivos multimedia del payload

Las fotos y videos que guarda la Orus L se listan, descargan y borran por su servidor web
(puerto 8000, el mismo host que §8.5c): `GET /list-file` (HTML con un enlace `/delete/<nombre>`
por archivo), `GET /download/<nombre>` (acepta `Range`), `GET /delete/<nombre>` (responde 302
haya borrado o no). Lo medido en nuestra Orus L el 2026-10-20 está en
`docs/protocol/orus-l.md`, "Media files": cada disparo crea `IMG_EO_<n>.jpeg` (~10 MB) e
`IMG_IR_<n>.jpeg` (<1 MB) con el mismo número, los videos `VID_EO_<n>.mp4` / `VID_IR_<n>.mp4`,
sin fecha en el nombre (la hora es `Last-Modified`) y sin miniaturas embebidas.

- `core/src/web/orus_media.*` (puras, probadas en `test_orus.cpp`): `parseMediaListing`
  (nombres, formulario de login, selector de almacenamiento), `mediaKindOf` / `isThermalName`,
  `shotOf` (pares EO/IR del mismo disparo), `timeFromName` / `timeFromExif` (formato Exif y el
  ISO con zona horaria que escribe la Orus) / `timeFromHttpDate`, `readJpegExif` (miniatura embebida y
  `DateTimeOriginal` a partir del inicio del JPEG, dice cuántos bytes faltan) y
  `groupIntoFlights` (más de 30 min sin capturas = otro vuelo).
- `core/src/web/http_client.*`: `httpStream` entrega el cuerpo por partes (a disco en las
  descargas), con plazo de inactividad en vez de plazo total, `Range`, `HEAD`, `Content-Range`,
  `Last-Modified` y chunked; `httpRequest` (la página de ajustes) usa la misma base.
- `core/src/session/media_manager.*`: dos hilos propios. `imago-media` lee la lista, borra,
  cambia de almacenamiento y, en segundo plano y del más nuevo al más viejo, averigua tamaño,
  fecha y miniatura de cada archivo: de una foto pide los primeros 256 KiB con `Range` y corta
  en cuanto tiene el segmento EXIF (unos KB en la Orus); una foto sin miniatura y de hasta
  1.5 MiB se baja entera (las IR); videos y TIFF, `HEAD`. Una EO con par usa la miniatura de su
  IR (`thumbnail_from_pair`); al pedir la miniatura de una EO, su IR pasa al frente.
  `imago_media_file` trae `camera`, `pair` (el otro archivo del disparo) y `primary` (el archivo
  que representa al disparo: la EO de un par); los vuelos cuentan disparos. Un borrado se
  comprueba con `HEAD /download/<nombre>` (404 = borrado), nunca con `HEAD /delete/`. Los archivos que la app pide (`imago_media_request_thumbnail`)
  pasan al frente; `imago_media_set_active(false)` pausa ese trabajo de fondo. Miniaturas en
  memoria hasta 48 MiB (se descartan las menos usadas). `imago-download` baja un archivo a la
  vez a `<ruta>.part` y lo renombra al terminar; cancelar corta el actual y vacía la cola.
  `imago_media_state.revision` cambia con cada novedad: las apps releen la lista solo entonces.
- Hora de captura: del nombre, si no del EXIF, si no del servidor (`Last-Modified`, en UTC); en
  la Orus los dos últimos coinciden y su reloj estaba bien el 2026-10-20.
- orus-sim: `tools/orus-sim/media_library.*` (tres vuelos inventados con los nombres, pares,
  tamaños, fechas y orden de la Orus real; las EO se rellenan hasta ~10 MB), fotos EO/IR de la
  misma escena en `tools/orus-sim/testdata/media/` hechas por `make_media.py` y embebidas en el
  binario por `embed_media.cmake`. La página de la lista copia la forma de la real. El servidor web
  del simulador atiende cada conexión en su hilo, con `Range`/`HEAD`; `--media-rate` limita la
  descarga en kB/s. Prueba "the payload's media files through its web server".
- `imago-cli`: `--web host[:puerto]`, `media`, `media show`, `media get <nombre> [carpeta]`,
  `media rm <nombre>`, `media cancel`.
- Estructura de la pantalla ("Flight rolls", elegida por el usuario con el skill de diseño
  `impeccable`; contrato en `.impeccable/surfaces/desktop-qml-pages-mediawindow-qml.md`):
  vuelos a la izquierda con su tira de tiempo, cuadrícula del vuelo, barra de selección que se
  vuelve fila de transferencia. Al abrir: `imago_media_set_active(true)` + `imago_media_refresh`;
  al cerrar, `set_active(false)`. Las apps sondean `imago_media_get_state` (~7-8 Hz) y releen
  la lista solo cuando cambia `revision`; las miniaturas se decodifican fuera del hilo
  principal, a tamaño de celda. Si el video está activo al empezar un lote, preguntan si
  pausarlo (`imago_video_stop`) y lo reanudan (`imago_video_start(NULL)`) al terminar el lote o
  al cerrar Media, solo si ellas lo pausaron. Descargas en una carpeta por vuelo
  ("yyyy-MM-dd HH-mm" del inicio en hora local, o "No capture time").
- iOS / Mac: `App/AppModel+Media.swift` (`MediaStore`, @MainActor @Observable: sondeo,
  selección, lote, pausa del video, borrados, vista previa, reintento a los 5 s),
  `Core/MediaFiles.swift` (tipos, formato, caché de miniaturas con CGImageSource, carpetas),
  `UI/MediaView.swift` (NavigationSplitView en ancho regular, NavigationStack en iPhone),
  `UI/MediaPreview.swift`; botón en `UI/TopBar.swift`, `fullScreenCover` en
  `UI/OperatorView.swift`, menú **Payload → Media…** (⇧⌘M) en `ImagoPayloadControlApp.swift`,
  envoltorios en `Core/PayloadSession.swift` (detener y reanudar el video corre en una cola
  serial propia porque detener espera al hilo de video). Descargas en `Documents/Media/`
  (visible en Archivos), vista previa en `Caches/MediaPreview/`.
- Escritorio: `src/core/MediaLibrary.*` (lógica sin Qt Quick: textos, rutas, posiciones en la
  tira, selección clic/Ctrl/Mayús, contador del lote; probada en `tests/test_logic.cpp`),
  `src/app/MediaModel.*` (`App.media`, modelos de vuelos y archivos que solo avisan las filas
  que cambian, `MediaImageProvider` asíncrono con 2 hilos y caché de 48 MB),
  `qml/Pages/MediaWindow.qml` y `qml/Media/*.qml`; ventana propia (Ctrl+M en `Main.qml`),
  carpeta en `media.folder` del ini (por omisión `<Imágenes>/Imago Payload Control`). Opciones
  de desarrollo `--media`, `--media-size`, `--media-folder`, `--media-demo
  select|download|pause|ask|delete|preview` (ver `desktop/README.md`); el job desktop-linux del
  CI guarda `shots/desktop-linux-media.png`.
- Dentro de un vuelo la cuadrícula va en orden de captura, del más viejo al más nuevo (se lee
  como la tira); los vuelos, del más nuevo al más viejo. En iPad y Mac, ⌘-toque y ⇧-toque se
  leen del teclado con `GCKeyboard` al terminar el toque, porque `TapGesture.modifiers` solo
  existe en macOS nativo (error en Xcode, 2026-10-20).
- Capturas del CI: `App/LaunchHook.swift` (solo actúa con `-imagoConnect <ip>`, más
  `-imagoRtsp`, `-imagoOpenMedia`, `-imagoSelect <n>`) conecta un perfil "Launch hook" y abre
  Media; el job "iOS and Mac app" arranca orus-sim y sube `apple-screenshots` (iPad, iPhone y
  Mac). Esos pasos no hacen fallar el job; la compilación sí.
- Android: pendiente (`pending-android-desktop.md`, sección 1).

### 8.5b Objetivo y puntos marcados

El payload calcula el punto al que apunta la cámara (la línea TARGET de su OSD) y lo publica
en sus valores de datos 10–12 (lon, lat, alt), que el core pide cada 500 ms. `imago_state`
los expone como `target_valid`, `target_lat_deg`, `target_lon_deg` y `target_alt_m`; es válido
si llegó en los últimos 3 s y no es 0, 0 (sin GPS). Son `float` de 32 bits: ~1 m de
resolución. iOS: `TargetPosition` (`Core/PayloadStatus.swift`, redondeado a 5 decimales), chip
`TargetChip` en `UI/TopBar.swift` (copiar, compartir, abrir en Mapas, marcar), puntos marcados
en `App/AppModel+Target.swift` (`Documents/marked-points.json`, exportación CSV y KML) y la lista
`MarkedPointsView` en `UI/SettingsView.swift`. Android: `target_*` cruza JNI en
`state_writer.cpp` ↔ `PayloadStatus.kt` (`TargetPosition`; fixture regenerado), chip en
`ui/TopBar.kt`, puntos en `app/MarkedPoints.kt` (`files/marked-points.json`, exportación por
el FileProvider en `cache/Exports/`) y `MarkedPointsPage` en `ui/SettingsScreen.kt`.
Escritorio: `src/core/MarkedPoints.*` (mismo JSON que iOS, CSV y KML; probado en
`tests/test_logic.cpp`), chip en `qml/Operator/TopBar.qml`, lista en `SettingsPage.qml`. orus-sim pone el objetivo a 412.5 m en el rumbo
de la cámara; prueba "the target position the payload works out".

### 8.6 Barra térmica (IR)

Visible mientras la vista es **IR** o **IR + EO**. Recorrido completo:

| Capa | Pieza |
| --- | --- |
| Core, estado | `ir_temp_min/max/mean_c` (índices 25-27), `ir_camera_type` (21), `ir_palette` (22), `ir_ffc_mode` (23), `ir_zoom_level` (1); intervalos pedidos en `kDataRates` (`session.cpp`) |
| Core, comandos | `imago_ir_ffc` (`USER_4` 2/7), `imago_ir_set_ffc_mode` (2/6), `imago_ir_set_isotherm`. La app de iOS no usa FFC desde 2026-10-16 (la cámara lo hace sola y el operador casi nunca lo necesita); siguen en el core y en `imago-cli` (`ffc`, `ffcmode`); Android y escritorio tampoco lo usan desde 2026-10-19 |
| Core, datos | `imago_ir_palette_name/colors`: nombres de Gremsy por núcleo térmico (F1/G1) en `orus_protocol.cpp`. La Orus L tiene el G1 (prueba de campo 2026-10-16; colores G1 tomados de sus imágenes). Qué valor de `ir_camera_type` es F1 no se conoce: `isF1()` devuelve siempre falso, así que todos los tipos usan G1 (el HUD muestra el tipo) |
| App, modelo | iOS `App/AppModel+Thermal.swift`: al entrar en vista IR lee `C_T_PALETTE`, `C_T_ZOOM`, `C_T_ISO_M`, `C_T_G`, `C_T_RG0_TMP`, `C_T_RG1_TMP`; copia a `cameraValues` los cambios de paleta y zoom que reporta el flujo de datos; recuerda la última banda de isoterma |
| App, UI | iOS `UI/ThermalBar.swift` (barra, escala de temperatura, panel de isoterma) y `UI/OperatorView.swift` (posición abajo a la derecha: desde el borde derecho del indicador de actitud, que queda al centro, hasta el borde de la pantalla, o hasta el riel del gimbal si este baja hasta la barra, medido con `EdgeReader`; ver `thermalBarInsets`). `ThermalBar` elige con `ViewThatFits` la variante que cabe: paleta con nombre → solo la muestra → zoom e isoterma en un menú "IR" → sin escala. Android hace lo mismo con un `SubcomposeLayout` en `ui/ThermalBar.kt` (equivalente de `ViewThatFits`) y escritorio por ancho disponible en `qml/Operator/ThermalBar.qml` |
| Android | `app/AppModel.kt` (sección "Thermal bar"), `ui/ThermalBar.kt`, `ui/OperatorScreen.kt`; JNI `irPaletteName/irPaletteColors/irSetIsotherm` en `cpp/jni_core.cpp`; campos IR en `state_writer.cpp` ↔ `PayloadStatus.kt` |
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
| `--web-port N` | Puerto del servidor web (ajustes y media), 8000 por defecto |
| `--media-rate KB` | Limita las descargas de media a ese número de kB/s, como un enlace de datos |
| `--no-ack`, `--verbose` | Sin acuses; registro detallado |

### imago-cli

Cliente de terminal que usa solo la API C. Sirve con el simulador y con una Orus L real:
`imago-cli 192.168.144.240` (la Orus L detrás de Herelink; `192.168.55.1` es solo la IP por defecto del SDK), `imago-cli - --rtsp rtsp://192.168.43.1:8554/fpv_stream` (escucha),
`--tlog out.tlog`, `--dump out.h264`, `--sysid N` / `--compid N` (nuestra identidad MAVLink;
1/193 por defecto, la del SDK de Gremsy; para probar si el payload y el gimbal manager aceptan
otra). Comandos interactivos: `rate`, `angle`, `mode`,
`proto <1|2|3>` (3 = a través del gimbal manager del autopiloto), `take` / `release` (control
primario del gimbal manager), `photo`, `rec`, `stop`, `zoom`, `zoomto`, `zoomrange`, `zoomsweep`, `view`, `track`, `eagle`,
`untrack`, `detect`, `lrf`, `param <ID> <valor>`, `get <ID>`, `ffc`, `ffcmode <0|1>`, `web`,
`iso`, `restart`, `media` (ver §8.5d), `help`, `quit`
(`--compact` graba el `--tlog` compacto; la IP por defecto es 192.168.144.240). `help` los lista
con su uso; la lista completa es la tabla `kCommands` de `tools/imago-cli/main.cpp`. El estado incluye una línea `ir` (tipo de sensor, paleta con
su nombre, zoom IR, modo FFC y temperaturas mín./media/máx.), el pan del gimbal y quién tiene
el control primario del gimbal manager (`gimbal manager control=`); sale cada 3 s, o cada
`--every N` segundos. Para saber si un payload tiene
isoterma: `get C_T_ISO_M` (si no hay respuesta, ese núcleo no la tiene; nuestra Orus L no la
tiene, 2026-10-16).

**Entrada y salida** (`tools/imago-cli/console.*`). En una terminal, la última línea es la de
entrada (`imago> `) y todo lo demás (el estado, las líneas `[event]`, las respuestas) se desplaza
por encima sin cortar lo que se escribe: cada impresión, venga del hilo del estado o de los
callbacks del core, toma un mutex, borra la línea de entrada, escribe y la vuelve a dibujar. Cada
comando enviado queda en la salida con el prompt delante, así el scrollback muestra qué se mandó
y qué respondió. No se usa una región de desplazamiento (DECSTBM) porque varias terminales no
guardan en el scrollback lo que sale de ella. La terminal queda en modo raw (termios) mientras
corre y se restaura al salir, también con `kill` o al cerrar la ventana.

- **Tab** completa el nombre del comando y después: los subcomandos de `media` (`get`, `rm`,
  `cancel`, `show`), los archivos de `media get` / `media rm` (los de la última lista de
  `media`), los IDs que el payload ya respondió en esta sesión para `param` / `get` (los de
  `CAMERA_PARAM` y `PARAM_RESULT`; los que solo se escribieron no cuentan) y `service` /
  `system` para `restart`. Con varias opciones completa lo común y un segundo Tab las lista;
  donde no hay lista muestra la línea de uso del comando (`proto ` + Tab explica 1, 2 y 3).
- **Historial**: flechas arriba/abajo o Ctrl-P / Ctrl-N, 500 líneas de la sesión (no se guarda
  en disco). Edición: izquierda/derecha, Inicio/Fin o Ctrl-A / Ctrl-E, Supr, Ctrl-U / Ctrl-K
  (borra hasta el inicio / el final), Ctrl-W (palabra anterior), Ctrl-L (limpia la pantalla).
- **Ctrl-C** borra la línea; en una línea vacía, igual que Ctrl-D o `quit`, cierra la sesión
  limpiamente y el `--tlog` queda completo. Ctrl-Z suspende; `fg` regresa.
- La línea de entrada nunca se parte: si es más ancha que la terminal se ve el tramo alrededor
  del cursor. Acepta UTF-8 (nombres de carpeta con acentos en `media get`).
- **Sin terminal** (stdin o stdout redirigidos, `TERM=dumb`, o Windows) lee y escribe línea por
  línea, como antes, para guiones: `printf 'get C_T_PALETTE\nquit\n' | imago-cli 127.0.0.1`.

### tlog-bursts.py

`tools/tlog-bursts.py logs/*.tlog` resume huecos, retenciones largas (> 250 ms), pérdidas por
emisor y retraso, separando segundos en reposo y con actividad, más las métricas propias de la
app grabadas en el `.tlog`.

### tlog-gimbal.py

`tools/tlog-gimbal.py logs/*.tlog [--from S] [--to S] [--ours COMPID]` pone en orden lo que se le mandó al
gimbal y lo que hizo: setpoints por ángulo de `GIMBAL_MANAGER_SET_ATTITUDE` y
`GIMBAL_DEVICE_SET_ATTITUDE` (las tasas solo cuando cambian), `DO_MOUNT_CONTROL`,
`DO_GIMBAL_MANAGER_CONFIGURE`, `GB_MODE`/`RC_MODE` y sus acuses, quién tiene el gimbal manager
y la actitud del gimbal dos veces por segundo. Al final, para cada ángulo o reset que mandamos,
dónde estaba el gimbal antes y 3 s después (el pitch es la señal limpia: el yaw contra la nariz
también cambia cuando el avión gira). Se escribió para la prueba de
[`docs/pruebas/centrado-en-tierra.md`](pruebas/centrado-en-tierra.md) y sirve también para medir
la tasa real del gimbal en [`docs/pruebas/tuning-gimbal.md`](pruebas/tuning-gimbal.md); usa el lector de
`tlog-bursts.py`. `--ours` marca nuestro componente si `imago-cli` corrió con `--compid`.

## 10. Pruebas e integración continua

- **Core**: `core/tests/` (doctest). Las pruebas de integración levantan `orus-sim` en un
  puerto libre y una sesión real contra él (`Fixture` en `test_session.cpp`). Correr también
  con ASan/UBSan y TSan antes de cambios de concurrencia.
- **Android**: pruebas JVM en `android/app/src/test/` (`LogicTest`, `PayloadSnapshotTest` con
  el fixture del estado) y lint.
- **Escritorio**: `desktop/tests/test_logic.cpp` (doctest) y prueba de humo con captura de
  pantalla contra `orus-sim`.
- **CI** (`.github/workflows/ci.yml`, en cada push): Core Linux GCC; Core ASan+UBSan; Core macOS
  + paquete Swift (macOS e iOS); app iOS y Mac sin firmar (dispositivo, simulador y Mac Catalyst); Android (APK,
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
- App de Mac: primera ejecución en una Mac (ventana redimensionable, sentido de la rueda,
  mandos USB/Bluetooth en sandbox, permiso de Red local, abrir carpetas en Finder) y primer
  `.dmg` notarizado con `build-mac.sh`.
- Qué valor de `ir_camera_type` reporta el núcleo F1 (la Orus L tiene el G1). La Orus L
  probada no tiene ganancia ni isoterma por MAVLink (2026-10-16, `docs/protocol/orus-l.md`);
  su página web sí configura una isoterma de un umbral.
- Un segundo componente en el rango de cámara (100) aparece por Herelink y puede desviar los
  comandos de cámara; falta un tlog para saber quién es.
- Tabla de zoom combinado sobre el índice 10, flags de yaw en modo lock, `RC_MODE` 1: ver
  `docs/protocol/orus-l.md`.
- Media del payload (§8.5d): medido en la Orus L el 2026-10-20; falta ver el TIFF
  radiométrico (no había ninguno), qué responde `/list-file` mientras arranca y si
  `PinPoint:*` del XMP es el punto que mira la cámara. Reanudar descargas con `Range` +
  `If-Range` es posible y no está hecho.
- Grabación MP4 local en el dispositivo y ajustes térmicos radiométricos (TIFF, raw IR): no
  implementados.
