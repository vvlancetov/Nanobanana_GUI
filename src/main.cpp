#include "config.h"
#include "generator.h"
#include "preview.h"
#include "util.h"

#include "../rc/resource.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <commctrl.h>
#include <shellapi.h>
#include <windows.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

namespace
{
constexpr const wchar_t* WindowClass = L"NanobananaGuiWindow";
constexpr const wchar_t* WindowTitle = L"Nanobanana GUI";

constexpr int Margin     = 12;
constexpr int LabelWidth = 92;
constexpr int RowHeight  = 26;

HINSTANCE g_instance = nullptr;
HWND      g_main     = nullptr;

config::Config g_config;
std::filesystem::path g_baseDir;
std::filesystem::path g_configPath;

HFONT g_font      = nullptr;
HFONT g_fontBold  = nullptr;
HFONT g_fontMono  = nullptr;
int   g_dpi       = 96;

struct LabelHandles
{
    HWND model   = nullptr;
    HWND prompt  = nullptr;
    HWND width   = nullptr;
    HWND height  = nullptr;
    HWND count   = nullptr;
    HWND quality = nullptr;
    HWND format  = nullptr;
    HWND output  = nullptr;
    HWND gallery = nullptr;
} g_labels;

std::unique_ptr<preview::PreviewWindow> g_preview;

std::thread        g_worker;
std::atomic<bool>  g_working{false};
std::mutex         g_resultMutex;
generator::Result  g_pendingResult;
bool               g_hasPending  = false;
bool               g_pendingOk   = false;
std::string        g_pendingError;

std::vector<std::filesystem::path> g_gallery;
int                                g_galleryIndex = -1;

int scale(int value)
{
    return MulDiv(value, g_dpi, 96);
}

BOOL CALLBACK applyFontProc(HWND child, LPARAM)
{
    const int id = GetDlgCtrlID(child);

    HFONT font = g_font;
    if (id == IDC_GENERATE)
        font = g_fontBold;
    else if (id == IDC_FILELIST || id == IDC_OUTPUT_PATH)
        font = g_fontMono;

    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return TRUE;
}

void createFonts()
{
    if (g_font)
        DeleteObject(g_font);
    if (g_fontBold)
        DeleteObject(g_fontBold);
    if (g_fontMono)
        DeleteObject(g_fontMono);

    const int fontHeight      = -MulDiv(21, g_dpi, 144);
    const int monoFontHeight  = -MulDiv(13, g_dpi, 96);

    g_font     = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontBold = CreateFontW(fontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontMono = CreateFontW(monoFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Consolas");

    if (g_main)
        EnumChildWindows(g_main, applyFontProc, 0);
}

void setStatus(const std::wstring& text)
{
    if (HWND status = GetDlgItem(g_main, IDC_STATUS))
        SetWindowTextW(status, text.c_str());
}

void setBusy(bool busy)
{
    SetDlgItemTextW(g_main, IDC_GENERATE, busy ? L"Генерация..." : L"Сгенерировать");
    EnableWindow(GetDlgItem(g_main, IDC_GENERATE), busy ? FALSE : TRUE);
    EnableWindow(GetDlgItem(g_main, IDC_MODEL), busy ? FALSE : TRUE);
    EnableWindow(GetDlgItem(g_main, IDC_PROMPT), busy ? FALSE : TRUE);
}

std::wstring getWindowText(HWND control)
{
    const int length = GetWindowTextLengthW(control);
    if (length <= 0)
        return {};

    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

std::string getControlUtf8(HWND control)
{
    return util::wideToUtf8(getWindowText(control));
}

int getControlInt(HWND control, int fallback)
{
    BOOL  translated = FALSE;
    const int value  = GetDlgItemInt(g_main, GetDlgCtrlID(control), &translated, FALSE);
    return translated ? value : fallback;
}

std::filesystem::path outputDir()
{
    return util::resolvePath(g_baseDir, g_config.outputDir);
}

void refreshGallery()
{
    HWND list = GetDlgItem(g_main, IDC_FILELIST);
    if (!list)
        return;

    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    g_gallery.clear();
    g_galleryIndex = -1;

    const std::filesystem::path dir = outputDir();
    std::error_code             code;

    if (!std::filesystem::exists(dir, code))
        return;

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code))
    {
        if (!entry.is_regular_file(code))
            continue;

        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });

        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".webp" || extension == ".gif")
            files.push_back(entry.path());
    }

    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right)
    {
        return left.filename().wstring() > right.filename().wstring();
    });

    for (const auto& file : files)
    {
        const std::wstring name = file.filename().wstring();
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        g_gallery.push_back(file);
    }
}

