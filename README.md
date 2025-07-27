![Banner](.readme/banner.png)

# Lumenite

[![Star](https://img.shields.io/github/stars/OusmBlueNinja/Lumenite?style=)](https://github.com/OusmBlueNinja/Lumenite/stargazers)
![commits](https://img.shields.io/github/commit-activity/m/OusmBlueNinja/Lumenite)
![Gitea Last Commit](https://img.shields.io/gitea/last-commit/GigabiteHosting/lumenite?gitea_url=https%3A%2F%2Fdock-it.dev%2F)
![GitHub top language](https://img.shields.io/github/languages/top/OusmBlueNinja/LUMENITE)
![Gitea Release](https://img.shields.io/gitea/v/release/GigabiteHosting/Lumenite?gitea_url=https%3A%2F%2Fdock-it.dev)


**Lumenite** is a blazing-fast, embeddable web server framework built in **C++** with a powerful **Lua scripting
interface**.
It combines native performance with the flexibility of scripting — ideal for lightweight apps, admin panels, and
embedded web tools.

![Screenshot](.readme/img.png)

---

## Features

* High-performance C++ backend
* Lua scripting for routes, templates, JSON, sessions, and more
* Modular architecture
* Built-in template engine
* Secure request parsing and production-grade cryptography

---

![img.png](https://github.com/OusmBlueNinja/Lumenite/blob/main/.readme/Gif-cli.gif?raw=true)

## Prerequisites

* CMake 3.16 or higher
* C++20-compatible compiler (GCC, Clang, or MSVC)
* Lua 5.4 (included)
* Git

---

## Build Instructions

```bash
git clone https://dock-it.dev/GigabiteHosting/Lumenite.git
cd Lumenite
mkdir build && cd build
cmake ..
make
```

The compiled binary will be located at:

```
./build/bin/lumenite
```

You can now run your own `app.lua` project using the binary.

---

## Documentation

Full documentation is available in the [Lumenite Wiki](https://dock-it.dev/GigabiteHosting/Lumenite/wiki).

---

![img.png](.readme/cli.png)
![img.png](.readme/pkgmngr.png)

## License

Creative Commons Attribution-ShareAlike 4.0 International
See [LICENSE](./LICENSE) for details.

---

## Support

If you find Lumenite useful, consider [starring the repository](https://github.com/OusmBlueNinja/Lumenite) to show your
support.
