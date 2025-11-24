from PIL import Image
import sys
import os

def convert_to_pixel_art(input_path, output_name, size=(32, 32), colors=16):
    try:
        img = Image.open(input_path)
    except FileNotFoundError:
        print(f"Error: Could not find {input_path}")
        return

    # 1. Resize using Nearest Neighbor (keeps it blocky)
    img_small = img.resize(size, Image.Resampling.NEAREST)

    # 2. Quantize (Reduce colors)
    # FIX: We must handle RGBA (Transparency) differently than standard RGB
    if img_small.mode == 'RGBA':
        # Method 2 (Fast Octree) is required for images with transparency
        # We also assume dither might help, but NONE gives cleaner pixel art lines
        result = img_small.quantize(colors=colors, method=2, kmeans=1, dither=Image.Dither.NONE)
    else:
        # For standard JPGs without transparency, Median Cut usually looks better
        result = img_small.quantize(colors=colors, method=0, kmeans=1, dither=Image.Dither.NONE)

    # 3. Convert back to RGBA to ensure it saves correctly
    result = result.convert("RGBA")
    
    # 4. Save
    output_filename = f"{output_name}.png"
    result.save(output_filename)
    print(f"Saved pixel art to: {output_filename}")

    # Optional: Scale it back up for Preview
    preview = result.resize((512, 512), Image.Resampling.NEAREST)
    preview.save(f"PREVIEW_{output_name}.png")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python pixel_converter.py <image_filename>")
    else:
        # Process the image
        # Feel free to change size=(32,32) to (64,64) if you want more detail
        convert_to_pixel_art(sys.argv[1], "idle_0", size=(80, 80), colors=64)