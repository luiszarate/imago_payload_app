import SwiftUI
import UIKit

/// The Mac app is this same app through Mac Catalyst (decided 2026-10-19: every iOS change
/// reaches the Mac with no port). What only the Mac needs lives here.
enum MacSupport {
    /// Smallest window that still fits both rails, the top bar and the thermal bar.
    static let minimumWindowSize = CGSize(width: 1000, height: 640)

    /// Window size limits and a title bar without the title, once the window exists.
    @MainActor
    static func configureWindow() {
        #if targetEnvironment(macCatalyst)
        for case let scene as UIWindowScene in UIApplication.shared.connectedScenes {
            scene.sizeRestrictions?.minimumSize = minimumWindowSize
            scene.titlebar?.titleVisibility = .hidden
            scene.titlebar?.toolbar = nil
        }
        #endif
    }

    /// Opens a folder in Finder. The app is sandboxed, so its Documents folder lives in its
    /// container (~/Library/Containers/aero.imago.payloadcontrol/Data/Documents).
    @MainActor
    static func showInFinder(_ folder: URL, onFailure: @escaping @MainActor () -> Void) {
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        Task { @MainActor in
            let opened = await UIApplication.shared.open(folder)
            if !opened { onFailure() }
        }
    }
}

/// Keeps the Mac and its display awake while connected: the iOS idle timer does nothing there.
@MainActor
final class SystemAwake {
    private var activity: NSObjectProtocol?

    func hold(_ on: Bool) {
        #if targetEnvironment(macCatalyst)
        if on, activity == nil {
            activity = ProcessInfo.processInfo.beginActivity(
                options: [.userInitiated, .idleDisplaySleepDisabled],
                reason: "Operating the payload")
        } else if !on, let current = activity {
            ProcessInfo.processInfo.endActivity(current)
            activity = nil
        }
        #else
        UIApplication.shared.isIdleTimerDisabled = on
        #endif
    }
}

#if targetEnvironment(macCatalyst)
/// Mouse wheel or two-finger scroll over the video zooms while it turns and stops 250 ms after
/// the last step, like the Windows / Linux app. Laid over the picture, it takes only scroll
/// events: clicks fall through to the overlay below (tap to track).
struct ScrollZoomView: UIViewRepresentable {
    let zoom: (Int32) -> Void

    func makeUIView(context: Context) -> ScrollCatcher {
        let view = ScrollCatcher()
        view.zoom = zoom
        return view
    }

    func updateUIView(_ view: ScrollCatcher, context: Context) {
        view.zoom = zoom
    }

    final class ScrollCatcher: UIView {
        var zoom: (Int32) -> Void = { _ in }
        private var direction: Int32 = 0
        private var stopTimer: Timer?

        override init(frame: CGRect) {
            super.init(frame: frame)
            backgroundColor = .clear
            let scroll = UIPanGestureRecognizer(target: self, action: #selector(scrolled(_:)))
            scroll.allowedScrollTypesMask = .all
            scroll.maximumNumberOfTouches = 0 // scroll only: no touch or click drags
            addGestureRecognizer(scroll)
        }

        required init?(coder: NSCoder) {
            fatalError("init(coder:) is not used")
        }

        override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
            event?.type == .scroll ? super.hitTest(point, with: event) : nil
        }

        @objc private func scrolled(_ recognizer: UIPanGestureRecognizer) {
            switch recognizer.state {
            case .changed:
                let dy = recognizer.velocity(in: self).y
                let step: Int32 = dy > 0 ? 1 : (dy < 0 ? -1 : 0)
                guard step != 0 else { return }
                if step != direction {
                    direction = step
                    zoom(step)
                }
                scheduleStop()
            case .ended, .cancelled, .failed:
                scheduleStop()
            default:
                break
            }
        }

        private func scheduleStop() {
            stopTimer?.invalidate()
            stopTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: false) { [weak self] _ in
                MainActor.assumeIsolated {
                    guard let self, self.direction != 0 else { return }
                    self.direction = 0
                    self.zoom(0)
                }
            }
        }
    }
}
#endif
