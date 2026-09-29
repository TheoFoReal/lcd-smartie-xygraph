// XYGraph.cpp
// LCD Smartie plugin: bar graph tracking a percentage over time.
// Build with MSVC: cl /LD /EHsc /O2 XYGraph.cpp /Fe:XYGraph.dll
//
// Usage in LCD Smartie:
//   $dll(XYGraph,1,<percentage>,<graph_width>)
//   Example: $dll(XYGraph,1,75,4)  -> 4-bar-wide graph
//
// Diagnostic mode:
//   $dll(XYGraph,1,?,4)  ->  echoes back the parameters it received
//   Example output:  P1=[?] P2=[4]
//   If P2 shows "(null)" or "[]", LCD Smartie is not forwarding param2.

#include <windows.h>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <algorithm>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static const int MAX_HISTORY  = 64;   // maximum samples kept in memory
static const int GRAPH_HEIGHT = 8;    // vertical pixels per character cell
static const int MAX_WIDTH    = 8;    // LCD Smartie has 8 custom char slots
static const int DEFAULT_WIDTH = 8;   // used when param2 is missing/empty

// ---------------------------------------------------------------------------
// Internal state (persists between calls)
// ---------------------------------------------------------------------------
static std::deque<int> g_history;     // percentage values (0-100)

// ---------------------------------------------------------------------------
// Helper: build a single 5x8 custom character that draws a vertical bar
// filled from the bottom up. Row 0 is the top of the cell, row 7 is the
// bottom. `filled` rows are drawn, starting at the bottom.
// ---------------------------------------------------------------------------
static std::vector<unsigned char> make_bar_char(int pct)
{
    std::vector<unsigned char> ch(GRAPH_HEIGHT, 0);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    // Number of rows to fill from the bottom: 0..8.
    // The +50 rounds to nearest so 50% lands on 4 rows, not 3.
    int filled = (pct * GRAPH_HEIGHT + 50) / 100;
    if (filled < 0) filled = 0;
    if (filled > GRAPH_HEIGHT) filled = GRAPH_HEIGHT;

    for (int i = 0; i < filled; ++i) {
        ch[GRAPH_HEIGHT - 1 - i] = 0x1F;  // all 5 pixels wide
    }
    return ch;
}

// ---------------------------------------------------------------------------
// Helper: build the complete $CustomChar()/$Chr() string for a bar graph of
// `width` columns.
// ---------------------------------------------------------------------------
static std::string build_graph_string(int width)
{
    if (width < 1)         width = 1;
    if (width > MAX_WIDTH) width = MAX_WIDTH;

    // Ensure we have enough history.
    while ((int)g_history.size() < width) {
        g_history.push_front(0);
    }

    // Take the last `width` samples.
    std::vector<int> samples;
    for (int i = (int)g_history.size() - width; i < (int)g_history.size(); ++i) {
        samples.push_back(g_history[i]);
    }

    std::ostringstream oss;

    for (int i = 0; i < width; ++i) {
        std::vector<unsigned char> ch = make_bar_char(samples[i]);

        // Emit $CustomChar(n, b0, b1, ..., b7)  with n = i+1.
        oss << "$CustomChar(" << (i + 1);
        for (int b = 0; b < GRAPH_HEIGHT; ++b) {
            oss << "," << (int)ch[b];
        }
        oss << ")";
    }

    // Emit the characters themselves. Slot N is displayed with $Chr(N-1).
    for (int i = 0; i < width; ++i) {
        oss << "$Chr(" << i << ")";
    }

    return oss.str();
}

// ---------------------------------------------------------------------------
// Exported plugin function
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) char* __stdcall function1(char* param1, char* param2)
{
    static std::string result;

    // --- Diagnostic mode -------------------------------------------------
    // If param1 is exactly "?", echo back the received parameters so you
    // can verify what LCD Smartie is actually handing to the plugin.
    // Trigger with: $dll(XYGraph,1,?,4)
    if (param1 != NULL && param1[0] == '?' && param1[1] == '\0') {
        std::ostringstream d;
        d << "P1=[";
        d << (param1 ? param1 : "NULL");
        d << "] P2=[";
        d << ((param2 && param2[0]) ? param2 : "EMPTY");
        d << "]";
        result = d.str();
        return const_cast<char*>(result.c_str());
    }

    // --- Normal mode -----------------------------------------------------
    int pct   = 0;
    int width = DEFAULT_WIDTH;

    if (param1 && *param1) {
        pct = atoi(param1);
    }
    if (param2 && *param2) {
        width = atoi(param2);
    }

    // Clamp
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    if (width < 1) width = 1;
    if (width > MAX_WIDTH) width = MAX_WIDTH;

    // Push new sample.
    g_history.push_back(pct);
    while ((int)g_history.size() > MAX_HISTORY) {
        g_history.pop_front();
    }

    result = build_graph_string(width);
    return const_cast<char*>(result.c_str());
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) void __stdcall SmartieInit()
{
    g_history.clear();
    for (int i = 0; i < MAX_WIDTH; ++i) {
        g_history.push_back(0);
    }
}

extern "C" __declspec(dllexport) void __stdcall SmartieFini()
{
    g_history.clear();
}

extern "C" __declspec(dllexport) int __stdcall GetMinRefreshInterval()
{
    return 500;
}
