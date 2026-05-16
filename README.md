# Digispark Keyboard Scripter IDE

A lightweight, dark-mode Win32 IDE and Transpiler for the Digispark ATtiny85. This tool allows you to write automation scripts in a simplified language and flash them directly to your hardware.

## 🚀 Features

- **Dark Mode & Word Wrap:** Professional coding environment built for the Win32 API.
- **Simplified Scripting:** Case-agnostic commands for keystrokes, delays, and hardware control.
- **Direct Flashing:** Integrated with `arduino-cli` to compile and upload payloads invisibly.
- **Hardware Support:** Built-in commands for the onboard LED and physical "Wait" buttons on GPIO 2.

## 🛠️ Installation & Requirements

1. **Hardware:** A Digispark (ATtiny85) development board.
2. **Drivers:** Ensure you have the Micronucleus drivers installed (via Zadig or the Digistump board manager).
3. **Compiler:** The IDE requires `arduino-cli` to be in your system PATH or the project directory.

## 📝 Scripting Specifications

The language is line-based and case-insensitive. Use `#` for comments.

| Command | Description | Example |
| :--- | :--- | :--- |
| `STRING("...")` | Types the literal text provided. | `STRING("Hello World")` |
| `KEYS(args...)` | Supports 1-3 keys (Modifiers first). | `KEYS(GUI, R)` |
| `DELAY(ms)` | Pauses the script. | `DELAY(1000)` |
| `WAIT()` | Pauses until Pin 2 is grounded. | `WAIT()` |
| `WAITBLINK(x)` | Blinks LED x times until button is pressed. | `WAITBLINK(3)` |
| `LED(ON/OFF)` | Controls the onboard LED (Pin 1). | `LED(ON)` |

## 🏗️ Building from Source

This project is written in pure C and targets the Win32 API. It is compatible with the **Tiny C Compiler (TCC)** for a small footprint or **MSVC (Visual Studio)** for enterprise-grade builds.

**To compile with TCC:**
```bash
tcc program.c -luser32 -lgdi32 -lcomdlg32 -lkernel32 -Wl,-subsystem=windows
```

## 📜 License

This project is licensed under the **MIT License**. See the `LICENSE` file for details.

## 🤝 Acknowledgments / AI Transparency

The core C codebase and Win32 GUI implementation for this project were heavily generated using Google's **Gemini AI**. 

My role in this project was as the project architect, prompt engineer, and tester—guiding the AI's output, defining the feature set, and testing the transpiler logic against physical Digispark hardware. Because this code is largely AI-generated, it is provided as Free and Open Source Software (FOSS) to benefit the community.