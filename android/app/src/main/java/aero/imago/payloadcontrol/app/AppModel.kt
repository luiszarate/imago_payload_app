package aero.imago.payloadcontrol.app

import aero.imago.payloadcontrol.control.GamepadAction
import aero.imago.payloadcontrol.control.GamepadController
import aero.imago.payloadcontrol.control.GamepadLearnTarget
import aero.imago.payloadcontrol.control.GamepadMapping
import aero.imago.payloadcontrol.control.GimbalInputShaper
import aero.imago.payloadcontrol.core.CameraMode
import aero.imago.payloadcontrol.core.CameraSetting
import aero.imago.payloadcontrol.core.CameraSettingSection
import aero.imago.payloadcontrol.core.CameraStatus
import aero.imago.payloadcontrol.core.ConnectionProfile
import aero.imago.payloadcontrol.core.GimbalControl
import aero.imago.payloadcontrol.core.GimbalMode
import aero.imago.payloadcontrol.core.GimbalStatus
import aero.imago.payloadcontrol.core.Imago
import aero.imago.payloadcontrol.core.LinkStatus
import aero.imago.payloadcontrol.core.PayloadEvent
import aero.imago.payloadcontrol.core.PayloadInfo
import aero.imago.payloadcontrol.core.PayloadSession
import aero.imago.payloadcontrol.core.ProfileStore
import aero.imago.payloadcontrol.core.TargetPosition
import aero.imago.payloadcontrol.core.ThermalStatus
import aero.imago.payloadcontrol.core.TrackingState
import aero.imago.payloadcontrol.core.TrackingStatus
import aero.imago.payloadcontrol.core.VideoMetrics
import aero.imago.payloadcontrol.core.VideoRenderer
import aero.imago.payloadcontrol.core.ViewSource
import aero.imago.payloadcontrol.core.WebIsotherm
import aero.imago.payloadcontrol.core.hit
import android.app.Application
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.edit
import androidx.core.net.toUri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * App state and actions, on the main thread. Compose reads the state; each part is written only
 * when it changes, so only the composables that read it recompose. Gimbal rates from the
 * joystick or a gamepad go straight to the session, which sends them on the calling thread.
 */
class AppModel(application: Application) : AndroidViewModel(application) {
    data class Toast(val id: Long, val text: String, val style: Style) {
        enum class Style { INFO, ERROR }
    }

    enum class Haptic { TAP, CONFIRM, ERROR }

    private val prefs = application.getSharedPreferences("imago", Context.MODE_PRIVATE)
    private val store = ProfileStore(prefs)
    private val network = NetworkBinder(application)

    // MARK: Connection

    var profiles by mutableStateOf(store.load()); private set
    var selectedProfileId by mutableStateOf<String?>(null); private set
    var isConnected by mutableStateOf(false); private set
    var videoActive by mutableStateOf(false); private set
    var videoMessage by mutableStateOf<String?>(null); private set
    var connectError by mutableStateOf<String?>(null); private set

    // MARK: Payload state (each part updated only when it changes)

    var link by mutableStateOf(LinkStatus()); private set
    var gimbal by mutableStateOf(GimbalStatus()); private set
    var camera by mutableStateOf(CameraStatus()); private set
    var tracking by mutableStateOf(TrackingStatus()); private set
    var metrics by mutableStateOf(VideoMetrics()); private set
    var info by mutableStateOf(PayloadInfo()); private set
    /** IR camera state; written by updateThermal. */
    var thermal by mutableStateOf(ThermalStatus()); private set
    /** Picture size in pixels; (0, 0) until the decoder reports it. */
    var videoWidth by mutableIntStateOf(0); private set
    var videoHeight by mutableIntStateOf(0); private set
    /** Aircraft heading from North, when the gimbal or the autopilot reports it. */
    var vehicleHeading by mutableStateOf<Float?>(null); private set
    /** How far behind live the picture is (a queue upstream), in 50 ms steps. 0 = live. */
    var videoLagMs by mutableIntStateOf(0); private set
    /** The core is restarting the stream to catch up; the last picture stays on screen. */
    var videoResyncing by mutableStateOf(false); private set

    // MARK: UI state

    var showHUD by mutableStateOf(false)
    /** Gimbal attitude instrument at the bottom of the video. Remembered across launches. */
    var showAttitude by mutableStateOf(prefs.getBoolean(SHOW_ATTITUDE_KEY, true))
        private set
    /**
     * The picture fits right of the camera rail, so the rail never covers the payload's OSD.
     * Remembered across launches.
     */
    var videoClearOfCameraRail by mutableStateOf(prefs.getBoolean(VIDEO_CLEAR_OF_CAMERA_RAIL_KEY, true))
        private set
    var showConnections by mutableStateOf(false)
    var showSettings by mutableStateOf(false)
    /** When on, a tap on the video re-points the gimbal (Eagle Eye) instead of tracking. */
    var pointMode by mutableStateOf(false)
    var detectionEnabled by mutableStateOf(false); private set
    /** Photo or video, as chosen on the camera rail. The payload confirms it in camera.mode. */
    var captureMode by mutableStateOf(CameraMode.PHOTO); private set
    var gamepadName by mutableStateOf<String?>(null); private set
    var toast by mutableStateOf<Toast?>(null); private set
    /** Zoom direction currently commanded, so the slider lets go when a continuous zoom takes over. */
    var zoomDirection by mutableIntStateOf(0); private set

