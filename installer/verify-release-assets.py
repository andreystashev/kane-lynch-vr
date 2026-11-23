"""Verify public Setup/Portable/SHA256 artifacts without installing or extracting."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import zipfile

spec=importlib.util.spec_from_file_location('stage_release',Path(__file__).with_name('stage-release.py'))
stage=importlib.util.module_from_spec(spec);spec.loader.exec_module(stage)

def verify(folder,allow_fixture=False):
    folder=Path(folder).absolute();raw=stage.checked(folder,'release.json');release=json.loads(raw.decode('utf-8-sig'))
    if set(release)!={'schemaVersion','product','version','fixture','payloadManifestSha256','files'}:raise ValueError('Private/unapproved public release metadata')
    version=release.get('version','')
    if release.get('schemaVersion')!=1 or release.get('product')!=stage.PRODUCT or not re.fullmatch(r'\d+(?:\.\d+){2,3}',version):raise ValueError('Invalid public release metadata')
    if type(release.get('fixture')) is not bool:raise ValueError('Invalid fixture marker')
    if release['fixture'] and not allow_fixture:raise ValueError('A fixture installer must not be published')
    expected={'KaneLynchVR-'+version+'-Setup.exe','KaneLynchVR-'+version+'-Portable.zip'}
    entries=release.get('files',[])
    if len(entries)!=2 or {e.get('file') for e in entries}!=expected:raise ValueError('Invalid public artifact list')
    if {p.name for p in folder.iterdir()}!=expected|{n+'.sha256' for n in expected}|{'release.json'}:raise ValueError('Public folder includes private/unlisted files')
    for entry in entries:
        if set(entry)!={'file','sha256'} or not re.fullmatch('[0-9a-f]{64}',entry.get('sha256','')):raise ValueError('Invalid artifact digest')
        name=entry['file'];data=stage.checked(folder,name)
        if stage.digest(data)!=entry['sha256'] or stage.private_path(data):raise ValueError('Public artifact hash/privacy mismatch: '+name)
        if stage.checked(folder,name+'.sha256')!=(entry['sha256']+'  '+name+'\n').encode():raise ValueError('Invalid SHA256 sidecar')
        if name.endswith('.exe'):
            if data[:2]!=b'MZ':raise ValueError('Setup is not an executable')
        else:
            with zipfile.ZipFile(folder/name) as z:
                names=z.namelist();infos=z.infolist()
                if len(names)>160 or len({n.casefold() for n in names})!=len(names) or sum(i.file_size for i in infos)>256*1024*1024:raise ValueError('Unsafe portable layout/size')
                if any(i.is_dir() or ((i.external_attr>>16)&0o170000)==0o120000 for i in infos):raise ValueError('Directory/link ZIP entry is not allowed')
                if 'payload-manifest.json' not in names or z.getinfo('payload-manifest.json').file_size > 2*1024*1024:
                    raise ValueError('Missing or oversized portable manifest')
                manifest_data=z.read('payload-manifest.json')
                if stage.digest(manifest_data)!=release.get('payloadManifestSha256'):raise ValueError('Portable manifest differs from installer build')
                payload=json.loads(manifest_data.decode('utf-8-sig'))
                keys={'schemaVersion','product','version','openxrIncluded','releaseMode','recorderIncluded','recorderAutoStart','diagnosticsCollectorIncluded','fixture','files'}
                if set(payload)!=keys:raise ValueError('Private/unapproved portable metadata')
                if payload.get('schemaVersion')!=1 or payload.get('product')!=stage.PRODUCT or payload.get('version')!=version or payload.get('recorderIncluded') is not True or payload.get('recorderAutoStart') is not False or payload.get('releaseMode') is not True or payload.get('openxrIncluded') is not True or payload.get('diagnosticsCollectorIncluded') is not True:raise ValueError('Invalid portable metadata')
                if type(payload.get('fixture')) is not bool or payload['fixture']!=release['fixture']:raise ValueError('Fixture identity differs between artifacts')
                if payload['fixture'] and not allow_fixture:raise ValueError('Portable fixture must not be published')
                allowed, required = stage.payload_layout(version)
                records=payload['files'];record_names=[e.get('file') for e in records]
                if len(set(record_names))!=len(records) or set(names)!=set(record_names)|{'payload-manifest.json'} or not required.issubset(record_names):raise ValueError('Unlisted or missing ZIP contents')
                setup=set(stage.SETUP)&set(record_names)
                if setup and setup!=set(stage.SETUP):raise ValueError('Incomplete setup helper payload')
                for record in records:
                    n=record['file']
                    if n not in allowed or stage.FORBIDDEN.search(n) or ':' in n or '\\' in n or any(p in ('','.','..') for p in n.split('/')):raise ValueError('Unsafe ZIP entry')
                    b=z.read(n)
                    if stage.digest(b)!=record['sha256'] or len(b)!=record['bytes'] or stage.private_path(b):raise ValueError('Invalid portable bytes: '+n)
                    if n.lower().endswith(('.exe','.dll')) and stage.machine(b)!=(0x8664 if n in stage.X64 else 0x14c):raise ValueError('Invalid portable PE architecture')
                stage.verify_defaults(z.read('vr.ini'),version)
                if stage.ini(z.read('launcher.ini')).get('Launcher','GameExe',fallback=None)!='':raise ValueError('Local game path in portable settings')
    if stage.private_path(raw):raise ValueError('Private path in public metadata')
    return release

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--allow-fixture',action='store_true');a=p.parse_args()
    value=verify(a.directory,a.allow_fixture);print('PASS public assets: clean Setup/Portable/SHA256/metadata; '+value['version'])
