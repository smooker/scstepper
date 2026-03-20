# initcfg/project.gdb — scstepper project GDB config
# Sourced by scripts/script2.gdb after the dashboard loads.

# ─── Dashboard panel: live stepper state ─────────────────────────────────────
python

class StepperPanel(Dashboard.Source):
    """Live scstepper firmware state panel."""

    def label(self):
        return 'scstepper'

    def lines(self, term_width, term_height, style_changed):
        def v(expr, default='?'):
            try:    return str(gdb.parse_and_eval(expr))
            except: return default
        def vi(expr, default=-1):
            try:    return int(gdb.parse_and_eval(expr))
            except: return default
        def vf(expr, default=0.0):
            try:    return float(gdb.parse_and_eval(expr))
            except: return default

        rows = []

        # ── Motor ──────────────────────────────────────────────────────────
        state = v('stepperState')
        pos   = vi('posSteps')
        homed = vi('posHomed')
        esblk = vi('esBlocked')
        rng   = vf('rangeUsableMm')
        rows.append(
            'motor │ {:30s} pos={:7d} steps │ homed={} esBlk={:+d} range={:.2f}mm'
            .format(state, pos, homed, esblk, rng))

        # ── Inputs ─────────────────────────────────────────────────────────
        btns = vi('buttonsEn')
        ends = vi('endstopsEn')
        diag = vi('diagMode')
        buzz = vi('buzzActive')
        sem  = vi('semaphore', 0)
        rows.append(
            'inputs│ btns={} ends={} diag={} buzz={} │ sem={:#010x}'
            .format(btns, ends, diag, buzz, sem))

        # ── Events / jog (static vars — available when stopped in main.c) ─
        evtf = vi("'main.c'::evtFlags", -1)
        jogL = v("'main.c'::jogStateL", '?')
        jogR = v("'main.c'::jogStateR", '?')
        if evtf >= 0:
            rows.append(
                'events│ evtFlags={:#010x} │ jogL={} jogR={}'
                .format(evtf, jogL, jogR))
        else:
            rows.append('events│ (static — stop inside main.c to read)')

        # ── CDC ring buffer ─────────────────────────────────────────────────
        rxh = vi("'main.c'::rxHead", -1)
        rxt = vi("'main.c'::rxTail", -1)
        txb = vi("'main.c'::txBusy", -1)
        if rxh >= 0:
            pending = (rxh - rxt + 512) % 512
            rows.append(
                'cdc   │ rx head={} tail={} pending={} B │ txBusy={}'
                .format(rxh, rxt, pending, txb))
        else:
            rows.append('cdc   │ (static — stop inside main.c to read)')

        # ── Motor params ────────────────────────────────────────────────────
        try:
            mp = gdb.parse_and_eval('motorParams')
            rows.append(
                'params│ max={:.1f} min={:.1f} acc={:.1f} dec={:.1f} spmm={} jog={:.2f}mm step={:.2f}mm'
                .format(float(mp['mmpsmax']['f']),
                        float(mp['mmpsmin']['f']),
                        float(mp['dvdtacc']['f']),
                        float(mp['dvdtdecc']['f']),
                        int(mp['spmm']['u']),
                        float(mp['jogmm']['f']),
                        float(mp['stepmm']['f'])))
        except:
            rows.append('params│ ?')

        return rows

end

# ─── st: quick state dump (mirrors dashboard panel as plain text) ─────────────
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
