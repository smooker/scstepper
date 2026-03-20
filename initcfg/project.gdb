# initcfg/project.gdb — scstepper project GDB config
# Sourced by scripts/script2.gdb after the dashboard loads.
#
# Dashboard panel 'scstepper' is in initcfg/scstepper_panel.py
# Install: make userinstall  (copies to ~/.gdbinit.d/)

# ─── inject: write command string into CDC RX ring buffer ────────────────────
# Usage:  inject params
#         inject set mmpsmax 80
# Firmware processes the command on next main-loop iteration (after 'continue').
python

class InjectCmd(gdb.Command):
    """Inject a CDC command into the firmware RX ring buffer.

Usage: inject COMMAND
Writes COMMAND + CR into UserRxBufferFS and advances rxHead.
Firmware processes it on the next main-loop iteration after 'continue'."""

    BUF_SIZE = 512

    def __init__(self):
        super().__init__('inject', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        cmd = arg.strip().strip('"').strip("'")
        if not cmd:
            print("Usage: inject COMMAND")
            return
        try:
            h = int(gdb.parse_and_eval("'main.c'::rxHead"))
            for ch in cmd:
                gdb.execute("set 'main.c'::UserRxBufferFS[{}] = {}".format(h, ord(ch)))
                h = (h + 1) % self.BUF_SIZE
            gdb.execute("set 'main.c'::UserRxBufferFS[{}] = 13".format(h))   # CR
            h = (h + 1) % self.BUF_SIZE
            gdb.execute("set 'main.c'::rxHead = {}".format(h))
            print("inject: '{}\\r' ({} bytes) -> rxHead={}".format(cmd, len(cmd) + 1, h))
        except Exception as e:
            print("inject error: {}".format(e))

InjectCmd()

end

# ─── st: quick state dump ────────────────────────────────────────────────────
define st
  printf "stepperState  : %s\n",   stepperState
  printf "posSteps      : %d\n",   posSteps
  printf "posHomed      : %d\n",   posHomed
  printf "rangeUsableMm : %.2f\n", rangeUsableMm
  printf "buttonsEn     : %d\n",   buttonsEn
  printf "endstopsEn    : %d\n",   endstopsEn
  printf "diagMode      : %d\n",   diagMode
  printf "semaphore     : 0x%08x\n", semaphore
  printf "buzzActive    : %d\n",   buzzActive
end
document st
Print scstepper key state (motor, inputs, semaphore).
end

# ─── params: motor parameters (mirrors CDC 'params' command) ─────────────────
define params
  printf "mmpsmax  : %.2f mm/s\n",   motorParams.mmpsmax.f
  printf "mmpsmin  : %.2f mm/s\n",   motorParams.mmpsmin.f
  printf "dvdtacc  : %.2f mm/s2\n",  motorParams.dvdtacc.f
  printf "dvdtdecc : %.2f mm/s2\n",  motorParams.dvdtdecc.f
  printf "jogmm    : %.2f mm\n",     motorParams.jogmm.f
  printf "stepmm   : %.2f mm\n",     motorParams.stepmm.f
  printf "spmm     : %u steps/mm\n", motorParams.spmm.u
  printf "homespd  : %.2f mm/s\n",   motorParams.homespd.f
  printf "homeoff  : %u steps\n",    motorParams.homeoff.u
  printf "dirinv   : %u\n",          motorParams.dirinv.u
  printf "debug    : 0x%x\n",        motorParams.debug.u
end
document params
Print motor parameters (matches CDC 'params' command output).
end

# ─── rxbuf: CDC RX ring buffer ────────────────────────────────────────────────
define rxbuf
  printf "rxHead=%d  rxTail=%d  pending=%d B\n", \
    'main.c'::rxHead, 'main.c'::rxTail, \
    ('main.c'::rxHead - 'main.c'::rxTail + 512) % 512
  printf "UserRxBufferFS (first 64 bytes):\n"
  x/64xb 'main.c'::UserRxBufferFS
end
document rxbuf
Dump CDC RX ring buffer occupancy and raw bytes.
end

# ─── fwcheck: verify running flash matches ELF on disk ───────────────────────
python

class FwCheckCmd(gdb.Command):
    """Compare running flash against ELF file on disk.

Usage: fwcheck
Runs GDB compare-sections and reports MATCH / MISMATCH per section.
A mismatch means the binary in flash differs from build/scstepper.elf —
either stale flash (forgot to ld) or flash corruption."""

    def __init__(self):
        super().__init__('fwcheck', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            out = gdb.execute('compare-sections', to_string=True)
        except Exception as e:
            print("fwcheck: compare-sections failed: {}".format(e))
            return
        mismatches = []
        matches = []
        for line in out.splitlines():
            if 'MIS-MATCHED' in line or 'mismatch' in line.lower():
                mismatches.append(line.strip())
            elif 'matched' in line.lower():
                matches.append(line.strip())
        if mismatches:
            print("\033[1;31m=== FWCHECK FAILED — flash != ELF ===\033[0m")
            for l in mismatches:
                print("  \033[31m{}\033[0m".format(l))
            print("  Run 'ld' to reflash.")
        elif matches:
            print("\033[1;32m=== FWCHECK OK — flash matches ELF ({} sections) ===\033[0m".format(len(matches)))
        else:
            print("fwcheck: no sections compared (is ELF loaded? run 'file build/scstepper.elf')")

FwCheckCmd()

end

# ─── pr: dump EEPROM flash pages raw ────────────────────────────────────────
define pr
  p/xz *(unsigned*) 0x08040000UL@32768
  p/xz *(unsigned*) 0x08060000UL@32768
end
document pr
Dump both EEPROM emulation flash pages (0x08040000 and 0x08060000), 128KB each.
end

# ─── mem_regions: STM32F411 memory map ───────────────────────────────────────
define mem_regions
  printf "Flash  : 0x08000000 - 0x0807FFFF  512 KB\n"
  printf "SRAM1  : 0x20000000 - 0x2001FFFF  128 KB\n"
  printf "Periph : 0x40000000 - 0x5FFFFFFF\n"
  printf "PPB    : 0xE0000000 - 0xE00FFFFF  (SCS/DWT/ITM/NVIC)\n"
  info target
end
document mem_regions
Print STM32F411 memory map + current target info.
end
