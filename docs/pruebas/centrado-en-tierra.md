# Prueba en tierra: centrado del gimbal con el gimbal manager del PX4

Objetivo: decidir cómo debe centrar la app en el modo "Autopilot gimbal manager".

## Por qué

Vuelo del 2026-10-27 (logs `imago-2026-10-27T180743` a `T183525`):

- El iPad (app actual) centra con un `GIMBAL_MANAGER_SET_ATTITUDE` (282) de ángulo 0/0 al PX4
  (cambio del 2026-10-15, `9d22440`). Con el control primario en 1/193, el PX4 **sí** lo pasó al
  gimbal (`GIMBAL_DEVICE_SET_ATTITUDE` 0/0, flags 12), varias veces, y la Orus L **no se movió**
  (pitch −20° antes y después, 12:07 + 155–160 s).
- Cuando nadie tenía el control (0/0), el PX4 descartó el 282 (12:17 + 243 s, 12:35 + 39–43 s).
- Lo que sí centró fue un `GB_MODE` = 4 que mandó otro cliente con la misma identidad 1/193 (la
  tableta Android con una compilación anterior al 2026-10-15): flags NEUTRAL y pitch 0 en ~2 s
  (12:07 + 74.8 s y + 162.1 s). Ese es el centrado que el 2026-10-15 produjo un vaivén de ±90°
  al mandarse con el gimbal cerca del nadir.
- Las **tasas** por el gimbal manager sí mueven el gimbal.

No se sabe si la Orus L ignora todo ángulo de `GIMBAL_DEVICE_SET_ATTITUDE`, solo en *follow*,
solo el que llega del PX4, o solo el 0/0.

## Hipótesis

| | Hipótesis | Paso que la prueba |
|---|---|---|
| H0 | Las tasas por el gimbal manager mueven el gimbal (control de la prueba) | 2 |
| H1 | La Orus L ignora un ángulo que le pasa el PX4 en *follow* (repetir el vuelo) | 3 |
| H2 | No es el 0/0: tampoco obedece otro ángulo | 4 |
| H3 | En *lock* (`YAW_LOCK`) sí obedece el ángulo | 5 |
| H4 | Tampoco obedece un ángulo directo (`GIMBAL_DEVICE_SET_ATTITUDE` desde nosotros) | 6 |
| H5 | Obedece `DO_MOUNT_CONTROL` por ángulo | 7 |
| H6 | `GB_MODE` = 4 centra limpio desde un pitch moderado con el PX4 de gimbal manager | 8 |
| H7 | `GB_MODE` = 4 desde cerca del nadir repite el vaivén del 2026-10-15 | 9 (opcional) |

## Preparación

1. **Aeronave en tierra, sin hélices**, desarmada, con el PX4, la Orus L y el Herelink
   encendidos y la configuración de vuelo: `MNT_MODE_IN` y `MNT_MODE_OUT` = MAVLink gimbal v2.
   Anotar los valores de `MNT_MODE_IN`, `MNT_MODE_OUT` y `MNT_MAN_*` en QGroundControl.
2. Espacio libre alrededor del gimbal: recorre pitch +40° a −120° y pan ±320°.
3. **Cerrar la app en el iPad y en la tableta Android.** Ambas usan la identidad 1/193, la
   misma que `imago-cli`, y mezclarían sus comandos y acuses con los de la prueba (pasó en el
   vuelo del 2026-10-27). En el control del Herelink, no tocar el gimbal ni las ruedas.
4. Mac en el Wi-Fi del Herelink. Compilar y arrancar en modo escucha, con el `.tlog` completo
   (sin `--compact`, que deja un mensaje por segundo):

   ```sh
   cmake --build build -j --target imago-cli
   mkdir -p logs
   ./build/tools/imago-cli/imago-cli - --tlog logs/centrado-$(date +%Y%m%d-%H%M).tlog --every 2
   ```

5. Esperar `link=UP` y `ids cam=101 gimbal=154`. Si sale `cam=100`, la app tomó la cámara del
   Herelink (42/100, problema conocido): esperar a que vuelva a 101 antes de mandar `mode`.
6. Llevar una hoja con la hora de cada paso y lo que se vio en el gimbal. Entre pasos, esperar
   **5 s** con el gimbal quieto: el análisis compara la actitud antes y 3 s después.

## Pasos

Cada comando se escribe en `imago-cli` y se confirma con Enter. "Ver" es la línea de estado
(`gimbal p=… y=… mode=…` y `gimbal manager control=…`) y el gimbal mismo.

