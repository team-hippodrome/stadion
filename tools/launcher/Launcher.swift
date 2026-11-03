// Pokémon Stadium launcher (macOS port): pick the Stadium ROM + save and up to four Game Boy cartridges + saves for
// the players' Transfer Paks, then PLAY starts build/pokestadium. Choices use the shared portable JSON config.
// Styled after the PokemonStadiumRecomp launcher.
// Plain AppKit (the command-line tools lack SwiftUI's macro plugins). Built into "Pokemon Stadium.app" by
// tools/make_app.sh. Debug option (checkbox, off by default): a "Save editor" button opens tools/save_editor's web UI on
// the Stadium save. Test hooks: LAUNCHER_SNAPSHOT=<png> (save the window picture and quit), LAUNCHER_AUTOPLAY=1,
// LAUNCHER_DEBUG=0|1 and LAUNCHER_SAVE=<file> (override without storing), LAUNCHER_OPEN_EDITOR=1 (open the editor at
// start), LAUNCHER_EDITOR_SNAPSHOT=<png> (save the editor page picture once loaded and quit; LAUNCHER_EDITOR_JS=<js>
// runs in the page first), LAUNCHER_EDITOR_TEST=<js> (run an async test suite in the editor page, print its result,
// close the window and quit; tools/save_editor/tests/test_ui.py) with LAUNCHER_AUTO_CONFIRM=yes|no.
import AppKit
import CryptoKit
import WebKit

// ---- paths and defaults ------------------------------------------------------------------------------------------

let projectDir: String = {
    if let p = Bundle.main.path(forResource: "project-path", ofType: "txt"),
       let s = try? String(contentsOfFile: p, encoding: .utf8) {
        return s.trimmingCharacters(in: .whitespacesAndNewlines)
    }
    return FileManager.default.currentDirectoryPath
}()

enum Defaults {
    static let rom = ""
    static let yellowRom = ""
    static var saves: String { launcherDirectory.path + "/saves" }
    static var stadiumSave: String { saves + "/stadium.flash" }
    static var yellowSave: String { saves + "/yellow.sav" }
    static let stadiumSha1 = "ed7bef5a306f88c0a6e96b15e71fee2ef32058f3" // Pokémon Stadium (USA) v1.0
}

let launcherDirectory = portableLauncherDirectory(bundle: Bundle.main.bundleURL,
                                                  executable: Bundle.main.executableURL,
                                                  environment: ProcessInfo.processInfo.environment)
let store = PortableLauncherStore(directory: launcherDirectory, legacy: UserDefaults.standard.dictionaryRepresentation())

func asset(_ name: String) -> NSImage? {
    if let p = Bundle.main.path(forResource: name, ofType: "png") { return NSImage(contentsOfFile: p) }
    return NSImage(contentsOfFile: projectDir + "/tools/launcher/assets/\(name).png")
}

// ---- colours -----------------------------------------------------------------------------------------------------

extension NSColor {
    convenience init(hex: UInt32, alpha: CGFloat = 1) {
        self.init(srgbRed: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
                  blue: CGFloat(hex & 0xFF) / 255, alpha: alpha)
    }
}

enum Theme {
    static let window = NSColor(hex: 0x12161D)
    static let card = NSColor(hex: 0x1A1F27)
    static let cardBorder = NSColor(hex: 0x2A313C)
    static let text = NSColor(hex: 0xE6E9EE)
    static let dim = NSColor(hex: 0x8B95A3)
    static let green = NSColor(hex: 0x2EA043)
    static let players = [NSColor(hex: 0xF0B429), NSColor(hex: 0xE5484D), NSColor(hex: 0x3B82F6), NSColor(hex: 0x8E7FD3)]
}

// ---- Game Boy cartridge / save inspection ---------------------------------------------------------------------------

struct CartInfo {
    var kind = ""        // "yellow" | "red" | "blue" | "green" | "" (unknown) — picks the cartridge picture
    var label = ""       // e.g. "Pokémon Gelb"
    var trainer = ""     // from the save (main data checksum must match)
    var trainerId = ""
}

func gen1Char(_ b: UInt8) -> String? {
    switch b {
    case 0x80...0x99: return String(UnicodeScalar(UInt8(65) + (b - 0x80)))
    case 0xA0...0xB9: return String(UnicodeScalar(UInt8(97) + (b - 0xA0)))
    case 0xF6...0xFF: return String(UnicodeScalar(UInt8(48) + (b - 0xF6)))
    case 0x7F: return " "
    case 0xC0: return "Ä" // German charmap extras
    case 0xC1: return "Ö"
    case 0xC2: return "Ü"
    case 0xC3: return "ä"
    case 0xC4: return "ö"
    case 0xC5: return "ü"
    default: return nil
    }
}

func inspectCart(rom: String, save: String) -> CartInfo? {
    guard let data = FileManager.default.contents(atPath: rom), data.count >= 0x150 else { return nil }
    let bytes = [UInt8](data)
    let title = String(bytes: bytes[0x134..<0x144].map { ($0 < 0x20 || $0 > 0x7E) ? 0x20 : $0 }, encoding: .ascii) ?? ""
    var info = CartInfo()
    let german = rom.contains("Germany") || rom.contains("(G)")
    if title.contains("YEL") || title.contains("GELB") {
        info.kind = "yellow"; info.label = german ? "Pokémon Gelb" : "Pokémon Yellow"
    } else if title.contains("RED") || title.contains("ROT") {
        info.kind = "red"; info.label = german ? "Pokémon Rot" : "Pokémon Red"
    } else if title.contains("BLUE") || title.contains("BLAU") {
        info.kind = "blue"; info.label = german ? "Pokémon Blau" : "Pokémon Blue"
    } else if title.contains("GREEN") {
        info.kind = "green"; info.label = "Pokémon Green"
    } else {
        info.label = title.trimmingCharacters(in: .whitespaces).capitalized
    }
    if let s = FileManager.default.contents(atPath: save), s.count >= 0x3524 {
        let sav = [UInt8](s)
        var sum: UInt32 = 0
        for i in 0x2598...0x3522 { sum &+= UInt32(sav[i]) }
        if UInt8(truncatingIfNeeded: ~sum) == sav[0x3523] {
            var name = ""
            for i in 0x2598...0x25A2 {
                if sav[i] == 0x50 || sav[i] == 0 { break }
                if let c = gen1Char(sav[i]) { name += c }
            }
            info.trainer = name
            info.trainerId = String(format: "%05d", (Int(sav[0x2605]) << 8) | Int(sav[0x2606]))
        }
    }
    return info
}

