#pragma once
#include <string>
#include <iostream>
#include <cstdio>

#define LUMENITE_RELEASE_VERSION "2025.2"
#define LUMENITE_BUILD_DATE __DATE__  // "Jul 22 2025"
#define LUMENITE_BUILD_TIME __TIME__  // "19:03:04"

// Month abbreviation → 2-digit number
inline std::string getLumeniteMonthNumber(const std::string &mon)
{
    if (mon == "Jan") return "01";
    if (mon == "Feb") return "02";
    if (mon == "Mar") return "03";
    if (mon == "Apr") return "04";
    if (mon == "May") return "05";
    if (mon == "Jun") return "06";
    if (mon == "Jul") return "07";
    if (mon == "Aug") return "08";
    if (mon == "Sep") return "09";
    if (mon == "Oct") return "10";
    if (mon == "Nov") return "11";
    if (mon == "Dec") return "12";
    return "00";
}

inline std::string getLumeniteNumericBuildID()
{
    std::string date = LUMENITE_BUILD_DATE;
    std::string time = LUMENITE_BUILD_TIME;

    std::string day = date.substr(4, 2);
    std::string hour = time.substr(0, 2);
    std::string min = time.substr(3, 2);

    return day + "." + hour + "." + min;
}

inline std::string getLumeniteBuildDateString()
{
    const std::string date = LUMENITE_BUILD_DATE;
    const std::string time = LUMENITE_BUILD_TIME;

    const std::string mon = date.substr(0, 3);
    const std::string day = date.substr(4, 2);
    const std::string year = date.substr(7, 4);
    const std::string hour = time.substr(0, 2);
    const std::string min = time.substr(3, 2);

    std::string monthFull;
    if (mon == "Jan") monthFull = "January";
    else if (mon == "Feb") monthFull = "February";
    else if (mon == "Mar") monthFull = "March";
    else if (mon == "Apr") monthFull = "April";
    else if (mon == "May") monthFull = "May";
    else if (mon == "Jun") monthFull = "June";
    else if (mon == "Jul") monthFull = "July";
    else if (mon == "Aug") monthFull = "August";
    else if (mon == "Sep") monthFull = "September";
    else if (mon == "Oct") monthFull = "October";
    else if (mon == "Nov") monthFull = "November";
    else if (mon == "Dec") monthFull = "December";
    else monthFull = mon;

    return monthFull + " " + day + ", " + year + " @ " + hour + ":" + min;
}

inline void printVersion()
{
    std::cout << "Release: " << LUMENITE_RELEASE_VERSION << "\n";
    std::cout << "Build: " << getLumeniteNumericBuildID() << "\n";
    std::cout << getLumeniteBuildDateString() << "\n";
}
