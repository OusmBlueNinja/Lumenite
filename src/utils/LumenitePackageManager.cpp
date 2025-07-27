#include "LumenitePackageManager.h"
#include "../ErrorHandler.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <curl/curl.h>
#include <yaml-cpp/yaml.h>
#include <json/json.h>

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#endif

namespace fs = std::filesystem;


std::vector<LumenitePackageManager::InstalledPlugin> LumenitePackageManager::installed;

void LumenitePackageManager::log_info(const std::string &msg)
{
    std::cout << CYAN << "[~] LPM:" << RESET << " " << msg << "\n";
}

void LumenitePackageManager::log_success(const std::string &msg)
{
    std::cout << GREEN << "[+] LPM:" << RESET << " " << msg << "\n";
}

void LumenitePackageManager::log_warn(const std::string &msg)
{
    std::cout << YELLOW << "[!] LPM:" << RESET << " " << msg << "\n";
}

void LumenitePackageManager::log_error(const std::string &msg)
{
    std::cerr << RED << "[X] LPM:" << RESET << " " << msg << "\n";
}

void LumenitePackageManager::log_notice(const std::string &msg, const std::string &advice)
{
    std::cout << CYAN << "[~] Notice  : " << RESET << msg << "\n"
            << "             " << advice << "\n";
}

// ─── Core Entrypoint ─────────────────────────────────────────
void LumenitePackageManager::run(const std::vector<std::string> &args)
{
    if (args.empty()) {
        log_info("Usage: lumenite package <get|remove|update|list> <plugin>");
        return;
    }

    loadYAML();
    ensurePluginFolder();

    const std::string &cmd = args[0];
    if (cmd == "get" && args.size() >= 2) {
        cmd_get(args[1]);
    } else if (cmd == "remove" && args.size() >= 2) {
        cmd_remove(args[1]);
    } else if (cmd == "update" && args.size() >= 2) {
        cmd_update(args[1]);
    } else if (cmd == "list") {
        cmd_list();
    } else {
        log_warn("Unknown or incomplete command.");
    }

    saveYAML();
}

// ─── Command: Get ────────────────────────────────────────────
void LumenitePackageManager::cmd_get(const std::string &name)
{
    if (isPluginInstalled(name)) {
        log_warn("Plugin '" + name + "' is already installed.");
        return;
    }

    for (const auto &pkg: fetchRegistry()) {
        if (pkg.name != name) continue;

        log_info("Installing: "
                 BOLD CYAN + pkg.name + RESET " "
                 BOLD YELLOW "[" + pkg.version + "]"
                 RESET);

        log_info("  " DIM GRAY + pkg.description + RESET);


        for (const auto &dep: pkg.depends) {
            if (!isPluginInstalled(dep)) {
                log_info("Installing dependency: " + dep);
                cmd_get(dep);
            }
        }

        std::string folder = pluginDir + pkg.name + "/";
        fs::create_directories(folder);

        std::string dllPath = folder + "lumenite_" + pkg.name + ".dll";
        if (!downloadFile(pkg.dll_url, dllPath)) {
            log_error("Failed to download DLL for: " + pkg.name);
            return;
        }
        std::cout << std::endl;

        for (const auto &[relPath, fileUrl]: pkg.files) {
            std::string fullPath = folder + relPath;
            if (!downloadFile(fileUrl, fullPath)) {
                log_warn("Failed to download: " + relPath);
            }
        }
        std::cout << std::endl;


        markPluginInstalled(pkg.name, pkg.version, pkg.description);
        log_success("Installed "
                    "'" BOLD CYAN + name + RESET "'" " "
                    BOLD YELLOW "[" + pkg.version + "]" RESET);

        return;
    }

    log_error("Plugin '" BOLD RED + name + RESET "' not found in registry.");
}

