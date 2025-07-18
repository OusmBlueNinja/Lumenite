//
// Created by spenc on 7/18/2025.
//
#pragma once

#include <string>
#include <unordered_map>


// forward‑declare the HTTP structs
struct HttpRequest;
struct HttpResponse;

/// Simple in‑memory session store exposed to Lua
class SessionManager
{
public:
    static void start(HttpRequest &req, HttpResponse &res);

    static std::string get(const std::string &key);

    static void set(const std::string &key, const std::string &val);

private:
    static std::unordered_map<
        std::string,
        std::unordered_map<std::string, std::string>
    > store;
    static thread_local std::string currentId;
    static thread_local bool isNew;
};