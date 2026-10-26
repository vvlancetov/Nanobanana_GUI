#pragma once

#include "config.h"

#include <filesystem>
#include <string>
#include <vector>

namespace generator
{
struct Request
{
    std::string              prompt;
    std::string              model;
    int                      width   = 1024;
    int                      height  = 1024;
    int                      n       = 1;
    std::string              quality;
    std::string              format;
    std::string              background;
    std::string              moderation;
    std::string              extraJson;
};

struct ImageResult
{
    std::filesystem::path path;
    std::string           bytes;
    std::string           extension;
};

struct Result
{
    bool                     success = false;
    std::string              error;
    std::vector<ImageResult> images;
    std::string              rawResponse;
    std::string              revisedPrompt;
};

std::string buildPayload(const Request& request, const config::Config& cfg);

Result generate(const Request& request, const config::Config& cfg, const std::filesystem::path& baseDir);
} // namespace generator