// ---- small view helpers --------------------------------------------------------------------------------------------

final class Flipped: NSView {
    override var isFlipped: Bool { true }
}

func label(_ s: String, size: CGFloat = 12, weight: NSFont.Weight = .regular, color: NSColor = Theme.text) -> NSTextField {
    let l = NSTextField(labelWithString: s)
    l.font = .systemFont(ofSize: size, weight: weight)
    l.textColor = color
    l.lineBreakMode = .byTruncatingMiddle
    return l
}

func makeCard(_ frame: NSRect) -> Flipped {
    let v = Flipped(frame: frame)
    v.wantsLayer = true
    v.layer?.backgroundColor = Theme.card.cgColor
    v.layer?.cornerRadius = 8
    v.layer?.borderColor = Theme.cardBorder.cgColor
    v.layer?.borderWidth = 1
    v.layer?.masksToBounds = true
    return v
}

func button(_ title: String, _ target: AnyObject, _ action: Selector) -> NSButton {
    let b = NSButton(title: title, target: target, action: action)
    b.bezelStyle = .rounded
    b.controlSize = .small
    b.font = .systemFont(ofSize: 11)
    return b
}

func dot(_ color: NSColor) -> NSView {
    let d = NSView(frame: NSRect(x: 0, y: 0, width: 8, height: 8))
    d.wantsLayer = true
    d.layer?.backgroundColor = color.cgColor
    d.layer?.cornerRadius = 4
    return d
}

func checkbox(_ title: String, _ target: AnyObject, _ action: Selector) -> NSButton {
    let c = NSButton(checkboxWithTitle: title, target: target, action: action)
    c.attributedTitle = NSAttributedString(string: title, attributes: [.foregroundColor: Theme.text,
                                                                       .font: NSFont.systemFont(ofSize: 12)])
    return c
}

func choosePath(save: Bool, current: String, types: [String], message: String) -> String? {
    let start = current.isEmpty ? URL(fileURLWithPath: Defaults.saves)
                                : URL(fileURLWithPath: (current as NSString).deletingLastPathComponent)
    if save {
        let p = NSSavePanel()
        p.directoryURL = start
        p.nameFieldStringValue = current.isEmpty ? "cartridge.sav" : (current as NSString).lastPathComponent
        p.canCreateDirectories = true
        p.message = message
        return p.runModal() == .OK ? p.url?.path : nil
    }
    let p = NSOpenPanel()
    p.directoryURL = start
    p.allowsMultipleSelection = false
    p.canChooseDirectories = false
    p.allowedFileTypes = types
    p.message = message
    return p.runModal() == .OK ? p.url?.path : nil
}

// ---- one player card ------------------------------------------------------------------------------------------------

final class PlayerCard: NSObject {
    let index: Int
    let view: Flipped
    var enabled: NSButton!
    let cart = NSImageView()
    let gameDot = dot(Theme.dim)
    let game = label("", size: 15, weight: .bold)
    let trainer = label("—")
    let trainerId = label("—")
    let saveName = label("—", size: 11)
    var changeRom: NSButton!
    var clearRom: NSButton!
    var saveButton: NSButton!
    var editButton: NSButton!
    var hint: NSTextField!
    var onEdit: ((Int) -> Void)?
    var rom: String { didSet { store.set(rom, forKey: "p\(index)_rom") } }
    var save: String { didSet { store.set(save, forKey: "p\(index)_save") } }
    var isOn: Bool { didSet { store.set(isOn, forKey: "p\(index)_on") } }

