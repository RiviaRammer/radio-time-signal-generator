"""Convert native LVGL framebuffer captures to PNG without extra dependencies."""
from pathlib import Path
import struct
import zlib

OUTPUT = Path(__file__).resolve().parents[1] / "build/ui-host"

def png(path, width, height, data):
    def chunk(tag, payload):
        return struct.pack("!I", len(payload)) + tag + payload + struct.pack("!I", zlib.crc32(tag + payload) & 0xffffffff)
    raw = b"".join(b"\0" + data[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

for file in OUTPUT.glob("*.ppm"):
    png(file.with_suffix(".png"), 480, 480, file.read_bytes().split(b"\n", 3)[3])

names = ["01-offline", "01-home", "02-settings", "03-protocol", "07-gpio", "08-wlan"]
pixels = bytearray(1440 * 960 * 3)
for i, name in enumerate(names):
    data = (OUTPUT / (name + ".ppm")).read_bytes().split(b"\n", 3)[3]
    for y in range(480):
        start = ((i // 3 * 480 + y) * 1440 + i % 3 * 480) * 3
        pixels[start:start+1440] = data[y*1440:(y+1)*1440]
png(OUTPUT / "settings-preview.png", 1440, 960, pixels)
print("Saved native LVGL previews")
