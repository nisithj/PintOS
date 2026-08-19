# Pintos Interactive Shell (Kernel Monitor) — A Walkthrough Guide

**Scope:** Project 0 — Interactive Shell for Pintos OS

**Audience:** Anyone picking up this assignment for the first time

This guide walks through *why* the code looks the way it does, not just *what* to paste. If you only copy the final code without understanding each piece, you'll struggle the moment a lecturer asks you to explain it or the moment something breaks and you need to debug it yourself.

---

## 1. What are we actually building?

Two things,but they're really one feature:

- **The kernel monitor** - the interactive loop itself: it prints a `CS2042>` prompt, reads what you type and figures out what to do with it.
- **The shell commands** — the specific actions the monitor recognizes: `whoami`, `shutdown`, `time`, `ram`, `thread`, `priority`, `exit`.

Don't think of these as two separate deliverables. The monitor is the *engine*; the commands are what it *does* once running.

---

## 2. Where does this code go?

**Only one file: `threads/init.c`.**

The assignment doc points you to a `// TODO` placeholder inside `pintos_init()` - this fires only when Pintos boots with **no command-line arguments**, i.e. when nobody told it to run a specific test or task. That's the natural place for an interactive shell: if there's nothing else to do, drop the user into a prompt.

You don't need to touch the Makefile or add new files. Everything you need (`input_getc()`, `printf()`, `shutdown_power_off()`, etc.) is already declared via `#include`s that exist at the top of `init.c`.

---

## 3. The fundamental constraint: there's no standard library here

This is the single most important thing to understand before writing a line of code.

You're writing **kernel-mode** code. Things like `scanf()`, `fgets()` or Python-style `input()` do not exist here, those are user space library functions that themselves rely on system calls into an OS. But you *are* the OS right now. There's nothing underneath you to call.

Pintos gives you a much more primitive building block instead:

```c
char c = input_getc();
```

This grabs exactly **one raw keystroke** from the keyboard/serial queue. 

There is no line buffering. No backspace handling. No on screen echo. You have to build all of that yourself.

This mirrors a pattern you'll see across all of computing: every convenience you're used (`scanf`, terminal line-editing, etc.) is a layer built on top of something more primitive. 

In kernel code, you *are* that primitive layer.

---

## 4. Building the monitor loop, piece by piece

### 4.1 The outer loop and prompt

```c
while (1) {
  printf("CS2042> ");
  ...
}
```

An infinite loop that keeps showing the prompt after every command — until the user types `exit`, which is the only thing that `break`s out.

### 4.2 Reading one line, character by character

Since there's no line reading function handed to you, you build one manually:

```c
char buffer[128];
int i = 0;

while (1) {
    char c = input_getc();  // one raw keystroke

    if (c == '\r' || c == '\n') {
        // Enter was pressed — the line is complete
        printf("\n");
        buffer[i] = '\0';   // null-terminate the string
        break;
    } else if (c == '\b' || c == 127) {
        // Backspace was pressed
        if (i > 0) {
            i--;
            printf("\b \b"); // erase the character visually too
        }
    } else if (i < 127) {
        // A normal printable character
        buffer[i] = c;
        i++;
        printf("%c", c);    // echo it back to the screen
    }
}
```

Four things are happening here that a normal terminal usually does *for* you, which you now have to do *yourself*:

| Concern | What the code does |
|---|---|
| **Accumulation** | Builds up a C string one keystroke at a time into `buffer` |
| **Line termination** | Decides "the user is done typing" when Enter (`\r`/`\n`) is pressed |
| **Editing** | Handles Backspace (`\b` or ASCII 127) by decrementing the index and visually erasing the character |
| **Echo** | Explicitly `printf`s each typed character — at this raw level, nothing appears on screen unless you print it |

**Why `printf("\b \b")` and not just `printf("\b")`?** A single backspace only moves the cursor left — it doesn't erase what's already on screen. You need: move left (`\b`), overwrite with a space (` `), then move left again (`\b`) so the cursor ends up in the right spot with the character visually gone.

**Bounds check (`i < 127`):** without this, typing more than 128 characters would write past the end of `buffer` — undefined behavior, possibly a kernel crash. Always guard fixed-size buffers like this.

### 4.3 Skip empty input

```c
if (strlen(buffer) == 0) {
    continue;
}
```

If the user just hits Enter with nothing typed, don't bother dispatching a command — just show the prompt again. Small touch, but avoids annoying "Unknown command: " spam on blank lines.

---

## 5. Dispatching commands

Once you have a clean, null terminated `buffer`, compare it against known command strings:

```c
if (strcmp(buffer, "whoami") == 0) {
    printf("Your name - Index\n");
}
else if (strcmp(buffer, "shutdown") == 0) {
    printf("Shutting down PintOS...\n");
    shutdown_power_off();
}
else if (strcmp(buffer, "time") == 0) {
    printf("Time since epoch: %lld seconds\n", (long long) rtc_get_time());
}
else if (strcmp(buffer, "ram") == 0) {
    printf("Available RAM: %u pages (%u KB)\n", init_ram_pages, init_ram_pages * 4);
}
else if (strcmp(buffer, "thread") == 0) {
    thread_print_stats();
}
else if (strcmp(buffer, "priority") == 0) {
    printf("Current thread priority: %d\n", thread_get_priority());
}
else if (strcmp(buffer, "exit") == 0) {
    printf("Exiting interactive shell... Bye!\n");
    break;
}
else {
    printf("Unknown command: %s\n", buffer);
}
```

