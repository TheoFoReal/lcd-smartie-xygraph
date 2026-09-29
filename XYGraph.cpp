// XYGraph.cpp
// LCD Smartie plugin: bar graph tracking a percentage over time.
// Build with MSVC: cl /LD /EHsc /O2 XYGraph.cpp /Fe:XYGraph.dll
//
// Usage:
//   $dll(XYGraph,1,<percentage>,<width>)     ->  channel 1
//   $dll(XYGraph,2,<percentage>,<width>)     ->  channel 2
//   ...
//   $dll(XYGraph,99,<percentage>,<width>)    ->  channel 99
//
// Each channel keeps its own independent sample history. Use a different
// function number on each screen so the graphs do not mix samples.
//
// Thread safety:
//   Every channel is guarded by its own CRITICAL_SECTION, so concurrent
//   calls from the render thread, the settings preview pane, and the
//   config-save thread cannot corrupt the deque or tear the output string.
//
// Bar rendering:
//   Each character cell is 8 pixels tall. Row 0 (the top) is always on
//   and forms a continuous horizontal line across the graph marking the
//   upper boundary (100%). Rows 1..7 are the bar area; a bar is drawn
//   from the bottom up, so its height is 1..7 rows.
//
//   Every sample shows at least one filled row below the boundary line,
//   including 0%, so the graph always has a visible baseline.
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
static const int BAR_ROWS      = GRAPH_HEIGHT - 1;  // bar area = 7 rows
static const int DEFAULT_WIDTH = 16;   // default graph width
static const int NUM_CHANNELS  = 99;   // one per exported function

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
// Build a 5x8 character:
//   - Row 0 is always on (the boundary line).
//   - Rows 1..7 are the bar area; `level` rows are filled at the bottom.
//   - level ranges 0..BAR_ROWS (0..7).
// ---------------------------------------------------------------------------
static std::vector<unsigned char> make_bar_char(int level)
{
    std::vector<unsigned char> ch(GRAPH_HEIGHT, 0);
    if (level < 0)        level = 0;
    if (level > BAR_ROWS) level = BAR_ROWS;

    // Top row: the upper boundary line, present on every column.
    ch[0] = 0x1F;

    // Fill `level` rows from the bottom of the cell.
    for (int i = 0; i < level; ++i) {
        ch[GRAPH_HEIGHT - 1 - i] = 0x1F;
    }
    return ch;
}

// ---------------------------------------------------------------------------
// Map a percentage to a bar height level (1..BAR_ROWS).
// Every value maps to at least one filled row, including 0%, so the graph
// always shows a visible baseline below the boundary line.
// ---------------------------------------------------------------------------
static int percent_to_level(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    // Map 0-100% onto 1..BAR_ROWS filled rows.
    int range = BAR_ROWS - 1;              // 6 for BAR_ROWS = 7
    int level = 1 + (pct * range + 50) / 100;

    if (level < 1)        level = 1;
    if (level > BAR_ROWS) level = BAR_ROWS;

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

    // Define the custom characters.
    // Slots 1..BAR_ROWS hold levels 1..BAR_ROWS (boundary line + N filled rows).
    for (int level = 1; level <= BAR_ROWS; ++level) {
        std::vector<unsigned char> ch = make_bar_char(level);
        oss << "$CustomChar(" << level;
        for (int b = 0; b < GRAPH_HEIGHT; ++b) {
            oss << "," << (int)ch[b];
        }
        oss << ")";
    }
    // Any remaining slots are defined blank so stale definitions cannot
    // leak through from a previous screen or plugin.
    for (int slot = BAR_ROWS + 1; slot <= GRAPH_HEIGHT; ++slot) {
        oss << "$CustomChar(" << slot;
        for (int b = 0; b < GRAPH_HEIGHT; ++b) {
            oss << ",0";
        }
        oss << ")";
    }

    // Emit one character per sample. Every sample maps to a level 1..BAR_ROWS.
    for (int i = 0; i < width; ++i) {
        int pct   = chan.history[start + i];
        int level = percent_to_level(pct);
        oss << "$Chr(" << CHR_CODES[level - 1] << ")";
    }

    out = oss.str();
}