void showImageAt(int index)
{
    if (!g_preview || index < 0 || index >= static_cast<int>(g_gallery.size()))
        return;

    g_galleryIndex = index;
    if (g_preview->loadImage(g_gallery[static_cast<std::size_t>(index)]))
    {
        const std::wstring name = g_gallery[static_cast<std::size_t>(index)].filename().wstring();
        setStatus(L"Показано: " + name);
    }
    else
    {
        setStatus(L"Не удалось открыть изображение");
    }
}

void collectResult()
{
    generator::Result result;
    bool               ok = false;
    std::string        error;

    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        if (!g_hasPending)
            return;

        result     = std::move(g_pendingResult);
        ok         = g_pendingOk;
        error      = g_pendingError;
        g_hasPending = false;
    }

    setBusy(false);

    if (!ok)
    {
        setStatus(L"Ошибка: " + util::utf8ToWide(error));
        MessageBoxW(g_main, util::utf8ToWide(error).c_str(), L"Ошибка генерации", MB_OK | MB_ICONWARNING);
        return;
    }

    refreshGallery();

    if (!result.images.empty())
    {
        const std::wstring first = result.images.front().path.filename().wstring();
        setStatus(L"Готово: " + first + L" (сохранено " + std::to_wstring(result.images.size()) + L")");

        for (int index = 0; index < static_cast<int>(g_gallery.size()); ++index)
        {
            if (g_gallery[static_cast<std::size_t>(index)] == result.images.front().path)
            {
                SendMessageW(GetDlgItem(g_main, IDC_FILELIST), LB_SETCURSEL, index, 0);
                showImageAt(index);
                break;
            }
        }
    }
}

void saveCurrentSettings();

void startGeneration()
{
    if (g_working.exchange(true))
        return;

    saveCurrentSettings();

    generator::Request request;
    request.prompt     = getControlUtf8(GetDlgItem(g_main, IDC_PROMPT));
    request.model      = getControlUtf8(GetDlgItem(g_main, IDC_MODEL));
    request.width      = getControlInt(GetDlgItem(g_main, IDC_WIDTH), 1024);
    request.height     = getControlInt(GetDlgItem(g_main, IDC_HEIGHT), 1024);
    request.n          = getControlInt(GetDlgItem(g_main, IDC_COUNT), 1);
    request.quality    = getControlUtf8(GetDlgItem(g_main, IDC_QUALITY));
    request.format     = getControlUtf8(GetDlgItem(g_main, IDC_FORMAT));
    request.background = SendDlgItemMessageW(g_main, IDC_TRANSPARENT, BM_GETCHECK, 0, 0) == BST_CHECKED ? "transparent" : "";

    if (request.prompt.empty())
    {
        g_working = false;
        setStatus(L"Введите промпт");
        SetFocus(GetDlgItem(g_main, IDC_PROMPT));
        return;
    }

    if (request.width < 64 || request.width > 8192 || request.height < 64 || request.height > 8192)
    {
        g_working = false;
        setStatus(L"Размер должен быть в диапазоне 64..8192 пикселей");
        return;
    }

    if (request.n < 1)
        request.n = 1;
    if (request.n > 10)
        request.n = 10;

    g_config.model  = request.model;
    g_config.width  = request.width;
    g_config.height = request.height;
    g_config.n      = request.n;

    std::string saveError;
    config::save(g_configPath, g_config, saveError);

    setBusy(true);
    setStatus(L"Запрос отправлен, ожидание ответа...");

    if (g_worker.joinable())
        g_worker.join();

    const std::filesystem::path baseDir = g_baseDir;
    const config::Config        cfg     = g_config;

    g_worker = std::thread([request, cfg, baseDir]
    {
        generator::Result result;
        std::string       error;

        try
        {
            result = generator::generate(request, cfg, baseDir);
            error  = result.error;
        }
        catch (const std::exception& exception)
        {
            error = std::string("Исключение: ") + exception.what();
        }
        catch (...)
        {
            error = "Неизвестная ошибка";
        }

        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            g_pendingResult = std::move(result);
            g_pendingOk     = error.empty() && g_pendingResult.success;
            g_pendingError  = error;
            g_hasPending    = true;
        }

        g_working = false;
        PostMessageW(g_main, g_pendingOk ? WM_APP_RESULT : WM_APP_FAILED, 0, 0);
    });
}

