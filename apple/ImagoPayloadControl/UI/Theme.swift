import CoreText
import SwiftUI
import UIKit

/// Imago Aerospace visual identity (www.imago.aero): near-black background, magenta-to-purple
/// accent, Space Grotesk for display text and figures, Manrope for body text.
enum Theme {
    static let background = Color(hex: 0x05060D)
    static let surface = Color(hex: 0x0A0B10)
    static let surfaceRaised = Color(hex: 0x14151C)
    static let text = Color(hex: 0xF5F6F8)
    static let textSecondary = Color(hex: 0xF5F6F8).opacity(0.62)
    static let textTertiary = Color(hex: 0xF5F6F8).opacity(0.38)
    static let stroke = Color.white.opacity(0.10)

    static let magenta = Color(hex: 0xEE49A7)
    static let fuchsia = Color(hex: 0xC2188F)
    static let purple = Color(hex: 0x7E2D91)
    static let accent = LinearGradient(colors: [magenta, fuchsia, purple],
                                       startPoint: .topLeading, endPoint: .bottomTrailing)

    // Status colours. Red is reserved for recording and errors.
    static let danger = Color(hex: 0xEF4444)
    static let ok = Color(hex: 0x34D399)
    static let warning = Color(hex: 0xFBBF24)

    /// Colours for detection classes: distinct hues that stay readable on video.
    static let classColors: [Color] = [
        Color(hex: 0x38BDF8), Color(hex: 0xFBBF24), Color(hex: 0x34D399), Color(hex: 0xF472B6),
        Color(hex: 0xA78BFA), Color(hex: 0xFB923C), Color(hex: 0x22D3EE), Color(hex: 0xA3E635),
    ]
}

extension Color {
    init(hex: UInt32, opacity: Double = 1) {
        self.init(.sRGB,
                  red: Double((hex >> 16) & 0xFF) / 255,
                  green: Double((hex >> 8) & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255,
                  opacity: opacity)
    }
}

/// The bundled fonts are variable fonts; the weight is set on the 'wght' axis.
enum AppFont {
    /// Space Grotesk: titles, labels, numbers.
    static func display(_ size: CGFloat, weight: CGFloat = 600) -> Font {
        Font(variable(family: "Space Grotesk", size: size, weight: weight) as CTFont)
    }

    /// Manrope: body text.
    static func body(_ size: CGFloat, weight: CGFloat = 500) -> Font {
        Font(variable(family: "Manrope", size: size, weight: weight) as CTFont)
    }

    /// Space Grotesk with tabular figures, for telemetry that changes while you read it.
    static func figures(_ size: CGFloat, weight: CGFloat = 500) -> Font {
        display(size, weight: weight).monospacedDigit()
    }

    private static let weightAxis = 0x77676874 // 'wght'
    private static var cache: [String: UIFont] = [:]
    private static let lock = NSLock()

    private static func variable(family: String, size: CGFloat, weight: CGFloat) -> UIFont {
        let key = "\(family)|\(size)|\(weight)"
        lock.lock()
        defer { lock.unlock() }
        if let font = cache[key] {
            return font
        }
        let descriptor = UIFontDescriptor(fontAttributes: [
            .family: family,
            UIFontDescriptor.AttributeName(rawValue: kCTFontVariationAttribute as String): [weightAxis: weight],
        ])
        let font = UIFont(descriptor: descriptor, size: size)
        cache[key] = font
        return font
    }
}

/// Translucent panel used for every control group over the video.
struct PanelBackground: ViewModifier {
    var cornerRadius: CGFloat = 18

    func body(content: Content) -> some View {
        content
            .background(Theme.surface.opacity(0.78), in: RoundedRectangle(cornerRadius: cornerRadius, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: cornerRadius, style: .continuous).stroke(Theme.stroke, lineWidth: 1))
    }
}

extension View {
    func panel(cornerRadius: CGFloat = 18) -> some View {
        modifier(PanelBackground(cornerRadius: cornerRadius))
    }
}
