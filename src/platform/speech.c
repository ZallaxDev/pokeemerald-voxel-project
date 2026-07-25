// Screen-reader output for the accessible pokeemerald port.
//
// Speaks through the user's running NVDA via its Controller Client DLL, so the
// game uses their configured voice, rate and settings. The DLL is loaded at
// run time: if nvdaControllerClient32.dll is missing, or NVDA isn't running,
// every call here is a harmless no-op and the game plays normally.
//
// This file is deliberately pure Win32 (no game headers) so it stays a thin,
// stable TTS backend. Game-side text decoding lives elsewhere and calls
// Speech_Say() with a finished UTF-8/ASCII string.

#include <windows.h>

typedef unsigned long nvda_status_t;
typedef nvda_status_t(__stdcall *nvdaSpeak_t)(const wchar_t *);
typedef nvda_status_t(__stdcall *nvdaCancel_t)(void);
typedef nvda_status_t(__stdcall *nvdaTest_t)(void);

static HMODULE sNvdaDll;
static nvdaSpeak_t sSpeak;
static nvdaCancel_t sCancel;
static nvdaTest_t sTestRunning;
static int sInitDone;

// Load NVDA's controller client once. Safe to call repeatedly.
void Speech_Init(void)
{
    if (sInitDone)
        return;
    sInitDone = 1;
    sNvdaDll = LoadLibraryA("nvdaControllerClient32.dll");
    if (!sNvdaDll)
        return;
    sSpeak = (nvdaSpeak_t)GetProcAddress(sNvdaDll, "nvdaController_speakText");
    sCancel = (nvdaCancel_t)GetProcAddress(sNvdaDll, "nvdaController_cancelSpeech");
    sTestRunning = (nvdaTest_t)GetProcAddress(sNvdaDll, "nvdaController_testIfRunning");
}

// True only when NVDA is present and actually running right now.
static int NvdaReady(void)
{
    if (!sSpeak)
        return 0;
    if (sTestRunning && sTestRunning() != 0) // non-zero == not running
        return 0;
    return 1;
}

// Speak an ASCII/UTF-8 string. interrupt != 0 cuts off whatever is speaking.
void Speech_Say(const char *text, int interrupt)
{
    wchar_t wbuf[1024];
    int n;

    if (!text || !text[0] || !NvdaReady())
        return;
    n = MultiByteToWideChar(CP_UTF8, 0, text, -1, wbuf, 1024);
    if (n <= 0)
    {
        // Fall back to treating the bytes as Latin-1 if UTF-8 decoding failed.
        n = MultiByteToWideChar(CP_ACP, 0, text, -1, wbuf, 1024);
        if (n <= 0)
            return;
    }
    if (interrupt && sCancel)
        sCancel();
    sSpeak(wbuf);
}

// Stop any in-progress speech.
void Speech_Silence(void)
{
    if (sCancel && NvdaReady())
        sCancel();
}
