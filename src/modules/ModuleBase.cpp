#include "ModuleBase.h"
#include "../ErrorHandler.h"
#include <iostream>
#include <filesystem>


#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#define ENGINE_VERSION "2025.5"
#define PKG_MNGR_NAME "LPM"

namespace fs = std::filesystem;
static const fs::path PLUGIN_DIR = "plugins";

inline void logLPM(const std::string &symbol, const std::string &message, const char *color = WHITE)
{
    std::cerr << WHITE "[" << color << symbol << WHITE "] "
            << CYAN << "LPM" << WHITE ": " << RESET
            << message << std::endl;
}

static void logLPM_hint(const std::string &hint)
{
    std::cerr << "         " << CYAN << "-> " << hint << RESET << std::endl;
}

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
    const auto it = registry().find(modname);
    if (it != registry().end()) {
        auto *fn = it->second->getLuaOpen();
        if (fn) {
            lua_pushcfunction(L, fn);
            return 1;
        }
    }
    return 0;
}

class LumeniteDynamicModule final : public LumeniteModule
{
public:
    LumeniteDynamicModule(std::string name, std::string version, int (*open)(lua_State *))
        : _name(std::move(name)), _version(std::move(version)), _luaopen(open)
    {
    }

    const std::string &name() const override { return _name; }
    int open(lua_State *L) override { return _luaopen(L); }
    int (*getLuaOpen() const)(lua_State *) override { return _luaopen; }

private:
    std::string _name;
    std::string _version;

    int (*_luaopen)(lua_State *);
};

void LumeniteModule::loadPluginsFromDirectory()
{
    if (!fs::exists(PLUGIN_DIR)) {
        logLPM("!", "Plugin directory not found: " + PLUGIN_DIR.string());
        return;
    }

    for (const auto &subdir: fs::directory_iterator(PLUGIN_DIR)) {
        if (!subdir.is_directory())
            continue;

        std::string folderName = subdir.path().filename().string();
        std::string expectedDllName = "lumenite_" + folderName;

#ifdef _WIN32
        expectedDllName += ".dll";
#else
        expectedDllName += ".so";
#endif

        fs::path dllPath = subdir.path() / expectedDllName;
        if (!fs::exists(dllPath) || !fs::is_regular_file(dllPath))
            continue;

        std::string pluginName = folderName;

#ifdef _WIN32
        std::wstring widePath = fs::absolute(dllPath).wstring();
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
        AddDllDirectory(fs::absolute(PLUGIN_DIR).wstring().c_str());

        HMODULE lib = LoadLibraryExW(widePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!lib) {
            DWORD errCode = GetLastError();
            LPSTR msg = nullptr;
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, errCode, 0,
                           (LPSTR) &msg, 0, nullptr);
            logLPM("!", "Failed to load plugin " + pluginName + ":\n         " + (msg ? msg : "Unknown error"));
            if (msg) LocalFree(msg);
            continue;
        }

        auto getMeta = reinterpret_cast<const LumenitePluginMeta *(*)()>(
            GetProcAddress(lib, "lumenite_get_pmeta"));
#else
        void *lib = dlopen(dllPath.c_str(), RTLD_NOW);
        if (!lib) {
            logLPM("!", "Failed to load plugin " + pluginName + ": " + dlerror());
            continue;
        }

        auto getMeta = reinterpret_cast<const LumenitePluginMeta *(*)()>(
            dlsym(lib, "lumenite_get_pmeta"));
#endif

        if (!getMeta) {
            logLPM("!", "Plugin " + pluginName + " is missing required export lumenite_get_pmeta()");
#ifdef _WIN32
            FreeLibrary(lib);
#else
            dlclose(lib);
#endif
            continue;
        }

        const LumenitePluginMeta *meta = getMeta();
        if (!meta || !meta->name || !meta->version || !meta->luaopen) {
            logLPM("!", "Plugin " + pluginName + " has invalid or incomplete metadata.");
#ifdef _WIN32
            FreeLibrary(lib);
#else
            dlclose(lib);
#endif
            continue;
        }

        if (std::string(meta->engine_version) != ENGINE_VERSION) {
            logLPM("-", "Skipping plugin " + std::string(meta->name) + ": engine version mismatch (" +
                        meta->engine_version + " != " + ENGINE_VERSION + ")", YELLOW);
#ifdef _WIN32
            FreeLibrary(lib);
#else
            dlclose(lib);
#endif
            continue;
        }

        std::string safeName = meta->name;
        std::string safeVersion = meta->version;
        if (!safeName.empty() && safeName.front() == '"' && safeName.back() == '"')
            safeName = safeName.substr(1, safeName.size() - 2);
        if (!safeVersion.empty() && safeVersion.front() == '"' && safeVersion.back() == '"')
            safeVersion = safeVersion.substr(1, safeVersion.size() - 2);

        registerModule(std::make_unique<LumeniteDynamicModule>(safeName, safeVersion, meta->luaopen));
        logLPM("+", "Plugin \"" + safeName + "\" loaded successfully  [" + safeVersion + "]", GREEN);
    }
}
