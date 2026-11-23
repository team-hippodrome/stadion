// Makes the launcher's coloured Game Boy cartridge pictures (tools/launcher/assets/red.png, blue.png, yellow.png) from
// the plain grey one (empty.png): every grey pixel of the cartridge is recoloured by its brightness, the dark
// background around it stays as it is. Plain shapes only, no game art.
// Usage: swift tools/make_cartridges.swift
import AppKit

let assets = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent()
    .appendingPathComponent("launcher/assets")
let colours: [(String, (Double, Double, Double))] = [
    ("red", (0.86, 0.20, 0.18)), ("blue", (0.20, 0.38, 0.86)), ("yellow", (0.96, 0.78, 0.16)),
]

let src = NSImage(contentsOf: assets.appendingPathComponent("empty.png"))!
var rect = CGRect(origin: .zero, size: src.size)
let image = src.cgImage(forProposedRect: &rect, context: nil, hints: nil)!
let w = image.width, h = image.height

for (name, (cr, cg, cb)) in colours {
    var px = [UInt8](repeating: 0, count: w * h * 4)
    let ctx = CGContext(data: &px, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                        space: CGColorSpace(name: CGColorSpace.sRGB)!,
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))
    for i in stride(from: 0, to: px.count, by: 4) {
        let r = Double(px[i]), g = Double(px[i + 1]), b = Double(px[i + 2])
        guard max(r, g, b) - min(r, g, b) < 18 else { continue } // not grey: the background
        let l = (r + g + b) / 3 / 128 // 1 = the cartridge's mid grey
        guard l > 0.42 else { continue } // near black: the background
        px[i] = UInt8(min(255, cr * 255 * l))
        px[i + 1] = UInt8(min(255, cg * 255 * l))
        px[i + 2] = UInt8(min(255, cb * 255 * l))
    }
    let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
    try! rep.representation(using: .png, properties: [:])!.write(to: assets.appendingPathComponent("\(name).png"))
    print("wrote", name + ".png")
}
