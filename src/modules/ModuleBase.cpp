#include "ModuleBase.h"
#include "../ErrorHandler.h"
#include "../LumeniteApp.h"  // contains #define PKG_MNGR_NAME "LPM"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <yaml-cpp/yaml.h>
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#else
    #include <dlfcn.h>
#endif

#include <openssl/sha.h>

namespace fs = std::filesystem;

constexpr const char *ENGINE_VERSION = "1.0.0";
const fs::path PLUGIN_DIR = "plugins";
const fs::path MANIFEST_PATH = PLUGIN_DIR / "modules.cpl";

// Compute SHA-256 hash of a file
std::string sha256_file(const std::string &filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return "";

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    char buffer[8192];
    while (file.good()) {
        file.read(buffer, sizeof(buffer));
        SHA256_Update(&ctx, buffer, file.gcount());
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);

    std::ostringstream result;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        result << std::hex << std::setw(2) << std::setfill('0') << (int) hash[i];
    return result.str();
}

// Registry
std::unordered_map<std::string, std::unique_ptr<LumeniteModule> > &LumeniteModule::registry()
{
    static std::unordered_map<std::string, std::unique_ptr<LumeniteModule> > mods;
    return mods;
}

void LumeniteModule::registerModule(std::unique_ptr<LumeniteModule> mod)
{
    registry()[mod->name()] = std::move(mod);
}

int LumeniteModule::load(const char *modname, lua_State *L)
{
    if (const auto it = registry().find(modname); it != registry().end()) {
        return it->second->open(L);
    }
    return 0;
}

// Plugin Loader
void LumeniteModule::loadPluginsFromConfig(const std::string &unusedPath)
{
    if (!fs::exists(MANIFEST_PATH)) {
        std::cerr << WHITE "[" RED "!" WHITE "] " RESET
                << PKG_MNGR_NAME << ": config not found: " << MANIFEST_PATH << std::endl;
        return;
    }

    try {
        YAML::Node config = YAML::LoadFile(MANIFEST_PATH.string());
        const auto &plugins = config["plugins"];

        if (!plugins || !plugins.IsSequence()) {
            std::cerr << WHITE "[" RED "!" WHITE "] " RESET
                    << PKG_MNGR_NAME << ": invalid or missing 'plugins' list in modules.cpl" << std::endl;
            return;
        }

        for (const auto &plugin: plugins) {
            std::string name = plugin["name"].as<std::string>();
            std::string file = plugin["path"].as<std::string>();
            std::string version = plugin["version"].as<std::string>();
            std::string engineVersion = plugin["Engine-Version"].as<std::string>();
            std::string expectedHash = plugin["sha256"].as<std::string>();

            // Engine version check
            if (engineVersion != ENGINE_VERSION) {
                std::cerr << WHITE "[" RED "!" WHITE "] " RESET
                        << PKG_MNGR_NAME << ": skipping " << BOLD << name << RESET
                        << " due to engine version mismatch (" << engineVersion
                        << " != " << ENGINE_VERSION << ")" << std::endl;
                continue;
            }

            // Path validation
            if (file.find("..") != std::string::npos || fs::path(file).is_absolute()) {
                std::cerr << WHITE "[" YELLOW "-" WHITE "] " RESET
                        << PKG_MNGR_NAME << ": invalid path for " << BOLD << name << RESET << ": " << file << std::endl;
                continue;
            }

            fs::path pluginPath = PLUGIN_DIR / fs::path(file);
            if (!fs::exists(pluginPath)) {
                std::cerr << WHITE "[" RED "!" WHITE "] " RESET
                        << PKG_MNGR_NAME << ": plugin file not found: " << pluginPath << std::endl;
                continue;
            }

            // SHA-256 hash check
            std::string actualHash = sha256_file(pluginPath.string());
            if (expectedHash != actualHash) {
                std::cerr << WHITE "[" RED "!" WHITE "] " RESET
                        << PKG_MNGR_NAME << ": plugin " << BOLD << name << RESET << " failed hash check." << std::endl;
                std::cerr << "    Expected: " << expectedHash << std::endl;
                std::cerr << "    Found   : " << actualHash << std::endl;
                continue;
            }

            // Load the plugin
#ifdef _WIN32
            HMODULE lib = LoadLibraryExA(pluginPath.string().c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!lib) {
                std::cerr << WHITE "[" RED "!" WHITE "] " RESET
                        << PKG_MNGR_NAME << ": failed to load DLL: " << pluginPath << std::endl;
                continue;
            }

            using LuaOpenFunc = int(*)(lua_State *);
            LuaOpenFunc openFunc = reinterpret_cast<LuaOpenFunc>(GetProcAddress(lib, "luaopen_plugin"));
#else
            void* lib = dlopen(pluginPath.string().c_str(), RTLD_NOW);
            if (!lib) {
                std::cerr << WHITE "[" RED "!" WHITE "] " RESET
                          << PKG_MNGR_NAME << ": failed to load plugin: " << pluginPath << " (" << dlerror() << ")" << std::endl;
                continue;
            }

            using LuaOpenFunc = int(*)(lua_State*);
            LuaOpenFunc openFunc = (LuaOpenFunc)dlsym(lib, "luaopen_plugin");
#endif

            if (!openFunc) {
                std::cerr << WHITE "[" RED "!" WHITE "] " RESET
                        << PKG_MNGR_NAME << ": " << BOLD << name << RESET << " does not export luaopen_plugin" <<
                        std::endl;
#ifdef _WIN32
                FreeLibrary(lib);
#else
                dlclose(lib);
#endif
                continue;
            }

            std::cout << WHITE "[" GREEN "+" WHITE "] " RESET
                    << PKG_MNGR_NAME << ": loaded plugin: " << BOLD << name << RESET
                    << " (" << version << ")" << std::endl;
        }
    } catch (const YAML::Exception &ex) {
        std::cerr << WHITE "[" RED "!" WHITE "] " RESET
                << PKG_MNGR_NAME << ": YAML parse error: " << ex.what() << std::endl;
    }
}
