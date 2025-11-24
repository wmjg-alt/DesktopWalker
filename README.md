# Desktop Walker 

**Bring your desktop to life.**

Desktop Walker is a lightweight, native Windows application that adds a physics-based companion to your screen. This is built in pure C++ using Win32. It uses very little RAM and CPU, sitting quietly on top of your windows without getting in the way.

## ✨ Features

- **Window Walking:** The character recognizes open windows as physical platforms. They will walk along the tops of your browser, jump between apps, and sit on your taskbar.
- **Smart Physics:** Features gravity, momentum, and an "Elevator Effect"—if you drag a window up under the character, they will be scooped up and carried with it.
- **Occlusion Aware:** The character understands depth. If a window moves *in front* of them, they might be pushed off their ledge or wake up if they were sleeping.
- **Ambient AI:**
  - **Sleeps** when bored (long cycles, wakes on disturbance).
  - **Watches Movies** with you (detects YouTube/Netflix in window titles).
  - **Jumps** to nearby ledges dynamically.
- **Multi-Monitor Support:** seamlessly walks across screens and detects boundaries.

## 🎨 Customization

The character is entirely sprite-based. You can swap in any pixel art you like.

### 1. The Assets Folder
The app looks for a folder named `assets/` next to the `.exe`. 
Images must be **Transparent PNGs**.

### 2. Naming Schema
The app looks for files named `[action]_[index].png`. You can add as many frames as you want for the walking animation; the code adapts.

**Required Files:**
- `idle_0.png`, `idle_1.png` (Standing still / breathing)
- `walk_0.png`, `walk_1.png` (Walking)
- `sit_0.png` (Sitting)
- `sleep_0.png`, `sleep_1.png` (Sleeping)
- `fall_0.png` (Falling / In Air)
- `jump_0.png` (Leaping up)
- `popcorn_0.png` (Watching a movie)

### 3. Tuning
Open `Main.cpp` and look at the `Config` namespace at the very top. You can tweak:
- Gravity & Walk Speed
- Jump Probability & Range
- Animation Speeds (in milliseconds)
- AI Personality (how often they sleep vs walk)

----

## 🛠️ For Developers: Build & Run

This project uses **Visual Studio Code** with the **MSVC (Microsoft Visual C++)** compiler.

### Prerequisites
1.  **Visual Studio Build Tools 2022** (Workload: "Desktop development with C++").
2.  **VS Code** with the C/C++ Extension.

### How to Compile w/ VSCode
1.  Open this folder in VS Code **via the Developer Command Prompt** (Start -> "Developer Command Prompt for VS 2022" -> `code .`).
2.  Create a `.vscode/tasks.json` file to tell the compiler to link Windows libraries (`User32`, `Gdiplus`, `Shcore`). 
3.  Press **Ctrl + Shift + B** to build.
4.  Press **F5** to run.

#### ** a .exe is generated **

### Utilities
Included is `pixel_converter.py`, a simple helper script. 
- **Usage:** cmd line a high-res image into the script to generate a pixel-art style sprite correctly scaled for the engine.

### Controls
- **ESC:** Instantly closes the application (Panic button).

#### TODO: 
- get rid of that command window, at least esc should close it
- test with other OS than WIN 11
- detect other movie windows like VLC etc
- jump via character's dynamic height
- detect animation length based on assets folder contents
