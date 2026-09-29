// XYGraph.cpp
// LCD Smartie plugin: bar graph tracking a percentage over time.
// Build with MSVC: cl /LD /EHsc /O2 XYGraph.cpp /Fe:XYGraph.dll
//
// Usage:
//   $dll(XYGraph,1,<percentage>,<width>)   ->  channel 1
//   $dll(XYGraph,2,<percentage>,<width>)   ->  channel 2
//   $dll(XYGraph,3,<percentage>,<width>)   ->  channel 3
//   $dll(XYGraph,4,<percentage>,<width>)   ->  channel 4
//
// Each channel keeps its own independent sample history. Use a different
// function number on each screen — for example $dll(XYGraph,1,...) for CPU
// on one screen and $dll(XYGraph,2,...) for RAM on another — so the two
// graphs do not mix their samples together.
//
// Thread safety:
//   Every channel is guarded by its own CRITICAL_SECTION, so concurrent
//   calls from the render thread, the settings preview pane, and the
//   config-save thread cannot corrupt the deque or tear the output string.
//
// Bar rendering:
//   The 8 custom-character slots are used for the 8 possible bar heights,
//   not for individual time slots. Slot N holds a bar that is N rows tall.
//   Each sample is rendered by emitting the $Chr() code for whichever
//   height it needs, so the same custom character is reused many times
//   across the row. A 0% sample is rendered as a space.
//
// Custom-char slot mapping (verified on LCD Smartie 5.6 with desktop.dll):
//   slot 1 -> $Chr(176)   slot 5 -> $Chr(133)
//   slot 2 -> $Chr(158)   slot 6 -> $Chr(134)
//   slot 3 -> $Chr(131)   slot 7 -> $Chr(135)
//   slot 4 -> $Chr(132)   slot 8 -> $Chr(136)

#include <windows.h>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <algorithm>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static const int MAX_HISTORY   = 128;  // samples kept per channel
static const int GRAPH_HEIGHT  = 8;    // pixels per character cell
static const int DEFAULT_WIDTH = 16;   // default graph width
static const int NUM_CHANNELS  = 4;    // one per exported function

static const int CHR_CODES[8] = { 176, 158, 131, 132, 133, 134, 135, 136 };

// ---------------------------------------------------------------------------
// Per-channel state
// ---------------------------------------------------------------------------
struct GraphChannel {
    std::deque<int>  history;
    CRITICAL_SECTION cs;
    bool             csInitialized;
    std::string      result;
};

static GraphChannel g_channels[NUM_CHANNELS];

// ---------------------------------------------------------------------------
// Build a 5x8 character whose bottom `rows_filled` rows are on.
// rows_filled ranges 0..8. Row 0 is the top of the cell, row 7 bottom.
// ---------------------------------------------------------------------------
static std::vector<unsigned char> make_bar_char(int rows_filled)
{
    std::vector<unsigned char> ch(GRAPH_HEIGHT, 0);
    if (rows_filled < 0)            rows_filled = 0;
    if (rows_filled > GRAPH_HEIGHT) rows_filled = GRAPH_HEIGHT;

    for (int i = 0; i < rows_filled; ++i) {
        ch[GRAPH_HEIGHT - 1 - i] = 0x1F;   // all 5 pixels on
    }
    return ch;
}

// ---------------------------------------------------------------------------
// Map a percentage to one of 8 height levels (1..8).
// Returns 0 to indicate "blank" (use a space).
// ---------------------------------------------------------------------------
static int percent_to_level(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    int level = (pct * GRAPH_HEIGHT + 50) / 100;
    if (level < 0)             level = 0;
    if (level > GRAPH_HEIGHT)  level = GRAPH_HEIGHT;

    // Any nonzero reading shows at least one pixel.
    if (level == 0 && pct > 0) level = 1;

    return level;
}

