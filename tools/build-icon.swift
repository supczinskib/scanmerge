// SPDX-License-Identifier: GPL-3.0-only

import AppKit

let args = CommandLine.arguments
guard args.count == 3, let artwork = NSImage(contentsOfFile: args[1]) else {
    fputs("Usage: build-icon ARTWORK.png OUTPUT.iconset\n", stderr)
    exit(1)
}
let directory = URL(fileURLWithPath: args[2], isDirectory: true)
try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)

func render(pixels: Int, name: String) throws {
    let size = CGFloat(pixels)
    guard
        let bitmap = NSBitmapImageRep(
            bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0),
        let context = NSGraphicsContext(bitmapImageRep: bitmap)
    else {
        throw NSError(domain: "ScanMergeIcon", code: 1)
    }
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = context
    context.imageInterpolation = .high
    NSColor.clear.setFill()
    NSRect(x: 0, y: 0, width: size, height: size).fill(using: .copy)
    let tile = NSRect(x: size * 0.075, y: size * 0.075, width: size * 0.85, height: size * 0.85)
    let outline = NSBezierPath(roundedRect: tile, xRadius: size * 0.19, yRadius: size * 0.19)
    NSColor.white.setFill()
    outline.fill()
    outline.addClip()
    let contentScale: CGFloat = 0.782
    let inset = size * (1 - contentScale) / 2
    let content = NSRect(
        x: inset, y: inset, width: size * contentScale, height: size * contentScale)
    artwork.draw(in: content, from: .zero, operation: .sourceOver, fraction: 1)
    NSGraphicsContext.restoreGraphicsState()
    guard let data = bitmap.representation(using: .png, properties: [:]) else {
        throw NSError(domain: "ScanMergeIcon", code: 2)
    }
    try data.write(to: directory.appendingPathComponent(name))
}
for logical in [16, 32, 128, 256, 512] {
    try render(pixels: logical, name: "icon_\(logical)x\(logical).png")
    try render(pixels: logical * 2, name: "icon_\(logical)x\(logical)@2x.png")
}

func bigEndian(_ value: UInt32) -> Data {
    var value = value.bigEndian
    return withUnsafeBytes(of: &value) { Data($0) }
}
var chunks = Data()
for (type, filename) in [
    ("icp4", "icon_16x16.png"), ("icp5", "icon_32x32.png"),
    ("icp6", "icon_32x32@2x.png"), ("ic07", "icon_128x128.png"),
    ("ic08", "icon_256x256.png"), ("ic09", "icon_512x512.png"),
    ("ic10", "icon_512x512@2x.png"), ("ic11", "icon_16x16@2x.png"),
    ("ic12", "icon_32x32@2x.png"), ("ic13", "icon_128x128@2x.png"),
    ("ic14", "icon_256x256@2x.png"),
] {
    let png = try Data(contentsOf: directory.appendingPathComponent(filename))
    chunks.append(Data(type.utf8))
    chunks.append(bigEndian(UInt32(png.count + 8)))
    chunks.append(png)
}
var icon = Data("icns".utf8)
icon.append(bigEndian(UInt32(chunks.count + 8)))
icon.append(chunks)
try icon.write(to: directory.deletingPathExtension().appendingPathExtension("icns"))
