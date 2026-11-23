"""Compare two recorded KL1 sessions without starting or attaching to a game.

Example: python compare_kl1_performance.py --logs logs --list
         python compare_kl1_performance.py --logs logs --a 7976 --b 22380 --output comparison.json
Rotated segments are selected using their recorded PID *and* creation time;
an ambiguous reused PID requires --a-creation or --b-creation.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import analyze_kl1_performance as analyzer


def select_session(directory: Path, pid: int, creation: int | None = None) -> tuple[tuple[int, int], list[Path]]:
    candidates: dict[tuple[int, int], list[Path]] = {}
    for path in sorted(directory.glob('performance-*.jsonl*')):
        if not path.is_file():
            continue
        try:
            with path.open(encoding='utf-8-sig') as handle:
                header = json.loads(handle.readline())
            if header.get('event') != 'session' or header.get('gamePid') != pid:
                continue
            created = header.get('processCreationFileTime')
            if not isinstance(created, int) or created <= 0:
                continue
            candidates.setdefault((pid, created), []).append(path)
        except (OSError, UnicodeError, json.JSONDecodeError):
            continue
    if creation is not None:
        candidates = {key: files for key, files in candidates.items() if key[1] == creation}
    if not candidates:
        raise ValueError(f'No session for PID {pid} with the requested creation time in {directory}')
    if len(candidates) != 1:
        raise ValueError(f'Ambiguous reused PID {pid}; specify creation time: {sorted(key[1] for key in candidates)}')
    return next(iter(candidates.items()))


def summarize(result: dict[str, Any], identity: tuple[int, int]) -> dict[str, Any]:
    sessions = [s for s in result.get('sessions', []) if (s.get('gamePid'), s.get('processCreationFileTime')) == identity]
    if not sessions:
        raise ValueError(f'Analyzer returned no verified header for {identity}')
    headers = {(s.get('version'), s.get('configuredRenderer'), s.get('gameExeSha256'), s.get('d3d9ModuleSha256')) for s in sessions}
    if len(headers) != 1:
        raise ValueError(f'Inconsistent headers for session {identity}')
    first = sessions[0]
    configs = []
    for group in result.get('sourceConfigurationSummaries', {}).get('groups', []):
        if (group.get('pid'), group.get('creationFileTime')) != identity:
            continue
        rates = group.get('rates', {})
        configs.append({'source': group.get('configuration'),
                        'focusedIntervals': group.get('focusedGameplayAdjacentIntervals'),
                        'presentHz': rates.get('sourceHz'), 'newHostPairsHz': rates.get('hostScenePairHz'),
                        'hostSubmitHz': rates.get('hostSubmitHz')})
    capture = []
    for group in result.get('cpuCaptureStages', {}).get('groups', []):
        if (group.get('pid'), group.get('creationFileTime')) != identity:
            continue
        stages = group.get('stages', {})
        capture.append({'source': group.get('configuration'), 'gpuTransportActive': group.get('gpuTransportActive'),
                        'nativeActionPreserveRig': group.get('nativeActionPreserveRig'),
                        'focusedIntervals': group.get('focusedGameplayAdjacentIntervals'),
                        'mapMsPerCall': stages.get('map', {}).get('weightedMeanMsPerCall'),
                        'mapCalls': stages.get('map', {}).get('callsDelta'),
                        'publishMsPerCall': stages.get('publish', {}).get('weightedMeanMsPerCall'),
                        'publishCalls': stages.get('publish', {}).get('callsDelta')})
    trace = result.get('nativeFrameTrace', {}).get('focusedGameplayStatusBrackets', {})
    return {'pid': identity[0], 'creationFileTime': identity[1], 'version': first.get('version'),
            'renderer': first.get('configuredRenderer'), 'gameExeSha256': first.get('gameExeSha256'),
            'rendererSha256': first.get('d3d9ModuleSha256'), 'samples': result.get('sampleCount'),
            'focusedSceneSamples': result.get('uniqueFocusedSceneSamples'), 'configurations': configs,
            'cpuCapture': capture, 'frameIntervalsMs': trace.get('intervalMs'),
            'repeatedHostSceneFraction': result.get('hostSceneDelivery', {}).get('focusedRepeatedSubmitFraction')}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--logs', required=True, type=Path, help='Directory containing recorded performance JSONL segments')
    parser.add_argument('--list', action='store_true', help='List recorded session identities only')
    parser.add_argument('--a', type=int, help='First game PID')
    parser.add_argument('--b', type=int, help='Second game PID')
    parser.add_argument('--a-creation', type=int, help='First session processCreationFileTime, if PID reused')
    parser.add_argument('--b-creation', type=int, help='Second session processCreationFileTime, if PID reused')
    parser.add_argument('--output', type=Path, help='Where to write comparison JSON')
    args = parser.parse_args()
    if args.list:
        found = {}
        for path in sorted(args.logs.glob('performance-*.jsonl*')):
            try:
                with path.open(encoding='utf-8-sig') as handle:
                    header = json.loads(handle.readline())
                key = (header.get('gamePid'), header.get('processCreationFileTime'))
                if header.get('event') != 'session' or not all(isinstance(n, int) and n > 0 for n in key):
                    continue
                if key not in found:
                    found[key] = {'pid': key[0], 'creationFileTime': key[1], 'utc': header.get('utc'),
                                  'version': header.get('version'), 'renderer': header.get('configuredRenderer'), 'segments': 0}
                found[key]['segments'] += 1
            except (OSError, UnicodeError, json.JSONDecodeError):
                continue
        print(json.dumps(sorted(found.values(), key=lambda row: row['creationFileTime']), ensure_ascii=False, indent=2))
        return
    if args.a is None or args.b is None or args.output is None:
        parser.error('--a, --b and --output are required unless --list is used')
    try:
        a, a_paths = select_session(args.logs, args.a, args.a_creation)
        b, b_paths = select_session(args.logs, args.b, args.b_creation)
        if a == b:
            raise ValueError('Choose two different sessions')
        runs = [summarize(analyzer.analyze(paths), identity) for identity, paths in ((a, a_paths), (b, b_paths))]
    except ValueError as error:
        parser.error(str(error))
    result = {'runs': runs, 'limitations': [
        'This is an observational comparison; different scenes, effects, background activity or headsets can dominate.',
        'The game logs do not establish equal graphics options beyond the recorded source/MSAA fields.',
        'Map can wait on earlier GPU work; publish includes IPC and copying. Do not add overlapping timings.',
        'Present, new complete stereo pairs and compositor submits are different rates.',
        'Use the same checkpoint, view, renderer and all settings except the single option under test.'
    ]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'output': str(args.output), 'sessions': [a, b], 'focusedSceneSamples': [r['focusedSceneSamples'] for r in runs]}))


if __name__ == '__main__':
    main()
