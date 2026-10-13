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
        }
    }
}