    // MARK: Settings

    /** Camera and gimbal settings as the payload last reported them (param_id -> value). */
    val cameraValues = mutableStateMapOf<String, Int>()
    /** How sticks, the on-screen joystick, D-pads and buttons turn into gimbal rates. */
    var gimbalTuning by mutableStateOf(GimbalInputShaper.load(prefs)); private set
    /** Which controller inputs drive what. */
    var gamepadMapping by mutableStateOf(GamepadMapping.load(prefs)); private set
    /** The action or axis waiting for a controller input, while the user assigns one. */
    var gamepadLearning by mutableStateOf<GamepadLearnTarget?>(null); private set

    // MARK: Diagnostics

    /** A .tlog of MAVLink is being written. */
    var isRecordingLog by mutableStateOf(false); private set
    /**
     * Logs keep everything (link debugging) instead of the payload's side once a second.
     * Remembered across launches; off by default, so everyday logs stay small.
     */
    var detailedLog by mutableStateOf(prefs.getBoolean(DETAILED_LOG_KEY, false)); private set
    /** The last log written, to share. */
    var lastLogFile by mutableStateOf<File?>(null); private set
    /** Commands the payload acknowledged to someone else (autopilot, GCS) this session. */
    var foreignCommandCount by mutableIntStateOf(0); private set
    private var lastForeignToastMs = 0L
    private var panLimitWarned = false
    /** The gimbal manager was heard this connection and the automatic take is settled. */
    private var gimbalControlSettled = false
    /** The thermal bar asked the camera for its IR settings since the view became IR. */
    private var thermalSettingsRead = false
    /** The isotherm as the payload's web settings page holds it; null until read. */
    var webIsotherm by mutableStateOf<WebIsotherm?>(null); private set
    /** A web settings request is on its way (Settings shows a spinner, buttons wait). */
    var webBusy by mutableStateOf(false); private set
    private val webRequests = mutableMapOf<Int, WebRequest>()

    // MARK: Target

    /** Where the camera points, while the payload reports it. */
    var target by mutableStateOf<TargetPosition?>(null); private set
    private val markedPointsFile = File(application.filesDir, "marked-points.json")
    /** Points the operator marked, kept in the app's files as marked-points.json. */
    var markedPoints by mutableStateOf(MarkedPoints.load(markedPointsFile)); private set

    // MARK: Plumbing

    val video = VideoRenderer()
    val gamepad = GamepadController(application)
    /** Set by the activity: buzzes the device. */
    var haptics: (Haptic) -> Unit = {}
    private var session: PayloadSession? = null
    private var pollJob: Job? = null
    private var toastJob: Job? = null
    private val pendingLabels = mutableMapOf<Int, String>()
    private var resumeOnStart = false
    /** Incremented on every disconnect; events from an older session are ignored. */
    private var generation = 0
    private var lastResyncMs = 0L
    private var toastCounter = 0L

    init {
        // Persist the built-in profiles once, so their IDs (and the selection) survive relaunch.
        store.save(profiles)
        selectedProfileId = store.selectedId?.takeIf { id -> profiles.any { it.id == id } } ?: profiles.firstOrNull()?.id

        video.onVideoSizeChange = { width, height ->
            videoWidth = width
            videoHeight = height
        }
        gamepad.onGimbalRate = { pitch, yaw -> session?.setGimbalRate(pitch, yaw) }
        gamepad.onZoom = { direction -> zoom(direction) }
        gamepad.onAction = ::handleGamepadAction
        gamepad.onLearned = ::gamepadLearned
        gamepad.onConnectionChange = { name ->
            if (name != null && name != gamepadName) show("$name connected", Toast.Style.INFO)
            gamepadName = name
        }
        gamepad.setMapping(gamepadMapping)
        gamepad.setShaper(gimbalTuning)
        gamepad.start()
    }

    override fun onCleared() {
        disconnect()
        gamepad.stop()
    }

    val selectedProfile: ConnectionProfile? get() = profiles.firstOrNull { it.id == selectedProfileId }

    /** The profile the running session was opened with (profiles can be edited meanwhile). */
    val connectedProfile: ConnectionProfile? get() = if (isConnected) session?.profile else null

    // MARK: Connect / disconnect

