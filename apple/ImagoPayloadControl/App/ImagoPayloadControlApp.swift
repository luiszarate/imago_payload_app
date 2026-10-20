import SwiftUI

@main
struct ImagoPayloadControlApp: App {
    @State private var model = AppModel()
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            OperatorView()
                .environment(model)
                .preferredColorScheme(.dark)
                .onChange(of: scenePhase) { _, phase in
                    model.scenePhaseChanged(phase)
                }
                .onAppear { MacSupport.configureWindow() }
        }
        .commands {
            // The Mac's menu bar (and an iPad's hardware keyboard, holding ⌘).
            CommandGroup(replacing: .appSettings) {
                Button("Settings…") { model.showSettings = true }
                    .keyboardShortcut(",")
            }
            CommandMenu("Payload") {
                Button("Connections…") { model.showConnections = true }
                    .keyboardShortcut("k")
                Divider()
                Button("Recenter gimbal") { model.recenterGimbal() }
                    .keyboardShortcut("r")
                    .disabled(!model.isConnected)
                Button("Stop tracking") { model.stopTracking() }
                    .keyboardShortcut(".")
                    .disabled(!model.isConnected)
            }
        }
    }
}
