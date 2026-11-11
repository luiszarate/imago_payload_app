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
├──────────────────────┴─────────────────────────────────┴──────────────────────────┤
│ quest/ (experimental)  OpenXR + GL ES nativo; AMediaCodec → SurfaceTexture → panel   │
└───────────────────────────────────────────────────────────────────────────────────┘
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
| `tools/tlog-rate-sign.py` | Sentido y proporción con que la actitud sigue a las tasas pedidas, por eje, en uno o varios `.tlog` |
| `docs/pruebas/` | Procedimientos de pruebas en tierra |
| `Package.swift` | Expone el core a Swift como módulo `ImagoCore` |
| `apple/` | App SwiftUI (iOS 17+, iPadOS); proyecto generado con XcodeGen (`apple/project.yml`) |
| `android/` | App Kotlin + Jetpack Compose (Android 7.0+, API 24), JNI al core |
| `desktop/` | App Qt 6 / QML para Windows 10/11 x64 y Linux |
| `quest/` | App experimental para Meta Quest 3: OpenXR nativo (loader de Khronos), Kotlin mínimo (perfiles y HUD); ver `quest/README.md` |
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

### Meta Quest 3 (experimental)

Mismos requisitos que Android (SDK 37, NDK 29.0.14206865, CMake 3.31.6); el loader OpenXR de
Khronos llega de Maven Central como módulo *prefab* (`org.khronos.openxr:openxr_loader_for_android`).

```sh
cd quest
./gradlew assembleDebug testDebugUnitTest lintDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk   # visor en modo desarrollador
```

`minSdk` y `targetSdk` 32 (Android 12L, lo que Meta pide), solo `arm64-v8a`. Los perfiles de
conexión viven en `profiles.json` en la carpeta externa de la app (sin pantalla de ajustes en
VR); formato y mandos en `quest/README.md` y en el manual de usuario §15.

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
| Head tracking | `imago_head_track_config_init/configure/set_mode/recentre/pose/pose_quaternion`, `imago_head_track_angles_from_quaternion` | El gimbal sigue la cabeza del operador (visor VR); ver §8.2b |
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
sensor, paleta, modo FFC), video (fps, bitrate, pérdidas, jitter, huecos entre cuadros,
latencias, retraso, resyncs) y head tracking (modo, pose fresca, siguiendo, retenido por el
tracker, objetivo pitch/yaw y error).

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
| `session/head_tracking` | Controlador **puro** del head tracking: poses, offset, filtro, lazo cerrado por tasa o setpoints por ángulo; `Session` le pasa la actitud del gimbal en cada vuelta del hilo de red y envía lo que devuelve |
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
- **Avisos (toasts)**: `show(text, style, kind)` con tres clases: `status` (enlace, comandos
  propios, foto, grabación, media: siempre), `others` (zoom movido sin la app, comandos de
  otros) y `diagnostic` (video por TCP, "catching up", STATUSTEXT del payload). Preferencias
  `ui.notices` (todos), `ui.noticesOthers` y `ui.noticesDiagnostics` (por defecto activadas);
  `show` descarta el aviso cuya clase está apagada. Se dibujan arriba a la derecha de la
  imagen, en una columna con el Network HUD (iOS `OperatorView`, Android `OperatorScreen`);
  en teléfono una línea más abajo para no coincidir con el badge. "Video over TCP…" se dice
  una vez por conexión (`tcpNoticeShown`). Escritorio igual (`src/app/AppModel.cpp`,
  `qml/Main.qml`, `qml/Operator/ToastView.qml`; preferencias en QSettings con las mismas claves).
- **Badge de video**: uno a la vez, por prioridad: `videoResyncing` → "CATCHING UP…";
  `!videoActive && videoHadPicture` → "VIDEO RECONNECTING…" (la última imagen se queda, el
  `ConnectPanel` solo aparece antes de la primera imagen de la conexión); `videoFrozenMs > 0`
  → "VIDEO FROZEN x.x s"; `videoLagMs ≥ 250` → "VIDEO x.x s BEHIND". `videoFrozenMs` se mide
  en la app, en el sondeo a 30 Hz, con el contador `rendered` del decodificador (iOS
  `VideoPipeline.DisplayCounters.rendered`, nuevo; Android `VideoRenderer.counters()`): si no
  cambia durante 1 s la imagen está congelada, llegue o no red; el reloj arranca en el primer
  `VIDEO_STARTED` de la conexión y en los reinicios sigue contando desde la última imagen
  mostrada (la vieja sigue en pantalla y `VIDEO_STARTED` llega antes que cualquier paquete).

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

**Quest (experimental, `quest/`).** No es un port de iOS sino un puesto de operador con pocas
funciones (fuera de la regla de paridad; `CLAUDE.md`). Casi todo es nativo:

