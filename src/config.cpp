#include "config.h"

#include "util.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace config
{
namespace
{
constexpr const char* DefaultBaseUrl    = "https://api.openai.com/v1";
constexpr const char* DefaultEndpoint   = "/images/generations";
constexpr const char* DefaultModels     = "gpt-image-1,dall-e-3";
constexpr const char* DefaultModel      = "gpt-image-1";
constexpr const char* DefaultQuality    = "auto";
constexpr const char* DefaultFormat     = "png";
constexpr const char* DefaultOutputDir  = "Output";

void readFields(Config& cfg)
{
    cfg.baseUrl      = cfg.value("base_url");
    cfg.apiKey       = cfg.value("api_key");
    cfg.endpoint     = cfg.value("endpoint");
    cfg.model        = cfg.value("model");
    cfg.quality      = cfg.value("quality");
    cfg.outputFormat = cfg.value("output_format");
    cfg.extraJson    = cfg.value("extra_json");
    cfg.outputDir    = cfg.value("output_dir");
    cfg.width        = util::parseInt(cfg.value("width"), 1024);
    cfg.height       = util::parseInt(cfg.value("height"), 1024);
    cfg.n            = util::parseInt(cfg.value("n"), 1);
    cfg.timeoutSec   = util::parseInt(cfg.value("timeout_sec"), 300);
    cfg.verifyTls    = util::parseBool(cfg.value("verify_tls"), true);
    cfg.sendResponseFormat = util::parseBool(cfg.value("send_response_format"), true);
    cfg.transparent  = util::parseBool(cfg.value("transparent"), false);

    // Missing keys fall back to defaults so a partial config stays usable.
    if (cfg.baseUrl.empty())
        cfg.baseUrl = DefaultBaseUrl;
    if (cfg.endpoint.empty())
        cfg.endpoint = DefaultEndpoint;
    if (cfg.quality.empty())
        cfg.quality = DefaultQuality;
    if (cfg.outputFormat.empty())
        cfg.outputFormat = DefaultFormat;
    if (cfg.outputDir.empty())
        cfg.outputDir = DefaultOutputDir;

    cfg.models = util::split(cfg.value("models"), ',');
    cfg.models.erase(std::remove_if(cfg.models.begin(),
                                    cfg.models.end(),
                                    [](const std::string& item) { return item.empty(); }),
                     cfg.models.end());

    if (cfg.models.empty() && !cfg.model.empty())
        cfg.models.push_back(cfg.model);

    if (cfg.n < 1)
        cfg.n = 1;
    if (cfg.n > 10)
        cfg.n = 10;
    if (cfg.width < 64)
        cfg.width = 1024;
    if (cfg.height < 64)
        cfg.height = 1024;
    if (cfg.timeoutSec < 10)
        cfg.timeoutSec = 300;
}

void writeFields(Config& cfg)
{
    cfg.assign("base_url", cfg.baseUrl);
    cfg.assign("api_key", cfg.apiKey);
    cfg.assign("endpoint", cfg.endpoint);
    cfg.assign("models", [&cfg]
    {
        std::string joined;
        for (const auto& item : cfg.models)
        {
            if (!joined.empty())
                joined += ',';
            joined += item;
        }
        return joined;
    }());
    cfg.assign("model", cfg.model);
    cfg.assign("quality", cfg.quality);
    cfg.assign("output_format", cfg.outputFormat);
    cfg.assign("extra_json", cfg.extraJson);
    cfg.assign("output_dir", cfg.outputDir);
    cfg.assign("width", std::to_string(cfg.width));
    cfg.assign("height", std::to_string(cfg.height));
    cfg.assign("n", std::to_string(cfg.n));
    cfg.assign("verify_tls", cfg.verifyTls ? "true" : "false");
    cfg.assign("timeout_sec", std::to_string(cfg.timeoutSec));
    cfg.assign("send_response_format", cfg.sendResponseFormat ? "true" : "false");
    cfg.assign("transparent", cfg.transparent ? "true" : "false");
}
} // namespace

std::string Config::value(std::string_view key) const
{
    for (const auto& [name, stored] : raw)
    {
        if (name == key)
            return stored;
    }
    return {};
}

void Config::assign(std::string_view key, std::string newValue)
{
    for (auto& [name, stored] : raw)
    {
        if (name == key)
        {
            stored = std::move(newValue);
            return;
        }
    }

    raw.emplace_back(std::string(key), std::move(newValue));
    order.push_back("K:" + std::string(key));
}

void applyDefaults(Config& cfg)
{
    cfg.raw.clear();
    cfg.order.clear();

    cfg.baseUrl      = DefaultBaseUrl;
    cfg.endpoint     = DefaultEndpoint;
    cfg.model        = DefaultModel;
    cfg.quality      = DefaultQuality;
    cfg.outputFormat = DefaultFormat;
    cfg.outputDir    = DefaultOutputDir;
    cfg.models       = {DefaultModel, "dall-e-3"};

    cfg.assign("base_url", cfg.baseUrl);
    cfg.assign("api_key", cfg.apiKey);
    cfg.assign("endpoint", cfg.endpoint);
    cfg.assign("models", DefaultModels);
    cfg.assign("model", cfg.model);
    cfg.assign("quality", cfg.quality);
    cfg.assign("output_format", cfg.outputFormat);
    cfg.assign("extra_json", cfg.extraJson);
    cfg.assign("output_dir", cfg.outputDir);
    cfg.assign("width", std::to_string(cfg.width));
    cfg.assign("height", std::to_string(cfg.height));
    cfg.assign("n", std::to_string(cfg.n));
    cfg.assign("verify_tls", cfg.verifyTls ? "true" : "false");
    cfg.assign("timeout_sec", std::to_string(cfg.timeoutSec));
    cfg.assign("send_response_format", cfg.sendResponseFormat ? "true" : "false");
    cfg.assign("transparent", cfg.transparent ? "true" : "false");
}

bool load(const std::filesystem::path& path, Config& cfg, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        applyDefaults(cfg);
        readFields(cfg);
        save(path, cfg, error);
        return true;
    }

    cfg.raw.clear();
    cfg.order.clear();

    std::string line;
    bool        firstLine = true;
    while (std::getline(file, line))
    {
        if (firstLine && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
        {
            line.erase(0, 3);
        }
        firstLine = false;

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        const std::string trimmed = util::trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
        {
            cfg.order.push_back("R:" + line);
            continue;
        }

        const std::size_t separator = trimmed.find('=');
        if (separator == std::string::npos)
        {
            cfg.order.push_back("R:" + line);
            continue;
        }

        const std::string key   = util::trim(trimmed.substr(0, separator));
        const std::string value = util::trim(trimmed.substr(separator + 1));
        if (key.empty())
        {
            cfg.order.push_back("R:" + line);
            continue;
        }

        cfg.raw.emplace_back(key, value);
        cfg.order.push_back("K:" + key);
    }

    readFields(cfg);

    if (cfg.models.empty())
        cfg.models.push_back(cfg.model.empty() ? DefaultModel : cfg.model);
    if (cfg.model.empty())
        cfg.model = cfg.models.front();

    if (std::find(cfg.models.begin(), cfg.models.end(), cfg.model) == cfg.models.end())
        cfg.models.push_back(cfg.model);

    return true;
}

bool save(const std::filesystem::path& path, Config& cfg, std::string& error)
{
    writeFields(cfg);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        error = "Не удалось записать конфиг: " + path.string();
        return false;
    }

    if (cfg.order.empty())
    {
        for (const auto& [key, value] : cfg.raw)
            file << key << '=' << value << "\n";
        return true;
    }

    std::vector<std::string> written;

    const auto writeKey = [&](std::string_view key)
    {
        if (std::find(written.begin(), written.end(), std::string(key)) != written.end())
            return;
        written.emplace_back(key);

        for (const auto& [name, value] : cfg.raw)
        {
            if (name == key)
            {
                file << name << '=' << value << "\n";
                return;
            }
        }
    };

    for (const auto& entry : cfg.order)
    {
        if (entry.rfind("K:", 0) == 0)
            writeKey(entry.substr(2));
        else if (entry.rfind("R:", 0) == 0)
            file << entry.substr(2) << "\n";
    }

    for (const auto& [key, value] : cfg.raw)
    {
        if (std::find(written.begin(), written.end(), key) == written.end())
            file << key << '=' << value << "\n";
    }

    return true;
}
} // namespace config
