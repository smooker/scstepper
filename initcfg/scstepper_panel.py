# scstepper_panel.py — GDB Dashboard module for scstepper firmware
# Install: copy to ~/.gdbinit.d/scstepper_panel.py  (make userinstall does this)

class StepperPanel(Dashboard.Module):
    """Live scstepper firmware state — motor, inputs, events, CDC, params."""

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
        rng   = vf('rangeUsableMm')
        rows.append(
            'motor  {:30s} pos={:7d}steps  homed={}  range={:.2f}mm'
            .format(state, pos, homed, rng))

        # ── Inputs ─────────────────────────────────────────────────────────
        btns = vi('buttonsEn')
        ends = vi('endstopsEn')
        diag = vi('diagMode')
        buzz = vi('buzzActive')
        sem  = vi('semaphore', 0)
        rows.append(
            'inputs btns={}  ends={}  diag={}  buzz={}  sem={:#010x}'
            .format(btns, ends, diag, buzz, sem))

        # ── Events / jog (static — available when stopped in main.c) ──────
        evtf = vi("'main.c'::evtFlags", -1)
        jogL = v("'main.c'::jogStateL", '?')
        jogR = v("'main.c'::jogStateR", '?')
        if evtf >= 0:
            rows.append(
                'events evtFlags={:#010x}  jogL={}  jogR={}'
                .format(evtf, jogL, jogR))
        else:
            rows.append('events (stop inside main.c to read static vars)')

        # ── CDC ring buffer ─────────────────────────────────────────────────
        rxh = vi("'main.c'::rxHead", -1)
        rxt = vi("'main.c'::rxTail", -1)
        txb = vi("'main.c'::txBusy", -1)
        if rxh >= 0:
            pending = (rxh - rxt + 512) % 512  # CDC_RX_BUF_SIZE=512
            rows.append(
                'cdc    rx head={}  tail={}  pending={}B  txBusy={}'
                .format(rxh, rxt, pending, txb))
        else:
            rows.append('cdc    (stop inside main.c to read static vars)')

        # ── Motor params ────────────────────────────────────────────────────
        try:
            mp = gdb.parse_and_eval('motorParams')
            rows.append(
                'params max={:.1f}  min={:.1f}  acc={:.1f}  dec={:.1f}'
                '  spmm={}  jog={:.2f}mm  step={:.2f}mm'
                .format(float(mp['mmpsmax']['f']),
                        float(mp['mmpsmin']['f']),
                        float(mp['dvdtacc']['f']),
                        float(mp['dvdtdecc']['f']),
                        int(mp['spmm']['u']),
                        float(mp['jogmm']['f']),
                        float(mp['stepmm']['f'])))
        except:
            rows.append('params ?')

        return rows
