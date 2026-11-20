package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.core.GeoPoint
import aero.imago.payloadcontrol.core.TargetPosition
import aero.imago.payloadcontrol.map.MapStyle
import aero.imago.payloadcontrol.map.TileId
import aero.imago.payloadcontrol.map.TileMath
import aero.imago.payloadcontrol.map.TileStore
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Flight
import androidx.compose.material.icons.filled.Map
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material.icons.filled.Satellite
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import java.util.Locale
import kotlin.math.floor
import kotlin.math.roundToInt

/** A 256 px tile is drawn this fraction of the map's side, so the map spans as much ground as the Quest's. */
private const val TILE_FRACTION = 170f / 300f
/** Translucent until touched; touched, it is opaque and shows its buttons for this long. */
private const val MAP_ACTIVE_MS = 5000L
private const val MAP_IDLE_ALPHA = 0.8f

/**
 * A small north-up map, as on the Quest (on a tablet in a card twin of the attitude indicator's,
 * MapCard; on a phone in the picture's bottom right corner): the aircraft in the middle turned
 * to its heading, the camera's cone (as wide as its field of view), and the payload's target
 * point. Tiles come only from this device's store (Settings › Map); a missing one shows as a
 * grid. A tap shows its buttons: put away, satellite / street, zoom. `legend`: north up and the
 * heading drawn on the map; off in the tablet's card, which shows them in its readout row as the
 * attitude indicator does.
 */
@Composable
fun MapOverlay(width: Dp, modifier: Modifier = Modifier, height: Dp = width, legend: Boolean = true) {
    val model = LocalModel.current
    val map = model.map
    val settings = map.settings
    var touches by remember { mutableIntStateOf(0) }
    var active by remember { mutableStateOf(false) }
    LaunchedEffect(touches) {
        if (touches == 0) return@LaunchedEffect
        active = true
        delay(MAP_ACTIVE_MS)
        active = false
    }

    if (!settings.visible) {
        // Put away: a faint button in the corner brings it back.
        MapButton(Icons.Filled.Map, "Show the map", modifier.alpha(0.6f)) { map.update(settings.copy(visible = true)) }
        return
    }

    val shape = RoundedCornerShape(16.dp)
    val measurer = rememberTextMeasurer()
    val position = model.aircraftPosition
    val heading = model.vehicleHeading
    Box(
        modifier
            .size(width, height)
            .graphicsLayer { alpha = if (active) 1f else MAP_IDLE_ALPHA }
            .clip(shape)
            .background(Theme.surfaceRaised)
            .border(1.dp, Color.White.copy(alpha = 0.28f), shape)
            // Every touch on the map is the map's (never a track on the picture under it), and
            // wakes its buttons; the buttons still get the touch.
            .pointerInput(Unit) {
                awaitEachGesture {
                    awaitFirstDown(requireUnconsumed = false, pass = PointerEventPass.Initial)
                    touches += 1
                }
            }
            .semantics {
                contentDescription = "Map"
                stateDescription = if (position == null) "No aircraft position" else "Centred on the aircraft"
            },
    ) {
        if (position != null) {
            Canvas(Modifier.fillMaxSize()) {
                // Read here, so a tile that finishes loading redraws only the map.
                @Suppress("UNUSED_VARIABLE") val version = map.tileVersion
                val gimbal = model.gimbal
                // Where the camera looks from North: the bearing the core works out, else the
                // aircraft's heading plus the gimbal's yaw (the attitude indicator's cone).
                val bearing = gimbal.yawNorth ?: if (heading != null && gimbal.yaw != null) heading + gimbal.yaw else null
                drawMap(
                    map.tiles, settings.style, settings.zoom, position, heading, bearing,
                    model.camera.horizontalFOV, model.target, measurer,
                )
            }
        } else {
            Text("No aircraft position", style = AppFont.body(12, 600, Theme.textSecondary),
                 modifier = Modifier.align(Alignment.Center).padding(horizontal = 8.dp))
        }
        val label = shadowed(AppFont.figures(11, 700, Theme.magenta))
        if (!active) {
            if (legend) Row(Modifier.align(Alignment.TopStart).padding(start = 8.dp, top = 6.dp),
                horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("N ↑", style = label)
                heading?.let {
                    Text(String.format(Locale.US, "HDG %03d°", it.roundToInt() % 360), style = label.copy(color = Theme.text))
                }
            }
        } else {
            Row(Modifier.align(Alignment.TopEnd).padding(5.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                MapButton(Icons.Filled.Close, "Hide the map") { map.update(settings.copy(visible = false)) }
                val satellite = settings.style == MapStyle.SATELLITE
                MapButton(if (satellite) Icons.Filled.Map else Icons.Filled.Satellite,
                          if (satellite) "Street map" else "Satellite") {
                    map.update(settings.copy(style = if (satellite) MapStyle.STREET else MapStyle.SATELLITE))
                }
                MapButton(Icons.Filled.Add, "Zoom in", enabled = settings.zoom < TileMath.MAX_ZOOM) {
                    map.update(settings.copy(zoom = settings.zoom + 1))
                }
                MapButton(Icons.Filled.Remove, "Zoom out", enabled = settings.zoom > TileMath.MIN_ZOOM) {
                    map.update(settings.copy(zoom = settings.zoom - 1))
                }
            }
        }
        Text(settings.credit(settings.style), style = shadowed(AppFont.body(8, 600, Theme.textSecondary)),
             maxLines = 1, overflow = TextOverflow.Ellipsis,
             modifier = Modifier.align(Alignment.BottomStart).padding(start = 7.dp, end = 7.dp, bottom = 4.dp))
    }
}

/**
 * The aircraft's own figures, when its autopilot's telemetry reaches the app: altitude above
 * home, ground and air speed, battery. Hidden when there is none.
 */
@Composable
fun AircraftFigures(modifier: Modifier = Modifier) {
    val aircraft = LocalModel.current.aircraft
    if (aircraft.isEmpty) return
    val items = aircraft.items
    Row(
        modifier
            .panel(14.dp)
            .blockTouches()
            .padding(horizontal = 10.dp, vertical = 6.dp)
            .semantics {
                contentDescription = "Aircraft"
                stateDescription = items.joinToString(", ") { "${it.first} ${it.second}" }
            },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(9.dp),
    ) {
        Icon(Icons.Filled.Flight, contentDescription = null, tint = Theme.textSecondary, modifier = Modifier.size(14.dp))
        for ((caption, figure) in items) {
            val color = when {
                caption != "BAT" -> Theme.text
                aircraft.batteryCritical -> Theme.danger
                aircraft.batteryWarning -> Theme.warning
                else -> Theme.text
            }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(caption, style = AppFont.display(10, 700, Theme.textTertiary).copy(letterSpacing = 1.sp))
                Text(figure, style = AppFont.figures(13, 600, color), maxLines = 1)
            }
        }
    }
}

