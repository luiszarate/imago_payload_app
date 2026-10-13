// swift-tools-version:5.9
// Swift Package exposing the portable C++ core to the Apple apps (iOS, iPadOS, macOS).
// The same sources build with CMake for Linux, Android and Windows.
import PackageDescription

let package = Package(
    name: "ImagoCore",
    platforms: [.iOS(.v17), .macOS(.v14)],
    products: [
        .library(name: "ImagoCore", targets: ["ImagoCore"]),
    ],
    targets: [
        .target(
            name: "ImagoCore",
            path: "core",
            exclude: ["tests", "third_party", "CMakeLists.txt"],
            sources: ["src"],
            publicHeadersPath: "include",
            cxxSettings: [
                .headerSearchPath("src"),
                .headerSearchPath("third_party/mavlink"),
            ]
        ),
        .testTarget(
            name: "ImagoCoreTests",
            dependencies: ["ImagoCore"],
            path: "apple/Tests/ImagoCoreTests"
        ),
    ],
    cxxLanguageStandard: .cxx20
)