| Pieza | Archivo | Qué hace |
| --- | --- | --- |
| Sesión OpenXR | `cpp/xr_app.{h,cpp}` | Hilo propio: loader (`xrInitializeLoaderKHR`), instancia (`XR_KHR_opengl_es_enable`, `XR_KHR_android_create_instance`, `XR_FB_passthrough` si existe), sesión, espacios LOCAL y VIEW, swapchains sRGB, acciones del perfil `oculus/touch_controller` (incluida la vibración), bucle de frames. Cada frame: pose de la cabeza → `Operator::headPose`, rayo del mando derecho contra el panel → puntero, `Operator::tick`, hápticos pedidos, render por ojo; con passthrough, la capa de la habitación va primero y la proyección se mezcla por alfa (fondo transparente) |
| Capas | `cpp/layers.h` | La interfaz en el espacio: la textura del overlay se muestra como cinco quads, cada uno un recorte (en unidades de imagen) a su profundidad: lo que va sobre la imagen (4 mm; 6 cm mientras hay una hoja abierta, `Operator::sheetOpen`), barras superior e inferior (2 cm) y los dos rieles (2.5 cm, girados 14° hacia el operador sobre el borde de la imagen). `buildLayers` las da de atrás hacia delante; el renderer las dibuja y `XrApp::pointerOn` interseca el rayo con **las mismas** (`intersect`), tomando la más cercana: lo que se ve es lo que se pulsa |
| Render | `cpp/gl_renderer.{h,cpp}` | EGL + GL ES 3 sin ventana (pbuffer); dos texturas externas (`ASurfaceTexture`): video y overlay; el panel como quad fijo en LOCAL con esquinas redondeadas (distancia al rectángulo redondeado en el fragment shader), las capas de la interfaz con mezcla premultiplicada y sin escribir profundidad, de atrás hacia delante; el fondo con passthrough es negro con alfa `roomDim` (oscurece la habitación; 0 la deja tal cual y 1 la apaga, y se cambia en vuelo con `kCmdRoomDim`: el botón de la barra superior recorre clara / atenuada / oscura como las apps de video del visor, cuyo control es del sistema y solo existe para apps 2D); el manifiesto declara `com.oculus.feature.PASSTHROUGH` (no obligatorio), que Horizon OS pide para ofrecer la extensión; el rayo del puntero se desvanece hacia donde cae. Las texturas son sRGB y el framebuffer también: el shader las pasa a lineal |
| Interfaz | `HudPainter.kt` + `cpp/hud_layout.h` | Estilo de la tableta dibujado a mano: Space Grotesk y Manrope (`res/font`, copiadas de Android), Material Icons como fuente con ligaduras (la del escritorio: un icono es su nombre), degradado magenta→púrpura en lo activo, paneles translúcidos y chips. Cristal: superficies al ~50 % con degradado vertical, brillo en el borde superior y sombra; cada control (`control`) suaviza su luz de hover (un degradado radial que sigue al puntero, recortado al control) y se encoge a 0.94 mientras el gatillo está apretado sobre él; los rieles se dibujan en una capa con alfa que baja a 0.4 tras 4 s sin puntero; la hoja Connections aparece con fundido y avisa al lado nativo para adelantar su capa. Se dibuja con lienzo por hardware en un buffer 1.5× más denso que el diseño (`BUFFER_SCALE`). Recibe un `Backend` (el nativo, o uno falso en `HudPainterTest`, que la renderiza a PNG con Robolectric en modo gráfico nativo). Interfaz de modo inmediato como la de las otras apps (barra superior, riel de cámara, riel del gimbal, barra inferior con la térmica, hoja "Connections"), pintada con `Canvas` sobre un overlay **más grande que la imagen** (márgenes `kMarginSide/Top/Bottom`, iguales en Kotlin y C++). El puntero llega en coordenadas de imagen (fuera de 0..1 sobre los rieles); cada pulsación del gatillo derecho es un clic contado por el lado nativo (`kClickCount`, `kClickX/Y`) que Kotlin resuelve contra sus botones, o contra la imagen (seguir ahí); los botones mantenidos (zoom) miran el nivel del gatillo. Los comandos vuelven por `NativeBridge.command` → `Operator::post` y se ejecutan en el hilo XR |
| Registro | `QuestActivity.logDir`, `Operator::connect` | Cada sesión graba su MAVLink en `files/logs/quest-<epoch>.tlog` (se conservan las 12 últimas): `adb pull /sdcard/Android/data/aero.imago.payloadcontrol.quest/files/logs` y `tools/tlog-gimbal.py` / `tlog-rate-sign.py` dicen qué se le mandó al gimbal y qué hizo |
| Pantalla que sigue | `XrApp::followHead` | Con head tracking activo y `panel.followHead`, la pantalla acompaña a la cabeza: su dirección desde la cabeza (yaw y pitch, sin roll) se suaviza con constante 0.10 s hacia donde mira la cabeza, partiendo de donde estaba; al apagar se queda. Las capas y el puntero usan el mismo `panel_` |
| Comodidad | `XrApp::updateVignette`, `Operator::tick` | Se probó colocar la pantalla donde el gimbal reporta que apunta ("imagen estable") y se quitó: en el visor iba a saltos, porque los reportes llegan pocas veces por segundo, y se sentía peor que el seguimiento simple (2026-11-11). **Viñeta**: uniforme `uVignette` del shader del video según la velocidad angular de la cabeza (de 25 a 110 °/s, ataque 80 ms, caída 450 ms). **Embrague**: grip izquierdo → `imago_head_track_hold`; retenido, la pantalla deja de seguir. El aviso de habitación oscura lo da `HudPainter` |
| Actitud | `HudPainter.drawAttitude` | El indicador de las otras apps (`AttitudeIndicator`), pintado a mano bajo el riel de cámara (`CAMERA_RAIL`), fuera de la capa que atenúa los rieles. Datos: `kYawValid`, `kYawNorth(Valid)`, `kPan(Valid)`, `kHeading(Valid)`, `kHfov` del snapshot (62..69; `kDetections` pasa a 70). Rumbo arriba si se conoce el yaw respecto a la nariz, norte arriba si solo el rumbo de la cámara. `theAttitudeDialShowsTheCameraAgainstTheAircraft` comprueba el cono y el avión por píxeles |
| Agarre | `HudPainter.drawHandles`, `XrApp::updateGrab` | Esquinas inferiores (escalar) y barra (mover) en el overlay; el clic en una manda `kCmdGrab` y soltar el gatillo lo termina. Nativo: al empezar guarda el punto agarrado en ejes del panel y su distancia sobre el rayo; **mover** lleva ese punto bajo el rayo (el stick derecho cambia la distancia, 1.5 m/s) con el panel siempre de cara a la cabeza y derecho; **escalar** multiplica el ancho por la razón entre la distancia actual del rayo al centro y la inicial. Al soltar, `Operator::setPanelGeometry` deja distancia, ancho y altura respecto a la mirada para el seguimiento y SCREEN. Mientras dura, el stick derecho no mueve el gimbal y la pantalla no sigue a la cabeza |
| Nitidez | `cpp/gl_renderer.cpp`, `xr_app.cpp`, `HudPainter.BUFFER_SCALE` | El HUD se veía pixelado en el visor (2026-11-11) por dos fallos del shader: precisión `mediump` en las coordenadas de una textura de miles de píxeles (bloques) y gamma aplicada sobre color premultiplicado (bordes del texto oscuros y dentados). Ahora `highp`, y el color se saca del alfa antes de linealizar. Además: multimuestreo 4× con `GL_EXT_multisampled_render_to_texture` si existe, buffers de ojo 1.25× lo recomendado y overlay a 2× |
| Operador | `cpp/operator.{h,cpp}` | Posee la `imago_session` y el `VideoDecoder` de Android (reutilizado por ruta desde `android/app/src/main/cpp/`); mandos → comandos del core (stick derecho con `imago_gimbal_set_fine_aim` a 2°/s y sin compensación de zoom cuando `gimbal_auto_speed` es 1, como las otras apps); flancos de botones; keep-control del gimbal manager; snapshot del HUD (`hud_layout.h`, contrato con `Hud` en `NativeBridge.kt`) |
| JNI | `cpp/quest_jni.cpp` | `start/stop/hudSnapshot/setVideoSize`; lee los `ProfileArgs` de Kotlin |
| Panel 2D | `PanelActivity.kt`, `PanelTheme.kt` | La app es **híbrida**: la actividad de lanzamiento (`com.oculus.intent.category.2D`) es una pantalla Compose con el tema de la tableta (`Theme`/`AppFont`, mismos valores que `android/…/ui/Theme.kt`): lista de perfiles, editor y **ENTER VR**, que lanza `QuestActivity` (categoría `VR`, tarea propia) con el índice del perfil. Nunca abre una sesión. El manifiesto pide ventana apaisada (`<layout android:defaultWidth="1280dp" …>`, `screenOrientation="landscape"`) y `PanelScreen` usa `BoxWithConstraints`: bajo 720 dp apila lista y ajustes en una columna (el visor abría la ventana del tamaño de un teléfono y la columna de ajustes quedaba sin ancho, 2026-11-11) |
| Actividad | `QuestActivity.kt` | Crea dos `SurfaceTexture` *detached* (video y overlay), carga `profiles.json`, arranca lo nativo, hilo del HUD a 15 Hz |
| Perfiles | `Profiles.kt` | `kotlinx.serialization`; valida; escribe los valores por defecto la primera vez |

