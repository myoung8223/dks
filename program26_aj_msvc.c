// ==================================================================================================
// Digispark Keyboard Scripter IDE
// Copyright (c) 2026 Mike Young
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
// ==================================================================================================

// ==================================================================================================
// MSVC compile command: cl /nologo /W3 /O2 /MT /std:c17 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE dks.c /Fe:dks.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comdlg32.lib shell32.lib kernel32.lib
// ==================================================================================================

// ============================================================================
// Digispark Payload IDE & Transpiler
// ============================================================================

#include <windows.h>
#include <commdlg.h>   // GetOpenFileNameA / GetSaveFileNameA / OPENFILENAMEA + OFN_* flags
#include <shellapi.h>  // ShellExecuteA
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

// ============================================================================
// --- APPLICATION CONSTANTS & GLOBALS ---
// ============================================================================

// Menu Event IDs
#define IDM_NEW 1001
#define IDM_OPEN 1002
#define IDM_SAVE 1003
#define IDM_FLASH 1004
#define IDM_TOGGLE_DARK 1005
#define IDM_TOGGLE_WRAP 1006
#define IDM_SAVE_AS 1007
#define IDM_ABOUT 1008

// Typing-delay preset menu IDs (safePrint inter-key delay)
#define IDM_DELAY_0  1010
#define IDM_DELAY_1  1011
#define IDM_DELAY_5  1012
#define IDM_DELAY_10 1013
#define IDM_DELAY_15 1014

// About-dialog control IDs
#define IDC_ABOUT_VISIT 2001
#define IDC_ABOUT_CLOSE 2002

// Application version string (shown in the About box; bump this on each release)
#define APP_VERSION "26"

// GitHub repository URL (shown in the About box and opened by the browser prompt)
#define GITHUB_URL "https://github.com/myoung8223/dks"

// Practical flash ceiling (bytes). The Micronucleus bootloader actually on the
// device can leave LESS usable flash than arduino-cli / the core reports as its
// "Maximum", so a sketch that "fits" per the compiler can still fail to upload
// or run. Sketches larger than this are blocked before upload. Adjust to match
// the bootloader on your hardware.
#define SAFE_FLASH_MAX 6012

// USB HID Modifier Bitmasks (Used by DigiKeyboard)
#define MOD_CONTROL_LEFT  (1 << 0)
#define MOD_SHIFT_LEFT    (1 << 1)
#define MOD_ALT_LEFT      (1 << 2)
#define MOD_GUI_LEFT      (1 << 3)

// Global Handles
HWND hEdit;                         // The main text editor window
char currentFile[MAX_PATH] = "";    // Tracks the currently opened file path
char iniPath[MAX_PATH] = "";        // Tracks the path to the settings.ini file
HBRUSH hEditBkBrush = NULL;         // Brush for Dark Mode background
WNDPROC oldEditProc = NULL;         // Subclass window procedure pointer for the editor
HMENU gDelayMenu = NULL;            // Handle to the "Typing Delay" submenu (for radio checks)

int isDarkMode = 0;                 // Dark mode toggle state
int isWordWrap = 1;                 // Word wrap toggle state
int safePrintDelay = 15;            // Inter-key delay (ms) injected into safePrint; 0 = none

// ============================================================================
// --- CONFIGURATION HELPERS ---
// ============================================================================

// Builds the absolute path to settings.ini in the same folder as the executable
void InitIniPath() {
    GetModuleFileName(NULL, iniPath, MAX_PATH);
    char *lastSlash = strrchr(iniPath, '\\');
    if (lastSlash) {
        strcpy(lastSlash + 1, "settings.ini");
    }
}

// Forces the process working directory to the folder containing the EXE, so
// every relative path the IDE relies on -- arduino-cli.yaml, ./portable_data,
// sketch\, temp_payload.txt -- resolves against the EXE's own folder instead of
// whatever directory the IDE happened to be launched from. Without this, running
// the app from a shortcut or a different folder makes arduino-cli miss the yaml
// and fall back to its default (empty) config -> "platform not found" errors.
void SetWorkingDirToExe() {
    char path[MAX_PATH];
    GetModuleFileName(NULL, path, MAX_PATH);
    char *lastSlash = strrchr(path, '\\');
    if (lastSlash) {
        *lastSlash = '\0';
        SetCurrentDirectory(path);
    }
}

// Reads settings from the INI file
void LoadSettings() {
    isDarkMode = GetPrivateProfileInt("Settings", "DarkMode", 0, iniPath);
    isWordWrap = GetPrivateProfileInt("Settings", "WordWrap", 1, iniPath);
    safePrintDelay = GetPrivateProfileInt("Settings", "SafePrintDelay", 15, iniPath);
}

// Writes settings to the INI file
void SaveSettings() {
    char buffer[16];
    sprintf(buffer, "%d", isDarkMode);
    WritePrivateProfileString("Settings", "DarkMode", buffer, iniPath);
    sprintf(buffer, "%d", isWordWrap);
    WritePrivateProfileString("Settings", "WordWrap", buffer, iniPath);
    sprintf(buffer, "%d", safePrintDelay);
    WritePrivateProfileString("Settings", "SafePrintDelay", buffer, iniPath);
}

