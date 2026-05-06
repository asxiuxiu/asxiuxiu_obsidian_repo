import tomllib, os, json

crates_dir = r'D:\workspace\bevy-main\crates'
results = {}

for crate in sorted(os.listdir(crates_dir)):
    crate_dir = os.path.join(crates_dir, crate)
    if not os.path.isdir(crate_dir):
        continue
    toml_path = os.path.join(crate_dir, 'Cargo.toml')
    if not os.path.isfile(toml_path):
        continue
    with open(toml_path, 'rb') as f:
        data = tomllib.load(f)
    pkg = data.get('package', {})
    name = pkg.get('name', crate)
    desc = pkg.get('description', 'N/A')
    features = list(data.get('features', {}).keys())
    deps = list(data.get('dependencies', {}).keys())
    results[name] = {
        'description': desc,
        'features': features,
        'dependencies': deps,
        'dir': crate
    }

for name in sorted(results.keys()):
    info = results[name]
    feats = ' '.join(info['features'][:5])
    if len(info['features']) > 5:
        feats += '...'
    print(f'{name}|{info["description"]}|{feats}')

print(f'Total crates: {len(results)}')