Ruta del video: hilo de video del core → `AMediaCodec` (mismo decodificador que Android) →
`Surface(SurfaceTexture)` → `updateTexImage` en el hilo XR (siempre el cuadro más nuevo, sin
cola) → quad del panel. Nada pasa por Kotlin. El head tracking entrega el cuaternión de la
VIEW space en LOCAL al core cada frame (72–90 Hz) con `imago_head_track_pose_quaternion`; el
lazo cerrado corre en el hilo de red del core (§8.2b). El panel se coloca delante de la cabeza
al arrancar (yaw de la cabeza, un poco por debajo de la vista) y se queda fijo; LOCAL es el
espacio de referencia tanto del panel como del yaw de la cabeza.

## 8. Funciones, de punta a punta

### 8.1 Video y latencia

- El core entrega cada cuadro al llegar su último paquete (sin jitter buffer). El decodificador
  de la plataforma lo toma de inmediato y la capa de pantalla lo muestra o lo descarta.
- **Guardia de retraso**: el core compara la llegada de cada cuadro contra su timestamp RTP;
  si durante 2 s todos llegan más tarde que el límite del perfil (500 ms por defecto), reinicia
  la sesión RTSP (`IMAGO_EVENT_VIDEO_RESYNC`), lo que vacía colas ajenas (Herelink, radio,
  Wi-Fi). La app marca el video como atrasado.
