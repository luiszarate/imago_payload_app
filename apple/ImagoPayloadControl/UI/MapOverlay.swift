import SwiftUI
import UIKit

/// A small north-up map, as on the Quest (on a tablet left of the attitude indicator, on a phone
/// in the picture's bottom right corner): the aircraft
/// turned to its heading, the camera's cone (as the attitude indicator draws it) and the point
/// the payload works out the camera looks at. Translucent until touched or pointed at, when its
/// own buttons show: put away, satellite / street, closer, further. Tiles come only from this
/// device's store (Settings › Map › Offline map); where there is none it draws a grid.
struct MapOverlay: View {
    @Environment(AppModel.self) private var model
    /// The map's side, points.
    let side: CGFloat
    /// Touched or pointed at: opaque, with its buttons.
    @State private var active = false
    @State private var hovering = false
    @State private var fadeTask: Task<Void, Never>?

    /// A 256 px tile drawn at 128 pt: one tile pixel per screen pixel on a 2x screen.
    static let tilePoints: CGFloat = 128
    private static let shownFor: Duration = .seconds(4)

    var body: some View {
        let map = model.offlineMap
        if map.settings.visible {
            content(map)
        } else {
            // Put away: a faint button in the corner brings it back.
            Button {
                map.settings.visible = true
            } label: {
                Image(systemName: "map")
                    .font(.system(size: 16, weight: .semibold))
                    .foregroundStyle(Theme.text)
                    .frame(width: 40, height: 40)
                    .panel(cornerRadius: 20)
            }
            .buttonStyle(PressScaleStyle())
            .opacity(0.7)
            .accessibilityLabel("Show the map")
        }
    }

    private func content(_ map: OfflineMap) -> some View {
        let settings = map.settings
        let position = model.aircraftPosition
        let zoom = settings.zoom
        let centre = position.map { (x: TileMath.x($0.longitude, zoom), y: TileMath.y($0.latitude, zoom)) }
        let cells = centre.map {
            TileMath.cover(centerX: $0.x, centerY: $0.y, halfSpan: side / 2 / Self.tilePoints, z: zoom)
        } ?? []
        _ = map.revision // redraw when a tile has been decoded
        let drawing = MapDrawing(
            centre: centre.map { CGPoint(x: $0.x, y: $0.y) },
            tiles: cells.map { MapDrawing.Tile(column: $0.column, row: $0.row,
                                               image: map.image(settings.style, $0.id)) },
            heading: model.vehicleHeading,
            bearing: model.gimbal.yawNorth,
            fieldOfView: model.camera.horizontalFOV,
            target: centre != nil ? model.target.map {
                CGPoint(x: TileMath.x($0.longitude, zoom), y: TileMath.y($0.latitude, zoom))
            } : nil,
            credit: settings.credit(settings.style))
        let request = TileRequest(style: settings.style, ids: cells.map(\.id), generation: map.generation)
        return ZStack(alignment: .topTrailing) {
            drawing
            if active || hovering {
                controls(map)
            }
        }
        .frame(width: side, height: side)
        .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 16, style: .continuous).stroke(Theme.stroke, lineWidth: 1))
        .opacity(active || hovering ? 1 : 0.8)
        .contentShape(Rectangle()) // a press on the map is the map's, never a track on the picture
        .onTapGesture { reveal() }
        .onHover { hovering = $0 }
        .animation(.easeOut(duration: 0.15), value: active || hovering)
        .task(id: request) {
            map.load(request.style, request.ids)
        }
        .onDisappear { fadeTask?.cancel() }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Map")
    }

    /// Two by two in the top right corner, so they fit the smallest map (a phone's).
    private func controls(_ map: OfflineMap) -> some View {
        let settings = map.settings
        return Grid(horizontalSpacing: 4, verticalSpacing: 4) {
            GridRow {
                mapButton(settings.style == .satellite ? "map" : "globe.americas.fill",
                          label: settings.style == .satellite ? "Street map" : "Satellite") {
                    map.settings.style = settings.style == .satellite ? .street : .satellite
                }
                mapButton("xmark", label: "Hide the map") {
                    map.settings.visible = false
                }
            }
            GridRow {
                mapButton("minus", label: "Further", enabled: settings.zoom > TileMath.minZoom) {
                    map.settings.zoom = max(settings.zoom - 1, TileMath.minZoom)
                }
                mapButton("plus", label: "Closer", enabled: settings.zoom < TileMath.maxZoom) {
                    map.settings.zoom = min(settings.zoom + 1, TileMath.maxZoom)
                }
            }
        }
        .padding(5)
        .transition(.opacity)
    }

    private func mapButton(_ systemImage: String, label: String, enabled: Bool = true,
                           action: @escaping () -> Void) -> some View {
        Button {
            action()
            reveal()
        } label: {
            Image(systemName: systemImage)
                .font(.system(size: 12, weight: .bold))
                .foregroundStyle(Theme.text.opacity(enabled ? 1 : 0.35))
                .frame(width: 28, height: 28)
                .background(Theme.surface.opacity(0.85), in: Circle())
                .overlay(Circle().stroke(Theme.stroke))
        }
        .buttonStyle(PressScaleStyle())
        .disabled(!enabled)
        .accessibilityLabel(label)
    }

    /// A touch shows the buttons for a few seconds; each press keeps them.
    private func reveal() {
        active = true
        fadeTask?.cancel()
        fadeTask = Task {
            try? await Task.sleep(for: Self.shownFor)
            guard !Task.isCancelled else { return }
            active = false
        }
    }

    private struct TileRequest: Equatable {
        let style: MapStyle
        let ids: [TileID]
        /// Asked again after the decoded tiles were dropped (OfflineMap.generation).
        let generation: Int
    }
}

