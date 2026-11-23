"""Continuous Kane & Lynch: Dead Men VR recording, with global start/stop hotkeys.

Adapted from the same project family's Kane & Lynch 2 recorder (2026-11-09);
compositor request ABI is separate from game capture and never records a mic.
"""
import argparse
import ctypes as C
import json
import hashlib
from datetime import datetime
from pathlib import Path
import math
import queue
import sys
import traceback
import os
import re
from typing import NamedTuple
import shutil
import struct
import subprocess
import tempfile
import threading
import time
import wave

RECORDING_SIZE = (1920, 1080)
RECORDING_MAGIC, RECORDING_VERSION = 0x52314C4B, 1
RECORDING_MAX_WIDTH, RECORDING_MAX_HEIGHT = 2048, 1536
RECORDING_MAP_SIZE = 128 + RECORDING_MAX_WIDTH * RECORDING_MAX_HEIGHT * 4


def qualified_game_image(data):
    """Same complete-image proof as src/profile.hpp; no game loading."""
    raw = hashlib.sha256(data).hexdigest()
    exact = {
        'a8a2c14a4cb0f8898b8df91d9c4415d397a2bf21bed6ad6e126bad1dfe1e4380',
        '3b70900fc19fc217d4a6ac37c8df7ee0d4de68da3ab175f938b0189759763937',
    }
    if raw in exact:
        return raw, True
    if len(data) != 7542024:
        return raw, False
    normalized = bytearray(data)
    normalized[0x138:0x13c] = bytes(4)  # COFF timestamp
    normalized[0x146] &= ~0x20        # audited LAA flag
    normalized[0x188:0x18c] = bytes(4)  # PE checksum
    return raw, hashlib.sha256(normalized).hexdigest() == '82760829dca5e1d80170ba7c04ef7c3ed6d719a3ade921c6e5277583a8b9f1c8'


def process_session_id(kernel, pid):
    session = C.c_uint32()
    if not kernel.ProcessIdToSessionId(pid, C.byref(session)):
        raise C.WinError(C.get_last_error())
    return session.value


def current_session_id():
    return process_session_id(kernel_api(), os.getpid())


def active_package_path(_package):
    # The mutex/hotkeys are Windows-session global. Package handoff must have
    # the same scope even when a later launch uses a different checkout/root.
    root = Path(os.environ.get('LOCALAPPDATA') or tempfile.gettempdir())
    return root / 'KaneLynchVR' / f'recorder-session-{current_session_id()}' / 'active-package.json'


def publish_active_package(args):
    # An explicit recorder start selects this package even before the game
    # runs. Resolve its live PID on the next clip, unless the launcher can
    # already provide one.
    path = active_package_path(args.package)
    path.parent.mkdir(parents=True, exist_ok=True)
    stage = path.with_suffix(f'.{os.getpid()}.tmp')
    stage.write_text(json.dumps(dict(package=str(args.package), output=str(args.output_directory),
                                    gamePid=args.game_pid)), encoding='utf-8')
    os.replace(stage, path)


def read_active_package(package):
    path = active_package_path(package)
    try:
        if path.stat().st_size > 16384:
            return None
        selected = json.loads(path.read_text('utf-8'))
        folder = Path(selected['package'])
        output = Path(selected['output'])
        pid = selected.get('gamePid', 0)
        if (folder / 'KaneLynchVRHost.exe').is_file() and (folder / 'KaneLynchVR.dll').is_file():
            return folder, output, pid if type(pid) is int and 0 <= pid <= 0xffffffff else 0
    except (OSError, ValueError, TypeError, KeyError, AttributeError):
        pass
    return None


class Frame(NamedTuple):
    width: int
    height: int
    pixels: bytes
    fov: tuple
    kind: int


class CompositorFrames:
    """Own the opt-in recorder request; the host publishes the complete VR view.

    This is separate from the read-only game transport. It never writes to a
    game/host mapping and never takes ownership of an existing recording.
    """
    def __init__(self, kernel, pid, eye, fps):
        self.kernel, self.pid, self.eye, self.fps = kernel, pid, eye, fps
        self.handle = self.address = None
        self.last_error = 0
        self.last_frame = None
        self.stop_event = threading.Event()
        self.thread = None
        kernel.GetTickCount.argtypes = []
        kernel.GetTickCount.restype = C.c_uint32
        kernel.CreateFileMappingW.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32,
            C.c_uint32, C.c_uint32, C.c_wchar_p]
        kernel.CreateFileMappingW.restype = C.c_void_p
        deadline = time.monotonic() + 3
        while True:
            self.handle = kernel.CreateFileMappingW(C.c_void_p(-1), None, 4, 0,
                RECORDING_MAP_SIZE, f'Local\\KaneLynchVR-Recording-{pid}')
            if not self.handle:
                raise C.WinError(C.get_last_error())
            if C.get_last_error() != 183:
                break
            kernel.CloseHandle(self.handle)
            self.handle = None
            if time.monotonic() >= deadline:
                raise RuntimeError('A recorder is already attached to this game; wait for it to finish.')
            time.sleep(.05)  # Previous host may still be releasing a stopped request.
        self.address = kernel.MapViewOfFile(self.handle, 0xF001F, 0, 0, RECORDING_MAP_SIZE)
        if not self.address:
            self.close()
            raise C.WinError(C.get_last_error())
        C.memmove(self.address, struct.pack('<4I', RECORDING_MAGIC, RECORDING_VERSION,
            RECORDING_MAP_SIZE, pid), 16)
        self.publish_request()
        self.thread = threading.Thread(target=self.heartbeat, daemon=True)
        self.thread.start()

    def publish_request(self, stopping=False):
        base = self.address
        sequence = C.c_uint32.from_address(base + 16).value
        C.c_uint32.from_address(base + 16).value = (sequence + 1) & 0xffffffff
        C.memmove(base + 20, struct.pack('<7I', 0 if stopping else self.kernel.GetTickCount(),
            os.getpid(), self.eye, self.fps, *RECORDING_SIZE, 0), 28)
        C.c_uint32.from_address(base + 16).value = (sequence + 2) & 0xffffffff

    def heartbeat(self):
        while not self.stop_event.wait(.25):
            self.publish_request()

    def read(self):
        base = self.address
        for _ in range(3):
            sequence = C.c_uint32.from_address(base + 64).value
            if not sequence or sequence & 1:
                continue
            host, tick, error, kind, width, height, stride = struct.unpack('<2Ii4I', C.string_at(base + 68, 28))
            frame_id = C.c_uint64.from_address(base + 96).value
            fov = struct.unpack('<4f', C.string_at(base + 112, 16))
            fresh = host and tick and ((self.kernel.GetTickCount() - tick) & 0xffffffff) <= 2000
            valid = (fresh and not error and frame_id and kind in (1, 2, 3, 4) and
                     (width, height) == RECORDING_SIZE and stride == width * 4)
            identity = host, frame_id
            changed = valid and identity != self.last_frame
            pixels = C.string_at(base + 128, stride * height) if changed else None
            if sequence != C.c_uint32.from_address(base + 64).value:
                continue
            if fresh and error:
                if error == -92001:
                    raise RuntimeError('This Meta runtime lacks the recording mirror exports. See KaneLynchVRHost.log.')
                if self.last_error != error:
                    print(f'VR recording source error {error}; holding the last frame until recovery.', flush=True)
                self.last_error = error  # Recoverable: keep the timeline and retry after loading.
            elif fresh and valid:
                if self.last_error:
                    print('VR recording source recovered.', flush=True)
                self.last_error = 0
            if changed:
                self.last_frame = identity
                return True, Frame(width, height, pixels, fov, kind)
            return bool(fresh), None
        return False, None

    def stop(self):
        self.stop_event.set()
        if self.thread:
            self.thread.join(timeout=1)
        if self.address and (not self.thread or not self.thread.is_alive()):
            self.publish_request(stopping=True)

    def close(self):
        self.stop()
        if self.address:
            self.kernel.UnmapViewOfFile(self.address)
            self.address = None
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