- **Baches y transporte** (`video_receiver.cpp`, prueba de campo 2026-11 por Herelink,
  `docs/protocol/orus-l.md`): un bache sin paquetes solo reinicia la sesión RTSP a los 5 s
  (`kStallTimeoutNs`); reiniciar cuesta más que esperar y un PLAY durante el bache también
  fracasa. En transporte **Auto** la caída a TCP ocurre solo si UDP nunca entregó un paquete en
  la conexión y dos sesiones UDP seguidas terminan sin paquetes (`kNoPacketsBeforeTcp`); una
  vez que UDP funcionó, un silencio posterior se trata como enlace caído y se reconecta por
  UDP. Antes, un solo bache durante una reconexión dejaba el receptor en TCP para siempre y
  cada pérdida se convertía en retraso y en nuevos reinicios. `orus-sim --udp-hold MS`
  reproduce el bache (el servidor sigue generando cuadros pero no envía UDP; la sesión sigue
  viva) y `RtspServer::setUdpPaused` lo controla desde las pruebas (`test_video.cpp`: "stays
  on UDP after a hold", "a short hold keeps the session", "falls back to TCP when UDP never
  arrives").
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
- *Fine aim* (`core/src/session/fine_aim.{h,cpp}`, API `imago_gimbal_set_fine_aim(floor_dps)`,
  0 = apagado, por defecto en el core). La Orus L ignora tasas menores de ~2°/s y sigue con
  suavidad setpoints de ángulo lentos (pruebas del 2026-11-09, `docs/protocol/orus-l.md`). Con
  el piso puesto, una tasa del joystick (`imago_gimbal_set_rate`) con los dos ejes bajo el piso
  no se manda como tasa: el hilo de red mueve un objetivo que arranca donde apunta el gimbal (su
  actitud reportada, yaw contra la nariz) a la tasa pedida y lo manda como ángulo
  (`GIMBAL_MANAGER_SET_ATTITUDE` o `GIMBAL_DEVICE_SET_ATTITUDE`, con las banderas del estado) cada
  periodo del gimbal (30 Hz). El objetivo avanza en lazo abierto (no se ata a la actitud
  reportada), se queda en −90..+40 de tilt y ±179.5 de yaw, y nunca es 0/0 exacto. Ajustes de
  las pruebas de campo del 2026-11-09 (`logs/fino-20261109-1623`, `fino2-20261109-1658`, logs
  de la app ese día):
  - *Histéresis*: entra bajo el piso y sale solo arriba de piso × 1.25 (2.5°/s), para que una
    palanca cerca de 2°/s no alterne entre tasas y ángulos.
  - *Espera tras tasas*: si hace menos de 1.5 s salió una tasa que el gimbal obedece (≥ piso),
    manda tres stops y espera a que la actitud reportada se quede quieta (0.5 s sin pasar de
    0.3°, máximo 1.5 s); arranca desde ahí más lo que pidió la palanca mientras tanto. Antes
    arrancaba de una actitud atrasada mientras el gimbal seguía de largo, y lo jalaba atrás.
  - *Reposo y stop*: al soltar mantiene el ángulo al menos 1.2 s (hasta 2.5 s si el gimbal aún se
    mueve) y luego manda tres stops de tasa: lo último que queda es un stop. Volver a tocar la
    palanca durante el reposo continúa desde el mismo objetivo.
  - *Marco del yaw*: la Orus L toma el yaw de un ángulo en su marco "absoluto", el
    `yaw_absolute` de `MOUNT_ORIENTATION`, no contra la nariz. El modelo guarda
    `yaw_absolute − yaw` (`PayloadModel::angleYawOffset`, fresco 2 s) y el yaw enviado es el
    objetivo más ese valor. Midió −1.5 a −2.2° en una sesión y +15 a +18° en la siguiente.
    Una versión anterior ataba el objetivo a ±2° de la actitud reportada: con el desfase de 18°
    persiguió al gimbal y lo hizo girar sin fin hacia un lado (`fino2-20261109-1658`).
  `FineAim` es puro y vive bajo `gimbalMutex_`; calcula si el gimbal está quieto con su propia
  historia de actitudes (una muestra cada 30 ms). `applyGimbalRate` solo guarda la tasa, así que
  el hilo del mando no espera al modelo. Antes de soltar el gimbal manager
  (`imago_gimbal_take_control(false)`), al cambiar de protocolo desde el manager y al detener la
  sesión se mandan tres stops por el manager: PX4 sigue repitiendo su último setpoint aun
  después de soltarlo. No aplica con el protocolo V1 (la Orus L rechaza ángulos por
  `DO_MOUNT_CONTROL`) ni a las tasas del head tracking. Las tres apps lo encienden con **Fine
  aim** (`GimbalInputShaper.fineAim`, campo JSON `fineAim`, piso 2°/s) al conectar y al cambiar
  el ajuste: iOS `PayloadSession.setFineAim`, Android `NativeCore.gimbalSetFineAim` (JNI) y
  desktop `PayloadSession::setFineAim`. Pendiente de campo: hasta qué velocidad sigue los
  ángulos. Pruebas: `core/tests/test_fine_aim.cpp` (controlador puro, incluido que el objetivo
  nunca persigue al gimbal) y, en `test_session.cpp`, "under the gimbal's floor the joystick aims
  by angle" y "fine aim allows for the gimbal's absolute yaw and never runs off" (contra
  `orus-sim --rate-floor 2 --angle-yaw-bias -18`).
