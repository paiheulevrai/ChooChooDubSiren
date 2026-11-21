import struct
import subprocess
import sys

with open(sys.argv[1], "rb") as image:
    header = image.read(24)
if header[:8] != b"\x89PNG\r\n\x1a\n":
    raise ValueError("Invalid emulator screenshot")
width, height = struct.unpack(">II", header[16:24])
if width <= height:
    raise ValueError(f"Android UI is not landscape: {width}x{height}")
screen = {"instrument": 95, "common": 253, "sticks": 411}[sys.argv[2]]
scale = min(width / 640, height / 480)
x = round((width - 640 * scale) / 2 + screen * scale)
y = round((height - 480 * scale) / 2 + 46 * scale)
print(f"Screenshot {width}x{height}; tap ({x}, {y})", flush=True)
subprocess.run(["adb", "shell", "input", "tap", str(x), str(y)], check=True)
