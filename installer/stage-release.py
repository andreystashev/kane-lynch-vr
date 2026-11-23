"""Stage the allowed files from a checked Kane & Lynch: Dead Men VR build."""
from __future__ import annotations
import argparse
import configparser
import hashlib
from io import StringIO
import json
from pathlib import Path, PurePosixPath
import re
import struct
import zipfile

PRODUCT = 'Kane & Lynch: Dead Men VR'
RUNTIME = (
    'LaunchVR.exe', 'KaneLynchVR.dll', 'KaneLynchVRAudio.dll', 'KaneLynchVRHost.exe',
    'KaneLynchVRMonitor.exe', 'KaneLynchVRSettings.exe', 'THIRD-PARTY-AUDIO.txt', 'OPENVR-LICENSE',
    'renderer-dxvk-x86/d3d9.dll', 'renderer-dxvk-x86/LICENSE', 'renderer-dxvk-x86/provenance.json',
    'renderer-d3d9on12-x86/d3d9.dll', 'renderer-d3d9on12-x86/NOTICE.txt',
    'renderer-d3d9on12-x86/MINHOOK-LICENSE.txt', 'renderer-d3d9on12-x86/provenance.json',
)
OPENXR = ('KaneLynchOpenXRHost.exe', 'openxr_loader.dll', 'OPENXR-LICENSE', 'JSONCPP-LICENSE')
DOCS = ('README.md', 'LICENSE', 'INSTALLATION.md', 'CONTROLS.md', 'KNOWN-ISSUES.md', 'THIRD-PARTY-NOTICES.md', 'TROUBLESHOOTING.md')
SUPPORT = ('Collect-KL1-Diagnostics.ps1', 'Collect-KL1-Diagnostics.cmd',
           'analyze_kl1_performance.py', 'tools-read/decode_visual_trace.py',
           'Launch-KaneLynch-VR.cmd', 'KaneLynchVRSettings.cmd', 'Open-Logs.cmd')
RECORDING = ('Record-KaneLynch-VR.cmd', 'Start-KL1-Recorder.ps1', 'tools-read/record-vr.py')
LICENSES = ('MinHook-LICENSE.txt', 'Oculus-SDK-LICENSE.txt', 'LLVM-LICENSE.txt',
            'libcxx-LICENSE.txt', 'libcxxabi-LICENSE.txt', 'libunwind-LICENSE.txt',
            'compiler-rt-LICENSE.txt', 'MinGW-w64-runtime-LICENSE.txt', 'winpthreads-LICENSE.txt')
GENERATED = ('vr.ini', 'launcher.ini')
SETUP = ('KaneLynchVRSetupGuard.exe', 'KaneLynchVRSetupCleanup.ps1')
SUPPORT_SOURCES = {
    'Launch-KaneLynch-VR.cmd': 'scripts/Launch-KaneLynch-VR.cmd',
    'KaneLynchVRSettings.cmd': 'scripts/KaneLynchVRSettings.cmd',
    'Open-Logs.cmd': 'scripts/Open-Logs.cmd',
    'Collect-KL1-Diagnostics.ps1': 'scripts/Collect-KL1-Diagnostics.ps1',
    'Collect-KL1-Diagnostics.cmd': 'scripts/Collect-KL1-Diagnostics.cmd',
    'analyze_kl1_performance.py': 'tools/analyze_kl1_performance.py',
    'tools-read/decode_visual_trace.py': 'tools/decode_visual_trace.py',
    'Record-KaneLynch-VR.cmd': 'scripts/Record-KaneLynch-VR.cmd',
    'Start-KL1-Recorder.ps1': 'scripts/Start-KL1-Recorder.ps1',
    'tools-read/record-vr.py': 'tools/record-vr.py',
}
DOC_SOURCES = {
    name: 'docs/' + name
    for name in DOCS
}
DOC_SOURCES.update({'README.md': 'docs/PACKAGE-README.md', 'LICENSE': 'LICENSE'})
X64 = {'KaneLynchVRHost.exe', 'KaneLynchOpenXRHost.exe', 'openxr_loader.dll', 'KaneLynchVRSetupGuard.exe'}
PERSONAL = re.compile(r'(?i)[A-Z]:[\\/]+(?:Users[\\/]+[^\\/\x00\r\n]+|SteamLibrary[\\/])')
FORBIDDEN = re.compile(r'(?i)(?:^logs/|^recordings/|^diagnostics/|^captures/|\.log$|\.jsonl$|status\.json$|\.mp4$|\.dmp$|^docs/history/|^test[^/]*\.exe$|(?:^|/)kaneandlynch\.exe$|(?:^|/)xlive\.dll$)')
BOUNDARY = (
    ('Debug','Invulnerable','0'), ('Debug','TestingModifiers','0'),
    ('Diagnostics','Enabled','0'), ('Recording','Enabled','0'),
    ('VR','CaptureWithoutHost','0'), ('VR','ForcePanel','0'), ('VR','AutoStart','1'),
    ('Weapons','NativeReticle','0'), ('Weapons','LaserSight','1'),
    ('Weapons','MountedReticle','1'), ('Weapons','ReticleStyle','1'),
)
# Fresh payload qualification only. Renderer is a user preference on upgrade;
# this is intentionally separate from the forced release/migration boundary.
FRESH_DEFAULTS = (('Launcher','Renderer','DXVK'),)
FRESH_DEFAULTS_0362 = (
    ('VR','DXVKGPUTransport','1'), ('VR','GPUSharedD3D9','1'),
    ('HUD','Immersive','1'), ('HUD','GameHints','1'),
)