- *Auto Speed*: solo está en la página web GIMBAL (`GET /setup-gimbal`, `<select
  name="Auto_Speed">`, 1/0). El core la lee solo, en su hilo web, al subir el enlace y cada 30 s,
  sin evento (`WebJob::silent`), y la deja en `imago_state.gimbal_auto_speed` (−1 sin leer).
  Con Auto Speed encendido, las tres apps le pasan zoom 1× al joystick (`joystickZoom` en el
  `AppModel` de cada una): la compensación por zoom se aparta y Settings desactiva el control con
  "Off while the payload's Auto Speed is on". Android lo lee del búfer de estado JNI
  (`state_writer.cpp`, tras `gimbal_pan_near_limit`; `PayloadSnapshotTest` lo comprueba) y desktop
  de `GimbalStatus.autoSpeed`. Prueba: "the gimbal's Auto Speed
  from the payload's web page" (`orus-sim`: `WebServer::setAutoSpeed`).

### 8.2b Head tracking (el gimbal sigue la cabeza)

Para la versión experimental con visor (Meta Quest 3). Toda la lógica está en el core
(`core/src/session/head_tracking.{h,cpp}`, API `imago_head_track_*`): la app solo entrega la
orientación del visor (`imago_head_track_pose`, o `_pose_quaternion` con el cuaternión de
OpenXR: +X derecha, +Y arriba, −Z al frente) desde su hilo de tracking, sin esperar a nada.

- **Modo ANGLE (el de la Orus L): ángulos en lazo abierto.** El objetivo es simple: la
  cabeza gira N grados y el gimbal va a ese ángulo y se queda. El controlador manda setpoints
  de actitud, uno por período del gimbal (30 Hz) mientras la cabeza se mueve y uno cada 500 ms
  mientras está quieta. El setpoint sigue a la cabeza filtrada con una holgura de
  `deadband_deg`/2 (cabeza quieta → ángulo quieto) y nunca más rápido que `max_rate_dps` (un
  giro brusco es una rampa, no un salto). El yaw sale sumándole `yaw_absolute − yaw` de
  `MOUNT_ORIENTATION` (`PayloadModel::angleYawOffset`), el marco en que la Orus L toma los
  ángulos, igual que Fine aim; nunca 0/0 exacto. Se envían como los de Fine aim
  (`GIMBAL_MANAGER_SET_ATTITUDE` o `GIMBAL_DEVICE_SET_ATTITUDE`), sin acuse: el siguiente
  reemplaza a uno perdido. Con `DO_MOUNT_CONTROL` (protocolo V1) la Orus L rechaza ángulos:
  fallo **NO_ANGLES**. Si el gimbal queda a más de 15° del setpoint durante 6 s (sin control
  del gimbal manager, motores apagados) el fallo es **NO_MOTION**. Base: los ángulos
  escalonados cada 1/30 s se siguen exacta y suavemente (pruebas del 2026-11-09 y 2026-11-10).
- **Modo RATE: lazo cerrado por velocidad**, para gimbals que no aceptan ángulos. En cada
  vuelta del hilo de red `Session::serviceHeadTracking` toma la actitud que reporta el gimbal
  y deja en `holdGimbalRate` una tasa proporcional al error (`gain_dps_per_deg`, saturada a
  `max_rate_dps`, nula dentro de `deadband_deg`), que sale a `gimbal_rate_hz`. **No sirve bien
  con la Orus L**, y fue la causa de que las dos primeras pruebas en el visor (2026-11-09 y
  2026-11-10) acabaran con el gimbal girando de más hasta el tope y el aviso "gimbal moved
  away": con Autospeed apagado el gimbal arranca ~0.6 s tarde y sigue ~0.7 s tras el stop
  (2026-10-28), y un lazo proporcional con ese retardo solo es estable si ganancia × retardo
  es menor que ~1.5; con la ganancia inicial de 3 se pasaba más en cada vaivén. La ganancia
  por defecto es ahora 0.6 (lento pero estable), y como el gimbal ignora tasas menores de
  ~2°/s se queda a unos grados del objetivo. `orus-sim --rate-delay 700` lo reproduce y hay
  una prueba con ambas ganancias.
- **Signo y guardas del modo RATE** (`HeadTracking::watchAxis`): bajo una orden de
  dirección constante, en cuanto la actitud se ha movido 2° decide el signo de ese eje por
  el sentido en que se movió y lo mantiene el resto de la sesión (`imago_state.head_track_
  pitch/yaw_inverted`; `invert_pitch/yaw` solo eligen hacia dónde van esos primeros 2°). Una
  sesión de `imago-cli` del 2026-11-09 por el gimbal manager muestra el signo normal en yaw.
  **NO_MOTION**: una tasa de ≥ 2°/s pedida 3 s sin que la actitud se mueva 0.5°. **RAN_AWAY**:
  la actitud se fue 15° en sentido contrario bajo una orden constante. Un recentrado o cambiar
  de modo quitan el fallo.
