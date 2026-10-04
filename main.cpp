// language: C++17, file: main.cpp, app: coinvault — crypto portfolio tracker
// Windows 11, MSVC, WinAPI + WinHTTP. No external dependencies.
// Fetches live prices from the public CoinGecko API and shows the
// portfolio in a ListView: amount, price, 24h change, value.
#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")

struct Holding {
    const wchar_t* symbol;
    const wchar_t* coinId;
    double amount;
};

// Demo portfolio — edit these lines to match your own holdings.
static Holding g_holdings[] = {
    { L"BTC",  L"bitcoin",   0.50 },
    { L"ETH",  L"ethereum",  4.00 },
    { L"SOL",  L"solana",   20.00 },
    { L"LINK", L"chainlink", 150.0 },
};

static HWND g_list, g_total, g_btn;
static std::vector<double> g_prices, g_changes;

// Minimal JSON field extractor: finds "usd":<num> and "usd_24h_change":<num>
static double ExtractNumber(const std::string& json, const std::string& field) {
    size_t p = json.find("\"" + field + "\":");
    if (p == std::string::npos) return 0.0;
    p += field.size() + 3;
    return std::stod(json.substr(p, json.find_first_of(",}", p) - p));
}

// HTTPS GET via WinHTTP — no third-party libraries.
static std::string HttpsGet(const std::wstring& host, const std::wstring& path) {
    std::string out;
    HINTERNET sess = WinHttpOpen(L"coinvault/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) return out;
    HINTERNET conn = WinHttpConnect(sess, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (conn) {
        HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
        if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                && WinHttpReceiveResponse(req, nullptr)) {
            DWORD bytes = 0;
            char buf[8192];
            for (;;) {
                if (!WinHttpReadData(req, buf, sizeof(buf), &bytes) || bytes == 0) break;
                out.append(buf, bytes);
            }
        }
        if (req) WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(sess);
    return out;
}

// Worker thread: one CoinGecko call for all ids, then post back to UI.
static DWORD WINAPI FetchPrices(LPVOID) {
    std::wstring ids;
    for (size_t i = 0; i < std::size(g_holdings); ++i) {
        if (i) ids += L",";
        ids += g_holdings[i].coinId;
    }
    std::wstring path = L"/api/v3/simple/price?ids=" + ids +
                        L"&vs_currencies=usd&include_24hr_change=true";
    std::string body = HttpsGet(L"api.coingecko.com", path);
    if (body.empty()) { PostMessage(g_list, WM_APP + 1, 0, 0); return 0; }

    g_prices.resize(std::size(g_holdings));
    g_changes.resize(std::size(g_holdings));
    for (size_t i = 0; i < std::size(g_holdings); ++i) {
        std::string id = std::string(g_holdings[i].coinId, g_holdings[i].coinId + wcslen(g_holdings[i].coinId));
        g_prices[i]  = ExtractNumber(body, id + "\":{\"usd\"");
        g_changes[i] = ExtractNumber(body, "usd_24h_change");
    }
    PostMessage(g_list, WM_APP + 1, 0, 0);
    return 0;
}

static void RefreshList() {
    ListView_DeleteAllItems(g_list);
    double total = 0;
    wchar_t buf[64];
    LVITEMW it{};
    for (size_t i = 0; i < std::size(g_holdings) && i < g_prices.size(); ++i) {
        double value = g_prices[i] * g_holdings[i].amount;
        total += value;
        it.mask = LVIF_TEXT; it.iItem = (int)i;
        it.pszText = (LPWSTR)g_holdings[i].symbol; it.iItem = ListView_InsertItem(g_list, &it);
        swprintf(buf, 64, L"%.6f", g_holdings[i].amount);
        ListView_SetItemText(g_list, it.iItem, 1, buf);
        swprintf(buf, 64, L"$%.2f", g_prices[i]);
        ListView_SetItemText(g_list, it.iItem, 2, buf);
        swprintf(buf, 64, L"%+.2f%%", g_changes[i]);
        ListView_SetItemText(g_list, it.iItem, 3, buf);
        swprintf(buf, 64, L"$%.2f", value);
        ListView_SetItemText(g_list, it.iItem, 4, buf);
    }
    swprintf(buf, 64, L"Total: $%.2f", total);
    SetWindowTextW(g_total, buf);
}

static LRESULT CALLBACK WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icc);
        g_btn = CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              16, 16, 120, 34, w, (HMENU)1, nullptr, nullptr);
        g_total = CreateWindowW(L"STATIC", L"Total: —", WS_CHILD | WS_VISIBLE,
                                152, 22, 400, 24, w, nullptr, nullptr, nullptr);
        g_list = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                 16, 64, 720, 380, w, (HMENU)2, nullptr, nullptr);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        const wchar_t* heads[] = { L"Asset", L"Amount", L"Price", L"24h", L"Value" };
        int widths[] = { 100, 140, 140, 120, 200 };
        for (int i = 0; i < 5; ++i) {
            col.pszText = (LPWSTR)heads[i]; col.cx = widths[i];
            ListView_InsertColumn(g_list, i, &col);
        }
        CreateThread(nullptr, 0, FetchPrices, nullptr, 0, nullptr);
        return 0;
    }
    case WM_APP + 1: RefreshList(); return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == 1) CreateThread(nullptr, 0, FetchPrices, nullptr, 0, nullptr);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(13, 17, 23));
    wc.lpszClassName = L"CoinVaultWnd";
    RegisterClassW(&wc);

    HWND w = CreateWindowExW(0, L"CoinVaultWnd", L"CoinVault — crypto portfolio tracker",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 770, 500,
                             nullptr, nullptr, inst, nullptr);
    ShowWindow(w, show);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
