# fetch

A donut.c-inspired fetch tool that spins your distro logo in 3D with live-updating system info.

![demo](demo.gif)

Takes any ASCII/Unicode distro logo, turns each character into a point cloud
based on its visual density, and renders it as a rotating 3D relief with
Blinn-Phong shading. System info is gathered natively with no external
dependencies.

Based on [gentoo.c](https://github.com/areofyl/gentoo.c).

---

## Recommended Windows Usage Flow

```text
Clone repository
      ↓
Build with CMake
      ↓
Run fetch.exe
      ↓
(Optional) put fetch.exe in a directory on PATH
      ↓
fetch
      ↓
(Optional) customize:
%APPDATA%\fetch\config
%APPDATA%\fetch\logo.txt
```

---

## Build

### Windows

#### Verified Local Toolchain
- **OS**: Windows 11 x86_64 (VERIFIED)
- **Compiler**: MinGW-w64 (VERIFIED)
- **Build System**: CMake (VERIFIED)

#### CMake (Recommended)

Generate the build configuration and compile:

```powershell
cmake -S . -B build
cmake --build build
```

For a Release configuration:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The compiled binary is generated at:

```powershell
.\build\fetch.exe
```

Run the test suite with CTest:

```powershell
ctest --test-dir build --output-on-failure
```

To build without tests:

```powershell
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build
```

#### Makefile (Alternative)

Build using `mingw32-make`:

```powershell
mingw32-make
.\fetch.exe
```

Run tests with `mingw32-make`:

```powershell
mingw32-make test
mingw32-make smoke
```

---

### POSIX (Linux / macOS)

Build with CMake:

```sh
cmake -S . -B build
cmake --build build
./build/fetch
```

Build with Makefile:

```sh
make
./fetch
```

*(Note: The POSIX platform backend and build targets are implemented, but native Linux/macOS execution is UNVERIFIED on the current development host).*

---

## Running `fetch`

### Direct Execution

Run `fetch` directly from the build directory:

```powershell
.\build\fetch.exe
```

Common command-line invocations:

```powershell
fetch --help
fetch --version
fetch --frames 5
fetch --no-info --frames 1
```

Interaction controls:
- **Exit**: Press any key to stop. The keypress passes through to the calling shell so you can continue typing immediately. `Ctrl-C` also exits cleanly.
- **Rotate**: Click and drag the logo with the mouse to manually rotate it in 3D; release the mouse button to fling it spinning with momentum.

---

## Running `fetch` From Anywhere

To run:

```powershell
fetch
```

from any PowerShell, CMD, or Windows Terminal session without prefixing the path, place `fetch.exe` in a directory of your choice and add that directory to your user `PATH`.

### 1. Place `fetch.exe` in a Directory

Example directory structure:

```text
C:\Tools\fetch
└── fetch.exe
```

### 2. Add Directory to User PATH via PowerShell

Use this safe PowerShell snippet to append the directory to your **User PATH** without overwriting existing entries:

```powershell
$fetchDir = "C:\Tools\fetch"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ([string]::IsNullOrWhiteSpace($userPath)) {
    [Environment]::SetEnvironmentVariable("Path", $fetchDir, "User")
} elseif (($userPath -split ';') -notcontains $fetchDir) {
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$fetchDir", "User")
}
```

This ensures that existing User and System PATH entries are completely preserved, and `$fetchDir` is only appended if it is not already present.

> **Note**: A new terminal session may be required after changing `PATH` for the updated environment variables to load.

### 3. Verify PATH Setup

Verify that the executable is discovered:

```powershell
where.exe fetch
```

or in PowerShell:

```powershell
Get-Command fetch
```

Once verified, simply type:

```powershell
fetch
```

from any working directory.

---

## Windows Configuration

### Configuration Paths

The primary native Windows configuration path is:

```text
%APPDATA%\fetch\config
```

Typically:

```text
C:\Users\<username>\AppData\Roaming\fetch\config
```

The primary native Windows custom logo path is:

```text
%APPDATA%\fetch\logo.txt
```

Typically:

```text
C:\Users\<username>\AppData\Roaming\fetch\logo.txt
```

You can create the directory in PowerShell with:

```powershell
mkdir "$env:APPDATA\fetch"
```

Edit the configuration file:

```powershell
notepad "$env:APPDATA\fetch\config"
```

Edit the custom logo file:

```powershell
notepad "$env:APPDATA\fetch\logo.txt"
```

---

## Configuration Fallback Behavior

`fetch` resolves configuration and custom logo files by checking paths in order. The first existing regular file is used.

### Configuration Lookup Order

```text
%APPDATA%\fetch\config
        ↓
%USERPROFILE%\.config\fetch\config
        ↓
%HOME%\.config\fetch\config
```

### Custom Logo Lookup Order

```text
%APPDATA%\fetch\logo.txt
        ↓
%USERPROFILE%\.config\fetch\logo.txt
        ↓
%HOME%\.config\fetch\logo.txt
```

`%APPDATA%\fetch\` is the recommended and native Windows location. Using `.config` is supported as a cross-platform fallback, but Windows users do not need to create or use `.config`.

---

## Configuration Format

The configuration file uses the project's line-oriented `key=value` format (it is **not** JSON or JSONC).

- List field names to show (one per line) in the order you want them displayed.
- Comment out (with `#`) or omit fields to hide them.
- Visual and 3D properties are set via `key=value`.

Example `%APPDATA%\fetch\config`:

```ini
# Fields to display (in order)
os
host
kernel
uptime
packages
shell
display
wm
cpu
gpu
memory
swap
disk
ip
battery
colors

# separators (blank line between groups)
# ---

# custom static fields
# custom_Pronouns=he/him
# custom_Website=example.com

# Settings
speed=1.0
size=1.0
depth=1.0
height=0
shading_mode=ascii
box=false
```

Command-line flags (such as `--speed`, `--size`, `--depth`, `--box`, `--shading-mode`) take precedence over settings defined in the configuration file.

---

## Custom Logo

Full reference: [docs/custom-logos.md](docs/custom-logos.md)

### Windows Built-in Logo & Aliases

On Windows, `fetch` includes a built-in Windows ASCII logo. You can explicitly request it with:

```powershell
fetch -l windows
```

The following aliases are recognized for the built-in Windows logo:
- `windows`
- `win`
- `win10`
- `win11`

When run on Windows without `--logo`, `fetch` automatically uses the built-in Windows logo (or fastfetch logo if fastfetch is installed).

If an explicit, unrelated logo is requested (e.g. `fetch -l arch`), `fetch` attempts to load that logo from fastfetch or matching built-in distro art; explicitly requested logos do not silently fall back to the Windows logo.

### Custom Logo File

Primary Windows path:

```text
%APPDATA%\fetch\logo.txt
```

If `%APPDATA%\fetch\logo.txt` does not exist, `fetch` checks the fallback paths (`%USERPROFILE%\.config\fetch\logo.txt` and `%HOME%\.config\fetch\logo.txt`).

Example logo file with distro color scheme:

```text
# distro: windows
        ,.=:!!t3Z3z.,
       :!=eeeeeeNNNEeeTick.
```

---

## Windows System Information

All system metrics are gathered natively with zero external dependencies and zero external process spawning (`popen`) inside the 20 FPS animation loop:

- **OS** – Windows product name, display version, and build number via Registry
- **Host** – Hardware manufacturer and model via BIOS Registry
- **Kernel** – Windows NT build version
- **Uptime** – System uptime via high-resolution tick counter
- **CPU** – Processor model, clock speed, and core/thread count
- **GPU** – GPU adapter model, vendor, and dedicated video memory (VRAM) via DXGI
- **Memory** – Total and currently used physical RAM via `GlobalMemoryStatusEx`
- **Swap** – Pagefile size and utilization
- **Disk** – Drive space and mount paths via volume APIs (supports additional drives via `disk=D:\` in config)
- **IP / Network** – Active IPv4 and IPv6 network adapters via IP Helper API
- **Battery** – Charge percentage and charging status via `GetSystemPowerStatus`
- **Display** – Active display resolution and refresh rate
- **Shell** – Parent shell process detection (PowerShell, pwsh, CMD) via process snapshot
- **Terminal** – Terminal host detection (Windows Terminal `wt.exe`, ConHost, etc.)
- **Theme / Font / Cursor** – System dark/light theme mode, console font name, and cursor configuration
- **Package Managers** – Installed package counts for detected Windows package managers (winget, Scoop, Chocolatey reported as separate, manager-specific counts; not combined into a single total)

Static metrics are cached once on startup. Dynamic metrics (`uptime`, `memory`, `swap`) refresh in-place every ~1 second (20 frames) during animation.

---

## Package Managers

`fetch` automatically detects and counts installed packages for supported Windows package managers:

- **winget**
- **Scoop**
- **Chocolatey**

Counts are strictly **manager-specific** and are displayed individually (e.g. `winget: 45, Scoop: 12, Chocolatey: 3`), rather than summed or combined into a single total. Only installed and detected package managers are shown.

---

## Terminal Support

### Verification Status by Environment
- **Windows Terminal**: **VERIFIED**
- **ConHost** (PowerShell / CMD classic console): **VERIFIED**
- **Linux / macOS terminal environments**: **UNVERIFIED** on the current development host

Only Windows Terminal and ConHost are verified on the development host.

### Supported Behavior
- **Virtual Terminal Processing**: Automatically enables VT100/ANSI processing and UTF-8 console output (`CP_UTF8`).
- **Terminal Resize**: Listens for window size changes and dynamically recalculates 3D canvas and layout.
- **Keyboard Input Passthrough**: Pressing any key terminates animation immediately without consuming the character from the console buffer, allowing instant shell interaction on startup.
- **Signal-Safe Interruption**: `Ctrl-C` is handled via a native console control handler to immediately and safely exit the render loop.
- **Terminal Cleanup & Restoration**: Original console modes, cursor visibility, and code pages are reliably restored on exit.
- **Interactive Mouse Tracking**: Click and drag on the logo to rotate in 3D; releasing flings the model with rotational inertia.

---

## Supported Platforms & Verification Status

```text
Windows 11 x86_64: VERIFIED
MinGW-w64: VERIFIED
Windows Terminal: VERIFIED
ConHost: VERIFIED

Linux: UNVERIFIED on the current development host
macOS: UNVERIFIED on the current development host
```

The POSIX platform backend (`src/platform/platform_posix.c`) and build rules are present in the repository, but native Linux and macOS builds have not been verified on this development environment.

---

## Options

| Flag | Description |
|------|-------------|
| `-l`, `--logo <name>` | Use a logo from fastfetch or built-in (`windows`, `win`, `win10`, `win11`, `gentoo`) |
| `--rotate-x` | Lock rotation to X axis only |
| `--rotate-y` | Lock rotation to Y axis only |
| `-s`, `--speed <float>` | Speed multiplier (default 1.0) |
| `--size <float>` | Scale the logo (e.g. 2.0 for double size) |
| `--depth <float>` | Scale the 3D depth (default 1.0) |
| `--height <n>` | Override render height in rows |
| `--box` | Draw a border box around the info block |
| `--no-info` | Just the logo, no system info |
| `--no-color` | Disable coloring |
| `--frames <n>` | Stop after n frames (default 2000) |
| `--infinite` | Run forever (until keypress or Ctrl-C) |
| `--shading-mode <mode>` | `ascii` (default), or opt into sub-cell blocks with `sextants` (2x3) or `blocks` (2x2) |
| `--shading-chars <str>` | Custom shading ramp, supports UTF-8 |
| `-h`, `--help` | Show help |
| `-V`, `--version` | Show version |

CLI flags override configuration file settings.

---

## Shading modes

Full reference: [docs/shading-modes.md](docs/shading-modes.md)

ASCII is the default. The sub-cell modes are opt-in, and trade the donut.c look
for a silhouette that lands on a fraction of a cell instead of snapping to the
character grid.

| `ascii` (default) | `blocks` | `sextants` |
|:---:|:---:|:---:|
| ![ascii](docs/shading-ascii.png) | ![blocks](docs/shading-blocks.png) | ![sextants](docs/shading-sextants.png) |
| brightness mapped onto `.,-~:;=!*#$@`, one character per cell | coverage sampled 2×2, edges on quadrants | coverage sampled 2×3, edges on block sextants |

`sextants` needs a terminal that draws the Symbols for Legacy Computing block;
`blocks` works anywhere with a UTF-8 locale.

---

## How it works

For a deep dive with visuals and code, see the [full blog post](https://asdesai.com/blog/how-fetch-works/).

1. **Logo loading** – reads ASCII/Unicode art from `%APPDATA%\fetch\logo.txt` (Windows) or `~/.config/fetch/logo.txt` (POSIX), built-in logos, or fastfetch. ANSI color codes are parsed and preserved per-character.

2. **Heightmap** – each character gets a weight based on visual density (`@` is heavy, `.` is light, `█` is full, `░` is thin). The weight becomes a Z height, turning the flat logo into a 3D relief map.

3. **Point cloud** – the heightmap is sampled into 3D points. Interior cells get multiple Z layers for a solid extrusion, edge cells get only front and back faces to keep outlines clean.

4. **Surface normals** – computed from the height gradient at each cell using finite differences, giving each point a direction for lighting.

5. **Rotation + projection** – every frame, all points are rotated around X/Y axes, then perspective-projected onto the terminal grid with a z-buffer to handle occlusion.

6. **Shading** – Blinn-Phong lighting (diffuse + specular) gives every visible point a brightness, which maps onto the `.,-~:;=!*#$@` ramp, one character per cell. `--shading-mode sextants` or `blocks` samples coverage finer than the character cell (2×3 or 2×2).

7. **Rendering** – the entire frame is written in a single atomic buffer write to avoid flicker. System info is displayed alongside the animation and fast-changing fields (uptime, memory, swap) update live every second.
