// Draws the macOS app icon (tools/macos/AppIcon.png): the words "Pokémon Stadium" laid out like the game's title logo
// (yellow "PokéMoN" with a blue outline over a red, black-outlined "STADIUM" whose end letters stand taller) on a
// cloudy sky. Lettering only, no game art; system fonts stand in for the logo's own lettering.
// Usage: swift tools/macos_icon.swift <output .png>
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

// ---- tile: macOS icon grid (an 824 pt rounded square), sky and clouds ------------------------------------------------
let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
let shape = CGPath(roundedRect: tile, cornerWidth: 185, cornerHeight: 185, transform: nil)
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 24, color: rgb(0x000000, 0.35))
ctx.addPath(shape)
ctx.setFillColor(rgb(0x1F5FC4))
ctx.fillPath()
ctx.restoreGState()
ctx.saveGState()
ctx.addPath(shape)
ctx.clip()
let sky = CGGradient(colorsSpace: nil, colors: [rgb(0x1D4FB8), rgb(0x3F86E0), rgb(0x8EC6F4)] as CFArray,
                     locations: [0, 0.6, 1])!
ctx.drawLinearGradient(sky, start: CGPoint(x: 0, y: tile.maxY), end: CGPoint(x: 0, y: tile.minY), options: [])
// clouds: clusters of soft white puffs (x, y, radius)
let clouds: [[(CGFloat, CGFloat, CGFloat)]] = [
    [(170, 850, 45), (230, 870, 60), (295, 850, 45)],
    [(600, 890, 40), (665, 905, 55), (730, 885, 42)],
    [(870, 800, 45), (925, 830, 55)],
    [(140, 200, 55), (215, 175, 70), (290, 195, 55)],
    [(700, 160, 60), (790, 185, 70), (880, 165, 60)],
]
ctx.setShadow(offset: CGSize(width: 0, height: -6), blur: 18, color: rgb(0x2050A0, 0.5))
for cluster in clouds {
    for (x, y, r) in cluster {
        ctx.addEllipse(in: CGRect(x: x - r, y: y - r * 0.8, width: 2 * r, height: 1.6 * r))
    }
    ctx.setFillColor(rgb(0xFFFFFF, 0.88))
    ctx.fillPath()
}
ctx.restoreGState()

// ---- lettering -------------------------------------------------------------------------------------------------------
struct Letter {
    var char: String
    var size: CGFloat    // point size
    var raise: CGFloat = 0 // baseline offset
    var stretchY: CGFloat = 1
    var blueCentre = false // the logo's o and é have blue centres
}

// draws letters left to right, centred on cx, with the strokes (outermost first) under the fill; `kern` overlaps them
func word(_ letters: [Letter], font: String, cx: CGFloat, baseline: CGFloat, kern: CGFloat, fill: CGColor,
          strokes: [(CGColor, CGFloat)], centre: CGColor? = nil) {
    var placed: [(CGPath, CGFloat, Letter)] = []
    var x: CGFloat = 0
    for l in letters {
        let f = CTFontCreateWithName(font as CFString, l.size, nil)
        var chars = Array(l.char.utf16)
        var glyphs = [CGGlyph](repeating: 0, count: chars.count)
        CTFontGetGlyphsForCharacters(f, &chars, &glyphs, chars.count)
        var adv = CGSize.zero
        CTFontGetAdvancesForGlyphs(f, .horizontal, &glyphs[0], &adv, 1)
        placed.append((CTFontCreatePathForGlyph(f, glyphs[0], nil)!, x, l))
        x += adv.width - kern
    }
    let left = cx - (x + kern) / 2
    for (color, width) in strokes + [(fill, 0)] {
        for (path, px, l) in placed {
            ctx.saveGState()
            ctx.translateBy(x: left + px, y: baseline + l.raise)
            ctx.scaleBy(x: 1, y: l.stretchY)
            ctx.addPath(path)
            if width > 0 {
                ctx.setLineJoin(.round)
                ctx.setLineWidth(width / l.stretchY)
                ctx.setStrokeColor(color)
                ctx.strokePath()
            } else {
                ctx.setFillColor(color)
                ctx.fillPath()
                if l.blueCentre, let centre = centre {
                    let b = path.boundingBoxOfPath
                    let r = min(b.width, b.height) * 0.22
                    ctx.addEllipse(in: CGRect(x: b.midX - r, y: b.midY - r * 1.1, width: 2 * r, height: 2.2 * r))
                    ctx.setFillColor(centre)
                    ctx.fillPath()
                }
            }
            ctx.restoreGState()
        }
    }
}

let yellow = rgb(0xFFCB05), blue = rgb(0x2E5EB8), deepBlue = rgb(0x1A2F7A)
let red = rgb(0xE01A22), black = rgb(0x0E0E10)

ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 12, color: rgb(0x000000, 0.45))
// "PokéMoN": capitals tall, o's and é small and sitting on the baseline, the line rising slightly to the middle
let pk = "Arial Rounded MT Bold"
word([
    Letter(char: "P", size: 215, raise: -22, stretchY: 1.2),
    Letter(char: "o", size: 165, raise: 4, blueCentre: true),
    Letter(char: "k", size: 190, raise: 14, stretchY: 1.08),
    Letter(char: "é", size: 165, raise: 20),
    Letter(char: "M", size: 190, raise: 14, stretchY: 1.08),
    Letter(char: "o", size: 165, raise: 4, blueCentre: true),
    Letter(char: "N", size: 205, raise: -10, stretchY: 1.15),
], font: pk, cx: 512, baseline: 530, kern: 18, fill: yellow, strokes: [(deepBlue, 46), (blue, 30)], centre: blue)

// "STADIUM": heavy serif, the end letters taller (the logo's slight bowl)
let st = "SuperClarendon-Black"
word([
    Letter(char: "S", size: 150, raise: -8, stretchY: 1.4),
    Letter(char: "T", size: 140, stretchY: 1.28),
    Letter(char: "A", size: 136, stretchY: 1.2),
    Letter(char: "D", size: 134, stretchY: 1.16),
    Letter(char: "I", size: 134, stretchY: 1.16),
    Letter(char: "U", size: 136, stretchY: 1.2),
    Letter(char: "M", size: 150, raise: -8, stretchY: 1.4),
], font: st, cx: 512, baseline: 335, kern: 10, fill: red, strokes: [(black, 34)])

let image = ctx.makeImage()!
let rep = NSBitmapImageRep(cgImage: image)
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: out))
print("wrote", out)