@Composable
private fun MapButton(icon: ImageVector, label: String, modifier: Modifier = Modifier, enabled: Boolean = true,
                      onClick: () -> Unit) {
    Box(
        modifier
            .size(30.dp)
            .background(Theme.surface.copy(alpha = 0.85f), CircleShape)
            .border(1.dp, Theme.stroke, CircleShape)
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick)
            .semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = null, tint = if (enabled) Theme.text else Theme.textTertiary,
             modifier = Modifier.size(16.dp))
    }
}

private fun shadowed(style: TextStyle) = style.copy(shadow = Shadow(Color.Black.copy(alpha = 0.85f), Offset(0f, 1f), 3f))

private fun DrawScope.drawMap(
    tiles: TileStore, style: MapStyle, zoom: Int, position: GeoPoint, heading: Float?, bearing: Float?,
    fieldOfView: Float?, target: TargetPosition?, measurer: TextMeasurer,
) {
    val c = center
    val n = 1 shl zoom
    val tx = TileMath.x(position.longitude, zoom)
    val ty = TileMath.y(position.latitude, zoom)
    // Scaled by the shorter side, so a wide map (the tablet's card) shows more ground, not bigger tiles.
    val tilePx = minOf(size.width, size.height) * TILE_FRACTION
    val halfX = size.width / 2 / tilePx
    val halfY = size.height / 2 / tilePx
    var shown = 0
    for (col in floor(tx - halfX).toInt()..floor(tx + halfX).toInt()) {
        for (row in floor(ty - halfY).toInt()..floor(ty + halfY).toInt()) {
            val left = c.x + ((col - tx) * tilePx).toFloat()
            val top = c.y + ((row - ty) * tilePx).toFloat()
            val image = if (row in 0 until n) tiles.bitmap(style, TileId(zoom, Math.floorMod(col, n), row)) else null
            if (image != null) {
                // Whole pixels, each tile reaching the next one's edge: no seams between them.
                val x0 = floor(left).toInt()
                val y0 = floor(top).toInt()
                drawImage(
                    image, dstOffset = IntOffset(x0, y0),
                    dstSize = IntSize(floor(left + tilePx).toInt() + 1 - x0, floor(top + tilePx).toInt() + 1 - y0),
                    filterQuality = FilterQuality.Low,
                )
                shown++
            } else {
                // A grid where there is no tile: the scale still reads.
                drawRect(Theme.stroke, Offset(left, top), Size(tilePx, tilePx), style = Stroke(1.dp.toPx()))
            }
        }
    }
    if (shown == 0) {
        val text = measurer.measure("No map stored here.\nDownload it in Settings › Map.",
                                    AppFont.body(10, 600, Theme.textSecondary).copy(textAlign = TextAlign.Center))
        drawText(text, topLeft = Offset(c.x - text.size.width / 2f, size.height * 0.2f))
    }
    if (bearing != null) drawCone(c, minOf(size.width, size.height) * 0.4f, bearing, fieldOfView)
    if (target != null) {
        val p = Offset(c.x + ((TileMath.x(target.longitude, zoom) - tx) * tilePx).toFloat(),
                       c.y + ((TileMath.y(target.latitude, zoom) - ty) * tilePx).toFloat())
        drawTargetMark(p)
    }
    if (heading != null) {
        rotate(heading, c) { drawAircraftSymbol(c, 11.dp.toPx()) }
    } else {
        // The heading is not known: a dot.
        drawCircle(Theme.text, 4.dp.toPx(), c)
    }
}