    fun connect() {
        val profile = selectedProfile
        if (profile == null) {
            showConnections = true
            return
        }
        profile.validationError?.let {
            connectError = it
            showConnections = true
            return
        }
        disconnect()
        connectError = null
        val rtspHost = profile.videoURL.trim().takeIf { it.isNotEmpty() }?.let { it.toUri().host }
        network.bindFor(listOfNotNull(
            profile.payloadIP.takeIf { profile.mavlinkMode == ConnectionProfile.MavlinkMode.DIRECT },
            rtspHost,
        ))
        val currentGeneration = generation
        val created = PayloadSession.create(profile, video) { event ->
            if (generation == currentGeneration) handle(event)
        }
        if (created == null) {
            network.unbind()
            connectError = "The payload address is not valid."
            return
        }
        if (!created.start()) {
            created.close()
            network.unbind()
            connectError = "Cannot open the MAVLink port. Another app may be using it."
            return
        }
        created.setRecordingDetail(compact = !detailedLog)
        session = created
        isConnected = true
        videoMessage = if (profile.videoURL.isBlank()) "Waiting for the payload's video address…" else "Connecting video…"
        startPolling()
    }

    fun disconnect() {
        val current = session ?: return
        generation += 1
        current.setGimbalRate(0f, 0f)
        if (zoomDirection != 0) {
            current.zoom(0)
            zoomDirection = 0
        }
        session = null
        current.close() // joins the core threads: no more frames after this
        stopPolling()
        video.reset()
        network.unbind()
        isConnected = false
        videoActive = false
        videoMessage = null
        link = LinkStatus()
        gimbal = GimbalStatus()
        panLimitWarned = false
        camera = CameraStatus()
        thermal = ThermalStatus()
        thermalSettingsRead = false
        tracking = TrackingStatus()
        target = null
        webIsotherm = null
        webBusy = false
        webRequests.clear()
        metrics = VideoMetrics()
        vehicleHeading = null
        videoLagMs = 0
        videoResyncing = false
        lastResyncMs = 0
        detectionEnabled = false
        cameraValues.clear()
        gimbalControlSettled = false
        isRecordingLog = false // the core closes the file with the session
        foreignCommandCount = 0
        pendingLabels.clear()
    }

    /** The activity lost focus (a dialog, the notification shade): never leave anything moving. */
    fun onPause() {
        session?.setGimbalRate(0f, 0f)
        gamepad.stopAll()
        if (zoomDirection != 0) zoom(0)
    }

    /** In the background: stop cleanly and come back on return. */
    fun onStop() {
        resumeOnStart = isConnected
        disconnect()
    }

    fun onStart() {
        if (resumeOnStart) {
            resumeOnStart = false
            connect()
        }
    }

    // MARK: State polling

    private fun startPolling() {
        stopPolling()
        pollJob = viewModelScope.launch {
            while (isActive) {
                poll()
                delay(POLL_INTERVAL_MS)
            }
        }
    }

    private fun stopPolling() {
        pollJob?.cancel()
        pollJob = null
    }

    private fun poll() {
        val state = session?.state() ?: return
        state.linkStatus.let { if (it != link) link = it }
        state.gimbalStatus.let { if (it != gimbal) gimbal = it }
        warnNearPanLimit()
        takeGimbalControlOnce()
        state.cameraStatus.let { if (it != camera) camera = it }
        updateThermal(state.thermalStatus)
        state.trackingStatus.let { if (it != tracking) tracking = it }
        state.targetPosition.let { if (it != target) target = it }
        state.payloadInfo.let { if (it != info) info = it }
        state.vehicleHeading.let { if (it != vehicleHeading) vehicleHeading = it }
        state.videoLagSteps.let { if (it != videoLagMs) videoLagMs = it }
        if (showHUD) {
            val counters = video.counters()
            val m = state.videoMetrics.copy(decodeFailures = counters.decodeFailures,
                                            framesSkipped = counters.skipped)
            if (m != metrics) metrics = m
        }
        gamepad.zoomLevel = maxOf(1f, camera.zoom)
    }

    /**
     * Once each time the pan gets within 30° of its ±320° stop: the yaw readouts stay amber
     * meanwhile. Recentre, or turn the other way, before it hits the stop.
     */
    private fun warnNearPanLimit() {
        if (gimbal.panNearLimit == panLimitWarned) return
        panLimitWarned = gimbal.panNearLimit
        val pan = gimbal.pan
        if (panLimitWarned && pan != null) {
            show(String.format(Locale.US, "Gimbal pan %+.0f°: near its ±%.0f° limit", pan, GimbalStatus.PAN_LIMIT),
                 Toast.Style.ERROR)
        }
    }

    // MARK: Events

