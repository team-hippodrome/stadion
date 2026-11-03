import Foundation

func portableLauncherDirectory(bundle: URL, executable: URL?, environment: [String: String]) -> URL {
    if let override = environment["SHIP_HOME"], !override.isEmpty {
        let home = environment["HOME"].flatMap { $0.isEmpty ? nil : $0 } ?? NSHomeDirectory()
        let path = override == "~" ? home : (override.hasPrefix("~/") ? (home as NSString).appendingPathComponent(String(override.dropFirst(2))) : override)
        return URL(fileURLWithPath: path).standardizedFileURL
    }
    if bundle.pathExtension.lowercased() == "app" { return bundle.deletingLastPathComponent().standardizedFileURL }
    return (executable ?? bundle).deletingLastPathComponent().standardizedFileURL
}

// The AppKit developer frontend and the shared in-game launcher use the same JSON schema and relative paths.
final class PortableLauncherStore {
    let directory: URL
    let file: URL
    private var root: [String: Any] = [:]
    private(set) var lastError: String?
    var onError: ((String) -> Void)?

    private struct Key {
        let components: [String]
        let path: Bool
        let flag: Bool
    }
    private static let keys: [String: Key] = {
        var keys = ["rom": Key(components: ["Rom"], path: true, flag: false),
                    "save": Key(components: ["Save"], path: true, flag: false),
                    "debug": Key(components: ["Debug"], path: false, flag: true),
                    "autoplay": Key(components: ["AutoPlay"], path: false, flag: true)]
        for i in 0..<4 {
            for (name, field) in [("rom", "Rom"), ("save", "Save"), ("on", "Enabled")] {
                keys["p\(i)_\(name)"] = Key(components: ["Gb\(i + 1)", field], path: name != "on", flag: name == "on")
            }
        }
        return keys
    }()

    init(directory: URL, legacy: [String: Any] = [:]) {
        let home = directory.standardizedFileURL
        self.directory = home
        self.file = home.appendingPathComponent("pokestadium.cfg.json")
        do {
            try FileManager.default.createDirectory(at: self.directory, withIntermediateDirectories: true)
            if FileManager.default.fileExists(atPath: file.path) {
                root = try read()
            } else {
                root = ["CVars": ["gLauncher": ["Rom": "", "Save": "saves/stadium.flash", "AutoPlay": 0, "Debug": 0]]]
                // Migrate only known legacy launcher preferences; unrelated UserDefaults never enter the config.
                for (name, key) in Self.keys {
                    if let value = legacy[name] { apply(value, key: key) }
                }
                try write()
            }
        } catch { failed(error) }
    }

    private func read() throws -> [String: Any] {
        guard FileManager.default.fileExists(atPath: file.path) else { return [:] }
        let value = try JSONSerialization.jsonObject(with: Data(contentsOf: file))
        guard let object = value as? [String: Any] else {
            throw NSError(domain: "LauncherConfig", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "The launcher config must be a JSON object."])
        }
        return object
    }

    private func write() throws {
        try JSONSerialization.data(withJSONObject: root, options: [.prettyPrinted, .sortedKeys]).write(to: file, options: .atomic)
    }

    private func failed(_ error: Error) {
        let message = "Could not read or save \(file.path): \(error.localizedDescription)"
        lastError = message
        onError?(message)
    }

    func object(forKey name: String) -> Any? {
        guard let key = Self.keys[name] else { return nil }
        var block = (root["CVars"] as? [String: Any])?["gLauncher"] as? [String: Any] ?? [:]
        if key.components.count == 2 { block = block[key.components[0]] as? [String: Any] ?? [:] }
        let value = block[key.components.last!]
        if key.flag, let number = value as? NSNumber { return number.boolValue }
        if key.path, let path = value as? String {
            if path.isEmpty { return "" }
            return URL(fileURLWithPath: path, relativeTo: directory.appendingPathComponent("", isDirectory: true)).standardizedFileURL.path
        }
        return value
    }

    func string(forKey name: String) -> String? { object(forKey: name) as? String }
    func bool(forKey name: String) -> Bool { object(forKey: name) as? Bool ?? false }

    private func apply(_ value: Any, key: Key) {
        var stored = value
        if key.flag, let number = value as? NSNumber { stored = number.boolValue ? 1 : 0 }
        if key.path, let path = value as? String, !path.isEmpty {
            let absolute = URL(fileURLWithPath: path, relativeTo: directory.appendingPathComponent("", isDirectory: true)).standardizedFileURL.path
            let prefix = directory.path.hasSuffix("/") ? directory.path : directory.path + "/"
            stored = absolute.hasPrefix(prefix) ? String(absolute.dropFirst(prefix.count)) : absolute
        }
        var cvars = root["CVars"] as? [String: Any] ?? [:]
        var launcher = cvars["gLauncher"] as? [String: Any] ?? [:]
        if key.components.count == 2 {
            var slot = launcher[key.components[0]] as? [String: Any] ?? [:]
            slot[key.components[1]] = stored
            launcher[key.components[0]] = slot
        } else { launcher[key.components[0]] = stored }
        cvars["gLauncher"] = launcher
        root["CVars"] = cvars
    }

    func useSafeRenderer() {
        guard lastError == nil else { return }
        do {
            root = try read()
            var window = root["Window"] as? [String: Any] ?? [:]
            window["Backend"] = ["Id": 3, "Name": "Metal"]
            root["Window"] = window
            apply(3, key: Key(components: ["Renderer"], path: false, flag: false))
            apply(false, key: Self.keys["autoplay"]!)
            try write()
        } catch { failed(error) }
    }

    func set(_ value: Any, forKey name: String) {
        guard lastError == nil, let key = Self.keys[name] else { return }
        do {
            root = try read() // Keep controller/window changes written by the game or another frontend.
            apply(value, key: key)
            try write()
        } catch { failed(error) }
    }
}

func importStadiumSave(executable: URL, source: URL, directory: URL) throws -> String {
    let process = Process()
    process.executableURL = executable
    process.arguments = ["--import-stadium-save", source.path]
    var environment = ProcessInfo.processInfo.environment
    environment["SHIP_HOME"] = directory.path
    process.environment = environment
    let output = Pipe(), errors = Pipe()
    process.standardOutput = output
    process.standardError = errors
    try process.run()
    let path = String(data: output.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
    let detail = String(data: errors.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
    process.waitUntilExit()
    guard process.terminationStatus == 0, !path.isEmpty, FileManager.default.fileExists(atPath: path) else {
        throw NSError(domain: "StadiumSaveImport", code: 1,
                      userInfo: [NSLocalizedDescriptionKey: detail.isEmpty ? "The game could not import this cartridge save." : detail])
    }
    return path
}
