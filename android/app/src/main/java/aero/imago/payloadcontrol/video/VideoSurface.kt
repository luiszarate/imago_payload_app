package aero.imago.payloadcontrol.video

import aero.imago.payloadcontrol.core.VideoRenderer
import android.annotation.SuppressLint
import android.content.Context
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.viewinterop.AndroidView

/**
 * The decoder renders straight into this SurfaceView's surface, which the system compositor
 * shows without passing through the app's own drawing (unlike a TextureView). The view is sized
 * to the picture's aspect ratio, since the decoder stretches to fill it.
 */
@Composable
fun VideoSurface(renderer: VideoRenderer, videoWidth: Int, videoHeight: Int, modifier: Modifier = Modifier) {
    BoxWithConstraints(modifier, contentAlignment = Alignment.Center) {
        val density = LocalDensity.current
        val container = with(density) { Size(maxWidth.toPx(), maxHeight.toPx()) }
        val rect = fitRect(videoWidth, videoHeight, container)
        Box(Modifier.size(with(density) { rect.width.toDp() }, with(density) { rect.height.toDp() })) {
            AndroidView(factory = { context -> VideoSurfaceView(context, renderer) }, modifier = Modifier.fillMaxSize())
        }
    }
}

/** Where a picture sits inside a container (aspect fit); 16:9 until its size is known. */
fun fitRect(videoWidth: Int, videoHeight: Int, container: Size): Rect {
    val aspect = if (videoWidth > 0 && videoHeight > 0) videoWidth.toFloat() / videoHeight else 16f / 9f
    if (container.width <= 0 || container.height <= 0) return Rect.Zero
    var width = container.width
    var height = width / aspect
    if (height > container.height) {
        height = container.height
        width = height * aspect
    }
    val left = (container.width - width) / 2
    val top = (container.height - height) / 2
    return Rect(left, top, left + width, top + height)
}

@SuppressLint("ViewConstructor") // created in code only
private class VideoSurfaceView(context: Context, private val renderer: VideoRenderer) :
    SurfaceView(context), SurfaceHolder.Callback {

    init {
        holder.addCallback(this)
        isFocusable = false
    }

    override fun surfaceCreated(holder: SurfaceHolder) = renderer.setSurface(holder.surface)

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit

    /** The decoder lets go of the surface before it is destroyed. */
    override fun surfaceDestroyed(holder: SurfaceHolder) = renderer.setSurface(null)
}
