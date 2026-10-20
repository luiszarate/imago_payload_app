import Foundation
import UIKit

/// A target position the operator kept, with when it was marked.
struct MarkedPoint: Identifiable, Codable, Equatable {
    var id = UUID()
    var name: String
    var time: Date
    var position: TargetPosition
}

/// The camera's target: copy it, mark it, export the marked points (CSV for spreadsheets, KML
/// for Google Earth and most mapping apps).
extension AppModel {
    private static var markedPointsURL: URL {
        URL.documentsDirectory.appending(path: "marked-points.json")
    }

    static func loadMarkedPoints() -> [MarkedPoint] {
        guard let data = try? Data(contentsOf: markedPointsURL) else { return [] }
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        return (try? decoder.decode([MarkedPoint].self, from: data)) ?? []
    }

    func saveMarkedPoints() {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        guard let data = try? encoder.encode(markedPoints) else { return }
        try? data.write(to: Self.markedPointsURL, options: .atomic)
    }

    func copyTarget() {
        guard let target else { return }
        UIPasteboard.general.string = target.coordinateText
        Haptics.tap()
        show("Coordinates copied", .info)
    }

    func markTarget() {
        guard let target else { return }
        let number = (markedPoints.map { Int($0.name.dropFirst(6)) ?? 0 }.max() ?? 0) + 1
        markedPoints.append(MarkedPoint(name: "Point \(number)", time: .now, position: target))
        Haptics.tap()
        show("Point \(number) marked · Settings → Camera → Marked points", .info)
    }

    /// Writes the marked points as CSV and KML for sharing; nil if they cannot be written.
    func exportMarkedPoints() -> (csv: URL, kml: URL)? {
        let folder = URL.temporaryDirectory
        let csv = folder.appending(path: "marked-points.csv")
        let kml = folder.appending(path: "marked-points.kml")
        let time = ISO8601DateFormatter()
        var rows = ["name,time_utc,latitude,longitude,altitude_m"]
        var placemarks: [String] = []
        for point in markedPoints {
            let p = point.position
            let name = point.name.replacingOccurrences(of: ",", with: " ")
            rows.append(String(format: "%@,%@,%.5f,%.5f,%.0f", name, time.string(from: point.time),
                               p.latitude, p.longitude, p.altitude))
            let escaped = point.name.replacingOccurrences(of: "&", with: "&amp;")
                .replacingOccurrences(of: "<", with: "&lt;")
            placemarks.append(String(format: """
                  <Placemark><name>%@</name><TimeStamp><when>%@</when></TimeStamp>\
                <Point><coordinates>%.5f,%.5f,%.0f</coordinates></Point></Placemark>
                """, escaped, time.string(from: point.time), p.longitude, p.latitude, p.altitude))
        }
        let document = """
            <?xml version="1.0" encoding="UTF-8"?>
            <kml xmlns="http://www.opengis.net/kml/2.2"><Document><name>Marked points</name>
            \(placemarks.joined(separator: "\n"))
            </Document></kml>
            """
        do {
            try (rows.joined(separator: "\n") + "\n").write(to: csv, atomically: true, encoding: .utf8)
            try document.write(to: kml, atomically: true, encoding: .utf8)
        } catch {
            return nil
        }
        return (csv, kml)
    }

    /// Mac: copies the exported files to Documents/Exports and shows them in Finder, where
    /// they can be dragged to a map or a spreadsheet.
    func showMarkedPointsInFinder(_ files: (csv: URL, kml: URL)) {
        let folder = URL.documentsDirectory.appending(path: "Exports", directoryHint: .isDirectory)
        do {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            for file in [files.csv, files.kml] {
                let destination = folder.appending(path: file.lastPathComponent)
                try? FileManager.default.removeItem(at: destination)
                try FileManager.default.copyItem(at: file, to: destination)
            }
        } catch {
            show("Cannot write the marked points: \(error.localizedDescription)", .error)
            return
        }
        MacSupport.showInFinder(folder) { [weak self] in
            self?.show("Cannot open the Exports folder in Finder", .error)
        }
    }
}
