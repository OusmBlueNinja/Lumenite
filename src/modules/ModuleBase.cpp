#include "ModuleBase.h"
#include "../ErrorHandler.h"
#include <iostream>
#include <filesystem>


#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif


#include <vector>
#include <string>
#include <fstream>


#define ENGINE_VERSION "2025.5"
#define PKG_MNGR_NAME "LPM"

namespace fs = std::filesystem;
static const fs::path PLUGIN_DIR = "plugins";

#ifdef _WIN32

std::vector<std::string> get_imported_dlls(const std::wstring &dll_path)
{
    std::vector<std::string> dlls;

    std::ifstream file(dll_path.c_str(), std::ios::binary);
    if (!file) {
        std::wcerr << L"[X] Could not open file: " << dll_path << std::endl;
        return dlls;
    }

    file.seekg(0, std::ios::end);
    const std::streamsize size = file.tellg();
    if (size < sizeof(IMAGE_DOS_HEADER)) {
        std::wcerr << L"[X] File too small to be a valid DLL." << std::endl;
        return dlls;
    }
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(static_cast<size_t>(size));
    file.read(buffer.data(), size);

    auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(buffer.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        std::wcerr << L"[X] Not a valid PE file (missing MZ)." << std::endl;
        return dlls;
    }

    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(buffer.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        std::wcerr << L"[X] Not a valid NT header (missing PE)." << std::endl;
        return dlls;
    }

    const DWORD import_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!import_rva) {
        std::wcout << L"[~] No import table found." << std::endl;
        return dlls;
    }

    auto section = IMAGE_FIRST_SECTION(nt);
    DWORD import_offset = 0;

    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        const DWORD sec_va = section->VirtualAddress;
        if (const DWORD sec_size = section->Misc.VirtualSize; import_rva >= sec_va && import_rva < sec_va + sec_size) {
            import_offset = section->PointerToRawData + (import_rva - sec_va);
            break;
        }
    }

    if (!import_offset) {
        std::wcerr << L"[X] Could not map import directory to file offset." << std::endl;
        return dlls;
    }

    auto imp_desc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(buffer.data() + import_offset);
    while (imp_desc->Name) {
        const DWORD name_rva = imp_desc->Name;

        std::string dll_name;
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            section = IMAGE_FIRST_SECTION(nt) + i;
            const DWORD sec_va = section->VirtualAddress;
            if (const DWORD sec_size = section->Misc.VirtualSize; name_rva >= sec_va && name_rva < sec_va + sec_size) {
                const DWORD name_offset = section->PointerToRawData + (name_rva - sec_va);
                dll_name = std::string(buffer.data() + name_offset);
                break;
            }
        }

        if (!dll_name.empty())
            dlls.push_back(dll_name);

        ++imp_desc;
    }

    return dlls;
}

std::vector<std::string> check_dependencies(const std::wstring &dll_path)
{
    std::vector<std::string> deps;

    for (const auto dep = get_imported_dlls(dll_path); const auto &it: dep) {
        if (const HMODULE h = LoadLibraryA(it.c_str())) {
            FreeLibrary(h);
        } else {
            deps.push_back(it);
        }
    }
    return deps;
}
#else
#message Windows only
std::vector<std::string> check_dependencies(const std::wstring &dll_path)
{
    std::vector<std::string> deps;
    return deps;
}
#endif

inline void logLPM(const std::string &symbol, const std::string &message, const char *color = WHITE)
{
    std::cerr << WHITE "[" << color << symbol << WHITE "] "
            << CYAN << "LPM" << WHITE ": " << RESET
            << message << std::endl;
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

    [[nodiscard]] const std::string &name() const override { return _name; }
    int open(lua_State *L) override { return _luaopen(L); }
    [[nodiscard]] int (*getLuaOpen() const)(lua_State *) override { return _luaopen; }

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

        const std::string &pluginName = folderName;

#ifdef _WIN32
        std::wstring widePath = fs::absolute(dllPath).wstring();
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
        AddDllDirectory(fs::absolute(PLUGIN_DIR).wstring().c_str());
        std::vector<std::string> missing_dlls = check_dependencies(widePath);
        HMODULE lib = LoadLibraryExW(widePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);

        if (!lib) {
            const DWORD errCode = GetLastError();
            LPSTR msg = nullptr;
            FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                nullptr,
                errCode,
                0,
                reinterpret_cast<LPSTR>(&msg),
                0,
                nullptr
            );
            logLPM("!", "Failed to load plugin: " BLUE + pluginName + RESET, BOLD RED);
            std::string cleanMsg = (msg ? msg : "Unknown error");
            while (!cleanMsg.empty() && (cleanMsg.back() == '\n' || cleanMsg.back() == '\r')) {
                cleanMsg.pop_back();
            }

            logLPM("!", std::string("Reason: ") + YELLOW + cleanMsg + RESET);

            if (!missing_dlls.empty()) {
                logLPM("!", "Missing DLL dependencies:");
                for (const auto &dep: missing_dlls) {
                    logLPM("?", "  - " GREEN + dep + RESET);
                }
            }

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
        logLPM("+", "Plugin \"" + safeName + "\" loaded successfully  [" += safeVersion + "]", GREEN);
    }
}
