"""KL1 recorder: mocked hotkeys, private IPC and synthetic playable A/V.
Adapted from the neighboring KL2 recorder tests; no live game or microphone.
"""
import ctypes as C
import importlib.util
import json
from pathlib import Path
import queue
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch
from types import SimpleNamespace
import os

spec = importlib.util.spec_from_file_location('record_vr', Path(__file__).resolve().parents[2] / 'tools/record-vr.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


class Kl1Qualification(unittest.TestCase):
    def test_complete_image_metadata_normalization_matches_native_profile(self):
        # Exercise real hashing when the locally pinned original is available;
        # never open a game process, GPU or audio endpoint.
        source = Path(os.environ.get('KL1_TEST_GAME_EXE', ''))
        if not source.is_file():
            self.skipTest('original executable unavailable for read-only fixture')
        data = source.read_bytes()
        self.assertEqual(r.qualified_game_image(data)[1], True)
        changed = bytearray(data)
        changed[0x138] ^= 0x55
        changed[0x188] ^= 0x55
        changed[0x146] ^= 0x20
        self.assertTrue(r.qualified_game_image(changed)[1])
        changed[0x1000] ^= 1
        self.assertFalse(r.qualified_game_image(changed)[1])
        self.assertFalse(r.qualified_game_image(data[:-1])[1])
        self.assertFalse(r.qualified_game_image(data+b'0')[1])

    def test_owned_process_keeps_creation_identity_and_pinned_exe(self):
        def image(_handle, _flags, name, _count):
            name.value = r'C:\fixture\KANEANDLYNCH.EXE'
            return 1
        creation_value = 134386423823871004
        def times(_handle, creation, *_):
            creation._obj.value = creation_value
            return 1
        kernel = SimpleNamespace(OpenProcess=Mock(return_value=44), WaitForSingleObject=Mock(return_value=258),
            CloseHandle=Mock(), QueryFullProcessImageNameW=Mock(side_effect=image), GetProcessTimes=Mock(side_effect=times))
        for pinned in ('a8a2c14a4cb0f8898b8df91d9c4415d397a2bf21bed6ad6e126bad1dfe1e4380',
                       '3b70900fc19fc217d4a6ac37c8df7ee0d4de68da3ab175f938b0189759763937'):
            with self.subTest(identity=pinned), \
                 patch.object(r.Path, 'read_bytes', return_value=b'synthetic executable'), \
                 patch.object(r.hashlib, 'sha256', return_value=SimpleNamespace(hexdigest=lambda: pinned)):
                kernel.CloseHandle.reset_mock()
                game = r.GameProcess(kernel, 123)
                self.assertEqual(game.creation_filetime, creation_value)
                self.assertEqual(game.sha256, pinned)
                self.assertGreater(game.created_unix, 1_700_000_000)
                self.assertTrue(game.alive())
                game.close()
                kernel.CloseHandle.assert_called_once_with(44)
        kernel.CloseHandle.reset_mock()
        with patch.object(r.Path, 'read_bytes', return_value=b'unknown executable'):
            with self.assertRaisesRegex(RuntimeError, 'Unsupported Kane'):
                r.GameProcess(kernel, 123)
        kernel.CloseHandle.assert_called_once_with(44)

    def test_windows_process_and_module_snapshots(self):
        kernel = r.kernel_api()
        processes = r.snapshot_entries(kernel, 0x2, 0, r.ProcessEntry(),
            kernel.Process32FirstW, kernel.Process32NextW)
        self.assertIn(os.getpid(), [entry.pid for entry in processes])
        modules = r.snapshot_entries(kernel, 0x18, os.getpid(), r.ModuleEntry(),
            kernel.Module32FirstW, kernel.Module32NextW)
        self.assertIn('kernel32.dll', [entry.name.casefold() for entry in modules])

    def test_process_selection_skips_stale_and_foreign_candidates(self):
        owned = Mock(pid=789)
        entries = [SimpleNamespace(pid=pid, name='kaneandlynch.exe') for pid in (123, 456, 789)]
        kernel = SimpleNamespace(Process32FirstW=Mock(), Process32NextW=Mock())
        package = Path(r'C:\fixture')
        with patch.object(r, 'snapshot_entries', return_value=iter(entries)), \
             patch.object(r, 'process_session_id', side_effect=lambda _kernel, pid: 2 if pid == 456 else 1), \
             patch.object(r, 'GameProcess', side_effect=[RuntimeError('Different package'), owned]) as game:
            self.assertIs(r.select_game_process(kernel, package, 123), owned)
            self.assertEqual([call.args[1] for call in game.call_args_list], [123, 789])

    def test_selection_requires_one_owning_game_or_explicit_live_pid(self):
        entries = [SimpleNamespace(pid=pid, name='kaneandlynch.exe') for pid in (123, 456)]
        kernel = SimpleNamespace(Process32FirstW=Mock(), Process32NextW=Mock())
        games = {entry.pid: Mock(pid=entry.pid) for entry in entries}
        package = Path(r'C:\fixture')
        with patch.object(r, 'snapshot_entries', side_effect=lambda *_: iter(entries)), \
             patch.object(r, 'process_session_id', return_value=1), \
             patch.object(r, 'GameProcess', side_effect=lambda _, pid, _package: games[pid]) as game:
            with self.assertRaisesRegex(RuntimeError, 'Several games'):
                r.select_game_process(kernel, package, 0)
            for selected in games.values():
                selected.close.assert_called_once_with()
            game.reset_mock()
            self.assertIs(r.select_game_process(kernel, package, 456), games[456])
            game.assert_called_once_with(kernel, 456, package)

    def test_game_audio_guid_needs_successful_route_and_current_process(self):
        guid = '{50906B60-C30B-4183-8F83-C13EA6DE115F}'
        speaker = SimpleNamespace(id='{0.0.0.00000000}.'+guid, name='Synthetic headset output')
        soundcard = SimpleNamespace(all_speakers=Mock(return_value=[speaker]))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root/'KaneLynchVRAudio-123-status.json'
            status = dict(pid=123, installed=True, endpointGuid=guid, redirected=2, lastTargetHresult=0)
            path.write_text(json.dumps(status))
            creation = path.stat().st_mtime-1
            with patch.dict(sys.modules, soundcard=soundcard):
                self.assertEqual(r.launcher_audio_device(123, '', root, None, 0, creation), speaker.id)
                self.assertEqual(r.launcher_audio_device(123, '', root, None, 0, path.stat().st_mtime+1), '')
                for changed in (dict(pid=999), dict(redirected=0), dict(lastTargetHresult=-1), dict(installed=False)):
                    path.write_text(json.dumps(status | changed))
                    self.assertEqual(r.launcher_audio_device(123, '', root, None, 0, creation), '')
                path.write_text(json.dumps(status))
                soundcard.all_speakers.return_value = [speaker, speaker]
                self.assertEqual(r.launcher_audio_device(123, '', root, None, 0, creation), '')
                self.assertEqual(r.launcher_audio_device(123, 'explicit device', root, None, 0, creation), 'explicit device')

    def test_logging_requires_explicit_master_for_legacy_and_unicode_profiles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertFalse(r.diagnostic_logs_enabled(root))
            path = root/'vr.ini'
            path.write_text('[Diagnostics]\nPerformanceLogging=1\nVisualDrawCapture=1\n')
            self.assertFalse(r.diagnostic_logs_enabled(root))
            for encoding in ('utf-8-sig', 'utf-16'):
                path.write_text('[Diagnostics]\nEnabled=1\n', encoding=encoding)
                self.assertTrue(r.diagnostic_logs_enabled(root))
            path.write_text('[Diagnostics]\nEnabled=0\n')
            self.assertFalse(r.diagnostic_logs_enabled(root))
            self.assertFalse((root/'logs').exists())

    def test_encoder_auto_failure_falls_back_without_using_real_gpu(self):
        with patch.object(r.subprocess, 'run', return_value=SimpleNamespace(returncode=1, stderr=b'synthetic driver unavailable')) as probe:
            self.assertEqual(r.select_encoder('fixture-ffmpeg', 'auto'), 'software')
            self.assertIn('h264_nvenc', probe.call_args.args[0])
            with self.assertRaisesRegex(RuntimeError, 'NVIDIA hardware encoder unavailable'):
                r.select_encoder('fixture-ffmpeg', 'nvenc')

    def test_vdxr_uses_software_for_auto_but_keeps_explicit_preferences(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            ini = path/'vr.ini'
            ini.write_text('[Connection]\nMode=VDXR\n', encoding='utf-16')
            self.assertEqual(r.recording_encoder(path, 'auto'), 'software')
            self.assertEqual(r.recording_encoder(path, 'nvenc'), 'nvenc')
            ini.write_text('[Connection]\nMode=SteamVR\n', encoding='utf-8')
            self.assertEqual(r.recording_encoder(path, 'auto'), 'auto')
            self.assertEqual(r.recording_encoder(path, 'software'), 'software')

    def test_package_wrapper_resolves_recording_package_without_starting_listener(self):
        wrapper = Path(r.__file__).parents[1]/'scripts/Start-KL1-Recorder.ps1'
        with tempfile.TemporaryDirectory(prefix='KL1 recorder package ') as directory:
            root = Path(directory)
            package = root/'dist-kane-lynch-vr-fixture'
            (package/'tools-read').mkdir(parents=True)
            (package/'KaneLynchVRHost.exe').touch()
            (package/'tools-read/record-vr.py').write_text('# synthetic package, never executed')
            result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(wrapper),
                '-PackageDirectory', str(package), '-ResolveOnly'], capture_output=True, text=True, timeout=10,
                creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(Path(result.stdout.strip()), package)
            self.assertFalse((package/'logs').exists())


    def test_recorder_rejects_incomplete_package(self):
        wrapper = Path(r.__file__).parents[1] / 'scripts/Start-KL1-Recorder.ps1'
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(wrapper),
                 '-PackageDirectory', directory, '-ResolveOnly'],
                capture_output=True, text=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('built or installed mod package', result.stderr)

    def test_different_package_roots_handoff_only_changes_the_next_clip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            old, current = root/'checkout-A'/'portable', root/'checkout-B'/'new package'
            for package in (old, current):
                package.mkdir(parents=True)
                for name in ('KaneLynchVRHost.exe', 'KaneLynchVR.dll'):
                    (package/name).touch()
            initial = SimpleNamespace(package=old, output_directory=old/'recordings', game_pid=123)
            following = SimpleNamespace(package=current, output_directory=current/'recordings', game_pid=0)
            args = SimpleNamespace(package=old, output_directory=old/'recordings', game_pid=0, hotkeys=True,
                audio_device='', eye='left', fps=60, seconds=0, no_audio=True, encoder='software')
            source = Mock()
            source.read.return_value = (True, r.Frame(2, 2, bytes(16), (), 1))
            clips = []
            def clip(_ffmpeg, output, *_, **__):
                clips.append(output)
                if len(clips) == 1:
                    r.publish_active_package(following)
                    self.assertEqual(args.package, old, 'handoff must not mutate a running clip')
                return 12, []
            with patch.dict(os.environ, LOCALAPPDATA=str(root/'local app data')), \
                 patch.object(r, 'current_session_id', return_value=42), \
                 patch.object(r, 'select_game_process', side_effect=[Mock(pid=123), Mock(pid=321)]) as game, \
                 patch.object(r, 'CompositorFrames', return_value=source), \
                 patch.object(r, 'launcher_audio_device', return_value=''), patch.object(r, 'sound'), \
                 patch.object(r, 'record_clip', side_effect=clip):
                self.assertEqual(r.active_package_path(old), r.active_package_path(current))
                r.publish_active_package(initial)
                recorder = r.Recorder(args, None)
                recorder.encoder = ('fixture-ffmpeg', 'software')
                recorder.capture()
                self.assertEqual(game.call_args.args, (None, old, 123))
                self.assertEqual(clips[0].parent, old/'recordings')
                recorder.capture()
                self.assertEqual(game.call_args.args, (None, current, 0))
                self.assertEqual(clips[1].parent, current/'recordings')
                with patch.object(r, 'current_session_id', return_value=43):
                    self.assertNotEqual(r.active_package_path(old).parent.name, 'recorder-session-42')

    def test_registration_failure_does_not_enter_polling_fallback(self):
        user = SimpleNamespace(RegisterHotKey=Mock(return_value=0), UnregisterHotKey=Mock(),
            PeekMessageW=Mock(), GetAsyncKeyState=Mock())
        with patch.object(r.C, 'WinDLL', return_value=user):
            with self.assertRaisesRegex(RuntimeError, 'already in use'):
                r.GlobalHotkeys()
        user.GetAsyncKeyState.assert_not_called()
        user.PeekMessageW.assert_not_called()

    def test_registration_error_is_audible_and_retains_error_status(self):
        kernel = SimpleNamespace(CreateMutexW=Mock(return_value=11), CreateEventW=Mock(return_value=22), CloseHandle=Mock())
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)
            with patch.object(sys, 'argv', ['record-vr.py', '--package', str(package), '--hotkeys', '--no-audio']), \
                 patch.object(r, 'active_package_path', return_value=package/'active.json'), \
                 patch.object(r, 'kernel_api', return_value=kernel), patch.object(r.C, 'get_last_error', return_value=0), \
                 patch.object(r, 'GlobalHotkeys', side_effect=RuntimeError('Ctrl+F10 is already in use')), \
                 patch.object(r, 'sound') as cue:
                self.assertEqual(r.main(), 1)
            state = json.loads((package/'KaneLynchVR-recorder-status.json').read_text(encoding='utf-8'))
            self.assertEqual(state['phase'], 'error')
            self.assertIn('already in use', state['message'])
            cue.assert_called_once_with('error', '')
            self.assertEqual(kernel.CloseHandle.call_count, 2)

    def test_exited_game_reports_actionable_error_before_querying_image(self):
        kernel = SimpleNamespace(OpenProcess=Mock(return_value=1), WaitForSingleObject=Mock(return_value=0),
            CloseHandle=Mock(), QueryFullProcessImageNameW=Mock())
        with self.assertRaisesRegex(RuntimeError, 'already exited'):
            r.GameProcess(kernel, 123)
        kernel.QueryFullProcessImageNameW.assert_not_called()
        kernel.CloseHandle.assert_called_once_with(1)
    def test_fullscreen_fallback_receives_edges_without_wm_hotkey(self):
        stopped = threading.Event()
        step = 0
        def peek(message, *_):
            if stopped.is_set():
                message._obj.message = 0x12
                return 1
            return 0  # Deliberately no WM_HOTKEY delivery.
        def key(number):
            nonlocal step
            if number == 0x11:
                step += 1
                return 0x8000
            return 0x8000 if (number == 0x79 and step <= 5) or (number == 0x7a and 10 <= step <= 15) else 0
        user = SimpleNamespace(RegisterHotKey=Mock(return_value=1), UnregisterHotKey=Mock(),
            PeekMessageW=Mock(side_effect=peek), GetAsyncKeyState=Mock(side_effect=key),
            PostThreadMessageW=Mock(side_effect=lambda *_: stopped.set() or 1))
        with patch.object(r.C, 'WinDLL', return_value=user):
            hotkeys = r.GlobalHotkeys()
            try:
                self.assertEqual(hotkeys.events.get(timeout=2), 'start')
                with self.assertRaises(queue.Empty):
                    hotkeys.events.get(timeout=.025)
                self.assertEqual(hotkeys.events.get(timeout=2), 'stop')
            finally:
                hotkeys.close()
        self.assertFalse(hotkeys.thread.is_alive())

    def test_recorder_signals_request_start_saved_and_cancel_distinctly(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)
            args = SimpleNamespace(package=package, output_directory=package, audio_device='Meta output',
                eye='left', fps=60, seconds=0, no_audio=True, encoder='software')
            frame = r.Frame(2, 2, bytes(16), (), 1)
            source = Mock()
            source.read.return_value = (True, frame)
            def clip(*_, **kw):
                kw['recording_started']()
                kw['capture_finished']()
                return 12, []
            with patch.object(r, 'select_game_process', return_value=Mock(pid=123)) as game, \
                 patch.object(r, 'CompositorFrames', return_value=source), \
                 patch.object(r, 'launcher_audio_device', return_value='Meta output'), \
                 patch.object(r, 'record_clip', side_effect=clip), patch.object(r, 'sound') as cue:
                recorder = r.Recorder(args, None)
                recorder.encoder = ('fixture-ffmpeg', 'software')
                recorder.capture()
                game.assert_called_once_with(None, package, 0)
                self.assertFalse((package/'KaneLynchVR-status.json').exists())
                self.assertEqual([x.args for x in cue.call_args_list],
                    [('requested', 'Meta output'), ('start', 'Meta output'), ('saved', 'Meta output')])
                cue.reset_mock()
                recorder.stop_event.set()
                recorder.capture()
                self.assertEqual([x.args[0] for x in cue.call_args_list], ['requested', 'cancelled'])


    def test_existing_controller_uses_newly_launched_package_and_pid(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            old, current = root/'old', root/'current'
            old.mkdir(); current.mkdir()
            for name in ['KaneLynchVRHost.exe', 'KaneLynchVR.dll']:
                (current/name).touch()
            # No status file yet: a successful native launch supplies its PID
            # directly, so early hotkeys cannot bind an old package's status.
            args = SimpleNamespace(package=current, output_directory=current/'recordings', game_pid=321)
            with patch.object(r, 'active_package_path', return_value=root/'active.json'):
                r.publish_active_package(args)
                args = SimpleNamespace(package=old, output_directory=old, game_pid=0, hotkeys=True,
                    audio_device='', eye='left', fps=60, seconds=0, no_audio=True, encoder='software')
                source = Mock()
                source.read.return_value = (True, r.Frame(2, 2, bytes(16), (), 1))
                with patch.object(r, 'select_game_process', return_value=Mock(pid=321)) as game, \
                     patch.object(r, 'CompositorFrames', return_value=source), \
                     patch.object(r, 'launcher_audio_device', return_value=''), patch.object(r, 'sound'), \
                     patch.object(r, 'record_clip', return_value=(12, [])):
                    recorder = r.Recorder(args, None)
                    recorder.encoder = ('fixture-ffmpeg', 'software')
                    recorder.capture()
                    game.assert_called_once_with(None, current, 321)
                    self.assertFalse(recorder.failed)
                    self.assertEqual(args.package, current)
                    self.assertEqual(args.output_directory, current/'recordings')
                    self.assertFalse((old/'KaneLynchVR-recorder-status.json').exists())
                    self.assertEqual(json.loads((current/'KaneLynchVR-recorder-status.json').read_text())['phase'], 'ready')

    def test_private_wire_holds_invalid_frames_and_recovers_after_replacement(self):
        kernel = r.kernel_api()
        source = r.CompositorFrames(kernel, 0x70000000 + __import__('os').getpid(), 0, 60)
        self.addCleanup(source.close)
        base = source.address
        self.assertEqual(struct.unpack('<4I', C.string_at(base, 16)),
                         (r.RECORDING_MAGIC, 1, r.RECORDING_MAP_SIZE, source.pid))
        request = struct.unpack('<8I', C.string_at(base + 16, 32))
        self.assertEqual(request[3:7], (0, 60, 1920, 1080))
        pixels = bytes((15, 30, 230, 255)) * (1920 * 1080)

        frame_id = 4
        def publish(kind=1, error=0, width=1920, tick=None, host=123):
            nonlocal frame_id
            frame_id += 1
            C.c_uint32.from_address(base + 64).value = 1
            C.memmove(base + 68, struct.pack('<2Ii4I', host, tick or kernel.GetTickCount(),
                error, kind, width, 1080, width * 4), 28)
            C.c_uint64.from_address(base + 96).value = frame_id
            C.memmove(base + 128, pixels, len(pixels))
            C.c_uint32.from_address(base + 64).value = 2

        publish()
        self.assertEqual(source.read()[1].pixels, pixels)
        self.assertEqual(source.read(), (True, None), 'same publication must not copy or encode another image')
        C.c_uint32.from_address(base + 64).value = 3
        self.assertIsNone(source.read()[1], 'host died halfway through publishing')
        publish(width=2049)
        self.assertIsNone(source.read()[1], 'invalid dimensions cannot stop the encoder or overread')
        publish(error=-92003)
        self.assertIsNone(source.read()[1], 'graphics failure keeps recording alive')
        publish(tick=(kernel.GetTickCount() - 3000) & 0xffffffff)
        self.assertIsNone(source.read()[1], 'expired host pixels must be held by the recorder')
        for kind in (2, 3, 4, 1):
            publish(kind=kind, host=456)
            frame = source.read()[1]
            self.assertEqual(frame.kind, kind, 'replacement host/menu/scope resumes on the same request')
        source.stop()
        self.assertEqual(C.c_uint32.from_address(base + 20).value, 0)



class FixtureAudio(threading.Thread):
    def __init__(self, path, device):
        super().__init__(daemon=True)
        self.path, self.error, self.started, self.device_name = path, None, 0, 'synthetic stereo'
        self.ready, self.stop_event = threading.Event(), threading.Event()

    def run(self):
        import wave
        self.started = time.monotonic()
        with wave.open(str(self.path), 'wb') as wav:
            wav.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
            wav.writeframesraw(struct.pack('<hh', 1200, -1200) * 4800)
            self.ready.set()
            while not self.stop_event.wait(.1):
                wav.writeframesraw(struct.pack('<hh', 1200, -1200) * 4800)

    def finish(self):
        self.stop_event.set()
        self.join(2)


@unittest.skipUnless(shutil.which('ffmpeg'), 'FFmpeg is needed')
class ContinuousVideo(unittest.TestCase):
    def test_loading_gap_keeps_wall_time_audio_and_new_mission_in_one_mp4(self):
        initial = r.Frame(64, 36, bytes((15, 15, 220, 255)) * 64 * 36, (), 1)
        following = initial._replace(pixels=bytes((220, 15, 15, 255)) * 64 * 36)
        reads = 0

        def read():
            nonlocal reads
            reads += 1
            if reads == 1:
                time.sleep(2.3)  # A stalled source must not shorten the timeline.
            return following

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'two-missions.mp4'
            count, warnings = r.record_clip(shutil.which('ffmpeg'), output, initial, 10, 3.4,
                read, lambda: False, audio_factory=FixtureAudio)
            self.assertFalse(warnings)
            decoded = subprocess.run([shutil.which('ffmpeg'), '-v', 'error', '-i', str(output),
                '-vsync', '0', '-vf', 'scale=1:1', '-pix_fmt', 'rgb24', '-f', 'rawvideo', 'pipe:1'], capture_output=True)
            self.assertEqual(decoded.returncode, 0, decoded.stderr)
            self.assertGreater(r.recorded_duration(shutil.which('ffmpeg'), output), 3.0)
            self.assertLess(r.recorded_duration(shutil.which('ffmpeg'), output), 4.2)
            self.assertLess(len(decoded.stdout) // 3, 20, 'loading time lives in the timestamps, not encoded duplicates')
            self.assertGreater(decoded.stdout[-1], decoded.stdout[-3] + 100)
            check = subprocess.run([shutil.which('ffmpeg'), '-hide_banner', '-threads', '1', '-i', str(output),
                '-map', '0:v:0', '-map', '0:a:0', '-f', 'null', '-'], capture_output=True)
            self.assertEqual(check.returncode, 0, check.stderr)
            self.assertIn(b'1920x1080', check.stderr)
            self.assertFalse(list(Path(directory).glob('*-parts-*')))

    def test_immediate_stop_and_capture_failure_both_finish_playable_files(self):
        initial = r.Frame(64, 36, bytes((10, 30, 80, 255)) * 64 * 36, (), 1)
        with tempfile.TemporaryDirectory() as directory:
            for name, read, stop in (
                ('immediate', lambda: initial, lambda: True),
                ('failure', lambda: (_ for _ in ()).throw(RuntimeError('source failed')), lambda: False),
            ):
                output = Path(directory) / (name + '.mp4')
                count, warnings = r.record_clip(shutil.which('ffmpeg'), output, initial, 10, 0,
                    read, stop, audio_factory=FixtureAudio)
                self.assertGreater(count, 0)
                self.assertEqual(bool(warnings), name == 'failure')
                check = subprocess.run([shutil.which('ffmpeg'), '-v', 'error', '-i', str(output),
                    '-map', '0:v:0', '-map', '0:a:0', '-f', 'null', '-'], capture_output=True)
                self.assertEqual(check.returncode, 0, check.stderr)

    def test_stopping_does_not_wait_forever_on_an_encoder_pipe(self):
        initial = r.Frame(640, 360, bytes((10, 30, 80, 255)) * 640 * 360, (), 1)
        original_popen = subprocess.Popen

        def blocked_encoder(command, **kwargs):
            return original_popen([sys.executable, '-c', 'import time; time.sleep(60)'], **kwargs)

        with tempfile.TemporaryDirectory() as directory, patch.object(r.subprocess, 'Popen', side_effect=blocked_encoder):
            start = time.monotonic()
            output = Path(directory) / 'blocked-encoder.mp4'
            with self.assertRaisesRegex(RuntimeError, 'No video frames were encoded'):
                r.record_clip(shutil.which('ffmpeg'), output, initial, 60, .2, lambda: None,
                              lambda: False, no_audio=True)
            self.assertLess(time.monotonic()-start, 15)
            self.assertFalse(output.exists())

    def test_encoder_which_consumes_frames_but_saves_nothing_preserves_sound(self):
        initial = r.Frame(64, 36, bytes((10, 30, 80, 255)) * 64 * 36, (), 1)
        original_popen = subprocess.Popen
        def empty_encoder(command, **kwargs):
            code = 'import pathlib, sys; pathlib.Path(sys.argv[1]).touch(); sys.stdin.buffer.read()'
            return original_popen([sys.executable, '-c', code, command[-1]], **kwargs)
        with tempfile.TemporaryDirectory() as directory, patch.object(r.subprocess, 'Popen', side_effect=empty_encoder):
            output = Path(directory) / 'empty-encoder.mp4'
            stats = {}
            with self.assertRaisesRegex(RuntimeError, 'Video encoder saved no packets'):
                r.record_clip(shutil.which('ffmpeg'), output, initial, 60, .25, lambda: initial,
                              lambda: False, audio_factory=FixtureAudio, capture_stats=stats)
            self.assertGreater(stats['submittedFrames'], 0)
            self.assertEqual(stats['encodedFrames'], 0)
            self.assertEqual(stats['videoBytes'], 0)
            surviving_audio = next(Path(directory).glob('*-parts-*/audio.wav'))
            self.assertGreater(surviving_audio.stat().st_size, 44)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
