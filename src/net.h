#pragma once

#include <string>
#include <string_view>

namespace net
{
struct Url
{
    std::string  scheme;
    std::string  host;
    unsigned short port = 0;
    std::string  path;
    std::string  query;
};

bool parseUrl(std::string_view text, Url& url, std::string& error);

std::string joinUrl(std::string_view base, std::string_view endpoint);

struct HttpResponse
{
    int         status = 0;
    std::string body;
    std::string error;
    bool        ok() const { return status >= 200 && status < 300; }
};

HttpResponse postJson(const std::string& url,
                      const std::string& body,
                      const std::string& bearerToken,
                      const std::string& extraHeaders,
                      bool               verifyTls,
                      int                timeoutSec);

HttpResponse getBytes(const std::string& url, const std::string& bearerToken, bool verifyTls, int timeoutSec);
} // namespace net