| Paso | Comandos | Ver | Si no pasa |
|---|---|---|---|
| 1. Tomar el control | `proto 3`, esperar `control=` distinto de `unknown`, `take` | `control=this app (1/193)` | Repetir `take`; si sigue `other`, alguien más lo tiene: cerrarlo |
| 2. Tasas (H0) | `rate -20 0`, a los 2 s `rate 0 0` | El pitch baja ~40° y se detiene | Sin esto no sigue la prueba: el camino PX4 → Orus no funciona |
| 3. Ángulo 0/0 en *follow* (H1) | `mode 2` (follow, por la Orus L), `angle 0 0` | ¿El pitch vuelve a 0? | — |
| 4. Otro ángulo (H2) | `angle -30 45` | ¿Pitch −30, yaw 45? | — |
| 5. Ángulo en *lock* (H3) | `mode 1` (lock), `rate -20 0` 2 s, `rate 0 0`, `angle 0 0`, luego `angle -30 45` | ¿Obedece ahora? | — |
| 6. Ángulo directo (H4) | `release`, `proto 2`, `mode 2`, `rate -20 0` 2 s, `rate 0 0`, `angle 0 0`, luego `angle -30 45` | ¿Obedece? Ojo: el PX4 sigue mandando su "hold" al gimbal | — |
| 7. `DO_MOUNT_CONTROL` (H5) | `proto 1`, `angle 0 0`, luego `angle -30 45` | ¿Obedece? ¿Llega `COMMAND_RESULT` con `result=0`? | — |
| 8. Reset moderado (H6) | `proto 3`, `take`, `rate -20 0` 1.5 s, `rate 0 0` (pitch ~−30), `param GB_MODE 4` | Centra y vuelve al modo anterior, sin vaivén | — |
| 9. Reset desde el nadir (H7, opcional) | `rate -30 0` hasta pitch ≈ −75, `rate 0 0`, `param GB_MODE 4` | ¿Vaivén como el del 2026-10-15? | Si oscila, no tocar: termina solo en ~10 s |
| 10. Cerrar | `release`, `quit` | `control=nobody` | — |

Con `proto 3`, `mode 4` no es `GB_MODE` = 4: es el centrado actual de la app (ángulo 0/0 por el
manager). Por eso los pasos 8 y 9 usan `param GB_MODE 4`. `mode 1` y `mode 2` sí son `GB_MODE`.

Si un `mode` o `param` no recibe `[event] PARAM_RESULT … result=0`, repetirlo: pudo ir a la
cámara del Herelink (`tlog-gimbal.py` muestra a quién se mandó).

## Análisis

```sh
python3 tools/tlog-gimbal.py logs/centrado-*.tlog
```

La lista final da, para cada ángulo o reset que mandamos, el pitch y el yaw antes y 3 s
después. En tierra el yaw contra la nariz no cambia solo, así que aquí también sirve. Enviar el
`.tlog` y la hoja para registrar el resultado en `docs/protocol/orus-l.md` y en la tabla de
pruebas de `docs/pending-android-desktop.md`.

## Qué se decide con el resultado

| Resultado | Centrado en la app |
|---|---|
| H3 sí, H1 no (solo obedece en *lock*) | Ángulo por el manager con `YAW_LOCK` mientras centra |
| H1–H5 no, H0 sí (solo tasas) | Centrado por tasas en el core: tasa proporcional hacia 0/0 con la actitud del gimbal, por el manager, hasta llegar |
| H5 sí, manager no | `DO_MOUNT_CONTROL` directo, con el control del manager soltado mientras centra |
| H6 sí y H7 sin vaivén | `GB_MODE` = 4 puede volver, soltando antes el control del manager para que el PX4 no mande "hold" a la vez |
| H7 con vaivén | `GB_MODE` = 4 nunca cerca del nadir: primero subir por tasas a −45°, luego reset |

En todos los casos, CENTER debe avisar cuando la app no tiene el control del manager, en vez
de mandar un setpoint que el PX4 descarta.

## Resultados (2026-10-27)

Tres sesiones de `imago-cli` en tierra (`logs/centrado-20261027-1308`, `-1320`, `-1329`). En
las dos primeras no se tomó el control del manager, así que sus pasos por el manager no
cuentan; la tercera sí lo tomó.

| Hipótesis | Resultado |
|---|---|
| H0 tasas por el manager | Sí, con el control tomado; a ~25 % de la tasa pedida (también directo) |
| H1 ángulo 0/0 por el manager | **No**: el PX4 lo reenvía y la Orus L lo ignora (también en el vuelo de 12:07) |
| H2 otro ángulo | Sí: `angle 1 1` y `angle -30 45` se alcanzan en ~2 s (lock, control tomado). Lo ignorado es el 0/0 exacto (cuaternión identidad) |
| H3 lock contra follow | El 0/0 se ignora en los dos; en follow con control no se probó otro ángulo |
| H4 ángulo directo | 0/0 ignorado; otros ángulos avanzan ~25 % y se detienen |
| H5 `DO_MOUNT_CONTROL` | Denegado (`MAV_RESULT_DENIED`) o ignorado |
| H6 `GB_MODE` 4 moderado | Limpio desde −56° y −69° (control tomado) |
| H7 `GB_MODE` 4 en el nadir | Limpio desde −77° (control tomado) y −87° (sin control); sin vaivén |

También: el acuse de `GB_MODE` llega 1.7–2.1 s después (cuatro envíos con el reintento de
500 ms); la bandera NEUTRAL del gimbal sale en ~0.3 s; tras un reset en follow el pitch baja
solo hasta −16° (~0.3°/s), en lock no.

Decisión aplicada en el core: CENTER = `GB_MODE` 4 en todos los protocolos, un solo envío,
aceptado por NEUTRAL o por la actitud; los demás `GB_MODE` esperan 3 s antes de reenviarse.
