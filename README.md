# Pintos OS

This repo contains coursework implementing parts of [Pintos](https://pintos-os.org/), a teaching operating system used to learn OS internals — threading, scheduling, synchronization, and (in later projects) user programs, virtual memory, and file systems.

Each project below has a detailed walkthrough guide in [`guides/`](./guides) — not just the final code, but the reasoning behind it, written so someone new to Pintos can actually follow along and understand *why*, not just copy-paste.

---

## Projects

### Project 0 — Interactive Shell

A kernel-level interactive monitor (`CS2042>`-style prompt) built into `threads/init.c`, supporting commands like `whoami`, `shutdown`, `time`, `ram`, `thread`, `priority`, and `exit`. Since there's no standard library available in kernel mode, input reading, line editing, and command dispatch are all built from scratch on top of Pintos's raw `input_getc()`.

📄 Guide: [`guides/project0-Interactive-shell.md`](./guides/project0-Interactive-shell.md)

### Project 1 — Threads

Three missions extending Pintos's thread system:

- **Mission 1 — Alarm Clock:** replacing busy-waiting in `timer_sleep()` with proper blocking, using a sorted sleep list woken by the timer interrupt.
- **Mission 2 — Priority Scheduling & Donation:** strict priority scheduling plus priority donation to solve priority inversion, including nested and multiple-donation cases.
- **Mission 3 — MLFQS (Advanced Scheduler):** a 4.4BSD-style multi-level feedback queue scheduler using fixed-point arithmetic (no FPU in the kernel).

📄 Guide: [`guides/project1-threads.md`](./guides/project1-threads.md) *(coming soon)*

---

## Building and running

Standard Pintos build/run flow — see the [official Pintos documentation](https://pintos-os.org/) for full setup instructions (toolchain, QEMU, etc.).

```bash
cd threads
make
```

---

## A note on this repo's history

This repo's git history includes the original Pintos skeleton commits (from the upstream maintainer) as its base, with this project's implementation work layered on top as separate commits. This is intentional — it preserves an honest record of what was starter code versus what was implemented here.