    private fun handle(event: PayloadEvent) {
        when (event) {
            PayloadEvent.LinkUp -> {
                show("Payload connected", Toast.Style.INFO)
                if (session?.profile?.setStandardRCMode == true) {
                    session?.setCameraParam("RC_MODE", 1)
                }
                // Keep the payload's camera mode in line with the rail.
                register(session?.setCameraMode(captureMode), "Camera mode")
            }
            PayloadEvent.LinkDown -> if (isConnected) show("Payload link lost", Toast.Style.ERROR)
            is PayloadEvent.VideoStarted -> {
                videoActive = true
                videoResyncing = false
                videoMessage = null
                if (event.tcp && session?.profile?.videoTransport == ConnectionProfile.VideoTransport.AUTO) {
                    show("Video over TCP: UDP did not get through", Toast.Style.INFO)
                }
            }
            PayloadEvent.VideoStopped -> videoActive = false
            is PayloadEvent.VideoError -> {
                videoActive = false
                videoResyncing = false
                videoMessage = "Video: ${event.message}"
            }
            is PayloadEvent.VideoResync -> {
                videoResyncing = true
                val now = System.currentTimeMillis()
                if (lastResyncMs != 0L && now - lastResyncMs < 30_000) {
                    // Falling behind again right away: the link cannot carry this stream.
                    show("Video keeps falling behind. Lower the stream bitrate or resolution.", Toast.Style.ERROR)
                } else {
                    show(String.format(Locale.US, "Video was %.1f s behind live. Catching up…", event.lagMs / 1000.0),
                         Toast.Style.INFO)
                }
                lastResyncMs = now
            }
            is PayloadEvent.CommandResult -> finish(event.requestId, event.result, commandName(event.command))
            is PayloadEvent.ParamResult -> finish(event.requestId, event.result, paramName(event.paramId))
            is PayloadEvent.StreamUri -> Unit
            is PayloadEvent.CameraParam -> cameraValues[event.id] = event.value
            is PayloadEvent.ForeignCommand -> foreignCommandSeen(event.command, event.from)
            is PayloadEvent.ZoomChangedExternally ->
                show("Zoom moved without this app: ${event.change.replace("x", "×")}", Toast.Style.ERROR)
            is PayloadEvent.TrackingIgnored -> {
                // Field test 2026-10-15: accepted for minutes, never applied, until the view changed.
                val still = when (event.state) {
                    TrackingState.TRACKED -> "still tracking"
                    TrackingState.LOST -> "target lost"
                    TrackingState.IDLE -> "idle"
                }
                show(if (event.command == "stop") "Payload accepted Stop but is $still. Try changing the view, or restart the camera service in Settings."
                     else "Payload accepted the point but did not track it ($still). Try changing the view, or restart the camera service in Settings.",
                     Toast.Style.ERROR)
            }
            is PayloadEvent.WebResult -> webRequestEnded(event.requestId, event.status, event.result, event.error)
            is PayloadEvent.StatusText -> if (event.text.isNotEmpty()) show(event.text, Toast.Style.INFO)
        }
    }

    fun register(requestId: Int?, label: String) {
        if (requestId == null || requestId == 0) return
        pendingLabels[requestId] = label
    }

    private fun finish(requestId: Int, result: Int, fallback: String) {
        val label = pendingLabels.remove(requestId) ?: fallback
        if (result == 0) return
        haptics(Haptic.ERROR)
        if (result == -1) {
            show("$label: no response from the payload", Toast.Style.ERROR)
        } else {
            show("$label was rejected (code $result)", Toast.Style.ERROR)
        }
    }

    fun show(text: String, style: Toast.Style) {
        val shown = Toast(++toastCounter, text, style)
        toast = shown
        toastJob?.cancel()
        toastJob = viewModelScope.launch {
            delay(if (style == Toast.Style.ERROR) 4000 else 2500)
            if (toast == shown) toast = null
        }
    }

    // MARK: Gimbal

    /** Joystick deflection: x right, y up, both -1..1. */
    fun joystickMoved(x: Float, y: Float) {
        val rates = gimbalTuning.rates(x, y, maxOf(1f, camera.zoom))
        session?.setGimbalRate(rates.pitch, rates.yaw)
    }

    fun setGimbalMode(mode: GimbalMode) = register(session?.setGimbalMode(mode), "Gimbal mode")

    /** With the autopilot's gimbal manager: take control, or give it back to the pilot / GCS. */
    fun toggleGimbalControl() {
        haptics(Haptic.TAP)
        val take = gimbal.control != GimbalControl.ThisApp
        register(session?.takeGimbalControl(take), if (take) "Take gimbal control" else "Release gimbal control")
    }

    val usesGimbalManager: Boolean
        get() = connectedProfile?.gimbalProtocol == ConnectionProfile.GimbalProtocol.MANAGER

    /**
     * Once per connection, as soon as the gimbal manager answers: take control, unless a ground
     * station holds it (then the rail button decides). Never again after a link blip.
     */
    private fun takeGimbalControlOnce() {
        if (!usesGimbalManager || gimbalControlSettled || gimbal.control == GimbalControl.Unknown) return
        gimbalControlSettled = true
        when (val control = gimbal.control) {
            GimbalControl.ThisApp -> Unit
            is GimbalControl.Other -> if (control.system == 255 || control.component == 190) {
                show("${control.holder} holds the gimbal. Tap TAKE CONTROL to drive it from here.", Toast.Style.INFO)
            } else {
                register(session?.takeGimbalControl(true), "Take gimbal control")
            }
            else -> register(session?.takeGimbalControl(true), "Take gimbal control")
        }
    }

