"""Probe the groovebox-panel SysEx handshake (docs/panel-protocol.md)."""
import sys, time, rtmidi

HDR = [0xF0, 0x7D, 0x47, 0x52, 0x56]  # F0 7D 'G' 'R' 'V'
QUERY, REPLY, RESET_DEFAULTS, SET_VELSRC, SET_PERSIST = 0x02, 0x03, 0x7F, 0x01, 0x04


def open_port(cls, name):
    m = cls()
    for i, p in enumerate(m.get_ports()):
        if name in p:
            m.open_port(i)
            return m
    raise SystemExit(f"port {name!r} not found: {m.get_ports()}")


def send(out, cmd, *args):
    frame = HDR + [cmd, *args, 0xF7]
    out.send_message(frame)
    print("TX:", " ".join(f"{b:02X}" for b in frame))


def expect_reply(midi_in, timeout=2.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        msg = midi_in.get_message()
        if msg:
            data, _ = msg
            print("RX:", " ".join(f"{b:02X}" for b in data))
            return data
        time.sleep(0.005)
    print("RX: (timeout — no reply)")
    return None


def main():
    out = open_port(rtmidi.MidiOut, "groovebox-panel")
    midi_in = open_port(rtmidi.MidiIn, "groovebox-panel")
    midi_in.ignore_types(sysex=False)

    ok = True

    print("\n== 1. QUERY -> expect REPLY proto=1 mask=0x07 ==")
    send(out, QUERY)
    r = expect_reply(midi_in)
    if r and r[:5] == HDR and r[5] == REPLY:
        ver, mask = r[6], r[7]
        print(f"   REPLY: protocol v{ver}, feature mask 0b{mask:03b}")
        ok &= (ver == 1 and (mask & 0b111) == 0b111)
    else:
        ok = False

    print("\n== 2. SET_VELOCITY_SOURCE fixed=100, then SET back to slider (no NAK expected) ==")
    send(out, SET_VELSRC, 0x08, 100)
    time.sleep(0.2)
    send(out, SET_VELSRC, 0x00)
    time.sleep(0.2)
    send(out, QUERY)
    r = expect_reply(midi_in)
    ok &= bool(r)

    print("\n== 3. unknown command 0x55 -> must be ignored, panel stays alive ==")
    send(out, 0x55)
    time.sleep(0.2)
    send(out, QUERY)
    r = expect_reply(midi_in)
    ok &= bool(r)

    print("\n== 4. RESET_DEFAULTS ==")
    send(out, RESET_DEFAULTS)
    time.sleep(0.2)
    send(out, QUERY)
    r = expect_reply(midi_in)
    ok &= bool(r)

    print("\nRESULT:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


main()
