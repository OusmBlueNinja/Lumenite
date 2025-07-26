#include "LumenitePackageManager.h"
#include "../ErrorHandler.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <curl/curl.h>
#include <yaml-cpp/yaml.h>
#include <json/json.h>


#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#endif

std::string LumenitePackageManager::http_get(const std::string &url)
{
#ifdef _WIN32
    HINTERNET hInternet = InternetOpenA("LumenitePM", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!hInternet) return "";

    HINTERNET hFile = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD, 0);
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
    log_error("HTTP fetch not implemented on this platform.");
    return "";
#endif
}


std::string LumenitePackageManager::http_download(const std::string &url, size_t &totalSizeOut)
{
    HINTERNET hInternet = InternetOpenA("LumenitePackageManager", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return "";

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return "";
    }

    // Get content length
    char buffer[32];
    DWORD len = sizeof(buffer);
    totalSizeOut = 0;

    if (HttpQueryInfoA(hUrl, HTTP_QUERY_CONTENT_LENGTH, buffer, &len, NULL)) {
        totalSizeOut = std::stoul(buffer);
    }

    std::string result;
    const DWORD chunkSize = 4096;
    char chunk[chunkSize];
    DWORD bytesRead = 0, downloaded = 0;

    constexpr int barWidth = 40;
    auto startTime = std::chrono::steady_clock::now();

    while (InternetReadFile(hUrl, chunk, chunkSize, &bytesRead) && bytesRead > 0) {
        result.append(chunk, bytesRead);
        downloaded += bytesRead;

        if (totalSizeOut > 0) {
            // Progress percentage
            double progress = static_cast<double>(downloaded) / totalSizeOut;
            int percent = static_cast<int>(progress * 100);
            int filled = static_cast<int>(progress * barWidth);

            // Colored progress bar
            std::string bar;
            for (int i = 0; i < barWidth; ++i) {
                if (i < filled)
                    bar += "\033[32m#\033[0m"; // Green
                else
                    bar += "\033[90m-\033[0m"; // Gray
            }

            // Speed and size
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - startTime).count();
            double speed = downloaded / 1024.0 / elapsed; // KB/s

            std::ostringstream sizeStr;
            if (totalSizeOut >= 1024 * 1024)
                sizeStr << std::fixed << std::setprecision(2) << (totalSizeOut / 1024.0 / 1024.0) << " MB";
            else
                sizeStr << std::fixed << std::setprecision(0) << (totalSizeOut / 1024.0) << " KB";

            std::ostringstream speedStr;
            if (speed > 1024.0)
                speedStr << std::fixed << std::setprecision(2) << (speed / 1024.0) << " MB/s";
            else
                speedStr << std::fixed << std::setprecision(1) << speed << " KB/s";

            std::cout << "\r\033[36m[~] LPM:\033[0m \033[97mDownloading\033[0m |" << bar << "| "
                    << "\033[93m" << percent << "%\033[0m "
                    << "\033[90m(" << sizeStr.str() << " @ " << speedStr.str() << ")\033[0m" << std::flush;
        }
    }

    if (totalSizeOut > 0)
        std::cout << "\033[0m\n"; // Reset colors and newline

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    return result;
}


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

void LumenitePackageManager::log_notice(const std::string &header, const std::string &detail)
{
    std::cout << CYAN << "[~] Notice  : " << RESET << header << "\n"
            << "             " << detail << "\n";
}

// ----- Progress Bar -----
void LumenitePackageManager::show_progress(size_t downloaded, size_t total)
{
    int width = 40;
    float ratio = static_cast<float>(downloaded) / total;
    int filled = static_cast<int>(ratio * width);
    std::string bar = std::string(filled, '█') + std::string(width - filled, '-');
    std::cout << "\r[~] LPM: Downloading |" << bar << "| " << int(ratio * 100) << "% ";
    std::cout.flush();
}

// ----- Command Dispatcher -----
void LumenitePackageManager::run(const std::vector<std::string> &args)
{
    if (args.empty()) {
        log_info("Usage: lumenite package <get|remove|update> <plugin>");
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
    } else {
        log_warn("Unknown or incomplete command.");
    }

    saveYAML();
}

// ----- Package Commands -----
void LumenitePackageManager::cmd_get(const std::string &name)
{
    if (isPluginInstalled(name)) {
        log_warn("Plugin '" + name + "' is already installed.");
        return;
    }

    log_info("Fetching registry...");
    for (const auto &pkg: fetchRegistry()) {
        if (pkg.name == name) {
            std::string outPath = pluginDir + "lumenite_" + name + ".dll";
            log_info("Installing plugin '" + name + "'...");
            if (downloadFile(pkg.url, outPath)) {
                log_success("Plugin '" + name + "' installed successfully  [\"" + pkg.version + "\"]");
                markPluginInstalled(name, pkg.version);
            }
            return;
        }
    }

    log_error("Plugin '" + name + "' not found in registry.");
}