/// The map itself. Positions are in tile coordinates at the map's zoom (fractions included):
/// a tile is `MapOverlay.tilePoints` wide.
private struct MapDrawing: View {
    struct Tile {
        let column: Int
        let row: Int
        let image: UIImage?
    }

    /// The aircraft, nil without a position.
    let centre: CGPoint?
    let tiles: [Tile]
    let heading: Float?
    /// The camera's bearing from North.
    let bearing: Float?
    let fieldOfView: Float?
    let target: CGPoint?
    let credit: String

    var body: some View {
        Canvas { context, size in
            context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(Theme.surfaceRaised))
            let mid = CGPoint(x: size.width / 2, y: size.height / 2)
            if let centre {
                drawTiles(in: &context, mid: mid, centre: centre, size: size)
                if let bearing {
                    drawCone(in: &context, mid: mid, bearing: Double(bearing), length: size.width * 0.42)
                }
                if let target {
                    drawTarget(in: &context, at: place(target, mid: mid, centre: centre))
                }
                drawAircraft(in: &context, mid: mid)
            } else {
                context.draw(label("No aircraft position", size: 12, color: Theme.textSecondary),
                             at: mid, anchor: .center)
            }
            drawLegend(in: &context, size: size)
        }
    }

    private func place(_ point: CGPoint, mid: CGPoint, centre: CGPoint) -> CGPoint {
        CGPoint(x: mid.x + (point.x - centre.x) * MapOverlay.tilePoints,
                y: mid.y + (point.y - centre.y) * MapOverlay.tilePoints)
    }

    private func drawTiles(in context: inout GraphicsContext, mid: CGPoint, centre: CGPoint, size: CGSize) {
        var grid = Path()
        var shown = 0
        for tile in tiles {
            let origin = place(CGPoint(x: tile.column, y: tile.row), mid: mid, centre: centre)
            // Half a point more, so no seam shows between tiles.
            let cell = CGRect(x: origin.x, y: origin.y,
                              width: MapOverlay.tilePoints + 0.5, height: MapOverlay.tilePoints + 0.5)
            if let image = tile.image {
                context.draw(Image(uiImage: image), in: cell)
                shown += 1
            } else {
                grid.addRect(cell) // a grid where there is no tile: the scale still reads
            }
        }
        context.stroke(grid, with: .color(Theme.stroke), lineWidth: 1)
        if shown == 0 {
            context.draw(label("No map stored here", size: 11, color: Theme.textSecondary),
                         at: CGPoint(x: mid.x, y: size.height * 0.27), anchor: .center)
            context.draw(label("Download it in Settings", size: 10, color: Theme.textTertiary),
                         at: CGPoint(x: mid.x, y: size.height * 0.27 + 14), anchor: .center)
        }
    }

    /// Where the camera looks: the attitude indicator's cone, turned to the camera's bearing.
    private func drawCone(in context: inout GraphicsContext, mid: CGPoint, bearing: Double, length: CGFloat) {
        let half = AttitudeDial.coneHalfAngle(fieldOfView)
        var wedge = Path()
        wedge.move(to: mid)
        wedge.addLine(to: AttitudeDial.point(mid, length, bearing - half))
        wedge.addArc(center: mid, radius: length,
                     startAngle: .degrees(bearing - half - 90), endAngle: .degrees(bearing + half - 90),
                     clockwise: false)
        wedge.closeSubpath()
        context.fill(wedge, with: .radialGradient(
            Gradient(colors: [Theme.magenta.opacity(0.6), Theme.fuchsia.opacity(0.1)]),
            center: mid, startRadius: 0, endRadius: length))
        context.stroke(wedge, with: .color(Theme.magenta), lineWidth: 1.2)
    }

    private func drawTarget(in context: inout GraphicsContext, at point: CGPoint) {
        var mark = Path(ellipseIn: CGRect(x: point.x - 6, y: point.y - 6, width: 12, height: 12))
        for (dx, dy) in [(-1.0, 0.0), (1, 0), (0, -1), (0, 1)] {
            mark.move(to: CGPoint(x: point.x + dx * 8, y: point.y + dy * 8))
            mark.addLine(to: CGPoint(x: point.x + dx * 13, y: point.y + dy * 13))
        }
        context.stroke(mark, with: .color(Theme.background.opacity(0.7)), lineWidth: 4)
        context.stroke(mark, with: .color(Theme.text), lineWidth: 2)
    }

    /// The aircraft turned to its heading; a dot when the heading is not known.
    private func drawAircraft(in context: inout GraphicsContext, mid: CGPoint) {
        guard let heading else {
            let dot = Path(ellipseIn: CGRect(x: mid.x - 5, y: mid.y - 5, width: 10, height: 10))
            context.fill(dot, with: .color(Theme.text))
            context.stroke(dot, with: .color(Theme.background.opacity(0.7)), lineWidth: 1.5)
            return
        }
        let turn = CGAffineTransform(rotationAngle: CGFloat(heading) * .pi / 180)
            .concatenating(CGAffineTransform(translationX: mid.x, y: mid.y))
        let shape = AttitudeDial.aircraftShape(center: .zero, size: 15).applying(turn)
        context.fill(shape, with: .color(Theme.text))
        context.stroke(shape, with: .color(Theme.background.opacity(0.8)), lineWidth: 1.5)
    }

    /// North up, the heading as a figure, and who the map is by.
    private func drawLegend(in context: inout GraphicsContext, size: CGSize) {
        context.drawLayer { layer in
            layer.addFilter(.shadow(color: .black.opacity(0.9), radius: 1.5))
            layer.draw(label("N ↑", size: 11, color: Theme.magenta), at: CGPoint(x: 9, y: 9), anchor: .topLeading)
            if let heading {
                let degrees = Int(heading.rounded()) % 360
                layer.draw(label(String(format: "HDG %03d°", degrees), size: 11, color: Theme.text),
                           at: CGPoint(x: 34, y: 9), anchor: .topLeading)
            }
            // Wrapped to the map's width, from its bottom edge up.
            let resolved = layer.resolve(label(credit, size: 8, color: Theme.textSecondary, weight: 500))
            let room = resolved.measure(in: CGSize(width: size.width - 14, height: 30))
            layer.draw(resolved, in: CGRect(x: 7, y: size.height - 5 - room.height,
                                            width: room.width, height: room.height))
        }
    }

    private func label(_ text: String, size: CGFloat, color: Color, weight: CGFloat = 700) -> Text {
        Text(text)
            .font(AppFont.figures(size, weight: weight))
            .foregroundStyle(color)
    }
}
