// ==================================================================================================
// Digispark Keyboard Scripter IDE
// Copyright (c) 2026 Mike Young
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
// ==================================================================================================

// ============================================================================
// Digispark Payload IDE & Transpiler
// ============================================================================

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

// ============================================================================
// --- MANUAL DEFINITIONS (Bypassing missing commdlg.h for TCC) ---
// ============================================================================
typedef struct {
    DWORD        lStructSize;
    HWND         hwndOwner;
    HINSTANCE    hInstance;
    LPCSTR       lpstrFilter;
    LPSTR        lpstrCustomFilter;
    DWORD        nMaxCustFilter;
    DWORD        nFilterIndex;
    LPSTR        lpstrFile;
    DWORD        nMaxFile;
    LPSTR        lpstrFileTitle;
    DWORD        nMaxFileTitle;
    LPCSTR       lpstrInitialDir;
    LPCSTR       lpstrTitle;
    DWORD        Flags;
    WORD         nFileOffset;
    WORD         nFileExtension;
    LPCSTR       lpstrDefExt;
    LPARAM       lCustData;
    void* lpfnHook;
    LPCSTR       lpTemplateName;
} OPENFILENAME;

// Dialog Flags for File Open/Save behavior
#define OFN_HIDEREADONLY     0x00000004
#define OFN_OVERWRITEPROMPT  0x00000002
#define OFN_PATHMUSTEXIST    0x00000800
#define OFN_FILEMUSTEXIST    0x00001000
#define OFN_EXPLORER         0x00080000

// Tell the compiler these functions exist in the comdlg32 library
BOOL WINAPI GetOpenFileNameA(OPENFILENAME *lpofn);
BOOL WINAPI GetSaveFileNameA(OPENFILENAME *lpofn);

#define GetOpenFileName GetOpenFileNameA
#define GetSaveFileName GetSaveFileNameA

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

int isDarkMode = 0;                 // Dark mode toggle state
int isWordWrap = 1;                 // Word wrap toggle state

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

// Reads settings from the INI file
void LoadSettings() {
    isDarkMode = GetPrivateProfileInt("Settings", "DarkMode", 0, iniPath);
    isWordWrap = GetPrivateProfileInt("Settings", "WordWrap", 1, iniPath);
}

