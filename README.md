# ✿ Vix Ultimate — The Editor That's Lowkey Obsessed With You

Hey. You just opened a terminal and something *vibed*.

Vix isn't your grandma's text editor. It's a C++ terminal editor that follows you around with AI ghost suggestions, highlights your brackets like a proud parent, and has a sidebar that actually respects your file structure. It even compiles your code so you don't have to lift a finger (okay maybe one finger. Ctrl+R).

---

## Keyboard 

| Keybind | What it does | Why you'd care |
| :--- | :--- | :--- |
| **`Ctrl + S`** | Save | Before your PC does the uno reverse card on ya |
| **`Ctrl + Q`** | Quit | Exit stage left, like a coward or a king, your call |
| **`Ctrl + H`** | Help | Opens a little window that tells you what all these keys do |
| **`F2`** | Settings | Opens the settings panel with 4 themes and config options |
| **`Ctrl + R`** | Run | Compiles & runs (C++, Python, Rust, Go, JS). Magic. |
| **`Ctrl + F`** | Search | Live search, like VS Code but in your terminal, no the air IS fine up here |
| **`F3` / `Shift+F3`** | Next / Prev match | Hop between search results like a caffeinated bunny |
| **`Ctrl + D`** | Replace | Find all, replace all, repent on your own time |
| **`Ctrl + G`** | Go To Line | Jump to line 69 like a 12-year-old |
| **`Ctrl + Z`** | Undo | Oopsie daisy |
| **`Ctrl + Y`** | Redo | Nevermind I meant to do that |

## Sidebar — We Have a File Explorer at Home

| Keybind | Action | Description |
| :--- | :--- | :--- |
| **`Ctrl + T`** | Toggle sidebar | Make it appear. Make it disappear. Your browser history could never. |
| **`Ctrl + W`** | Focus swap | Switch between editor and sidebar like an emotional pendulum |
| **`Arrows`** | Navigate | Move up, down, left, right. Yes it IS that simple. |
| **`Enter`** | Open file / folder | Enter the void (or just a folder, whatever) |
| **`a`** | New file | Spawns a file. Name it something. No pressure. |
| **`d`** | Delete file | Poof. Gone. Just like my will to comment my code. |

## AI Ghost Engine — The Spookiest Coder Since Skynet

Ever feel like someone's typing *for* you? That's Vix's ghost suggestion engine. Gray text appears in front of you like a phantom, predicting what you want to write based on ~ vibes ~ and Python witchcraft.

- **Ghost text** shows up as grayed-out code predictions
- **Press `TAB`** to accept the ghost's offering
- **Press `TAB` again** to insert 2 spaces because we're not animals
- The ghost runs on Python embedded in C++. We don't know how it works either. It just does. Don't question it.

## Sidebar Colors (The Fashion Part)

| Color | What it means |
| :--- | :--- |
| **Cyan** (Python) | Snake language. Hisses softly. |
| **Dark Blue** (C++) | The OGs. We come from the before-fore times. |
| **Orange** (C / HTML) | Grandpa C and his weird markup cousin |
| **Red** (Archives) | `.zip`, `.tar`, `.gz` — ancient artifacts |
| **Yellow** (JSON / JS) | Yellow because it's always on fire 🔥 |

## Languages Vix Actually Likes

| Language | Extensions |
| :--- | :--- |
| **C++** | `.cpp`, `.hpp`, `.cc`, `.hh` — the usual suspects |
| **Python** | `.py` — yes, even snake_case |
| **JavaScript** | `.js`, `.mjs` — laughs in callback |
| **Rust** | `.rs` — the borrow checker is watching |
| **Go** | `.go` — simple like me |

## 🛠 How to Build

Requirements: Python 3.14, ncurses, Ninja, and a can-do attitude (or coffee, whatever).

### Quick build
```bash
cmake --preset default
cmake --build --preset default
```

### Manual build
```bash
cmake -B build -G Ninja
cmake --build build -j$(nproc)
```

### Install
```bash
cmake --install build --prefix ~/.local
```
Then run `vix` from anywhere.

### Debug build
```bash
cmake --preset debug
cmake --build --preset debug
```