Why `strcmp(buffer, "whoami") == 0` and not `buffer == "whoami"`? In C, strings are just `char*` pointers — comparing them with `==` checks if they're the *same memory address*, not the same text. `strcmp` actually compares the characters and returns `0` when they match.

### Where each command's data comes from

| Command | Source |
|---|---|
| `whoami` | Just a hardcoded `printf` — your name and index number |
| `shutdown` | `shutdown_power_off()` — already implemented in `devices/shutdown.h`, you just call it |
| `time` | `rtc_get_time()` from `devices/rtc.h` — returns `time_t` (which Pintos `typedef`s as `unsigned long`) |
| `ram` | `init_ram_pages` — a global tracking how many 4KB pages of RAM Pintos detected at boot |
| `thread` | `thread_print_stats()` — already implemented in `threads/thread.c` |
| `priority` | `thread_get_priority()` — already implemented (you may have modified this yourself in the Priority Scheduling mission) |
| `exit` | `break`s the loop, falls through to the existing `shutdown(); thread_exit();` at the bottom of `pintos_init()` — no need to duplicate teardown logic |

### A small gotcha: the `%lld` format specifier

```c
printf("Time since epoch: %lld seconds\n", (long long) rtc_get_time());
```

`rtc_get_time()` returns `unsigned long` (per `devices/rtc.h`). `printf` is a variadic function — it has no compile-time way to know the actual type of each argument, so it trusts the format specifier completely. If the specifier and the actual argument type don't match, you can get garbage output or undefined behavior.

`%lld` expects a `long long`. Casting the return value explicitly with `(long long)` makes the types match exactly — safe, and avoids a compiler format mismatch warning.

---

## 6. Putting it all together

Full picture, dropped into the `else` branch of the existing `if (*argv != NULL) { ... } else { ... }` in `pintos_init()`:

```c
if (*argv != NULL) {
    run_actions(argv);
} else {
    while (1) {
      printf("CS2042> ");

      char buffer[128];
      int i = 0;

      while (1) {
          char c = input_getc();

          if (c == '\r' || c == '\n') {
              printf("\n");
              buffer[i] = '\0';
              break;
          } else if (c == '\b' || c == 127) {
              if (i > 0) {
                  i--;
                  printf("\b \b");
              }
          } else if (i < 127) {
              buffer[i] = c;
              i++;
              printf("%c", c);
          }
      }

      if (strlen(buffer) == 0) {
          continue;
      }

      if (strcmp(buffer, "whoami") == 0) {
          printf("Your name - Index\n");
      }
      else if (strcmp(buffer, "shutdown") == 0) {
          printf("Shutting down PintOS...\n");
          shutdown_power_off();
      }
      else if (strcmp(buffer, "time") == 0) {
          printf("Time since epoch: %lld seconds\n", (long long) rtc_get_time());
      }
      else if (strcmp(buffer, "ram") == 0) {
          printf("Available RAM: %u pages (%u KB)\n", init_ram_pages, init_ram_pages * 4);
      }
      else if (strcmp(buffer, "thread") == 0) {
          thread_print_stats();
      }
      else if (strcmp(buffer, "priority") == 0) {
          printf("Current thread priority: %d\n", thread_get_priority());
      }
      else if (strcmp(buffer, "exit") == 0) {
          printf("Exiting interactive shell... Bye!\n");
          break;
      }
      else {
          printf("Unknown command: %s\n", buffer);
      }
    }
}

/* Finish up. */
shutdown();
thread_exit();
```

Note that `exit`'s `break` falls through naturally into the *existing* `shutdown(); thread_exit();` lines that were already there — you don't need to write your own teardown code for that path.

---

## 7. How to test it

1. Build Pintos normally (`make` inside `threads/`).
2. Run the kernel with **no arguments** (no `run <test>` action) — this is what routes execution into your `else` branch instead of `run_actions()`.
3. You should see the normal boot messages, then:
   ```
   Boot complete.
   CS2042>
   ```
4. Try each command. Try `exit` last — it should print the goodbye message and shut the VM down cleanly.
5. Try an unrecognized command (e.g. `foo`) — it should print `Unknown command: foo` and re show the prompt, not crash.
6. Try Backspace while typing — the character should visually disappear, not just stop appearing.

---

## 8. Common mistakes to watch for

- **Forgetting the bounds check** on `buffer` — leads to potential stack buffer overflow if someone pastes/types a very long line.
- **Using `==` instead of `strcmp`** for string comparison — this is a classic C gotcha coming from higher-level languages.
- **Missing the `(long long)` cast** on `rtc_get_time()` — works "by accident" on some setups, but it's undefined behavior technically, and the compiler will warn you.
- **Trying to use `scanf`/`fgets`** — they don't exist here. If your code doesn't compile because of a missing library function, it's usually because you're reaching for something that only exists in user space.

---

*This guide covers the Interactive Shell portion of Project 0 only. See the accompanying Threads Project writeup for Alarm Clock, Priority Donation, and MLFQS.*
