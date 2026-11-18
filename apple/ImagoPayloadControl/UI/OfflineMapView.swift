import SwiftUI

/// Settings › Map › Offline map: the map's tiles are downloaded here, with internet, before
/// going to the aircraft (on the payload's Wi-Fi there is none). They belong to the device, not
/// to a profile.
struct OfflineMapView: View {
    @Environment(AppModel.self) private var model
    @State private var latitude = ""
    @State private var longitude = ""
    @State private var radius = ""
    @State private var problem: String?
    @State private var confirmDelete: MapStyle?

    var body: some View {
        let map = model.offlineMap
        // The switch lives in the map's settings, so it is kept for the next download.
        let detail = Binding(get: { map.settings.downloadDetail },
                             set: { map.settings.downloadDetail = $0 })
        Form {
            Section {
                coordinateField("Latitude, °", text: $latitude)
                coordinateField("Longitude, °", text: $longitude)
                coordinateField("Radius, km", text: $radius)
                if let position = model.aircraftPosition {
                    Button("Use the aircraft's position") { fill(position.latitude, position.longitude) }
                }
            } header: {
                Text("Flight area")
            } footer: {
                Text("The map shows what is stored on this device: on the payload's Wi-Fi there is no internet. With internet, download the flight area here first. After a flight, the aircraft's last position is filled in.")
            }

            Section {
                Toggle("Closest zoom too (17)", isOn: detail)
                    .disabled(map.downloading)
                if map.downloading {
                    Button("Stop", role: .destructive) { map.stopDownload() }
                } else {
                    Button("Download satellite") { download(.satellite) }
                    Button("Download street map") { download(.street) }
                }
                if let problem {
                    Label(problem, systemImage: "exclamationmark.triangle.fill")
                        .foregroundStyle(Theme.warning)
                }
                if !map.downloadStatus.isEmpty {
                    HStack(spacing: 8) {
                        if map.downloading {
                            ProgressView().controlSize(.small)
                        }
                        Text(map.downloadStatus)
                            .font(AppFont.body(13, weight: 600))
                    }
                }
                if !map.storedText.isEmpty {
                    Text(map.storedText)
                        .font(AppFont.body(13))
                        .foregroundStyle(Theme.textSecondary)
                }
            } header: {
                Text("Download")
            } footer: {
                Text("Zoom 11 to 16; with the closest zoom, to 17, about three times the tiles and the wait. The closest zooms are left out when the area is very large. A few tiles a second, as the tile servers ask.")
            }

            if !map.downloading {
                Section {
                    Button("Delete satellite", role: .destructive) { confirmDelete = .satellite }
                    Button("Delete street map", role: .destructive) { confirmDelete = .street }
                }
            }

            TileSourcesSection()
        }
        .navigationTitle("Offline map")
        .navigationBarTitleDisplayMode(.inline)
        .onAppear {
            // The aircraft where it is now, else where it was last seen, else the last area.
            let settings = map.settings
            if let position = model.aircraftPosition {
                fill(position.latitude, position.longitude)
            } else if let last = settings.lastPosition {
                fill(last.lat, last.lon)
            }
            radius = Self.trimmed(settings.downloadRadiusKm)
            map.refreshStored()
        }
        .confirmationDialog(confirmDelete.map { "Delete the stored \($0 == .satellite ? "satellite tiles" : "street map")?" } ?? "",
                            isPresented: Binding(get: { confirmDelete != nil },
                                                 set: { if !$0 { confirmDelete = nil } }),
                            titleVisibility: .visible) {
            Button("Delete", role: .destructive) {
                if let style = confirmDelete {
                    map.delete(style)
                }
                confirmDelete = nil
            }
        } message: {
            Text("The map shows a grid where they were until they are downloaded again.")
        }
    }

    private func coordinateField(_ title: String, text: Binding<String>) -> some View {
        LabeledContent(title) {
            TextField(title, text: text)
                .keyboardType(.numbersAndPunctuation)
                .multilineTextAlignment(.trailing)
                .font(AppFont.figures(15))
                .frame(maxWidth: 160)
        }
    }

    private func fill(_ lat: Double, _ lon: Double) {
        latitude = String(format: "%.5f", lat)
        longitude = String(format: "%.5f", lon)
    }

