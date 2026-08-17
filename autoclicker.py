import time
import threading
import os
import sys
import webbrowser
from pynput.mouse import Button, Controller, Listener
from pynput import keyboard

mouse = Controller()

enabled = False
running = True
skip_intro = False

DISCORD = "https://discord.gg/dCnPgKhrm3"

CYAN = "\033[96m"
BLUE = "\033[94m"
WHITE = "\033[97m"
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
GRAY = "\033[90m"
MAGENTA = "\033[95m"
RESET = "\033[0m"
BOLD = "\033[1m"

os.system("")

os.system("title Codix AutoClicker V3")
os.system("mode con: cols=110 lines=40")
os.system("cls")


def clear():
    os.system("cls")


def open_discord():
    webbrowser.open(DISCORD)


def discord():
    print(
        MAGENTA +
        BOLD +
        "                 Discord: " +
        WHITE +
        DISCORD +
        RESET
    )

    print(
        GRAY +
        "                 Press D to open Discord" +
        RESET
    )

    print()


def wait_with_skip(seconds):
    start = time.perf_counter()

    while time.perf_counter() - start < seconds:
        if skip_intro:
            return

        time.sleep(0.03)


def intro_key(key):
    global skip_intro

    if key == keyboard.Key.space:
        skip_intro = True

    elif key == keyboard.KeyCode.from_char("d"):
        open_discord()


intro_listener = keyboard.Listener(
    on_press=intro_key
)

intro_listener.start()

logo = r"""
 ██████╗ ██████╗ ██████╗ ██╗██╗  ██╗
██╔════╝██╔═══██╗██╔══██╗██║╚██╗██╔╝
██║     ██║   ██║██║  ██║██║ ╚███╔╝
██║     ██║   ██║██║  ██║██║ ██╔██╗
╚██████╗╚██████╔╝██████╔╝██║██╔╝ ██╗
 ╚═════╝ ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═╝

              A U T O C L I C K E R

                    V 3 . 0
"""

print(CYAN + BOLD + logo + RESET)

discord()

print(
    GRAY +
    "              Press SPACE to skip the intro" +
    RESET
)

wait_with_skip(2)

if not skip_intro:
    clear()

    discord()

    print(
        CYAN +
        BOLD +
        "============================================================" +
        RESET
    )

    print(
        WHITE +
        "                    starting autoclicker..." +
        RESET
    )

    print(
        CYAN +
        BOLD +
        "============================================================" +
        RESET
    )

    print()

    wait_with_skip(1)

    messages = [
        ("checking mouse...", 0.9),
        ("mouse is, in fact, a mouse", 1.0),
        ("checking mouse 5...", 0.9),
        ("yep, it's there", 0.8),
        ("loading the clicky thing...", 1.0),
        ("doing some computer stuff", 1.1),
        ("checking if everything is still alive", 1.0),
        ("probably working", 1.0),
        ("warming up the mouse", 1.0),
        ("asking the computer nicely to cooperate", 1.1),
        ("installing absolutely nothing", 0.9),
        ("checking if Daniel knows what he's doing...", 1.5),
        ("waiting for a response from Daniel...", 2.0),
        ("still waiting...", 1.8),
        ("Daniel?", 1.5),
        ("hello?", 1.5),
        ("anyone?", 1.8),
        ("ok he probably doesn't know", 1.7),
        ("answer: probably not", 2.0),
        ("ok we're good", 1.2)
    ]

    for message, delay in messages:
        if skip_intro:
            break

        print(
            CYAN +
            " > " +
            WHITE +
            message +
            RESET
        )

        wait_with_skip(delay)

if not skip_intro:
    print()

    discord()

    print(
        YELLOW +
        " loading stuff..." +
        RESET
    )

    print()

    bar_length = 45

    for i in range(bar_length + 1):
        if skip_intro:
            break

        percent = int((i / bar_length) * 100)

        bar = (
            "█" * i +
            "░" * (bar_length - i)
        )

        sys.stdout.write(
            "\r" +
            CYAN +
            "[" +
            WHITE +
            bar +
            CYAN +
            "] " +
            WHITE +
            f"{percent}%" +
            RESET
        )

        sys.stdout.flush()

        time.sleep(0.075)

    print()
    print()

    wait_with_skip(0.8)

    if not skip_intro:
        print(
            GREEN +
            "ok we're good" +
            RESET
        )

        wait_with_skip(1)

