import tomllib, os, re

crates_dir = r'D:\workspace\bevy-main\crates'
internal_path = os.path.join(crates_dir, 'bevy_internal', 'Cargo.toml')

with open(internal_path, 'rb') as f:
    internal = tomllib.load(f)

# Parse internal deps
internal_deps = {}
for dep_name, dep_val in internal.get('dependencies', {}).items():
    if isinstance(dep_val, dict):
        internal_deps[dep_name] = {
            'optional': dep_val.get('optional', False),
            'path': dep_val.get('path', ''),
            'features': dep_val.get('features', [])
        }
    else:
        internal_deps[dep_name] = {'optional': False, 'path': '', 'features': []}

# Parse internal features to find which feature enables which crate
feature_to_crates = {}
for feat_name, feat_vals in internal.get('features', {}).items():
    for val in feat_vals:
        m = re.match(r'dep:([\w-]+)', val)
        if m:
            crate = m.group(1)
            if crate not in feature_to_crates:
                feature_to_crates[crate] = []
            feature_to_crates[crate].append(feat_name)

# Also find forwarded features
forwarded = {}
for feat_name, feat_vals in internal.get('features', {}).items():
    for val in feat_vals:
        m = re.match(r'([\w-]+)/([\w-]+)', val)
        if m:
            crate = m.group(1)
            subfeat = m.group(2)
            if crate not in forwarded:
                forwarded[crate] = []
            forwarded[crate].append((feat_name, subfeat))

# Gather all crate info
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
    results[name] = {
        'description': desc,
        'features': features,
    }

# Output table: crate | description | key features | bevy_internal reference
for name in sorted(results.keys()):
    info = results[name]
    desc = info['description']
    feats = ', '.join(info['features'][:5])
    if len(info['features']) > 5:
        feats += '...'
    
    refs = []
    if name in internal_deps:
        dep = internal_deps[name]
        if dep['optional']:
            refs.append('optional dep')
        else:
            refs.append('required dep')
        if dep['features']:
            refs.append(f"default features: {', '.join(dep['features'])}")
    
    if name in feature_to_crates:
        refs.append(f"enabled by features: {', '.join(feature_to_crates[name][:3])}")
    
    if name in forwarded:
        fwd = forwarded[name]
        refs.append(f"forwards: {', '.join([f[1] for f in fwd[:3]])}")
    
    ref_str = '; '.join(refs) if refs else 'not directly referenced'
    print(f'{name}|{desc}|{feats}|{ref_str}')
