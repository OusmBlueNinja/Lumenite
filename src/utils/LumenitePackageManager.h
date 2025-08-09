#pragma once
#include <string>
#include <vector>

class LumenitePackageManager
{
public:
    static void run(const std::vector<std::string> &args);

    static std::pair<long, std::string> http_get(const std::string &url);


private:
    struct AvailablePlugin
    {
        std::string name;
        std::string description;
        std::string version;
        std::string engine_version;
        std::string dll_url;
        std::vector<std::pair<std::string, std::string> > files;
        std::vector<std::string> depends;
    };

    struct InstalledPlugin
    {
        std::string name;
        std::string version;
        std::string description;
    };

    static constexpr bool use_cache = false;;
    static inline const std::string pluginDir = "./plugins/";
    static inline const std::string metadataFile = pluginDir + "modules.cpl";
    static inline const std::string registryURL =
            "https://dock-it.dev/GigabiteHosting/Lumenite-Package-Manager/raw/branch/main/registry.json";

    static std::vector<InstalledPlugin> installed;

    static void cmd_get(const std::string &name);

    static void cmd_remove(const std::string &name);

    static void cmd_update(const std::string &name);

    static void cmd_list();



    static bool downloadFile(const std::string &url, const std::string &outPath);

    static void ensurePluginFolder();

    static void loadYAML();

    static void saveYAML();

    static bool isPluginInstalled(const std::string &name);

    static void markPluginInstalled(const std::string &name, const std::string &version,
                                    const std::string &description);

    static std::vector<AvailablePlugin> fetchRegistry();

    static void log_info(const std::string &msg);

    static void log_success(const std::string &msg);

    static void log_warn(const std::string &msg);

    static void log_error(const std::string &msg);

    static void log_notice(const std::string &msg, const std::string &advice);
};