void LumenitePackageManager::cmd_remove(const std::string &name)
{
    std::string path = pluginDir + "lumenite_" + name + ".dll";
    if (fs::exists(path)) {
        fs::remove(path);
        log_success("Removed plugin '" + name + "'");
    }

    installed.erase(std::remove_if(installed.begin(), installed.end(),
                                   [&](const InstalledPlugin &p) { return p.name == name; }), installed.end());
}

void LumenitePackageManager::cmd_update(const std::string &name)
{
    for (const auto &pkg: fetchRegistry()) {
        if (pkg.name == name) {
            for (const auto &inst: installed) {
                if (inst.name == name && inst.version == pkg.version) {
                    log_warn("Plugin '" + name + "' is already up to date.");
                    return;
                }
            }

            log_info("Updating plugin '" + name + "'...");
            std::string outPath = pluginDir + "lumenite_" + name + ".dll";
            if (downloadFile(pkg.url, outPath)) {
                log_success("Plugin '" + name + "' updated to v" + pkg.version);
                markPluginInstalled(name, pkg.version);
            }
            return;
        }
    }

    log_error("Plugin '" + name + "' not found in registry.");
}


size_t LumenitePackageManager::curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    auto *tuple = static_cast<std::pair<FILE *, size_t> *>(userdata);
    fwrite(ptr, size, nmemb, tuple->first);
    tuple->second += total;

    // Simulated max size (for visual bar)
    const size_t assumed_total_size = 500000;
    show_progress(tuple->second, assumed_total_size);

    return total;
}


bool LumenitePackageManager::downloadFile(const std::string &url, const std::string &outPath)
{
    log_info("Downloading...");

    size_t totalSize = 0;
    std::string data = http_download(url, totalSize);
    if (data.empty()) {
        log_error("Download failed or returned empty response.");
        return false;
    }

    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        log_error("Failed to open file: " + outPath);
        return false;
    }

    out.write(data.data(), data.size());
    out.close();

    log_success("Downloaded to: " + outPath);
    return true;
}


// ----- Persistence -----

void LumenitePackageManager::ensurePluginFolder()
{
    if (!fs::exists(pluginDir)) fs::create_directory(pluginDir);
}

void LumenitePackageManager::loadYAML()
{
    installed.clear();
    if (!fs::exists(metadataFile)) return;

    YAML::Node config = YAML::LoadFile(metadataFile);
    if (!config["plugins"]) return;

    for (const auto &plugin: config["plugins"]) {
        installed.push_back({plugin["name"].as<std::string>(), plugin["version"].as<std::string>()});
    }
}

void LumenitePackageManager::saveYAML()
{
    YAML::Emitter out;
    out << YAML::Comment("Lumenite Plugins");
    out << YAML::BeginMap;
    out << YAML::Key << "plugins" << YAML::Value << YAML::BeginSeq;

    for (const auto &plugin: installed) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << plugin.name;
        out << YAML::Key << "version" << YAML::Value << plugin.version;
        out << YAML::EndMap;
    }

    out << YAML::EndSeq << YAML::EndMap;

    std::ofstream fout(metadataFile);
    fout << out.c_str();
}

std::vector<LumenitePackageManager::AvailablePlugin> LumenitePackageManager::fetchRegistry()
{
    std::vector<AvailablePlugin> list;
    std::string rawJson = http_get(registryURL);
    if (rawJson.empty()) {
        log_error("Failed to fetch registry.");
        return list;
    }


    if (rawJson.empty()) {
        log_error("Registry returned empty response.");
        return list;
    }

    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errs;
    std::istringstream stream(rawJson);

    if (!Json::parseFromStream(reader, stream, &root, &errs)) {
        log_error("Failed to parse package registry JSON.");
        log_warn("Registry contents:\n" + rawJson); // show actual content
        return list;
    }

    for (const auto &item: root) {
        list.push_back({
            item["name"].asString(),
            item["description"].asString(),
            item["version"].asString(),
            item["engine_version"].asString(),
            item["url"].asString()
        });
    }

    return list;
}


// ----- Utility -----
bool LumenitePackageManager::isPluginInstalled(const std::string &name)
{
    for (const auto &p: installed) {
        if (p.name == name) return true;
    }
    return false;
}

void LumenitePackageManager::markPluginInstalled(const std::string &name, const std::string &version)
{
    for (auto &p: installed) {
        if (p.name == name) {
            p.version = version;
            return;
        }
    }
    installed.push_back({name, version});
}