    fun recenterGimbal() {
        haptics(Haptic.TAP)
        val id = session?.setGimbalMode(GimbalMode.RESET)
        // Through the gimbal manager it is a streamed setpoint: nothing acknowledges it.
        if (!usesGimbalManager) register(id, "Recenter")
    }

    // MARK: Camera

    fun chooseCaptureMode(mode: CameraMode) {
        if (mode == captureMode) return
        captureMode = mode
        register(session?.setCameraMode(mode), "Camera mode")
    }

    /** Shutter: a photo in photo mode, start/stop recording in video mode. */
    fun shutter() {
        haptics(Haptic.TAP)
        if (captureMode == CameraMode.VIDEO) toggleRecording()
        else register(session?.takePhoto(), "Photo")
    }

    fun toggleRecording() {
        captureMode = CameraMode.VIDEO
        val start = !camera.recording
        register(session?.setRecording(start), if (start) "Start recording" else "Stop recording")
    }

    fun zoom(direction: Int) {
        zoomDirection = direction
        val id = session?.zoom(direction)
        if (direction != 0) register(id, "Zoom")
    }

    /** Exact level from the zoom slider, on every change while it is dragged. The core paces it. */
    fun zoomTo(level: Float) {
        session?.zoomTarget(level)
    }

    fun setView(source: ViewSource) = register(session?.setView(source), "View ${source.title}")

    fun cycleView(step: Int) {
        val all = ViewSource.entries
        val current = camera.view?.let { all.indexOf(it) } ?: 0
        setView(all[(current + step + all.size) % all.size])
    }

    // MARK: Thermal bar (IR palette, zoom, gain and isotherm)

    /** The bar shows while the IR picture is the main one. */
    val showsThermalBar: Boolean
        get() = isConnected && (camera.view == ViewSource.IR || camera.view == ViewSource.IR_EO)

    /** Palette on the camera: what it last confirmed, else its data stream. */
    val irPalette: Int? get() = cameraValues["C_T_PALETTE"] ?: thermal.palette

    /** IR digital zoom, 1..8×. */
    val irZoom: Int? get() = cameraValues["C_T_ZOOM"]?.let { it + 1 } ?: thermal.zoom

    /** The camera answered a read of its isotherm switch (Gremsy's F1 thermal core has it). */
    val isothermSupported: Boolean get() = "C_T_ISO_M" in cameraValues
    val isothermOn: Boolean get() = cameraValues["C_T_ISO_M"] == 1

    /** The band on the camera when it is on and known, else the last one applied here. */
    val isothermBand: IntRange
        get() {
            val low = cameraValues["C_T_RG0_TMP"]
            val high = cameraValues["C_T_RG1_TMP"]
            if (isothermOn && low != null && high != null && low < high && high <= IsothermBand.LIMITS.last) {
                return low..high
            }
            val savedLow = prefs.getInt(ISOTHERM_LOW_KEY, 35)
            val savedHigh = prefs.getInt(ISOTHERM_HIGH_KEY, 40)
            return if (savedLow < savedHigh) savedLow..savedHigh else 35..40
        }

    /** Low gain measures up to 550 °C, high gain up to 150 °C. null when the camera has no gain setting. */
    val irLowGain: Boolean? get() = cameraValues["C_T_G"]?.let { it == Imago.IR_GAIN_LOW }

    /**
     * Called on every poll: once the view turns IR, asks for the settings the bar shows (the
     * isotherm and gain ones go unanswered on a camera without them), and follows changes the
     * data stream reports, so a palette changed elsewhere shows here too.
     */
    private fun updateThermal(status: ThermalStatus) {
        val previous = thermal
        if (previous != status) {
            thermal = status
            if (status.palette != null && status.palette != previous.palette) {
                cameraValues["C_T_PALETTE"] = status.palette
            }
            if (status.zoom != null && status.zoom != previous.zoom && status.zoom >= 1) {
                cameraValues["C_T_ZOOM"] = status.zoom - 1
            }
        }
        val current = session
        if (showsThermalBar && !thermalSettingsRead && current != null) {
            thermalSettingsRead = true
            for (id in listOf("C_T_PALETTE", "C_T_ZOOM", "C_T_ISO_M", "C_T_G", "C_T_RG0_TMP", "C_T_RG1_TMP")) {
                current.readCameraParam(id)
            }
        } else if (!showsThermalBar) {
            thermalSettingsRead = false
        }
    }

    fun setIRPalette(index: Int) {
        val current = session ?: return
        haptics(Haptic.TAP)
        register(current.setCameraParam("C_T_PALETTE", index), "Palette")
    }

    /** One step of IR digital zoom (1..8×). */
    fun stepIRZoom(step: Int) = setIRZoom((irZoom ?: 1) + step)

    fun setIRZoom(zoom: Int) {
        val current = session ?: return
        val next = zoom.coerceIn(1, 8)
        if (next == irZoom) return
        haptics(Haptic.TAP)
        register(current.setCameraParam("C_T_ZOOM", next - 1), "IR zoom")
    }

