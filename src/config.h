#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace config
{
struct Config
{
    std::string              baseUrl;
    std::string              apiKey;
    std::string              endpoint;
    std::string              model;
    std::string              quality;
    std::string              outputFormat;
    std::string              extraJson;
    std::string              outputDir;
    std::vector<std::string> models;

    int  width        = 1024;
    int  height       = 1024;
    int  n            = 1;
    int  timeoutSec   = 300;
    bool verifyTls    = true;
    bool sendResponseFormat = true;
    bool transparent  = false;

    std::vector<std::pair<std::string, std::string>> raw;
    std::vector<std::string>                         order;

    [[nodiscard]] std::string value(std::string_view key) const;
    void                      assign(std::string_view key, std::string newValue);
};

void applyDefaults(Config& cfg);

bool load(const std::filesystem::path& path, Config& cfg, std::string& error);
bool save(const std::filesystem::path& path, Config& cfg, std::string& error);
} // namespace config