def payload_layout(version: str) -> tuple[set[str], set[str]]:
    required = set(RUNTIME + OPENXR + DOCS + SUPPORT + RECORDING + GENERATED)
    required.update('licenses/' + name for name in LICENSES)
    allowed = required | set(SETUP)
    historical_version = tuple(map(int, version.split('.')))
    if historical_version <= (0, 3, 65):
        required.discard('LICENSE')
    if historical_version <= (0, 3, 64):
        allowed.add('Run-VR.cmd')
    return allowed, required


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def checked(root: Path, name: str) -> bytes:
    root = Path(root).absolute()
    if not name or '\\' in name or ':' in name or PurePosixPath(name).is_absolute() or any(p in ('', '.', '..') for p in name.split('/')):
        raise ValueError('Unsafe relative release input: ' + name)
    path = root.joinpath(*name.split('/'))
    if not path.is_file():
        raise ValueError('Missing regular release input: ' + name)
    for component in (path, *path.parents):
        if component == root.parent: break
        if component.is_symlink() or getattr(component.stat(follow_symlinks=False), 'st_file_attributes', 0) & 0x400:
            raise ValueError('Linked/reparse release input: ' + name)
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('Escaped release input: ' + name)
    data = path.read_bytes()
    if not data: raise ValueError('Empty release input: ' + name)
    return data

def private_path(data: bytes) -> bool:
    return bool(PERSONAL.search(data.decode('utf-8', errors='ignore'))) or any(
        PERSONAL.search(data[offset:].decode(encoding, errors='ignore'))
        for offset in (0,1) for encoding in ('utf-16-le','utf-16-be'))

def machine(data: bytes) -> int:
    if len(data) < 64 or data[:2] != b'MZ': raise ValueError('Runtime is not a Windows PE image')
    offset = struct.unpack_from('<I', data, 60)[0]
    if offset > len(data)-6 or data[offset:offset+4] != b'PE\0\0': raise ValueError('Invalid runtime PE header')
    return struct.unpack_from('<H', data, offset+4)[0]

def ini(data: bytes) -> configparser.ConfigParser:
    text = data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
    config = configparser.ConfigParser(interpolation=None, strict=True)
    config.optionxform = str
    config.read_string(text)
    sections = set()
    for section in config.sections():
        if section.casefold() in sections: raise ValueError('Duplicate case-insensitive INI section')
        sections.add(section.casefold()); keys = set()
        for key in config[section]:
            if key.casefold() in keys: raise ValueError('Duplicate case-insensitive INI key')
            keys.add(key.casefold())
    return config

def defaults(root: Path) -> bytes:
    # No fallback to a local played vr.ini: this file is the checked-in template.
    config = ini(checked(root, 'vr-release.ini'))
    for section, key, value in BOUNDARY + (('VR','CinematicVR','0'), ('Weapons','MountedMouseAim','0')):
        if not config.has_section(section): config.add_section(section)
        config.set(section, key, value)
    for section, key in [('Launcher','GameExe'), ('Launcher','GamePath'), ('Connection','RuntimeJson')]:
        if config.has_section(section): config.set(section,key,'')
    if not config.has_section('Connection'): config.add_section('Connection')
    config.set('Connection','Mode','Auto')
    out=StringIO();config.write(out,space_around_delimiters=False)
    return b'\xff\xfe'+out.getvalue().replace('\n','\r\n').encode('utf-16-le')