// ---------------------------------------------------------------------------
// Shared entry point for all channels.
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
// Exported entry points: function1 .. function99
// ---------------------------------------------------------------------------
#define DEFINE_CHANNEL(n)                                                         \
    extern "C" __declspec(dllexport) char* __stdcall function##n(char* p1, char* p2) \
    {                                                                             \
        return process_channel(g_channels[(n) - 1], p1, p2);                      \
    }

DEFINE_CHANNEL(1)
DEFINE_CHANNEL(2)
DEFINE_CHANNEL(3)
DEFINE_CHANNEL(4)
DEFINE_CHANNEL(5)
DEFINE_CHANNEL(6)
DEFINE_CHANNEL(7)
DEFINE_CHANNEL(8)
DEFINE_CHANNEL(9)
DEFINE_CHANNEL(10)
DEFINE_CHANNEL(11)
DEFINE_CHANNEL(12)
DEFINE_CHANNEL(13)
DEFINE_CHANNEL(14)
DEFINE_CHANNEL(15)
DEFINE_CHANNEL(16)
DEFINE_CHANNEL(17)
DEFINE_CHANNEL(18)
DEFINE_CHANNEL(19)
DEFINE_CHANNEL(20)
DEFINE_CHANNEL(21)
DEFINE_CHANNEL(22)
DEFINE_CHANNEL(23)
DEFINE_CHANNEL(24)
DEFINE_CHANNEL(25)
DEFINE_CHANNEL(26)
DEFINE_CHANNEL(27)
DEFINE_CHANNEL(28)
DEFINE_CHANNEL(29)
DEFINE_CHANNEL(30)
DEFINE_CHANNEL(31)
DEFINE_CHANNEL(32)
DEFINE_CHANNEL(33)
DEFINE_CHANNEL(34)
DEFINE_CHANNEL(35)
DEFINE_CHANNEL(36)
DEFINE_CHANNEL(37)
DEFINE_CHANNEL(38)
DEFINE_CHANNEL(39)
DEFINE_CHANNEL(40)
DEFINE_CHANNEL(41)
DEFINE_CHANNEL(42)
DEFINE_CHANNEL(43)
DEFINE_CHANNEL(44)
DEFINE_CHANNEL(45)
DEFINE_CHANNEL(46)
DEFINE_CHANNEL(47)
DEFINE_CHANNEL(48)
DEFINE_CHANNEL(49)
DEFINE_CHANNEL(50)
DEFINE_CHANNEL(51)
DEFINE_CHANNEL(52)
DEFINE_CHANNEL(53)
DEFINE_CHANNEL(54)
DEFINE_CHANNEL(55)
DEFINE_CHANNEL(56)
DEFINE_CHANNEL(57)
DEFINE_CHANNEL(58)
DEFINE_CHANNEL(59)
DEFINE_CHANNEL(60)
DEFINE_CHANNEL(61)
DEFINE_CHANNEL(62)
DEFINE_CHANNEL(63)
DEFINE_CHANNEL(64)
DEFINE_CHANNEL(65)
DEFINE_CHANNEL(66)
DEFINE_CHANNEL(67)
DEFINE_CHANNEL(68)
DEFINE_CHANNEL(69)
DEFINE_CHANNEL(70)
DEFINE_CHANNEL(71)
DEFINE_CHANNEL(72)
DEFINE_CHANNEL(73)
DEFINE_CHANNEL(74)
DEFINE_CHANNEL(75)
DEFINE_CHANNEL(76)
DEFINE_CHANNEL(77)
DEFINE_CHANNEL(78)
DEFINE_CHANNEL(79)
DEFINE_CHANNEL(80)
DEFINE_CHANNEL(81)
DEFINE_CHANNEL(82)
DEFINE_CHANNEL(83)
DEFINE_CHANNEL(84)
DEFINE_CHANNEL(85)
DEFINE_CHANNEL(86)
DEFINE_CHANNEL(87)
DEFINE_CHANNEL(88)
DEFINE_CHANNEL(89)
DEFINE_CHANNEL(90)
DEFINE_CHANNEL(91)
DEFINE_CHANNEL(92)
DEFINE_CHANNEL(93)
DEFINE_CHANNEL(94)
DEFINE_CHANNEL(95)
DEFINE_CHANNEL(96)
DEFINE_CHANNEL(97)
DEFINE_CHANNEL(98)
DEFINE_CHANNEL(99)

#undef DEFINE_CHANNEL

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
