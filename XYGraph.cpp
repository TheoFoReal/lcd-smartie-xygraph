// XYGraph.cpp
// LCD Smartie plugin: bar graph tracking a percentage over time.
// Build with MSVC: cl /LD /EHsc /O2 XYGraph.cpp /Fe:XYGraph.dll
//
// Usage:
//   $dll(XYGraph,1,<percentage>,<width>)              ->  channel 1
//   $dll(XYGraph,1,<percentage>,<width>/<barwidth>)   ->  channel 1, custom bar width
//   ...
//   $dll(XYGraph,99,<percentage>,<width>)             ->  channel 99
//
// param1 : percentage value (0-100)
// param2 : <graph width>[/<bar width>]
//
//   graph width : number of samples (columns) to display
//                 (1..MAX_HISTORY; typically the width of your LCD row)
//   bar width   : optional, 1..3
//                   1 = 1 pixel wide
//                   2 = 3 pixels wide
//                   3 = 5 pixels wide   [default when no '/' is present]
//                 Bars are always centred within the 5-pixel cell.
//
// Examples:
//   $dll(XYGraph,1,$CPU%,16)       -> 16 columns, 5-pixel-wide bars
//   $dll(XYGraph,1,$CPU%,16/1)     -> 16 columns, 1-pixel-wide bars
//   $dll(XYGraph,1,$CPU%,16/2)     -> 16 columns, 3-pixel-wide bars
//   $dll(XYGraph,1,$CPU%,16/3)     -> 16 columns, 5-pixel-wide bars (touching)
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
//   Each character cell is 8 pixels tall and 5 pixels wide.
//
//   A bar is drawn from the bottom up. Its width is one of:
//     1 pixel  -> 0b00100 (bit 2)
//     3 pixels -> 0b01110 (bits 1, 2, 3)
//     5 pixels -> 0b11111 (bits 0..4)   [default]
//
//   A bar's height ranges from 1 to 8 rows, so the vertical resolution
//   is 8 levels. Every sample shows at least one filled row, including
//   0%, so the graph always has a visible baseline.
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
#include <string.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static const int MAX_HISTORY   = 128;  // samples kept per channel
static const int GRAPH_HEIGHT  = 8;    // pixels per character cell
static const int DEFAULT_WIDTH = 16;   // default graph width
static const int DEFAULT_BARW  = 3;    // default bar width index (5 pixels)
static const int NUM_CHANNELS  = 99;   // one per exported function

// Pixel patterns for the three selectable bar widths.
// Index 0 is unused; indices 1..3 match the param2 "/N" suffix.
static const unsigned char BAR_PATTERNS[4] = {
    0x00,  // (unused)
    0x04,  // 1 pixel:  0b00100
    0x0E,  // 3 pixels: 0b01110
    0x1F   // 5 pixels: 0b11111
};

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
// Build a 5x8 character whose bottom `level` rows are filled with the
// given pixel pattern. level ranges 1..GRAPH_HEIGHT.
// ---------------------------------------------------------------------------
static std::vector<unsigned char> make_bar_char(int level, unsigned char pattern)
{
    std::vector<unsigned char> ch(GRAPH_HEIGHT, 0);
    if (level < 1)             level = 1;
    if (level > GRAPH_HEIGHT)  level = GRAPH_HEIGHT;

    for (int i = 0; i < level; ++i) {
        ch[GRAPH_HEIGHT - 1 - i] = pattern;
    }
    return ch;
}

// ---------------------------------------------------------------------------
// Map a percentage to a bar height level (1..GRAPH_HEIGHT).
// Every value maps to at least one filled row, including 0%, so the graph
// always shows a visible baseline.
// ---------------------------------------------------------------------------
static int percent_to_level(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    int range = GRAPH_HEIGHT - 1;               // 7
    int level = 1 + (pct * range + 50) / 100;

    if (level < 1)             level = 1;
    if (level > GRAPH_HEIGHT)  level = GRAPH_HEIGHT;

    return level;
}

// ---------------------------------------------------------------------------
// Build the output string for one channel.
// Caller must hold the channel's critical section.
// ---------------------------------------------------------------------------
static void build_graph_string(GraphChannel& chan, int width, int bar_width,
                               std::string& out)
{
    if (width < 1)            width = 1;
    if (width > MAX_HISTORY)  width = MAX_HISTORY;

    if (bar_width < 1) bar_width = 1;
    if (bar_width > 3) bar_width = 3;

    const unsigned char pattern = BAR_PATTERNS[bar_width];

    // Ensure enough history.
    while ((int)chan.history.size() < width) {
        chan.history.push_front(0);
    }

    int start = (int)chan.history.size() - width;

    std::ostringstream oss;

    // Define the custom characters: one per bar height level, 1..8.
    for (int level = 1; level <= GRAPH_HEIGHT; ++level) {
        std::vector<unsigned char> ch = make_bar_char(level, pattern);
        oss << "$CustomChar(" << level;
        for (int b = 0; b < GRAPH_HEIGHT; ++b) {
            oss << "," << (int)ch[b];
        }
        oss << ")";
    }

    // Emit one character per sample. Every sample maps to a level 1..8.
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

    int pct       = 0;
    int width     = DEFAULT_WIDTH;
    int bar_width = DEFAULT_BARW;

    if (param1 && *param1) pct = atoi(param1);

    // --- Parse param2 as "<width>[/<bar_width>]" ------------------------
    if (param2 && *param2) {
        char buf[64];
        strncpy(buf, param2, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char* slash = strchr(buf, '/');
        if (slash != NULL) {
            *slash = '\0';
            int w = atoi(buf);
            if (w > 0) width = w;

            int bw = atoi(slash + 1);
            if (bw >= 1 && bw <= 3) bar_width = bw;
        } else {
            int w = atoi(buf);
            if (w > 0) width = w;
        }
    }

    // --- Clamp ----------------------------------------------------------
    if (pct < 0)             pct       = 0;
    if (pct > 100)           pct       = 100;
    if (width < 1)           width     = 1;
    if (width > MAX_HISTORY) width     = MAX_HISTORY;
    if (bar_width < 1)       bar_width = 1;
    if (bar_width > 3)       bar_width = 3;

    chan.history.push_back(pct);
    while ((int)chan.history.size() > MAX_HISTORY) {
        chan.history.pop_front();
    }

    build_graph_string(chan, width, bar_width, chan.result);

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