void populateModels()
{
    HWND combo = GetDlgItem(g_main, IDC_MODEL);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    int selected = 0;
    for (std::size_t index = 0; index < g_config.models.size(); ++index)
    {
        const std::wstring wide = util::utf8ToWide(g_config.models[index]);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
        if (g_config.models[index] == g_config.model)
            selected = static_cast<int>(index);
    }

    SendMessageW(combo, CB_SETCURSEL, selected, 0);
    if (g_config.models.empty())
        SetWindowTextW(combo, util::utf8ToWide(g_config.model).c_str());
}

void populateFields()
{
    populateModels();

    SetWindowTextW(GetDlgItem(g_main, IDC_WIDTH), std::to_wstring(g_config.width).c_str());
    SetWindowTextW(GetDlgItem(g_main, IDC_HEIGHT), std::to_wstring(g_config.height).c_str());
    SetWindowTextW(GetDlgItem(g_main, IDC_COUNT), std::to_wstring(g_config.n).c_str());
    SetWindowTextW(GetDlgItem(g_main, IDC_OUTPUT_PATH), outputDir().wstring().c_str());

    HWND quality = GetDlgItem(g_main, IDC_QUALITY);
    SendMessageW(quality, CB_RESETCONTENT, 0, 0);
    for (const wchar_t* value : {L"auto", L"low", L"medium", L"high"})
        SendMessageW(quality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));

    int qualityIndex = 0;
    const std::wstring qualityValue = util::utf8ToWide(g_config.quality);
    const LRESULT      found        = SendMessageW(quality, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                            reinterpret_cast<LPARAM>(qualityValue.c_str()));
    if (found != CB_ERR)
        qualityIndex = static_cast<int>(found);
    SendMessageW(quality, CB_SETCURSEL, qualityIndex, 0);

    HWND format = GetDlgItem(g_main, IDC_FORMAT);
    SendMessageW(format, CB_RESETCONTENT, 0, 0);
    for (const wchar_t* value : {L"png", L"jpeg", L"webp"})
        SendMessageW(format, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));

    int formatIndex = 0;
    const std::wstring formatValue = util::utf8ToWide(g_config.outputFormat);
    const LRESULT      formatFound = SendMessageW(format, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                            reinterpret_cast<LPARAM>(formatValue.c_str()));
    if (formatFound != CB_ERR)
        formatIndex = static_cast<int>(formatFound);
    SendMessageW(format, CB_SETCURSEL, formatIndex, 0);

    SendDlgItemMessageW(g_main, IDC_TRANSPARENT, BM_SETCHECK,
                        g_config.transparent ? BST_CHECKED : BST_UNCHECKED, 0);
}

void layoutControls();