if not skip_intro:
    clear()

    discord()

    credits = [
        "",
        "",
        "AUTOCLICKER V3",
        "",
        "",
        "Made by Codix",
        "",
        "",
        "Inspired by OP Auto Clicker",
        "",
        "",
        "Coding",
        "Codix",
        "",
        "",
        "Testing",
        "Codix + friends",
        "",
        "",
        "Mouse Department",
        "my mouse",
        "",
        "",
        "Professional Clicking",
        "Codix",
        "",
        "",
        "Bug Finding",
        "definitely none",
        "",
        "",
        "Quality Assurance",
        "bro trust me",
        "",
        "",
        "Special Thanks",
        "whoever is reading this",
        "",
        "",
        "Thanks for not skipping this",
        "",
        "",
        "Made with Python",
        "",
        "",
        "Made because I felt like it",
        "",
        "",
        "Thanks to my mouse",
        "for surviving",
        "",
        "",
        "© Codix 2026",
        "",
        "",
        "Thanks for using it",
        "",
        "",
        ""
    ]

    terminal_height = 35
    position = -len(credits)

    while position < terminal_height:
        if skip_intro:
            break

        clear()

        discord()

        print(
            MAGENTA +
            BOLD +
            "                 C O D I X   P R E S E N T S" +
            RESET
        )

        print()

        for line_number in range(terminal_height):
            index = position + line_number

            if 0 <= index < len(credits):
                text = credits[index]

                padding = max(
                    0,
                    (110 - len(text)) // 2
                )

                print(
                    " " * padding +
                    WHITE +
                    BOLD +
                    text +
                    RESET
                )

            else:
                print()

        print(
            GRAY +
            "\n                 SPACE = skip intro" +
            RESET
        )

        position += 1

        time.sleep(0.16)

intro_listener.stop()

clear()

discord()

print(
    CYAN +
    BOLD +
    r"""
╔══════════════════════════════════════════════════════════════════════════════════════════════════════╗
║                                                                                                      ║
║                              C O D I X   V 3 . 0                                                     ║
║                                                                                                      ║
║                                  MODE SELECT                                                         ║
║                                                                                                      ║
╠══════════════════════════════════════════════════════════════════════════════════════════════════════╣
║                                                                                                      ║
║       [1] SAFE MODE                                                                                  ║
║                                                                                                      ║
║           ├── [1] 100 CPS                                                                             ║
║           └── [2] 200 CPS                                                                             ║
║                                                                                                      ║
║       [2] LAG MODE                                                                                   ║
║                                                                                                      ║
║           ├── [1] 300 CPS                                                                             ║
║           ├── [2] 500 CPS                                                                             ║
║           └── [3] 1000 CPS                                                                            ║
║                                                                                                      ║
║       [3] EXIT                                                                                        ║
║                                                                                                      ║
╚══════════════════════════════════════════════════════════════════════════════════════════════════════╝
"""
    + RESET
)

print()

print(
    GREEN +
    "  Safe mode = normal testing" +
    RESET
)

print(
    RED +
    "  Lag mode = high CPS, can make your browser suffer" +
    RESET
)

print()