    init(index: Int, frame: NSRect) {
        self.index = index
        rom = store.string(forKey: "p\(index)_rom") ?? (index == 0 ? Defaults.yellowRom : "")
        save = store.string(forKey: "p\(index)_save") ?? (index == 0 ? Defaults.yellowSave : "")
        isOn = store.object(forKey: "p\(index)_on") as? Bool ?? false
        view = makeCard(frame)
        super.init()

        let w = frame.width
        let accent = Theme.players[index]
        let strip = NSView(frame: NSRect(x: 0, y: 0, width: w, height: 3))
        strip.wantsLayer = true
        strip.layer?.backgroundColor = accent.cgColor
        view.addSubview(strip)

        let title = label("PLAYER \(index + 1)", size: 12, weight: .bold, color: accent)
        title.frame = NSRect(x: 12, y: 14, width: 100, height: 16)
        view.addSubview(title)
        enabled = checkbox("Enabled", self, #selector(toggle))
        enabled.frame = NSRect(x: w - 86, y: 12, width: 80, height: 20)
        view.addSubview(enabled)

        cart.frame = NSRect(x: (w - 128) / 2, y: 40, width: 128, height: 134)
        cart.imageScaling = .scaleProportionallyUpOrDown
        view.addSubview(cart)

        changeRom = button("Change ROM", self, #selector(pickRom))
        clearRom = button("×", self, #selector(clear))
        view.addSubview(changeRom)
        view.addSubview(clearRom)

        gameDot.frame.origin = NSPoint(x: 12, y: 223)
        view.addSubview(gameDot)
        game.frame = NSRect(x: 26, y: 214, width: w - 34, height: 22)
        view.addSubview(game)

        for (i, (name, value)) in [("Trainer", trainer), ("ID", trainerId)].enumerated() {
            let l = label(name, size: 12, color: Theme.dim)
            l.frame = NSRect(x: 12, y: 246 + CGFloat(i) * 20, width: 60, height: 16)
            value.frame = NSRect(x: 72, y: 246 + CGFloat(i) * 20, width: w - 84, height: 16)
            view.addSubview(l)
            view.addSubview(value)
        }
        let saveLabel = label("Save", size: 12, color: Theme.dim)
        saveLabel.frame = NSRect(x: 12, y: 300, width: 40, height: 16)
        view.addSubview(saveLabel)
        saveName.frame = NSRect(x: 52, y: 300, width: w - 132, height: 16)
        view.addSubview(saveName)
        saveButton = button("Change", self, #selector(pickSave))
        saveButton.frame = NSRect(x: w - 76, y: 294, width: 66, height: 26)
        view.addSubview(saveButton)

        hint = label("Transfer Pak · controller \(index + 1)", size: 10, color: Theme.dim)
        hint.frame = NSRect(x: 12, y: frame.height - 26, width: w - 24, height: 14)
        view.addSubview(hint)
        editButton = button("Edit save", self, #selector(edit))
        editButton.toolTip = "Open this cartridge's save in the save editor (debug)"
        editButton.frame = NSRect(x: w - 90, y: frame.height - 33, width: 80, height: 26)
        editButton.isHidden = true
        view.addSubview(editButton)
        refresh()
    }

    var hasCart: Bool { !rom.isEmpty }

    func refresh() {
        let info = hasCart ? inspectCart(rom: rom, save: save) : nil
        let w = view.frame.width
        enabled.state = isOn && hasCart ? .on : .off
        enabled.isEnabled = hasCart
        cart.image = asset(info.map { $0.kind.isEmpty ? "empty" : $0.kind } ?? "empty")
        cart.alphaValue = isOn && hasCart ? 1 : 0.45
        if hasCart {
            changeRom.title = "Change ROM"
            changeRom.frame = NSRect(x: w / 2 - 70, y: 182, width: 100, height: 26)
            clearRom.frame = NSRect(x: w / 2 + 36, y: 182, width: 34, height: 26)
            clearRom.isHidden = false
        } else {
            changeRom.title = "Load ROM"
            changeRom.frame = NSRect(x: w / 2 - 45, y: 182, width: 90, height: 26)
            clearRom.isHidden = true
        }
        if let info = info {
            game.stringValue = info.label
            game.textColor = Theme.text
            gameDot.layer?.backgroundColor = Theme.players[index].cgColor
            let saveExists = FileManager.default.fileExists(atPath: save)
            trainer.stringValue = !info.trainer.isEmpty ? info.trainer : (saveExists ? "?" : "new save")
            trainerId.stringValue = info.trainerId.isEmpty ? "—" : info.trainerId
        } else {
            game.stringValue = hasCart ? "ROM not found" : "No game loaded"
            game.textColor = hasCart ? .systemRed : Theme.dim
            gameDot.layer?.backgroundColor = Theme.dim.cgColor
            trainer.stringValue = "—"
            trainerId.stringValue = "—"
        }
        saveName.stringValue = save.isEmpty ? "—" : (save as NSString).lastPathComponent
        saveName.toolTip = save
        saveButton.isEnabled = hasCart
    }

    @objc func toggle() { isOn = enabled.state == .on; refresh() }

    @objc func pickRom() {
        guard let p = choosePath(save: false, current: rom.isEmpty ? Defaults.yellowRom : rom, types: ["gb", "gbc"],
                                 message: "Game Boy cartridge for player \(index + 1)") else { return }
        rom = p
        if save.isEmpty {
            save = Defaults.saves + "/" + ((p as NSString).lastPathComponent as NSString).deletingPathExtension + ".sav"
        }
        isOn = true
        refresh()
    }

    @objc func pickSave() {
        guard let p = choosePath(save: true, current: save, types: ["sav"],
                                 message: "Battery save for player \(index + 1)'s cartridge (existing or new)") else { return }
        save = p
        refresh()
    }

    @objc func clear() { rom = ""; save = ""; isOn = false; refresh() }

    @objc func edit() { onEdit?(index) }

    // the GB save the editor can open with this card (enabled, cartridge and save present)
    var editable: Bool { isOn && hasCart && FileManager.default.fileExists(atPath: save) }
}

// ---- save editor (debug option) ---------------------------------------------------------------------------------------
// tools/save_editor's web UI as a child process on a free localhost port (serve --port 0 --write --exit-with-parent),
// shown in a WKWebView window. "Save to file" overwrites the opened save (previous contents kept as <save>.bak).
// The server is stopped when the window closes or the launcher quits; --exit-with-parent covers a launcher crash.

final class SaveEditor: NSObject, NSWindowDelegate, WKNavigationDelegate, WKUIDelegate, WKDownloadDelegate {
    let path: String
    let onClose: () -> Void
    var server: Process?
    var window: NSWindow?
    var web: WKWebView?
    var snapshotPath: String?  // test hooks LAUNCHER_EDITOR_SNAPSHOT, LAUNCHER_EDITOR_JS
    var testScript: String?
    var testFile: String?      // LAUNCHER_EDITOR_TEST: async JS test suite, result printed, then the window is closed
    // LAUNCHER_AUTO_CONFIRM=yes|no: answer the page's confirm() and the unsaved-changes prompt without showing them
    let autoAnswer: Bool? = ProcessInfo.processInfo.environment["LAUNCHER_AUTO_CONFIRM"].map { $0 == "yes" }

    func testLog(_ line: String) {
        FileHandle.standardOutput.write((line.replacingOccurrences(of: "\n", with: " ") + "\n").data(using: .utf8)!)
    }

    let gb: [(label: String, save: String, rom: String)]  // Transfer Pak cartridges edited alongside
    var section: String?

    init(path: String, gb: [(label: String, save: String, rom: String)] = [], section: String? = nil,
         onClose: @escaping () -> Void) {
        self.path = path
        self.gb = gb
        self.section = section
        self.onClose = onClose
        super.init()
    }

    // bring the window forward on a section ("gb0" ... ) of the page
    func focus(section s: String?) {
        window?.makeKeyAndOrderFront(nil)
        if let s = s, let web = web { web.evaluateJavaScript("window.showSection && showSection('\(s)')") } else { section = s }
    }

    var isOpen: Bool { window != nil }

    func start() throws {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        p.arguments = ["python3", "-m", "stadium_save", "serve", path, "--port", "0", "--write", "--exit-with-parent"]
            + gb.flatMap { ["--gb", "\($0.label)|\($0.save)|\($0.rom)"] }
        p.currentDirectoryURL = URL(fileURLWithPath: projectDir + "/tools/save_editor")
        var env = ProcessInfo.processInfo.environment
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        p.environment = env
        let out = Pipe()
        p.standardOutput = out
        let logPath = Defaults.saves + "/save_editor.log"
        FileManager.default.createFile(atPath: logPath, contents: nil)
        p.standardError = FileHandle(forWritingAtPath: logPath)
        var buffer = ""
        out.fileHandleForReading.readabilityHandler = { [weak self] h in
            let chunk = h.availableData
            if chunk.isEmpty { h.readabilityHandler = nil; return }
            buffer += String(decoding: chunk, as: UTF8.self)
            if let r = buffer.range(of: #"127\.0\.0\.1:(\d+)"#, options: .regularExpression),
               let port = Int(buffer[r].split(separator: ":").last ?? "") {
                h.readabilityHandler = nil
                DispatchQueue.main.async { self?.show(port: port) }
            }
        }
        p.terminationHandler = { [weak self] proc in
            DispatchQueue.main.async {
                guard let self = self, self.server === proc else { return }
                self.server = nil
                if self.web == nil {  // died before the page came up
                    let a = NSAlert()
                    a.messageText = "Save editor"
                    a.informativeText = "The save editor could not start (python3 required).\nDetails: \(logPath)"
                    a.runModal()
                    self.close()
                }
            }
        }
        try p.run()
        server = p
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1180, height: 800),
                         styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        w.title = "Save editor — " + ([path] + gb.map { $0.save }).map { ($0 as NSString).lastPathComponent }.joined(separator: " + ")
        w.representedURL = URL(fileURLWithPath: path)
        w.appearance = NSAppearance(named: .darkAqua)
        w.backgroundColor = Theme.window
        w.contentMinSize = NSSize(width: 820, height: 560)
        w.isReleasedWhenClosed = false
        w.delegate = self
        w.center()
        w.makeKeyAndOrderFront(nil)
        window = w
    }

    func show(port: Int) {
        guard let w = window, web == nil else { return }
        let v = WKWebView(frame: w.contentView!.bounds, configuration: WKWebViewConfiguration())
        v.autoresizingMask = [.width, .height]
        v.navigationDelegate = self
        v.uiDelegate = self
        w.contentView = v
        web = v
        v.load(URLRequest(url: URL(string: "http://127.0.0.1:\(port)/" + (section.map { "#" + $0 } ?? ""))!))
    }

    func stop() {
        if let p = server, p.isRunning {
            p.terminate()
            p.waitUntilExit()
        }
        server = nil
    }

    func close() { window?.close() }

    // Ask the page about unsaved changes before closing (the page sets window.hasUnsavedChanges).
    var closeConfirmed = false
    func windowShouldClose(_ sender: NSWindow) -> Bool {
        guard let web = web, !closeConfirmed else { return true }
        web.evaluateJavaScript("window.hasUnsavedChanges ? window.hasUnsavedChanges() : false") { result, _ in
            if result as? Bool == true, let answer = self.autoAnswer {
                self.testLog("CLOSE-PROMPT -> \(answer ? "close" : "cancel")")
                guard answer else { return }
            } else if result as? Bool == true {
                let a = NSAlert()
                a.messageText = "Close the save editor without saving?"
                a.informativeText = "Your changes to \((self.path as NSString).lastPathComponent) have not been saved."
                a.addButton(withTitle: "Cancel")
                a.addButton(withTitle: "Close without saving")
                guard a.runModal() == .alertSecondButtonReturn else { return }
            }
            self.closeConfirmed = true
            sender.close()
        }
        return false
    }

    func windowWillClose(_ notification: Notification) {
        stop()
        web = nil
        window = nil
        onClose()
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        if let file = testFile { return runTest(webView, file) }
        guard let shot = snapshotPath else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {  // let /api/initial fill the page
            if let js = self.testScript { webView.evaluateJavaScript(js) }
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + (testScript == nil ? 1.0 : 3.0)) {
            webView.takeSnapshot(with: nil) { image, _ in
                if let tiff = image?.tiffRepresentation, let rep = NSBitmapImageRep(data: tiff) {
                    try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: shot))
                }
                NSApp.terminate(nil)
            }
        }
    }

    func runTest(_ webView: WKWebView, _ file: String) {
        guard let body = try? String(contentsOfFile: file, encoding: .utf8) else {
            testLog("TEST-RESULT: cannot read \(file)"); NSApp.terminate(nil); return
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {  // let /api/initial fill the page
            webView.callAsyncJavaScript(body, arguments: ["mode": ProcessInfo.processInfo.environment["LAUNCHER_AUTO_CONFIRM"] ?? ""],
                                        in: nil, in: .page) { result in
                switch result {
                case .success(let v): self.testLog("TEST-RESULT: \(v)")
                case .failure(let e): self.testLog("TEST-RESULT: error \(e)")
                }
                self.window?.performClose(nil)  // goes through the unsaved-changes prompt
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                    self.testLog("WINDOW: \(self.window == nil ? "closed" : "open")")
                    NSApp.terminate(nil)
                }
            }
        }
    }

    // the page's alert() / confirm(): WKWebView shows nothing (and confirm() answers false) without these
    func webView(_ webView: WKWebView, runJavaScriptAlertPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping () -> Void) {
        if autoAnswer != nil { testLog("ALERT: \(message)"); return completionHandler() }
        let a = NSAlert()
        a.messageText = "Save editor"
        a.informativeText = message
        if let w = window { a.beginSheetModal(for: w) { _ in completionHandler() } } else { a.runModal(); completionHandler() }
    }

    func webView(_ webView: WKWebView, runJavaScriptConfirmPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping (Bool) -> Void) {
        if let answer = autoAnswer { testLog("CONFIRM: \(message) -> \(answer ? "ok" : "cancel")"); return completionHandler(answer) }
        let a = NSAlert()
        a.messageText = message
        a.addButton(withTitle: "OK")
        a.addButton(withTitle: "Cancel")
        if let w = window {
            a.beginSheetModal(for: w) { completionHandler($0 == .alertFirstButtonReturn) }
        } else {
            completionHandler(a.runModal() == .alertFirstButtonReturn)
        }
    }

    // the page's "Open save…" file input
    func webView(_ webView: WKWebView, runOpenPanelWith parameters: WKOpenPanelParameters,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping ([URL]?) -> Void) {
        let p = NSOpenPanel()
        p.directoryURL = URL(fileURLWithPath: (path as NSString).deletingLastPathComponent)
        p.allowsMultipleSelection = false
        completionHandler(p.runModal() == .OK ? p.urls : nil)
    }

    // the page's "Download save" (a blob link) → save panel
    func webView(_ webView: WKWebView, decidePolicyFor navigationAction: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        decisionHandler(navigationAction.shouldPerformDownload ? .download : .allow)
    }

    func webView(_ webView: WKWebView, navigationAction: WKNavigationAction, didBecome download: WKDownload) {
        download.delegate = self
    }

    func download(_ download: WKDownload, decideDestinationUsing response: URLResponse, suggestedFilename: String,
                  completionHandler: @escaping (URL?) -> Void) {
        let p = NSSavePanel()
        p.directoryURL = URL(fileURLWithPath: (path as NSString).deletingLastPathComponent)
        p.nameFieldStringValue = suggestedFilename
        guard p.runModal() == .OK, let url = p.url else { return completionHandler(nil) }
        try? FileManager.default.removeItem(at: url)  // the save panel already confirmed replacing it
        completionHandler(url)
    }
}

// Big green PLAY button.
final class PlayButton: NSButton {
    override init(frame: NSRect) {
        super.init(frame: frame)
        isBordered = false
        wantsLayer = true
        layer?.backgroundColor = Theme.green.cgColor
        layer?.cornerRadius = 8
        layer?.borderColor = NSColor.white.withAlphaComponent(0.85).cgColor
        layer?.borderWidth = 2
        attributedTitle = NSAttributedString(string: "PLAY", attributes: [
            .foregroundColor: NSColor.white, .font: NSFont.systemFont(ofSize: 30, weight: .heavy)])
    }
    required init?(coder: NSCoder) { fatalError() }
}

// ---- the launcher window ---------------------------------------------------------------------------------------------

final class Launcher: NSObject, NSApplicationDelegate, NSWindowDelegate {
    var window: NSWindow?
    var cards: [PlayerCard] = []
    let romTitle = label("", size: 17, weight: .bold)
    let romStatus = label("", size: 12)
    let saveLabel = label("", size: 11, color: Theme.dim)
    var autoplay: NSButton!
    var debugBox: NSButton!
    var editorButton: NSButton!
    var editor: SaveEditor?
    var rom: String { didSet { store.set(rom, forKey: "rom") } }
    var save: String { didSet { store.set(save, forKey: "save") } }
    var debug: Bool { didSet { store.set(debug, forKey: "debug") } }
    var game: Process?

    override init() {
        let env = ProcessInfo.processInfo.environment
        rom = store.string(forKey: "rom") ?? Defaults.rom
        save = env["LAUNCHER_SAVE"] ?? store.string(forKey: "save") ?? Defaults.stadiumSave
        debug = env["LAUNCHER_DEBUG"].map { $0 == "1" } ?? store.bool(forKey: "debug")
        super.init()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        buildMenu()
        if let error = store.lastError { fail(error); NSApp.terminate(nil); return }
        store.onError = { [weak self] message in self?.fail(message) }
        let env = ProcessInfo.processInfo.environment
        let W: CGFloat = 960, H: CGFloat = 556
        let content = Flipped(frame: NSRect(x: 0, y: 0, width: W, height: H))
        content.wantsLayer = true
        content.layer?.backgroundColor = Theme.window.cgColor

        let title = label("Pokémon Stadium", size: 18, weight: .bold)
        title.frame = NSRect(x: 16, y: 10, width: 170, height: 24)
        let sep = label("|", size: 16, color: Theme.cardBorder)
        sep.frame = NSRect(x: 182, y: 11, width: 10, height: 22)
        let sub = label("macOS port · Transfer Pak", size: 14, color: Theme.dim)
        sub.frame = NSRect(x: 198, y: 13, width: 300, height: 20)
        for v in [title, sep, sub] { content.addSubview(v) }
        let headerLine = NSView(frame: NSRect(x: 0, y: 44, width: W, height: 1))
        headerLine.wantsLayer = true
        headerLine.layer?.backgroundColor = Theme.cardBorder.cgColor
        content.addSubview(headerLine)

        let cw: CGFloat = 222, gap: CGFloat = 14
        for i in 0..<4 {
            let card = PlayerCard(index: i, frame: NSRect(x: 16 + CGFloat(i) * (cw + gap), y: 58, width: cw, height: 366))
            card.onEdit = { [weak self] i in self?.openEditor(gbIndex: i) }
            cards.append(card)
            content.addSubview(card.view)
        }

        // bottom bar: Stadium cartridge, ROM check, save, PLAY
        let bar = Flipped(frame: NSRect(x: 0, y: 440, width: W, height: 116))
        bar.wantsLayer = true
        bar.layer?.backgroundColor = Theme.card.cgColor
        let barLine = NSView(frame: NSRect(x: 0, y: 0, width: W, height: 1))
        barLine.wantsLayer = true
        barLine.layer?.backgroundColor = Theme.cardBorder.cgColor
        bar.addSubview(barLine)
        let n64 = NSImageView(frame: NSRect(x: 24, y: 20, width: 124, height: 80))
        n64.image = asset("n64")
        n64.imageScaling = .scaleProportionallyUpOrDown
        bar.addSubview(n64)
        romTitle.frame = NSRect(x: 168, y: 22, width: 230, height: 24)
        romStatus.frame = NSRect(x: 400, y: 26, width: 160, height: 18)
        bar.addSubview(romTitle)
        bar.addSubview(romStatus)
        let changeRom = button("Change", self, #selector(pickRom))
        changeRom.frame = NSRect(x: 164, y: 56, width: 70, height: 26)
        bar.addSubview(changeRom)
        saveLabel.frame = NSRect(x: 246, y: 61, width: 200, height: 16)
        bar.addSubview(saveLabel)
        let changeSave = button("Import save", self, #selector(pickSave))
        changeSave.frame = NSRect(x: 450, y: 56, width: 100, height: 26)
        bar.addSubview(changeSave)
        editorButton = button("Save editor", self, #selector(openEditorButton))
        editorButton.toolTip = "Inspect and edit this save (tools/save_editor)"
        editorButton.frame = NSRect(x: 556, y: 56, width: 96, height: 26)
        bar.addSubview(editorButton)

        let playButton = PlayButton(frame: NSRect(x: W - 206, y: 12, width: 182, height: 66))
        playButton.target = self
        playButton.action = #selector(play)
        playButton.keyEquivalent = "\r"
        bar.addSubview(playButton)
        autoplay = checkbox("Auto-play", self, #selector(toggleAutoplay))
        autoplay.toolTip = "Start the game directly next time (hold ⌥ Option when opening the app to see this launcher)"
        autoplay.state = store.bool(forKey: "autoplay") ? .on : .off
        autoplay.frame = NSRect(x: W - 158, y: 86, width: 100, height: 18)
        bar.addSubview(autoplay)
        debugBox = checkbox("Debug", self, #selector(toggleDebug))
        debugBox.toolTip = "Show debugging tools (save editor)"
        debugBox.state = debug ? .on : .off
        debugBox.frame = NSRect(x: 168, y: 90, width: 70, height: 18)
        bar.addSubview(debugBox)
        content.addSubview(bar)

        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: W, height: H), styleMask: [.titled, .closable, .miniaturizable],
                         backing: .buffered, defer: false)
        w.title = "Pokémon Stadium — Launcher"
        w.appearance = NSAppearance(named: .darkAqua)
        w.backgroundColor = Theme.window
        w.contentView = content
        w.delegate = self
        w.center()
        window = w
        refreshRom()
        w.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)

        // Auto-play: start right away (hold ⌥ Option while opening the app to get the launcher)
        let recovery = launcherDirectory.appendingPathComponent("frontend-session.txt")
        let interrupted = FileManager.default.fileExists(atPath: recovery.path)
        if interrupted { store.set(false, forKey: "autoplay") }
        if CommandLine.arguments.contains("--safe-mode") { store.useSafeRenderer() }
        if store.bool(forKey: "autoplay") && !interrupted && !CommandLine.arguments.contains("--launcher") &&
           !CommandLine.arguments.contains("--safe-mode") && env["PORT_FORCE_LAUNCHER"] == nil &&
           !NSEvent.modifierFlags.contains(.option) && env["LAUNCHER_SNAPSHOT"] == nil {
            play()
            if game != nil { return }
        }

        if env["LAUNCHER_OPEN_EDITOR"] != nil || env["LAUNCHER_EDITOR_SNAPSHOT"] != nil || env["LAUNCHER_EDITOR_TEST"] != nil {
            openEditor(gbIndex: nil)
            editor?.testFile = env["LAUNCHER_EDITOR_TEST"]
            editor?.snapshotPath = env["LAUNCHER_EDITOR_SNAPSHOT"]
            editor?.testScript = env["LAUNCHER_EDITOR_JS"]
        }
        if let shot = env["LAUNCHER_SNAPSHOT"] {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) {
                if let rep = content.bitmapImageRepForCachingDisplay(in: content.bounds) {
                    content.cacheDisplay(in: content.bounds, to: rep)
                    try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: shot))
                }
                NSApp.terminate(nil)
            }
        } else if env["LAUNCHER_AUTOPLAY"] != nil {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { self.play() }
        }
    }

    func refreshRom() {
        let fm = FileManager.default
        if !fm.fileExists(atPath: rom) {
            romTitle.stringValue = "No Stadium ROM"
            romStatus.stringValue = "✗ not found"
            romStatus.textColor = .systemRed
        } else {
            let sha = fm.contents(atPath: rom).map { Insecure.SHA1.hash(data: $0).map { String(format: "%02x", $0) }.joined() }
            if sha == Defaults.stadiumSha1 {
                romTitle.stringValue = "Pokémon Stadium (USA)"
                romStatus.stringValue = "✓ Verified"
                romStatus.textColor = Theme.green
            } else {
                romTitle.stringValue = (rom as NSString).lastPathComponent
                romStatus.stringValue = "⚠ unknown dump"
                romStatus.textColor = .systemOrange
            }
        }
        romTitle.toolTip = rom
        let exists = fm.fileExists(atPath: save)
        saveLabel.stringValue = "Save: \((save as NSString).lastPathComponent)\(exists ? "" : "  (new)")"
        saveLabel.toolTip = save
        editorButton.isHidden = !debug
        editorButton.isEnabled = game == nil && editor == nil
        for c in cards {
            c.editButton.isHidden = !debug || !c.editable
            c.editButton.isEnabled = game == nil
            c.hint.stringValue = c.editButton.isHidden ? "Transfer Pak · controller \(c.index + 1)" : "Controller \(c.index + 1)"
        }
    }

    func buildMenu() {
        let main = NSMenu()
        let appItem = NSMenuItem()
        main.addItem(appItem)
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Quit Pokémon Stadium", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appItem.submenu = appMenu
        // standard Edit menu: copy/paste/undo in text fields (the save editor's page) need these key equivalents
        let editItem = NSMenuItem()
        main.addItem(editItem)
        let edit = NSMenu(title: "Edit")
        edit.addItem(withTitle: "Undo", action: Selector(("undo:")), keyEquivalent: "z")
        edit.addItem(withTitle: "Redo", action: Selector(("redo:")), keyEquivalent: "Z")
        edit.addItem(.separator())
        edit.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
        edit.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        edit.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
        edit.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
        editItem.submenu = edit
        let windowItem = NSMenuItem()
        main.addItem(windowItem)
        let win = NSMenu(title: "Window")
        win.addItem(withTitle: "Close Window", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        win.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        windowItem.submenu = win
        let filesItem = NSMenuItem()
        main.addItem(filesItem)
        let files = NSMenu(title: "Files & Help")
        for (title, action) in [("Check setup", #selector(checkSetup)),
                                ("Open settings folder", #selector(openSettings)),
                                ("Open saves folder", #selector(openSaves)),
                                ("Export N64 save", #selector(exportSave)),
                                ("Export diagnostics", #selector(exportDiagnostics)),
                                ("Disable Auto-play", #selector(disableAutoplay)),
                                ("Use safe renderer next start", #selector(safeRenderer))] {
            let item = files.addItem(withTitle: title, action: action, keyEquivalent: "")
            item.target = self
        }
        filesItem.submenu = files
        NSApp.mainMenu = main
    }

    func runUtility(_ arguments: [String]) throws -> (Int32, String) {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: projectDir + "/build/pokestadium")
        p.arguments = arguments
        var env = ProcessInfo.processInfo.environment
        env["SHIP_HOME"] = launcherDirectory.path
        p.environment = env
        let out = Pipe()
        p.standardOutput = out; p.standardError = out
        try p.run()
        let data = out.fileHandleForReading.readDataToEndOfFile()
        p.waitUntilExit()
        return (p.terminationStatus, String(data: data, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? "")
    }
    func preflight() throws -> (Int32, String) {
        var arguments = ["--check-launcher", rom, save]
        for i in 0..<4 {
            let on = i < cards.count ? cards[i].isOn : store.bool(forKey: "p\(i)_on")
            let path = i < cards.count ? cards[i].rom : store.string(forKey: "p\(i)_rom") ?? ""
            let selected = i < cards.count ? cards[i].save : store.string(forKey: "p\(i)_save") ?? ""
            if on && path.isEmpty { return (1, "Error: Player \(i + 1): Choose a Game Boy ROM or turn this Transfer Pak off.") }
            let battery = selected.isEmpty ? URL(fileURLWithPath: path).deletingPathExtension().appendingPathExtension("sav").path : selected
            arguments += [on ? path : "", on ? battery : ""]
        }
        return try runUtility(arguments)
    }
    @objc func checkSetup() {
        do { let (_, text) = try preflight(); fail(text.isEmpty ? "All selected files and save locations are ready." : text) }
        catch { fail(error.localizedDescription) }
    }
    @objc func openSettings() { NSWorkspace.shared.open(launcherDirectory) }
    @objc func openSaves() {
        let folder = URL(fileURLWithPath: save).deletingLastPathComponent()
        do { try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true); NSWorkspace.shared.open(folder) }
        catch { fail(error.localizedDescription) }
    }
    @objc func exportSave() {
        guard game == nil, editor == nil else { return fail("Close the game and save editor before exporting.") }
        do {
            let (status, text) = try runUtility(["--export-stadium-save", save])
            if status != 0 { return fail(text) }
            NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: text)])
        } catch { fail(error.localizedDescription) }
    }
    @objc func exportDiagnostics() {
        do {
            let (status, text) = try runUtility(["--launcher-diagnostics"])
            if status != 0 { return fail(text) }
            NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: text)])
        } catch { fail(error.localizedDescription) }
    }
    @objc func disableAutoplay() { store.set(false, forKey: "autoplay"); autoplay?.state = .off }
    @objc func safeRenderer() { store.useSafeRenderer(); autoplay?.state = .off; fail("Metal selected and Auto-play disabled. Restart to apply. Saves and controller mappings are kept.") }

    @objc func pickRom() {
        if let p = choosePath(save: false, current: rom, types: ["z64", "n64", "v64"], message: "Pokémon Stadium (USA) ROM") {
            rom = p
            refreshRom()
        }
    }

    @objc func pickSave() {
        if let p = choosePath(save: false, current: save, types: ["flash", "fla", "sra", "sav", "bin"],
                              message: "Import a raw 128 KiB Stadium cartridge save; the original is kept") {
            do {
                save = try importStadiumSave(executable: URL(fileURLWithPath: projectDir + "/build/pokestadium"),
                                            source: URL(fileURLWithPath: p), directory: launcherDirectory)
                refreshRom()
            } catch { fail(error.localizedDescription) }
        }
    }

    @objc func toggleAutoplay() { store.set(autoplay.state == .on, forKey: "autoplay") }

    @objc func toggleDebug() { debug = debugBox.state == .on; refreshRom() }

    // the Transfer Pak saves the editor shows next to the Stadium save (LAUNCHER_GB="save|rom;..." overrides, tests)
    func editorGB() -> [(label: String, save: String, rom: String)] {
        if let o = ProcessInfo.processInfo.environment["LAUNCHER_GB"] {
            return o.split(separator: ";").enumerated().map { i, spec in
                let p = spec.split(separator: "|", omittingEmptySubsequences: false).map(String.init)
                return ("Player \(i + 1)", p[0], p.count > 1 ? p[1] : "")
            }
        }
        return cards.filter { $0.editable }.map { ("Player \($0.index + 1)", $0.save, $0.rom) }
    }

    @objc func openEditorButton() { openEditor(gbIndex: nil) }

    // gbIndex: open on that player's cartridge
    func openEditor(gbIndex: Int?) {
        guard debug, game == nil else { return }  // never edit a save the running game may write
        let gb = editorGB()
        let section = gbIndex.flatMap { i in gb.firstIndex { $0.label == "Player \(i + 1)" } }.map { "gb\($0)" }
        if let e = editor { return e.focus(section: section) }
        guard FileManager.default.fileExists(atPath: save) else {
            return fail("The save does not exist yet:\n\(save)\n\nPlay once (or pick an existing save) first.")
        }
        let e = SaveEditor(path: save, gb: gb, section: section) { [weak self] in self?.editor = nil; self?.refreshRom() }
        do {
            try e.start()
            editor = e
        } catch {
            fail("Could not start the save editor: \(error.localizedDescription)")
        }
        refreshRom()
    }

    func applicationWillTerminate(_ notification: Notification) { editor?.stop() }

    func fail(_ text: String) {
        let a = NSAlert()
        a.messageText = "Pokémon Stadium"
        a.informativeText = text
        a.alertStyle = .warning
        a.runModal()
    }

    @objc func play() {
        if let e = editor {  // the game must not run while the editor can still write the save
            let a = NSAlert()
            a.messageText = "The save editor is open"
            a.informativeText = "Close it before playing? Changes not saved with \"Save to file\" are lost."
            a.addButton(withTitle: "Close editor and play")
            a.addButton(withTitle: "Cancel")
            guard a.runModal() == .alertFirstButtonReturn else { return }
            e.close()
        }
        let fm = FileManager.default
        let exe = projectDir + "/build/pokestadium"
        guard fm.isExecutableFile(atPath: exe) else { return fail("The game is not built yet:\n\(exe)") }
        guard fm.fileExists(atPath: rom) else { return fail("Stadium ROM not found:\n\(rom)") }
        do {
            let (status, text) = try preflight()
            if status != 0 { return fail(text) }
            if !text.isEmpty {
                let alert = NSAlert()
                alert.messageText = "Check cartridge saves"; alert.informativeText = text
                alert.addButton(withTitle: "Back"); alert.addButton(withTitle: "Play anyway")
                guard alert.runModal() == .alertSecondButtonReturn else { return }
            }
        } catch { return fail(error.localizedDescription) }
        var env = ProcessInfo.processInfo.environment
        try? fm.createDirectory(atPath: Defaults.saves, withIntermediateDirectories: true)
        try? fm.createDirectory(atPath: (save as NSString).deletingLastPathComponent, withIntermediateDirectories: true)
        env["SHIP_HOME"] = launcherDirectory.path
        env["PORT_FLASH_FILE"] = save
        let romVars = ["PORT_GB_ROM", "PORT_GB_ROM2", "PORT_GB_ROM3", "PORT_GB_ROM4"]
        let saveVars = ["PORT_GB_SAVE", "PORT_GB_SAVE2", "PORT_GB_SAVE3", "PORT_GB_SAVE4"]
        // the cards exist only when the window was shown; with auto-play the stored choices are used
        let slots: [(on: Bool, rom: String, save: String)] = (0..<4).map { i in
            if i < cards.count { return (cards[i].isOn, cards[i].rom, cards[i].save) }
            return (store.object(forKey: "p\(i)_on") as? Bool ?? false,
                    store.string(forKey: "p\(i)_rom") ?? (i == 0 ? Defaults.yellowRom : ""),
                    store.string(forKey: "p\(i)_save") ?? (i == 0 ? Defaults.yellowSave : ""))
        }
        for (i, s) in slots.enumerated() {
            env.removeValue(forKey: romVars[i])
            env.removeValue(forKey: saveVars[i])
            guard s.on, !s.rom.isEmpty else { continue }
            guard fm.fileExists(atPath: s.rom) else { return fail("Player \(i + 1): Game Boy ROM not found:\n\(s.rom)") }
            let battery = s.save.isEmpty ? URL(fileURLWithPath: s.rom).deletingPathExtension().appendingPathExtension("sav").path : s.save
            if !battery.isEmpty {
                try? fm.createDirectory(atPath: (battery as NSString).deletingLastPathComponent, withIntermediateDirectories: true)
                env[saveVars[i]] = battery
            }
            env[romVars[i]] = s.rom
        }
        let p = Process()
        p.executableURL = URL(fileURLWithPath: exe)
        p.arguments = [rom]
        p.currentDirectoryURL = URL(fileURLWithPath: projectDir + "/build")
        p.environment = env
        let logPath = Defaults.saves + "/last_run.log"
        fm.createFile(atPath: logPath, contents: nil)
        if let log = FileHandle(forWritingAtPath: logPath) {
            p.standardOutput = log
            p.standardError = log
        }
        p.terminationHandler = { proc in
            DispatchQueue.main.async {
                try? FileManager.default.removeItem(at: launcherDirectory.appendingPathComponent("frontend-session.txt"))
                if proc.terminationStatus != 0 {
                    self.game = nil
                    self.disableAutoplay()
                    self.window?.makeKeyAndOrderFront(nil)
                    NSApp.activate(ignoringOtherApps: true)
                    self.fail("The game stopped unexpectedly. Auto-play has been disabled.\nUse Files & Help to select the safe renderer or export diagnostics.\nDetails: \(logPath)")
                    return
                }
                NSApp.terminate(nil)
            }
        }
        do {
            try Data("game\n".utf8).write(to: launcherDirectory.appendingPathComponent("frontend-session.txt"), options: .atomic)
            try p.run()
            game = p
            refreshRom()
            window?.orderOut(nil) // the launcher waits in the background until the game ends
        } catch {
            try? fm.removeItem(at: launcherDirectory.appendingPathComponent("frontend-session.txt"))
            fail("Could not start the game: \(error.localizedDescription)")
        }
    }

    func windowWillClose(_ notification: Notification) {
        if game == nil { NSApp.terminate(nil) }
    }
}

@main
@MainActor
struct LauncherApp {
    static func main() {
        let app = NSApplication.shared
        let delegate = Launcher()
        app.delegate = delegate
        app.setActivationPolicy(.regular)
        withExtendedLifetime(delegate) { app.run() }
    }
}
