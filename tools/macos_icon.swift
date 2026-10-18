// Draws the macOS app icon (tools/macos/AppIcon.png): the words "POKéMON STADIUM" in the colours and layout of the
// game's title logo (yellow POKéMON with a blue outline, arched; red STADIUM with a dark outline) on a sky background.
// Text only, no game art. Usage: swift tools/macos_icon.swift <output .png>
import AppKit
import CoreText

let size: CGFloat = 1024
let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "AppIcon.png"

func rgb(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
    CGColor(red: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
            blue: CGFloat(hex & 0xFF) / 255, alpha: a)
}

let ctx = CGContext(data: nil, width: Int(size), height: Int(size), bitsPerComponent: 8, bytesPerRow: 0,
                    space: CGColorSpace(name: CGColorSpace.sRGB)!,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!

// macOS icon grid: an 824 pt rounded square in the 1024 canvas
let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
let shape = CGPath(roundedRect: tile, cornerWidth: 185, cornerHeight: 185, transform: nil)
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 24, color: rgb(0x000000, 0.35))
ctx.addPath(shape)
ctx.setFillColor(rgb(0x3A8EDB))
ctx.fillPath()
ctx.restoreGState()
ctx.saveGState()
ctx.addPath(shape)
ctx.clip()
let sky = CGGradient(colorsSpace: nil, colors: [rgb(0xBFE6FF), rgb(0x5FB4F0), rgb(0x2F7FD0)] as CFArray,
                     locations: [0, 0.55, 1])!
ctx.drawLinearGradient(sky, start: CGPoint(x: 0, y: tile.maxY), end: CGPoint(x: 0, y: tile.minY), options: [])
ctx.restoreGState()

let font = CTFontCreateWithName("Impact" as CFString, 1, nil)

// one glyph run's path, at the given point size, centred horizontally on x = 0
func glyphPaths(_ text: String, _ pt: CGFloat) -> [(CGPath, CGFloat, CGFloat)] {
    let f = CTFontCreateCopyWithAttributes(font, pt, nil, nil)
    var result: [(CGPath, CGFloat, CGFloat)] = [] // path, x offset, advance
    var x: CGFloat = 0
    for ch in text.utf16 {
        var c = ch
        var g: CGGlyph = 0
        CTFontGetGlyphsForCharacters(f, &c, &g, 1)
        var adv = CGSize.zero
        CTFontGetAdvancesForGlyphs(f, .horizontal, &g, &adv, 1)
        let p = CTFontCreatePathForGlyph(f, g, nil) ?? CGMutablePath()
        result.append((p, x, adv.width))
        x += adv.width
    }
    return result.map { ($0.0, $0.1 - x / 2, $0.2) }
}

// draws text centred at (cx, baseline), each letter placed on a circle of the given radius (0: straight), with outer
// and inner strokes under the fill
func word(_ text: String, pt: CGFloat, cx: CGFloat, baseline: CGFloat, radius: CGFloat, stretchY: CGFloat,
          fill: CGColor, strokes: [(CGColor, CGFloat)]) {
    let glyphs = glyphPaths(text, pt)
    for (color, width) in strokes + [(fill, 0)] {
        for (path, x, adv) in glyphs {
            ctx.saveGState()
            let mid = x + adv / 2
            if radius > 0 {
                let angle = mid / radius
                ctx.translateBy(x: cx + radius * sin(angle), y: baseline - radius + radius * cos(angle))
                ctx.rotate(by: -angle)
            } else {
                ctx.translateBy(x: cx + mid, y: baseline)
            }
            ctx.scaleBy(x: 1, y: stretchY)
            ctx.translateBy(x: -adv / 2, y: 0)
            ctx.addPath(path)
            if width > 0 {
                ctx.setLineJoin(.round)
                ctx.setLineWidth(width / stretchY)
                ctx.setStrokeColor(color)
                ctx.strokePath()
            } else {
                ctx.setFillColor(color)
                ctx.fillPath()
            }
            ctx.restoreGState()
        }
    }
}

let yellow = rgb(0xFFCB05), blue = rgb(0x2A5DA8), navy = rgb(0x1B2A55), red = rgb(0xE3350D), white = rgb(0xFFFFFF)
ctx.setShadow(offset: CGSize(width: 0, height: -8), blur: 10, color: rgb(0x0A1A3A, 0.45))
word("POKéMON", pt: 158, cx: size / 2, baseline: 545, radius: 1400, stretchY: 1.4, fill: yellow,
     strokes: [(navy, 44), (blue, 28)])
word("STADIUM", pt: 160, cx: size / 2, baseline: 320, radius: 0, stretchY: 1.2, fill: red,
     strokes: [(white, 44), (navy, 26)])

let image = ctx.makeImage()!
let rep = NSBitmapImageRep(cgImage: image)
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: out))
print("wrote", out)
