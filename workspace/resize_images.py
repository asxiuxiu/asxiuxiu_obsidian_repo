#!/usr/bin/env python3
"""
Resize images in Assets folder to max width of 800px
"""
import os
from pathlib import Path
from PIL import Image

# Configuration
ASSETS_DIR = Path("Assets")
MAX_WIDTH = 800
SUPPORTED_EXTENSIONS = {'.png', '.jpg', '.jpeg', '.gif', '.bmp', '.webp'}

def get_image_files(directory):
    """Recursively find all image files in directory"""
    image_files = []
    for root, dirs, files in os.walk(directory):
        for file in files:
            if Path(file).suffix.lower() in SUPPORTED_EXTENSIONS:
                image_files.append(Path(root) / file)
    return image_files

def check_and_resize_image(image_path):
    """Check image size and resize if width > MAX_WIDTH"""
    try:
        with Image.open(image_path) as img:
            width, height = img.size
            
            if width > MAX_WIDTH:
                # Calculate new height to maintain aspect ratio
                ratio = MAX_WIDTH / width
                new_height = int(height * ratio)
                
                # Resize image
                resized_img = img.resize((MAX_WIDTH, new_height), Image.Resampling.LANCZOS)
                
                # Save resized image
                resized_img.save(image_path, quality=90, optimize=True)
                
                return {
                    'path': str(image_path),
                    'old_size': (width, height),
                    'new_size': (MAX_WIDTH, new_height),
                    'resized': True
                }
            else:
                return {
                    'path': str(image_path),
                    'size': (width, height),
                    'resized': False
                }
    except Exception as e:
        return {
            'path': str(image_path),
            'error': str(e),
            'resized': False
        }

def main():
    print("=" * 60)
    print("Image Resize Tool - Max Width: 800px")
    print("=" * 60)
    
    # Get all image files
    image_files = get_image_files(ASSETS_DIR)
    print(f"\nFound {len(image_files)} image files")
    
    # Process each image
    resized_count = 0
    skipped_count = 0
    error_count = 0
    
    resized_images = []
    
    for i, image_path in enumerate(image_files, 1):
        print(f"\n[{i}/{len(image_files)}] Processing: {image_path}")
        
        result = check_and_resize_image(image_path)
        
        if 'error' in result:
            print(f"  [ERROR] {result['error']}")
            error_count += 1
        elif result['resized']:
            old_w, old_h = result['old_size']
            new_w, new_h = result['new_size']
            print(f"  [RESIZED] {old_w}x{old_h} -> {new_w}x{new_h}")
            resized_images.append(result)
            resized_count += 1
        else:
            w, h = result['size']
            print(f"  [SKIP] Already within limit: {w}x{h}")
            skipped_count += 1
    
    # Summary
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)
    print(f"Total images: {len(image_files)}")
    print(f"Resized: {resized_count}")
    print(f"Skipped (already within limit): {skipped_count}")
    print(f"Errors: {error_count}")
    
    if resized_images:
        print("\nResized images:")
        for img in resized_images:
            old_w, old_h = img['old_size']
            new_w, new_h = img['new_size']
            print(f"  - {img['path']}: {old_w}x{old_h} -> {new_w}x{new_h}")

if __name__ == "__main__":
    main()