class SystemAudio(threading.Thread):
    """WASAPI loopback of the selected output; never records a microphone."""
    def __init__(self, path, device):
        # SoundCard initializes its module-wide COM library on the importing
        # thread. Keep that on main; the capture worker initializes COM itself.
        try:
            import soundcard
            import numpy
        except ImportError as error:
            raise RuntimeError('For system audio install: python -m pip install SoundCard') from error
        super().__init__(daemon=True)
        self.path, self.device = path, device
        self.ready, self.stop_event = threading.Event(), threading.Event()
        self.error, self.started, self.device_name = None, 0, ''

    def run(self):
        ole = C.OleDLL('ole32')
        initialized = False
        try:
            ole.CoInitializeEx(None, 0)
            initialized = True
            import soundcard as sc
            import numpy as np
            speaker = sc.get_speaker(self.device) if self.device else sc.default_speaker()
            if speaker is None:
                raise RuntimeError('No audio output device found.')
            self.device_name = speaker.name
            loopback = sc.get_microphone(speaker.id, include_loopback=True)
            with wave.open(str(self.path), 'wb') as wav:
                wav.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
                with loopback.recorder(samplerate=48000, channels=2, blocksize=960) as recorder:
                    self.started = time.monotonic()
                    self.ready.set()
                    while not self.stop_event.is_set():
                        samples = recorder.record(numframes=4800)
                        wav.writeframesraw((np.clip(samples, -1, 1) * 32767).astype('<i2').tobytes())
        except Exception as error:
            self.error = str(error)
        finally:
            self.ready.set()
            if initialized:
                ole.CoUninitialize()

    def finish(self):
        self.stop_event.set()
        self.join(timeout=5)
        if self.is_alive() or self.error:
            raise RuntimeError(f'Audio capture failed: {self.error or "device did not stop"}; files: {self.path.parent}')


def encoder_args(encoder):
    if encoder == 'nvenc':
        return ['-c:v', 'h264_nvenc', '-preset', 'medium', '-rc', 'vbr', '-cq', '20',
                '-b:v', '12M', '-maxrate', '24M', '-bufsize', '24M']
    return ['-c:v', 'libx264', '-preset', 'ultrafast', '-tune', 'zerolatency',
            '-threads', '4', '-crf', '20']


def select_encoder(ffmpeg, requested):
    """Probe the real driver, not just FFmpeg's compiled encoder list."""
    if requested == 'software':
        return 'software'
    probe = subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error',
        '-f', 'lavfi', '-i', 'color=c=black:s=1920x1080:r=30', '-frames:v', '1',
        *encoder_args('nvenc'), '-pix_fmt', 'yuv420p', '-f', 'null', '-'],
        capture_output=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP)
    if probe.returncode == 0:
        return 'nvenc'
    reason = probe.stderr.decode(errors='replace').strip()
    if requested == 'nvenc':
        raise RuntimeError(f'NVIDIA hardware encoder unavailable: {reason}')
    print('NVIDIA encoder unavailable; using software H.264.', flush=True)
    return 'software'


def find_ffmpeg(software=False):
    if software:
        # VD's minimal binary provides NVENC but does not compile libx264.
        # Keep software encoding on the regular full FFmpeg installation.
        return shutil.which('ffmpeg')
    # Some old FFmpeg builds expose an NVENC API unsupported by new drivers.
    # An existing optional Streamer installation supplies another encoder;
    # discover its actual Windows program folders, without assuming a drive.
    for key in ('ProgramW6432', 'ProgramFiles', 'ProgramFiles(x86)'):
        value = os.environ.get(key)
        if not value or not Path(value).is_absolute():
            continue
        installed = Path(value) / 'Virtual Desktop Streamer' / 'ffmpeg.exe'
        if installed.is_file():
            return str(installed)
    return shutil.which('ffmpeg')


