// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "VulkaxPhysicsStudioMac",
    platforms: [.macOS(.v14)],
    products: [.executable(name: "VulkaxPhysicsStudioMac", targets: ["VulkaxPhysicsStudioMac"])],
    targets: [.executableTarget(name: "VulkaxPhysicsStudioMac")],
    swiftLanguageModes: [.v5]
)