void createControls()
{
    const auto createLabel = [](const wchar_t* text)
    {
        HWND handle = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                      0, 0, 10, 10, g_main, nullptr, g_instance, nullptr);
        SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        return handle;
    };

    const auto createEdit = [](int id, DWORD style)
    {
        HWND handle = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                                      0, 0, 10, 10, g_main, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      g_instance, nullptr);
        SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        return handle;
    };

    g_labels.model  = createLabel(L"Модель:");
    g_labels.prompt = createLabel(L"Промпт:");

    HWND combo = CreateWindowExW(0, L"COMBOBOX", nullptr,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 0, 0, 10, 200, g_main, reinterpret_cast<HMENU>(IDC_MODEL), g_instance, nullptr);
    SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    createEdit(IDC_PROMPT, WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN);

    g_labels.width = createLabel(L"Ширина:");
    createEdit(IDC_WIDTH, ES_NUMBER);

    g_labels.height = createLabel(L"Высота:");
    createEdit(IDC_HEIGHT, ES_NUMBER);

    g_labels.count = createLabel(L"Кол-во:");
    createEdit(IDC_COUNT, ES_NUMBER);

    g_labels.quality = createLabel(L"Качество:");
    HWND qualityCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                        0, 0, 10, 160, g_main, reinterpret_cast<HMENU>(IDC_QUALITY), g_instance, nullptr);
    SendMessageW(qualityCombo, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    g_labels.format = createLabel(L"Формат:");
    HWND formatCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                       0, 0, 10, 140, g_main, reinterpret_cast<HMENU>(IDC_FORMAT), g_instance, nullptr);
    SendMessageW(formatCombo, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    HWND transparent = CreateWindowExW(0, L"BUTTON", L"Прозрачный фон",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                       0, 0, 10, 10, g_main,
                                       reinterpret_cast<HMENU>(IDC_TRANSPARENT), g_instance, nullptr);
    SendMessageW(transparent, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    HWND generate = CreateWindowExW(0, L"BUTTON", L"Сгенерировать",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
                                    0, 0, 10, 10, g_main, reinterpret_cast<HMENU>(IDC_GENERATE), g_instance, nullptr);
    SendMessageW(generate, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBold), TRUE);

    HWND openOutput = CreateWindowExW(0, L"BUTTON", L"Открыть Output",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                      0, 0, 10, 10, g_main, reinterpret_cast<HMENU>(IDC_OPEN_OUTPUT), g_instance, nullptr);
    SendMessageW(openOutput, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    HWND refresh = CreateWindowExW(0, L"BUTTON", L"Обновить",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   0, 0, 10, 10, g_main, reinterpret_cast<HMENU>(IDC_REFRESH), g_instance, nullptr);
    SendMessageW(refresh, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    g_labels.output = createLabel(L"Output:");

    HWND outputPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                      WS_CHILD | WS_VISIBLE | ES_READONLY,
                                      0, 0, 10, 10, g_main, reinterpret_cast<HMENU>(IDC_OUTPUT_PATH), g_instance, nullptr);
    SendMessageW(outputPath, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontMono), TRUE);

    HWND status = CreateWindowExW(0, L"STATIC", L"Готов к работе", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                  0, 0, 10, 10, g_main, reinterpret_cast<HMENU>(IDC_STATUS), g_instance, nullptr);
    SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    g_labels.gallery = createLabel(L"Галерея:");

    HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
                                0, 0, 10, 10, g_main, reinterpret_cast<HMENU>(IDC_FILELIST), g_instance, nullptr);
    SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontMono), TRUE);

    HWND previewHost = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", nullptr,
                                       WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
                                       0, 0, 10, 10, g_main,
                                       reinterpret_cast<HMENU>(IDC_PREVIEW), g_instance, nullptr);

    g_preview = std::make_unique<preview::PreviewWindow>();
    g_preview->create(previewHost);

    layoutControls();
}

