# Other consoles

Warthog is Chupathingy's console line. This repository is the original
Xbox's. Each other console has a repository of its own, with the game as a
submodule, the same way:

| Console | Repository |
|---|---|
| Original Xbox | this one (`port/xbox`, `ninja xbox`) |
| Xbox 360 | [ChupathingyCE/warthog-360](https://github.com/ChupathingyCE/warthog-360) |
| Wii U | [ChupathingyCE/warthog-wiiu](https://github.com/ChupathingyCE/warthog-wiiu) |
| Nintendo Switch | [ChupathingyCE/warthog-switch](https://github.com/ChupathingyCE/warthog-switch) |
| PlayStation 3 | [ChupathingyCE/warthog-ps3](https://github.com/ChupathingyCE/warthog-ps3) |

Each port is a `--console-port` of the game's `configure.py`
(`python3 chupathingyce/configure.py --console-port port/xbox ...`;
docs/building.md). A target exists only when its toolchain is found.

Every console keeps the network protocol of the PC builds (network version
and layout). A console adapts to it, never the other way (the game's
docs/cross-play.md). Code that more than one console needs goes in the
game behind a console-neutral switch (`HALO_CONSOLE_*`), not an Xbox one.

## Shared console code (the game's `port/console`)

In the game (the submodule):

- `port/console/include`: the console's name and type, controller input,
  byte swaps and readers that are safe at any alignment, and the tag and
  cache byte swaps the big-endian consoles use.
- `port/console/https`: the HTTPS client the consoles share (the game list,
  the signed legacy table, map lists), with the site's keys pinned.
- `port/console/link`: Link Profile, with the console's own key.
- `port/console/reports`: reports of joined games, for service records.

## Packages

A console's package holds the game only, never the maps: a package is one
installable file, easily passed around, and the maps are the user's own.
They stay a folder beside it.

## Testing

| Console | Emulator | Hardware |
|---|---|---|
| Original Xbox | xemu (128 MB, NAT) | devkit, or a modified retail console |