// ============================================================================
// --- STRING UTILITIES ---
// ============================================================================

// Helper: Aggressive Trim
// Removes leading and trailing whitespace/newlines from a string.
char* clean_str(char *str) {
    char *end;
    // Trim leading space
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str; // All spaces?
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// Helper: Writes a C-string-escaped copy of src to the output file.
// Ensures backslashes, quotes, tabs, and newlines in a payload survive
// intact when embedded inside a "..." literal in the generated sketch.
void fputs_escaped(const char *src, FILE *out) {
    for (const char *p = src; *p != '\0'; p++) {
        switch (*p) {
            case '\\': fputs("\\\\", out); break;
            case '\"': fputs("\\\"", out); break;
            case '\n': fputs("\\n", out);  break;
            case '\r': fputs("\\r", out);  break;
            case '\t': fputs("\\t", out);  break;
            default:   fputc(*p, out);     break;
        }
    }
}

// ============================================================================
// --- TRANSPILER ENGINE ---
// ============================================================================

int transpile(const char* inputFilename) {
    int current_mod = 0;
    int default_delay = 0;

    FILE *in = fopen(inputFilename, "r");
    CreateDirectory("sketch", NULL); 
    FILE *out = fopen("sketch\\sketch.ino", "w"); 
    
    if (!in)  { if (out) fclose(out); return 0; }
    if (!out) { fclose(in); return 0; }

    fprintf(out, "#include \"DigiKeyboard.h\"\n\n");
    fprintf(out, "// --- USB HID Constants (defined only if the core omits them) ---\n");
    fprintf(out, "#ifndef KEY_ENTER\n#define KEY_ENTER 40\n#endif\n");
    fprintf(out, "#ifndef KEY_ESC\n#define KEY_ESC 41\n#endif\n");
    fprintf(out, "#ifndef KEY_ESCAPE\n#define KEY_ESCAPE 41\n#endif\n");
    fprintf(out, "#ifndef KEY_BACKSPACE\n#define KEY_BACKSPACE 42\n#endif\n");
    fprintf(out, "#ifndef KEY_TAB\n#define KEY_TAB 43\n#endif\n");
    fprintf(out, "#ifndef KEY_SPACE\n#define KEY_SPACE 44\n#endif\n");
    fprintf(out, "#ifndef KEY_MINUS\n#define KEY_MINUS 45\n#endif\n");
    fprintf(out, "#ifndef KEY_EQUAL\n#define KEY_EQUAL 46\n#endif\n");
    fprintf(out, "#ifndef KEY_DELETE\n#define KEY_DELETE 76\n#endif\n");
    fprintf(out, "#ifndef KEY_RIGHT\n#define KEY_RIGHT 79\n#endif\n");
    fprintf(out, "#ifndef KEY_LEFT\n#define KEY_LEFT 80\n#endif\n");
    fprintf(out, "#ifndef KEY_DOWN\n#define KEY_DOWN 81\n#endif\n");
    fprintf(out, "#ifndef KEY_UP\n#define KEY_UP 82\n#endif\n\n");

    fprintf(out, "// Helper: Rate-limits typing to prevent dropped keystrokes (PROGMEM optimized)\n");
    fprintf(out, "void safePrint(const __FlashStringHelper* text) {\n");
    fprintf(out, "  const char *p = (const char *)text;\n");
    fprintf(out, "  while (true) {\n");
    fprintf(out, "    unsigned char c = pgm_read_byte(p++);\n");
    fprintf(out, "    if (c == 0) break;\n");
    fprintf(out, "    DigiKeyboard.print((char)c);\n");
    if (safePrintDelay > 0)
        fprintf(out, "    DigiKeyboard.delay(%d);\n", safePrintDelay);
    fprintf(out, "  }\n");
    fprintf(out, "}\n\n");

    fprintf(out, "// Helper: Blinks N times, then pauses 2s, repeating until Pin 2 goes LOW\n");
    fprintf(out, "void waitForButtonBlink(int count) {\n");
    fprintf(out, "  while(digitalRead(2) == HIGH) {\n");
    fprintf(out, "    for(int i=0; i < count; i++) {\n");
    fprintf(out, "      digitalWrite(1, HIGH); DigiKeyboard.delay(200);\n");
    fprintf(out, "      digitalWrite(1, LOW);  DigiKeyboard.delay(200);\n");
    fprintf(out, "      if(digitalRead(2) == LOW) return;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    for(int j=0; j < 40; j++) {\n");
    fprintf(out, "      DigiKeyboard.delay(50); DigiKeyboard.update();\n");
    fprintf(out, "      if(digitalRead(2) == LOW) return;\n");
    fprintf(out, "    }\n");
    fprintf(out, "  }\n");
    fprintf(out, "  DigiKeyboard.delay(200);\n");
    fprintf(out, "}\n\n");
    
    fprintf(out, "void setup() {\n");
    fprintf(out, "  DigiKeyboard.sendKeyStroke(0);\n");
    fprintf(out, "  DigiKeyboard.delay(500);\n");
    fprintf(out, "  pinMode(1, OUTPUT);\n");
    fprintf(out, "  pinMode(2, INPUT_PULLUP);\n");
    fprintf(out, "}\n\nvoid loop() {\n");
    fprintf(out, "  DigiKeyboard.update();\n  DigiKeyboard.delay(3000);\n\n");

    char line[256];
    while (fgets(line, sizeof(line), in)) {
        char temp[256];
        strcpy(temp, line);
        
        int in_quotes = 0;
        for (int i = 0; temp[i] != '\0'; i++) {
            if (temp[i] == '\"') in_quotes = !in_quotes;
            if (temp[i] == '#' && !in_quotes) {
                temp[i] = '\0';
                break;
            }
        }

        char *trimmed = clean_str(temp);
        if (trimmed[0] == '\0') continue;

        char cmd[256];
        strcpy(cmd, trimmed);
        _strupr(cmd); 

        int is_keyboard_action = 0;

        if (strncmp(cmd, "DEFAULTDELAY", 12) == 0) {
            sscanf(cmd, "DEFAULTDELAY %d", &default_delay);
            continue;
        }

        if (strncmp(cmd, "STRING(", 7) == 0) {
            char *start = strchr(trimmed, '\"');
            char *end = strrchr(trimmed, '\"');

            if (start && end && start != end) {
                *end = '\0';
                fputs("  safePrint(F(\"", out);
                fputs_escaped(start + 1, out);
                fputs("\"));\n", out);
                is_keyboard_action = 1;
                *end = '\"';
            } else {
                char errorMsg[512];
                sprintf(errorMsg, "Syntax error (missing quotes):\n\n%s", trimmed);
                MessageBox(NULL, errorMsg, "Transpiler Crash", MB_ICONERROR);
                fclose(in); fclose(out); return 0; 
            }
        }
        else if (strncmp(cmd, "KEYDOWN(", 8) == 0) {
            char k1[32];
            if (sscanf(cmd, "KEYDOWN(%31[^)])", k1) >= 1) {
                char *ck1 = clean_str(k1);
                if (strcmp(ck1, "CONTROL") == 0)      current_mod |= MOD_CONTROL_LEFT;
                else if (strcmp(ck1, "ALT") == 0)     current_mod |= MOD_ALT_LEFT;
                else if (strcmp(ck1, "SHIFT") == 0)   current_mod |= MOD_SHIFT_LEFT;
                else if (strcmp(ck1, "GUI") == 0)     current_mod |= MOD_GUI_LEFT;
                else {
                    fprintf(out, "  DigiKeyboard.sendKeyPress(KEY_%s, %d);\n", ck1, current_mod);
                    is_keyboard_action = 0; 
                    goto trigger_delay; 
                }
                fprintf(out, "  DigiKeyboard.sendKeyPress(0, %d);\n", current_mod);
                is_keyboard_action = 0; 
            }
        }
        else if (strncmp(cmd, "KEYUP()", 7) == 0) {
            current_mod = 0; 
            fprintf(out, "  DigiKeyboard.sendKeyPress(0, 0);\n");
            is_keyboard_action = 1;
        }        
        else if (strncmp(cmd, "KEYS(", 5) == 0) {
            char inner[128] = {0};
            if (sscanf(cmd, "KEYS(%127[^)])", inner) == 1) {
                char *t1 = strtok(inner, ",");
                char *t2 = strtok(NULL, ",");
                char *t3 = strtok(NULL, ",");
                
                if (t1) t1 = clean_str(t1);
                if (t2) t2 = clean_str(t2);
                if (t3) t3 = clean_str(t3);

                if (t1 && t2 && t3) {
                    fprintf(out, "  DigiKeyboard.sendKeyPress(KEY_%s, MOD_%s_LEFT | MOD_%s_LEFT);\n", t3, t1, t2);
                    fprintf(out, "  DigiKeyboard.delay(100);\n");
                    fprintf(out, "  DigiKeyboard.sendKeyPress(0, 0);\n"); 
                } else if (t1 && t2) {
                    fprintf(out, "  DigiKeyboard.sendKeyStroke(KEY_%s, MOD_%s_LEFT);\n", t2, t1);
                } else if (t1) {
                    fprintf(out, "  DigiKeyboard.sendKeyStroke(KEY_%s);\n", t1);
                }
                is_keyboard_action = 1;
            }
        }
        else if (strncmp(cmd, "KEY(", 4) == 0) {
            char k1[32];
            if (sscanf(cmd, "KEY(%31[^)])", k1) >= 1) {
                if (current_mod > 0) {
                    fprintf(out, "  DigiKeyboard.sendKeyPress(KEY_%s, %d);\n", clean_str(k1), current_mod);
                    fprintf(out, "  DigiKeyboard.delay(50);\n");
                    fprintf(out, "  DigiKeyboard.sendKeyPress(0, %d);\n", current_mod);
                } else {
                    fprintf(out, "  DigiKeyboard.sendKeyStroke(KEY_%s);\n", clean_str(k1));
                }
                is_keyboard_action = 1;
            }
        }       
        else if (strncmp(cmd, "DELAY(", 6) == 0) {
            int ms;
            if (sscanf(cmd, "DELAY(%d)", &ms) >= 1) {
                fprintf(out, "  DigiKeyboard.delay(%d);\n", ms);
            }
        }
        else if (strncmp(cmd, "WAITBLINK(", 10) == 0) {
            int blinks = 0;
            if (sscanf(cmd, "WAITBLINK(%d)", &blinks) >= 1) {
                fprintf(out, "  waitForButtonBlink(%d);\n", blinks);
            }
        }
        else if (strcmp(cmd, "LED(ON)") == 0) {
            fprintf(out, "  digitalWrite(1, HIGH);\n");
        }
        else if (strcmp(cmd, "LED(OFF)") == 0) {
            fprintf(out, "  digitalWrite(1, LOW);\n");
        }
        else if (strcmp(cmd, "WAIT()") == 0) {
            fprintf(out, "  while(digitalRead(2) == HIGH) { DigiKeyboard.update(); DigiKeyboard.delay(50); }\n");
            fprintf(out, "  DigiKeyboard.delay(200);\n");
        }
        else {
            char errorMsg[512];
            sprintf(errorMsg, "The transpiler encountered a syntax error and doesn't understand this line:\n\n%s", trimmed);
            MessageBox(NULL, errorMsg, "Transpiler Error", MB_ICONERROR);
            fclose(in); fclose(out); return 0; 
        }

        trigger_delay:
        if (is_keyboard_action && default_delay > 0) {
            fprintf(out, "  DigiKeyboard.delay(%d); // Auto-delay\n", default_delay);
        }
    }

    fprintf(out, "\n  for(;;){ DigiKeyboard.delay(1000); }\n}\n");
    fclose(in); 
    fclose(out);
    return 1;
}

// ============================================================================
// --- FILE & PROCESS HELPERS ---
// ============================================================================

void SaveFile(HWND hwnd, const char* filename) {
    int len = GetWindowTextLength(hEdit);
    if (len > 0) {
        char* buffer = (char*)malloc(len + 1);
        GetWindowText(hEdit, buffer, len + 1);
        FILE* fp = fopen(filename, "wb");
        if (fp) {
            fwrite(buffer, 1, len, fp);
            fclose(fp);
        }
        free(buffer);
    } else {
        FILE* fp = fopen(filename, "wb");
        if (fp) fclose(fp);
    }
}

void LoadFile(HWND hwnd, const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        rewind(fp);
        
        char* buffer = (char*)malloc(size + 1);
        size_t bytesRead = fread(buffer, 1, size, fp);
        buffer[bytesRead] = '\0';
        fclose(fp);

        char* normalized = (char*)malloc(bytesRead * 2 + 1);
        size_t j = 0;
        size_t i = 0;
        while (i < bytesRead) {
            // Normalize each line break to a single \r\n. Unlike a run-consuming
            // loop, this treats \r\n as ONE break, and lone \r or \n as one each,
            // so consecutive breaks (blank lines) are preserved rather than merged.
            if (buffer[i] == '\r') {
                normalized[j++] = '\r';
                normalized[j++] = '\n';
                i += (i + 1 < bytesRead && buffer[i + 1] == '\n') ? 2 : 1; // eat paired \n
            } else if (buffer[i] == '\n') {
                normalized[j++] = '\r';
                normalized[j++] = '\n';
                i += 1;
            } else {
                normalized[j++] = buffer[i++];
            }
        }
        normalized[j] = '\0';

        SetWindowText(hEdit, normalized);
        free(buffer);
        free(normalized);
    }
}

