package aero.imago.payloadcontrol.ui

import android.content.Intent
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.FileProvider
import java.io.File
import java.util.Locale

/** Network and latency figures, to tell app delays from datalink delays in the field. */
@Composable
fun HUDView(modifier: Modifier) {
    val model = LocalModel.current
    val m = model.metrics
    // Taps on the figures reach the video below (tap to track); only the log buttons take them.
    Column(modifier.width(280.dp).panel(14.dp).padding(12.dp), verticalArrangement = Arrangement.spacedBy(3.dp)) {
        Row(Modifier.fillMaxWidth().height(24.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("LINK", style = AppFont.display(10, 700, Theme.magenta).copy(letterSpacing = 1.5.sp))
            Box(Modifier.weight(1f))
            LogControls()
        }
        MetricRow("Video", if (m.active) "${m.codec} · ${m.transport}" else "off")
        MetricRow("Frame rate", fmt("%.1f fps", m.fps))
        MetricRow("Bitrate", fmt("%.1f Mb/s", m.kbps / 1000))
        MetricRow("Lost / late", "${m.packetsLost} / ${m.packetsLate}")
        MetricRow("Jitter", fmt("%.1f ms", m.jitterMs))
        MetricRow("Frame transfer", fmt("%.1f ms", m.transferMs))
        // About one frame period on a smooth link; bursts show as gaps of several.
        MetricRow("Frame gaps p95 / max", String.format(Locale.US, "%.0f / %.0f ms", m.frameGapP95Ms, m.frameGapMaxMs))
        MetricRow("Behind live", "${Math.round(m.lagMs)} ms · ${m.resyncs} resync${if (m.resyncs == 1) "" else "s"}")
        Divider()
        MetricRow("App: assemble", fmt("%.2f ms", m.assemblyMs))
        MetricRow("App: decode", fmt("%.1f ms", m.decodeMs))
        if (m.presentMs > 0f) {
            MetricRow("App: shown (smoothing)", fmt("%.0f ms", m.presentMs))
        }
        // Where frozen pictures come from: lost packets, the decoder, or the display.
        MetricRow("Corrupt / failed / skipped", "${m.framesCorrupt} / ${m.decodeFailures} / ${m.framesSkipped}")
        Divider()
        MetricRow("Command RTT", "${model.link.roundTripMs} ms (last ${m.roundTripLastMs.toInt()})")
        MetricRow("MAVLink rx / gaps", "${m.mavlinkReceived} / ${m.mavlinkGaps}")
        // Acknowledgements the payload sent to the autopilot or a GCS: they command it too.
        MetricRow("Others' commands", "${model.foreignCommandCount}")
        if (model.info.model.isNotEmpty()) MetricRow("Payload", "${model.info.model} ${model.info.softwareVersion}")
        // Which value means which thermal core (palette names) is still to be confirmed.
        if (model.thermal.cameraType >= 0) MetricRow("IR sensor type", "${model.thermal.cameraType}")
    }
}

private fun fmt(format: String, value: Float) = String.format(Locale.US, format, value)

@Composable
private fun Divider() {
    Box(Modifier.fillMaxWidth().padding(vertical = 3.dp).height(1.dp).background(Theme.stroke))
}

@Composable
private fun MetricRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(14.dp)) {
        Text(label, style = AppFont.body(12, 500, Theme.textSecondary), modifier = Modifier.weight(1f), maxLines = 1)
        Text(value, style = AppFont.figures(12, 600), maxLines = 1)
    }
}

/** Record all MAVLink to a .tlog, and share the last one. */
@Composable
private fun LogControls() {
    val model = LocalModel.current
    val context = LocalContext.current
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        val file = model.lastLogFile
        if (file != null && !model.isRecordingLog) {
            Icon(Icons.Filled.Share, contentDescription = "Share MAVLink log", tint = Theme.text,
                 modifier = Modifier.size(18.dp).clickable(role = Role.Button) { shareLog(context, file) })
        }
        Row(
            Modifier
                .background(Theme.surfaceRaised, CircleShape)
                .clickable(role = Role.Button) { model.toggleLogRecording() }
                .semantics { contentDescription = if (model.isRecordingLog) "Stop MAVLink log" else "Record MAVLink log" }
                .padding(horizontal = 8.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(5.dp),
        ) {
            Box(Modifier.size(8.dp).background(if (model.isRecordingLog) Theme.danger else Theme.textTertiary, CircleShape))
            Text(if (model.isRecordingLog) "Stop log" else "Record log", style = AppFont.body(11, 600))
        }
    }
}

private fun shareLog(context: android.content.Context, file: File) {
    val uri = FileProvider.getUriForFile(context, "${context.packageName}.files", file)
    val send = Intent(Intent.ACTION_SEND).apply {
        type = "application/octet-stream"
        putExtra(Intent.EXTRA_STREAM, uri)
        putExtra(Intent.EXTRA_SUBJECT, file.name)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(send, "Share MAVLink log"))
}
