import SwiftUI

/// Saved connection profiles: pick one, edit, add or delete.
struct ConnectionsView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var editing: ConnectionProfile?

    var body: some View {
        NavigationStack {
            List {
                Section {
                    ForEach(model.profiles) { profile in
                        HStack(spacing: 12) {
                            Image(systemName: profile.id == model.selectedProfileID ? "checkmark.circle.fill" : "circle")
                                .foregroundStyle(profile.id == model.selectedProfileID ? Theme.magenta : Theme.textTertiary)
                                .font(.system(size: 20))
                            VStack(alignment: .leading, spacing: 3) {
                                Text(profile.name)
                                    .font(AppFont.display(16, weight: 600))
                                Text(summary(profile))
                                    .font(AppFont.figures(12))
                                    .foregroundStyle(Theme.textSecondary)
                                    .lineLimit(1)
                            }
                            Spacer()
                            Button {
                                editing = profile
                            } label: {
                                Image(systemName: "pencil")
                                    .foregroundStyle(Theme.text)
                            }
                            .buttonStyle(.borderless)
                            .accessibilityLabel("Edit \(profile.name)")
                        }
                        .contentShape(Rectangle())
                        .onTapGesture {
                            model.selectProfile(profile.id)
                        }
                    }
                    .onDelete { offsets in
                        model.deleteProfiles(at: offsets)
                    }
                } footer: {
                    Text("Changes apply the next time you connect.")
                }
            }
            .navigationTitle("Connections")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button {
                        editing = ConnectionProfile(name: "New payload", payloadIP: "", videoURL: "")
                    } label: {
                        Label("Add", systemImage: "plus")
                    }
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { dismiss() }
                        .fontWeight(.semibold)
                }
            }
            .sheet(item: $editing) { profile in
                ProfileEditor(profile: profile) { saved in
                    model.saveProfile(saved)
                }
            }
        }
        .tint(Theme.magenta)
    }

    private func summary(_ profile: ConnectionProfile) -> String {
        let control = profile.mavlinkMode == .direct
            ? "\(profile.payloadIP):\(profile.mavlinkPort)"
            : "listen \(profile.localPort == 0 ? 14550 : profile.localPort)"
        let video = profile.videoURL.isEmpty ? "video from payload" : profile.videoURL
        return "\(control) · \(video)"
    }
}

/// Form for one profile. Mirrors Payload Plus: one field for the payload, one for the video.
struct ProfileEditor: View {
    @Environment(\.dismiss) private var dismiss
    @State private var draft: ConnectionProfile
    let onSave: (ConnectionProfile) -> Void

    init(profile: ConnectionProfile, onSave: @escaping (ConnectionProfile) -> Void) {
        _draft = State(initialValue: profile)
        self.onSave = onSave
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("Name") {
                    TextField("Name", text: $draft.name)
                }

                Section {
                    Picker("Control", selection: $draft.mavlinkMode) {
                        ForEach(ConnectionProfile.MavlinkMode.allCases) { mode in
                            Text(mode.title).tag(mode)
                        }
                    }
                    if draft.mavlinkMode == .direct {
                        TextField("Payload IP", text: $draft.payloadIP)
                            .keyboardType(.numbersAndPunctuation)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                        portField("MAVLink port", value: $draft.mavlinkPort)
                    } else {
                        portField("Listen port (0 = 14550)", value: $draft.localPort)
                    }
                } header: {
                    Text("Payload (MAVLink)")
                } footer: {
                    Text(draft.mavlinkMode == .direct
                         ? "The Orus L IP, as entered in Payload Plus. Through Herelink it is usually 192.168.144.x."
                         : "Use when Herelink forwards the payload's MAVLink to this device. Commands take about twice as long as with the payload IP.")
                }

                Section {
                    TextField("rtsp://192.168.43.1:8554/fpv_stream", text: $draft.videoURL)
                        .keyboardType(.URL)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    Picker("Transport", selection: $draft.videoTransport) {
                        ForEach(ConnectionProfile.VideoTransport.allCases) { transport in
                            Text(transport.title).tag(transport)
                        }
                    }
                    Picker("Smoothing", selection: $draft.videoSmoothingMs) {
                        ForEach(ConnectionProfile.smoothingChoices, id: \.self) { ms in
                            Text(ms == 0 ? "Off" : "\(ms) ms").tag(ms)
                        }
                    }
                    Picker("Resync when behind", selection: $draft.maxVideoLagMs) {
                        ForEach(ConnectionProfile.videoLagChoices, id: \.self) { ms in
                            Text(ms == 0 ? "Never" : (ms < 1000 ? "\(ms) ms" : "\(ms / 1000) s")).tag(ms)
                        }
                    }
                } header: {
                    Text("Video (RTSP)")
                } footer: {
                    Text("Leave empty to use the stream the payload announces. Auto tries UDP first and falls back to TCP. If the picture stays behind live for 2 s, for example after the link degrades, the stream restarts to catch up. Smoothing shows frames that arrive in bursts at their own pace, at the cost of up to that much delay; Off shows every frame at once. Frame gaps in the network HUD shows how bursty the link is.")
                }

                Section {
                    Picker("Gimbal commands", selection: $draft.gimbalProtocol) {
                        ForEach(ConnectionProfile.GimbalProtocol.allCases) { option in
                            Text(option.title).tag(option)
                        }
                    }
                    Toggle("Set RC mode to standard", isOn: $draft.setStandardRCMode)
                    Toggle("Keep Wi-Fi awake (experimental)", isOn: $draft.keepWifiAwake)
                } header: {
                    Text("Advanced")
                } footer: {
                    Text("Autopilot gimbal manager: the gimbal is driven through PX4 (MNT_MODE_IN and MNT_MODE_OUT set to MAVLink gimbal v2), which needs the autopilot on this link, e.g. Herelink forwarding. The app takes control once when it connects (not from a ground station that holds it) and gives it back when it disconnects. Keep Wi-Fi awake (experimental) sends a tiny packet 30 times a second so the tablet's Wi-Fi does not doze between video packets; turn it off if the video stalls.")
                }

                if let problem = draft.validationError {
                    Section {
                        Label(problem, systemImage: "exclamationmark.triangle.fill")
                            .foregroundStyle(Theme.warning)
                    }
                }
            }
            .navigationTitle(draft.name.isEmpty ? "Connection" : draft.name)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        var saved = draft
                        saved.name = saved.name.trimmingCharacters(in: .whitespaces)
                        saved.payloadIP = saved.payloadIP.trimmingCharacters(in: .whitespaces)
                        saved.videoURL = saved.videoURL.trimmingCharacters(in: .whitespaces)
                        onSave(saved)
                        dismiss()
                    }
                    .fontWeight(.semibold)
                    .disabled(draft.validationError != nil)
                }
            }
        }
        .tint(Theme.magenta)
    }

    private func portField(_ title: String, value: Binding<UInt16>) -> some View {
        let text = Binding<String>(
            get: { String(value.wrappedValue) },
            set: { newValue in
                if let port = UInt16(newValue.filter(\.isNumber)) {
                    value.wrappedValue = port
                } else if newValue.isEmpty {
                    value.wrappedValue = 0
                }
            })
        return LabeledContent(title) {
            TextField("Port", text: text)
                .keyboardType(.numberPad)
                .multilineTextAlignment(.trailing)
                .frame(maxWidth: 100)
        }
    }
}
