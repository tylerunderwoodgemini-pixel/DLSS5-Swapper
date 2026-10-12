"""Package and verify the portable runtime, excluding local development files."""
from pathlib import Path
import hashlib, json, sys, zipfile
root=Path(__file__).resolve().parents[2]
version=json.loads((root/'resources/app/package.json').read_text())['version']
# An independently staged shell permits packaging while the local app is open.
runtime_exe=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'DLSS 5 Swapper.exe'
assert runtime_exe.is_file(),runtime_exe
destination=root/'releases'/f'DLSS5-Swapper-CreatorBase-{version}-win-x64.zip'
if destination.exists():raise SystemExit('Refusing to replace an existing release archive')
def published(path):
    return path.suffix.lower() not in {'.obj','.exp','.lib','.pdb','.pyc','.log'} and '__pycache__' not in path.parts
files=[p for p in root.iterdir() if p.is_file() and p.suffix.lower() in {'.exe','.dll','.pak','.bin','.dat','.json','.md','.txt','.html'}]
for name in ['resources','locales']:
    files.extend(p for p in (root/name).rglob('*') if p.is_file() and published(p))
names={p.relative_to(root).as_posix() for p in files}
required={'DLSS 5 Swapper.exe','resources/app/package.json','resources/payload/vr-foveated/native-vr-foveation-nvngx.dll.addon64','resources/payload/deep-fried-chicken/deep-fried-chicken.addon64','COMPATIBILITY_MATRIX.md','USER_GUIDE.md','resources/payload/deep-fried-chicken/dlss5-native-vulkan-bridge.addon64','resources/app/src/core/native-vulkan-mirror.js'}
assert required<=names,required-names
destination.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for path in sorted(files):archive.write(runtime_exe if path==root/'DLSS 5 Swapper.exe' else path,path.relative_to(root).as_posix())
with zipfile.ZipFile(destination) as archive:
    assert archive.testzip() is None,'Archive CRC error'
    assert json.loads(archive.read('resources/app/package.json'))['version']==version
    for name in required:
        assert hashlib.sha256(archive.read(name)).digest()==hashlib.sha256((runtime_exe if name=='DLSS 5 Swapper.exe' else root/name).read_bytes()).digest(),name
checksum=hashlib.sha256(destination.read_bytes()).hexdigest()
(destination.parent/'SHA256SUMS.txt').write_text(f'{checksum}  {destination.name}\n')
print(f'PASS: {len(files)} files; CRC, version and critical runtime hashes verified; {destination.stat().st_size} bytes')
print(checksum)