- **Stop**: al apagar, al perder la pose o al fallar, lo último que se manda son tres tasas
  cero (`Session::applyHeadCommand`), también después de ángulos: el gimbal manager de PX4
  sigue reenviando su último setpoint (2026-11-09).
- **Escalado con el zoom** (`zoom_compensation`, 0..1; 0 por defecto en el core, 1 en la app
  Quest): el objetivo es `escala × cabeza + offset`, con `escala = zoom^−zoom_compensation`
  (zoom EO), el mismo criterio que `GimbalInputShaper` aplica a las tasas del joystick. En la
  primera prueba con movimientos reales de cabeza (2026-11-11) sin escalar, con zoom los giros
  eran demasiado amplios. Al cambiar la escala el offset absorbe la diferencia
  (`offset += (escala anterior − nueva) × cabeza`), así que hacer zoom no mueve el gimbal; el
  yaw de la cabeza se sigue desenvuelto a través de ±180 para que la escala no dé un salto
  ahí. A diferencia del joystick, se aplica también con Auto Speed activado: ese ajuste escala
  tasas y esto son ángulos. `imago_state.head_track_scale` dice el factor vigente.
- **Embrague** (`imago_head_track_hold`): mientras está retenido no se manda nada (un stop) y
  `head_track_held` lo dice; al soltar, el offset se vuelve a capturar contra el gimbal, como
  al activar, así que no hay salto aunque la cabeza haya girado entre tanto.
- **Offset**: al activar, la dirección actual de la cabeza se corresponde con la actitud
  actual del gimbal (no salta). `recentre` hace que la dirección actual de la cabeza sea la
  nariz y el horizonte; se aplica en la siguiente vuelta, con la pose que haya entonces. Un
  joystick (`imago_gimbal_set_rate`) mientras está activo **no pelea** con el lazo: desplaza
  el offset a esa tasa (*trim*).
- **Filtro**: primer orden con constante `smoothing_ms` (80), con el yaw envuelto a ±180.
  Límites: pitch `pitch_min/max_deg` (−90..40, el recorrido de tilt), yaw ±`yaw_range_deg`
  (150, para no cruzar el ±180 hacia el otro lado del recorrido de pan ±320).
- **Pausas**: sin pose durante `pose_timeout_ms` (500; visor quitado) se manda un stop y el
  gimbal se queda donde está, reanudando con la siguiente pose, sin perder el modo. Mientras
  el tracker del payload tiene un objetivo (`tracking_mode` activo o estado *tracked*) el head
  tracking espera (`head_track_held_by_tracker`). Apagar manda un stop y deja el gimbal.
- Estado en `imago_state`: `head_track_mode`, `_pose_fresh`, `_following`,
  `_held_by_tracker`, `_target_pitch/yaw_deg`, `_error_deg`, `_fault`,
  `_pitch/yaw_inverted`.
- Pruebas: `core/tests/test_head_tracking.cpp` (controlador puro con ticks sintéticos y un
  gimbal falso con signo y velocidad propios, e integración contra `orus-sim`: ángulos con
  `--angle-yaw-bias −18` y a través del gimbal manager, tasas con `--rate-scale 0.25`,
  `--rate-reversed`, `--rate-delay 700` y un gimbal que no se mueve).

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
| `--rate-scale X` | El gimbal gira a X veces la tasa pedida (0.25 ≈ la Orus L con Autospeed encendido, 2026-10-27) |
| `--rate-reversed` | El gimbal gira al revés de la tasa pedida, para probar que el head tracking lo detecta |
| `--rate-delay MS` | Las tasas (y su stop) hacen efecto MS ms tarde, como la Orus L con Autospeed apagado (~700); reproduce el head tracking por tasas pasándose del objetivo |
| `--rate-floor X` | El gimbal ignora tasas menores de X °/s (2 ≈ la Orus L, 2026-11-09) |
| `--angle-yaw-bias X` | Los setpoints de ángulo quedan X° a la derecha en yaw y `MOUNT_ORIENTATION` reporta `yaw_absolute = yaw − X`, como la Orus L (+2 y −18 el 2026-11-09) |
| `--autopilot` | Autopiloto tipo PX4 con gimbal manager |
| `--lag MS`, `--lag-every S`, `--lag-catch-up` | Simula video que se atrasa |
| `--burst N` | Cuadros en ráfagas, como un Wi-Fi que dormita |
| `--udp-hold MS` | Cada `--lag-every` segundos deja de enviar UDP durante MS ms con la sesión RTSP viva, como un salto Wi-Fi caído |
| `--no-isotherm` | Núcleo IR sin ganancia ni isoterma |
| `--web-port N` | Puerto del servidor web (ajustes y media), 8000 por defecto |
| `--media-rate KB` | Limita las descargas de media a ese número de kB/s, como un enlace de datos |
| `--no-ack`, `--verbose` | Sin acuses; registro detallado |

### imago-cli