    fun setIRLowGain(low: Boolean) {
        val current = session ?: return
        haptics(Haptic.TAP)
        register(current.setCameraParam("C_T_G", if (low) Imago.IR_GAIN_LOW else Imago.IR_GAIN_HIGH), "IR gain")
    }

    /** Colours the band over the palette; above 150 °C the core also sets low gain. */
    fun applyIsotherm(band: IntRange) {
        val current = session ?: return
        haptics(Haptic.TAP)
        prefs.edit {
            putInt(ISOTHERM_LOW_KEY, band.first)
            putInt(ISOTHERM_HIGH_KEY, band.last)
        }
        register(current.setIsotherm(band), "Isotherm")
    }

    fun disableIsotherm() {
        val current = session ?: return
        haptics(Haptic.TAP)
        register(current.setIsotherm(null), "Isotherm off")
    }

    // MARK: Web settings (the isotherm on the payload's web page, restarting its camera service)

    private enum class WebRequest { READ, SAVE, RESTART_SERVICE }

    fun readWebIsotherm() {
        val current = session ?: return
        if (webBusy) return
        startWebRequest(current.readWebIRSettings(), WebRequest.READ)
    }

    fun saveWebIsotherm(isotherm: WebIsotherm) {
        val current = session ?: return
        if (webBusy) return
        haptics(Haptic.TAP)
        startWebRequest(current.setWebIsotherm(isotherm.enabled, isotherm.thresholdC, isotherm.measurementRange),
                        WebRequest.SAVE)
    }

    /**
     * The tracker that stops obeying in long sessions (2026-10-15, again 2026-10-19) may come
     * back with a restart of the payload's camera service *(unverified)*.
     */
    fun restartPayloadService() {
        val current = session ?: return
        if (webBusy) return
        haptics(Haptic.TAP)
        startWebRequest(current.restartPayloadService(), WebRequest.RESTART_SERVICE)
    }

    private fun startWebRequest(id: Int, kind: WebRequest) {
        if (id == 0) {
            show("The payload's web page needs the payload IP in the connection profile", Toast.Style.ERROR)
            return
        }
        webRequests[id] = kind
        webBusy = true
    }

    private fun webRequestEnded(id: Int, status: Int, result: Int, error: String) {
        val kind = webRequests.remove(id) ?: return
        webBusy = webRequests.isNotEmpty()
        if (result == Imago.RESULT_ACCEPTED && kind != WebRequest.RESTART_SERVICE) {
            session?.webIsotherm()?.let { webIsotherm = it }
        }
        when {
            kind == WebRequest.SAVE && result == Imago.RESULT_ACCEPTED -> show("Isotherm saved", Toast.Style.INFO)
            // The service may go down before the page answers.
            kind == WebRequest.RESTART_SERVICE && (result == Imago.RESULT_ACCEPTED || result == Imago.RESULT_TIMEOUT) ->
                show("Camera service restarting. Video and control come back by themselves.", Toast.Style.INFO)
            kind == WebRequest.READ && result == Imago.RESULT_ACCEPTED -> Unit
            else -> {
                haptics(Haptic.ERROR)
                val what = when (kind) {
                    WebRequest.READ -> "Reading"
                    WebRequest.SAVE -> "Saving"
                    WebRequest.RESTART_SERVICE -> "Restarting"
                }
                show("$what the payload's web settings failed: ${error.ifEmpty { "code $status" }}", Toast.Style.ERROR)
            }
        }
    }

    // MARK: Target (copy, mark, export the marked points)

    fun copyTarget() {
        val current = target ?: return
        val clipboard = getApplication<Application>().getSystemService(ClipboardManager::class.java)
        clipboard?.setPrimaryClip(ClipData.newPlainText("Target", current.coordinateText))
        haptics(Haptic.TAP)
        show("Coordinates copied", Toast.Style.INFO)
    }

    fun markTarget() {
        val current = target ?: return
        val name = MarkedPoints.nextName(markedPoints)
        updateMarkedPoints(markedPoints + MarkedPoint(name = name, time = MarkedPoints.timestamp(), position = current))
        haptics(Haptic.TAP)
        show("$name marked · Settings → Camera → Marked points", Toast.Style.INFO)
    }

    fun deleteMarkedPoint(id: String) = updateMarkedPoints(markedPoints.filter { it.id != id })

    private fun updateMarkedPoints(points: List<MarkedPoint>) {
        markedPoints = points
        MarkedPoints.save(markedPointsFile, points)
    }

    /** Writes the marked points as CSV or KML for sharing (FileProvider "exports"); null if it fails. */
    fun exportMarkedPoints(kml: Boolean): File? {
        val directory = File(getApplication<Application>().cacheDir, "Exports")
        if (!directory.isDirectory && !directory.mkdirs()) return null
        val file = File(directory, if (kml) "marked-points.kml" else "marked-points.csv")
        return runCatching {
            file.writeText(if (kml) MarkedPoints.kml(markedPoints) else MarkedPoints.csv(markedPoints))
            file
        }.getOrNull()
    }

    // MARK: Tracking

