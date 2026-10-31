---
name: feedback_use_python_for_bridge
description: Use Python only for bridge client commands — never shell echo/printf to FIFO, shell escapes ! and corrupts commands
type: feedback
---

Always use Python `os.write()` to send commands to the bridge FIFO. Never use bash `echo` or `printf` — bash escapes `!` to `\!` (histexpand) and corrupts bridge commands like `!reset`.

**Why:** Wasted time debugging `\!reset` → `unknown` because bash added backslash. FIFO doesn't modify data — shell does.

**How to apply:** All communication with emi_test bridge must go through Python. No bash one-liners for serial/bridge commands.
