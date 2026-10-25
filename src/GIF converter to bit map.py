from PIL import Image

gif_path = input("Enter the GIF image path: ") 
output_header = "anime_frames.h"
canvas_size = 210  #GC9A01

img = Image.open(gif_path)
frames = []

try:
    while True:
        # Convert to standard RGB color space
        frame = img.copy().convert("RGB")
        
        # Scale down while maintaining the aspect ratio

        frame.thumbnail((canvas_size, canvas_size), Image.Resampling.LANCZOS)
        
        # Create a square black background frame
        new_frame = Image.new("RGB", (canvas_size, canvas_size), (0, 0, 0))
        
        # Paste into the center
        paste_x = (canvas_size - frame.width) // 2
        paste_y = (canvas_size - frame.height) // 2
        new_frame.paste(frame, (paste_x, paste_y))
        
        frames.append(new_frame)
        img.seek(img.tell() + 1)
except EOFError:
    pass

print(f"Processed {len(frames)} frames!")

# --- COLOR CONVERSION FUNCTION ---
def to_rgb565(r, g, b):
    val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    # Swap bytes for TFT hardware compatibility
    return ((val >> 8) | (val << 8)) & 0xFFFF

# Write to a C header file (.h)
with open(output_header, "w") as f:
    f.write(f"#include <stdint.h>\n\n")
    f.write(f"#define ANIME_WIDTH {canvas_size}\n")
    f.write(f"#define ANIME_HEIGHT {canvas_size}\n")
    f.write(f"#define TOTAL_FRAMES {len(frames)}\n\n")
    
    for idx, frame in enumerate(frames):
        f.write(f"const uint16_t frame_{idx}[] PROGMEM = {{\n    ")
        # Use `getdata()` directly on the normalized RGB image.
        pixels = list(frame.getdata())
        line_count = 0
        for r, g, b in pixels:
            val = to_rgb565(r, g, b)
            f.write(f"0x{val:04X}, ")
            line_count += 1
            if line_count >= 12:
                f.write("\n    ")
                line_count = 0
        f.write("\n};\n\n")
    
    f.write("const uint16_t* const anime_frames[] = {\n")
    for idx in range(len(frames)):
        f.write(f"    frame_{idx},\n")
    f.write("};\n")

print(f"Processed {len(frames)} frames and created {output_header} successfully!")