void layoutControls()
{
    if (!g_main)
        return;

    RECT client{};
    GetClientRect(g_main, &client);
    const int width  = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0)
        return;

    const int margin    = scale(Margin);
    const int labelW    = scale(LabelWidth);
    const int rowH      = scale(RowHeight);
    const int gap       = scale(6);
    const int buttonH   = scale(30);
    const int labelH    = scale(18);
    const int listWidth = scale(260);
    const int bottomMin = scale(180);
    const int promptMin = scale(72);
    const int smallField = scale(80);

    const int labelX = margin;
    const int fieldX = margin + labelW;
    const int fieldW = std::max(scale(120), width - fieldX - margin);

    const int listLabelH = labelH;
    const int fixedAbovePrompt = margin + rowH + gap;
    const int fixedBelowPrompt = gap + rowH + gap + rowH + gap + buttonH + gap + rowH + gap + labelH + gap;
    const int available = height - fixedAbovePrompt - fixedBelowPrompt - margin - listLabelH;

    int promptH = promptMin;
    int bottomH = bottomMin;

    if (available > promptMin + bottomMin)
    {
        const int extra = available - promptMin - bottomMin;
        promptH = promptMin + extra / 2;
        bottomH = bottomMin + extra - extra / 2;
    }
    else if (available > promptMin + scale(80))
    {
        promptH = promptMin;
        bottomH = std::max(scale(80), available - promptH);
    }
    else
    {
        promptH = std::max(scale(48), available - scale(60));
        bottomH = scale(60);
    }

    const auto place = [](HWND control, int x, int rowY, int w, int h)
    {
        if (control)
            SetWindowPos(control, nullptr, x, rowY, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    const auto placeLabel = [&](HWND control, int x, int rowY, int w)
    {
        place(control, x, rowY + std::max(0, (rowH - labelH) / 2), w, labelH);
    };

    int y = margin;

    placeLabel(g_labels.model, labelX, y, labelW - gap);
    place(GetDlgItem(g_main, IDC_MODEL), fieldX, y, fieldW, rowH);
    y += rowH + gap;

    placeLabel(g_labels.prompt, labelX, y, labelW - gap);
    place(GetDlgItem(g_main, IDC_PROMPT), fieldX, y, fieldW, promptH);
    y += promptH + gap;

    placeLabel(g_labels.width, labelX, y, labelW - gap);
    place(GetDlgItem(g_main, IDC_WIDTH), fieldX, y, smallField, rowH);

    const int heightLabelX = fieldX + smallField + gap;
    placeLabel(g_labels.height, heightLabelX, y, scale(56));
    const int heightFieldX = heightLabelX + scale(58);
    place(GetDlgItem(g_main, IDC_HEIGHT), heightFieldX, y, smallField, rowH);

    const int countLabelX = heightFieldX + smallField + gap + scale(4);
    placeLabel(g_labels.count, countLabelX, y, scale(56));
    const int countFieldX = countLabelX + scale(58);
    place(GetDlgItem(g_main, IDC_COUNT), countFieldX, y, scale(56), rowH);
    y += rowH + gap;

    placeLabel(g_labels.quality, fieldX, y, std::max(scale(50), labelX + labelW - fieldX - gap));
    const int qualityX = fieldX + scale(64);
    place(GetDlgItem(g_main, IDC_QUALITY), qualityX, y, scale(96), scale(160));

    const int formatLabelX = qualityX + scale(104);
    placeLabel(g_labels.format, formatLabelX, y, scale(56));
    const int formatX = formatLabelX + scale(60);
    place(GetDlgItem(g_main, IDC_FORMAT), formatX, y, scale(88), scale(140));

    place(GetDlgItem(g_main, IDC_TRANSPARENT), formatX + scale(98), y + std::max(0, (rowH - scale(22)) / 2),
          scale(160), scale(22));
    y += rowH + gap;

    place(GetDlgItem(g_main, IDC_GENERATE), fieldX, y, scale(140), buttonH);
    place(GetDlgItem(g_main, IDC_OPEN_OUTPUT), fieldX + scale(148), y, scale(130), buttonH);
    place(GetDlgItem(g_main, IDC_REFRESH), fieldX + scale(286), y, scale(100), buttonH);
    y += buttonH + gap;

    placeLabel(g_labels.output, labelX, y, labelW - gap);
    place(GetDlgItem(g_main, IDC_OUTPUT_PATH), fieldX, y, fieldW, rowH);
    y += rowH + gap;

    place(GetDlgItem(g_main, IDC_STATUS), labelX, y, width - 2 * margin, labelH);
    y += labelH + gap;

    placeLabel(g_labels.gallery, labelX, y, labelW - gap);

    place(GetDlgItem(g_main, IDC_FILELIST), fieldX, y, listWidth, bottomH);

    const int previewX = fieldX + listWidth + margin;
    const int previewW = std::max(scale(80), width - previewX - margin);
    place(GetDlgItem(g_main, IDC_PREVIEW), previewX, y, previewW, bottomH);
}

void onCommand(HWND hwnd, int id, int code)
{
    switch (id)
    {
        case IDC_GENERATE:
            if (code == BN_CLICKED)
                startGeneration();
            break;

        case IDC_OPEN_OUTPUT:
            if (code == BN_CLICKED)
                util::openFolder(outputDir());
            break;

        case IDC_REFRESH:
            if (code == BN_CLICKED)
            {
                refreshGallery();
                setStatus(L"Галерея обновлена");
            }
            break;

        case IDC_FILELIST:
            if (code == LBN_SELCHANGE)
            {
                const int index = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_FILELIST), LB_GETCURSEL, 0, 0));
                showImageAt(index);
            }
            break;

        case IDM_OPEN_OUTPUT:
            util::openFolder(outputDir());
            break;

        case IDM_OPEN_CONFIG:
        {
            std::filesystem::path configPath = g_configPath;
            ShellExecuteW(nullptr, L"open", L"notepad.exe", configPath.wstring().c_str(), nullptr, SW_SHOWNORMAL);
            break;
        }

        case IDM_RELOAD:
        {
            std::string error;
            config::load(g_configPath, g_config, error);
            populateFields();
            refreshGallery();
            setStatus(error.empty() ? L"Конфиг перезагружен" : L"Ошибка: " + util::utf8ToWide(error));
            break;
        }

        case IDM_GENERATE:
            startGeneration();
            break;

        case IDM_ABOUT:
            MessageBoxW(hwnd,
                        L"Nanobanana GUI 0.1\n\nОболочка для генерации изображений через OpenAI-совместимый API.\n"
                        L"Настройки: config.ini рядом с исполняемым файлом.\n"
                        L"Результаты: папка Output.",
                        L"О программе",
                        MB_OK | MB_ICONINFORMATION);
            break;

        case IDM_EXIT:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;

        default:
            break;
    }
}

