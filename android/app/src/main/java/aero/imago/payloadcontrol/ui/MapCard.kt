package aero.imago.payloadcontrol.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.util.Locale
import kotlin.math.roundToInt

/** The map's faint "show the map" button, put away (MapOverlay's MapButton). */
val MAP_BUTTON_SIDE = 30.dp

/** The tablet map card's width while the attitude indicator is hidden: the dial's square and padding. */
val MAP_CARD_WIDTH: Dp get() = AttitudeCard.dialSide(false) + AttitudeCard.padding(false).width * 2

/**
 * The map on a tablet: a twin of the attitude indicator's card beside it, of one size, on one
 * baseline, with the same padding, corners and readout row (north up and the aircraft's
 * heading, which the phone's map draws on the map itself). A map of its own size next to the
 * indicator looked out of line (operator, 2026-11-19). Put away, only the map's faint button is
 * left. `twin`: the attitude indicator's card, to match; null while it is hidden (the card then
 * takes the dial's measures).
 */
@Composable
fun MapCard(twin: DpSize?, modifier: Modifier = Modifier) {
    val model = LocalModel.current
    if (!model.map.settings.visible) {
        MapOverlay(MAP_BUTTON_SIDE, modifier) // its "show the map" button
        return
    }
    val inset = AttitudeCard.padding(false)
    val side = AttitudeCard.dialSide(false)
    val width = twin?.let { maxOf(it.width - inset.width * 2, side) } ?: side
    Column(
        modifier
            .then(if (twin != null) Modifier.size(twin) else Modifier)
            .panel(AttitudeCard.CORNER_RADIUS)
            // A touch between the map and the card's edge is not a track on the picture.
            .blockTouches()
            .padding(horizontal = inset.width, vertical = inset.height),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(AttitudeCard.rowSpacing(false)),
    ) {
        MapOverlay(width, height = side, legend = false)
        Row(horizontalArrangement = Arrangement.spacedBy(14.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("N ↑", style = AppFont.display(10, 700, Theme.magenta, 1.sp))
            InstrumentReadout("HDG", headingText(model.vehicleHeading), compact = false)
        }
    }
}

private fun headingText(heading: Float?): String {
    if (heading == null) return "—"
    return String.format(Locale.US, "%03d°", (heading.roundToInt() % 360 + 360) % 360)
}

/**
 * The aircraft's own figures on a tablet, one per line in a small panel left of the map's card,
 * on its baseline (the phone keeps them as a pill over its map). Hidden when the aircraft
 * reports none of them.
 */
@Composable
fun AircraftFiguresPanel(width: Dp, modifier: Modifier = Modifier) {
    val aircraft = LocalModel.current.aircraft
    if (aircraft.isEmpty) return
    val items = aircraft.items
    Column(
        modifier
            .width(width)
            .panel(12.dp)
            .blockTouches()
            .padding(horizontal = 10.dp, vertical = 7.dp)
            .semantics {
                contentDescription = "Aircraft"
                stateDescription = items.joinToString(", ") { "${it.first} ${it.second}" }
            },
        verticalArrangement = Arrangement.spacedBy(3.dp),
    ) {
        for ((caption, figure) in items) {
            val color: Color = when {
                caption != "BAT" -> Theme.text
                aircraft.batteryCritical -> Theme.danger
                aircraft.batteryWarning -> Theme.warning
                else -> Theme.text
            }
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(caption, style = AppFont.display(10, 700, Theme.textTertiary, 0.8.sp), maxLines = 1,
                     modifier = Modifier.width(26.dp))
                Text(figure, style = AppFont.figures(12, 600, color), maxLines = 1)
            }
        }
    }
}

/** Space between the instruments of the tablet's row. */
val INSTRUMENT_SPACING = 10.dp

/** The aircraft figures' panel beside the map's card, and the least room it takes there. */
val AIRCRAFT_FIGURES_WIDTH = 150.dp
val AIRCRAFT_FIGURES_MIN_WIDTH = 120.dp

/**
 * The tablet's instruments left of the centre: the aircraft's figures, then the map's card. The
 * figures sit left of the card on its baseline, or above it when `room` (what the camera rail
 * leaves beside the card) is under 120 dp (a small tablet).
 */
@Composable
fun TabletMapAndFigures(twin: DpSize?, room: Dp, modifier: Modifier) {
    if (room >= AIRCRAFT_FIGURES_MIN_WIDTH) {
        Row(modifier, verticalAlignment = Alignment.Bottom,
            horizontalArrangement = Arrangement.spacedBy(INSTRUMENT_SPACING)) {
            AircraftFiguresPanel(minOf(AIRCRAFT_FIGURES_WIDTH, room))
            MapCard(twin)
        }
    } else {
        Column(modifier, horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.spacedBy(6.dp)) {
            AircraftFiguresPanel(AIRCRAFT_FIGURES_WIDTH)
            MapCard(twin)
        }
    }
}
