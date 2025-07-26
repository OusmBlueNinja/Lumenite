#pragma once
#include <string>
#include <vector>

class LumenitePackageManager
{
public:
    static void run(const std::vector<std::string> &args);

private:
    struct AvailablePlugin
    {
        std::string name;
        std::string description;
        std::string version;
        std::string engine_version;
        std::string url;
    };

    struct InstalledPlugin
    {
        std::string name;
        std::string version;
    };

    static std::string http_get(const std::string &url);

    static std::string http_download(const std::string &url, size_t &totalSizeOut);


    static inline const std::string pluginDir = "./plugins/";
    static inline const std::string metadataFile = pluginDir + "modules.cpl";
    static inline const std::string registryURL =
            "https://dock-it.dev/GigabiteHosting/Lumenite-Package-Manager/raw/branch/main/registry.json";

    static std::vector<InstalledPlugin> installed;

    static void cmd_get(const std::string &name);

    static void cmd_remove(const std::string &name);

    static void cmd_update(const std::string &name);

    static void ensurePluginFolder();

    static bool downloadFile(const std::string &url, const std::string &outPath);

    static void loadYAML();

    static void saveYAML();

    static bool isPluginInstalled(const std::string &name);

    static void markPluginInstalled(const std::string &name, const std::string &version);

    static std::vector<AvailablePlugin> fetchRegistry();

    static void log_info(const std::string &msg);

    static void log_success(const std::string &msg);

    static void log_warn(const std::string &msg);

    static void log_error(const std::string &msg);

    static size_t curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);

    static void show_progress(size_t downloaded, size_t total);


    static void log_notice(const std::string &msg, const std::string &advice);
};