// ─── Command: Remove ─────────────────────────────────────────
void LumenitePackageManager::cmd_remove(const std::string &name)
{
    std::string path = pluginDir + name + "/";
    if (fs::exists(path)) fs::remove_all(path);

    installed.erase(std::remove_if(installed.begin(), installed.end(),
                                   [&](const InstalledPlugin &p) { return p.name == name; }), installed.end());

    log_success("Removed plugin '" + name + "'");
}

// ─── Command: Update ─────────────────────────────────────────
void LumenitePackageManager::cmd_update(const std::string &name)
{
    for (const auto &pkg: fetchRegistry()) {
        if (pkg.name == name) {
            for (const auto &inst: installed) {
                if (inst.name == name && inst.version == pkg.version) {
                    log_info("Plugin '" + name + "' is up to date.");
                    return;
                }
            }
            cmd_get(name);
            return;
        }
    }
    log_error("Plugin '" + name + "' not found in registry.");
}

// ─── Command: List ───────────────────────────────────────────
void LumenitePackageManager::cmd_list()
{
    if (installed.empty()) {
        log_info("No plugins installed.");
        return;
    }

    std::cout << CYAN << "\n[~] LPM:" << RESET << " Installed Plugins:\n\n";
    for (const auto &pkg: installed) {
        std::cout << CYAN << " * " << BOLD << pkg.name << RESET
                << " " << DIM << "[" << pkg.version << "]" << RESET << "\n"
                << "   " << GRAY << pkg.description << RESET << "\n";
    }
}

// ─── File & YAML ─────────────────────────────────────────────
void LumenitePackageManager::ensurePluginFolder()
{
    if (!fs::exists(pluginDir)) fs::create_directories(pluginDir);
}

void LumenitePackageManager::loadYAML()
{
    installed.clear();
    if (!fs::exists(metadataFile)) return;

    YAML::Node config = YAML::LoadFile(metadataFile);
    if (!config["plugins"]) return;

    for (const auto &plugin: config["plugins"]) {
        installed.push_back({
            plugin["name"].as<std::string>(),
            plugin["version"].as<std::string>(),
            plugin["description"] ? plugin["description"].as<std::string>() : ""
        });
    }
}

void LumenitePackageManager::saveYAML()
{
    YAML::Emitter out;
    out << YAML::BeginMap << YAML::Key << "plugins" << YAML::Value << YAML::BeginSeq;

    for (const auto &p: installed) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << p.name;
        out << YAML::Key << "version" << YAML::Value << p.version;
        out << YAML::Key << "description" << YAML::Value << p.description;
        out << YAML::EndMap;
    }

    out << YAML::EndSeq << YAML::EndMap;
    std::ofstream fout(metadataFile);
    fout << out.c_str();
}

// ─── Registry ────────────────────────────────────────────────
std::vector<LumenitePackageManager::AvailablePlugin> LumenitePackageManager::fetchRegistry()
{
    std::vector<AvailablePlugin> list;
    std::string rawJson = http_get(registryURL);
    if (rawJson.empty()) {
        log_error("Registry fetch failed.");
        return list;
    }

    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errs;
    std::istringstream stream(rawJson);

    if (!Json::parseFromStream(reader, stream, &root, &errs)) {
        log_error("Failed to parse registry JSON.");
        return list;
    }

    for (const auto &item: root) {
        std::vector<std::pair<std::string, std::string> > files;
        for (const auto &f: item["files"]) {
            files.emplace_back(f["path"].asString(), f["url"].asString());
        }

        std::vector<std::string> depends;
        for (const auto &d: item["depends"]) {
            depends.push_back(d.asString());
        }

        list.push_back({
            item["name"].asString(),
            item["description"].asString(),
            item["version"].asString(),
            item["engine_version"].asString(),
            item["dll_url"].asString(),
            files,
            depends
        });
    }

    return list;
}

// ─── Plugin State ────────────────────────────────────────────
bool LumenitePackageManager::isPluginInstalled(const std::string &name)
{
    return std::any_of(installed.begin(), installed.end(),
                       [&](const InstalledPlugin &p) { return p.name == name; });
}