    /** Tap on the video at a normalized point. */
    fun videoTapped(x: Float, y: Float) {
        haptics(Haptic.TAP)
        if (pointMode) {
            register(session?.pointGimbal(x, y), "Point gimbal")
            return
        }
        val detection = tracking.detections.hit(x, y)
        if (detection != null) {
            // The detection's own box: the tracker starts on the whole object the AI found.
            register(session?.track(detection.rect), "Track ${detection.label}")
        } else {
            register(session?.track(x, y), "Track")
        }
    }

    fun stopTracking() = register(session?.stopTracking(), "Stop tracking")

    fun toggleDetection() {
        detectionEnabled = !detectionEnabled
        register(session?.setDetection(detectionEnabled), "AI detection")
    }

    // MARK: Gamepad

    private fun handleGamepadAction(action: GamepadAction) {
        when (action) {
            GamepadAction.SHUTTER -> shutter()
            GamepadAction.RECORD -> toggleRecording()
            GamepadAction.STOP_TRACKING -> stopTracking()
            GamepadAction.TOGGLE_DETECTION -> toggleDetection()
            GamepadAction.PREVIOUS_VIEW -> cycleView(-1)
            GamepadAction.NEXT_VIEW -> cycleView(1)
            GamepadAction.RECENTER -> recenterGimbal()
            GamepadAction.TOGGLE_POINT_MODE -> pointMode = !pointMode
            GamepadAction.TOGGLE_LOCK_FOLLOW ->
                setGimbalMode(if (gimbal.mode == GimbalMode.LOCK) GimbalMode.FOLLOW else GimbalMode.LOCK)
            GamepadAction.ZOOM_IN, GamepadAction.ZOOM_OUT -> Unit // held: sent through onZoom
        }
    }

    // MARK: Profiles

    fun selectProfile(id: String) {
        selectedProfileId = id
        store.selectedId = id
    }

    fun saveProfile(profile: ConnectionProfile) {
        val index = profiles.indexOfFirst { it.id == profile.id }
        profiles = if (index >= 0) profiles.toMutableList().also { it[index] = profile } else profiles + profile
        store.save(profiles)
        if (selectedProfileId == null) selectProfile(profile.id)
    }

    fun deleteProfile(id: String) {
        profiles = profiles.filter { it.id != id }
        store.save(profiles)
        if (selectedProfileId == id) {
            val first = profiles.firstOrNull()
            if (first != null) selectProfile(first.id) else {
                selectedProfileId = null
                store.selectedId = null
            }
        }
    }

    // MARK: Settings

    fun toggleAttitude() {
        showAttitude = !showAttitude
        prefs.edit { putBoolean(SHOW_ATTITUDE_KEY, showAttitude) }
    }

    fun updateVideoClearOfCameraRail(on: Boolean) {
        videoClearOfCameraRail = on
        prefs.edit { putBoolean(VIDEO_CLEAR_OF_CAMERA_RAIL_KEY, on) }
    }

    fun updateDetailedLog(on: Boolean) {
        detailedLog = on
        prefs.edit { putBoolean(DETAILED_LOG_KEY, on) }
        session?.setRecordingDetail(compact = !on)
    }

    /** Asks the payload for each setting's value; they arrive in cameraValues. */
    fun readSettings(sections: List<CameraSettingSection>, onlyMissing: Boolean = false) {
        val current = session ?: return
        for (setting in sections.flatMap { it.settings }) {
            if (!onlyMissing || setting.id !in cameraValues) current.readCameraParam(setting.id)
        }
    }

    /** The payload confirms the new value, which then shows up in cameraValues. */
    fun set(setting: CameraSetting, value: Int) {
        val current = session ?: return
        haptics(Haptic.TAP)
        register(current.setCameraParam(setting.id, value), setting.title)
    }

    /** Lower bitrates survive a weak link with fewer lost packets (frozen picture). */
    fun setStreamBitrate(bitsPerSecond: Int) {
        val current = session ?: return
        haptics(Haptic.TAP)
        register(current.setStreamBitrate(0, bitsPerSecond), "Stream bitrate")
    }

    fun updateGimbalTuning(tuning: GimbalInputShaper) {
        gimbalTuning = tuning
        tuning.save(prefs)
        gamepad.setShaper(tuning)
    }

    fun updateGamepadMapping(mapping: GamepadMapping) {
        gamepadMapping = mapping
        mapping.save(prefs)
        gamepad.setMapping(mapping)
    }

    /** Waits for the next button press or axis movement on the controller; null cancels. */
    fun learnGamepadInput(target: GamepadLearnTarget?) {
        gamepadLearning = target
        gamepad.setLearning(target)
    }

    private fun gamepadLearned(target: GamepadLearnTarget, learned: GamepadController.Learned) {
        // Cancelled, or another assignment started, while this input was on its way.
        if (gamepadLearning != target) return
        when {
            target is GamepadLearnTarget.Action && learned is GamepadController.Learned.Button ->
                updateGamepadMapping(gamepadMapping.bind(target.action, learned.keyCode))
            target is GamepadLearnTarget.Axis && learned is GamepadController.Learned.Axis ->
                updateGamepadMapping(gamepadMapping.bind(target.function, learned.axis, learned.inverted))
        }
        gamepadLearning = null
        haptics(Haptic.TAP)
    }

