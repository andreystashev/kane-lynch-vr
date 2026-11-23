"""Release staging and public-asset checks with isolated synthetic fixtures."""
import configparser
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

HERE=Path(__file__).resolve().parents[2]/"installer"
spec=importlib.util.spec_from_file_location('stage_release',HERE/'stage-release.py');stage=importlib.util.module_from_spec(spec);spec.loader.exec_module(stage)
spec2=importlib.util.spec_from_file_location('verify_release_assets',HERE/'verify-release-assets.py');public=importlib.util.module_from_spec(spec2);spec2.loader.exec_module(public)

class ReleaseStaging(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.base=Path(self.tmp.name);self.root=self.base/'source';self.package=self.base/'played package';self.output=self.base/'release'
        self.root.mkdir();(self.root/'installer').mkdir();self.package.mkdir();self.version='0.3.64'
        for name in stage.RUNTIME+stage.OPENXR:
            p=self.package/name;p.parent.mkdir(parents=True,exist_ok=True)
            if name.lower().endswith(('.exe','.dll')):
                b=bytearray(128);b[:2]=b'MZ';struct.pack_into('<I',b,60,80);b[80:84]=b'PE\0\0';struct.pack_into('<H',b,84,0x8664 if name in stage.X64 else 0x14c)
                data=bytes(b)+(name+' '+self.version+'-kane-lynch-vr').encode()
            elif name=='renderer-dxvk-x86/provenance.json':data=json.dumps({'purpose':'Old product name','files':{}}).encode()
            else:data=('public fixture '+name).encode()
            p.write_bytes(data)
        for path in stage.DOC_SOURCES.values():
            p = self.root / path
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('Public guide {version}')
        for name in stage.LICENSES:
            p = self.root / 'licenses' / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('public fixture license')
        for name, source in stage.SUPPORT_SOURCES.items():
            p = self.root / source
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('source-owned ' + name)
        self.template=('[Launcher]\nRenderer=DXVK\nGamePath=\n[Connection]\nMode=Auto\nRuntimeJson=\n'
                       '[VR]\nCinematicVR=0\nDXVKGPUTransport=1\nGPUSharedD3D9=1\n[Diagnostics]\nPerformanceLogging=1\n[Hands]\nNativeArms=1\n'
                       '[HUD]\nImmersive=1\nGameHints=1\n[Weapons]\nPhysicalReload=1\n')
        (self.root/'vr-release.ini').write_text(self.template)
        (self.package/'vr.ini').write_text('[Debug]\nInvulnerable=1\n[Launcher]\nGamePath=C:\\Users\\private\\KANEANDLYNCH.EXE\n')
        (self.package/'logs').mkdir();(self.package/'logs/private.jsonl').write_text('private')
        (self.package/'recordings').mkdir();(self.package/'recordings/private.mp4').write_bytes(b'private recording')
        (self.package/'KANEANDLYNCH.EXE').write_bytes(b'private game')
        self.ready=dict(version=self.version,files={n:stage.digest((self.package/n).read_bytes()) for n in stage.RUNTIME+stage.OPENXR})
        self.lock=dict(schemaVersion=1,product=stage.PRODUCT,version=self.version,files=dict(self.ready['files']))
        (self.root/'installer/runtime-lock.json').write_text(json.dumps(self.lock));self.save_ready()
    def save_ready(self):
        self.build_manifest=self.package/f'BUILD-{self.version.replace(".", "")}.json'
        self.build_manifest.write_text(json.dumps(self.ready))
    def run_stage(self,**kwargs):return stage.stage(self.root,self.package,self.output,self.version,**kwargs)
    def config(self):return stage.ini((self.output/'vr.ini').read_bytes())
    def test_local_build_validates_manifest_without_the_official_lock(self):
        (self.root / 'installer/runtime-lock.json').unlink()
        self.run_stage(local_build=True)

    def test_local_build_rejects_unchecked_bytes(self):
        (self.package / 'LaunchVR.exe').write_bytes(b'unauthorized change')
        with self.assertRaisesRegex(ValueError, 'Runtime differs from checked package'):
            self.run_stage(local_build=True)

    def test_allowlist_and_source_scripts_ignore_private_played_payload(self):
        m=self.run_stage();names={e['file'] for e in m['files']}
        self.assertFalse(any(stage.FORBIDDEN.search(n) for n in names));self.assertTrue(m['recorderIncluded']);self.assertFalse(m['recorderAutoStart'])
        self.assertNotIn('sourceCommit',m);self.assertNotIn('sourceDirty',m);self.assertNotIn('KANEANDLYNCH.EXE',names);self.assertNotIn('logs/private.jsonl',names);self.assertNotIn('recordings/private.mp4',names)
        self.assertIn('tools-read/record-vr.py',names);self.assertIn('tools-read/decode_visual_trace.py',names)
        self.assertEqual((self.output/'Start-KL1-Recorder.ps1').read_text(),'source-owned Start-KL1-Recorder.ps1')
        self.assertEqual((self.output/'Record-KaneLynch-VR.cmd').read_text(),'source-owned Record-KaneLynch-VR.cmd')
        self.assertNotIn('Run-VR.cmd',names)
        for name in ('Launch-KaneLynch-VR.cmd','KaneLynchVRSettings.cmd','Open-Logs.cmd','Collect-KL1-Diagnostics.cmd'):
            self.assertEqual((self.output/name).read_text(),'source-owned '+name)
        self.assertEqual(stage.verify(self.output),m)
    def test_release_defaults_are_laser_only_with_original_mounted_crosshair(self):
        self.run_stage();c=self.config()
        for section,key,value in stage.BOUNDARY:self.assertEqual(c[section][key],value,(section,key))
        self.assertEqual(c['VR']['CinematicVR'],'0');self.assertEqual(c['Connection']['Mode'],'Auto')
        self.assertEqual(c['Diagnostics']['PerformanceLogging'],'1','master checkbox can enable retained preset')
        self.assertEqual(c['Launcher']['GamePath'],'');self.assertFalse(stage.private_path((self.output/'vr.ini').read_bytes()))
        self.assertEqual(stage.ini((self.output/'launcher.ini').read_bytes())['Launcher']['GameExe'],'')
        self.assertTrue((self.output/'vr.ini').read_bytes().startswith(b'\xff\xfe'))
        self.assertEqual(c['Launcher']['Renderer'],'DXVK','fresh public installs use the qualified default renderer')
        self.assertEqual(c['VR']['DXVKGPUTransport'],'1','fresh DXVK installs enable direct GPU transport')
        self.assertEqual(c['VR']['GPUSharedD3D9'],'1','fresh DXVK installs enable shared Direct3D 9 images')
        self.assertEqual(c['Weapons']['PhysicalReload'],'1','fresh installs enable physical reload')
        self.assertEqual(c['HUD']['Immersive'],'1')
        self.assertEqual(c['HUD']['GameHints'],'1')
    def test_older_release_still_verifies_without_new_explicit_preferences(self):
        (self.root/'vr-release.ini').write_text(self.template.replace('[HUD]\nImmersive=1\nGameHints=1\n','')
                                                .replace('PhysicalReload=1\n','').replace('DXVKGPUTransport=1\n','')
                                                .replace('GPUSharedD3D9=1\n',''))
        stage.verify_defaults(stage.defaults(self.root),'0.3.61')
    def test_previous_releases_still_verify_with_original_reload_default(self):
        (self.root/'vr-release.ini').write_text(self.template.replace('PhysicalReload=1','PhysicalReload=0'))
        for version in ('0.3.62','0.3.63'):
            with self.subTest(version=version):
                stage.verify_defaults(stage.defaults(self.root),version)
    def test_fresh_payload_rejects_missing_or_on12_default_in_source_preset(self):
        for text in (self.template.replace('Renderer=DXVK\n',''),self.template.replace('Renderer=DXVK','Renderer=D3D9On12')):
            with self.subTest(preset=text):
                (self.root/'vr-release.ini').write_text(text)
                with self.assertRaisesRegex(ValueError,'Release default mismatch: Launcher/Renderer'):self.run_stage()
                self.assertFalse(self.output.exists(),'invalid fresh defaults must fail before creating release files')
    def test_new_release_rejects_incorrect_fresh_feature_defaults(self):
        for key, old, value in (('VR/DXVKGPUTransport','DXVKGPUTransport=1','DXVKGPUTransport=0'),
                                ('VR/DXVKGPUTransport','DXVKGPUTransport=1\n',''),
                                ('VR/GPUSharedD3D9','GPUSharedD3D9=1','GPUSharedD3D9=0'),
                                ('VR/GPUSharedD3D9','GPUSharedD3D9=1\n',''),
                                ('Weapons/PhysicalReload','PhysicalReload=1','PhysicalReload=0'),
                                ('Weapons/PhysicalReload','PhysicalReload=1\n',''),
                                ('HUD/GameHints','GameHints=1','GameHints=0'),
                                ('HUD/Immersive','Immersive=1','Immersive=0')):
            with self.subTest(setting=key):
                (self.root/'vr-release.ini').write_text(self.template.replace(old,value))
                with self.assertRaisesRegex(ValueError,'Release default mismatch: '+key):self.run_stage()
                self.assertFalse(self.output.exists())
    def test_fresh_payload_verifier_refuses_on12_even_with_updated_integrity_hash(self):
        self.run_stage();p=self.output/'vr.ini'
        p.write_bytes(p.read_bytes().replace('Renderer=DXVK'.encode('utf-16-le'),'Renderer=D3D9On12'.encode('utf-16-le')))
        manifest=json.loads((self.output/'payload-manifest.json').read_text())
        for row in manifest['files']:
            if row['file']=='vr.ini':row.update(bytes=p.stat().st_size,sha256=stage.digest(p.read_bytes()))
        (self.output/'payload-manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError,'Release default mismatch: Launcher/Renderer'):stage.verify(self.output)
    def test_no_fallback_to_local_or_played_ini(self):
        (self.root/'vr-release.ini').unlink();(self.root/'vr.ini').write_text('[Debug]\nInvulnerable=1\n')
        with self.assertRaisesRegex(ValueError,'vr-release.ini'):self.run_stage()
    def test_dangerous_template_values_are_forced_off_and_paths_cleared(self):
        text=self.template+'NativeReticle=1\nLaserSight=0\nMountedReticle=0\n[Debug]\nInvulnerable=1\nTestingModifiers=1\n[Recording]\nEnabled=1\n'
        text=text.replace('GamePath=','GamePath=D:\\SteamLibrary\\private').replace('RuntimeJson=','RuntimeJson=C:\\Users\\private\\runtime.json')
        (self.root/'vr-release.ini').write_text(text);self.run_stage()
        stage.verify_defaults((self.output/'vr.ini').read_bytes(),self.version)
    def test_preset_duplicate_case_key_is_rejected(self):
        (self.root/'vr-release.ini').write_text(self.template+'LaserSight=0\nlasersight=1\n')
        with self.assertRaisesRegex(ValueError,'Duplicate'):self.run_stage()
    def test_runtime_version_mismatch_is_refused(self):
        self.ready['version']='0.3.60';self.save_ready()
        with self.assertRaisesRegex(ValueError,'matching checked'):self.run_stage()
    def test_manifest_core_hash_change_is_refused(self):
        (self.package/'LaunchVR.exe').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'differs from checked'):self.run_stage()
    def test_new_hash_in_incoming_manifest_cannot_override_source_lock(self):
        p=self.package/'LaunchVR.exe';p.write_bytes(p.read_bytes()+b'changed');self.ready['files'][p.name]=stage.digest(p.read_bytes());self.save_ready()
        with self.assertRaisesRegex(ValueError,'source-owned release lock'):self.run_stage()
    def test_source_lock_version_mismatch_is_refused(self):
        self.lock['version']='0.3.60';(self.root/'installer/runtime-lock.json').write_text(json.dumps(self.lock))
        with self.assertRaisesRegex(ValueError,'lock does not match'):self.run_stage()
    def test_all_runtime_hash_entries_are_required(self):
        del self.ready['files']['KaneLynchVRAudio.dll'];self.save_ready()
        with self.assertRaisesRegex(ValueError,'Runtime differs'):self.run_stage()
    def test_complete_openxr_is_always_required(self):
        (self.package/'openxr_loader.dll').unlink()
        with self.assertRaisesRegex(ValueError,'openxr_loader.dll'):self.run_stage(fixture=True)
    def test_wrong_host_architecture_is_refused_even_with_both_hashes_updated(self):
        p=self.package/'KaneLynchVRHost.exe';b=bytearray(p.read_bytes());struct.pack_into('<H',b,84,0x14c);p.write_bytes(b)
        self.ready['files'][p.name]=self.lock['files'][p.name]=stage.digest(b);self.save_ready();(self.root/'installer/runtime-lock.json').write_text(json.dumps(self.lock))
        with self.assertRaisesRegex(ValueError,'architecture'):self.run_stage()
    def test_personal_path_in_binary_is_refused(self):
        p=self.package/'LaunchVR.exe';p.write_bytes(p.read_bytes()+r'C:\Users\builder\secret'.encode('utf-16-le'))
        self.ready['files'][p.name]=self.lock['files'][p.name]=stage.digest(p.read_bytes());self.save_ready();(self.root/'installer/runtime-lock.json').write_text(json.dumps(self.lock))
        with self.assertRaisesRegex(ValueError,'Private'):self.run_stage()
    def test_portable_manifest_input_is_supported(self):
        self.build_manifest.unlink();self.ready['version']=self.version+' alpha';(self.package/'PACKAGE-SHA256.json').write_text(json.dumps(self.ready));self.run_stage()
    def test_missing_manifest_is_refused(self):
        self.build_manifest.unlink()
        with self.assertRaisesRegex(ValueError,'checked BUILD'):self.run_stage()
    def test_unlisted_output_and_poisoned_default_are_refused(self):
        self.run_stage();(self.output/'private.txt').write_text('unknown')
        with self.assertRaisesRegex(ValueError,'Unlisted'):stage.verify(self.output)
        (self.output/'private.txt').unlink();p=self.output/'vr.ini';p.write_bytes(p.read_bytes().replace('NativeReticle=0'.encode('utf-16-le'),'NativeReticle=1'.encode('utf-16-le')))
        m=json.loads((self.output/'payload-manifest.json').read_text())
        for e in m['files']:
            if e['file']=='vr.ini':e.update(bytes=p.stat().st_size,sha256=stage.digest(p.read_bytes()))
        (self.output/'payload-manifest.json').write_text(json.dumps(m))
        with self.assertRaisesRegex(ValueError,'Release default mismatch'):stage.verify(self.output)
    def test_game_or_external_runtime_cannot_be_added_to_manifest(self):
        self.run_stage();m=json.loads((self.output/'payload-manifest.json').read_text());p=self.output/'xlive.dll';p.write_bytes(b'MZ external')
        m['files'].append(dict(file=p.name,bytes=p.stat().st_size,sha256=stage.digest(p.read_bytes())));(self.output/'payload-manifest.json').write_text(json.dumps(m))
        with self.assertRaisesRegex(ValueError,'Unapproved'):stage.verify(self.output)
    def test_no_overwrite_nested_stage_or_escaping_path(self):
        self.output.mkdir()
        with self.assertRaisesRegex(ValueError,'new release'):self.run_stage()
        with self.assertRaisesRegex(ValueError,'Unsafe'):stage.checked(self.package,'../outside')
        with self.assertRaisesRegex(ValueError,'separate'):stage.stage(self.root,self.package,self.package/'nested',self.version)
    def test_optional_setup_helper_pair_is_integrity_checked(self):
        b=bytearray((self.package/'KaneLynchVRHost.exe').read_bytes());g=self.base/'guard.exe';g.write_bytes(b)
        (self.root/'installer/cleanup-previous-install.ps1').write_text('source-owned cleanup')
        result=self.run_stage(guard=g);self.assertIn('KaneLynchVRSetupGuard.exe',{e['file'] for e in result['files']})
        (self.output/'KaneLynchVRSetupCleanup.ps1').write_text('changed')
        with self.assertRaisesRegex(ValueError,'integrity'):stage.verify(self.output)
    def test_portable_zip_reproducible_and_complete(self):
        self.run_stage();a=self.base/'a.zip';b=self.base/'b.zip';stage.portable(self.output,a);stage.portable(self.output,b);self.assertEqual(a.read_bytes(),b.read_bytes())
        with zipfile.ZipFile(a) as z:self.assertEqual(set(z.namelist()),{p.relative_to(self.output).as_posix() for p in self.output.rglob('*') if p.is_file()})
    def public_assets(self,fixture=False,legacy=False):
        self.run_stage(fixture=fixture)
        if legacy:
            path=self.output/'payload-manifest.json'
            manifest=json.loads(path.read_text())
            (self.output/'LICENSE').unlink()
            manifest['files']=[r for r in manifest['files'] if r['file']!='LICENSE']
            old_shortcut=b'@echo off\r\n'
            (self.output/'Run-VR.cmd').write_bytes(old_shortcut)
            manifest['files'].append(dict(file='Run-VR.cmd',bytes=len(old_shortcut),sha256=stage.digest(old_shortcut)))
            path.write_text(json.dumps(manifest))
        folder=self.base/'assets';folder.mkdir();exe='KaneLynchVR-'+self.version+'-Setup.exe';archive='KaneLynchVR-'+self.version+'-Portable.zip'
        (folder/exe).write_bytes(b'MZ fixture setup');stage.portable(self.output,folder/archive);entries=[]
        for name in [exe,archive]:
            sha=stage.digest((folder/name).read_bytes());entries.append(dict(file=name,sha256=sha));(folder/(name+'.sha256')).write_bytes((sha+'  '+name+'\n').encode())
        m=dict(schemaVersion=1,product=stage.PRODUCT,version=self.version,fixture=fixture,files=entries,payloadManifestSha256=stage.digest((self.output/'payload-manifest.json').read_bytes()))
        (folder/'release.json').write_text(json.dumps(m));return folder,m
    def test_published_legacy_shortcut_and_missing_root_license_still_verify(self):
        folder,manifest=self.public_assets(legacy=True)
        self.assertEqual(public.verify(folder),manifest)
        self.assertIn('LICENSE',stage.payload_layout('0.3.66')[1])
        self.assertNotIn('Run-VR.cmd',stage.payload_layout('0.3.65')[0])

    def test_public_assets_exclude_private_build_reports(self):
        folder,m=self.public_assets();self.assertEqual(public.verify(folder),m);(folder/'installer-build.json').write_text(r'C:\Users\builder\private')
        with self.assertRaisesRegex(ValueError,'private/unlisted'):public.verify(folder)
    def test_private_source_metadata_is_not_allowed_in_payload_or_release(self):
        folder,m=self.public_assets();m['sourceCommit']='a'*40;(folder/'release.json').write_text(json.dumps(m))
        with self.assertRaisesRegex(ValueError,'unapproved public release metadata'):public.verify(folder)
        payload=json.loads((self.output/'payload-manifest.json').read_text());payload['sourceDirty']=True
        (self.output/'payload-manifest.json').write_text(json.dumps(payload))
        with self.assertRaisesRegex(ValueError,'unapproved payload metadata'):stage.verify(self.output)

    def test_public_fixture_cannot_be_published(self):
        folder,m=self.public_assets(fixture=True)
        with self.assertRaisesRegex(ValueError,'fixture'):public.verify(folder)
        self.assertTrue(public.verify(folder,allow_fixture=True)['fixture'])

if __name__=='__main__':unittest.main(verbosity=2)
