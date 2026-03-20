# initcfg/project.gdb — scstepper project GDB config
# Sourced by scripts/script2.gdb after the dashboard loads.
#
# Dashboard panel 'scstepper' is in initcfg/scstepper_panel.py
# Install: make userinstall  (copies to ~/.gdbinit.d/)

# ─── st: quick state dump ────────────────────────────────────────────────────
define st
  printf "stepperState  : %s\n",   stepperState
  printf "posSteps      : %d\n",   posSteps
  printf "posHomed      : %d\n",   posHomed
  printf "esBlocked     : %d\n",   esBlocked
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