void updateModelFromCombo()
{
    HWND  combo  = GetDlgItem(g_main, IDC_MODEL);
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0)
        return;

    wchar_t buffer[256] = {};
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(buffer));
    g_config.model = util::wideToUtf8(buffer);

    std::string saveError;
    config::save(g_configPath, g_config, saveError);
}

void updateSizeFields()
{
    const int width  = getControlInt(GetDlgItem(g_main, IDC_WIDTH), g_config.width);
    const int height = getControlInt(GetDlgItem(g_main, IDC_HEIGHT), g_config.height);
    const int count  = getControlInt(GetDlgItem(g_main, IDC_COUNT), g_config.n);

    if (width >= 64 && width <= 8192)
        g_config.width = width;
    if (height >= 64 && height <= 8192)
        g_config.height = height;
    if (count >= 1 && count <= 10)
        g_config.n = count;

    std::string saveError;
    config::save(g_configPath, g_config, saveError);
}

void updateOptionFields()
{
    const std::string quality = getControlUtf8(GetDlgItem(g_main, IDC_QUALITY));
    const std::string format  = getControlUtf8(GetDlgItem(g_main, IDC_FORMAT));

    if (!quality.empty())
        g_config.quality = quality;
    if (!format.empty())
        g_config.outputFormat = format;

    g_config.transparent = SendDlgItemMessageW(g_main, IDC_TRANSPARENT, BM_GETCHECK, 0, 0) == BST_CHECKED;

    std::string saveError;
    config::save(g_configPath, g_config, saveError);
}