def verify_defaults(data: bytes, version: str):
    config = ini(data)
    numeric_version = tuple(map(int, version.split('.')))
    extra = ()
    if numeric_version >= (0, 3, 62):
        physical_reload = '1' if numeric_version >= (0, 3, 64) else '0'
        extra = FRESH_DEFAULTS_0362 + (('Weapons','PhysicalReload',physical_reload),)
    for section,key,value in BOUNDARY + FRESH_DEFAULTS + extra:
        if config.get(section,key,fallback=None) != value:
            raise ValueError('Release default mismatch: '+section+'/'+key)
    for section,key in [('Launcher','GameExe'),('Launcher','GamePath'),('Connection','RuntimeJson')]:
        if config.get(section,key,fallback='') != '': raise ValueError('Local path in default settings')
    if private_path(data): raise ValueError('Private path in release defaults')

def runtime_manifest(package: Path, version: str) -> dict:
    build = f'BUILD-{version.replace(".", "")}.json'
    candidates = [build, 'PACKAGE-SHA256.json']
    for name in candidates:
        if (package/name).is_file():
            value=json.loads(checked(package,name).decode('utf-8-sig'))
            if value.get('version') not in (version, version+' alpha') or not isinstance(value.get('files'),dict):
                raise ValueError('Only the matching checked package may be released')
            return value
    raise ValueError('A checked BUILD or PACKAGE-SHA256 manifest is required')