int RunCommandHidden(const char* cmd) {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));
    
    char cmdBuffer[1024];
    strncpy(cmdBuffer, cmd, sizeof(cmdBuffer) - 1);
    cmdBuffer[sizeof(cmdBuffer) - 1] = '\0';

    if (CreateProcess(NULL, cmdBuffer, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (int)exitCode;
    }
    return -1;
}

int extract_line(const char* src, const char* needle, char* dest, int destSize) {
    const char* p = strstr(src, needle);
    if (!p) { if (destSize > 0) dest[0] = '\0'; return 0; }
    int i = 0;
    while (p[i] != '\0' && p[i] != '\r' && p[i] != '\n' && i < destSize - 1) {
        dest[i] = p[i];
        i++;
    }
    dest[i] = '\0';
    return 1;
}

int RunCommandCapture(const char* cmd, char* outBuf, int outBufSize) {
    if (outBuf && outBufSize > 0) outBuf[0] = '\0';

    char tmpDir[MAX_PATH], tmpFile[MAX_PATH];
    GetTempPath(MAX_PATH, tmpDir);
    if (!GetTempFileName(tmpDir, "dig", 0, tmpFile)) return -1;

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hFile = CreateFile(tmpFile, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { DeleteFile(tmpFile); return -1; }

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput  = NULL;
    si.hStdOutput = hFile;
    si.hStdError  = hFile;
    ZeroMemory(&pi, sizeof(pi));

    char cmdBuffer[1024];
    strncpy(cmdBuffer, cmd, sizeof(cmdBuffer) - 1);
    cmdBuffer[sizeof(cmdBuffer) - 1] = '\0';

    int exitCode = -1;
    if (CreateProcess(NULL, cmdBuffer, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD dwExit;
        GetExitCodeProcess(pi.hProcess, &dwExit);
        exitCode = (int)dwExit;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    CloseHandle(hFile);

    if (outBuf && outBufSize > 0) {
        FILE* fp = fopen(tmpFile, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long fsize = ftell(fp);
            long want  = (long)outBufSize - 1;
            if (fsize > want) {
                fseek(fp, fsize - want, SEEK_SET);
            } else {
                rewind(fp);
            }
            size_t n = fread(outBuf, 1, (size_t)outBufSize - 1, fp);
            outBuf[n] = '\0';
            fclose(fp);
        }
    }

    DeleteFile(tmpFile);
    return exitCode;
}

// ============================================================================
// --- EDITOR SUBCLASS HOOK (Robust Clipboard Normalization) ---
// ============================================================================

// Helper to safely normalize and inject text into the ANSI Edit Control
void InsertNormalizedText(HWND hwnd, const char* text) {
    if (!text) return;
    size_t len = strlen(text);
    char* normalized = (char*)malloc(len * 2 + 1);
    size_t j = 0;
    
    for (size_t i = 0; i < len; i++) {
        // Handle \r (Mac), \n (Unix), and \r\n (Windows) universally
        if (text[i] == '\r') {
            normalized[j++] = '\r';
            normalized[j++] = '\n';
            if (text[i + 1] == '\n') i++; // Skip paired \n
        } else if (text[i] == '\n') {
            normalized[j++] = '\r';
            normalized[j++] = '\n';
        } else {
            normalized[j++] = text[i];
        }
    }
    normalized[j] = '\0';
    
    // Using SendMessageA ensures the ANSI control processes it correctly
    SendMessageA(hwnd, EM_REPLACESEL, TRUE, (LPARAM)normalized);
    free(normalized);
}

LRESULT CALLBACK EditSubProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // ------------------------------------------------------------------------
    // Intercept Ctrl+A for "Select All"
    // ------------------------------------------------------------------------
    if (msg == WM_KEYDOWN) {
        if (wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SendMessage(hwnd, EM_SETSEL, 0, -1);
            return 0; 
        }
    }

    // ------------------------------------------------------------------------
    // Suppress the system "ding" sound on WM_CHAR for Ctrl+A
    // ------------------------------------------------------------------------
    if (msg == WM_CHAR) {
        if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 1 || wParam == 'a' || wParam == 'A')) {
            return 0;
        }
    }

    // ------------------------------------------------------------------------
    // Custom Paste Handling (Robust Clipboard Normalization)
    // ------------------------------------------------------------------------
    if (msg == WM_PASTE) {
        if (OpenClipboard(hwnd)) {
            // Priority 1: CF_UNICODETEXT (Notepad++, Web Browsers, VS Code)
            if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
                HANDLE hClip = GetClipboardData(CF_UNICODETEXT);
                if (hClip) {
                    WCHAR* clipText = (WCHAR*)GlobalLock(hClip);
                    if (clipText) {
                        int ansiLen = WideCharToMultiByte(CP_ACP, 0, clipText, -1, NULL, 0, NULL, NULL);
                        if (ansiLen > 0) {
                            char* ansiText = (char*)malloc(ansiLen);
                            WideCharToMultiByte(CP_ACP, 0, clipText, -1, ansiText, ansiLen, NULL, NULL);
                            InsertNormalizedText(hwnd, ansiText);
                            free(ansiText);
                        }
                        GlobalUnlock(hClip);
                    }
                }
            }
            // Priority 2: Standard CF_TEXT Fallback
            else if (IsClipboardFormatAvailable(CF_TEXT)) {
                HANDLE hClip = GetClipboardData(CF_TEXT);
                if (hClip) {
                    char* clipText = (char*)GlobalLock(hClip);
                    if (clipText) {
                        InsertNormalizedText(hwnd, clipText);
                        GlobalUnlock(hClip);
                    }
                }
            }
            CloseClipboard();
            return 0; // Handled paste message completely
        }
    }
    
    // Pass everything else to the original Edit control procedure
    return CallWindowProc(oldEditProc, hwnd, msg, wParam, lParam);
}

