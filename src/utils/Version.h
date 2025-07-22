#pragma once
#include <string>
#define LUMENITE_RELEASE_VERSION "2025.2"

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define LUMENITE_BUILD_DATE __DATE__
#define LUMENITE_BUILD_TIME __TIME__

// Example: 20250722.1903
inline std::string getLumeniteVersion()
{
    const std::string date = LUMENITE_BUILD_DATE;
    const std::string time = LUMENITE_BUILD_TIME;

    std::string months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const std::string monthStr = date.substr(0, 3);
    const int month = (months.find(monthStr) / 3) + 1;

    const std::string day = date.substr(4, 2);
    const std::string year = date.substr(7, 4);
    const std::string hour = time.substr(0, 2);
    const std::string minute = time.substr(3, 2);

    char version[32];
    snprintf(version, sizeof(version), "%s%02d%s.%s%s", year.c_str(), month, day.c_str(), hour.c_str(), minute.c_str());

    return version;
}
