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
import aero.imago.payloadcontrol.core.LinkStatus
import aero.imago.payloadcontrol.core.PayloadEvent
import aero.imago.payloadcontrol.core.PayloadInfo
import aero.imago.payloadcontrol.core.PayloadSession
import aero.imago.payloadcontrol.core.ProfileStore
import aero.imago.payloadcontrol.core.TrackingStatus
import aero.imago.payloadcontrol.core.VideoMetrics
import aero.imago.payloadcontrol.core.VideoRenderer
import aero.imago.payloadcontrol.core.ViewSource
import aero.imago.payloadcontrol.core.hit
import android.app.Application
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

    /** A .tlog of all MAVLink is being written. */
    var isRecordingLog by mutableStateOf(false); private set
    /** The last log written, to share. */
    var lastLogFile by mutableStateOf<File?>(null); private set
    /** Commands the payload acknowledged to someone else (autopilot, GCS) this session. */
    var foreignCommandCount by mutableIntStateOf(0); private set
    private var lastForeignToastMs = 0L
    /** The gimbal manager was heard this connection and the automatic take is settled. */
    private var gimbalControlSettled = false

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
        camera = CameraStatus()
        tracking = TrackingStatus()
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
        takeGimbalControlOnce()
        state.cameraStatus.let { if (it != camera) camera = it }
        state.trackingStatus.let { if (it != tracking) tracking = it }
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
        register(session?.setGimbalMode(GimbalMode.RESET), "Recenter")
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
            else -> id
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
