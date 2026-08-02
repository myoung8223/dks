# Digispark Keyboard Scripter IDE

A lightweight, dark-mode Win32 IDE and transpiler for the Digispark (ATtiny85). Write automation scripts in a simplified language, keep an eye on the flash/RAM budget as you build, and flash payloads straight to the hardware — all from a single small native executable.

## 🚀 Features

- **Dark Mode & Word Wrap:** Toggleable, and remembered between sessions via `settings.ini`.
- **Simplified Scripting:** Case-insensitive commands for keystrokes, held modifiers, delays, and hardware control.
- **Direct Flashing:** Integrated with `arduino-cli` to compile and upload payloads invisibly — no console windows pop up.
- **Build Feedback:** After each compile, the sketch's flash and RAM usage are shown.
- **Optimized Output:** Typed strings are kept in flash (PROGMEM, via `F()`) and streamed a byte at a time on the device, sparing the ATtiny85's 512 bytes of SRAM.
- **Editor Niceties:** `Ctrl+A` select-all, plus robust paste that normalizes line endings and Unicode text pasted from editors like Notepad++.
- **Hardware Support:** Built-in commands for the onboard LED and a physical "Wait" button on GPIO 2.
- **Configurable Key Input Delay:** Key input delay can be varied from no delay to 15ms in 5ms increments.
- **About Box:** Shows the version, credits, and a one-click link back to this repository.

## 🛠️ Installation & Requirements

Windows Binary Release Version:

- [https://www.mikesshorts.com/misc/dks/dks.exe](https://www.mikesshorts.com/misc/dks/dks.exe)

See this PDF for complete build from source instructions...

[https://github.com/myoung8223/dks/blob/main/Digispark Keyboard Scripter Basic IDE.pdf](https://github.com/myoung8223/dks/blob/main/Digispark%20Keyboard%20Scripter%20Basic%20IDE.pdf)

At this time there is no binary version available and the project must be built from source.  Though the instructions for doing so are comprehensive.  No deep programming background is needed to build from source.

1. **Hardware:** A Digispark (ATtiny85) development board.
2. **Drivers:** The Micronucleus / libusb driver so Windows can talk to the board (installable via Zadig).
3. **arduino-cli:** Must be available on your PATH or in the project directory.
4. **Board core:** The IDE drives `arduino-cli` through a local `arduino-cli.yaml` and the Digistump AVR core. Because the original Digistump board index is no longer maintained, the actively maintained **ArminJo DigistumpArduino** fork is recommended.

## 📝 Scripting Specifications

The language is line-based and case-insensitive. Use `#` for comments (they're stripped even mid-line, but not inside quoted strings). Text inside `STRING("...")` keeps its original case.

| Command | Description | Example |
| :--- | :--- | :--- |
| `STRING("...")` | Types the literal text provided. | `STRING("Hello World")` |
| `KEY(name)` | Presses a single key. If a modifier is being held (see `KEYDOWN`), it's sent with that modifier. | `KEY(ENTER)` |
| `KEYS(args...)` | 1–3 keys, modifiers first. | `KEYS(GUI, R)` |
| `KEYDOWN(name)` | Holds a modifier (`CONTROL`, `ALT`, `SHIFT`, `GUI`) down for the keys that follow. | `KEYDOWN(CONTROL)` |
| `KEYUP()` | Releases all held modifiers. | `KEYUP()` |
| `DEFAULTDELAY ms` | Auto-inserts a delay after every keystroke action. **Note: no parentheses.** | `DEFAULTDELAY 50` |
| `DELAY(ms)` | Pauses the script. | `DELAY(1000)` |
| `WAIT()` | Pauses until Pin 2 is grounded. | `WAIT()` |
| `WAITBLINK(x)` | Blinks the LED x times, repeating until the button is pressed. | `WAITBLINK(3)` |
| `LED(ON/OFF)` | Controls the onboard LED (Pin 1). | `LED(ON)` |

**Key names** include `ENTER`, `ESC`, `TAB`, `SPACE`, `BACKSPACE`, `DELETE`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `MINUS`, `EQUAL`, and single characters (`A`–`Z`, `0`–`9`). **Modifiers:** `CONTROL`, `ALT`, `SHIFT`, `GUI`.

Example — open the Run dialog and launch Notepad:

```
KEYS(GUI, R)
DELAY(500)
STRING("notepad")
KEY(ENTER)
```

## 🏗️ Building from Source

This project is written in pure C against the Win32 API and builds with the **Tiny C Compiler (TCC)**. It includes a handful of manual Win32 declarations (`OPENFILENAME`, `ShellExecuteA`, `WideCharToMultiByte`, `SS_NOPREFIX`) specifically to work around TCC's minimal header set, so it compiles to a very small standalone `.exe` with no external runtime.

**To compile with TCC:**

```bash
tcc program.c -luser32 -lgdi32 -lcomdlg32 -lshell32 -lkernel32 -Wl,-subsystem=windows
```

Notes:
- `-lshell32` is required for the "Visit GitHub Link" button in the About box.
- `-Wl,-subsystem=windows` builds it as a GUI app so no console window appears behind it.
- Because the source hand-declares structures that a full SDK header already defines (notably `OPENFILENAME`), building under MSVC would require removing those TCC shims first to avoid redefinition conflicts.

## 📜 License

This project is licensed under the **MIT License**. See the `LICENSE` file for details.

## 🤝 Acknowledgments / AI Transparency

The C codebase and Win32 GUI for this project were largely generated with AI assistance — **Google Gemini** and **Anthropic Claude**.

My role was as the project's creator and lead: architecting the design, defining the feature set, directing and reviewing the AI's output, and testing everything against physical Digispark hardware. Because this code is largely AI-generated, it is released as Free and Open Source Software (FOSS) to benefit the community.

## 📺 YouTube

https://youtu.be/hyzvSi9E6LM
