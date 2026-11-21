import struct
import subprocess
import sys
import time

with open(sys.argv[1], "rb") as image:
    header = image.read(24)
if header[:8] != b"\x89PNG\r\n\x1a\n":
    raise ValueError("Invalid emulator screenshot")
width, height = struct.unpack(">II", header[16:24])
if width <= height:
    raise ValueError(f"Android UI is not landscape: {width}x{height}")
logical_width = max(640, min(1600, round(480 * width / height)))
target = sys.argv[2]
if target == "pad":
    if logical_width < 800:
        raise ValueError(f"No wide pad at logical width {logical_width}")
    pad_width = (logical_width - 688) // 2
    logical_x, logical_y = 650 + pad_width // 2, 157
else:
    logical_x, logical_y = {"instrument": 95, "common": 253, "sticks": 411}[target], 46
scale = min(width / logical_width, height / 480)
x = round((width - logical_width * scale) / 2 + logical_x * scale)
y = round((height - 480 * scale) / 2 + logical_y * scale)
print(f"Screenshot {width}x{height}; tap ({x}, {y})", flush=True)
if target == "pad":
    with subprocess.Popen(["adb", "shell", "input", "swipe", str(x), str(y), str(x), str(y), "2000"],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) as touch:
        time.sleep(1)
        with open(sys.argv[3], "wb") as screenshot:
            subprocess.run(["adb", "exec-out", "screencap", "-p"], stdout=screenshot, check=True)
        if touch.wait() != 0:
            raise RuntimeError("Could not hold wide pad")
else:
    subprocess.run(["adb", "shell", "input", "tap", str(x), str(y)], check=True)