void saveCurrentSettings()
{
    if (!g_main)
        return;

    const std::string model = getControlUtf8(GetDlgItem(g_main, IDC_MODEL));
    if (!model.empty())
        g_config.model = model;

    const int width = getControlInt(GetDlgItem(g_main, IDC_WIDTH), g_config.width);
    const int height = getControlInt(GetDlgItem(g_main, IDC_HEIGHT), g_config.height);
    const int count = getControlInt(GetDlgItem(g_main, IDC_COUNT), g_config.n);

    if (width >= 64 && width <= 8192)
        g_config.width = width;
    if (height >= 64 && height <= 8192)
        g_config.height = height;
    if (count >= 1 && count <= 10)
        g_config.n = count;

    const std::string quality = getControlUtf8(GetDlgItem(g_main, IDC_QUALITY));
    const std::string format  = getControlUtf8(GetDlgItem(g_main, IDC_FORMAT));
    if (!quality.empty())
        g_config.quality = quality;
    if (!format.empty())
        g_config.outputFormat = format;

    g_config.transparent = SendDlgItemMessageW(g_main, IDC_TRANSPARENT, BM_GETCHECK, 0, 0) == BST_CHECKED;

    std::string saveError;
    config::save(g_configPath, g_config, saveError);
}

HMENU buildMenu()
{
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_GENERATE, L"Сгенерировать\tCtrl+G");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_OPEN_OUTPUT, L"Открыть папку Output\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_RELOAD, L"Перезагрузить config.ini\tF5");
    AppendMenuW(file, MF_STRING, IDM_OPEN_CONFIG, L"Открыть config.ini\tCtrl+E");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"Выход\tAlt+F4");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"Файл");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"О программе");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Справка");

    return menu;
}