Cliente de terminal que usa solo la API C. Sirve con el simulador y con una Orus L real:
`imago-cli 192.168.144.240` (la Orus L detrás de Herelink; `192.168.55.1` es solo la IP por defecto del SDK), `imago-cli - --rtsp rtsp://192.168.43.1:8554/fpv_stream` (escucha),
`--tlog out.tlog`, `--dump out.h264`, `--log salida.txt`, `--plain`, `--sysid N` / `--compid N` (nuestra identidad MAVLink;
1/193 por defecto, la del SDK de Gremsy; para probar si el payload y el gimbal manager aceptan
otra). Comandos interactivos: `rate`, `angle`, `mode`,
`proto <1|2|3>` (3 = a través del gimbal manager del autopiloto), `fine <°/s>` (fine aim: tasas
bajo ese piso salen como ángulos; 0 lo apaga), `take` / `release` (control
primario del gimbal manager), `photo`, `rec`, `stop`, `zoom`, `zoomto`, `zoomrange`, `zoomsweep`, `view`, `track`, `eagle`,
`untrack`, `detect`, `lrf`, `param <ID> <valor>`, `get <ID>`, `ffc`, `ffcmode <0|1>`, `web`,
`iso`, `restart`, `media` (ver §8.5d), `help`, `quit`
(`--compact` graba el `--tlog` compacto; la IP por defecto es 192.168.144.240). `help` los lista
con su uso; la lista completa es la tabla `kCommands` de `tools/imago-cli/main.cpp`. El estado incluye una línea `ir` (tipo de sensor, paleta con
su nombre, zoom IR, modo FFC y temperaturas mín./media/máx.), el pan del gimbal, Auto Speed (`auto speed=on|off|-`, de `imago_state.gimbal_auto_speed`) y quién tiene
el control primario del gimbal manager (`gimbal manager control=`); sale cada 3 s, o cada
`--every N` segundos. Para saber si un payload tiene
isoterma: `get C_T_ISO_M` (si no hay respuesta, ese núcleo no la tiene; nuestra Orus L no la
tiene, 2026-10-16).

**Entrada y salida** (`tools/imago-cli/`: `console.*`, `output_pane.*`, `terminal.*`). Cada
impresión, venga del hilo del estado o de los callbacks del core, pasa por un mutex, así la
salida nunca corta lo que se escribe. Hay tres modos:

- **Pantalla completa** (por defecto en una terminal): usa la pantalla alterna, como `less`.
  Arriba, el panel de salida (estado, líneas `[event]`, respuestas y cada comando enviado con
  el prompt delante) con su propio scroll de 10 000 líneas: rueda del mouse (3 filas) o
  PageUp/PageDown (una página). Abajo, fijos, una línea separadora, los últimos 3 comandos
  enviados (1 si la terminal tiene menos de 16 filas) y la línea `imago> `. Mientras se lee
  hacia atrás la salida nueva no mueve la vista: el separador se pone amarillo y cuenta
  "↓ N new lines"; Fin o Esc regresan al final, y también enviar un comando o un Tab que
  muestra opciones. Al salir la terminal vuelve a lo que tenía antes y la salida de la sesión
  no queda en ella: para conservarla, `--log salida.txt`. Con la rueda capturada, seleccionar
  texto con el mouse pide Option (macOS) o Shift (Linux). Se redibuja al cambiar el tamaño de
  la ventana (se revisa cada 250 ms). Las líneas se cortan al ancho de la terminal y los
  caracteres de control que mande el payload se muestran como `?`.
- **`--plain`**: la terminal se desplaza como siempre y su scrollback guarda todo; la última
  línea es la de entrada, y cada impresión la borra, escribe y la vuelve a dibujar. No se usa
  una región de desplazamiento (DECSTBM) porque varias terminales no guardan en el scrollback
  lo que sale de ella.
- **Sin terminal** (stdin o stdout redirigidos, `TERM=dumb`, o Windows): lee y escribe línea
  por línea, como antes, para guiones: `printf 'get C_T_PALETTE\nquit\n' | imago-cli 127.0.0.1`.

`--log archivo.txt` copia en los tres modos todo lo impreso y cada comando (`imago> …`). En los
dos modos de terminal, la terminal queda en modo raw (termios) mientras corre y se restaura al
salir, también con `kill` o al cerrar la ventana.

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
- Prueba unitaria: `tools/imago-cli/test_output_pane.cpp` (`imago_cli_tests` en `ctest`) cubre
  el corte de líneas y el scroll del panel de salida.

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

### tlog-rate-sign.py

```sh
tools/tlog-rate-sign.py logs/*.tlog [--min-seconds 1.0] [--min-rate 3] [--verbose]
```

Para cada tramo en que las tasas enviadas (284, o 282 con cuaternión NaN) mantienen un
sentido en un eje al menos un segundo, compara la actitud (285) al principio y al final:
*same* (una tasa positiva sube el ángulo, lo que el head tracking supone de entrada),
*opposite* o *still*, y la mediana de |medido/pedido|. El pitch es el eje más limpio: el yaw
también cambia si la aeronave gira. Escrita tras la prueba del visor del 2026-11-09.

### gimbal-slow-test.py

