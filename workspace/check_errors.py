#!/usr/bin/env python3
"""Check error files"""

error_files = [
    'Assets/games/bvh_culling.png',
    'Assets/games/gpu_instancing.png',
    'Assets/games/pbr_textures.jpg',
    'Assets/games/submesh_structure.png',
    'Assets/games/images/ndf_distribution.png'
]

for f in error_files:
    try:
        with open(f, 'rb') as file:
            header = file.read(16)
            print(f"{f}:")
            print(f"  Header (hex): {header.hex()}")
            print(f"  Size: {len(open(f, 'rb').read())} bytes")
            
            # Try to identify file type from header
            if header[:8] == b'\x89PNG\r\n\x1a\n':
                print(f"  Format: PNG (valid)")
            elif header[:2] == b'\xff\xd8':
                print(f"  Format: JPEG (valid)")
            elif header[:4] == b'GIF8':
                print(f"  Format: GIF (valid)")
            elif header[:4] == b'RIFF' and header[8:12] == b'WEBP':
                print(f"  Format: WEBP (valid)")
            elif header[:2] == b'BM':
                print(f"  Format: BMP (valid)")
            else:
                # Check if it's HTML
                try:
                    text_start = header.decode('utf-8', errors='ignore').lower()
                    if '<html' in text_start or '<!doctype' in text_start:
                        print(f"  Format: HTML (not an image!)")
                    elif '<?xml' in text_start:
                        print(f"  Format: XML/SVG")
                    else:
                        print(f"  Format: Unknown or corrupted")
                        print(f"  Text preview: {text_start[:50]}")
                except:
                    print(f"  Format: Unknown binary")
            print()
    except Exception as e:
        print(f"{f}: Error - {e}")
