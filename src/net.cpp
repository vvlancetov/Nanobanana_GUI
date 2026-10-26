#include "net.h"

#include "util.h"

#include <SFML/Network/Http.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace net
{
namespace
{
std::string toLowerCopy(std::string_view text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character)
    {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

HttpResponse sendRequest(const std::string& url,
                         const std::string& body,
                         const std::string& bearerToken,
                         const std::string& extraHeaders,
                         bool               verifyTls,
                         int                timeoutSec,
                         bool               isGet)
{
    HttpResponse result;

    Url parsed;
    if (!parseUrl(url, parsed, result.error))
        return result;

    std::string origin = parsed.scheme + "://" + parsed.host;

    sf::Http http;
    if (!http.setHost(origin, parsed.port))
    {
        result.error = "Не удалось разрешить хост: " + parsed.host;
        return result;
    }

    std::string uri = parsed.path;
    if (!parsed.query.empty())
        uri += "?" + parsed.query;

    sf::Http::Request request(uri, isGet ? sf::Http::Request::Method::Get : sf::Http::Request::Method::Post, body);
    request.setField("Accept", "*/*");
    request.setField("Connection", "close");
    if (!isGet)
        request.setField("Content-Type", "application/json");
    if (!bearerToken.empty())
        request.setField("Authorization", "Bearer " + bearerToken);

    std::istringstream headerStream(extraHeaders);
    std::string        headerLine;
    while (std::getline(headerStream, headerLine))
    {
        if (!headerLine.empty() && headerLine.back() == '\r')
            headerLine.pop_back();

        const std::size_t separator = headerLine.find(':');
        if (separator == std::string::npos)
            continue;

        const std::string name  = util::trim(headerLine.substr(0, separator));
        const std::string value = util::trim(headerLine.substr(separator + 1));
        if (!name.empty())
            request.setField(name, value);
    }

    util::logLine(std::string(isGet ? "GET " : "POST ") + url + " (" + std::to_string(body.size()) + " байт)");

    const auto timeout = sf::seconds(static_cast<float>(timeoutSec));
    sf::Http::Response response;
    try
    {
        response = http.sendRequest(request, timeout, verifyTls);
    }
    catch (const std::exception& exception)
    {
        result.error = std::string("Исключение при запросе: ") + exception.what();
        return result;
    }

    result.status = static_cast<int>(response.getStatus());

    if (response.getStatus() == sf::Http::Response::Status::ConnectionFailed)
    {
        result.error = "Не удалось подключиться к серверу (TLS/сеть/прокси)";
        return result;
    }
    if (response.getStatus() == sf::Http::Response::Status::InvalidResponse)
    {
        result.error = "Сервер вернул некорректный HTTP-ответ";
        return result;
    }

    result.body = response.getBody();
    util::logLine("HTTP " + std::to_string(result.status) + " (" + std::to_string(result.body.size()) + " байт)");

    return result;
}
} // namespace

bool parseUrl(std::string_view text, Url& url, std::string& error)
{
    url = Url{};

    std::string_view rest = text;
    const std::size_t schemeEnd = rest.find("://");
    if (schemeEnd == std::string_view::npos)
    {
        url.scheme = "https";
    }
    else
    {
        url.scheme = toLowerCopy(rest.substr(0, schemeEnd));
        rest       = rest.substr(schemeEnd + 3);
    }

    if (url.scheme != "http" && url.scheme != "https")
    {
        error = "Поддерживаются только http и https";
        return false;
    }

    const std::size_t pathStart = rest.find('/');
    std::string_view  authority = rest.substr(0, pathStart);
    if (pathStart != std::string_view::npos)
    {
        url.path = std::string(rest.substr(pathStart));
    }

    const std::size_t queryStart = url.path.find('?');
    if (queryStart != std::string::npos)
    {
        url.query = url.path.substr(queryStart + 1);
        url.path.erase(queryStart);
    }

    if (authority.empty())
    {
        error = "В URL отсутствует хост";
        return false;
    }

    const std::size_t portSeparator = authority.rfind(':');
    if (portSeparator != std::string_view::npos && authority.find(']') == std::string_view::npos)
    {
        url.host = std::string(authority.substr(0, portSeparator));
        try
        {
            url.port = static_cast<unsigned short>(std::stoi(std::string(authority.substr(portSeparator + 1))));
        }
        catch (...)
        {
            error = "Некорректный порт в URL";
            return false;
        }
    }
    else
    {
        url.host = std::string(authority);
    }

    if (url.path.empty())
        url.path = "/";

    return true;
}

std::string joinUrl(std::string_view base, std::string_view endpoint)
{
    std::string result(base);
    while (!result.empty() && result.back() == '/')
        result.pop_back();

    std::string tail(endpoint);
    if (tail.empty())
        return result;

    if (tail.front() != '/')
        tail.insert(tail.begin(), '/');

    return result + tail;
}

HttpResponse postJson(const std::string& url,
                      const std::string& body,
                      const std::string& bearerToken,
                      const std::string& extraHeaders,
                      bool               verifyTls,
                      int                timeoutSec)
{
    return sendRequest(url, body, bearerToken, extraHeaders, verifyTls, timeoutSec, false);
}

HttpResponse getBytes(const std::string& url, const std::string& bearerToken, bool verifyTls, int timeoutSec)
{
    return sendRequest(url, {}, bearerToken, {}, verifyTls, timeoutSec, true);
}
} // namespace net
