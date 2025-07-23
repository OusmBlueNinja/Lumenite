# ⚡ Lumenite

[![Star](https://img.shields.io/github/stars/OusmBlueNinja/Lumenite?style=social)](https://github.com/OusmBlueNinja/Lumenite/stargazers)

**Lumenite** is a blazing-fast, embeddable web server framework built in **C++** with a powerful **Lua scripting
interface**.  
It combines native performance with the flexibility of scripting — ideal for lightweight apps, panels, and embedded web
tooling.

---

## 📦 Features

- 🚀 High-performance C++ backend
- 🧠 Lua scripting with routes, templates, JSON, sessions, and more
- 🧩 Modular architecture
- 🎨 Built-in templating engine
- ⚙️ Built-in scaffold tool to generate starter projects
- 🛡️ Production-grade cryptography and secure request parsing

---

## 🚀 Getting Started

## Quick Start (Linux)

```bash
git clone https://dock-it.dev/GigabiteHosting/Lumenite.git
cd Lumenite
mkdir build && cd build
cmake ..
make
mkdir Project
cd Project
../build/bin/lumenite --init Project
../build/bin/lumenite
````

### Prerequisites

- CMake 3.16+
- C++20 compiler (GCC, Clang, MSVC)
- Lua 5.4 (included)
- Git

### Clone & Build

```bash
git clone https://dock-it.dev/GigabiteHosting/Lumenite.git
cd Lumenite
mkdir build && cd build
cmake ..
make
````

This produces a `./lumenite` binary in `build/bin`.

---

## 📂 Scaffold

Lumenite includes a built-in scaffolding tool to quickly create a new web app layout:

```bash
./build/bin/lumenite --new MyApp
```

This will generate the following structure in your current directory:

```
<cwd>/
├── app.lua                   # Main Lua entrypoint
├── templates/
│   └── index.html            # Example template
├── types/
│   └── __syntax__.lua        # Optional for IDE hinting
├── .vscode/                  # VSCode config (optional)
├── .gitignore                # Standard ignores
```

The generated `app.lua` will contain a minimal starter app.

Run your app:

```bash
./build/bin/lumenite
```

Visit [http://localhost:8080](http://localhost:8080)

---

## 📚 Documentation

📖 View the full docs on the [Lumenite Wiki »](https://dock-it.dev/GigabiteHosting/Lumenite/wiki)

---

## ⭐️ Support the Project

If you like Lumenite, please consider [starring the repository](https://github.com/OusmBlueNinja/Lumenite) to show your
support!

---

## 📜 License

Creative Commons Attribution-ShareAlike 4.0 International — see [LICENSE](./LICENSE)