    private func download(_ style: MapStyle) {
        guard let lat = MapSettings.typedNumber(latitude), let lon = MapSettings.typedNumber(longitude),
              (-85...85).contains(lat), (-180...180).contains(lon) else {
            problem = "Type the latitude and longitude of the flight area, in degrees (19.4326, -99.1332)."
            return
        }
        let typed = radius.trimmingCharacters(in: .whitespaces)
        guard let km = typed.isEmpty ? 3 : MapSettings.typedNumber(typed), km > 0 else {
            problem = "Type the radius of the flight area in km, 0.5 to 15."
            return
        }
        problem = nil
        model.offlineMap.download(style, lat: lat, lon: lon, radiusKm: km)
        radius = Self.trimmed(model.offlineMap.settings.downloadRadiusKm)
    }

    private static func trimmed(_ value: Double) -> String {
        value == value.rounded() ? String(Int(value)) : String(value)
    }
}

/// Where tiles come from, and who the map credits. Point them at a provider you have the right
/// to cache from: the defaults are public services meant for online viewing. Edited as a draft
/// and saved with Save: applied on every keystroke, a half-typed URL would be fetched.
private struct TileSourcesSection: View {
    @Environment(AppModel.self) private var model
    @State private var draft = TileSources(MapSettings())

    var body: some View {
        let map = model.offlineMap
        let saved = TileSources(map.settings)
        let problem = draft.problem
        Section {
            sourceField("Satellite tiles", text: $draft.satelliteURL)
            sourceField("Satellite credit", text: $draft.satelliteCredit)
            sourceField("Street map tiles", text: $draft.streetURL)
            sourceField("Street map credit", text: $draft.streetCredit)
            if let problem {
                Label(problem, systemImage: "exclamationmark.triangle.fill")
                    .foregroundStyle(Theme.warning)
            }
            Button("Save") {
                draft.apply(to: &map.settings)
                draft = TileSources(map.settings)
            }
                .disabled(draft == saved || problem != nil)
            Button("Restore defaults") {
                draft = TileSources(MapSettings())
                draft.apply(to: &map.settings)
            }
            .disabled(saved == TileSources(MapSettings()))
        } header: {
            Text("Tile sources")
        } footer: {
            Text("URLs with {z}, {x} and {y}; the credit is drawn on the map. Mind the providers' terms: OpenStreetMap's tile policy discourages bulk downloads and Esri's imagery is licensed. A flight area of a few hundred tiles is modest use; for regular work, use your own provider's URL (with its key).")
        }
        .onAppear { draft = saved }
    }

    private func sourceField(_ title: String, text: Binding<String>) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title)
                .font(AppFont.body(12))
                .foregroundStyle(Theme.textSecondary)
            TextField(title, text: text, axis: .vertical)
                .font(AppFont.figures(13))
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .keyboardType(.URL)
        }
    }
}

/// The tile sources being edited.
private struct TileSources: Equatable {
    var satelliteURL: String
    var satelliteCredit: String
    var streetURL: String
    var streetCredit: String

    init(_ settings: MapSettings) {
        satelliteURL = settings.satelliteURL
        satelliteCredit = settings.satelliteCredit
        streetURL = settings.streetURL
        streetCredit = settings.streetCredit
    }

    /// The first thing wrong, naming the field.
    var problem: String? {
        if let problem = MapSettings.templateProblem(satelliteURL.trimmingCharacters(in: .whitespacesAndNewlines)) {
            return "Satellite tiles: \(problem)"
        }
        if let problem = MapSettings.templateProblem(streetURL.trimmingCharacters(in: .whitespacesAndNewlines)) {
            return "Street map tiles: \(problem)"
        }
        return nil
    }

    func apply(to settings: inout MapSettings) {
        var updated = settings
        updated.satelliteURL = satelliteURL.trimmingCharacters(in: .whitespacesAndNewlines)
        updated.satelliteCredit = satelliteCredit.trimmingCharacters(in: .whitespacesAndNewlines)
        updated.streetURL = streetURL.trimmingCharacters(in: .whitespacesAndNewlines)
        updated.streetCredit = streetCredit.trimmingCharacters(in: .whitespacesAndNewlines)
        settings = updated // one change: one save, one reload
    }
}