private fun DrawScope.drawCone(c: Offset, length: Float, bearing: Float, fieldOfView: Float?) {
    val half = ((fieldOfView ?: 30f) / 2).coerceIn(3f, 40f)
    val wedge = Path().apply {
        moveTo(c.x, c.y)
        arcTo(Rect(c, length), bearing - half - 90, half * 2, false)
        close()
    }
    drawPath(wedge, Brush.radialGradient(listOf(Theme.magenta.copy(alpha = 0.6f), Theme.fuchsia.copy(alpha = 0.1f)), c, length))
    drawPath(wedge, Theme.magenta, style = Stroke(1.5f.dp.toPx()))
}

private fun DrawScope.drawTargetMark(p: Offset) {
    val line = 2.dp.toPx()
    val inner = 3.dp.toPx()
    val outer = 9.dp.toPx()
    for ((color, width) in listOf(Color.Black.copy(alpha = 0.6f) to line * 2, Theme.text to line)) {
        drawCircle(color, 6.dp.toPx(), p, style = Stroke(width))
        drawLine(color, Offset(p.x - outer, p.y), Offset(p.x - inner, p.y), width)
        drawLine(color, Offset(p.x + inner, p.y), Offset(p.x + outer, p.y), width)
        drawLine(color, Offset(p.x, p.y - outer), Offset(p.x, p.y - inner), width)
        drawLine(color, Offset(p.x, p.y + inner), Offset(p.x, p.y + outer), width)
    }
}

/** The attitude indicator's silhouette, nose up (the canvas is turned to the heading). */
private fun DrawScope.drawAircraftSymbol(c: Offset, size: Float) {
    val right = AIRCRAFT_OUTLINE
    val left = right.reversed().drop(1).dropLast(1).map { Offset(-it.x, it.y) }
    val shape = Path()
    (right + left).forEachIndexed { index, p ->
        val x = c.x + p.x * size
        val y = c.y - p.y * size
        if (index == 0) shape.moveTo(x, y) else shape.lineTo(x, y)
    }
    shape.close()
    drawPath(shape, Theme.text)
    drawPath(shape, Theme.background.copy(alpha = 0.85f), style = Stroke(1.5f.dp.toPx()))
}