// ---------------------------------------------------------------------------
// Build the output string for one channel.
// Caller must hold the channel's critical section.
// ---------------------------------------------------------------------------
static void build_graph_string(GraphChannel& chan, int width, std::string& out)
{
    if (width < 1)            width = 1;
    if (width > MAX_HISTORY)  width = MAX_HISTORY;

    // Ensure enough history.
    while ((int)chan.history.size() < width) {
        chan.history.push_front(0);
    }

    int start = (int)chan.history.size() - width;

    std::ostringstream oss;

    // Define the 8 custom characters, one per height level 1..8.
    for (int level = 1; level <= GRAPH_HEIGHT; ++level) {
        std::vector<unsigned char> ch = make_bar_char(level);
        oss << "$CustomChar(" << level;
        for (int b = 0; b < GRAPH_HEIGHT; ++b) {
            oss << "," << (int)ch[b];
        }
        oss << ")";
    }

    // Emit one character per sample.
    for (int i = 0; i < width; ++i) {
        int pct   = chan.history[start + i];
        int level = percent_to_level(pct);

        if (level == 0) {
            oss << " ";
        } else {
            oss << "$Chr(" << CHR_CODES[level - 1] << ")";
        }
    }

    out = oss.str();
}

// ---------------------------------------------------------------------------
// Shared entry point for all four channels.
// ---------------------------------------------------------------------------
static char* process_channel(GraphChannel& chan, char* param1, char* param2)
{
    // Defensive lazy init: in case SmartieInit was never called.
    if (!chan.csInitialized) {
        InitializeCriticalSection(&chan.cs);
        chan.csInitialized = true;
    }

    EnterCriticalSection(&chan.cs);

    int pct   = 0;
    int width = DEFAULT_WIDTH;

    if (param1 && *param1) pct   = atoi(param1);
    if (param2 && *param2) width = atoi(param2);

    if (pct < 0)             pct   = 0;
    if (pct > 100)           pct   = 100;
    if (width < 1)           width = 1;
    if (width > MAX_HISTORY) width = MAX_HISTORY;

    chan.history.push_back(pct);
    while ((int)chan.history.size() > MAX_HISTORY) {
        chan.history.pop_front();
    }

    build_graph_string(chan, width, chan.result);

    char* ret = const_cast<char*>(chan.result.c_str());
    LeaveCriticalSection(&chan.cs);
    return ret;
}

// ---------------------------------------------------------------------------
// Exported entry points
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) char* __stdcall function1(char* p1, char* p2)
{
    return process_channel(g_channels[0], p1, p2);
}

extern "C" __declspec(dllexport) char* __stdcall function2(char* p1, char* p2)
{
    return process_channel(g_channels[1], p1, p2);
}

extern "C" __declspec(dllexport) char* __stdcall function3(char* p1, char* p2)
{
    return process_channel(g_channels[2], p1, p2);
}

extern "C" __declspec(dllexport) char* __stdcall function4(char* p1, char* p2)
{
    return process_channel(g_channels[3], p1, p2);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) void __stdcall SmartieInit()
{
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (!g_channels[i].csInitialized) {
            InitializeCriticalSection(&g_channels[i].cs);
            g_channels[i].csInitialized = true;
        }
        EnterCriticalSection(&g_channels[i].cs);
        g_channels[i].history.clear();
        for (int j = 0; j < DEFAULT_WIDTH; ++j) {
            g_channels[i].history.push_back(0);
        }
        g_channels[i].result.clear();
        LeaveCriticalSection(&g_channels[i].cs);
    }
}

extern "C" __declspec(dllexport) void __stdcall SmartieFini()
{
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (g_channels[i].csInitialized) {
            EnterCriticalSection(&g_channels[i].cs);
            g_channels[i].history.clear();
            g_channels[i].result.clear();
            LeaveCriticalSection(&g_channels[i].cs);
            DeleteCriticalSection(&g_channels[i].cs);
            g_channels[i].csInitialized = false;
        }
    }
}

extern "C" __declspec(dllexport) int __stdcall GetMinRefreshInterval()
{
    return 500;
}