// Recreates the Edit control to toggle styles like Word Wrap
void RecreateEditor(HWND hwndParent) {
    int len = GetWindowTextLength(hEdit);
    char* buffer = NULL;
    if (len > 0) {
        buffer = (char*)malloc(len + 1);
        GetWindowText(hEdit, buffer, len + 1);
    }

    HFONT hCurrentFont = (HFONT)SendMessage(hEdit, WM_GETFONT, 0, 0);

    DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
    if (!isWordWrap) editStyle |= ES_AUTOHSCROLL | WS_HSCROLL;

    HWND hOld = hEdit;
    hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", buffer ? buffer : "", editStyle, 
                           0, 0, 0, 0, hwndParent, NULL, GetModuleHandle(NULL), NULL);
    
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hCurrentFont, TRUE);
    oldEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubProc);

    DestroyWindow(hOld);

    RECT rc; GetClientRect(hwndParent, &rc);
    MoveWindow(hEdit, 0, 0, rc.right, rc.bottom, TRUE);
    if (buffer) free(buffer);
}

// ============================================================================
// --- ABOUT DIALOG ---
// ============================================================================

LRESULT CALLBACK AboutWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            static HFONT hTitleFont = NULL, hBodyFont = NULL;
            if (!hBodyFont)
                hBodyFont = CreateFont(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            if (!hTitleFont)
                hTitleFont = CreateFont(-19, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            HWND hTitle = CreateWindow("STATIC",
                "Digispark Keyboard Scripter IDE",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                22, 18, 406, 26, hDlg, NULL, GetModuleHandle(NULL), NULL);
            SendMessage(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

            HWND hText = CreateWindow("STATIC",
                "Version " APP_VERSION "\n\n"
                "Creator & Project Lead:\n"
                "    Mike Young\n\n"
                "Software Engineering:\n"
                "    Google Gemini and Anthropic Claude\n\n"
                "GitHub repository:\n"
                "    " GITHUB_URL,
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                22, 50, 406, 188, hDlg, NULL, GetModuleHandle(NULL), NULL);
            SendMessage(hText, WM_SETFONT, (WPARAM)hBodyFont, TRUE);

            HWND hVisit = CreateWindow("BUTTON", "Visit GitHub Link",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                22, 255, 150, 30, hDlg, (HMENU)(UINT_PTR)IDC_ABOUT_VISIT,
                GetModuleHandle(NULL), NULL);
            SendMessage(hVisit, WM_SETFONT, (WPARAM)hBodyFont, TRUE);

            HWND hClose = CreateWindow("BUTTON", "Close",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                333, 255, 95, 30, hDlg, (HMENU)(UINT_PTR)IDC_ABOUT_CLOSE,
                GetModuleHandle(NULL), NULL);
            SendMessage(hClose, WM_SETFONT, (WPARAM)hBodyFont, TRUE);
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_ABOUT_VISIT:
                    ShellExecute(hDlg, "open", GITHUB_URL, NULL, NULL, SW_SHOWNORMAL);
                    break;
                case IDC_ABOUT_CLOSE:
                    DestroyWindow(hDlg);
                    break;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hDlg);
            break;

        default:
            return DefWindowProc(hDlg, msg, wParam, lParam);
    }
    return 0;
}

