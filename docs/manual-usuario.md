# Manual de usuario — Imago Payload Control

Imago Payload Control opera el payload **Gremsy Orus L** desde un iPad o iPhone, un teléfono o
tablet Android, o una PC con Windows o Linux: video en vivo, gimbal, cámara EO con zoom,
cámara térmica, seguimiento de objetivos y detección por IA.

Los nombres de botones y pantallas se escriben en inglés, tal como aparecen en la app.

> Este manual describe la app de iOS/iPadOS, que es la referencia. Las diferencias de Android y
> de escritorio se indican en cada sección y se resumen en
> [Diferencias por plataforma](#14-diferencias-por-plataforma).

## Contenido

1. [Antes de empezar](#1-antes-de-empezar)
2. [Instalación](#2-instalación)
3. [Conectarse al payload](#3-conectarse-al-payload)
4. [La pantalla de operación](#4-la-pantalla-de-operación)
5. [Cámara](#5-cámara)
6. [Gimbal](#6-gimbal)
7. [Seguimiento, Point y detección IA](#7-seguimiento-point-y-detección-ia)
8. [Modo térmico (barra IR)](#8-modo-térmico-barra-ir)
9. [Fotos y videos del payload (Media)](#9-fotos-y-videos-del-payload-media)
10. [Ajustes](#10-ajustes)
11. [Mandos, gamepads y HOTAS](#11-mandos-gamepads-y-hotas)
12. [Diagnóstico: HUD y registro](#12-diagnóstico-hud-y-registro)
13. [Solución de problemas](#13-solución-de-problemas)
14. [Diferencias por plataforma](#14-diferencias-por-plataforma)

---

## 1. Antes de empezar

| Plataforma | Requisito |
| --- | --- |
| iPad / iPhone | iPadOS / iOS 17 o posterior |
| Mac | macOS 14 o posterior, Apple silicon o Intel |
| Android | Android 7.0 o posterior, teléfono o tablet, en horizontal |
| Windows | Windows 10 u 11 de 64 bits |
| Linux | Distribución de 64 bits con Qt 6 y FFmpeg |

Necesita además:

- Una Orus L encendida y alcanzable por red: a través de **Herelink** (lo habitual) o por cable
  Ethernet directo.
- Su **IP** (la misma que se usa en Payload Plus; con Herelink suele ser `192.168.144.x`) y, si
  el video viene de Herelink, la dirección RTSP (normalmente
  `rtsp://192.168.43.1:8554/fpv_stream`).
- Opcional: un gamepad (Xbox, PlayStation, MFi) o un HOTAS.

## 2. Instalación

- **iPad / iPhone**: distribución interna (TestFlight o instalación desde Xcode). En la primera
  conexión iOS pide permiso de **Red local**: hay que permitirlo, o la app no podrá conectarse
  (Ajustes de iOS › Privacidad › Red local › Imago Payload, si se rechazó).
- **Mac**: abrir el `.dmg` que entrega Imago y arrastrar **Imago Payload** a Aplicaciones. Está
  firmado y notarizado por Apple, así que abre sin advertencias. En la primera conexión macOS
  pide permiso de **Red local**: hay que permitirlo (Ajustes del Sistema › Privacidad y
  seguridad › Red local, si se rechazó).
- **Android**: instalar el APK firmado que entrega Imago (permitir la instalación desde esa
  fuente cuando Android lo pida).
- **Windows**: descomprimir la carpeta entregada y ejecutar `ImagoPayloadControl.exe`. No requiere
  instalación.
- **Linux**: ejecutar `ImagoPayloadControl` desde la carpeta de compilación o el paquete
  entregado.

## 3. Conectarse al payload

### Perfiles de conexión

La app guarda **perfiles**: cada uno dice cómo llegar a un payload. Vienen tres de fábrica:

| Perfil | Uso |
| --- | --- |
| **Orus L · Herelink** | Control a la IP `192.168.144.240` por el puente Ethernet de Herelink, video desde Herelink |
| **Orus L · Direct** | Orus L por cable a `192.168.55.1`, video anunciado por el propio payload |
| **orus-sim …** | Simulador para pruebas sin hardware |

Para elegir o editar perfiles, toque el botón de **conexiones** (icono de deslizadores) en la
barra superior, o **Edit connections…** en el panel de conexión.

Campos de un perfil:

| Campo | Qué es |
| --- | --- |
| **Name** | Nombre a mostrar |
| **Payload (MAVLink)** › modo | **Payload IP**: la app envía a la IP de la Orus L (recomendado, menor latencia). **Listen (forwarded)**: la app escucha el MAVLink que reenvía Herelink (puerto 14550) |
| **Payload IP**, **MAVLink port** | IP de la Orus L y su puerto (14566) |
| **Listen port** | Solo en modo escucha (0 = 14550) |
| **Video (RTSP)** › URL | Vacío = usar el video que anuncia el payload |
| **Transport** | **Auto** (UDP y, si no llega nada, TCP), **UDP** o **TCP** |
| **Resync when behind** | Si el video se queda atrasado ese tiempo (300 ms, 500 ms, 1 s) durante 2 s, la app reinicia el stream para volver a vivo. **Never** lo desactiva |
| **Smoothing** | Muestra a su ritmo los cuadros que llegan en ráfagas, a cambio de hasta ese retraso (50–200 ms). **Off** = mínima latencia |
| **Advanced** › **Gimbal commands** | **Gimbal v2 (direct)** (normal), **Mount control** (protocolo antiguo), **Autopilot gimbal manager** (el gimbal se mueve a través de PX4) |
| **Set RC mode to standard** | Pone el RC de la Orus L en modo estándar al conectar |
| **Keep Wi-Fi awake (experimental)** | Envía un paquete diminuto 30 veces por segundo para que el Wi-Fi de la tablet no dormite y entregue video a saltos. Apagarlo si el video se congela |

Los cambios se aplican la próxima vez que se conecta.

### Conectar y desconectar

1. Elija el perfil en el panel central y toque **Connect**.
2. La barra superior muestra el nombre del perfil, el tiempo de ida y vuelta de los comandos
   (p. ej. "130 ms") o "waiting for payload" mientras no hay enlace.
3. Aparecen el video y los controles. Un aviso indica "Payload connected".
4. Para desconectar, toque la **X** de la barra superior.

Si la app pasa a segundo plano (iOS), se desconecta y vuelve a conectar sola al regresar.

## 4. La pantalla de operación

```
┌──────────────────────────────────────────────────────────────────────────┐
│ IMAGO  perfil · RTT    [LRF] [SD] [mando]    (actitud)(HUD)(⚙)(≡)(X)       │  barra superior
│ ┌──────┐ ┌──────────────────────────────────────────────────────────────┐│
│ │Cámara│ │ OSD de                VIDEO                        ┌───────┐ ││
│ │ foto │ │ la cámara                                          │Gimbal │ ││
│ │ vista│ │              (tocar = seguir objetivo)             │ modo  │ ││
│ │ zoom │ │                                                    │ centro│ ││
│ │      │ │                                                    │ AI    │ ││
│ └──────┘ │                                                    │joystick ││
│          │                                                    └───────┘ ││
│          └──────────────────────────────────────────────────────────────┘│
│                           (actitud)  [ barra térmica, en IR / IR + EO ]  │
└──────────────────────────────────────────────────────────────────────────┘
```

- **Barra superior**: coordenadas del objetivo (ver abajo), rango del telémetro láser, espacio
  libre en la tarjeta, mando conectado, "REC mm:ss" mientras graba, y los botones: **Gimbal attitude** (mostrar u ocultar el
  instrumento), **Network HUD**, **Media** (fotos y videos guardados en el payload, sección 9),
  **Settings** (engrane), **Connections** y **Disconnect**.
- **Coordenadas del objetivo** (chip con alfiler, p. ej. "22.16984, -101.05302"): el punto al
  que apunta la cámara, tal como lo calcula el payload (la línea TARGET de su OSD: distancia
  del telémetro láser, ángulos del gimbal y GPS del avión). Aparece mientras el payload la
  reporta; con poco espacio (teléfono) se reduce a "TGT". Al tocarlo: **Copy coordinates**,
  **Share**, **Open in Maps** y **Mark point**, que guarda el punto con la hora. La altitud
  aparece en el título del menú. La precisión es de alrededor de 1 m en el número; la del
  punto depende del telémetro y del GPS.
- **Riel izquierdo**: cámara (sección 5). **Riel derecho**: gimbal y seguimiento (secciones 6
  y 7).
- **Video**: empieza a la derecha del riel de cámara, para que el riel no tape el texto (OSD)
  que la cámara dibuja en el borde izquierdo de la imagen. En una tablet el video queda un poco
  más chico (alrededor del 92 % en un iPad de 11"); en un teléfono solo se recorre a la
  derecha. Se puede volver al video a todo lo ancho en **Settings → Camera → Display → Keep
  the picture clear of the camera rail**.
- **Instrumento de actitud**: vista desde arriba del avión con el cono de la cámara (ancho
  según su campo de visión), anillo de rumbo (brújula cuando se conoce el rumbo del avión) y
  arco de inclinación. Debajo: **PITCH**, **YAW** (hasta ±320°, en ámbar cerca del tope) y
  **BRG** (marcación respecto al norte, cuando se conoce). Siempre está abajo al centro,
  también en modo térmico.
- **Avisos**: mensajes breves abajo al centro (información en magenta, errores en rojo).
- **"VIDEO x.x s BEHIND"**: el video llega atrasado; no se guíe por él. **"CATCHING UP WITH
  LIVE VIDEO"**: la app está reiniciando el stream para volver a vivo.

## 5. Cámara

Riel izquierdo:

- **Foto / Video**: elige el modo de captura.
- **Obturador**: en foto toma una fotografía; en video inicia o detiene la grabación (en la
  tarjeta de la Orus L).
- **Vista** (menú): **EO**, **IR**, **EO + IR** (EO con IR en recuadro), **IR + EO**, **Sync**,
  **Side by side**.
- **Zoom**:
  - Mantener **+** o **−** hace zoom continuo; un toque hace zoom al menos 300 ms.
  - El **slider** entre ellos va directo a un nivel. Es logarítmico y solo se detiene en los
    niveles de la cámara (1, 2, 4 … 25× ópticos; 50 … 300× digitales, marcados en ámbar). El
    pulgar es el nivel pedido; la marca junto al riel es dónde está el lente mientras llega.
  - **Pellizcar** el video también hace zoom.
  - Rango: 25× en modo *Super resolution* o 300× en *Combined* (Settings › Zoom).

En un teléfono, el zoom ocupa una segunda columna del riel.

## 6. Gimbal

Riel derecho:

- **Joystick**: mueve el gimbal; más desviación = más velocidad. La velocidad baja
  automáticamente al acercar el zoom.
- **Mode**: **Lock** (la cámara mantiene su rumbo aunque el avión gire), **Follow** (sigue la
  nariz del avión), **Mapping** (mira hacia abajo), **Off**.
- **Center**: vuelve el gimbal al frente (pitch y yaw a 0) con el reset propio del payload,
  en unos 3 s, con cualquier opción de **Gimbal commands** y aunque la app no tenga el control
  del gimbal manager. La etiqueta del resultado sale en cuanto el gimbal empieza a centrar.
  En **Follow**, la Orus L vuelve a inclinarse sola despacio (~0.3°/s) hasta unos −16° en el
  minuto siguiente; en **Lock** se queda al frente (pruebas en tierra del 2026-10-27).
- **P / Y**: inclinación y giro actuales; Y en ámbar cerca del tope de ±320°, con un aviso
  "Gimbal pan … near its ±320° limit". Gire en sentido contrario o centre antes de llegar al
  tope.
- **TAKE CONTROL / IN CONTROL** (solo con el perfil en *Autopilot gimbal manager*): indica quién
  maneja el gimbal a través del autopiloto. La app toma el control al conectar (salvo que lo
  tenga una estación de tierra) y lo devuelve al desconectar; toque para tomarlo o liberarlo.
  Si el control queda libre sin que usted lo haya liberado (otro dispositivo lo soltó, el
  autopiloto se reinició), la app lo vuelve a tomar sola. Si usted lo liberó, espera a que
  toque **TAKE CONTROL**. Sin el control, el joystick no mueve el gimbal; **Center** sí.

## 7. Seguimiento, Point y detección IA

- **Tocar el video** fija el seguimiento (tracking) sobre el objeto tocado; el gimbal lo sigue
  y aparecen corchetes alrededor. Mientras sigue o lo pierde aparece **Stop track** (o
  **Lost · Stop**) para detenerlo.
- **Point**: con este botón activo, un toque solo reapunta el gimbal hacia ese punto sin seguir
  (Eagle Eye).
- **AI**: activa la detección de objetos de la Orus L; aparecen cajas con clase y confianza.
  Tocar dentro de una caja sigue ese objeto completo.
- Si el payload acepta la orden pero su tracker no obedece, aparece "Payload accepted … Try
  changing the view, or restart the camera service in Settings." Cambiar la vista suele
  destrabarlo. Si no basta (pasa sobre todo en sesiones largas), use **Settings → Camera →
  Payload → Restart camera service…**: reinicia el software de la cámara; video, seguimiento y
  control se pierden un momento y vuelven solos. Que esto destrabe el seguimiento está por
  confirmar en campo. Un punto lejano del centro no lo
  dispara: mientras el gimbal gira hacia el objetivo, la app lo cuenta como obedecido.

## 8. Modo térmico (barra IR)

Al poner la vista en **IR** o **IR + EO**, aparece una barra abajo a la derecha, junto al
indicador de actitud, que no se mueve. En una tablet ocupa la franja negra bajo el video, sin
taparlo. La barra se adapta al espacio:

- **Tablet**: la paleta se muestra como su muestra de color (el nombre está en el menú), la
  escala de temperatura, el zoom IR y el botón **ISO** (si la cámara tiene isoterma):

```
┌────┬──────────────────────────┬─────────┬─────┐
│▮ ⌄ │ ██████████▓▓▓▓▓▓▓▓▓▓▓▓▓▓ │ − 2× +  │ ISO │
│    │ 12.4°  MEAN 23.9°  48.2° │         │35–40│
└────┴──────────────────────────┴─────────┴─────┘
```

- **Teléfono**: solo la paleta y un botón **IR** (muestra el zoom actual, p. ej. "2×", y se
  pone en color cuando la isoterma está activa). Su menú tiene **IR zoom** (1× a 8×) e **Isotherm…**
  (abre el panel de la isoterma, si la cámara la tiene). La escala de
  temperatura aparece solo si hay espacio, lo que casi nunca ocurre en un teléfono.
- **Sin indicador de actitud** (oculto con su botón de la barra superior): la barra ocupa todo
  el espacio entre los rieles y la paleta muestra también su nombre.

| Control | Qué hace |
| --- | --- |
| **PALETTE** | Elige la paleta de color de la imagen térmica (en la Orus L: White hot, Fulgurite, Iron red, Hot iron, Medical, Arctic, Rainbow 1, Rainbow 2, Tint, Black hot). La muestra de color indica la paleta actual |
| **Escala de temperatura** | Temperatura mínima (izquierda), media (marca y "MEAN") y máxima (derecha) de toda la imagen térmica, en °C, sobre los colores de la paleta. Si la isoterma está activa, su banda aparece remarcada en rojo |
| **− 2× +** | Zoom digital de la cámara térmica, 1× a 8× (en teléfono, en el menú **IR**) |
| **ISO** | Isoterma (solo aparece si la cámara la soporta; ver abajo). Muestra la banda activa o "OFF" |

La app no ofrece la recalibración del sensor (*FFC*, *flat-field correction*): la cámara la
hace sola. Su modo y su periodo se ajustan en la página web de la cámara ("IR Camera").

### Isoterma

La isoterma **resalta todo lo que está entre dos temperaturas**, coloreándolo de negro a rojo
sobre la paleta. Sirve para encontrar rápidamente personas, puntos calientes o fuego.

Toque **ISO** para abrir el panel:

1. **Interruptor**: activa o desactiva la isoterma.
2. **Presets**: **People** 35–40 °C, **Hot** 100–150 °C, **Fire** 150–550 °C. Se aplican al
   tocarlos.
3. **From / To**: banda personalizada en grados enteros (0 a 550 °C); mantener **−** / **+**
   repite.
4. **GAIN** (si la cámara lo reporta): **High** mide hasta 150 °C con más detalle; **Low** mide
   hasta 550 °C. Una banda por encima de 150 °C cambia la cámara a ganancia baja
   automáticamente.
5. **Apply / Turn on**: envía la banda a la cámara.

La app recuerda la última banda usada.

**Limitaciones**

- Gremsy documenta la isoterma y la ganancia solo para uno de sus núcleos térmicos. La app
  pregunta a la cámara al entrar en vista IR: si no responde, **ISO** no aparece. La Orus L
  que probamos (16 de octubre de 2026) no las ofrece por este medio, así que en ella **ISO** no
  aparece. Su isoterma está en su página web de ajustes, y la app la maneja desde **Settings**
  (ver abajo).
- La cámara ajusta sola el rango de color de la paleta a la escena; no existe un ajuste para
  fijar manualmente el mínimo y máximo de la escala. La isoterma es la manera de destacar una
  banda de temperaturas concreta.
- Los nombres de las paletas dependen del sensor térmico del payload. Los de la app son los
  del sensor de la Orus L, comprobados en campo. Si en otro payload el nombre no coincide con
  lo que se ve, el número de orden sí es el correcto (y se puede reportar el "IR sensor type"
  que muestra el HUD).
- Las temperaturas de la escala son de toda la imagen, no de un punto.

### Isoterma de la Orus L (Settings)

La Orus L guarda su isoterma en su propia página web de ajustes, no en los mensajes de control.
La app la lee y la cambia desde **Settings → Thermal & stream → Isotherm (payload web
settings)**:

1. Al abrir la página se leen los valores actuales ("Reading the payload's web page…").
2. **Isotherm** (activar o no), **Threshold** (umbral en °C, 0 a 550; p. ej. 30 para
   personas) y **Measurement range** (−20 a 150 °C o −20 a 550 °C).
3. **Save to payload** guarda y el cambio se ve en el video de inmediato, sin reiniciar nada
   (aunque la página web de la cámara diga que se aplica al reiniciar).

Hace falta que el perfil de conexión tenga la IP del payload.

## 9. Fotos y videos del payload (Media)

El botón **Media** de la barra superior abre las fotos y videos guardados en la Orus L para
verlos, descargarlos y borrarlos. En escritorio se abre en su propia ventana (también con
**Ctrl+M**), así el video en vivo sigue a la vista; en iPad, iPhone y Mac ocupa la pantalla y se
cierra con **Done** (en Mac también desde el menú **Payload → Media…**, ⇧⌘M). Hay que estar
conectado: sin conexión dice "Connect to the payload to see its media.". La app lee los archivos
por la página web del payload (puerto 8000, la IP del perfil), igual que la isoterma.

**Disparos.** Cada foto de la Orus L son dos archivos, uno por cámara, con el mismo número:
`IMG_EO_205.jpeg` (la cámara visible, unos 10 MB) e `IMG_IR_205.jpeg` (la térmica, menos de
1 MB); los videos igual (`VID_EO_36.mp4` y `VID_IR_36.mp4`). La app muestra **una celda por
disparo** (marca **EO+IR**), y descargar y borrar actúan sobre los dos archivos.

**Vuelos.** A la izquierda, las capturas agrupadas en vuelos: si pasan más de 30 minutos sin
capturas, empieza otro. Cada vuelo muestra la fecha ("Oct 19"), la hora de inicio y fin
("11:02 - 11:33"), cuántas fotos y videos tiene, su tamaño y una **tira de tiempo**: una
raya por foto y una barra por video, en el momento en que se tomaron. Tocar o arrastrar sobre
la tira del vuelo elegido lleva la cuadrícula a esa captura. El botón de descarga del vuelo
(**Download flight**) baja el vuelo completo. Los archivos sin hora quedan en **No capture
time**. Las horas son las del reloj del payload, mostradas en la hora local del equipo; si el
reloj del payload está mal (el nuestro iba 8 días atrasado), las fechas lo estarán también.
Arriba, el espacio usado de la tarjeta ("17.6 of 119.1 GB") y el filtro **All | Photos |
Videos**; **Refresh** vuelve a leer la lista.

**Cuadrícula.** Un disparo por celda, en orden de captura, con la hora. Las fotos de la Orus no
traen miniatura y la EO pesa unos 10 MB, así que la celda muestra la imagen **térmica** del
mismo disparo, con la etiqueta **IR view**: sirve para reconocer la toma sin bajar 10 MB por
celda. La foto EO real se ve en la vista previa. Marcas: **EO+IR** (los dos archivos), **IR**
(un archivo térmico sin par), **TIFF** (imagen radiométrica), ▶ y tamaño en los videos (sin
miniatura), y una palomita verde cuando los dos archivos del disparo ya están en este equipo.

**Seleccionar.** En escritorio y Mac: clic selecciona, Ctrl-clic (⌘-clic en Mac) suma o quita,
Mayús-clic selecciona un rango, Ctrl+A (⌘A) todo el vuelo, Esc limpia la selección, Supr o
Retroceso pide borrar, doble clic o Enter abre la vista previa; en escritorio las flechas
mueven la selección y Espacio la marca. En iPad y iPhone: **Select** activa la selección (un
toque marca o desmarca) y **Select All** marca todo el vuelo; fuera de ese modo un toque abre la
vista previa. Clic derecho o mantener presionado abre el menú del disparo (**Download**,
**Delete…**, o "Download 6 shots" con varios). Abajo aparece la selección con el tamaño de
los dos archivos de cada disparo ("6 shots · 120.4 MB" en iPad/iPhone/Mac, "6 selected · 120.4
MB" en escritorio) con **Download**, **Delete** y **Clear**.

**Descargar.** Los archivos se guardan en una carpeta por vuelo con la fecha y hora de inicio
("2026-10-19 11-02") y el nombre que tienen en el payload; los que ya están en el equipo no se
vuelven a bajar ("Already on this device."). Mientras baja, la barra inferior muestra
"Downloading 3 of 12" (cuenta archivos: un disparo son dos), el archivo, el porcentaje, la
velocidad y **Cancel**; al terminar, "12 downloaded" y, si hubo, cuántos fallaron y por qué.
Por Herelink se bajan alrededor de 1.2–1.6 MB/s: una foto EO tarda unos 7 s y un video de
3 minutos en 4K (186 MB) unos 2 minutos.

- Escritorio: en `Imágenes/Imago Payload Control/` por omisión; el botón de carpeta del
  encabezado muestra la ruta, **Change folder…** y **Open folder**.
- iPad / iPhone: en la carpeta de la app, visible en la app Archivos (En mi iPad → Imago
  Payload Control → Media). Un archivo descargado se puede compartir desde su menú o desde la
  vista previa.
- Mac: en la carpeta Media de la app; **Show downloads in Finder** la abre.

**El video en vivo y las descargas.** Las descargas usan el mismo enlace de datos que el video.
Si el video está activo al empezar a descargar, la app pregunta ("Live video is on"):

- **Pause live video**: detiene el video mientras bajan los archivos y lo reanuda al terminar,
  cancelar o cerrar Media. Recomendado por Herelink o con archivos grandes.
- **Download alongside**: descarga con el video activo. En la prueba del 2026-10-20 (11 Mbit/s
  de descarga por Herelink) el video siguió fluido, pero con un enlace más débil puede trabarse
  o atrasarse.
- **Cancel**: no descarga.

**Borrar.** **Delete** pide confirmación ("Delete 6 shots from the payload?", "Each shot's EO
and IR files are deleted. This cannot be undone."). Se borran de la tarjeta del payload los dos
archivos de cada disparo; no hay papelera. La app comprueba después que ya no estén.

**Vista previa.** Doble clic, Enter o un toque abre el disparo en grande con el selector
**EO | IR**: EO baja la foto completa (unos 10 MB, con su avance; mientras tanto no se muestra
la térmica en su lugar), IR aparece al instante. Anterior y siguiente con flechas o
deslizando; hora, tamaño, **Download** y **Delete** (del disparo). Los
videos no se reproducen dentro de la app: se descargan y se abren con el reproductor del
equipo (**Open** en escritorio y Mac) o se comparten (iPad/iPhone). Los TIFF radiométricos se
descargan para abrirlos en un programa de análisis térmico.

**Mensajes.**

| Mensaje | Qué significa |
| --- | --- |
| "Reading the payload's media…" | Leyendo la lista |
| "No photos or videos on the payload." | La tarjeta está vacía |
| "The payload is still starting its cameras." | El payload recién encendió; la app reintenta sola cada 5 s |
| "This payload asks for a login before it shares its media." | Ese modelo pide usuario y contraseña; la app todavía no lo soporta |
| "Could not read the payload's media." / "Cannot read the payload's media" | Sin respuesta de la página web: revisar la IP del perfil y el enlace; **Try again** |
| "Live video paused while Media downloads." (escritorio, ventana principal) | La app pausó el video por las descargas |

## 10. Ajustes

Botón **Settings** (engrane). Los ajustes de la cámara se leen del payload al abrir cada
página; un guion "—" indica que aún no responde.

| Página / sección | Ajustes |
| --- | --- |
| **Display** | Keep the picture clear of the camera rail (activado por defecto; ver sección 4). Es de la app, se puede cambiar sin conexión |
| **Payload** | **Restart camera service…** (con confirmación): reinicia el software de la cámara cuando el seguimiento deja de obedecer (sección 7). Solo con conexión |
| **Marked points** | Lista de puntos marcados (más reciente arriba; deslizar para borrar), **Share as KML (maps)** y **Share as CSV (spreadsheet)**. Se guarda en el dispositivo |
| **MAVLink log** | Detailed log (link debugging), desactivado por defecto: ver sección 12 |
| **Zoom** | Zoom mode (Combined 300× / Super resolution 25×), Zoom speed, Stable zoom |
| **Focus** | Focus mode (Auto, Manual, One-push), Focus speed |
| **Image** | Defog y nivel, Stabilization (EIS) y nivel, Noise reduction, Flicker reduction, Flip image |
| **Day / night** | IR-cut filter (Auto/Manual) y su posición manual, Lens heater |
| **Overlay** | Payload OSD in the video (texto que la cámara dibuja en el video y en sus grabaciones) |
| **Thermal** | Palette (por nombre) y Digital zoom; los mismos que la barra térmica |
| **Isotherm (payload web settings)** | Isotherm, Threshold, Measurement range y **Save to payload** (sección 8) |
| **Stream** | Video bitrate: con Herelink, un bitrate menor pierde menos paquetes (menos imagen congelada) |
| **Gimbal** | Mode, RC input (Gremsy / Standard) |
| **Sticks and on-screen joystick** | Velocidad máxima, curva (lineal a fina) y zona muerta |
| **D-pad and buttons** | Velocidad fija para crucetas y botones |
| **Controller** | Asignación de botones y ejes (sección 11) |

## 11. Mandos, gamepads y HOTAS

Al conectar un mando aparece su nombre en la barra superior. Asignación por defecto:

| Entrada | Acción |
| --- | --- |
| Stick izquierdo | Gimbal (giro e inclinación) |
| Stick derecho | Zoom |
| A | Obturador |
| B | Grabar / detener |
| X | Detener seguimiento |
| Y | Detección IA on/off |
| L1 / R1 | Vista anterior / siguiente |
| Menu | Centrar gimbal |

En **Settings › Controller** cualquier botón, eje, cruceta o gatillo se puede reasignar: toque
la acción y luego mueva o presione la entrada deseada. Acciones disponibles: Shutter, Start /
stop recording, Stop tracking, AI detection on / off, Previous view, Next view, Center gimbal,
Point mode on / off, Gimbal lock / follow, Zoom in / out (mantener); ejes: Gimbal pan, Gimbal
tilt, Zoom (con opción de invertir).

En iPad solo funcionan los mandos que iPadOS reconoce como gamepad (incluidos los HOTAS que se
presentan como tal). Android y escritorio reconocen además joysticks y HOTAS genéricos.

## 12. Diagnóstico: HUD y registro

**Network HUD** (botón de indicador en la barra superior) muestra:

| Dato | Significado |
| --- | --- |
| Video, Frame rate, Bitrate | Códec y transporte, cuadros por segundo, megabits por segundo |
| Lost / late | Paquetes perdidos / llegados tarde |
| Jitter, Frame transfer | Variación de llegada y tiempo de transferencia de un cuadro |
| Frame gaps p95 / max | Separación entre cuadros (~33 ms si el enlace es parejo; valores altos = ráfagas) |
| Behind live | Retraso del video y cuántas veces se reinició para alcanzar el vivo |
| App: assemble / decode / shown | Lo que tarda la propia app |
| Corrupt / failed / skipped | Origen de una imagen congelada: paquetes perdidos, decodificador o pantalla |
| Command RTT | Ida y vuelta de los comandos |
| MAVLink rx / gaps | Mensajes recibidos / huecos |
| Others' commands | Comandos que el payload recibió de otro (autopiloto, estación de tierra) |
| Payload, IR sensor type | Modelo y versión; tipo de sensor térmico reportado |

**Record log** graba el MAVLink en un archivo `.tlog` (en iOS: app Archivos › Imago
Payload › Logs, o el botón de compartir; en Mac, además, el botón de carpeta lo muestra en
Finder). Se abre con QGroundControl o MAVExplorer y es lo que
Imago necesita para analizar un problema de campo. Por defecto el registro es **compacto**:
guarda lo que dice el payload y lo que se le ordena (comandos, respuestas, ajustes y avisos
completos; actitud del gimbal, coordenadas del objetivo y demás datos una vez por segundo), y
queda pequeño. Para investigar el enlace (video que se congela, comandos que se pierden) active
antes **Settings → Thermal & stream → MAVLink log → Detailed log (link debugging)**: guarda
todo lo que pasa por el enlace y las mediciones de video de la app, y crece rápido. Desactívelo
al terminar.

## 13. Solución de problemas

| Síntoma | Qué hacer |
| --- | --- |
| No conecta; "Network is unreachable" (iOS) | Permitir **Red local** para la app en los Ajustes de iOS |
| "waiting for payload" no cambia | Verificar estar en el Wi-Fi de Herelink, la IP y el puerto del perfil; probar el modo **Listen (forwarded)** |
| Hay control pero no video | Revisar la URL RTSP; probar **Transport** = TCP; en Android, que los datos móviles no estén tomando la ruta (la app se ata a la red del payload) |
| Video a saltos | Mirar "Frame gaps" en el HUD; probar **Smoothing** 50–200 ms o **Keep Wi-Fi awake**; en laptops, desactivar el ahorro de energía del Wi-Fi |
| "VIDEO … BEHIND" frecuente / "Video keeps falling behind" | El enlace no soporta el bitrate: bajarlo en **Settings › Stream** |
| El gimbal no se mueve | Revisar **Gimbal commands** del perfil; con *Autopilot gimbal manager*, tomar el control (**TAKE CONTROL**) |
| "Zoom moved without this app" / "Others' commands" sube | Otro equipo (autopiloto, estación de tierra) está comandando el payload |
| El seguimiento no obedece | Cambiar la vista y volver a intentar; si sigue, **Settings → Camera → Restart camera service…** |
| Imagen térmica con manchas o bandas | La cámara se recalibra sola (FFC); revisar en su página web que "FFC Mode" esté en automático |
| No aparece **ISO** | La cámara no respondió a la isoterma; no está disponible en ese sensor |

## 14. Diferencias por plataforma

| Tema | iPad / iPhone | Android | Windows / Linux |
| --- | --- | --- | --- |
| Tocar el video | Toque | Toque | Clic ("Click to release" en el control del gimbal manager) |
| Zoom adicional | Pellizcar | Pellizcar | Rueda del ratón sobre el video o sobre el slider |
| Borrar un perfil o una asignación | Deslizar | Botón y confirmación | Botón y confirmación |
| Mandos | Gamepads que iPadOS reconoce | Gamepads y joysticks | Gamepads, joysticks y HOTAS (SDL2); siguen funcionando con otra ventana enfocada |
| Compartir el `.tlog` | Compartir / Archivos | Compartir | Abre la carpeta de registros |
| HUD | — | Igual que iOS | Además "Resolution" y "Decoder" |
| Pantalla completa | Siempre | Siempre | F11; Esc cierra una página |
| Barra térmica: variantes reducidas | Sin escala, queda centrada en su espacio | Sin escala, queda pegada a la derecha | Igual que Android; con el tamaño mínimo de ventana sigue cabiendo la muestra de paleta, así que el menú **IR** casi no aparece |
| Coordenadas del objetivo | **Share** (enlace de Apple Maps), **Open in Maps** abre Mapas | **Share** envía las coordenadas y un enlace de Google Maps; **Open in Maps** abre la app de mapas instalada | **Copy map link** copia un enlace de Google Maps; **Open in Maps** lo abre en el navegador |
| Marked points | Página propia; deslizar para borrar; **Share as KML / CSV** | Subpágina de Settings; botón y "Delete Point N?"; compartir KML / CSV | Lista dentro de Settings → Camera; botón y confirmación; **Save as KML (maps) and CSV (spreadsheet)** guarda los archivos y abre su carpeta |
| Restart camera service… | Confirmación | Diálogo; mientras está abierto el gamepad no mueve el gimbal | Diálogo |
| Barra térmica: panel de isoterma | Ventana flotante | Menú desplegable; mientras está abierto el gamepad no mueve el gimbal | Ventana flotante |
| Red | — | Se ata a la red del payload y mantiene el Wi-Fi en baja latencia | Ahorro de energía del Wi-Fi según el sistema |
| Media | Pantalla completa; en iPad la lista de vuelos al lado, en iPhone primero los vuelos; **Select** para seleccionar; "6 shots · …"; descargas en la app Archivos (Mac Catalyst: **Show downloads in Finder**; la app de iPad en la Mac: `~/Library/Containers/…/Data/Documents/Media`) | Todavía no existe | Ventana propia (Ctrl+M); clic, Ctrl-clic, Mayús-clic y teclado; "6 selected · …"; carpeta de descarga elegible; los TIFF no tienen vista previa |

**Mac.** Es la misma app de iPad, con estas diferencias:

- Ventana que se puede agrandar o achicar (mínimo 1000 × 640) y pantalla completa desde el botón
  verde o con ⌃⌘F.
- Clic en lugar de toque. Zoom con la rueda del ratón o deslizando dos dedos en el trackpad
  sobre el video (se detiene 250 ms después del último paso), o pellizcando en el trackpad.
- Menú **Payload**: **Connections…** (⌘K), **Recenter gimbal** (⌘R), **Stop tracking** (⌘.) y
  **Media…** (⇧⌘M).
  **Settings…** está en el menú de la app (⌘,).
- Al minimizar u ocultar la ventana sigue conectada (también la grabación del registro); al
  pasar a otra app, el gimbal y el zoom se detienen como en iOS. Mientras está conectada, la
  Mac y su pantalla no se duermen.
- Registros y puntos marcados: botones para mostrarlos en Finder (**Show KML and CSV in Finder**
  en Marked points), además de compartir.
