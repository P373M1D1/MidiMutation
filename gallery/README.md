# Gallery Image Folder

Drop your source images here. Any common format works (JPEG, PNG, BMP, etc.).
Subfolders are supported, so you can keep folder-based sets (for example three
separate folders) and the generator will pick them up recursively.

The conversion script will rescale them to **448×300 pixels** (RGB565) and compile
them directly into the firmware as C arrays.

## How to add images

1. Copy your image files (or folders of images) into this folder.
2. Run the generator script from the project root:

  python tools/gen_gallery.py
```raw

3. Rebuild the firmware (`cmake --build build/Debug --target ChatTest`).
4. The new images appear in **GLOBAL → Gallery** and can be scrolled with **ENC2**.

## Flash-safe subset build

If your firmware is close to flash limits, compile only the first N images:

  python tools/gen_gallery.py --max-images 1

Then increase N gradually until the build no longer fits flash.

## Notes

- Images are compiled into flash; each 448×300 image occupies **268 800 bytes**.
- Keep the total number of gallery images small to avoid overflowing flash.
- The script reads files recursively in alphabetical path order; rename files
  and folders with numeric prefixes (for example `01/01_logo.png`) to control
  display order.
- Supported input: anything Pillow can open (JPEG, PNG, BMP, GIF first frame, WEBP, …).
- Install Pillow once with: `pip install Pillow`

```