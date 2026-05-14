import base64, sys
path = sys.argv[1]
encoded = sys.argv[2]
content = base64.b64decode(encoded).decode('utf-8')
with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
print(f"Wrote {len(content)} chars to {path}")
