// Renders Sultana's static app icon with the same drawing code as the
// live Dock tile (SultanaDock/IconArt.swift), around a frame the firmware
// drew.
//
//   swiftc -O -parse-as-library -o /tmp/make_app_icon \
//       sultana-dock/tools/make_app_icon.swift sultana-dock/SultanaDock/IconArt.swift
//   /tmp/make_app_icon FRAME.png sultana-dock/SultanaDock/Assets.xcassets/AppIcon.appiconset
//
// FRAME.png is a 240x240 screen, e.g. from `sultana-replay --frames`.

import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

@main
enum MakeAppIcon {
    static let sizes: [(points: Int, scale: Int)] = [
        (16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2), (256, 1), (256, 2), (512, 1), (512, 2),
    ]

    static func main() throws {
        let args = CommandLine.arguments
        guard args.count == 3 else {
            FileHandle.standardError.write(Data("usage: make_app_icon FRAME.png OUT_DIR\n".utf8))
            exit(2)
        }
        guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: args[1]) as CFURL, nil),
            let face = CGImageSourceCreateImageAtIndex(source, 0, nil)
        else {
            FileHandle.standardError.write(Data("can't read \(args[1])\n".utf8))
            exit(1)
        }
        let outDir = URL(fileURLWithPath: args[2], isDirectory: true)
        try FileManager.default.createDirectory(at: outDir, withIntermediateDirectories: true)

        var images: [[String: String]] = []
        for (points, scale) in sizes {
            let pixels = points * scale
            let name = "icon_\(points)x\(points)\(scale == 2 ? "@2x" : "").png"
            guard let context = CGContext(
                data: nil, width: pixels, height: pixels, bitsPerComponent: 8, bytesPerRow: 0,
                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
            else { exit(1) }
            IconArt.draw(face, in: context, size: CGFloat(pixels), pixelScale: 1)
            guard let image = context.makeImage(),
                let destination = CGImageDestinationCreateWithURL(
                    outDir.appendingPathComponent(name) as CFURL, UTType.png.identifier as CFString, 1, nil)
            else { exit(1) }
            CGImageDestinationAddImage(destination, image, nil)
            guard CGImageDestinationFinalize(destination) else { exit(1) }
            images.append(["filename": name, "idiom": "mac", "scale": "\(scale)x", "size": "\(points)x\(points)"])
            print("wrote \(name)")
        }
        let contents: [String: Any] = ["images": images, "info": ["author": "xcode", "version": 1]]
        let json = try JSONSerialization.data(withJSONObject: contents, options: [.prettyPrinted, .sortedKeys])
        try json.write(to: outDir.appendingPathComponent("Contents.json"))
    }
}
