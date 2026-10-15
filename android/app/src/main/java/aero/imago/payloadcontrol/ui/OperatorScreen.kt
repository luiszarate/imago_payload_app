package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.app.AppModel
import aero.imago.payloadcontrol.video.VideoSurface
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material.icons.filled.Sync
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import java.util.Locale

/** The single operating screen: full-screen video with control rails floating over it. */
@Composable
fun OperatorScreen(model: AppModel) {
    val compact = with(LocalDensity.current) { LocalWindowInfo.current.containerSize.height.toDp() } < 480.dp
    CompositionLocalProvider(LocalModel provides model, LocalCompact provides compact) {
        Box(Modifier.fillMaxSize().background(Theme.background)) {
            VideoSurface(model.video, model.videoWidth, model.videoHeight, Modifier.fillMaxSize())

            if (model.isConnected) {
                VideoOverlay(Modifier.fillMaxSize())
            }

            // While the stream restarts to catch up, the last picture stays instead of the panel.
            if (!model.isConnected || (!model.videoActive && !model.videoResyncing)) {
                ConnectPanel(Modifier.align(Alignment.Center))
            }

            // Controls stay clear of a camera cutout; the video runs under it.
            val safe = Modifier.windowInsetsPadding(WindowInsets.displayCutout.only(WindowInsetsSides.Horizontal))
            val gutter = if (compact) 12.dp else 16.dp

            TopBar(safe.fillMaxWidth().padding(horizontal = gutter).padding(top = if (compact) 6.dp else 10.dp))

            if (model.isConnected) {
                Row(
                    safe.fillMaxSize().padding(horizontal = gutter).padding(top = if (compact) 44.dp else 56.dp,
                                                                         bottom = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    CameraRail()
                    Spacer(Modifier.weight(1f))
                    GimbalRail()
                }
            }

            if (model.isConnected && model.showAttitude) {
                AttitudeIndicator(Modifier.align(Alignment.BottomCenter).padding(bottom = if (compact) 6.dp else 12.dp))
            }

            if (model.isConnected && (model.videoLagMs >= AppModel.LAG_WARNING_MS || model.videoResyncing)) {
                VideoLagBadge(model.videoLagMs, model.videoResyncing,
                              Modifier.align(Alignment.TopCenter).padding(top = if (compact) 46.dp else 58.dp))
            }

            if (model.showHUD && model.isConnected) {
                HUDView(Modifier.align(Alignment.TopEnd)
                            .padding(top = if (compact) 50.dp else 66.dp, end = if (compact) 160.dp else 180.dp))
            }

            ToastView(raised = model.isConnected && model.showAttitude, compact = compact,
                      modifier = Modifier.align(Alignment.BottomCenter))

            if (model.showConnections) {
                BackHandler { model.showConnections = false }
                ConnectionsScreen(onDone = { model.showConnections = false })
            }
            if (model.showSettings) {
                BackHandler { model.showSettings = false }
                SettingsScreen(onDone = { model.showSettings = false })
            }
        }
    }
}

/** Says the picture is behind live, so nobody steers by a stale image without knowing. */
@Composable
private fun VideoLagBadge(lagMs: Int, resyncing: Boolean, modifier: Modifier) {
    Row(
        modifier.background(Theme.warning, CircleShape).padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        Icon(if (resyncing) Icons.Filled.Sync else Icons.Filled.Schedule, contentDescription = null,
             tint = Theme.background, modifier = Modifier.size(14.dp))
        Text(
            if (resyncing) "CATCHING UP WITH LIVE VIDEO"
            else String.format(Locale.US, "VIDEO %.1f s BEHIND", lagMs / 1000.0),
            style = AppFont.figures(12, 700, Theme.background),
        )
    }
}

/** Temporary messages: command failures, link changes. */
@Composable
private fun ToastView(raised: Boolean, compact: Boolean, modifier: Modifier) {
    val model = LocalModel.current
    val toast = model.toast
    // The last message stays drawn while it slides away.
    var last by remember { mutableStateOf(toast) }
    if (toast != null) last = toast
    Box(modifier.padding(bottom = if (raised) (if (compact) 178.dp else 238.dp) else 18.dp)) {
        AnimatedVisibility(
            visible = toast != null,
            enter = slideInVertically { it } + fadeIn(),
            exit = slideOutVertically { it } + fadeOut(),
        ) {
            val shown = last ?: return@AnimatedVisibility
            val error = shown.style == AppModel.Toast.Style.ERROR
            Row(
                Modifier.widthIn(max = 560.dp).panel(14.dp).padding(horizontal = 16.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Icon(if (error) Icons.Filled.Warning else Icons.Filled.Info, contentDescription = null,
                     tint = if (error) Theme.danger else Theme.magenta, modifier = Modifier.size(18.dp))
                Text(shown.text, style = AppFont.body(14, 600), textAlign = TextAlign.Start)
            }
        }
    }
}
