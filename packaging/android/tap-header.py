import struct
import subprocess
import sys

with open(sys.argv[1], "rb") as image:
    header = image.read(24)
if header[:8] != b"\x89PNG\r\n\x1a\n":
    raise ValueError("Invalid emulator screenshot")
width, height = struct.unpack(">II", header[16:24])
scale = min(width / 640, height / 480)
x = round((width - 640 * scale) / 2 + 30 * scale)
y = round((height - 480 * scale) / 2 + 45 * scale)
subprocess.run(["adb", "shell", "input", "tap", str(x), str(y)], check=True)
