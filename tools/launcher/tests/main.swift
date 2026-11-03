import Foundation

func require(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() { fatalError(message) }
}

let fm = FileManager.default
let root = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try fm.createDirectory(at: root, withIntermediateDirectories: true)

let fresh = PortableLauncherStore(directory: root.appendingPathComponent("fresh"))
require(fresh.lastError == nil && fm.fileExists(atPath: fresh.file.path), "Fresh config not generated")

let home = root.appendingPathComponent("original", isDirectory: true)
let migrated = PortableLauncherStore(directory: home, legacy: ["rom": home.appendingPathComponent("game.z64").path,
    "save": home.appendingPathComponent("saves/stadium.flash").path, "p0_on": true, "debug": true,
    "unrelated-private-preference": "must not be copied"])
require(migrated.lastError == nil && migrated.bool(forKey: "p0_on"), "Legacy flags lost")
let migratedData = try Data(contentsOf: migrated.file)
require(!String(decoding: migratedData, as: UTF8.self).contains("unrelated-private"), "Unrelated defaults migrated")
var config = try JSONSerialization.jsonObject(with: migratedData) as! [String: Any]
let launcher = (config["CVars"] as! [String: Any])["gLauncher"] as! [String: Any]
require(launcher["Rom"] as? String == "game.z64", "ROM path is not portable")
require(launcher["Save"] as? String == "saves/stadium.flash", "Save path is not portable")
let jsonText = String(decoding: migratedData, as: UTF8.self)
require(jsonText.range(of: #""Debug"\s*:\s*1"#, options: .regularExpression) != nil,
        "Shared native config expects integer flags, not JSON booleans")

let moved = root.appendingPathComponent("moved folder", isDirectory: true)
try fm.copyItem(at: home, to: moved)
let relocated = PortableLauncherStore(directory: moved)
require(relocated.string(forKey: "save") == moved.appendingPathComponent("saves/stadium.flash").path,
        "Save did not follow a relocated launcher")
relocated.set(moved.appendingPathComponent("saves/포켓몬.flash").path, forKey: "save")
require(relocated.string(forKey: "save") == moved.appendingPathComponent("saves/포켓몬.flash").path,
        "Unicode paths did not round-trip")

config = try JSONSerialization.jsonObject(with: Data(contentsOf: relocated.file)) as! [String: Any]
config["Window"] = ["Backend": ["Id": 4, "Name": "Vulkan"]]
var cvars = config["CVars"] as! [String: Any]
cvars["gControllers"] = ["CustomBinding": "retained"]
config["CVars"] = cvars
try JSONSerialization.data(withJSONObject: config).write(to: relocated.file)
relocated.set(false, forKey: "debug")
let merged = try JSONSerialization.jsonObject(with: Data(contentsOf: relocated.file)) as! [String: Any]
require((merged["Window"] as? [String: Any]) != nil, "Window settings were overwritten")
require(((merged["CVars"] as? [String: Any])?["gControllers"] as? [String: Any]) != nil,
        "Game-written controls were overwritten")
require(!relocated.bool(forKey: "debug"), "Boolean setting not persisted")

relocated.useSafeRenderer()
let safe = try JSONSerialization.jsonObject(with: Data(contentsOf: relocated.file)) as! [String: Any]
require(((safe["Window"] as? [String: Any])?["Backend"] as? [String: Any])?["Id"] as? Int == 3,
        "Safe recovery did not select Metal")
require(!relocated.bool(forKey: "autoplay"), "Safe recovery kept Auto-play enabled")
require(((safe["CVars"] as? [String: Any])?["gControllers"] as? [String: Any]) != nil,
        "Recovery erased controller mappings")
require(relocated.string(forKey: "save") == moved.appendingPathComponent("saves/포켓몬.flash").path,
        "Recovery changed selected save")

let broken = root.appendingPathComponent("broken", isDirectory: true)
try fm.createDirectory(at: broken, withIntermediateDirectories: true)
let badFile = broken.appendingPathComponent("pokestadium.cfg.json")
let original = Data("invalid JSON".utf8)
try original.write(to: badFile)
let invalid = PortableLauncherStore(directory: broken)
invalid.set(true, forKey: "debug")
let unchanged = try Data(contentsOf: badFile)
require(invalid.lastError != nil && unchanged == original, "Invalid config was silently overwritten")

require(portableLauncherDirectory(bundle: root.appendingPathComponent("Game.app"), executable: nil, environment: [:]) == root,
        "Bundle config would be written inside the signed app")
require(portableLauncherDirectory(bundle: root.appendingPathComponent("Game.app"), executable: nil,
                                 environment: ["SHIP_HOME": moved.path]) == moved,
        "Explicit SHIP_HOME was ignored")
print("Swift launcher config checks passed")
