#include "generator.h"

#include "base64.h"
#include "json.h"
#include "net.h"
#include "util.h"

#include <fstream>

namespace generator
{
namespace
{
std::string sizeString(int width, int height)
{
    return std::to_string(width) + "x" + std::to_string(height);
}

void mergeExtraObject(json::Value& payload, const std::string& extraJson)
{
    const std::string trimmed = util::trim(extraJson);
    if (trimmed.empty())
        return;

    json::Value extra;
    std::string error;
    if (!json::parse(trimmed, extra, error) || !extra.isObject())
    {
        util::logLine("extra_json проигнорирован: " + error);
        return;
    }

    for (const auto& [key, value] : extra.asObject())
        payload[key] = value;
}

std::string extractError(const json::Value& root)
{
    if (const json::Value* error = root.find("error"))
    {
        if (error->isObject())
        {
            std::string message;
            if (const json::Value* found = error->find("message"))
                message = found->asString();
            if (message.empty())
            {
                if (const json::Value* code = error->find("code"))
                    message = code->asString();
            }
            if (const json::Value* type = error->find("type"))
            {
                if (!type->asString().empty())
                    message += message.empty() ? type->asString() : " (" + type->asString() + ")";
            }
            if (!message.empty())
                return message;
        }
        else if (!error->asString().empty())
        {
            return error->asString();
        }
        return error->dump();
    }

    if (const json::Value* message = root.find("message"))
        return message->asString();

    return {};
}

bool saveBinary(const std::filesystem::path& path, const std::string& bytes, std::string& error)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        error = "Не удалось открыть файл для записи: " + path.string();
        return false;
    }

    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file)
    {
        error = "Ошибка записи файла: " + path.string();
        return false;
    }

    return true;
}

struct DecodedImage
{
    std::string bytes;
    std::string extension;
    std::string error;
};

DecodedImage parseImageFromString(const json::Value& item, const config::Config& cfg)
{
    DecodedImage result;

    std::string payload;
    bool        isBase64 = false;
    bool        isUrl    = false;

    if (const json::Value* b64 = item.find("b64_json"))
    {
        payload  = b64->asString();
        isBase64 = true;
    }
    else if (const json::Value* data = item.find("image"))
    {
        payload  = data->asString();
        isBase64 = true;
    }
    else if (const json::Value* url = item.find("url"))
    {
        payload = url->asString();
        isUrl   = true;
    }

    if (payload.empty())
    {
        result.error = "В ответе нет изображения (b64_json/url)";
        return result;
    }

    std::string bytes;
    if (isBase64)
    {
        if (payload.rfind("data:", 0) == 0)
        {
            const std::size_t comma = payload.find(',');
            if (comma != std::string::npos)
                payload = payload.substr(comma + 1);
        }

        if (!base64::decode(payload, bytes) || bytes.empty())
        {
            result.error = "Не удалось декодировать base64";
            return result;
        }
    }
    else if (isUrl && (payload.rfind("http://", 0) == 0 || payload.rfind("https://", 0) == 0))
    {
        const net::HttpResponse download = net::getBytes(payload, "", cfg.verifyTls, cfg.timeoutSec);
        if (!download.error.empty())
        {
            result.error = "Не удалось скачать изображение: " + download.error;
            return result;
        }
        if (!download.ok())
        {
            result.error = "Сервер вернул HTTP " + std::to_string(download.status) + " при скачивании";
            return result;
        }

        bytes = download.body;
        if (bytes.empty())
        {
            result.error = "Скачанный файл пуст";
            return result;
        }
    }
    else
    {
        result.error = "Неизвестный формат изображения в ответе";
        return result;
    }

    result.bytes     = std::move(bytes);
    result.extension = util::detectImageExtension(result.bytes, cfg.outputFormat);

    return result;
}
} // namespace

std::string buildPayload(const Request& request, const config::Config& cfg)
{
    json::Value payload = json::Value::makeObject();

    const std::string model = request.model.empty() ? cfg.model : request.model;

    payload["model"]  = model;
    payload["prompt"] = request.prompt;
    payload["n"]      = request.n;
    payload["size"]   = sizeString(request.width, request.height);

    if (!request.quality.empty() && request.quality != "auto")
        payload["quality"] = request.quality;

    if (!request.format.empty() && request.format != "png")
        payload["output_format"] = request.format;

    if (!request.background.empty())
        payload["background"] = request.background;

    if (!request.moderation.empty())
        payload["moderation"] = request.moderation;

    const bool gptImage = model.rfind("gpt-image", 0) == 0;
    if (cfg.sendResponseFormat && !gptImage)
        payload["response_format"] = "b64_json";

    mergeExtraObject(payload, cfg.extraJson);
    mergeExtraObject(payload, request.extraJson);

    return payload.dump();
}

Result generate(const Request& request, const config::Config& cfg, const std::filesystem::path& baseDir)
{
    Result result;

    if (request.prompt.empty())
    {
        result.error = "Промпт пуст";
        return result;
    }
    if (cfg.apiKey.empty())
    {
        result.error = "Не задан API-ключ в config.ini";
        return result;
    }

    const std::string url = net::joinUrl(cfg.baseUrl, cfg.endpoint);
    const std::string payload = buildPayload(request, cfg);

    const net::HttpResponse response = net::postJson(url, payload, cfg.apiKey, "", cfg.verifyTls, cfg.timeoutSec);

    if (!response.error.empty())
    {
        result.error = response.error;
        return result;
    }

    result.rawResponse = response.body;

    json::Value root;
    std::string parseError;
    if (!json::parse(response.body, root, parseError))
    {
        result.error = "Не удалось разобрать ответ: " + parseError;
        return result;
    }

    if (!response.ok())
    {
        std::string message = extractError(root);
        if (message.empty())
            message = "HTTP " + std::to_string(response.status);
        result.error = message;
        return result;
    }

    const json::Value* data = root.find("data");
    if (!data || !data->isArray())
    {
        std::string message = extractError(root);
        result.error = message.empty() ? "В ответе нет массива data" : message;
        return result;
    }

    std::filesystem::path outputDir = util::resolvePath(baseDir, cfg.outputDir);
    std::string           dirError;
    if (!util::ensureDirectory(outputDir, dirError))
    {
        result.error = dirError;
        return result;
    }

    const std::string stamp = util::timestampNow();
    int               index = 0;

    for (const auto& item : data->asArray())
    {
        if (!item.isObject())
            continue;

        const auto image = parseImageFromString(item, cfg);
        if (!image.error.empty())
        {
            result.error = image.error;
            continue;
        }

        ImageResult stored;
        stored.bytes     = image.bytes;
        stored.extension = image.extension;

        if (const json::Value* revised = item.find("revised_prompt"))
        {
            if (result.revisedPrompt.empty())
                result.revisedPrompt = revised->asString();
        }

        const std::string fileName = "nb_" + stamp + "_" + std::to_string(index) + "_" +
                                    util::sanitizeFileName(request.model) + image.extension;

        stored.path = outputDir / fileName;

        std::string saveError;
        if (!saveBinary(stored.path, stored.bytes, saveError))
        {
            result.error = saveError;
            continue;
        }

        util::logLine("Сохранено: " + stored.path.string());
        result.images.push_back(std::move(stored));
        ++index;
    }

    if (result.images.empty())
    {
        if (result.error.empty())
            result.error = "Сервер не вернул ни одного изображения";
        return result;
    }

    result.error.clear();
    result.success = true;
    return result;
}
} // namespace generator