    // MARK: Diagnostics

    /** Where logs go: the app's folder on shared storage (Android/data/aero.imago.payloadcontrol/files/Logs). */
    private val logsDirectory: File
        get() = File(getApplication<Application>().getExternalFilesDir(null) ?: getApplication<Application>().filesDir, "Logs")

    /** Starts or stops recording all MAVLink to a .tlog (QGroundControl, MAVExplorer, pymavlink). */
    fun toggleLogRecording() {
        val current = session ?: return
        if (isRecordingLog) {
            val complete = current.stopRecording()
            isRecordingLog = false
            show(if (complete) "MAVLink log saved" else "MAVLink log saved, but incomplete (storage full?)",
                 if (complete) Toast.Style.INFO else Toast.Style.ERROR)
            return
        }
        val directory = logsDirectory
        if (!directory.isDirectory && !directory.mkdirs()) {
            show("Cannot create the log folder", Toast.Style.ERROR)
            return
        }
        // java.time needs Android 8; SimpleDateFormat works from Android 7.
        val stamp = SimpleDateFormat("yyyy-MM-dd'T'HHmmss", Locale.US).format(Date())
        var file = File(directory, "imago-$stamp.tlog")
        var n = 2
        while (file.exists()) {
            file = File(directory, "imago-$stamp-$n.tlog")
            n += 1
        }
        if (!current.startRecording(file.absolutePath)) {
            show("Cannot write the MAVLink log", Toast.Style.ERROR)
            return
        }
        isRecordingLog = true
        lastLogFile = file
        haptics(Haptic.TAP)
    }

    /** The payload acknowledged a command that came from elsewhere (autopilot, GCS). */
    private fun foreignCommandSeen(command: Int, from: String) {
        foreignCommandCount += 1
        if (command !in CAMERA_COMMANDS) return
        // At most one warning every few seconds: a GCS can repeat a command.
        val now = System.currentTimeMillis()
        if (now - lastForeignToastMs < 3000) return
        lastForeignToastMs = now
        show("${commandName(command)} sent to the camera by ${senderName(from)}", Toast.Style.ERROR)
    }

    companion object {
        private const val POLL_INTERVAL_MS = 33L
        private const val SHOW_ATTITUDE_KEY = "ui.showAttitude"
        private const val VIDEO_CLEAR_OF_CAMERA_RAIL_KEY = "ui.videoClearOfCameraRail"
        private const val DETAILED_LOG_KEY = "log.detailed"
        private const val ISOTHERM_LOW_KEY = "thermal.isothermLow"
        private const val ISOTHERM_HIGH_KEY = "thermal.isothermHigh"
        /** The picture is flagged as stale from this lag on. */
        const val LAG_WARNING_MS = 250

        /** Commands that change what the camera does, worth a warning when someone else sends them. */
        private val CAMERA_COMMANDS = setOf(530, 531, 532, 2000, 2001, 2500, 2501, 31013)

        fun commandName(command: Int) = when (command) {
            530 -> "Camera mode"
            531 -> "Zoom"
            532 -> "Focus"
            2000 -> "Photo"
            2500 -> "Start recording"
            2501 -> "Stop recording"
            31013 -> "Tracking"
            else -> "Command $command"
        }

        private fun paramName(id: String) = when (id) {
            "GB_MODE" -> "Gimbal mode"
            "C_SOURCE" -> "View"
            "TRACK_MODE" -> "AI detection"
            "RC_MODE" -> "RC mode"
            "C_T_G" -> "IR gain"
            else -> if (id.startsWith("C_T_ISO") || id.startsWith("C_T_RG")) "Isotherm" else id
        }

        /** "system/component" as people know it. */
        private fun senderName(from: String): String {
            val parts = from.split("/").mapNotNull { it.toIntOrNull() }
            if (parts.size != 2) return from
            return when {
                parts[1] == 1 -> "the autopilot ($from)"
                parts[0] == 255 || parts[1] == 190 -> "a ground station ($from)"
                else -> "another controller ($from)"
            }
        }
    }
}

/** A temperature band for the isotherm, whole °C. */
data class IsothermBand(val name: String, val range: IntRange) {
    companion object {
        /**
         * From Gremsy's own examples (people) and common thermal practice (hot spots, fire). Fire
         * goes past 150 °C, so the camera drops to low gain for it.
         */
        val PRESETS = listOf(
            IsothermBand("People", 35..40),
            IsothermBand("Hot", 100..150),
            IsothermBand("Fire", 150..550),
        )

        /** The camera measures 0..550 °C in our Celsius settings. */
        val LIMITS = 0..Imago.IR_GAIN_LOW_MAX_C.toInt()
        val HIGH_GAIN_MAX = Imago.IR_GAIN_HIGH_MAX_C.toInt()
    }
}
