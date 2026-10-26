#include "preview.h"

#include "util.h"

#include <algorithm>
#include <cmath>

namespace preview
{
namespace
{
constexpr float MinZoom = 0.05f;
constexpr float MaxZoom = 40.0f;
constexpr const char* HintText = "Здесь появится изображение";

float clamp(float value, float low, float high)
{
    return std::max(low, std::min(high, value));
}
} // namespace

PreviewWindow::PreviewWindow() = default;

PreviewWindow::~PreviewWindow()
{
    destroy();
}

bool PreviewWindow::create(HWND host)
{
    if (m_window)
        return true;

    m_host = host;
    if (!m_host)
        return false;

    try
    {
        m_window = std::make_unique<sf::RenderWindow>(static_cast<sf::WindowHandle>(m_host));
        m_window->setFramerateLimit(60);
    }
    catch (const std::exception& exception)
    {
        util::logLine(std::string("Не удалось создать окно SFML: ") + exception.what());
        m_window.reset();
        return false;
    }

    m_fontReady = m_font.openFromFile("C:/Windows/Fonts/segoeui.ttf");

    return true;
}

void PreviewWindow::destroy()
{
    m_sprite.reset();
    m_texture.reset();
    m_window.reset();
    m_host = nullptr;
}

void PreviewWindow::handleEvents()
{
    if (!m_window)
        return;

    while (const std::optional event = m_window->pollEvent())
    {
        if (const auto* scrolled = event->getIf<sf::Event::MouseWheelScrolled>())
        {
            if (!m_sprite)
                continue;

            if (scrolled->wheel != sf::Mouse::Wheel::Vertical)
                continue;

            const float factor = std::pow(1.15f, scrolled->delta);
            const float oldZoom = m_zoom;
            m_zoom = clamp(m_zoom * factor, MinZoom, MaxZoom);

            if (oldZoom > 0.0f)
            {
                const sf::Vector2f size   = sf::Vector2f(m_window->getSize());
                const sf::Vector2f center = size / 2.0f;
                const sf::Vector2f cursor = sf::Vector2f(scrolled->position);

                const sf::Vector2u textureSize = m_texture->getSize();
                const sf::Vector2f imageSize(static_cast<float>(textureSize.x), static_cast<float>(textureSize.y));
                const float        fitScale = std::min(size.x / imageSize.x, size.y / imageSize.y);
                const float        oldScale = fitScale * oldZoom;
                const float        newScale = fitScale * m_zoom;

                if (oldScale > 0.0f)
                {
                    const sf::Vector2f imagePoint = (cursor - center - sf::Vector2f(m_panX, m_panY)) / oldScale;
                    m_panX = cursor.x - center.x - imagePoint.x * newScale;
                    m_panY = cursor.y - center.y - imagePoint.y * newScale;
                }
            }
            continue;
        }

        if (const auto* pressed = event->getIf<sf::Event::MouseButtonPressed>())
        {
            if (pressed->button == sf::Mouse::Button::Left)
            {
                m_dragging  = true;
                m_lastMouse = sf::Vector2f(pressed->position);
            }
            continue;
        }

        if (const auto* released = event->getIf<sf::Event::MouseButtonReleased>())
        {
            if (released->button == sf::Mouse::Button::Left)
                m_dragging = false;
            continue;
        }

        if (const auto* moved = event->getIf<sf::Event::MouseMoved>())
        {
            if (m_dragging)
            {
                const sf::Vector2f current(moved->position);
                const sf::Vector2f delta = current - m_lastMouse;
                m_panX += delta.x;
                m_panY += delta.y;
                m_lastMouse = current;
            }
            continue;
        }
    }
}

void PreviewWindow::render()
{
    if (!m_window)
        return;

    const sf::Vector2u size = m_window->getSize();
    if (size.x == 0 || size.y == 0)
        return;

    m_window->setView(sf::View(sf::FloatRect({0.0f, 0.0f}, sf::Vector2f(size))));
    m_window->clear(sf::Color(24, 24, 28));

    if (m_sprite && m_texture)
    {
        const sf::Vector2u textureSize = m_texture->getSize();
        const sf::Vector2f imageSize(static_cast<float>(textureSize.x), static_cast<float>(textureSize.y));
        const float        fitScale = std::min(static_cast<float>(size.x) / imageSize.x,
                                               static_cast<float>(size.y) / imageSize.y);

        m_sprite->setOrigin(imageSize / 2.0f);
        m_sprite->setPosition(sf::Vector2f(size) / 2.0f + sf::Vector2f(m_panX, m_panY));
        m_sprite->setScale(sf::Vector2f(fitScale * m_zoom, fitScale * m_zoom));

        m_window->draw(*m_sprite);
    }
    else if (m_fontReady)
    {
        sf::Text hint(m_font, sf::String(util::utf8ToWide(HintText)), 18);
        hint.setFillColor(sf::Color(120, 120, 130));
        const sf::FloatRect bounds = hint.getLocalBounds();
        hint.setOrigin(bounds.position + bounds.size / 2.0f);
        hint.setPosition(sf::Vector2f(size) / 2.0f);
        m_window->draw(hint);
    }

    m_window->display();
}

bool PreviewWindow::loadImage(const std::filesystem::path& path)
{
    m_texture = std::make_unique<sf::Texture>();
    if (!m_texture->loadFromFile(path))
    {
        m_texture.reset();
        m_sprite.reset();
        return false;
    }

    m_sprite.emplace(*m_texture);
    m_path = path;
    resetView();
    return true;
}

void PreviewWindow::clearImage()
{
    m_sprite.reset();
    m_texture.reset();
    m_path.clear();
    resetView();
}

void PreviewWindow::resetView()
{
    m_zoom = 1.0f;
    m_panX = 0.0f;
    m_panY = 0.0f;
}
} // namespace preview