def stage(root: Path, package: Path, output: Path, version: str, guard: Path | None = None, *, fixture=False, local_build=False) -> dict:
    root,package,output=(Path(p).absolute() for p in (root,package,output))
    if not re.fullmatch(r'\d+(?:\.\d+){2,3}',version) or any(int(n)>65535 for n in version.split('.')):
        raise ValueError('Invalid numeric version')
    if output.exists() or output.is_symlink(): raise ValueError('Choose a new release staging directory')
    if output.resolve().is_relative_to(package.resolve()) or package.resolve().is_relative_to(output.resolve()):
        raise ValueError('Release output must be separate from runtime input')
    ready=runtime_manifest(package,version)
    lock = None
    if not local_build:
        lock = json.loads(checked(root, 'installer/runtime-lock.json').decode('utf-8-sig'))
        if lock.get('schemaVersion') != 1 or lock.get('product') != PRODUCT or lock.get('version') != version:
            raise ValueError('Source runtime lock does not match release version')
    files={}
    for name in RUNTIME+OPENXR:
        data=checked(package,name);actual=digest(data)
        if ready['files'].get(name)!=actual: raise ValueError('Runtime differs from checked package: '+name)
        if lock is not None and lock.get('files',{}).get(name)!=actual: raise ValueError('Runtime differs from source-owned release lock: '+name)
        files[name]=data
    if (version+'-kane-lynch-vr').encode() not in files['KaneLynchVR.dll']:
        raise ValueError('Runtime version differs from release version')
    dxvk=json.loads(files['renderer-dxvk-x86/provenance.json'].decode('utf-8-sig'))
    dxvk['purpose']='Optional reversible Kane & Lynch: Dead Men renderer; unrelated renderer files are preserved.'
    files['renderer-dxvk-x86/provenance.json']=(json.dumps(dxvk,indent=2)+'\n').encode('utf-8')
    files['vr.ini']=defaults(root);verify_defaults(files['vr.ini'],version)
    files['launcher.ini']=b'\xff\xfe'+'[Launcher]\r\nGameExe=\r\n'.encode('utf-16-le')
    for name, source in DOC_SOURCES.items():
        files[name] = checked(root, source).replace(b'{version}', version.encode())
    for name, source in SUPPORT_SOURCES.items():
        data = checked(root, source)
        if name.endswith('.cmd'):
            data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
        files[name] = data
    for name in LICENSES:
        files['licenses/' + name] = checked(root, 'licenses/' + name)
    if guard:
        guard=Path(guard).absolute();files[SETUP[0]]=checked(guard.parent,guard.name)
        files[SETUP[1]]=checked(root,'installer/cleanup-previous-install.ps1')
    for name,data in files.items():
        if FORBIDDEN.search(name) or private_path(data): raise ValueError('Private or diagnostic content in release: '+name)
        if name.lower().endswith(('.exe','.dll')) and machine(data)!=(0x8664 if name in X64 else 0x14c):
            raise ValueError('Wrong release architecture: '+name)
    manifest=dict(schemaVersion=1,product=PRODUCT,version=version,
                  openxrIncluded=True,releaseMode=True,recorderIncluded=True,recorderAutoStart=False,
                  diagnosticsCollectorIncluded=True,fixture=bool(fixture),
                  files=[dict(file=name,bytes=len(data),sha256=digest(data)) for name,data in sorted(files.items())])
    output.mkdir(parents=True)
    for name,data in files.items():
        target=output/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    (output/'payload-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    verify(output);return manifest

def verify(output: Path) -> dict:
    output=Path(output).absolute();manifest=json.loads(checked(output,'payload-manifest.json').decode('utf-8-sig'))
    public_keys={'schemaVersion','product','version','openxrIncluded','releaseMode','recorderIncluded','recorderAutoStart','diagnosticsCollectorIncluded','fixture','files'}
    if set(manifest)!=public_keys:raise ValueError('Unexpected private/unapproved payload metadata')
    if type(manifest.get('fixture')) is not bool or not re.fullmatch(r'\d+(?:\.\d+){2,3}',manifest.get('version','')):raise ValueError('Invalid release identity')
    if manifest.get('schemaVersion')!=1 or manifest.get('product')!=PRODUCT or manifest.get('releaseMode') is not True or manifest.get('recorderIncluded') is not True or manifest.get('recorderAutoStart') is not False:
        raise ValueError('Not a KL1 release payload')
    allowed, required = payload_layout(manifest['version'])
    names=set()
    for entry in manifest['files']:
        name=entry['file']
        if name not in allowed or name.casefold() in names or FORBIDDEN.search(name):raise ValueError('Unapproved release entry: '+name)
        data=checked(output,name);names.add(name.casefold())
        if digest(data)!=entry['sha256'] or len(data)!=entry['bytes'] or private_path(data):raise ValueError('Release integrity/privacy mismatch: '+name)
        if name.lower().endswith(('.exe','.dll')) and machine(data)!=(0x8664 if name in X64 else 0x14c):raise ValueError('Wrong release architecture: '+name)
    setup={n.casefold() for n in SETUP}&names
    if setup and setup!={n.casefold() for n in SETUP}:raise ValueError('Incomplete setup helper payload')
    if not manifest.get('openxrIncluded') or not manifest.get('diagnosticsCollectorIncluded') or not {n.casefold() for n in required}.issubset(names):raise ValueError('Missing required release contents')
    actual={p.relative_to(output).as_posix().casefold() for p in output.rglob('*') if p.is_file()}
    if actual!=names|{'payload-manifest.json'}:raise ValueError('Unlisted release contents')
    verify_defaults(checked(output,'vr.ini'),manifest['version'])
    if ini(checked(output,'launcher.ini')).get('Launcher','GameExe',fallback=None)!='':raise ValueError('Local game path in release selection')
    return manifest

def portable(payload: Path, archive: Path):
    manifest=verify(payload);archive=Path(archive)
    if archive.exists():raise ValueError('Portable output already exists')
    archive.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for path in sorted(Path(payload).rglob('*')):
            if path.is_file():
                info=zipfile.ZipInfo(path.relative_to(payload).as_posix(),(2026,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
                z.writestr(info,path.read_bytes())
    return manifest

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--package',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--version',required=True);p.add_argument('--guard',type=Path);p.add_argument('--portable',type=Path);p.add_argument('--fixture',action='store_true');p.add_argument('--local-build',action='store_true')
    a=p.parse_args();result=stage(a.root,a.package,a.output,a.version,a.guard,fixture=a.fixture,local_build=a.local_build)
    if a.portable:portable(a.output,a.portable)
    print(json.dumps(dict(version=result['version'],files=len(result['files']),openxrIncluded=True,recorderIncluded=True,recorderAutoStart=False)))