// Writes settings to the INI file
void SaveSettings() {
    char buffer[16];
    sprintf(buffer, "%d", isDarkMode);
    WritePrivateProfileString("Settings", "DarkMode", buffer, iniPath);
    sprintf(buffer, "%d", isWordWrap);
    WritePrivateProfileString("Settings", "WordWrap", buffer, iniPath);
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

// ============================================================================
// --- TRANSPILER ENGINE ---
// ============================================================================

// Converts the custom payload script into a valid Arduino C++ sketch.
// Returns 1 on success, 0 on syntax error or file error.
int transpile(const char* inputFilename) {
    int current_mod = 0;
    int default_delay = 0;

    FILE *in = fopen(inputFilename, "r");
    CreateDirectory("sketch", NULL); 
    FILE *out = fopen("sketch\\sketch.ino", "w"); 
    
    if (!in || !out) return 0;

    // --- Write Arduino Sketch Header ---
    fprintf(out, "#include \"DigiKeyboard.h\"\n\n");
    
    // Inject missing USB HID Definitions
    fprintf(out, "// --- Missing USB HID Constants ---\n");
    fprintf(out, "#define KEY_ENTER 40\n");
    fprintf(out, "#define KEY_ESC 41\n");
    fprintf(out, "#define KEY_ESCAPE 41\n");
    fprintf(out, "#define KEY_BACKSPACE 42\n");
    fprintf(out, "#define KEY_TAB 43\n");
    fprintf(out, "#define KEY_SPACE 44\n");
    fprintf(out, "#define KEY_MINUS 45\n");
    fprintf(out, "#define KEY_EQUAL 46\n");
    fprintf(out, "#define KEY_DELETE 76\n");
    fprintf(out, "#define KEY_RIGHT 79\n");
    fprintf(out, "#define KEY_LEFT 80\n");
    fprintf(out, "#define KEY_DOWN 81\n");
    fprintf(out, "#define KEY_UP 82\n\n");

    // --- INJECT HELPER FUNCTIONS ---
    
    // Helper 1: Prevents dropped keystrokes by giving the OS time to process
    fprintf(out, "// Helper: Rate-limits typing to prevent dropped keystrokes\n");
    fprintf(out, "void safePrint(String text) {\n");
    fprintf(out, "  for (int i = 0; i < text.length(); i++) {\n");
    fprintf(out, "    DigiKeyboard.print(text[i]);\n");
    fprintf(out, "    DigiKeyboard.delay(15);\n");
    fprintf(out, "  }\n");
    fprintf(out, "}\n\n");

    // Helper 2: Blinks N times, then pauses 2s, repeating until Pin 2 goes LOW
    fprintf(out, "// Helper: Blinks N times, then pauses 2s, repeating until Pin 2 goes LOW\n");
    fprintf(out, "void waitForButtonBlink(int count) {\n");
    fprintf(out, "  while(digitalRead(2) == HIGH) {\n");
    fprintf(out, "    for(int i=0; i < count; i++) {\n");
    fprintf(out, "      digitalWrite(1, HIGH); DigiKeyboard.delay(200);\n");
    fprintf(out, "      digitalWrite(1, LOW);  DigiKeyboard.delay(200);\n");
    fprintf(out, "      if(digitalRead(2) == LOW) return; // Exit if button pressed\n");
    fprintf(out, "    }\n");
    fprintf(out, "    // 2s pause, calling update() to keep USB alive\n");
    fprintf(out, "    for(int j=0; j < 40; j++) {\n");
    fprintf(out, "      DigiKeyboard.delay(50); DigiKeyboard.update();\n");
    fprintf(out, "      if(digitalRead(2) == LOW) return;\n");
    fprintf(out, "    }\n");
    fprintf(out, "  }\n");
    fprintf(out, "  DigiKeyboard.delay(200); // Debounce\n");
    fprintf(out, "}\n\n");
    
    // Start Arduino standard functions & initialize hardware pins
    fprintf(out, "void setup() {\n");
    fprintf(out, "  DigiKeyboard.sendKeyStroke(0); // Clear OS buffer\n");
    fprintf(out, "  DigiKeyboard.delay(500);       // Wait for OS to mount virtual USB\n");
    fprintf(out, "  pinMode(1, OUTPUT); // Built-in LED\n");
    fprintf(out, "  pinMode(2, INPUT_PULLUP); // Wait button (Pin 2 to GND)\n");
    fprintf(out, "}\n\nvoid loop() {\n");
    fprintf(out, "  DigiKeyboard.update();\n  DigiKeyboard.delay(3000);\n\n");

    // --- Parse Input Script Line-by-Line ---
    char line[256];
    while (fgets(line, sizeof(line), in)) {
        char temp[256];
        strcpy(temp, line);
        
        // --- STRIP OUT COMMENTS (Quote-Aware) ---
        int in_quotes = 0;
        for (int i = 0; temp[i] != '\0'; i++) {
            if (temp[i] == '\"') in_quotes = !in_quotes; // Toggle quote state
            if (temp[i] == '#' && !in_quotes) {
                temp[i] = '\0'; // Only terminate if NOT in a string
                break;
            }
        }

        char *trimmed = clean_str(temp);
        if (trimmed[0] == '\0') continue; // Skip empty lines

        // --- CREATE AN ALL-CAPS VERSION FOR PARSING ---
        char cmd[256];
        strcpy(cmd, trimmed);
        _strupr(cmd); 

        int is_keyboard_action = 0;

        // Command: DEFAULTDELAY <ms>
        if (strncmp(cmd, "DEFAULTDELAY", 12) == 0) {
            sscanf(cmd, "DEFAULTDELAY %d", &default_delay);
            continue;
        }

        // Command: string("...")
        if (strncmp(cmd, "STRING(", 7) == 0) {
            // Search in 'trimmed' (the clean, comment-free version)
            char *start = strchr(trimmed, '\"');
            char *end = strrchr(trimmed, '\"');

            if (start && end && start != end) {
                *end = '\0';
                // RETROFIT: Use the new safePrint helper instead of DigiKeyboard.print
                fprintf(out, "  safePrint(\"%s\");\n", start + 1);
                is_keyboard_action = 1;
                *end = '\"'; // Restore it for consistency
            } else {
                char errorMsg[512];
                sprintf(errorMsg, "Syntax error (missing quotes):\n\n%s", trimmed);
                MessageBox(NULL, errorMsg, "Transpiler Crash", MB_ICONERROR);
                fclose(in); fclose(out); return 0; 
            }
        }

        // Command: keydown(...) 
        else if (strncmp(cmd, "KEYDOWN(", 8) == 0) {
            char k1[32];
            if (sscanf(cmd, "KEYDOWN(%[^)])", k1) >= 1) {
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
        // Command: keyup()
        else if (strncmp(cmd, "KEYUP()", 7) == 0) {
            current_mod = 0; 
            fprintf(out, "  DigiKeyboard.sendKeyPress(0, 0);\n");
            is_keyboard_action = 1;
        }        
        // Command: keys(...) 
        else if (strncmp(cmd, "KEYS(", 5) == 0) {
            char inner[128] = {0};
            if (sscanf(cmd, "KEYS(%[^)])", inner) == 1) {
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

		/*
        // Command: key(...) [Legacy fallback]
        else if (strncmp(cmd, "KEY(", 4) == 0) {
            char k1[32];
            if (sscanf(cmd, "KEY(%[^)])", k1) >= 1) {
                fprintf(out, "  DigiKeyboard.sendKeyStroke(KEY_%s);\n", clean_str(k1));
                is_keyboard_action = 1;
            }
        } 
		*/
		
		// Command: key(...) [Legacy fallback]
        else if (strncmp(cmd, "KEY(", 4) == 0) {
            char k1[32];
            if (sscanf(cmd, "KEY(%[^)])", k1) >= 1) {
                if (current_mod > 0) {
                    // Send the key WITH the held modifiers
                    fprintf(out, "  DigiKeyboard.sendKeyPress(KEY_%s, %d);\n", clean_str(k1), current_mod);
                    fprintf(out, "  DigiKeyboard.delay(50);\n"); // Brief tap
                    // Release the key, but KEEP holding the modifiers
                    fprintf(out, "  DigiKeyboard.sendKeyPress(0, %d);\n", current_mod);
                } else {
                    // Normal behavior if no modifiers are currently held
                    fprintf(out, "  DigiKeyboard.sendKeyStroke(KEY_%s);\n", clean_str(k1));
                }
                is_keyboard_action = 1;
            }
        }		
		
        // Command: delay(<ms>)
        else if (strncmp(cmd, "DELAY(", 6) == 0) {
            int ms;
            if (sscanf(cmd, "DELAY(%d)", &ms) >= 1) {
                fprintf(out, "  DigiKeyboard.delay(%d);\n", ms);
            }
        }
        // Command: WAITBLINK(x)
        else if (strncmp(cmd, "WAITBLINK(", 10) == 0) {
            int blinks = 0;
            if (sscanf(cmd, "WAITBLINK(%d)", &blinks) >= 1) {
                fprintf(out, "  waitForButtonBlink(%d);\n", blinks);
            }
        }
        // Command: LED(ON)
        else if (strcmp(cmd, "LED(ON)") == 0) {
            fprintf(out, "  digitalWrite(1, HIGH);\n");
        }
        // Command: LED(OFF)
        else if (strcmp(cmd, "LED(OFF)") == 0) {
            fprintf(out, "  digitalWrite(1, LOW);\n");
        }
        // Command: WAIT()
        else if (strcmp(cmd, "WAIT()") == 0) {
            fprintf(out, "  while(digitalRead(2) == HIGH) { DigiKeyboard.update(); DigiKeyboard.delay(50); }\n");
            fprintf(out, "  DigiKeyboard.delay(200);\n");
        }
        // CATCH-ALL: Syntax error / Unknown command!
        else {
            char errorMsg[512];
            sprintf(errorMsg, "The transpiler encountered a syntax error and doesn't understand this line:\n\n%s", trimmed);
            MessageBox(NULL, errorMsg, "Transpiler Error", MB_ICONERROR);
            
            fclose(in); 
            fclose(out); 
            return 0; // Abort build
        }

        // Apply DEFAULTDELAY if an actual keyboard action occurred
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

// Grabs all text from the Editor window and saves it to a file
void SaveFile(HWND hwnd, const char* filename) {
    int len = GetWindowTextLength(hEdit);
    if (len > 0) {
        char* buffer = (char*)malloc(len + 1);
        GetWindowText(hEdit, buffer, len + 1);
        FILE* fp = fopen(filename, "w");
        if (fp) {
            fputs(buffer, fp);
            fclose(fp);
        }
        free(buffer);
    } else {
        // Create empty file if no text
        FILE* fp = fopen(filename, "w");
        if (fp) fclose(fp);
    }
}

// Reads a file from disk and populates the Editor window
void LoadFile(HWND hwnd, const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        rewind(fp);
        char* buffer = (char*)malloc(size + 1);
        fread(buffer, 1, size, fp);
        buffer[size] = '\0';
        fclose(fp);
        SetWindowText(hEdit, buffer);
        free(buffer);
    }
}

// Executes a CLI tool completely invisibly and waits for it to finish.
// Returns the exit code of the program (0 usually means success).
int RunCommandHidden(const char* cmd) {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // Magic flag to hide the console window
    
    ZeroMemory(&pi, sizeof(pi));

    // CreateProcess requires a modifiable char buffer for the command
    char cmdBuffer[1024];
    strncpy(cmdBuffer, cmd, sizeof(cmdBuffer) - 1);
    cmdBuffer[sizeof(cmdBuffer) - 1] = '\0';

    // CREATE_NO_WINDOW prevents a console from even being allocated
    if (CreateProcess(NULL, cmdBuffer, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE); // Block until complete
        
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (int)exitCode;
    }
    return -1; // Failed to launch process
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
    DestroyWindow(hOld);

    RECT rc; GetClientRect(hwndParent, &rc);
    MoveWindow(hEdit, 0, 0, rc.right, rc.bottom, TRUE);
    if (buffer) free(buffer);
}

// ============================================================================
// --- WINDOWS GUI EVENT LOOP ---
// ============================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // 1. Build the Application Menus
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
            AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hOptionsMenu, "Options");

            SetMenu(hwnd, hMenu);

            // 2. Create the Text Editor Control
            DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
            if (!isWordWrap) editStyle |= ES_AUTOHSCROLL | WS_HSCROLL;

            hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
                editStyle,
                0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
            
            // Brush for Dark Mode
            hEditBkBrush = CreateSolidBrush(RGB(30, 30, 30));

            // 3. Set a monospaced font for coding
            HFONT hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, 
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                     DEFAULT_PITCH | FF_SWISS, "Consolas");
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            break;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            if (isDarkMode) {
                SetTextColor(hdc, RGB(220, 220, 220)); // Off-white text
                SetBkColor(hdc, RGB(30, 30, 30));      // Dark gray background
                return (INT_PTR)hEditBkBrush;
            } else {
                SetTextColor(hdc, RGB(0, 0, 0));       // Black text
                SetBkColor(hdc, RGB(255, 255, 255));   // White background
                return (INT_PTR)GetStockObject(WHITE_BRUSH);
            }
        }

        case WM_SIZE: {
            // Automatically resize the text editor to fill the parent window
            MoveWindow(hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
            break;
        }

        case WM_COMMAND: {
            // Handle Menu Clicks
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
                    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;

                    if (GetOpenFileName(&ofn)) {
                        LoadFile(hwnd, currentFile);
                        char title[MAX_PATH + 32];
                        sprintf(title, "Digispark Keyboard Scripter IDE - %s", currentFile);
                        SetWindowText(hwnd, title);
                    }
                    break;
                }

                case IDM_SAVE: {
                    if (strlen(currentFile) > 0) {
                        SaveFile(hwnd, currentFile);
                    } else {
                        // If there is no file opened/saved yet, trigger Save As logic
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
                    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
                    ofn.lpstrDefExt = "txt";

                    if (GetSaveFileName(&ofn)) {
                        SaveFile(hwnd, currentFile);
                        char title[MAX_PATH + 32];
                        sprintf(title, "Digispark Keyboard Scripter IDE - %s", currentFile);
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

                case IDM_FLASH: {
                    // Step A: Save current editor text to a temp file
                    SaveFile(hwnd, "temp_payload.txt");
                    
                    // Step B: Transpile script to Arduino sketch
                    if (transpile("temp_payload.txt")) {
                        
                        // Step C: Compile sketch via arduino-cli
                        int compileStatus = RunCommandHidden("arduino-cli compile --config-file arduino-cli.yaml --fqbn digistump:avr:digispark-tiny sketch");
                        
                        if (compileStatus != 0) {
                            MessageBox(hwnd, "Compilation failed!\n\nThere is likely a syntax error in your script.", "Build Error", MB_ICONERROR);
                            break; 
                        }
                        
                        // Step D: Inform user to plug in hardware
                        MessageBox(hwnd, "Compilation successful!\n\nUnplug your Digispark, click OK, and then plug it back in within 60 seconds to start flashing.", "Ready to Flash", MB_OK | MB_ICONINFORMATION);
                        
                        // Step E: Upload firmware via arduino-cli (Micronucleus)
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
            SaveSettings(); // Save user preferences before closing
            if (hEditBkBrush) DeleteObject(hEditBkBrush); // Clean up GDI object
            PostQuitMessage(0); // Exit application safely
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
    // Initialize file path and load preferences before building the UI
    InitIniPath();
    LoadSettings();

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DigiIDEClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    // Create the Main Window
    HWND hwnd = CreateWindowEx(0, "DigiIDEClass", "Digispark Keyboard Scripter IDE - Untitled",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    // Main Message Pump
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}