void LumenitePackageManager::markPluginInstalled(const std::string &name, const std::string &version,
                                                 const std::string &description)
{
    for (auto &p: installed) {
        if (p.name == name) {
            p.version = version;
            p.description = description;
            return;
        }
    }
    installed.push_back({name, version, description});
}


std::string LumenitePackageManager::http_get(const std::string &url)
{
#ifdef _WIN32
    HINTERNET hInternet = InternetOpenA("LumenitePM", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!hInternet) return "";

    DWORD flags = INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_COOKIES;

    if (!use_cache) {
        flags |= INTERNET_FLAG_RELOAD |
                INTERNET_FLAG_NO_CACHE_WRITE | // never write to cache
                INTERNET_FLAG_PRAGMA_NOCACHE | // no client or proxy cache
                INTERNET_FLAG_NO_AUTH | // skip auth
                INTERNET_FLAG_NO_AUTO_REDIRECT | // stop auto redirects
                INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP |
                INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS;
    }

    HINTERNET hFile = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0, flags, 0);
    if (!hFile) {
        InternetCloseHandle(hInternet);
        return "";
    }

    std::ostringstream ss;
    char buffer[4096];
    DWORD bytesRead;
    while (InternetReadFile(hFile, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        ss.write(buffer, bytesRead);
    }

    InternetCloseHandle(hFile);
    InternetCloseHandle(hInternet);
    return ss.str();
#else
    log_error("http_get not implemented on this platform.");
    return "";
#endif
}


bool LumenitePackageManager::downloadFile(const std::string &url, const std::string &outPath)
{
#ifdef _WIN32
    HINTERNET hInternet = InternetOpenA("LumenitePM", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!hInternet) return false;

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return false;
    }

    DWORD contentLength = 0;
    DWORD len = sizeof(DWORD);
    HttpQueryInfoA(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &len, nullptr);

    fs::create_directories(fs::path(outPath).parent_path());
    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return false;
    }

    constexpr DWORD chunkSize = 8192;
    char buffer[chunkSize];
    DWORD bytesRead = 0;
    size_t totalDownloaded = 0;
    const int barWidth = 40;
    auto start = std::chrono::steady_clock::now();

    std::string relPath = fs::relative(outPath, pluginDir).string();
    std::ostringstream sizeStr;
    std::ostringstream speedStr;

    std::string lastOutput;

    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        out.write(buffer, bytesRead);
        totalDownloaded += bytesRead;

        double progress = contentLength > 0 ? static_cast<double>(totalDownloaded) / contentLength : 0;
        int percent = static_cast<int>(progress * 100.0);
        int filled = static_cast<int>(barWidth * progress);

        std::string bar;
        for (int i = 0; i < barWidth; ++i)
            bar += i < filled ? "\033[32m#\033[0m" : "\033[90m-\033[0m";

        if (contentLength >= 1024 * 1024)
            sizeStr.str(""), sizeStr << std::fixed << std::setprecision(2) << (contentLength / 1024.0 / 1024.0) <<
                    " MB";
        else
            sizeStr.str(""), sizeStr << (contentLength / 1024) << " KB";

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();

        if (double speed = totalDownloaded / 1024.0 / elapsed; speed > 1024.0)
            speedStr.str(""), speedStr << std::fixed << std::setprecision(2) << (speed / 1024.0) << " MB/s";
        else
            speedStr.str(""), speedStr << std::fixed << std::setprecision(1) << speed << " KB/s";

        std::ostringstream right;
        right << " " << GRAY << relPath << RESET;

        std::ostringstream output;
        output << "\r        |" << bar << "| "
                << std::setw(3) << YELLOW << percent << "%" << RESET
                << " (" << MAGENTA << sizeStr.str() << " @ " << speedStr.str() << RESET << ")"
                << std::setw(40) << std::right << right.str()
                << std::string(20, ' ');

        lastOutput = output.str();
        std::cout << lastOutput << std::flush;
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return true;
#else
    log_error("downloadFile not implemented on this platform.");
    return false;
#endif
}
