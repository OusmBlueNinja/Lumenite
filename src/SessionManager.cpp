#include "SessionManager.h"

#include <chrono>
#include <random>
#include <string.h>

#include "Server.h"


// ————— SessionManager definitions —————
std::unordered_map<std::string, std::unordered_map<std::string, std::string> > SessionManager::store;
thread_local std::string SessionManager::currentId;
thread_local bool SessionManager::isNew = false;

static std::string make_id()
{
    uint64_t r = std::mt19937_64(
        static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count())
    )();

    char buffer[17];
    snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(r));
    return std::string(buffer);
}

void SessionManager::start(HttpRequest &req, HttpResponse &res)
{
    auto it = req.headers.find("Cookie");
    if (it != req.headers.end()) {
        const std::string &cookies = it->second;
        size_t p = cookies.find("LUMENITE_SESSION=");
        if (p != std::string::npos) {
            size_t start = p + strlen("LUMENITE_SESSION=");
            size_t end = cookies.find(';', start);
            currentId = cookies.substr(start, (end == std::string::npos ? cookies.size() : end) - start);
        }
    }

    auto sessionIt = store.find(currentId);
    if (currentId.empty() || sessionIt == store.end()) {
        currentId = make_id();
        store[currentId] = {};
        res.headers["Set-Cookie"] = "LUMENITE_SESSION=" + currentId + "; Path=/; HttpOnly";
    }
}

std::string SessionManager::get(const std::string &key)
{
    auto &session = store[currentId];
    auto it = session.find(key);
    return it == session.end() ? "" : it->second;
}

void SessionManager::set(const std::string &key, const std::string &val)
{
    store[currentId][key] = val;
}