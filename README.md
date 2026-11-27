# stadion
stadion is a native PC and Android port of an N64 game, built on a fork of [libultraship](https://github.com/team-hippodrome/libultraship) and our decomp [pokestadium](https://github.com/team-hippodrome/pokestadium). The game is now available to be played on:
- Windows (x64 and ARM64)
- macOS (Apple Silicon)
- Linux (x86_64 AppImage)
- Android (ARM64, Android 10 or newer)

To download the game, please see the [latest release](https://github.com/team-hippodrome/stadion/releases/latest).

> **Note:** You must supply your own big-endian `.z64` ROM (32 MiB): USA 1.0, Rev 1 or Rev 2, Europe (English 1.0 or Rev 1), France, Germany, Spain or Italy. See the [supported ROM checksums](docs/USAGE.md#1-verify-your-rom). Transfer Pak and Game Boy Tower features also need your own Game Boy ROMs and saves.

- [**Join our Discord server**](https://discord.com/invite/8EvuQxbxu8)
- [**Report an issue**](https://github.com/team-hippodrome/stadion/issues/new/choose)
- [**Building Instructions**](docs/BUILDING.md)
- [**Setup, settings and troubleshooting**](docs/USAGE.md)

## Features
- Full Transfer Pak support with your own Game Boy ROMs and saves.
- Built-in Game Boy Tower.
- Local multiplayer for up to 4 players, with keyboard and controller mapping.
- Widescreen support.
- High internal resolutions, up to 2160p or your window's full size.
- Anti-aliasing.
- Frame interpolation for smooth motion.
- DirectX 11, OpenGL, Metal and Vulkan renderers.
- A launcher that checks your ROM and save files, imports existing N64 saves and backs them up automatically.

## Acknowledgments
- The [decompilation contributors](https://github.com/michiiik/pokestadium).
- The [libultraship contributors](https://github.com/Kenix3/libultraship).
- [Harbour Masters](https://github.com/HarbourMasters) for the porting framework and tooling used by this project.

---

Part of the [R.A.I.D. (Retro AI Development) Community](https://discord.gg/tCpZzcFwv8)
