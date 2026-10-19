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
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import java.util.Locale

/** The single operating screen: full-screen video with control rails floating over it. */
@Composable
fun OperatorScreen(model: AppModel) {
    val density = LocalDensity.current
    val compact = with(density) { LocalWindowInfo.current.containerSize.height.toDp() } < 480.dp
    // Widths of the side rails, so the thermal bar fits between them.
    var cameraRailWidth by remember { mutableStateOf(0.dp) }
    var gimbalRailWidth by remember { mutableStateOf(0.dp) }
    // Right edge of the camera rail on screen: the picture starts after it.
    var cameraRailEdge by remember { mutableStateOf(0.dp) }
    // Where the gimbal rail ends: on a phone it reaches down beside the thermal bar.
    var gimbalRailBottom by remember { mutableStateOf(0.dp) }
    // The thermal bar starts right of the attitude indicator.
    var attitudeWidth by remember { mutableStateOf(0.dp) }
    var screenWidth by remember { mutableStateOf(0.dp) }
    var screenHeight by remember { mutableStateOf(0.dp) }
    CompositionLocalProvider(LocalModel provides model, LocalCompact provides compact) {
        Box(
            Modifier.fillMaxSize().background(Theme.background).onSizeChanged {
                screenWidth = with(density) { it.width.toDp() }
                screenHeight = with(density) { it.height.toDp() }
            },
        ) {
            // The camera draws its OSD down the left edge of the picture, under the camera rail
            // (field report 2026-10-15). With the setting on, the picture fits right of the rail
            // instead: a little smaller on a tablet, only shifted on a phone, whose picture is
            // narrower than the screen anyway. Taps, AI boxes and the reticle follow, because both
            // views fit the picture in the same frame.
            val videoInset = if (model.isConnected && model.videoClearOfCameraRail && cameraRailEdge > 0.dp) {
                cameraRailEdge + 8.dp
            } else 0.dp
            VideoSurface(model.video, model.videoWidth, model.videoHeight,
                         Modifier.fillMaxSize().padding(start = videoInset))

            if (model.isConnected) {
                VideoOverlay(Modifier.fillMaxSize().padding(start = videoInset))
            }

            // While the stream restarts to catch up, the last picture stays instead of the panel.
            if (!model.isConnected || (!model.videoActive && !model.videoResyncing)) {
                ConnectPanel(Modifier.align(Alignment.Center))
            }

            // Controls stay clear of a camera cutout; the video runs under it.
            val safe = Modifier.windowInsetsPadding(WindowInsets.displayCutout.only(WindowInsetsSides.Horizontal))
            val gutter = if (compact) 12.dp else 16.dp
            val bottomMargin = if (compact) 6.dp else 10.dp
            // The thermal bar runs from the attitude indicator (or the camera rail when the
            // indicator is hidden) to the right edge, or to the gimbal rail when the rail reaches
            // down beside it.
            val thermalLeading = if (model.showAttitude && attitudeWidth > 0.dp) {
                screenWidth / 2 + attitudeWidth / 2 + 10.dp
            } else gutter + cameraRailWidth + 10.dp
            val thermalBarTop = screenHeight - bottomMargin - thermalBarHeight(compact)
            val thermalTrailing = if (gimbalRailBottom > thermalBarTop - 8.dp) gutter + gimbalRailWidth + 10.dp else gutter
            val thermalBar = model.showsThermalBar

            TopBar(safe.fillMaxWidth().padding(horizontal = gutter).padding(top = if (compact) 6.dp else 10.dp))

            if (model.isConnected) {
                Row(
                    safe.fillMaxSize().padding(horizontal = gutter).padding(top = if (compact) 44.dp else 56.dp,
                                                                         bottom = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Box(
                        Modifier.onGloballyPositioned {
                            cameraRailWidth = with(density) { it.size.width.toDp() }
                            cameraRailEdge = with(density) { it.boundsInRoot().right.toDp() }
                        },
                    ) {
                        CameraRail()
                    }
                    Spacer(Modifier.weight(1f))
                    Box(
                        Modifier.onGloballyPositioned {
                            gimbalRailWidth = with(density) { it.size.width.toDp() }
                            gimbalRailBottom = with(density) { it.boundsInRoot().bottom.toDp() }
                        },
                    ) {
                        GimbalRail()
                    }
                }
            }

            if (thermalBar) {
                // Bottom right, so the attitude indicator keeps its place in the centre. The start
                // inset counts from the screen's edge, so only the end keeps clear of a cutout.
                ThermalBar(Modifier.align(Alignment.BottomStart)
                               .windowInsetsPadding(WindowInsets.displayCutout.only(WindowInsetsSides.End))
                               .fillMaxWidth()
                               .padding(start = thermalLeading, end = thermalTrailing, bottom = bottomMargin))
            }

            if (model.isConnected && model.showAttitude) {
                AttitudeIndicator(
                    Modifier.align(Alignment.BottomCenter).padding(bottom = if (compact) 6.dp else 12.dp)
                        .onSizeChanged { attitudeWidth = with(density) { it.width.toDp() } }
                )
            }

            if (model.isConnected && (model.videoLagMs >= AppModel.LAG_WARNING_MS || model.videoResyncing)) {
                VideoLagBadge(model.videoLagMs, model.videoResyncing,
                              Modifier.align(Alignment.TopCenter).padding(top = if (compact) 46.dp else 58.dp))
            }

            if (model.showHUD && model.isConnected) {
                HUDView(Modifier.align(Alignment.TopEnd)
                            .padding(top = if (compact) 50.dp else 66.dp, end = if (compact) 160.dp else 180.dp))
            }

            // Toasts sit above whatever occupies the bottom centre: the attitude indicator, or the
            // thermal bar when the indicator is hidden.
            val toastInset = when {
                !model.isConnected -> 18.dp
                model.showAttitude -> if (compact) 178.dp else 238.dp
                thermalBar -> bottomMargin + thermalBarHeight(compact) + 10.dp
                else -> 18.dp
            }
            ToastView(toastInset, Modifier.align(Alignment.BottomCenter))

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
private fun ToastView(bottomInset: Dp, modifier: Modifier) {
    val model = LocalModel.current
    val toast = model.toast
    // The last message stays drawn while it slides away.
    var last by remember { mutableStateOf(toast) }
    if (toast != null) last = toast
    Box(modifier.padding(bottom = bottomInset)) {
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