def recorder_config(folder):
    import configparser
    try:
        config = configparser.ConfigParser(interpolation=None)
        raw = (Path(folder) / 'vr.ini').read_bytes()
        config.read_string(raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'))
        return config
    except (OSError, ValueError, UnicodeError, configparser.Error):
        return configparser.ConfigParser(interpolation=None)


def diagnostic_logs_enabled(folder):
    try:
        return recorder_config(folder).getint('Diagnostics', 'Enabled', fallback=0) != 0
    except ValueError:
        return False


def recording_encoder(folder, requested):
    mode = recorder_config(folder).get('Connection', 'Mode', fallback='')
    # The VDXR streaming session already uses the GPU video encoder.
    # Keep this optional recording on an independent codec in auto mode.
    return 'software' if requested == 'auto' and mode.strip().casefold() == 'vdxr' else requested


def read_audio_endpoint(kernel, pid, created_filetime):
    """Read only the live process's bounded audio routing mailbox; no log needed."""
    if not kernel or not created_filetime:
        return None
    handle = kernel.OpenFileMappingW(4, False, f'Local\\KaneLynchVR-AudioEndpoint-{pid}')
    if not handle:
        return None
    view = None
    try:
        view = kernel.MapViewOfFile(handle, 4, 0, 0, 88)
        if not view:
            return None
        for _ in range(4):
            raw = C.string_at(view, 88)
            seq, magic, version, size, owner, _, created, installed, redirected, result, _ = struct.unpack('<6IQ2IiI', raw[:48])
            if not seq or seq & 1:
                continue
            if C.c_uint32.from_address(view).value != seq:
                continue
            if (magic, version, size, owner, created) != (0x41454c4b, 1, 88, pid, created_filetime):
                return None
            return dict(pid=owner, installed=installed == 1, redirected=redirected,
                        lastTargetHresult=result, endpointGuid=raw[48:88].split(b'\0', 1)[0].decode('ascii'))
    finally:
        if view:
            kernel.UnmapViewOfFile(view)
        kernel.CloseHandle(handle)
    return None


def launcher_audio_device(pid, explicit, folder, kernel, wait_seconds=3, created_unix=0, created_filetime=0):
    """Follow this live game's actual successful DirectSound target endpoint."""
    if explicit:
        return explicit
    folder = Path(folder)
    candidates = [folder / f'KaneLynchVRAudio-{pid}-status.json']
    if not isinstance(created_unix, (int, float)) or created_unix <= 0:
        return ''  # A PID alone cannot qualify a previous process's route.
    deadline = time.monotonic() + wait_seconds
    while True:
        pending = False
        mailbox = read_audio_endpoint(kernel, pid, created_filetime)
        states = []
        if mailbox:
            states.append(mailbox)
        for path in candidates:
            try:
                info = path.stat()
                if info.st_size > 65536 or info.st_mtime < created_unix:
                    continue
                states.append(json.loads(path.read_text(encoding='utf-8-sig')))
            except (OSError, ValueError, TypeError):
                continue
        for state in states:
            try:
                if not isinstance(state, dict) or state.get('pid') != pid or state.get('installed') is not True:
                    continue
                endpoint = state.get('endpointGuid', '')
                if (state.get('redirected', 0) > 0 and state.get('lastTargetHresult') == 0 and
                        isinstance(endpoint, str) and re.fullmatch(r'\{[0-9a-fA-F-]{36}\}', endpoint)):
                    import soundcard as sc
                    matches = [speaker for speaker in sc.all_speakers()
                               if endpoint.casefold() in speaker.id.casefold()]
                    if len(matches) == 1:
                        print('Audio follows the game: ' + matches[0].name, flush=True)
                        return matches[0].id
                pending = True
            except (OSError, ValueError, TypeError, ImportError):
                continue
        if not pending or time.monotonic() >= deadline:
            return ''
        time.sleep(.1)


def recorded_duration(ffmpeg, video):
    """Read the duration or scan surviving packets if the encoder was interrupted."""
    flags = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
    probe = subprocess.run([ffmpeg, '-nostdin', '-hide_banner', '-i', str(video)],
                           capture_output=True, text=True, errors='replace', timeout=30,
                           creationflags=flags)
    timestamp = r'(\d+):(\d+):(\d+(?:\.\d+)?)'
    def duration(marks):
        return max((float(h)*3600+float(m)*60+float(s) for h,m,s in marks), default=0)
    value = duration(re.findall('Duration: '+timestamp, probe.stderr))
    if math.isfinite(value) and value > 0:
        return value
    scan = subprocess.run([ffmpeg, '-nostdin', '-v', 'error', '-nostats', '-progress', 'pipe:1',
                           '-i', str(video), '-map', '0:v:0', '-c:v', 'copy', '-f', 'null', '-'],
                          capture_output=True, text=True, errors='replace', timeout=120, creationflags=flags)
    value = duration(re.findall('out_time='+timestamp, scan.stdout))
    if math.isfinite(value) and value > 0:
        return value
    raise RuntimeError(f'Cannot determine video duration; captured parts preserved: {video.parent}')


def ebml_prefix(tag, size):
    width = 1
    while size >= (1 << (7*width))-1:
        width += 1
    return tag.to_bytes((tag.bit_length()+7)//8, 'big') + (size | 1 << (7*width)).to_bytes(width, 'big')


def ebml_bytes(tag, value):
    return ebml_prefix(tag, len(value)) + value


def ebml_uint(tag, value):
    return ebml_bytes(tag, value.to_bytes(max(1, (value.bit_length()+7)//8), 'big'))


def timestamped_video_header(width, height, fps):
    # One raw, top-down BGRA track with millisecond timestamps. VFW codec
    # private data is a BITMAPINFOHEADER, and the live segment is unbounded.
    u, e = ebml_uint, ebml_bytes
    header = e(0x1a45dfa3, u(0x4286, 1) + u(0x42f7, 1) + u(0x42f2, 4) + u(0x42f3, 8) +
               e(0x4282, b'matroska') + u(0x4287, 4) + u(0x4285, 2))
    segment = bytes.fromhex('1853806701ffffffffffffff')
    info = e(0x1549a966, u(0x2ad7b1, 1_000_000) + e(0x4d80, b'KL1 Recorder') + e(0x5741, b'KL1 Recorder'))
    bitmap = struct.pack('<IiiHHIIiiII', 40, width, -height, 1, 32, 0, width*height*4, 0, 0, 0, 0)
    track = u(0xd7, 1) + u(0x73c5, 1) + u(0x83, 1) + u(0x23e383, round(1e9/fps))
    track += e(0x86, b'V_MS/VFW/FOURCC') + e(0x63a2, bitmap)
    track += e(0xe0, u(0xb0, width) + u(0xba, height))
    return header + segment + info + e(0x1654ae6b, e(0xae, track))


def write_video_frame(pipe, timestamp, pixels):
    cluster_time = ebml_uint(0xe7, timestamp)
    block = ebml_prefix(0xa3, len(pixels)+4) + b'\x81\x00\x00\x80'
    pipe.write(ebml_prefix(0x1f43b675, len(cluster_time)+len(block)+len(pixels)) + cluster_time + block)
    pipe.write(pixels)


def record_clip(ffmpeg, output, frame, fps, seconds, read_frame, stop_requested,
                normalizer=None, audio_device='', no_audio=False, audio_factory=SystemAudio,
                encoder='software', capture_finished=None, recording_started=None,
                capture_stats=None, recording_progress=None):
    """Always finish captured frames, including after a device/pipe failure."""
    output = Path(output).resolve()
    if output.exists():
        raise RuntimeError(f'Output already exists: {output}. Choose another filename.')
    output.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=output.stem + '-parts-', dir=output.parent))
    video, completed = work / 'video.mkv', work / 'completed.mp4'
    audio_path = work / 'audio.wav'
    audio = process = None
    frames, warnings = 0, []
    stats = capture_stats if capture_stats is not None else {}
    stats.update(encoder=encoder, sourceFrames=1, submittedFrames=0, encodedFrames=0, videoBytes=0)
    published = False
    width, height = normalizer.output if normalizer else frame[:2]
    log_path = output.with_suffix('.ffmpeg.log')
    with log_path.open('wb') as log:
        try:
            if not no_audio:
                audio = audio_factory(audio_path, audio_device)
                audio.start()
                if not audio.ready.wait(timeout=10) or audio.error:
                    raise RuntimeError(f'Cannot start system audio: {audio.error or "timeout"}. Use --list-audio-devices or --audio-device.')
            process = subprocess.Popen([
                ffmpeg, '-hide_banner', '-loglevel', 'warning', '-n',
                '-nostats', '-progress', 'pipe:2', '-fpsprobesize', '0',
                '-f', 'matroska', '-i', 'pipe:0', '-an',
                '-filter_threads', '1',
                '-vf', 'scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2,setsar=1',
                '-vsync', 'passthrough',
                *encoder_args(encoder), '-bf', '0', '-pix_fmt', 'yuv420p',
                '-flush_packets', '1', '-cluster_time_limit', '250', str(video)
            ], stdin=subprocess.PIPE, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP)
            if recording_started:
                recording_started()
            print(f'Recording -> {output}\nVideo: 1920x1080, {fps} FPS, {encoder}. Audio: {audio.device_name if audio else "disabled"}', flush=True)
            started = next_frame = time.monotonic()
            failure = None
            encoder_error = None
            write_started = None
            pending = queue.Queue(maxsize=1)
            finish_video = threading.Event()

            def pixels(image):
                return normalizer.transform(image) if normalizer else image.pixels

            def offer(timestamp, image):
                # Only the latest unencoded sample is useful after a busy
                # pipe. Its timestamp stays relative to the recording start.
                try:
                    pending.get_nowait()
                except queue.Empty:
                    pass
                pending.put_nowait((timestamp, image))

            def encode(first):
                nonlocal frames, encoder_error, write_started
                try:
                    with process.stdin as pipe:
                        write_started = time.monotonic()
                        pipe.write(timestamped_video_header(width, height, fps))
                        write_video_frame(pipe, 0, pixels(first))
                        frames += 1
                        write_started = None
                        while not finish_video.is_set() or not pending.empty():
                            try:
                                timestamp, image = pending.get(timeout=.05)
                            except queue.Empty:
                                continue
                            write_started = time.monotonic()
                            write_video_frame(pipe, timestamp, pixels(image))
                            frames += 1
                            write_started = None
                except Exception as error:
                    encoder_error = error

            writer = threading.Thread(target=encode, args=(frame,), daemon=True)
            writer.start()
            last_timestamp = 0
            source_gap = False
            next_progress = started
            def update_stats():
                stats['submittedFrames'] = frames
                stats['videoBytes'] = video.stat().st_size if video.exists() else 0
                try:
                    with log_path.open('rb') as progress_log:
                        progress_log.seek(max(0, progress_log.seek(0, os.SEEK_END)-4096))
                        reported = re.findall(rb'(?m)^frame=(\d+)', progress_log.read())
                        if reported:
                            stats['encodedFrames'] = int(reported[-1])
                except OSError:
                    pass
            try:
                while True:
                    now = time.monotonic()
                    if recording_progress and now >= next_progress:
                        update_stats()
                        recording_progress()
                        next_progress = now + 1
                    if encoder_error:
                        raise encoder_error
                    if process.poll() is not None:
                        raise RuntimeError(f'Video encoder exited ({process.returncode})')
                    writing = write_started
                    if writing is not None and now - writing > 8:
                        raise RuntimeError('Video encoder stopped accepting frames')
                    if stop_requested() or (seconds and time.monotonic() - started >= seconds):
                        break
                    if audio and audio.error:
                        raise RuntimeError(f'Audio capture stopped: {audio.error}')
                    next_frame = max(next_frame + 1 / fps, time.monotonic())
                    time.sleep(max(0, next_frame - time.monotonic()))
                    if stop_requested() or (seconds and time.monotonic() - started >= seconds):
                        break
                    current = read_frame()
                    if current:
                        if not normalizer and current[:2] != (width, height):
                            warnings.append('Raw eye resolution changed; saved the clip up to that point. Use natural view for continuous recording.')
                            break
                        frame = current
                        stats['sourceFrames'] += 1
                        last_timestamp = max(last_timestamp+1, round((time.monotonic()-started)*1000))
                        offer(last_timestamp, frame)
                        if source_gap:
                            print('VR recording received a new frame again.', flush=True)
                            source_gap = False
                    elif not source_gap and time.monotonic()-started-last_timestamp/1000 >= 2:
                        print('VR recording is waiting for a new compositor frame.', flush=True)
                        source_gap = True
            except KeyboardInterrupt:
                pass
            except Exception as error:
                failure = error
            finally:
                # A final held sample gives an absent source a definite end
                # on the audio timeline without materializing all the gaps.
                end = round((time.monotonic()-started)*1000)
                if not failure and end-last_timestamp >= 1000/fps:
                    offer(end, frame)
                finish_video.set()
                if capture_finished:
                    capture_finished()
                if audio:
                    audio.stop_event.set()
                writer.join(timeout=3 if failure else 8)
                if writer.is_alive():
                    process.kill()
                    writer.join(timeout=3)
                    failure = failure or RuntimeError('Video encoder pipe did not finish')
                failure = failure or encoder_error
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    failure = failure or RuntimeError('Video encoder did not finish in time')
                update_stats()
            if not frames:
                raise RuntimeError(f'No video frames were encoded. See {log_path}') from failure
            if not stats['videoBytes']:
                detail = str(failure) if failure else f'exit {process.returncode}'
                raise RuntimeError(f'Video encoder saved no packets ({detail}). Captured parts: {work}')
            if failure or process.returncode:
                warnings.append(f'Recording interrupted: {failure or "video encoder error"}. Preserved the playable part.')
            if audio:
                try:
                    audio.finish()
                except RuntimeError as error:
                    warnings.append(str(error))
                if audio.is_alive():
                    raise RuntimeError(f'Audio device did not release its file; captured parts preserved: {work}')
            duration = recorded_duration(ffmpeg, video)
            command = [ffmpeg, '-nostdin', '-hide_banner', '-loglevel', 'warning', '-n', '-i', str(video)]
            have_audio = False
            if audio and audio.path.exists():
                try:
                    with wave.open(str(audio.path), 'rb') as wav:
                        have_audio = wav.getnframes() > 0
                except (wave.Error, EOFError, OSError) as error:
                    warnings.append(f'Cannot read captured audio: {error}')
            if have_audio:
                command += ['-ss', str(max(0, started - audio.started)), '-i', str(audio.path),
                            '-map', '0:v:0', '-map', '1:a:0', '-c:a', 'aac', '-b:a', '192k',
                            # An explicit end avoids unbounded apad/shortest muxing.
                            '-af', 'apad', '-t', str(duration)]
            else:
                command += ['-map', '0:v:0', '-an']
                if audio:
                    warnings.append('Audio device supplied no samples; saved video without audio.')
            command += ['-c:v', 'copy', '-movflags', '+faststart', str(completed)]
            print('Saving gameplay MP4... Keep this window open until Saved: appears.', flush=True)
            try:
                result = subprocess.run(command, stderr=log, timeout=max(120, duration * 2),
                                        creationflags=subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP)
            except subprocess.TimeoutExpired as error:
                raise RuntimeError(f'MP4 finalization timed out; captured parts preserved: {work}. See {log_path}') from error
            if result.returncode:
                raise RuntimeError(f'Cannot finish gameplay MP4. See {log_path}; captured parts preserved: {work}')
            # Windows rename is atomic and refuses an existing destination.
            os.rename(completed, output)
            published = True
        finally:
            if audio:
                audio.stop_event.set()
                audio.join(timeout=5)
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            # Remove only our known temporary files, after success or if nothing
            # was ever captured. Never remove recoverable data after an error.
            have_audio_data = audio_path.exists() and audio_path.stat().st_size > 44
            if published or (not frames and not have_audio_data):
                for part in (video, completed, audio_path):
                    if part.exists() and not (audio and audio.is_alive()):
                        part.unlink()
                if not any(work.iterdir()):
                    work.rmdir()
    for warning in warnings:
        print(f'Warning: {warning}', flush=True)
    print(f'Saved: {output}', flush=True)
    return frames, warnings


class ProcessEntry(C.Structure):
    _fields_ = [('size', C.c_uint32), ('usage', C.c_uint32), ('pid', C.c_uint32),
                ('heap', C.c_size_t), ('module_id', C.c_uint32), ('threads', C.c_uint32),
                ('parent_pid', C.c_uint32), ('priority', C.c_int32), ('flags', C.c_uint32),
                ('name', C.c_wchar * 260)]


class ModuleEntry(C.Structure):
    _fields_ = [('size', C.c_uint32), ('module_id', C.c_uint32), ('pid', C.c_uint32),
                ('global_usage', C.c_uint32), ('process_usage', C.c_uint32),
                ('base', C.c_void_p), ('base_size', C.c_uint32), ('module', C.c_void_p),
                ('name', C.c_wchar * 256), ('path', C.c_wchar * 260)]


def kernel_api():
    kernel = C.WinDLL('kernel32', use_last_error=True)
    for name, args, result in (
        ('OpenFileMappingW', [C.c_uint32, C.c_int, C.c_wchar_p], C.c_void_p),
        ('MapViewOfFile', [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_size_t], C.c_void_p),
        ('UnmapViewOfFile', [C.c_void_p], C.c_int),
        ('CloseHandle', [C.c_void_p], C.c_int),
        ('OpenProcess', [C.c_uint32, C.c_int, C.c_uint32], C.c_void_p),
        ('WaitForSingleObject', [C.c_void_p, C.c_uint32], C.c_uint32),
        ('CreateMutexW', [C.c_void_p, C.c_int, C.c_wchar_p], C.c_void_p),
        ('CreateEventW', [C.c_void_p, C.c_int, C.c_int, C.c_wchar_p], C.c_void_p),
        ('OpenEventW', [C.c_uint32, C.c_int, C.c_wchar_p], C.c_void_p),
        ('SetEvent', [C.c_void_p], C.c_int),
        ('QueryFullProcessImageNameW', [C.c_void_p, C.c_uint32, C.c_wchar_p, C.POINTER(C.c_uint32)], C.c_int),
        ('GetProcessTimes', [C.c_void_p] + [C.POINTER(C.c_uint64)] * 4, C.c_int),
        ('ProcessIdToSessionId', [C.c_uint32, C.POINTER(C.c_uint32)], C.c_int),
        ('CreateToolhelp32Snapshot', [C.c_uint32, C.c_uint32], C.c_void_p),
        ('Process32FirstW', [C.c_void_p, C.POINTER(ProcessEntry)], C.c_int),
        ('Process32NextW', [C.c_void_p, C.POINTER(ProcessEntry)], C.c_int),
        ('Module32FirstW', [C.c_void_p, C.POINTER(ModuleEntry)], C.c_int),
        ('Module32NextW', [C.c_void_p, C.POINTER(ModuleEntry)], C.c_int),
    ):
        function = getattr(kernel, name)
        function.argtypes, function.restype = args, result
    return kernel


def snapshot_entries(kernel, flags, pid, entry, first, next_entry):
    for _ in range(8):
        snapshot = kernel.CreateToolhelp32Snapshot(flags, pid)
        if snapshot != C.c_void_p(-1).value:
            break
        error = C.get_last_error()
        if error != 24:  # ERROR_BAD_LENGTH while the loader changes its module list.
            raise C.WinError(error)
    else:
        raise C.WinError(error)
    try:
        entry.size = C.sizeof(entry)
        more = first(snapshot, C.byref(entry))
        while more:
            yield entry
            more = next_entry(snapshot, C.byref(entry))
        error = C.get_last_error()
        if error != 18:  # ERROR_NO_MORE_FILES
            raise C.WinError(error)
    finally:
        kernel.CloseHandle(snapshot)


def package_module_loaded(kernel, pid, package):
    expected = os.path.normcase(str((package / 'KaneLynchVR.dll').resolve()))
    entries = snapshot_entries(kernel, 0x18, pid, ModuleEntry(), kernel.Module32FirstW, kernel.Module32NextW)
    try:
        return any(entry.name.casefold() == 'kanelynchvr.dll' and
                   os.path.normcase(str(Path(entry.path).resolve())) == expected for entry in entries)
    finally:
        entries.close()


class GameProcess:
    """Retain the actual process, so a reused PID cannot extend a recording."""
    def __init__(self, kernel, pid, package=None):
        self.kernel, self.pid = kernel, pid
        self.handle = kernel.OpenProcess(0x101000, False, pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())
        try:
            if not self.alive():
                raise RuntimeError('The game has already exited. Start the game before Ctrl+F10.')
            name, count = C.create_unicode_buffer(32768), C.c_uint32(32768)
            if not kernel.QueryFullProcessImageNameW(self.handle, 0, name, C.byref(count)):
                if not self.alive():
                    raise RuntimeError('The game has already exited. Start the game before Ctrl+F10.')
                raise C.WinError(C.get_last_error())
            creation, exit_time, kernel_time, user_time = (C.c_uint64() for _ in range(4))
            if not kernel.GetProcessTimes(self.handle, C.byref(creation), C.byref(exit_time),
                                          C.byref(kernel_time), C.byref(user_time)):
                raise C.WinError(C.get_last_error())
            self.creation_filetime = creation.value
            self.created_unix = creation.value / 10_000_000 - 11_644_473_600
            self.path = Path(name.value)
            if self.path.name.lower() != 'kaneandlynch.exe':
                raise RuntimeError('The process is not the supported Kane & Lynch: Dead Men executable.')
            if package is not None and not package_module_loaded(kernel, pid, package):
                raise RuntimeError('The running game has no VR module from this package. Start it with this package launcher.')
            self.sha256, supported = qualified_game_image(self.path.read_bytes())
            if not supported:
                raise RuntimeError('Unsupported Kane & Lynch: Dead Men executable; recording was not started.')
            if not self.alive():
                raise RuntimeError('The game has already exited.')
        except BaseException:
            self.close()
            raise

    def alive(self):
        result = self.kernel.WaitForSingleObject(self.handle, 0)
        if result == 0xffffffff:
            raise C.WinError(C.get_last_error())
        return result == 0x102

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def select_game_process(kernel, package, preferred_pid):
    session = process_session_id(kernel, os.getpid())
    entries = snapshot_entries(kernel, 0x2, 0, ProcessEntry(), kernel.Process32FirstW, kernel.Process32NextW)
    candidates = [entry.pid for entry in entries if entry.name.casefold() == 'kaneandlynch.exe']
    if preferred_pid in candidates:
        candidates.remove(preferred_pid)
        candidates.insert(0, preferred_pid)
    selected = None
    failure = None
    for pid in candidates:
        try:
            if process_session_id(kernel, pid) != session:
                continue
            game = GameProcess(kernel, pid, package)
        except (OSError, RuntimeError) as error:
            failure = error
            continue
        if pid == preferred_pid:
            return game
        if selected:
            selected.close()
            game.close()
            raise RuntimeError('Several games use this VR package. Close the extra game or select a game PID.')
        selected = game
    if selected:
        return selected
    if failure:
        raise failure
    raise RuntimeError('Start the game with this package launcher before pressing Ctrl+F10.')


class GlobalHotkeys:
    """Register real global hotkeys; no game hook, polling or focus changes."""
    ACTIONS = {1: 'start', 2: 'stop', 3: 'quit'}

    def __init__(self):
        self.events = queue.Queue()
        self.ready = threading.Event()
        self.error, self.thread_id = None, 0
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()
        if not self.ready.wait(5):
            raise RuntimeError('Hotkey registration timed out.')
        if self.error:
            raise RuntimeError(self.error)

    def run(self):
        from ctypes import wintypes
        user = C.WinDLL('user32', use_last_error=True)
        user.RegisterHotKey.argtypes = [C.c_void_p, C.c_int, C.c_uint, C.c_uint]
        user.UnregisterHotKey.argtypes = [C.c_void_p, C.c_int]
        user.PeekMessageW.argtypes = [C.POINTER(wintypes.MSG), C.c_void_p, C.c_uint, C.c_uint, C.c_uint]
        user.GetAsyncKeyState.argtypes = [C.c_int]
        user.GetAsyncKeyState.restype = C.c_short
        registered = []
        try:
            self.thread_id = C.windll.kernel32.GetCurrentThreadId()
            for number, key in ((1, 0x79), (2, 0x7a), (3, 0x7b)):
                if not user.RegisterHotKey(None, number, 0x4002, key):  # CTRL | NOREPEAT
                    raise RuntimeError(f'Ctrl+F{number+9} is already in use; close the other recorder/application.')
                registered.append(number)
            self.ready.set()
            message = wintypes.MSG()
            held = {number: False for number in self.ACTIONS}
            last_event = (0, 0.0)

            def emit(number):
                nonlocal last_event
                now = time.monotonic()
                if last_event[0] != number or now-last_event[1] >= .15:
                    self.events.put(self.ACTIONS[number])
                    last_event = (number, now)

            while True:
                while user.PeekMessageW(C.byref(message), None, 0, 0, 1):
                    if message.message == 0x12:  # WM_QUIT, our own thread.
                        return
                    if message.message == 0x312 and message.wParam in self.ACTIONS:
                        emit(message.wParam)
                # Some exclusive fullscreen/input paths swallow WM_HOTKEY.
                # Observe the same chords as a fallback; no game hook, input
                # injection or focus change. Rising edges avoid held repeats.
                ctrl = bool(user.GetAsyncKeyState(0x11) & 0x8000)
                for number, key in ((1, 0x79), (2, 0x7a), (3, 0x7b)):
                    down = ctrl and bool(user.GetAsyncKeyState(key) & 0x8000)
                    if down and not held[number]:
                        emit(number)
                    held[number] = down
                time.sleep(.01)
        except Exception as error:
            self.error = str(error)
            self.events.put('quit')
        finally:
            for number in registered:
                user.UnregisterHotKey(None, number)
            self.ready.set()

    def close(self):
        user = C.WinDLL('user32', use_last_error=True)
        user.PostThreadMessageW.argtypes = [C.c_uint32, C.c_uint, C.c_size_t, C.c_ssize_t]
        if self.thread.is_alive():
            user.PostThreadMessageW(self.thread_id, 0x12, 0, 0)  # WM_QUIT, our own thread only
            self.thread.join(3)


def sound(kind, device=''):
    tones = {'requested': (650,), 'start': (900, 1200), 'stop': (1200, 900),
             'saved': (800, 1100), 'cancelled': (550, 550), 'error': (350, 350, 350)}[kind]
    if device:
        try:
            # Use the same output as the game's launcher, including Meta
            # headphones when only the game is routed there. System Beep alone
            # can be inaudible in the headset on a different default endpoint.
            import soundcard as sc
            import numpy as np
            ole = C.WinDLL('ole32')
            result = ole.CoInitializeEx(None, 0)
            try:
                speaker = sc.get_speaker(device)
                segments = []
                for frequency in tones:
                    t = np.arange(4800) / 48000
                    envelope = np.minimum(1, np.minimum(t/.008, (.1-t)/.008))
                    segments.extend((.18*np.sin(2*np.pi*frequency*t)*envelope, np.zeros(1440)))
                samples = np.concatenate(segments)
                speaker.play(np.column_stack((samples, samples)), samplerate=48000)
                return
            finally:
                if result in (0, 1):
                    ole.CoUninitialize()
        except Exception as error:
            print('Recorder signal device fallback: '+str(error), flush=True)
    import winsound
    try:
        for frequency in tones:
            winsound.Beep(frequency, 110)
    except RuntimeError:
        pass


class Recorder:
    def __init__(self, args, kernel):
        self.args, self.kernel = args, kernel
        self.stop_event = threading.Event()
        self.thread = None
        self.encoder = None
        self.encoder_request = None
        self.failed = False
        self.signal_device = args.audio_device

    def state(self, phase, **details):
        value = dict(phase=phase, controllerPid=os.getpid(), updated=time.time(), **details)
        path = self.args.package / 'KaneLynchVR-recorder-status.json'
        stage = path.with_suffix('.tmp')
        stage.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
        os.replace(stage, path)
        print(phase + ': ' + json.dumps(details, ensure_ascii=False), flush=True)

    def start(self):
        if self.thread and self.thread.is_alive():
            return  # Repeated start never splits or overwrites a clip.
        self.stop_event.clear()
        print('Ctrl+F10 received: recording requested.', flush=True)
        self.thread = threading.Thread(target=self.capture, daemon=False)
        self.thread.start()

    def stop(self):
        if self.thread and self.thread.is_alive() and not self.stop_event.is_set():
            print('Stop received: finishing the current recording.', flush=True)
            sound('stop', self.signal_device)
        self.stop_event.set()

    def capture(self):
        game = compositor = None
        capture_stats = {}
        self.failed = False
        try:
            launched_pid = getattr(self.args, 'game_pid', 0)
            if getattr(self.args, 'hotkeys', False):
                selected = read_active_package(self.args.package)
                if selected:
                    self.args.package, self.args.output_directory, launched_pid = selected
            self.state('starting')
            game = select_game_process(self.kernel, self.args.package, launched_pid)
            pid = game.pid
            self.signal_device = launcher_audio_device(pid, self.args.audio_device, self.args.package, self.kernel,
                                                        created_unix=game.created_unix, created_filetime=getattr(game, 'creation_filetime', 0))
            sound('requested', self.signal_device)
            requested = recording_encoder(self.args.package, self.args.encoder)
            if not self.encoder or self.encoder_request not in (None, requested):
                ffmpeg = find_ffmpeg(requested == 'software')
                if not ffmpeg:
                    if requested == 'software':
                        raise RuntimeError('Software recording needs FFmpeg with libx264 on PATH.')
                    raise RuntimeError('FFmpeg is not installed or not on PATH.')
                encoder = select_encoder(ffmpeg, requested)
                if encoder == 'software':
                    ffmpeg = find_ffmpeg(True)
                    if not ffmpeg:
                        raise RuntimeError('Software recording needs FFmpeg with libx264 on PATH.')
                self.encoder = ffmpeg, encoder
                self.encoder_request = requested
            if self.stop_event.is_set():
                self.state('ready', cancelled=True)
                sound('cancelled', self.signal_device)
                return
            ffmpeg, encoder = self.encoder
            compositor = CompositorFrames(self.kernel, pid, 0 if self.args.eye == 'left' else 1, self.args.fps)
            frame = None
            self.state('waiting_for_frames', gamePid=pid)
            # Startup/loading/F9 replacement may take arbitrarily long. Only
            # explicit stop or the retained game process ending cancels this.
            while frame is None and game.alive() and not self.stop_event.is_set():
                _, frame = compositor.read()
                self.stop_event.wait(.03)
            if frame is None:
                self.state('ready', cancelled=True)
                sound('cancelled', self.signal_device)
                return
            audio_device = '' if self.args.no_audio else launcher_audio_device(
                pid, self.args.audio_device, self.args.package, self.kernel, created_unix=game.created_unix, created_filetime=getattr(game, 'creation_filetime', 0))
            output = self.args.output_directory / (datetime.now().strftime('KaneLynch-%Y%m%d-%H%M%S-%f') + '.mp4')

            def began():
                self.state('recording', gamePid=pid, output=str(output), encoder=encoder)
                sound('start', self.signal_device)

            def finished():
                compositor.stop()
                self.state('saving', gamePid=pid, output=str(output))

            def progress():
                try:
                    self.state('recording', gamePid=pid, output=str(output), **capture_stats)
                except OSError:
                    pass  # A status reader may be holding the file open.

            count, warnings = record_clip(ffmpeg, output, frame, self.args.fps, self.args.seconds,
                lambda: compositor.read()[1], lambda: self.stop_event.is_set() or not game.alive(),
                audio_device=audio_device, no_audio=self.args.no_audio, encoder=encoder,
                capture_finished=finished, recording_started=began, capture_stats=capture_stats,
                recording_progress=progress)
            self.state('ready', lastOutput=str(output), frames=count, warnings=warnings, **capture_stats)
            sound('saved', self.signal_device)
        except Exception as error:
            self.failed = True
            self.state('error', message=str(error), **capture_stats)
            traceback.print_exc()
            sound('error', self.signal_device)
        finally:
            if compositor:
                compositor.close()
            if game:
                game.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--game-pid', type=int, default=0, help='Qualified game PID supplied by LaunchVR')
    parser.add_argument('--output-directory', type=Path)
    parser.add_argument('--hotkeys', action='store_true', help='Arm Ctrl+F10 start / Ctrl+F11 stop / Ctrl+F12 exit')
    parser.add_argument('--quit', action='store_true', help='Save and stop the existing recorder controller')
    parser.add_argument('--background', action='store_true', help='Write controller output to logs/recorder-controller.log')
    parser.add_argument('--eye', choices=('left', 'right'), default='left')
    parser.add_argument('--fps', type=int, choices=range(1, 61), default=60)
    parser.add_argument('--seconds', type=float, default=0)
    parser.add_argument('--audio-device', default='')
    parser.add_argument('--no-audio', action='store_true')
    parser.add_argument('--list-audio-devices', action='store_true')
    parser.add_argument('--encoder', choices=('auto', 'nvenc', 'software'), default='auto')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Windows is required.')
    if not math.isfinite(args.seconds) or args.seconds < 0:
        parser.error('--seconds must be zero or positive.')
    if not 0 <= args.game_pid <= 0xffffffff:
        parser.error('--game-pid must be a Windows process ID.')
    args.package = args.package.resolve()
    args.output_directory = (args.output_directory or args.package / 'recordings').resolve()
    if args.background:
        if diagnostic_logs_enabled(args.package):
            log_folder = args.package / 'logs'
            log_folder.mkdir(parents=True, exist_ok=True)
            sys.stdout = sys.stderr = (log_folder / 'recorder-controller.log').open('a', encoding='utf-8', buffering=1)
        else:
            sys.stdout = sys.stderr = open(os.devnull, 'w', encoding='utf-8')
    if args.list_audio_devices:
        import soundcard as sc
        for device in sc.all_speakers():
            print(device.name)
        return 0
    kernel = kernel_api()
    quit_name = 'Local\\KaneLynchVR-Recorder-Quit'
    if args.quit:
        event = kernel.OpenEventW(2, False, quit_name)
        if event:
            try:
                if not kernel.SetEvent(event):
                    raise C.WinError(C.get_last_error())
            finally:
                kernel.CloseHandle(event)
        return 0
    if args.hotkeys:
        publish_active_package(args)
    # Hotkeys are session-global, even if several package launchers are opened.
    mutex = kernel.CreateMutexW(None, False, 'Local\\KaneLynchVR-Recorder')
    if not mutex:
        raise C.WinError(C.get_last_error())
    if C.get_last_error() == 183:
        kernel.CloseHandle(mutex)
        print('Kane & Lynch: Dead Men recorder is already armed.', flush=True)
        return 0
    quit_event = kernel.CreateEventW(None, True, False, quit_name)
    if not quit_event:
        kernel.CloseHandle(mutex)
        raise C.WinError(C.get_last_error())
    recorder, hotkeys = Recorder(args, kernel), None
    try:
        if not args.no_audio:
            # Keep SoundCard's module-wide COM initialization on the main thread.
            # Capture threads initialize their own COM for each clip.
            import soundcard
            import numpy
        if args.hotkeys:
            hotkeys = GlobalHotkeys()
            recorder.state('ready', hotkeys='Ctrl+F10 start; Ctrl+F11 stop/save; Ctrl+F12 exit')
            print('No video/audio is captured until Ctrl+F10. Each start creates a new MP4.', flush=True)
            while True:
                if kernel.WaitForSingleObject(quit_event, 0) == 0:
                    action = 'quit'
                else:
                    try:
                        action = hotkeys.events.get(timeout=.25)
                    except queue.Empty:
                        continue
                if action == 'start':
                    recorder.start()
                else:
                    recorder.stop()
                    if action == 'quit':
                        break
        else:
            recorder.start()
            recorder.thread.join()
    except KeyboardInterrupt:
        recorder.stop()
    except (OSError, RuntimeError, ImportError) as error:
        recorder.failed = True
        recorder.state('error', message=str(error))
        traceback.print_exc()
        sound('error', recorder.signal_device)
    finally:
        recorder.stop()
        if recorder.thread:
            recorder.thread.join()  # Wait for the owned encoder to finish MP4.
        if hotkeys:
            hotkeys.close()
        if not recorder.failed:
            recorder.state('stopped')
        kernel.CloseHandle(quit_event)
        kernel.CloseHandle(mutex)
    return 1 if recorder.failed else 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ImportError) as error:
        print(f'Recording failed: {error}', file=sys.stderr, flush=True)
        raise SystemExit(1)