while True:
    mode = input(
        CYAN +
        "  Pick one > " +
        WHITE
    ).strip()

    if mode == "1":
        print()

        discord()

        print(
            GREEN +
            BOLD +
            "  SAFE MODE" +
            RESET
        )

        print()

        print("  [1] 100 CPS")
        print("  [2] 200 CPS")
        print()

        while True:
            speed = input(
                CYAN +
                "  CPS > " +
                WHITE
            ).strip()

            if speed == "1":
                CPS = 100
                MODE = "SAFE"
                break

            elif speed == "2":
                CPS = 200
                MODE = "SAFE"
                break

            else:
                print(
                    RED +
                    "  nah, pick 1 or 2" +
                    RESET
                )

        break

    elif mode == "2":
        print()

        discord()

        print(
            RED +
            BOLD +
            "  LAG MODE" +
            RESET
        )

        print()

        print(
            YELLOW +
            "  don't blame me if your browser starts crying" +
            RESET
        )

        print()

        print("  [1] 300 CPS")
        print("  [2] 500 CPS")
        print("  [3] 1000 CPS")
        print()

        while True:
            speed = input(
                CYAN +
                "  CPS > " +
                WHITE
            ).strip()

            if speed == "1":
                CPS = 300
                MODE = "LAG"
                break

            elif speed == "2":
                CPS = 500
                MODE = "LAG"
                break

            elif speed == "3":
                CPS = 1000
                MODE = "LAG"
                break

            else:
                print(
                    RED +
                    "  pick 1, 2 or 3" +
                    RESET
                )

        break

    elif mode == "3":
        print(
            YELLOW +
            "\n  bye lol" +
            RESET
        )

        time.sleep(1)

        sys.exit()

    else:
        print(
            RED +
            "  that's not an option" +
            RESET
        )

INTERVAL = 1.0 / CPS

clear()

discord()

print(
    CYAN +
    BOLD +
    r"""
╔══════════════════════════════════════════════════════════════════════════════════════════════════════╗
║                                                                                                      ║
║                         C O D I X   A U T O C L I C K E R                                           ║
║                                                                                                      ║
║                                      V 3 . 0                                                         ║
║                                                                                                      ║
╠══════════════════════════════════════════════════════════════════════════════════════════════════════╣
║                                                                                                      ║
║                         Mode       : """ +
    MODE +
r"""                                                                       ║
║                         CPS        : """ +
    str(CPS) +
r"""                                                                       ║
║                         Toggle     : Mouse 5                                                          ║
║                         Emergency  : F6                                                              ║
║                                                                                                      ║
║                         Creator    : Codix                                                           ║
║                                                                                                      ║
╚══════════════════════════════════════════════════════════════════════════════════════════════════════╝
"""
    + RESET
)

print()

print(
    GREEN +
    "                         READY" +
    RESET
)

print()

print(
    YELLOW +
    "                 Mouse 5 -> ON / OFF" +
    RESET
)

print(
    RED +
    "                 F6 -> EMERGENCY STOP" +
    RESET
)

print()

print(
    GRAY +
    "                 Press D to open Discord" +
    RESET
)

print()

def click_loop():
    global enabled

    while running:
        if enabled:
            start = time.perf_counter()

            mouse.click(Button.left)

            elapsed = time.perf_counter() - start

            remaining = INTERVAL - elapsed

            if remaining > 0:
                time.sleep(remaining)

        else:
            time.sleep(0.01)


def on_click(x, y, button, pressed):
    global enabled

    if button == Button.x2 and pressed:
        enabled = not enabled

        if enabled:
            print(
                GREEN +
                "\n  [ON] clicking at " +
                WHITE +
                str(CPS) +
                GREEN +
                " CPS" +
                RESET
            )

        else:
            print(
                RED +
                "\n  [OFF] stopped clicking" +
                RESET
            )


def on_key(key):
    global running, enabled

    if key == keyboard.Key.f6:
        enabled = False
        running = False

        print()

        print(
            RED +
            "╔════════════════════════════════════════════════════════════════════════════════════╗"
        )

        print(
            "║                              STOPPED                                               ║"
        )

        print(
            "╚════════════════════════════════════════════════════════════════════════════════════╝"
            + RESET
        )

        print()

        print(
            MAGENTA +
            "  Discord: " +
            WHITE +
            DISCORD +
            RESET
        )

        return False

    elif key == keyboard.KeyCode.from_char("d"):
        open_discord()


mouse_listener = Listener(
    on_click=on_click
)

mouse_listener.start()

click_thread = threading.Thread(
    target=click_loop,
    daemon=True
)

click_thread.start()

with keyboard.Listener(
    on_press=on_key
) as keyboard_listener:

    keyboard_listener.join()

mouse_listener.stop()

print()

print(
    GRAY +
    "  © Codix 2026" +
    RESET
)

print()

print(
    MAGENTA +
    "  Discord: " +
    WHITE +
    DISCORD +
    RESET
)

input(
    "\n  Press ENTER to close..."
)