void ShowAboutDialog(HWND hParent) {
    int w = 450, h = 340;
    RECT rp;
    GetWindowRect(hParent, &rp);
    int x = rp.left + ((rp.right - rp.left) - w) / 2;
    int y = rp.top  + ((rp.bottom - rp.top) - h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    CreateWindowEx(WS_EX_DLGMODALFRAME, "AboutDlgClass",
        "About Digispark Keyboard Scripter IDE",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h, hParent, NULL, GetModuleHandle(NULL), NULL);
}

// Maps a safePrint delay value to its Typing-Delay menu command ID (-1 if none).
int DelayToMenuId(int d) {
    switch (d) {
        case 0:  return IDM_DELAY_0;
        case 1:  return IDM_DELAY_1;
        case 5:  return IDM_DELAY_5;
        case 10: return IDM_DELAY_10;
        case 15: return IDM_DELAY_15;
        default: return -1;
    }
}

// ============================================================================
// --- WINDOWS GUI EVENT LOOP ---
// ============================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HMENU hMenu = CreateMenu();
            HMENU hFileMenu = CreatePopupMenu();
            HMENU hBuildMenu = CreatePopupMenu();
            HMENU hOptionsMenu = CreatePopupMenu();

            AppendMenu(hFileMenu, MF_STRING, IDM_NEW, "New");
            AppendMenu(hFileMenu, MF_STRING, IDM_OPEN, "Open...");
            AppendMenu(hFileMenu, MF_STRING, IDM_SAVE, "Save");
            AppendMenu(hFileMenu, MF_STRING, IDM_SAVE_AS, "Save As...");
            AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, "File");

            AppendMenu(hBuildMenu, MF_STRING, IDM_FLASH, "Compile && Flash Script");
            AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hBuildMenu, "Build");

            AppendMenu(hOptionsMenu, MF_STRING | (isDarkMode ? MF_CHECKED : 0), IDM_TOGGLE_DARK, "Dark Mode");
            AppendMenu(hOptionsMenu, MF_STRING | (isWordWrap ? MF_CHECKED : 0), IDM_TOGGLE_WRAP, "Word Wrap");

            // Typing Delay submenu (safePrint inter-key delay presets)
            gDelayMenu = CreatePopupMenu();
            AppendMenu(gDelayMenu, MF_STRING, IDM_DELAY_0,  "No Delay");
            AppendMenu(gDelayMenu, MF_STRING, IDM_DELAY_1,  "1 ms");
            AppendMenu(gDelayMenu, MF_STRING, IDM_DELAY_5,  "5 ms");
            AppendMenu(gDelayMenu, MF_STRING, IDM_DELAY_10, "10 ms");
            AppendMenu(gDelayMenu, MF_STRING, IDM_DELAY_15, "15 ms");
            AppendMenu(hOptionsMenu, MF_POPUP, (UINT_PTR)gDelayMenu, "Typing Delay");
            {
                int sel = DelayToMenuId(safePrintDelay);
                if (sel != -1)
                    CheckMenuRadioItem(gDelayMenu, IDM_DELAY_0, IDM_DELAY_15, (UINT)sel, MF_BYCOMMAND);
            }

            AppendMenu(hOptionsMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hOptionsMenu, MF_STRING, IDM_ABOUT, "About...");
            AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hOptionsMenu, "Options");

            SetMenu(hwnd, hMenu);

            DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
            if (!isWordWrap) editStyle |= ES_AUTOHSCROLL | WS_HSCROLL;

            hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
                editStyle,
                0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
            
            // Subclass the edit control to handle custom pasting logic
            oldEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubProc);

            hEditBkBrush = CreateSolidBrush(RGB(30, 30, 30));

            HFONT hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, 
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                     DEFAULT_PITCH | FF_SWISS, "Consolas");
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            break;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            if (isDarkMode) {
                SetTextColor(hdc, RGB(220, 220, 220));
                SetBkColor(hdc, RGB(30, 30, 30));
                return (INT_PTR)hEditBkBrush;
            } else {
                SetTextColor(hdc, RGB(0, 0, 0));
                SetBkColor(hdc, RGB(255, 255, 255));
                return (INT_PTR)GetStockObject(WHITE_BRUSH);
            }
        }

        case WM_SIZE: {
            MoveWindow(hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
            break;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                
                case IDM_NEW:
                    SetWindowText(hEdit, "");
                    strcpy(currentFile, "");
                    SetWindowText(hwnd, "Digispark Keyboard Scripter IDE - Untitled");
                    break;

                case IDM_OPEN: {
                    OPENFILENAME ofn;
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = currentFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                    if (GetOpenFileName(&ofn)) {
                        LoadFile(hwnd, currentFile);
                        char title[MAX_PATH + 64];
                        snprintf(title, sizeof(title), "Digispark Keyboard Scripter IDE - %s", currentFile);
                        SetWindowText(hwnd, title);
                    }
                    break;
                }

                case IDM_SAVE: {
                    if (strlen(currentFile) > 0) {
                        SaveFile(hwnd, currentFile);
                    } else {
                        SendMessage(hwnd, WM_COMMAND, IDM_SAVE_AS, 0);
                    }
                    break;
                }

                case IDM_SAVE_AS: {
                    OPENFILENAME ofn;
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = currentFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
                    ofn.lpstrDefExt = "txt";

                    if (GetSaveFileName(&ofn)) {
                        SaveFile(hwnd, currentFile);
                        char title[MAX_PATH + 64];
                        snprintf(title, sizeof(title), "Digispark Keyboard Scripter IDE - %s", currentFile);
                        SetWindowText(hwnd, title);
                    }
                    break;
                }

                case IDM_TOGGLE_DARK:
                    isDarkMode = !isDarkMode;
                    CheckMenuItem(GetMenu(hwnd), IDM_TOGGLE_DARK, isDarkMode ? MF_CHECKED : MF_UNCHECKED);
                    InvalidateRect(hEdit, NULL, TRUE);
                    break;

                case IDM_TOGGLE_WRAP:
                    isWordWrap = !isWordWrap;
                    CheckMenuItem(GetMenu(hwnd), IDM_TOGGLE_WRAP, isWordWrap ? MF_CHECKED : MF_UNCHECKED);
                    RecreateEditor(hwnd);
                    break;

                case IDM_ABOUT:
                    ShowAboutDialog(hwnd);
                    break;

                case IDM_DELAY_0:
                case IDM_DELAY_1:
                case IDM_DELAY_5:
                case IDM_DELAY_10:
                case IDM_DELAY_15: {
                    UINT id = LOWORD(wParam);
                    switch (id) {
                        case IDM_DELAY_0:  safePrintDelay = 0;  break;
                        case IDM_DELAY_1:  safePrintDelay = 1;  break;
                        case IDM_DELAY_5:  safePrintDelay = 5;  break;
                        case IDM_DELAY_10: safePrintDelay = 10; break;
                        case IDM_DELAY_15: safePrintDelay = 15; break;
                    }
                    CheckMenuRadioItem(gDelayMenu, IDM_DELAY_0, IDM_DELAY_15, id, MF_BYCOMMAND);
                    break;
                }

                case IDM_FLASH: {
                    // Re-anchor CWD to the EXE folder right before invoking arduino-cli,
                    // so the build/upload always resolves arduino-cli.yaml and
                    // ./portable_data correctly even if the working directory drifted.
                    SetWorkingDirToExe();
                    SaveFile(hwnd, "temp_payload.txt");
                    
                    if (transpile("temp_payload.txt")) {
                        char buildOut[8192];
                        int compileStatus = RunCommandCapture(
                            "arduino-cli compile --config-file arduino-cli.yaml --fqbn digistump:avr:digispark-tiny sketch",
                            buildOut, sizeof(buildOut));
                        
                        if (compileStatus != 0) {
                            int tooBig = (strstr(buildOut, "overflowed") != NULL)
                                      || (strstr(buildOut, "will not fit in region") != NULL)
                                      || (strstr(buildOut, "not within region") != NULL);

                            char errMsg[8192 + 512];
                            if (tooBig) {
                                snprintf(errMsg, sizeof(errMsg),
                                    "Compilation failed: the sketch is TOO LARGE for the Digispark's flash.\n\n"
                                    "Shorten the script, or move long payloads to a downloaded stage.\n\n"
                                    "--- Compiler output ---\n%s", buildOut);
                            } else {
                                snprintf(errMsg, sizeof(errMsg),
                                    "Compilation failed!\n\nThere is likely a syntax error in your script.\n\n"
                                    "--- Compiler output ---\n%s", buildOut);
                            }
                            MessageBox(hwnd, errMsg, "Build Error", MB_ICONERROR);
                            break; 
                        }
                        
                        char sketchLine[256] = "", ramLine[256] = "";
                        extract_line(buildOut, "Sketch uses", sketchLine, sizeof(sketchLine));
                        extract_line(buildOut, "Global variables", ramLine, sizeof(ramLine));

                        // Enforce the practical flash ceiling BEFORE uploading.
                        // arduino-cli may report a higher "Maximum" than the
                        // bootloader actually leaves usable, so a sketch that
                        // "fits" per the compiler can still fail on hardware.
                        int usedBytes = 0;
                        if (sscanf(sketchLine, "Sketch uses %d", &usedBytes) == 1
                                && usedBytes > SAFE_FLASH_MAX) {
                            char warn[640];
                            snprintf(warn, sizeof(warn),
                                "Sketch is %d bytes, over the reliable limit of %d bytes for this "
                                "Digispark's bootloader.\n\n%s\n\n"
                                "The compiler reports it fits, but payloads above %d bytes often "
                                "fail to upload or run correctly, so the upload was cancelled.\n\n"
                                "Please shorten the script (or move long payloads to a downloaded stage).",
                                usedBytes, SAFE_FLASH_MAX, sketchLine, SAFE_FLASH_MAX);
                            MessageBox(hwnd, warn, "Sketch Too Large (Safety Limit)", MB_OK | MB_ICONERROR);
                            break;
                        }

                        char okMsg[1024];
                        snprintf(okMsg, sizeof(okMsg),
                            "Compilation successful!\n\n%s\n%s\n\n"
                            "Unplug your Digispark, click OK, and then plug it back in within 60 seconds to start flashing.",
                            sketchLine[0] ? sketchLine : "(flash usage unavailable)",
                            ramLine[0]    ? ramLine    : "");
                        MessageBox(hwnd, okMsg, "Ready to Flash", MB_OK | MB_ICONINFORMATION);
                        
                        int uploadStatus = RunCommandHidden("arduino-cli upload --config-file arduino-cli.yaml --fqbn digistump:avr:digispark-tiny sketch");
                        
                        if (uploadStatus != 0) {
                            MessageBox(hwnd, "Flashing failed! Did the Digispark time out?", "Flashing Failed", MB_ICONERROR);
                        } else {
                            MessageBox(hwnd, "Script flashed successfully!", "Flashing Succeeded", MB_OK);
                        }
                        
                    } else {
                        MessageBox(hwnd, "Transpilation failed. Check your script for invalid commands.", "Transpilation Failed", MB_ICONERROR);
                    }
                    break;
                }
            }
            break;
        }

        case WM_DESTROY:
            SaveSettings();
            if (hEditBkBrush) DeleteObject(hEditBkBrush);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ============================================================================
// --- APPLICATION ENTRY POINT ---
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SetWorkingDirToExe(); // Anchor all relative paths to the EXE folder (portable setup)
    InitIniPath();
    LoadSettings();

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DigiIDEClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    wc.hIcon = LoadIcon(hInstance, "MAINICON");

    RegisterClass(&wc);

    WNDCLASS wcAbout = {0};
    wcAbout.lpfnWndProc   = AboutWndProc;
    wcAbout.hInstance     = hInstance;
    wcAbout.lpszClassName = "AboutDlgClass";
    wcAbout.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcAbout.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClass(&wcAbout);

    HWND hwnd = CreateWindowEx(0, "DigiIDEClass", "Digispark Keyboard Scripter IDE - Untitled",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}