HACCEL buildAccelerators()
{
    ACCEL entries[] = {
        {FVIRTKEY | FCONTROL, 'G', IDM_GENERATE},
        {FVIRTKEY | FCONTROL, 'O', IDM_OPEN_OUTPUT},
        {FVIRTKEY | FCONTROL, 'E', IDM_OPEN_CONFIG},
        {FVIRTKEY, VK_F5, IDM_RELOAD},
    };

    return CreateAcceleratorTableW(entries, static_cast<int>(std::size(entries)));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
            g_main = hwnd;

            g_dpi = static_cast<int>(GetDpiForSystem());
            if (g_dpi <= 0)
                g_dpi = 96;

            createFonts();
            createControls();
            populateFields();
            refreshGallery();
            if (!g_gallery.empty())
            {
                SendMessageW(GetDlgItem(hwnd, IDC_FILELIST), LB_SETCURSEL, 0, 0);
                showImageAt(0);
                setStatus(L"Готов к работе");
            }
            return 0;

        case WM_SIZE:
            layoutControls();
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = scale(760);
            info->ptMinTrackSize.y = scale(600);
            return 0;
        }

        case WM_DPICHANGED:
        {
            g_dpi = HIWORD(wParam);

            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested)
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);

            createFonts();
            layoutControls();
            return 0;
        }

        case WM_COMMAND:
            onCommand(hwnd, LOWORD(wParam), HIWORD(wParam));
            if (LOWORD(wParam) == IDC_MODEL && HIWORD(wParam) == CBN_SELCHANGE)
                updateModelFromCombo();
            if ((LOWORD(wParam) == IDC_WIDTH || LOWORD(wParam) == IDC_HEIGHT || LOWORD(wParam) == IDC_COUNT) &&
                HIWORD(wParam) == EN_KILLFOCUS)
                updateSizeFields();
            if ((LOWORD(wParam) == IDC_QUALITY || LOWORD(wParam) == IDC_FORMAT) && HIWORD(wParam) == CBN_SELCHANGE)
                updateOptionFields();
            if (LOWORD(wParam) == IDC_TRANSPARENT && HIWORD(wParam) == BN_CLICKED)
                updateOptionFields();
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_F5)
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_RELOAD, 0), 0);
            return 0;

        case WM_APP_RESULT:
        case WM_APP_FAILED:
            collectResult();
            return 0;

        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, IDC_PREVIEW))
                return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
            break;

        case WM_CLOSE:
            saveCurrentSettings();

            if (g_working.load())
            {
                if (MessageBoxW(hwnd, L"Идёт генерация. Всё равно выйти?", L"Подтверждение",
                                MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return 0;

                g_working = false;
            }

            if (g_worker.joinable())
                g_worker.detach();

            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_preview)
                g_preview->destroy();

            if (g_font)
                DeleteObject(g_font);
            if (g_fontBold)
                DeleteObject(g_fontBold);
            if (g_fontMono)
                DeleteObject(g_fontMono);

            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void pumpPreview()
{
    if (!g_preview || !g_preview->windowHandle())
        return;

    g_preview->handleEvents();
    g_preview->render();
}

std::filesystem::path locateConfig(const std::filesystem::path& exeDir)
{
    std::error_code code;

    std::filesystem::path current = exeDir;
    for (int level = 0; level < 3; ++level)
    {
        const std::filesystem::path candidate = current / "config.ini";
        if (std::filesystem::exists(candidate, code))
            return candidate;

        if (!current.has_parent_path() || current.parent_path() == current)
            break;
        current = current.parent_path();
    }

    return exeDir / "config.ini";
}
} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    g_instance = instance;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC  = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    g_baseDir    = util::moduleDir();
    g_configPath = locateConfig(g_baseDir);
    g_baseDir    = g_configPath.parent_path();

    std::string configError;
    config::load(g_configPath, g_config, configError);

    g_dpi = static_cast<int>(GetDpiForSystem());
    if (g_dpi <= 0)
        g_dpi = 96;
    createFonts();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.style         = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc   = windowProc;
    windowClass.hInstance     = instance;
    windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = WindowClass;
    windowClass.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&windowClass))
        return 1;

    const HMENU menu = buildMenu();

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int windowWidth  = std::min(scale(1180), static_cast<int>(workArea.right - workArea.left) - scale(80));
    const int windowHeight = std::min(scale(820), static_cast<int>(workArea.bottom - workArea.top) - scale(80));
    const int windowX      = workArea.left + ((workArea.right - workArea.left) - windowWidth) / 2;
    const int windowY      = workArea.top + ((workArea.bottom - workArea.top) - windowHeight) / 2;

    HWND window = CreateWindowExW(0, WindowClass, WindowTitle,
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                  windowX, windowY, windowWidth, windowHeight,
                                  nullptr, menu, instance, nullptr);

    if (!window)
        return 1;

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    const int actualDpi = static_cast<int>(GetDpiForWindow(window));
    if (actualDpi > 0 && actualDpi != g_dpi)
    {
        g_dpi = actualDpi;
        createFonts();
        layoutControls();
    }

    const HACCEL accelerators = buildAccelerators();

    MSG message{};
    while (true)
    {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
                return static_cast<int>(message.wParam);

            if (!accelerators || !TranslateAcceleratorW(window, accelerators, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        pumpPreview();
        Sleep(1);
    }
}
