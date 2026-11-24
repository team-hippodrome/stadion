// Draws the macOS app icon (tools/macos/AppIcon.png): "stadion" in white with a navy outline (Avenir Next Heavy) over
// the outline of a stadium bowl, on a night-sky gradient. A neutral placeholder: no game art, logos or trademarks.
// Usage: swift tools/macos_icon.swift <output .png>
//        swift tools/macos_icon.swift --android-background|--android-foreground <output .png>
// The Android modes draw the two layers of the adaptive launcher icon (android/app/src/main/res, see
// tools/make_android_icons.sh): the sky and clouds edge to edge, and the lettering alone on transparency, both 0.8 times
// the macOS size, so that what the macOS tile shows fills the 72 dp of 108 dp a launcher's mask shows and the lettering
// stays inside the 66 dp safe zone.
import AppKit
import CoreText

let args = Array(CommandLine.arguments.dropFirst())
let mode = args.first(where: { $0.hasPrefix("--") }) ?? ""
let out = args.first(where: { !$0.hasPrefix("--") }) ?? "AppIcon.png"
let android = !mode.isEmpty
let drawSky = mode != "--android-foreground", drawLetters = mode != "--android-background"

func rgb(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
    CGColor(red: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
            blue: CGFloat(hex & 0xFF) / 255, alpha: a)
}

let ctx = CGContext(data: nil, width: 1024, height: 1024, bitsPerComponent: 8, bytesPerRow: 0,
                    space: CGColorSpace(name: CGColorSpace.sRGB)!,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!

// ---- tile: macOS icon grid (an 824 pt rounded square), sky and clouds ------------------------------------------------
let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
let shape = android ? CGPath(rect: CGRect(x: -400, y: -400, width: 1824, height: 1824), transform: nil)
                    : CGPath(roundedRect: tile, cornerWidth: 185, cornerHeight: 185, transform: nil)
if android { // everything 0.8 times as large, around the centre
    ctx.translateBy(x: 512, y: 512)
    ctx.scaleBy(x: 0.8, y: 0.8)
    ctx.translateBy(x: -512, y: -512)
}
if drawSky {
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 24, color: rgb(0x000000, 0.35))
ctx.addPath(shape)
ctx.setFillColor(rgb(0x1B2F5E))
ctx.fillPath()
ctx.restoreGState()
ctx.saveGState()
ctx.addPath(shape)
ctx.clip()
let sky = CGGradient(colorsSpace: nil, colors: [rgb(0x0E1A3A), rgb(0x1E3A78), rgb(0x2E8C8A)] as CFArray,
                     locations: [0, 0.55, 1])!
ctx.drawLinearGradient(sky, start: CGPoint(x: 0, y: tile.maxY), end: CGPoint(x: 0, y: tile.minY),
                       options: android ? [.drawsBeforeStartLocation, .drawsAfterEndLocation] : [])
// stadium bowl: two concentric ellipses (stands and pitch) below the lettering, and four floodlight dots
ctx.setLineWidth(18)
ctx.setStrokeColor(rgb(0xFFFFFF, 0.35))
ctx.strokeEllipse(in: CGRect(x: 230, y: 190, width: 564, height: 230))
ctx.setStrokeColor(rgb(0xFFFFFF, 0.6))
ctx.strokeEllipse(in: CGRect(x: 330, y: 240, width: 364, height: 130))
ctx.setFillColor(rgb(0xFFF4C2, 0.9))
for x: CGFloat in [250, 400, 624, 774] {
    ctx.fillEllipse(in: CGRect(x: x - 14, y: 760 - 14, width: 28, height: 28))
}
ctx.restoreGState()
}

// ---- lettering -------------------------------------------------------------------------------------------------------
// text centred on cx, sitting on `baseline`, scaled to maxW wide and about targetH tall; strokes outermost
// first, then the fill
func word(_ text: String, font: String, cx: CGFloat, baseline: CGFloat, maxW: CGFloat, targetH: CGFloat,
          fill: CGColor, strokes: [(CGColor, CGFloat)]) {
    let f = CTFontCreateWithName(font as CFString, 100, nil)
    var glyphs: [(CGPath, CGFloat)] = [] // path, x (at 100 pt)
    var x: CGFloat = 0
    var capH: CGFloat = 1
    for ch in text.utf16 {
        var c = ch
        var g: CGGlyph = 0
        CTFontGetGlyphsForCharacters(f, &c, &g, 1)
        var adv = CGSize.zero
        CTFontGetAdvancesForGlyphs(f, .horizontal, &g, &adv, 1)
        let p = CTFontCreatePathForGlyph(f, g, nil) ?? CGMutablePath()
        capH = max(capH, p.boundingBoxOfPath.maxY)
        glyphs.append((p, x))
        x += adv.width - 3
    }
    let w = x + 3
    let sx = maxW / w
    let sy = min(max(targetH / capH, sx * 0.8), sx * 1.7)
    for (color, width) in strokes + [(fill, 0)] {
        for (path, gx) in glyphs {
            ctx.saveGState()
            ctx.translateBy(x: cx + (gx - w / 2) * sx, y: baseline)
            ctx.scaleBy(x: sx, y: sy)
            ctx.addPath(path)
            if width > 0 {
                ctx.setLineJoin(.round)
                ctx.setLineWidth(width / sx)
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

if drawLetters {
ctx.setShadow(offset: CGSize(width: 0, height: -8), blur: 10, color: rgb(0x0A1A3A, 0.45))
word("stadion", font: "AvenirNext-Heavy", cx: 512, baseline: 470, maxW: 640, targetH: 200, fill: rgb(0xFFFFFF),
     strokes: [(rgb(0x0B1530), 36)])
}

let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: out))
print("wrote", out)
