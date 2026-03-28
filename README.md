
# cterm

> A fast, modern, GPU-accelerated terminal emulator written in C from scratch.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg)
![Language](https://img.shields.io/badge/language-C99-orange.svg)
![Build](https://img.shields.io/badge/build-CMake-green.svg)

cterm is a production-quality terminal emulator built entirely in C using SDL2 and FreeType. It supports split panes, tabs, a built-in tool launcher, GPU-accelerated rendering, and a config file — all without depending on GTK, Qt, or any GUI framework.

---

## Features

| Feature | Description |
|---|---|
| **GPU rendering** | All text drawn via SDL2 + FreeType glyph cache |
| **Real PTY** | Runs actual bash via `openpty()` + `fork()` + `exec()` |
| **ANSI/VT100** | Full color, cursor movement, erase, insert/delete |
| **Tabs** | Multiple independent shell sessions, `Ctrl+T` to open |
| **Split panes** | Horizontal and vertical splits, `Ctrl+Shift+→/↓` |
| **Scrollback** | 5000-line history, mouse wheel to scroll |
| **Tool launcher** | `Ctrl+P` opens searchable launcher for installed tools |
| **Auto-detect tools** | Only shows tools actually installed on your system |
| **Fullscreen** | `F11` or `Alt+Enter` toggles fullscreen |
| **Config file** | `~/.config/cterm/cterm.conf` for font, colors, size |
| **Cross-platform** | Linux (PTY) + Windows (ConPTY) architecture |

---

## Screenshots

```
┌─────────────────────────────────────────────────────┐
│  bash [1]  │  bash [2]  │  htop [3]  │  +          │
├──────────────────────┬──────────────────────────────┤
│                      │                              │
│  asad@machine:~$ _   │   asad@machine:~/proj$ vim   │
│                      │                              │
│                      │                              │
└──────────────────────┴──────────────────────────────┘
```

---

## Installation

### Requirements

```bash
# Ubuntu / Debian
sudo apt install cmake libsdl2-dev libfreetype-dev libgl1-mesa-dev git

# Fedora / RHEL
sudo dnf install cmake SDL2-devel freetype-devel mesa-libGL-devel git

# Arch Linux
sudo pacman -S cmake sdl2 freetype2 git
```

### Build from source

```bash
git clone https://github.com/yourusername/cterm.git
cd cterm
mkdir build && cd build
cmake ..
make
./cterm
```

### Font (optional)

cterm auto-detects monospace fonts on your system. To use JetBrains Mono:

```bash
sudo apt install fonts-jetbrains-mono
```

Or set a custom font in `~/.config/cterm/cterm.conf`.

---

## Usage

### Keyboard shortcuts

#### Tabs
| Shortcut | Action |
|---|---|
| `Ctrl+T` | Open new tab |
| `Ctrl+W` | Close current tab |
| `Ctrl+Tab` | Next tab |
| `Ctrl+Shift+Tab` | Previous tab |
| `Ctrl+1` to `Ctrl+9` | Jump to tab N |
| Click tab | Switch to it |
| Click `x` on tab | Close tab |

#### Split panes
| Shortcut | Action |
|---|---|
| `Ctrl+Shift+→` | Split horizontally (left/right) |
| `Ctrl+Shift+↓` | Split vertically (top/bottom) |
| `Ctrl+Shift+F` | Cycle focus between panes |
| `Ctrl+Shift+W` | Close focused pane |
| Click pane | Focus it |

#### Tool launcher
| Shortcut | Action |
|---|---|
| `Ctrl+P` | Open tool launcher |
| Type | Filter tools by name |
| `↑` / `↓` | Navigate list |
| `PageUp` / `PageDown` | Scroll list |
| `Enter` | Launch selected tool |
| `Escape` | Close launcher |

#### Window
| Shortcut | Action |
|---|---|
| `F11` | Toggle fullscreen |
| `Alt+Enter` | Toggle fullscreen |
| Drag window edge | Resize |

---

## Configuration

On first run, cterm creates `~/.config/cterm/cterm.conf`:

```ini
# cterm configuration file

# Font
font_size = 16
# font_path = /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf

# Colors (R G B, each 0-255)
fg_color      = 200 200 200
bg_color      = 18 18 18
cursor_color  = 220 220 220

# Window
window_width  = 900
window_height = 560

# Terminal
scrollback = 5000
```

Edit the file and restart cterm to apply changes.

### Color themes

**Solarized Dark:**
```ini
bg_color = 0 43 54
fg_color = 131 148 150
cursor_color = 181 137 0
```

**Nord:**
```ini
bg_color = 46 52 64
fg_color = 216 222 233
cursor_color = 136 192 208
```

**Dracula:**
```ini
bg_color = 40 42 54
fg_color = 248 248 242
cursor_color = 80 250 123
```

---

## Tool Launcher

Press `Ctrl+P` to open the built-in tool launcher. It automatically detects which tools are installed on your system and shows only those. Supports 60+ tools across categories:

- **System monitors** — btop, htop, top, iotop, glances
- **Text editors** — vim, nvim, nano, micro, emacs, helix
- **Network** — nmap, curl, wget, netstat, tcpdump, mtr
- **Security** — reaver, aircrack-ng, john, hashcat, sqlmap, metasploit
- **File managers** — ranger, mc, ncdu, nnn
- **Development** — python3, node, gdb, cargo, go, make
- **Git** — lazygit, tig, gitui
- **Databases** — mysql, psql, sqlite3, redis-cli
- **Shells** — bash, zsh, fish

New tools you install appear automatically — no config needed.

---

## Architecture

```
cterm/
├── src/
│   ├── main.c       — main loop, events, rendering
│   ├── window.c     — SDL2 window + fullscreen
│   ├── font.c       — FreeType glyph cache
│   ├── pty.c        — PTY process management
│   ├── ansi.c       — ANSI/VT100 parser
│   ├── tabs.c       — tab management
│   ├── pane.c       — split pane tree
│   ├── tools.c      — tool launcher
│   └── config.c     — config file
├── include/
│   ├── window.h
│   ├── font.h
│   ├── pty.h
│   ├── ansi.h
│   ├── tabs.h
│   ├── pane.h
│   ├── tools.h
│   ├── config.h
│   └── platform.h
├── assets/
│   └── font.ttf
└── CMakeLists.txt
```

### How it works

```
SDL2 window event → main.c event loop
                         │
              ┌──────────┴──────────┐
              ↓                     ↓
         keyboard input        PTY read
              │                     │
         pty_write()       terminal_process()
              │                     │
         bash process        cell grid update
                                    │
                            render_pane_tree()
                                    │
                           SDL2 GPU draw calls
                                    │
                              screen output
```

---

## Security

cterm follows secure coding practices:

- **No privilege escalation** — runs entirely as the user, never needs root
- **Input sanitization** — all PTY input is passed through as-is (bash handles it)
- **Memory safety** — all allocations checked, all freed in reverse order
- **No network access** — cterm itself makes zero network connections
- **OSC sanitization** — OSC/DCS sequences are consumed silently, never executed
- **Process isolation** — each tab/pane runs in an independent process group

See [SECURITY.md](SECURITY.md) for the full security policy and reporting vulnerabilities.

---

## Building for Windows

```bash
# In MSYS2 MinGW64 terminal
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-freetype mingw-w64-x86_64-cmake

mkdir build-windows && cd build-windows
cmake .. -G "MinGW Makefiles"
mingw32-make
./cterm.exe
```

Windows uses ConPTY (requires Windows 10 1809+) instead of POSIX PTY.

---

## Contributing

Contributions are welcome. Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Make your changes with clear commit messages
4. Ensure the build passes with zero warnings (`-Wall -Wextra`)
5. Open a pull request with a description of what you changed and why

### Areas that need work

- [ ] Full 256-color palette support
- [ ] Unicode / UTF-8 character rendering
- [ ] Mouse reporting (for vim mouse mode)
- [ ] Alternate screen buffer (for full-screen apps)
- [ ] Selection and clipboard copy/paste
- [ ] Search in scrollback buffer
- [ ] Plugin/extension API

---

## License

MIT License — see [LICENSE](LICENSE) for details.

---

## Author

**Muhammed Asad**
- GitHub: [@yourusername](https://github.com/muhammedasadn)
- Website: [your-cterm-website.com](https://your-cterm-website.com)

---

## Acknowledgements

Built with:
- [SDL2](https://www.libsdl.org/) — window management and GPU rendering
- [FreeType](https://freetype.org/) — font rasterization
- POSIX PTY API — `openpty()`, `fork()`, `execl()`
- Inspired by [Terminator](https://gnome-terminator.org/), [GNOME Terminal](https://gitlab.gnome.org/GNOME/gnome-terminal), and [Alacritty](https://alacritty.org/)
