---
name: feedback_gdb_cleanup
description: Always kill stale GDB processes before starting new ones — leaving them running blocks BMP port and is unacceptable
type: feedback
---

Always kill all stale `arm-none-eabi-gdb` processes before starting a new GDB session. Never leave GDB running after a failed test.

**Why:** Multiple GDB processes holding /dev/ttyBmpGdb causes subsequent GDB connections to timeout/fail. User explicitly flagged this as "nedopustimo" (unacceptable). BMP serial port can only be used by one GDB at a time.

**How to apply:** Before every GDB start: `pkill -9 -f arm-none-eabi-gdb`. After every test (success or failure): kill GDB in cleanup/finally. Use `try/finally` pattern. Track subprocess.Popen object and call `.kill()` explicitly.
