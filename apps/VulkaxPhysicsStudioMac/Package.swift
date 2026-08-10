// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "VulkaxPhysicsStudioMac",
    platforms: [.macOS(.v14)],
    products: [.executable(name: "VulkaxPhysicsStudioMac", targets: ["VulkaxPhysicsStudioMac"])],
    targets: [
        .target(
            name: "VulkaxRuntimeContract",
            path: "Sources/VulkaxRuntimeContract",
            publicHeadersPath: "include"
        ),
        .target(
            name: "VulkaxEquationBridge",
            path: "Sources/VulkaxEquationBridge",
            publicHeadersPath: "include",
            cxxSettings: [.unsafeFlags(["-I../../src"])]
        ),
        .executableTarget(
            name: "VulkaxPhysicsStudioMac",
            dependencies: ["VulkaxRuntimeContract", "VulkaxEquationBridge"],
            swiftSettings: [.unsafeFlags(["-parse-as-library"])]
        )
    ],
    swiftLanguageModes: [.v5],
    cxxLanguageStandard: .cxx20
)
