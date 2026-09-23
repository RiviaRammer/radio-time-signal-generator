"""Run the actual portable C encoder and envelope planner; use an x64 MSVC shell."""
import ctypes as c
import datetime as dt
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "host-tests"


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    dll = BUILD / "bpc_protocol.dll"
    result = subprocess.run([
        "cl", "/nologo", "/LD", "/std:c11", "/W3", "/wd4244",
        str(ROOT / "main/bpc_protocol.c"), str(ROOT / "tests/protocol_adapter.c"),
        f"/Fe{dll}", "/link", "/EXPORT:encode_for_test", "/EXPORT:bpc_encode",
        "/EXPORT:bpc_make_edges"], cwd=BUILD, capture_output=True, text=True,
        encoding="utf-8", errors="replace")
    (BUILD / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    lib = c.CDLL(str(dll))
    encode = lib.encode_for_test
    encode.argtypes = [c.c_int] * 7 + [c.POINTER(c.c_uint16)]
    encode.restype = c.c_int

    class Edge(c.Structure):
        _fields_ = [("at_us", c.c_uint32), ("carrier", c.c_bool)]

    plan = lib.bpc_make_edges
    plan.argtypes = [c.POINTER(c.c_uint16), c.POINTER(Edge)]
    plan.restype = c.c_bool
    widths = (c.c_uint16 * 20)()
    edges = (Edge * 39)()
    count = 0

    def check(t):
        nonlocal count
        assert encode(t.year, t.month, t.day, t.hour, t.minute, t.second,
                      (t.weekday() + 1) % 7, widths)
        q = [v // 100 - 1 for v in widths]
        assert widths[0] == 0
        assert all(v in (100, 200, 300, 400) for v in widths[1:])
        number = lambda seq: sum(value * 4 ** i for i, value in enumerate(reversed(seq)))
        assert q[1] == t.second // 20 and q[2] == 0
        assert number(q[3:5]) + (q[10] >> 1) * 12 == t.hour
        assert number(q[5:8]) == t.minute
        assert number(q[8:10]) == t.weekday() + 1
        assert number(q[11:14]) == t.day
        assert number(q[14:16]) == t.month
        assert number(q[16:19]) + (q[19] >> 1) * 64 == t.year - 2000
        assert (sum(v.bit_count() for v in q[1:10]) + (q[10] & 1)) % 2 == 0
        assert (sum(v.bit_count() for v in q[11:19]) + (q[19] & 1)) % 2 == 0
        assert plan(widths, edges)
        assert edges[0].at_us == 0 and edges[0].carrier
        for second in range(1, 20):
            off, on = edges[second * 2 - 1], edges[second * 2]
            assert off.at_us == second * 1_000_000 and not off.carrier
            assert on.at_us - off.at_us == widths[second] * 1000 and on.carrier
        assert edges[-1].at_us < 20_000_000
        count += 1

    for day in range(7):
        base = dt.datetime(2026, 9, 21) + dt.timedelta(days=day)
        for hour in range(24):
            for minute in range(60):
                for second in (0, 20, 40):
                    check(base.replace(hour=hour, minute=minute, second=second))
    for year in (2000, 2024, 2063, 2064, 2099):
        day = dt.datetime(year, 1, 1)
        while day.year == year:
            for hour in (0, 11, 12, 23):
                for second in (0, 20, 40):
                    check(day.replace(hour=hour, minute=59, second=second))
            day += dt.timedelta(days=1)
    for t in (dt.datetime(2024, 2, 28, 23, 40), dt.datetime(2024, 2, 29, 23, 40),
              dt.datetime(2026, 12, 31, 23, 40), dt.datetime(2063, 12, 31, 23, 40)):
        check(t + dt.timedelta(minutes=30))
    for args in ((1999, 1, 1, 0, 0, 0, 1), (2100, 1, 1, 0, 0, 0, 1),
                 (2026, 0, 1, 0, 0, 0, 1), (2026, 1, 1, 24, 0, 0, 1),
                 (2026, 1, 1, 0, 60, 0, 1), (2026, 1, 1, 0, 0, 60, 1)):
        assert not encode(*args, widths)
    assert not plan(None, edges)
    widths[0] = 100
    assert not plan(widths, edges)
    for xtal in (40_000_000, 48_000_000):
        divisor = ((xtal << 8) + 68500) // (68500 * 2)
        actual = (xtal << 8) / (divisor * 2)
        assert abs(actual - 68500) < 1
        print(f"LEDC {xtal // 1000000} MHz: {actual:.6f} Hz (ideal crystal)")
    print(f"PASS: {count} C encoder/envelope cases; year-64, noon, Sunday, leap dates, +30 min rollover, invalid inputs")


if __name__ == "__main__":
    main()
