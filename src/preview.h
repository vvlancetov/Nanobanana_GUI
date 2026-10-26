#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include <SFML/Graphics.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace preview
{
class PreviewWindow
{
public:
    PreviewWindow();
    ~PreviewWindow();

    bool create(HWND host);
    void destroy();

    void handleEvents();
    void render();

    bool loadImage(const std::filesystem::path& path);
    void clearImage();
    void resetView();

    [[nodiscard]] bool hasImage() const { return m_texture != nullptr; }
    [[nodiscard]] const std::filesystem::path& currentPath() const { return m_path; }
    [[nodiscard]] HWND windowHandle() const { return m_host; }

private:
    HWND                              m_host = nullptr;
    std::unique_ptr<sf::RenderWindow>  m_window;
    std::unique_ptr<sf::Texture>      m_texture;
    std::optional<sf::Sprite>         m_sprite;
    std::filesystem::path             m_path;

    float   m_zoom      = 1.0f;
    float   m_panX      = 0.0f;
    float   m_panY      = 0.0f;
    bool    m_dragging  = false;
    sf::Vector2f m_lastMouse{};
    sf::Font     m_font;
    bool         m_fontReady = false;
};
} // namespace preview