La Orus L ignora tasas menores de ~2°/s (prueba en tierra del 2026-11-09: 1.5°/s no la movió en
9 s; 2 y 3°/s dieron 2 y 3, igual a 1× y a 10×). `tools/gimbal-slow-test.py` prueba si se puede
girar más lento sin bajar de ese piso: lanza `imago-cli` como proceso hijo (por tubería, así que
usa su modo de texto plano), lee su estado y le manda órdenes cada 1/30 s en el eje de pan:
referencia a 2°/s, PWM de 2°/s y 0 (1 y 0.5°/s, periodos de 0.2, 0.4 y 1 s), sigma-delta (pulsos
de 2°/s solo en el sentido pedido mientras se debe ángulo) y ángulos que avanzan a 1, 0.5 y
0.25°/s. Cada caso dura 8 s y alterna sentido. Por defecto escucha por Herelink, toma el gimbal
manager (`proto 3`, `take`), pone *lock* y zoom 10×; `--zoom`, `--only pwm|angle`, `--proto 2
--payload 127.0.0.1 -- --local-port 14567` contra `orus-sim` (en directo `imago-cli` abre el puerto
del payload y en la misma Mac choca con el simulador). Deja `logs/lento-<hora>.tlog` y `.txt` de
`imago-cli` y `-cases.csv` con la hora Unix de cada caso para alinearla con el `.tlog`.
Pase lo que pase (fin normal, error o Ctrl-C) manda `rate 0 0` y, con el gimbal manager,
`release`: un `take` olvidado dejó a la app del iPad sin gimbal (2026-11-09). El procedimiento para el operador está en la guía de
tuning (Claude Docs, sección "Prueba de giro lento").

## 10. Pruebas e integración continua

- **Core**: `core/tests/` (doctest). Las pruebas de integración levantan `orus-sim` en un
  puerto libre y una sesión real contra él (`Fixture` en `test_session.cpp`). `ctest` las
  corre en dos entradas: `imago_core_tests` (todo menos `*head*`, límite 120 s) e
  `imago_head_tracking_tests` (los casos de head tracking, que mueven un gimbal simulado
  decenas de segundos; juntos superaban el límite en el runner de macOS). Correr también
  con ASan/UBSan y TSan antes de cambios de concurrencia.
- **imago-cli**: `tools/imago-cli/test_output_pane.cpp` (doctest, `imago_cli_tests`): corte de
  líneas y scroll del panel de salida.
- **Android**: pruebas JVM en `android/app/src/test/` (`LogicTest`, `PayloadSnapshotTest` con
  el fixture del estado) y lint.
- **Escritorio**: `desktop/tests/test_logic.cpp` (doctest) y prueba de humo con captura de
  pantalla contra `orus-sim`.
- **Quest**: `quest/app/src/test/`: `ProfilesTest`, y `HudPainterTest`, que dibuja la
  interfaz inmersiva fuera del visor (Robolectric, gráficos nativos) a
  `quest/app/build/outputs/hud/*.png`, comprueba que un clic llega a los botones y que sobre
  la imagen inicia el seguimiento; el CI publica esas capturas (`quest-interface-screenshots`).
  Lint. El resto de la app solo se puede ejecutar en un visor.
- **CI por áreas**: el job `changes` (`.github/scripts/changed-areas.sh`) mira qué rutas tocó
  el push y cada job corre solo si le toca: un commit que solo cambia `quest/` compila solo
  Quest, uno que solo cambia los manuales no compila nada, y uno que toca `core/`, el
  `CMakeLists.txt` raíz o `.github/` compila todo, porque el core es común. Quest también
  depende de `android/app/src/main/cpp/` (el decodificador compartido).
- **CI** (`.github/workflows/ci.yml`, en cada push): Core Linux GCC; Core ASan+UBSan; Core macOS
  + paquete Swift (macOS e iOS); app iOS y Mac sin firmar (dispositivo, simulador y Mac Catalyst); Android (APK,
  pruebas, lint; artefacto `imago-payload-control-debug-apk`); Quest (APK, pruebas, lint;
  artefacto `imago-payload-quest-debug-apk`); escritorio Linux (pruebas, humo,
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
- App Quest: nunca ejecutada en un visor (solo compilada en CI). Primera prueba: que el loader
  encuentre el runtime, la textura del decodificador en el panel, el sentido del yaw de la
  cabeza contra el del gimbal, la ganancia del head tracking con la Orus L real (¼ de la tasa
  pedida) y sobre Herelink (retardo), el tamaño del panel y que el passthrough
  (`XR_FB_passthrough`, capa bajo la proyección con mezcla por alfa) se vea con la escena
  encima. Primera prueba (2026-11-09): la app arranca, se ve el passthrough, el panel y el
  HUD, los mandos y los hápticos funcionan; el head tracking llevó el gimbal al tope (signo
  de las tasas contra la actitud, ahora aprendido por el lazo, §8.2b) y el video se frenaba
  al mover el gimbal o mandar comandos (sin confirmar la causa; se agregaron el keep-awake
  del Wi-Fi y el candado de baja latencia, y las tasas del lazo ya no salen a 60 Hz).
  Pendientes: probar en el visor el panel 2D y el cambio panel ↔ VR (patrón de app híbrida
  de Meta, sin verificar), mantener la sesión del core entre los dos modos, migrar la interfaz
  inmersiva a Compose sobre una superficie virtual (paridad exacta con la tableta), manos sin
  mando.
