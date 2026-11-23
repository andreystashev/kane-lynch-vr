"""Analyze retained KL1 diagnostics offline; never start or attach to a game."""
import argparse
import hashlib
import json
import math
import pathlib
import re
import statistics
import sys


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def distribution(values):
    values = sorted(float(value) for value in values if number(value))
    if not values:
        return None
    rank = lambda percent: values[max(0, math.ceil(len(values) * percent) - 1)]
    return {"count": len(values), "min": round(values[0], 4), "median": round(statistics.median(values), 4),
            "p95": round(rank(.95), 4), "max": round(values[-1], 4)}


def frame_distribution(values):
    values = sorted(float(value) for value in values if number(value))
    result = distribution(values)
    if result:
        result["p99"] = round(values[max(0, math.ceil(len(values) * .99) - 1)], 4)
    return result


def gameplay_scene(record):
    if not focused_scene(record):
        return False
    game = record["game"]
    return (not any(game.get(field) for field in ("cinematicActive", "actorCinematicApplied", "cinematicPanel",
                                                 "nativeMovieActive", "nativeMenuActive", "nativeLoadingActive"))
            and not game.get("nativeActionKind", 0))


CONFIGURATION_FIELDS = ("renderWidth", "renderHeight", "renderMultisampleType", "renderMultisampleQuality", "sourceWidth", "sourceHeight")
WAIT_SITES = ("producer_camera_wait", "worker_dispatch_wait", "render_join", "worker_start",
              "system_timer_sleep", "task_poll_sleep", "other_native_sleep", "other_native_wait")
BUFFER_FLUSH_PHASES = ("staticFlush", "BeginScene")
BUFFER_FLUSH_RESOURCES = ("unknown", "VB", "IB")


def configuration(record):
    game = record.get("game") or {}
    return tuple(game.get(field) if number(game.get(field)) else None for field in CONFIGURATION_FIELDS)


def process_identity(record):
    process = record.get("process") or {}
    creation = (process.get("game") or {}).get("creationFileTime") if isinstance(process, dict) else None
    return record["_session"], (record.get("game") or {}).get("pid"), creation


def native_wait_trace(records):
    """Retained completed waits and separately sampled in-progress IAT waits."""
    required = {"seq", "tick", "thread", "callerRva", "api", "site", "startUs", "endUs", "wallUs",
                "timeout", "objectCount", "waitAll", "result", "lastError"}
    events, seen, payloads, last = [], set(), set(), {}
    malformed, duplicate, stale, overwrites = 0, 0, 0, 0
    active_seen, active_malformed, active_stale = False, 0, 0
    active_observations = {}
    active_fields = {"thread", "callerRva", "api", "site", "startUs", "ageUs", "timeout", "objectCount", "waitAll"}
    for record in records:
        game = record.get("game") or {}
        trace = game.get("nativeWaitTrace")
        if not isinstance(trace, dict):
            continue
        identity = process_identity(record)
        columns, rows = trace.get("columns"), trace.get("events")
        if (not isinstance(columns, list) or not all(isinstance(column, str) for column in columns)
                or len(set(columns)) != len(columns) or not required.issubset(columns) or not isinstance(rows, list)):
            malformed += 1
            continue
        if not fresh(record, "game"):
            stale += len(rows)
            if isinstance(trace.get("activeWaits"), list):
                active_stale += len(trace["activeWaits"])
            continue
        active_latest = []
        if "activeWaits" in trace:
            active_seen = True
            active_rows = trace["activeWaits"]
            if not isinstance(active_rows, list):
                active_malformed += 1
                active_rows = []
            for item in active_rows:
                if (not isinstance(item, dict) or not active_fields.issubset(item)
                        or any(not number(item[field]) or item[field] < 0 or int(item[field]) != item[field] for field in active_fields)
                        or not item["thread"] or not item["startUs"] or item["api"] >= 3 or item["site"] >= len(WAIT_SITES)
                        or any(item[field] > 0xffffffff for field in active_fields - {"startUs", "ageUs"})):
                    active_malformed += 1
                    continue
                observation = {field: item[field] for field in active_fields}
                active_latest.append(observation)
                key = identity, *(item[field] for field in ("thread", "startUs", "callerRva", "api", "site", "timeout", "objectCount", "waitAll"))
                if key not in active_observations or item["ageUs"] > active_observations[key]["ageUs"]:
                    active_observations[key] = observation
        last[identity] = {field: trace.get(field) for field in ("installed", "enabled", "verified", "droppedEvents", "sites",
                                                              "retentionVersion", "filterThresholdUs", "filteredFastEvents", "activeUntracked")}
        last[identity]["activeWaits"] = active_latest
        payload = (identity, tuple(columns), json.dumps(rows, sort_keys=True), trace.get("overwritten"))
        if payload not in payloads:
            payloads.add(payload)
            count = trace.get("overwritten")
            if number(count) and count >= 0:
                overwrites += count
        for row in rows:
            if not isinstance(row, list) or len(row) != len(columns):
                malformed += 1
                continue
            event = dict(zip(columns, row))
            if (any(not number(event[field]) or event[field] < 0 or int(event[field]) != event[field] for field in required)
                    or not event["seq"] or event["tick"] > 0xffffffff or event["api"] >= 3 or event["site"] >= len(WAIT_SITES)
                    or event["endUs"] < event["startUs"] or event["wallUs"] != event["endUs"] - event["startUs"]
                    or any(event[field] > 0xffffffff for field in ("thread", "callerRva", "timeout", "objectCount", "waitAll", "result", "lastError"))):
                malformed += 1
                continue
            key = identity, event["seq"]
            if key in seen:
                duplicate += 1
                continue
            seen.add(key)
            event.update(_identity=identity)
            events.append(event)
    events.sort(key=lambda event: (repr(event["_identity"]), event["seq"]))
    sites = {}
    for site, name in enumerate(WAIT_SITES):
        selected = [event for event in events if event["site"] == site]
        if selected:
            sites[name] = {"completedCalls": len(selected), "wallMs": frame_distribution(event["wallUs"] / 1000 for event in selected),
                           "timeouts": sum(event["result"] == 258 for event in selected),
                           "failed": sum(event["result"] == 0xffffffff for event in selected)}
    active_ages = {}
    for site, name in enumerate(WAIT_SITES):
        ages = [item["ageUs"] / 1000 for item in active_observations.values() if item["site"] == site]
        if ages:
            active_ages[name] = frame_distribution(ages)
    longest_active = []
    for key, item in sorted(active_observations.items(), key=lambda pair: pair[1]["ageUs"], reverse=True)[:20]:
        identity = key[0]
        longest_active.append({"session": identity[0], "pid": identity[1], "creationFileTime": identity[2],
                               **item, "siteName": WAIT_SITES[item["site"]]})
    report = {"present": bool(last), "uniqueEvents": len(events), "duplicateEvents": duplicate,
              "malformedEventsOrBatches": malformed, "staleStatusEventsRejected": stale, "reportedRingOverwrites": overwrites,
              "completedWaitsBySite": sites,
              "activeWaitObservations": {"present": active_seen, "uniqueObservedWaits": len(active_observations),
                                         "malformedRecordsOrBatches": active_malformed, "staleObservationsRejected": active_stale,
                                         "maximumObservedAgeMsBySite": active_ages, "longestObserved": longest_active},
              "lastCumulativeByProcess": [{"session": key[0], "pid": key[1], "creationFileTime": key[2], **value} for key, value in last.items()],
              "limits": ["Completed events are a retained subset; version 2 omits fast unknown polling from detail while cumulative site counters include every enabled call.",
                         "Overwritten events, contended-ring drops and active-table overflow leave missing evidence; no observed overlap does not prove there was no native wait.",
                         "Active waits are separate snapshots, never added to completed-wait overlap. The same wait may be observed active and later complete.",
                         "These wrappers only observe the game's verified IAT; internal waits in renderer/runtime DLLs are outside this trace.",
                         "Wait wall time includes scheduling. An asynchronous worker waiting for work is not automatically the cause of a render-thread stall.",
                         "Overlapping waits on different threads are dependencies, not additive CPU frame costs."]}
    return report, events


def wait_overlap(event, waits):
    start = event["startUs"] - event["intervalUs"] + event["previousCaptureUs"] + event["previousPresentUs"]
    end = event["startUs"]
    if start > end or start < 0:
        return None
    same, asynchronous = [], []
    for wait in waits.get(event["_identity"], []):
        left, right = max(start, wait["startUs"]), min(end, wait["endUs"])
        if left >= right:
            continue
        detail = {"thread": wait["thread"], "callerRva": wait["callerRva"], "site": WAIT_SITES[wait["site"]],
                  "api": wait["api"], "timeout": wait["timeout"], "result": wait["result"],
                  "waitWallMs": round(wait["wallUs"] / 1000, 4), "overlapMs": round((right - left) / 1000, 4),
                  "_start": left, "_end": right}
        signal, producer = wait.get("drawStartUs"), wait.get("drawStartThread")
        if (detail["site"] == "worker_dispatch_wait" and number(signal) and number(producer) and producer > 0
                and signal > 0 and wait["startUs"] <= signal <= wait["endUs"]):
            detail.update(drawStartThread=producer, beforeDrawStartMs=round((signal - wait["startUs"]) / 1000, 4),
                          afterDrawStartMs=round((wait["endUs"] - signal) / 1000, 4))
        (same if wait["thread"] == event["thread"] else asynchronous).append(detail)
    def union_us(items):
        total, old_end = 0, start
        for item in sorted(items, key=lambda item: item["_start"]):
            total += max(0, item["_end"] - max(old_end, item["_start"]))
            old_end = max(old_end, item["_end"])
        return total
    def details(items):
        return [{key: value for key, value in item.items() if not key.startswith("_")}
                for item in sorted(items, key=lambda item: -item["overlapMs"])[:8]]
    result = {"sameThreadUnionMs": round(union_us(same) / 1000, 4), "sameThreadCalls": len(same),
            "sameThreadWaits": details(same), "asynchronousCalls": len(asynchronous), "asynchronousWaits": details(asynchronous)}
    first_shader = event.get("firstShaderUs")
    if event.get("_pre_shader_valid"):
        before = [{**item, "_end": min(item["_end"], first_shader)} for item in same if item["_start"] < first_shader]
        after = [{**item, "_start": max(item["_start"], first_shader)} for item in same if item["_end"] > first_shader]
        result.update(sameThreadUnionBeforeFirstShaderMs=round(union_us(before) / 1000, 4),
                      sameThreadUnionAfterFirstShaderMs=round(union_us(after) / 1000, 4))
    return result


def native_buffer_flush_trace(records):
    """Optional static shadow upload/BeginScene trace; pending calls stay separate."""
    flags = {"pending", "valid", "shadowReady"}
    required = {"seq", "tick", "thread", "phase", "resource", "startUs", "endUs", "wallUs", "capacity",
                "ranges", "submittedRanges", "bytes", "spanBytes", "result"} | flags
    progress_fields = {"thread", "phase", "resource", "startUs", "ageUs", "pending", "valid", "capacity",
                       "ranges", "submittedRanges", "bytes"}
    integer = lambda value: number(value) and value >= 0 and int(value) == value
    flag = lambda value: isinstance(value, bool) or integer(value) and value <= 1
    events, seen, payloads, last, progress = [], set(), set(), {}, {}
    malformed, duplicate, stale, overwrites, malformed_progress, stale_progress = 0, 0, 0, 0, 0, 0
    for record in records:
        trace = (record.get("game") or {}).get("nativeBufferFlushTrace")
        if not isinstance(trace, dict):
            continue
        identity = process_identity(record)
        columns, rows = trace.get("columns"), trace.get("events")
        if (not isinstance(columns, list) or not all(isinstance(column, str) for column in columns)
                or len(set(columns)) != len(columns) or not required.issubset(columns) or not isinstance(rows, list)):
            malformed += 1
            continue
        if not fresh(record, "game"):
            stale += len(rows)
            stale_progress += isinstance(trace.get("inProgress"), dict)
            continue
        snapshot = {field: trace.get(field) if flag(trace.get(field)) else None
                    for field in ("installed", "enabled", "verified", "batching", "on12", "batchingFaulted", "batchingSupported")}
        snapshot["backend"] = trace.get("backend") if trace.get("backend") in ("Unknown", "Original", "DXVK", "D3D9On12") else None
        snapshot.update({field: trace.get(field) if integer(trace.get(field)) else None
                         for field in ("batchedCalls", "savedLocks", "droppedEvents", "singleMapCalls",
                                       "lockFailures", "unlockFailures", "invalidMappings")})
        phases = trace.get("phases")
        snapshot["phases"] = [{"phase": name, **{field: item.get(field) if integer(item.get(field)) else None
            for field in ("calls", "active", "totalUs", "maxUs", "bytes", "ranges")}}
            for name, item in zip(BUFFER_FLUSH_PHASES, phases) if isinstance(item, dict)] if isinstance(phases, list) else []
        snapshot["inProgress"] = None
        item = trace.get("inProgress")
        if item is not None:
            if (not isinstance(item, dict) or not progress_fields.issubset(item)
                    or any(not integer(item[field]) for field in progress_fields - flags)
                    or any(not flag(item[field]) for field in progress_fields & flags)
                    or not item["thread"] or not item["startUs"] or item["phase"] >= len(BUFFER_FLUSH_PHASES)
                    or item["resource"] >= len(BUFFER_FLUSH_RESOURCES)
                    or any(item[field] > 0xffffffff for field in ("thread", "capacity", "ranges", "submittedRanges"))):
                malformed_progress += 1
            else:
                snapshot["inProgress"] = {field: item[field] for field in progress_fields}
                key = identity, *(item[field] for field in ("thread", "startUs", "phase", "resource"))
                if key not in progress or item["ageUs"] > progress[key]["ageUs"]:
                    progress[key] = snapshot["inProgress"]
        last[identity] = snapshot
        payload = (identity, tuple(columns), json.dumps(rows, sort_keys=True), trace.get("overwritten"))
        if payload not in payloads:
            payloads.add(payload)
            if integer(trace.get("overwritten")):
                overwrites += trace["overwritten"]
        for row in rows:
            if not isinstance(row, list) or len(row) != len(columns):
                malformed += 1
                continue
            event = dict(zip(columns, row))
            if (any(not integer(event[field]) for field in required - flags - {"result"})
                    or any(not flag(event[field]) for field in flags)
                    or not number(event["result"]) or int(event["result"]) != event["result"]
                    or not -0x80000000 <= event["result"] <= 0x7fffffff
                    or not event["seq"] or not event["thread"] or event["phase"] >= len(BUFFER_FLUSH_PHASES)
                    or event["resource"] >= len(BUFFER_FLUSH_RESOURCES)
                    or event["endUs"] < event["startUs"] or event["wallUs"] != event["endUs"] - event["startUs"]
                    or any(event[field] > 0xffffffff for field in ("tick", "thread", "capacity", "ranges", "submittedRanges", "spanBytes"))):
                malformed += 1
                continue
            key = identity, event["seq"]
            if key in seen:
                duplicate += 1
                continue
            seen.add(key)
            event["_identity"] = identity
            events.append(event)
    events.sort(key=lambda event: (repr(event["_identity"]), event["seq"]))
    completed = {}
    for phase, name in enumerate(BUFFER_FLUSH_PHASES):
        selected = [event for event in events if event["phase"] == phase]
        if selected:
            completed[name] = {"completedCalls": len(selected), "wallMs": frame_distribution(event["wallUs"] / 1000 for event in selected),
                "totalWallMs": round(sum(event["wallUs"] for event in selected) / 1000, 4),
                "bytes": sum(event["bytes"] for event in selected), "ranges": sum(event["ranges"] for event in selected),
                "submittedRanges": sum(event["submittedRanges"] for event in selected), "failed": sum(event["result"] < 0 for event in selected)}
    longest = [{"session": key[0][0], "pid": key[0][1], "creationFileTime": key[0][2], **item,
                "phaseName": BUFFER_FLUSH_PHASES[int(item["phase"])], "resourceName": BUFFER_FLUSH_RESOURCES[int(item["resource"])]}
               for key, item in sorted(progress.items(), key=lambda pair: -pair[1]["ageUs"])[:20]]
    return {"present": bool(last), "uniqueEvents": len(events), "duplicateEvents": duplicate,
            "malformedEventsOrBatches": malformed, "staleStatusEventsRejected": stale,
            "reportedRingOverwrites": overwrites, "completedByPhase": completed,
            "inProgressObservations": {"uniqueObservedCalls": len(progress), "malformedRecords": malformed_progress,
                                       "staleObservationsRejected": stale_progress, "longestObserved": longest},
            "lastCumulativeByProcess": [{"session": key[0], "pid": key[1], "creationFileTime": key[2], **value} for key, value in last.items()],
            "limits": ["Completed rows are a retained subset; last cumulative phase totals also include calls absent from the ring.",
                       "Phase active counts completed calls with pending work (or BeginScene), not calls still in progress.",
                       "In-progress ages are sampled separately and are never added to completed duration or pre-camera overlap.",
                       "Pre-camera correlation requires a matching process lifetime, thread and validated camera marker; absent/overwritten evidence does not prove no work occurred.",
                       "Wall durations include driver calls and scheduling; savedLocks counts merged upload locks, not a measured FPS improvement."]}, events


def buffer_flush_overlap(event, flushes):
    if not event.get("_camera_valid") or event["intervalUs"] <= 0:
        return None
    start = event["startUs"] - event["intervalUs"] + event["previousCaptureUs"] + event["previousPresentUs"]
    end = event["firstCameraUs"]
    items = []
    for flush in flushes.get(event["_identity"], []):
        left, right = max(start, flush["startUs"]), min(end, flush["endUs"])
        if flush["thread"] != event["thread"] or left >= right:
            continue
        items.append({"sequence": flush["seq"], "phase": BUFFER_FLUSH_PHASES[int(flush["phase"])],
                      "resource": BUFFER_FLUSH_RESOURCES[int(flush["resource"])], "wallMs": round(flush["wallUs"] / 1000, 4),
                      "overlapMs": round((right - left) / 1000, 4), "ranges": flush["ranges"],
                      "submittedRanges": flush["submittedRanges"], "bytes": flush["bytes"], "result": flush["result"],
                      "_start": left, "_end": right})
    def union_ms(selected):
        total, old_end = 0, start
        for item in sorted(selected, key=lambda item: item["_start"]):
            total += max(0, item["_end"] - max(old_end, item["_start"]))
            old_end = max(old_end, item["_end"])
        return round(total / 1000, 4)
    return {"preCameraMs": round((end - start) / 1000, 4), "sameThreadUnionMs": union_ms(items), "sameThreadCalls": len(items),
            "byPhase": {name: {"calls": sum(item["phase"] == name for item in items),
                               "unionMs": union_ms([item for item in items if item["phase"] == name])} for name in BUFFER_FLUSH_PHASES},
            "longestCalls": [{key: value for key, value in item.items() if not key.startswith("_")}
                             for item in sorted(items, key=lambda item: -item["overlapMs"])[:8]]}


def native_frame_trace(records, wait_events=(), buffer_flush_events=()):
    """Every-frame evidence, separate from the overlapping rolling status windows."""
    events, seen, previous, payloads = [], set(), {}, set()
    waits = {}
    for event in wait_events:
        waits.setdefault(event["_identity"], []).append(event)
    flushes = {}
    for event in buffer_flush_events:
        flushes.setdefault(event["_identity"], []).append(event)
    malformed, duplicate, stale, reported_overwrites = 0, 0, 0, 0
    required = {"seq", "tick", "thread", "present", "startUs", "intervalUs", "cpuUs", "cpuKnown", "captureUs",
                "presentUs", "previousCaptureUs", "previousPresentUs"}
    for record in records:
        game = record.get("game")
        if not isinstance(game, dict):
            if record.get("event") == "sample":
                previous = {key: value for key, value in previous.items() if key[0] != record["_session"]}
            continue
        process = record.get("process") or {}
        creation = (process.get("game") or {}).get("creationFileTime") if isinstance(process, dict) else None
        identity = (record["_session"], game.get("pid"), creation)
        prior = previous.get(identity)
        previous[identity] = record
        trace = game.get("nativeFrameTrace")
        if not isinstance(trace, dict):
            continue
        columns, rows = trace.get("columns"), trace.get("events")
        if (not isinstance(columns, list) or not all(isinstance(column, str) for column in columns)
                or len(set(columns)) != len(columns) or not required.issubset(columns) or not isinstance(rows, list)):
            malformed += 1
            continue
        if not fresh(record, "game"):
            stale += len(rows)
            continue
        payload_key = (identity, tuple(columns), json.dumps(rows, sort_keys=True), trace.get("overwritten"))
        if payload_key not in payloads:
            payloads.add(payload_key)
            overwritten = trace.get("overwritten")
            if number(overwritten) and overwritten >= 0:
                reported_overwrites += overwritten
        elapsed = elapsed_between(record, prior)
        focused = elapsed is not None and focused_scene(record) and focused_scene(prior)
        gameplay = focused and gameplay_scene(record) and gameplay_scene(prior)
        for row in rows:
            if not isinstance(row, list) or len(row) != len(columns):
                malformed += 1
                continue
            event = dict(zip(columns, row))
            numeric = required - {"cpuKnown"}
            if (any(not number(event[field]) or event[field] < 0 or int(event[field]) != event[field] for field in numeric)
                    or event["seq"] <= 0 or event["tick"] > 0xffffffff or event["cpuKnown"] not in (0, 1, False, True)):
                malformed += 1
                continue
            if any(field in event and (not number(event[field]) or event[field] < 0 or int(event[field]) != event[field])
                   for field in ("sourceFrameId", "eye", "kind", "gate")):
                malformed += 1
                continue
            key = (identity, int(event["seq"]))
            if key in seen:
                duplicate += 1
                continue
            # An event has no per-frame focus bit. Require both surrounding
            # fresh status snapshots and a tick inside their interval; never
            # relabel a menu batch as gameplay because the final sample is.
            bracketed = focused and 0 <= ((int(event["tick"]) - int(prior["game"]["updatedTick"])) & 0xffffffff) <= elapsed
            shader_numeric = ("firstShaderUs", "shaderThread", "shaderSpanUs", "shaderCpuUs", "preShaderUs")
            shader_flags = ("shaderCpuKnown", "preShaderKnown", "shaderKnown")
            if (any(field in event and (not number(event[field]) or event[field] < 0 or int(event[field]) != event[field]) for field in shader_numeric)
                    or any(field in event and event[field] not in (0, 1, False, True) for field in shader_flags)):
                malformed += 1
                continue
            if (any(field in event and (not number(event[field]) or event[field] < 0 or int(event[field]) != event[field])
                    for field in ("firstCameraUs", "cameraThread"))
                    or ("cameraKnown" in event and event["cameraKnown"] not in (0, 1, False, True))):
                malformed += 1
                continue
            seen.add(key)
            shader_valid = (event.get("shaderKnown") and all(field in event for field in ("firstShaderUs", "shaderSpanUs", "shaderThread"))
                            and 0 < event["firstShaderUs"] <= event["startUs"]
                            and event["shaderSpanUs"] == event["startUs"] - event["firstShaderUs"])
            previous_end = event["startUs"] - event["intervalUs"] + event["previousCaptureUs"] + event["previousPresentUs"]
            pre_valid = (shader_valid and event.get("preShaderKnown") and "preShaderUs" in event
                         and 0 <= previous_end <= event["firstShaderUs"] and event["preShaderUs"] == event["firstShaderUs"] - previous_end)
            camera_valid = (event.get("cameraKnown") and all(field in event for field in ("firstCameraUs", "cameraThread"))
                            and event["cameraThread"] == event["thread"] and 0 < event["firstCameraUs"] <= event["startUs"]
                            and (not event["intervalUs"] or 0 <= previous_end <= event["firstCameraUs"]))
            camera_split = (camera_valid and pre_valid and event.get("shaderThread") == event["thread"]
                            and previous_end <= event["firstCameraUs"] <= event["firstShaderUs"])
            stable_configuration = configuration(record) == configuration(prior) if prior else False
            event.update(_identity=identity, _session=record["_session"], _pid=game.get("pid"),
                         _file=record["_file"], _line=record["_line"], _utc=record.get("utc"),
                         _configuration=configuration(record) if stable_configuration else None,
                         _shader_valid=bool(shader_valid), _pre_shader_valid=bool(pre_valid),
                         _camera_valid=bool(camera_valid), _camera_split_valid=bool(camera_split),
                         _focused_all=bool(bracketed), _gameplay_all=bool(bracketed and gameplay),
                         _focused=bool(bracketed and event.get("kind") == 1),
                         _gameplay=bool(bracketed and gameplay and event.get("kind") == 1))
            events.append(event)
    events.sort(key=lambda event: (repr(event["_identity"]), event["seq"]))

    def summarize(selected):
        intervals = [event for event in selected if event["intervalUs"] > 0]
        valid_budget = [event for event in intervals
                        if event["intervalUs"] >= event["previousCaptureUs"] + event["previousPresentUs"]]
        cpu = [event for event in intervals if event["cpuKnown"]]
        shader = [event for event in selected if event["_shader_valid"]]
        pre_shader = [event for event in selected if event["_pre_shader_valid"]]
        shader_cpu = [event for event in shader if event.get("shaderCpuKnown") and "shaderCpuUs" in event and event.get("shaderThread") == event["thread"]]
        camera = [event for event in selected if event["_camera_valid"]]
        camera_split = [event for event in selected if event["_camera_split_valid"]]
        stalls = [event for event in intervals if event["intervalUs"] >= 50000]
        prior, repeated_frame, repeated_eye, source_skips, sequence_gaps = {}, 0, 0, 0, 0
        for event in selected:
            old = prior.get(event["_identity"])
            prior[event["_identity"]] = event
            if old is None:
                continue
            if event["seq"] != old["seq"] + 1:
                sequence_gaps += max(0, int(event["seq"] - old["seq"] - 1))
                continue
            if event.get("kind") != 1 or old.get("kind") != 1 or not event.get("sourceFrameId") or not old.get("sourceFrameId"):
                continue
            repeated_frame += event["sourceFrameId"] == old["sourceFrameId"]
            repeated_eye += event.get("eye") == old.get("eye")
            source_skips += max(0, int(event["sourceFrameId"] - old["sourceFrameId"] - 1))
        def stall_details(event):
            outside = event["intervalUs"] - event["previousCaptureUs"] - event["previousPresentUs"]
            dominant = ("invalid_budget" if outside < 0 else
                        max((("outside_capture_present", outside), ("previous_capture", event["previousCaptureUs"]),
                             ("previous_native_present", event["previousPresentUs"])), key=lambda item: item[1])[0])
            return {"session": event["_session"], "pid": event["_pid"], "sequence": event["seq"], "tick": event["tick"],
                    "statusUtc": event["_utc"], "thread": event["thread"], "present": event["present"],
                    "intervalMs": round(event["intervalUs"] / 1000, 4),
                    "previousCaptureMs": round(event["previousCaptureUs"] / 1000, 4),
                    "previousNativePresentMs": round(event["previousPresentUs"] / 1000, 4),
                    "outsideCapturePresentMs": round(outside / 1000, 4) if outside >= 0 else None,
                    "dominantWallRegion": dominant,
                    "threadCpuMs": round(event["cpuUs"] / 1000, 4) if event["cpuKnown"] else None,
                    "threadCpuFraction": round(event["cpuUs"] / event["intervalUs"], 4) if event["cpuKnown"] else None,
                    "firstShaderPhase": {"known": event["_shader_valid"], "preShaderKnown": event["_pre_shader_valid"],
                        "shaderThread": event.get("shaderThread"), "preShaderMs": round(event["preShaderUs"] / 1000, 4) if event["_pre_shader_valid"] else None,
                        "firstShaderToPresentMs": round(event["shaderSpanUs"] / 1000, 4) if event["_shader_valid"] else None,
                        "shaderThreadCpuMs": round(event["shaderCpuUs"] / 1000, 4) if event in shader_cpu else None},
                    "nativeCameraPhase": {"known": event["_camera_valid"], "splitKnown": event["_camera_split_valid"],
                        "cameraThread": event.get("cameraThread"),
                        "previousPresentEndToCameraMs": round((event["firstCameraUs"] - event["firstShaderUs"] + event["preShaderUs"]) / 1000, 4) if event["_camera_split_valid"] else None,
                        "cameraToFirstShaderMs": round((event["firstShaderUs"] - event["firstCameraUs"]) / 1000, 4) if event["_camera_split_valid"] else None,
                        "cameraToPresentMs": round((event["startUs"] - event["firstCameraUs"]) / 1000, 4) if event["_camera_valid"] else None},
                    **{field: event.get(field) for field in ("sourceFrameId", "eye", "kind", "gate", "captureResult", "presentResult")}}
        details = [stall_details(event) for event in stalls]
        worst = sorted(zip(details, stalls), key=lambda pair: -pair[0]["intervalMs"])[:20]
        for detail, event in worst:
            detail["nativeWaitOverlap"] = wait_overlap(event, waits) if event["_identity"] in waits else None
            detail["nativeBufferFlushPreCameraOverlap"] = buffer_flush_overlap(event, flushes) if event["_identity"] in flushes else None
        return {"eventCount": len(selected), "intervalCount": len(intervals),
                "intervalMs": frame_distribution(event["intervalUs"] / 1000 for event in intervals),
                "captureMs": frame_distribution(event["captureUs"] / 1000 for event in selected),
                "nativePresentMs": frame_distribution(event["presentUs"] / 1000 for event in selected),
                "outsideCapturePresentMs": frame_distribution((event["intervalUs"] - event["previousCaptureUs"] - event["previousPresentUs"]) / 1000 for event in valid_budget),
                "threadCpuMs": frame_distribution(event["cpuUs"] / 1000 for event in cpu),
                "threadCpuFraction": frame_distribution(event["cpuUs"] / event["intervalUs"] for event in cpu),
                "cpuKnownIntervals": len(cpu), "invalidAttributionBudgets": len(intervals) - len(valid_budget),
                "firstShaderPhase": {"knownFrames": len(shader), "preShaderKnownFrames": len(pre_shader), "cpuKnownFrames": len(shader_cpu),
                    "firstShaderToPresentMs": frame_distribution(event["shaderSpanUs"] / 1000 for event in shader),
                    "previousPresentEndToFirstShaderMs": frame_distribution(event["preShaderUs"] / 1000 for event in pre_shader),
                    "shaderThreadCpuMs": frame_distribution(event["shaderCpuUs"] / 1000 for event in shader_cpu),
                    "invalidReportedPhases": sum(bool(event.get("shaderKnown")) and not event["_shader_valid"] or
                        bool(event.get("preShaderKnown")) and not event["_pre_shader_valid"] for event in selected)},
                "nativeCameraPhase": {"knownFrames": len(camera), "splitKnownFrames": len(camera_split),
                    "previousPresentEndToFirstCameraMs": frame_distribution((event["firstCameraUs"] - event["firstShaderUs"] + event["preShaderUs"]) / 1000 for event in camera_split),
                    "firstCameraToFirstShaderMs": frame_distribution((event["firstShaderUs"] - event["firstCameraUs"]) / 1000 for event in camera_split),
                    "firstCameraToPresentMs": frame_distribution((event["startUs"] - event["firstCameraUs"]) / 1000 for event in camera),
                    "invalidReportedPhases": sum(bool(event.get("cameraKnown")) and not event["_camera_valid"] for event in selected)},
                "stalls": {"atLeast50Ms": len(stalls), "atLeast100Ms": sum(event["intervalUs"] >= 100000 for event in stalls),
                           "atLeast250Ms": sum(event["intervalUs"] >= 250000 for event in stalls),
                           "dominantWallRegionCounts": {region: sum(detail["dominantWallRegion"] == region for detail in details)
                               for region in ("outside_capture_present", "previous_capture", "previous_native_present", "invalid_budget")}},
                "worstStalls": [detail for detail, _ in worst],
                "consecutiveSceneIdentity": {"repeatedFrameIds": repeated_frame, "repeatedEyes": repeated_eye,
                                             "skippedSourceFrameIds": source_skips},
                "sequenceGapsWithinSelection": sequence_gaps}

    summaries = {"allRetainedNativeFrames": summarize(events),
                 "focusedStatusBracketsAllKinds": summarize([event for event in events if event["_focused_all"]]),
                 "focusedGameplayStatusBracketsAllKinds": summarize([event for event in events if event["_gameplay_all"]]),
                 "focusedSceneStatusBrackets": summarize([event for event in events if event["_focused"]]),
                 "focusedGameplayStatusBrackets": summarize([event for event in events if event["_gameplay"]])}
    groups = {}
    for event in events:
        if event["_gameplay_all"] and event["_configuration"] is not None:
            groups.setdefault((event["_identity"], event["_configuration"]), []).append(event)
    return {"present": bool(events), "uniqueEvents": len(events), "duplicateEvents": duplicate, "malformedEventsOrBatches": malformed,
            "staleStatusEventsRejected": stale, "reportedRingOverwrites": reported_overwrites,
            **summaries,
            "focusedGameplayBySourceConfiguration": [{"session": key[0][0], "pid": key[0][1], "creationFileTime": key[0][2],
                "configuration": dict(zip(CONFIGURATION_FIELDS, key[1])), "frames": summarize(values)} for key, values in groups.items()],
            "limits": ["Each interval is start-to-start; its capture/Present attribution belongs to the previous event, not the current capture.",
                       "Focus/gameplay selection uses adjacent fresh status snapshots and event kind, not an exact per-frame focus history.",
                       "All-kinds gameplay brackets include failed/stale scene captures labelled kind2; accepted kind1 alone can omit the worst stalls.",
                       "Thread CPU covers the whole interval only on the same native thread; it does not identify a blocking API or GPU utilization.",
                       "First-shader phases use an observed native shader marker, not the beginning/end of GPU execution; missing markers remain unknown.",
                       "Same-thread wait overlap is clipped to the preceding Present-end→current Present-start interval. Asynchronous waits are reported separately, never added as a render cost.",
                       "Repeated source identities can be retries; sequence gaps and ring overwrites are missing evidence, not measured dropped gameplay frames."]}


def host_scene_delivery(records):
    counters = ("freshPairs", "sceneSubmits", "repeatedSceneSubmits", "sourcePairIdGaps",
                "holdEpisodes50Ms", "holdEpisodes100Ms", "holdEpisodes250Ms")
    gauges = ("lastNewPairAgeMs", "pairIntervalMedianMs", "pairIntervalP95Ms", "pairIntervalP99Ms", "pairIntervalMaxMs")
    previous, last, windows, selected = {}, {}, [], []
    deltas = {field: 0 for field in counters}
    for record in records:
        if not fresh(record, "game") or not fresh(record, "host"):
            previous = {key: value for key, value in previous.items() if key[0] != record["_session"]}
            continue
        game, host = record["game"], record["host"]
        if host.get("gamePid") != game.get("pid") or not isinstance(host.get("sceneDelivery"), dict):
            continue
        process = record.get("process") or {}
        creation = (process.get("host") or {}).get("creationFileTime") if isinstance(process, dict) else None
        key = (record["_session"], host.get("hostPid"), creation)
        prior = previous.get(key)
        previous[key] = record
        metrics = host["sceneDelivery"]
        clean = {field: metrics[field] for field in (*counters, *gauges, "maxRepeatAgeMs")
                 if number(metrics.get(field)) and metrics[field] >= 0}
        last[key] = clean
        if focused_scene(record):
            selected.append(clean)
        if prior is None or not focused_scene(record) or not focused_scene(prior) or elapsed_between(record, prior) is None:
            continue
        old = prior["host"].get("sceneDelivery") or {}
        window = {}
        for field in counters:
            value, before = metrics.get(field), old.get(field)
            if number(value) and number(before) and value >= before >= 0:
                window[field] = value - before
        # A host reset/counter regression must not mix one valid counter delta
        # with a different lifetime's denominator or silently imply zero holds.
        if len(window) != len(counters):
            continue
        if window["repeatedSceneSubmits"] > window["sceneSubmits"]:
            continue
        windows.append(window)
        for field, value in window.items():
            deltas[field] += value
    submitted = deltas["sceneSubmits"]
    return {"present": bool(last), "focusedSamples": len(selected), "focusedAdjacentIntervals": len(windows),
            "focusedAdjacentCounterDeltas": deltas,
            "focusedRepeatedSubmitFraction": deltas["repeatedSceneSubmits"] / submitted if submitted else None,
            "reportedRollingPairArrivalWindows": {field: distribution(metrics.get(field) for metrics in selected) for field in gauges},
            "lastCumulativeByHost": [{"session": key[0], "hostPid": key[1], "creationFileTime": key[2],
                                      "metrics": metrics} for key, metrics in last.items()],
            "limits": ["Fresh pairs count distinct complete image arrivals/uploads; successful scene submits and repeated submits are separate.",
                       "Pair-interval median/p95/p99 are reported rolling arrival spacing, not compositor refresh and not exclusive render time.",
                       "sourcePairIdGaps also include invalidated or never-published source identities; they do not prove transport drops.",
                       "Hold episode thresholds count each held pair once per threshold; repeated compositor submits are expected during normal timewarp."]}


def fresh(record, scope):
    status = record.get(scope)
    freshness = record.get("freshness") or {}
    return (isinstance(status, dict) and isinstance(freshness, dict) and freshness.get(scope + "Valid") is not False
            and status.get("statusFresh") is not False and status.get("processAlive") is not False)


def focused_scene(record):
    if not fresh(record, "game") or not fresh(record, "host"):
        return False
    game, host = record["game"], record["host"]
    return (game.get("gameFocused") is True and host.get("gamePid") == game.get("pid")
            and host.get("hmdMounted") is not False and host.get("renderMode") in ("scene", "scene_hold")
            and not str(host.get("phase", "")).startswith("stopped"))


def elapsed_between(record, previous):
    if previous is None or not fresh(record, "game") or not fresh(previous, "game"):
        return None
    tick, before = record["game"].get("updatedTick"), previous["game"].get("updatedTick")
    if not number(tick) or not number(before):
        return None
    elapsed = (int(tick) - int(before)) & 0xffffffff
    return elapsed if 0 < elapsed <= 3000 else None


def gate_window(record, previous, elapsed):
    current, prior = record["game"].get("sceneGateAttempts"), previous["game"].get("sceneGateAttempts")
    if not isinstance(current, dict) or not isinstance(prior, dict) or current.keys() != prior.keys():
        return None
    if any(not number(value) or not number(prior[name]) or value < prior[name] for name, value in current.items()):
        return None
    deltas = {name: value - prior[name] for name, value in current.items()}
    total = sum(deltas.values())
    return {"attempts": deltas, "total": total, "rejectionPercent": 100 * (total - deltas.get("ready", 0)) / total if total else None,
            "attemptsHz": 1000 * total / elapsed}


def summarize_gates(windows):
    counts = {}
    for window in windows:
        for name, delta in window["attempts"].items():
            counts[name] = counts.get(name, 0) + delta
    total, ready = sum(counts.values()), counts.get("ready", 0)
    return {"intervalCount": len(windows), "totalAttemptsDelta": total, "readyAttemptsDelta": ready,
            "rejectedAttemptsDelta": total - ready, "rejectionPercent": 100 * (total - ready) / total if total else None,
            "attemptsHz": distribution(window["attemptsHz"] for window in windows),
            "windowRejectionPercent": distribution(window["rejectionPercent"] for window in windows),
            "reasons": [{"name": name, "attemptsDelta": delta, "percentOfAttempts": 100 * delta / total if total else None}
                        for name, delta in sorted(counts.items(), key=lambda item: (-item[1], item[0]))]}


GAME_FIELDS = ["frameIntervalMedianMs", "frameIntervalP95Ms", "frameIntervalP99Ms", "renderThreadCpuPercent",
               "renderThreadCpuMeanMs", "captureMedianMs", "captureP95Ms", "nativePresentMedianMs", "nativePresentP95Ms"]
HOST_FIELDS = ["newScenePairHz", "hostSubmitHz", "ipcReadCpuMeanMs", "uploadCpuMeanMs", "submitCpuMeanMs",
               "leftFrameAgeMs", "rightFrameAgeMs", "submitStartIntervalMeanMs", "submitStartIntervalP95Ms", "submitCallP95Ms"]
RATE_FIELDS = ["sourceHz", "sceneEyeHz", "scenePairHz", "hostSubmitHz", "hostScenePairHz"]
COUNTERS = ["presents", "captured", "sceneFrames", "gpuCopies", "gpuBusy", "gpuFailures", "captureFailures",
            "frameStalls50Ms", "captureStalls50Ms", "presentStalls50Ms", "resetCalls", "resetFailures"]


def load_records(paths):
    sources, sessions, records, invalid = [], [], [], 0
    for path in paths:
        path = pathlib.Path(path)
        data = path.read_bytes()
        sources.append({"file": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        text = data.decode("utf-8-sig", errors="replace")
        try:
            complete = json.loads(text)
        except ValueError:
            complete = None
        lines = [complete] if isinstance(complete, dict) else text.splitlines()
        session = re.sub(r"\.jsonl(?:\.\d+)?$", "", str(path))
        for index, line in enumerate(lines, 1):
            try:
                record = line if isinstance(line, dict) else json.loads(line)
            except ValueError:
                invalid += bool(str(line).strip())
                continue
            if not isinstance(record, dict):
                continue
            if record.get("event") == "session":
                session = str(record.get("utc", session)) + ":" + str(record.get("gamePid", "")) + ":" + str(record.get("processCreationFileTime", ""))
                sessions.append(record)
            elif "pid" in record and "presents" in record and "game" not in record:
                record = {"event": "status_snapshot", "game": record, "host": None}
            record = dict(record, _session=session, _file=str(path), _line=index)
            records.append(record)
    return sources, sessions, records, invalid


def sample_rates(record, previous):
    supplied = record.get("rates")
    has_rates = isinstance(supplied, dict)
    output = {field: supplied.get(field) if has_rates and number(supplied.get(field)) else None for field in RATE_FIELDS}
    if has_rates and "hostWindowMs" in supplied:
        return output
    elapsed = elapsed_between(record, previous)
    if elapsed is None:
        return output
    game, prior = record["game"], previous["game"]
    if game.get("pid") != prior.get("pid"):
        return output

    def rate(current, old, key, duration):
        value, start = current.get(key), old.get(key)
        return 1000.0 * (value - start) / duration if duration > 0 and number(value) and number(start) and value >= start else None

    if not has_rates:
        output["sourceHz"] = rate(game, prior, "presents", elapsed)
        output["sceneEyeHz"] = rate(game, prior, "sceneFrames", elapsed)
        output["scenePairHz"] = output["sceneEyeHz"] / 2 if output["sceneEyeHz"] is not None else None
    if fresh(record, "host") and fresh(previous, "host"):
        host, old_host = record["host"], previous["host"]
        if (host.get("gamePid") == game.get("pid") and old_host.get("gamePid") == game.get("pid")
                and host.get("hostPid") == old_host.get("hostPid")):
            age = (record.get("freshness") or {}).get("hostAgeMs")
            old_age = (previous.get("freshness") or {}).get("hostAgeMs")
            now, before = record.get("elapsedMs"), previous.get("elapsedMs")
            # Early loggers used game sampling time to divide host deltas.
            # Recover the host report interval from the recorded ages when present.
            if all(number(value) and value >= 0 for value in (age, old_age, now, before)):
                host_elapsed = now - before + old_age - age if host != old_host else 0
            elif not has_rates:
                host_elapsed = elapsed
            else:
                return output
            output["hostSubmitHz"] = rate(host, old_host, "framesSubmitted", host_elapsed)
            output["hostScenePairHz"] = rate(host, old_host, "newScenePairs", host_elapsed)
    return output


def source_configuration_summaries(records):
    previous, groups, seen = {}, {}, set()
    for record in records:
        if not isinstance(record.get("game"), dict):
            if record.get("event") == "sample":
                previous = {key: value for key, value in previous.items() if key[0] != record["_session"]}
            continue
        identity = process_identity(record)
        key = identity, record["game"].get("updatedTick")
        if key in seen:
            continue
        seen.add(key)
        prior = previous.get(identity)
        previous[identity] = record
        if not fresh(record, "game"):
            continue
        config = configuration(record)
        group = groups.setdefault((identity, config), {"samples": 0, "intervals": [], "rolling": [], "hostSizes": set(), "eyeSizes": set()})
        group["samples"] += 1
        if focused_scene(record) and fresh(record, "host"):
            host = record["host"]
            width, height = host.get("sourceWidth"), host.get("sourceHeight")
            if number(width) and number(height):
                group["hostSizes"].add((width, height))
            sizes = host.get("recommendedEyeSize")
            if (isinstance(sizes, list) and len(sizes) == 2 and all(isinstance(eye, list) and len(eye) == 2
                    and all(number(value) and value >= 0 for value in eye) for eye in sizes)):
                group["eyeSizes"].add(tuple(tuple(eye) for eye in sizes))
        if (prior is not None and config == configuration(prior) and elapsed_between(record, prior) is not None
                and gameplay_scene(record) and gameplay_scene(prior)):
            group["intervals"].append(sample_rates(record, prior))
            group["rolling"].append(record["game"])
    return {"groups": [{"session": key[0][0], "pid": key[0][1], "creationFileTime": key[0][2],
                "configuration": dict(zip(CONFIGURATION_FIELDS, key[1])), "freshSamples": value["samples"],
                "focusedGameplayAdjacentIntervals": len(value["intervals"]),
                "rates": {field: distribution(window.get(field) for window in value["intervals"]) for field in RATE_FIELDS},
                "reportedRollingWindows": {field: distribution(window.get(field) for window in value["rolling"]) for field in GAME_FIELDS},
                "hostSourceDimensions": sorted(value["hostSizes"]), "recommendedPerEyeDimensions": sorted(value["eyeSizes"])}
                for key, value in groups.items()],
            "limits": ["Rate windows require adjacent focused non-cinematic snapshots with unchanged source/render dimensions and MSAA configuration.",
                       "Groups are separated by session/PID/process creation; different routes or settings are not a controlled A/B benchmark.",
                       "Reported rolling timing windows can still include frames preceding a setting change; exact per-frame distributions are separate.",
                       "Source pixels, runtime-recommended per-eye dimensions and headset stream encoding are different quantities; encoder dimensions/bitrate are not inferred."]}


def cpu_capture_stage_summaries(records):
    """Disjoint capture-stage counter deltas; never infer old missing fields."""
    names = ("resolve", "readback", "map", "convert", "publish", "unmap")
    groups, previous, seen = {}, {}, set()
    payloads, invalid, missing = 0, 0, 0
    integer = lambda value: number(value) and value >= 0 and int(value) == value
    for record in records:
        game = record.get("game")
        if not isinstance(game, dict):
            if record.get("event") == "sample":
                previous = {key: value for key, value in previous.items() if key[0] != record["_session"]}
            continue
        identity = process_identity(record)
        key = identity, game.get("updatedTick")
        if key in seen:
            continue
        seen.add(key)
        prior = previous.get(identity)
        previous[identity] = record
        current = game.get("cpuCaptureStages")
        if isinstance(current, dict) and any(name in current for name in names):
            payloads += 1
        elapsed = elapsed_between(record, prior)
        if (prior is None or elapsed is None or not gameplay_scene(record) or not gameplay_scene(prior)
                or configuration(record) != configuration(prior)):
            continue
        before = prior["game"]
        previous_stages = before.get("cpuCaptureStages")
        if not isinstance(current, dict) and not isinstance(previous_stages, dict):
            continue
        if (game.get("gpuTransportActive") != before.get("gpuTransportActive")
                or game.get("nativeActionPreserveRig") != before.get("nativeActionPreserveRig")):
            continue
        group_key = identity, configuration(record), game.get("gpuTransportActive"), game.get("nativeActionPreserveRig")
        group = groups.setdefault(group_key, {"windows": 0, "stages": {name: {"calls": 0, "totalUs": 0,
            "windows": 0, "msPerCall": [], "msPerSourceFrame": [], "sessionMaxMs": []} for name in names}})
        group["windows"] += 1
        source_delta = game.get("presents", 0) - before.get("presents", 0) if all(number(value) for value in (game.get("presents"), before.get("presents"))) else 0
        for name in names:
            now = current.get(name) if isinstance(current, dict) else None
            old = previous_stages.get(name) if isinstance(previous_stages, dict) else None
            if not isinstance(now, dict) or not isinstance(old, dict):
                missing += 1
                continue
            if any(not integer(value.get(field)) for value in (old, now) for field in ("calls", "totalUs", "maxUs")):
                invalid += 1
                continue
            calls, total = now["calls"] - old["calls"], now["totalUs"] - old["totalUs"]
            if calls < 0 or total < 0 or now["maxUs"] < old["maxUs"] or (not calls and total):
                invalid += 1
                continue
            stage = group["stages"][name]
            stage["calls"] += calls
            stage["totalUs"] += total
            stage["windows"] += 1
            stage["sessionMaxMs"].append(now["maxUs"] / 1000)
            if calls:
                stage["msPerCall"].append(total / calls / 1000)
            if source_delta > 0:
                stage["msPerSourceFrame"].append(total / source_delta / 1000)
    output = []
    for key, value in groups.items():
        output.append({"session": key[0][0], "pid": key[0][1], "creationFileTime": key[0][2],
            "configuration": dict(zip(CONFIGURATION_FIELDS, key[1])), "gpuTransportActive": key[2],
            "nativeActionPreserveRig": key[3], "focusedGameplayAdjacentIntervals": value["windows"],
            "stages": {name: {"validIntervals": stage["windows"], "callsDelta": stage["calls"],
                "totalUsDelta": stage["totalUs"], "weightedMeanMsPerCall": round(stage["totalUs"] / stage["calls"] / 1000, 4) if stage["calls"] else None,
                "windowMeanMsPerCall": distribution(stage["msPerCall"]), "windowMsPerSourceFrame": distribution(stage["msPerSourceFrame"]),
                "sessionMaxObservedCallMs": max(stage["sessionMaxMs"]) if stage["sessionMaxMs"] else None}
                for name, stage in value["stages"].items()}})
    return {"present": payloads > 0, "uniquePayloadSnapshots": payloads, "invalidOrResetStageIntervals": invalid,
            "missingStageIntervals": missing, "groups": output,
            "limits": ["Only adjacent fresh focused gameplay snapshots with unchanged source/MSAA/transport and native-rig policy are grouped.",
                       "Map is LockRect wall time and can wait for earlier asynchronous GPU work; it is not pure CPU work or a copy bandwidth measurement.",
                       "Publish includes scene admission, IPC synchronization and the existing pixel copy; stage means do not isolate those subcomponents.",
                       "Stage scopes are disjoint but omit allocation/other capture work. Do not add them to inclusive capture time or nested mod scopes.",
                       "Session max is cumulative and can originate before the selected focused interval; counter snapshots can straddle an in-flight call.",
                       "Old logs without stage fields remain unknown; different routes are not a controlled A/B benchmark."]}


def scene_publication_holds(records):
    """Find source/scene holds even after the host labels old images stale."""
    previous, seen, active, episodes = {}, set(), {}, []
    healthy_scope_intervals = 0
    def delta(current, old, field):
        a, b = current.get(field), old.get(field)
        return a - b if number(a) and number(b) and a >= b else None
    def finish(key):
        episode = active.pop(key, None)
        if episode:
            episode["lastSceneGateSamples"] = dict(episode["lastSceneGateSamples"])
            episodes.append(episode)
    for record in records:
        game = record.get("game")
        if not isinstance(game, dict):
            continue
        identity = process_identity(record)
        key = identity, game.get("updatedTick")
        if key in seen:
            continue
        seen.add(key)
        prior = previous.get(identity)
        previous[identity] = record
        elapsed = elapsed_between(record, prior)
        if prior is None or elapsed is None or not fresh(record, "game") or not fresh(prior, "game"):
            finish(identity)
            continue
        before = prior["game"]
        eligible = all(g.get("gameFocused") is True and not any(g.get(name) for name in
            ("nativeMenuActive", "cinematicPanel", "nativeMovieActive", "nativeLoadingActive")) for g in (before, game))
        presents, scene, panels = (delta(game, before, name) for name in ("presents", "sceneFrames", "panelFrames"))
        host, old_host = record.get("host"), prior.get("host")
        matched_host = (fresh(record, "host") and fresh(prior, "host")
            and host.get("gamePid") == game.get("pid") and old_host.get("gamePid") == game.get("pid")
            and host.get("hostPid") == old_host.get("hostPid") and host.get("hmdMounted") is not False)
        pairs = delta(host, old_host, "newScenePairs") if matched_host else None
        camera = delta(game, before, "gpuCameraCalls")
        age = host.get("leftFrameAgeMs") if matched_host else None
        if (eligible and matched_host and game.get("nativeScopeActive") is True
                and host.get("renderMode") in ("scope", "scope_hold")
                and number(age) and 0 <= age <= 1000):
            # Native optics intentionally publish one mono image, not fresh
            # stereo pairs. Retire only healthy scope intervals: an old image
            # or a host which has gone stale must remain diagnosable below.
            healthy_scope_intervals += 1
            finish(identity)
            continue
        kind = None
        if eligible and presents == 0:
            kind = "no_native_presents"
        elif eligible and presents is not None and presents > 0 and scene == 0 and pairs == 0 and panels in (0, None):
            kind = "no_native_camera_packets" if camera == 0 else "scene_not_published"
        elif eligible and presents is not None and presents > 0 and scene is not None and scene > 0 and pairs == 0:
            kind = "host_pair_delivery_not_progressing"
        if kind is None:
            finish(identity)
            continue
        current = active.get(identity)
        host_pid = host.get("hostPid") if matched_host else None
        if current and (current["kind"] != kind or current["hostPid"] != host_pid):
            finish(identity)
            current = None
        if current is None:
            current = active[identity] = {"session": identity[0], "pid": identity[1], "creationFileTime": identity[2],
                "hostPid": host_pid, "kind": kind, "startUtc": prior.get("utc"), "endUtc": record.get("utc"),
                "durationMs": 0, "statusIntervals": 0, "nativePresentsDelta": 0, "sceneFramesDelta": 0,
                "nativeCameraCallsDelta": 0, "maxReportedLeftAgeMs": None, "lastSceneGateSamples": {}}
        current["endUtc"] = record.get("utc")
        current["durationMs"] += elapsed
        current["statusIntervals"] += 1
        current["nativePresentsDelta"] += presents or 0
        current["sceneFramesDelta"] += scene or 0
        current["nativeCameraCallsDelta"] += camera or 0
        gate = str(game.get("lastSceneGate", "unknown"))
        current["lastSceneGateSamples"][gate] = current["lastSceneGateSamples"].get(gate, 0) + 1
        if number(age) and age >= 0:
            current["maxReportedLeftAgeMs"] = max(age, current["maxReportedLeftAgeMs"] or 0)
    for key in list(active):
        finish(key)
    episodes.sort(key=lambda value: value["durationMs"], reverse=True)
    return {"episodeCount": len(episodes), "significantEpisodeCount": sum(e["durationMs"] >= 2000 for e in episodes),
            "healthyScopeIntervalsExcluded": healthy_scope_intervals,
            "longestEpisodes": episodes[:64], "limits": [
                "Host stale/panel modes are deliberately included; requiring scene/scene_hold would hide the freeze being diagnosed.",
                "No-camera packets means the native queued world-camera hook did not run while Presents continued; it does not prove a deadlock or a rejected VR pose.",
                "Native loading/briefing can create the player before drawing any world camera. A late menu flag alone does not prove live gameplay.",
                "Short host delivery gaps can be status-writer cadence. Episodes of at least two seconds are counted separately, not treated as per-frame proof.",
                "Verified active mono scope/scope_hold images at most 1000 ms old are counted separately; missing or older/stale scope images are not excused as stereo-pair waits.",
                "Native menu, cinematic panel, movie, unfocused, stale-game and mismatched-host intervals are excluded; missing counter fields remain unknown."]}


def on12_queue_pacing(records):
    """Own bounded pacing is not a native driver/rendering stall."""
    fields = ("issued", "completed", "waits", "polls", "waitUs", "budgetExhausted", "skipped", "failures")
    previous, last, windows = {}, {}, []
    invalid = 0
    for record in records:
        game = record.get("game") or {}
        current = game.get("on12QueuePacing")
        if not isinstance(current, dict) or not fresh(record, "game"):
            continue
        identity = process_identity(record)
        prior = previous.get(identity)
        previous[identity] = record
        last[identity] = current
        if prior is None or not gameplay_scene(record) or not gameplay_scene(prior):
            continue
        before = prior["game"].get("on12QueuePacing") or {}
        elapsed = elapsed_between(record, prior)
        presents = game.get("presents"), prior["game"].get("presents")
        if not all(number(value) and value >= 0 for value in presents):
            continue
        frames = presents[0] - presents[1]
        if (elapsed is None or frames <= 0 or not current.get("enabled") or not before.get("enabled")
                or not current.get("supported") or not before.get("supported") or current.get("resets") != before.get("resets")):
            continue
        delta = {}
        for field in fields:
            values = current.get(field), before.get(field)
            if not all(number(value) and value >= 0 and int(value) == value for value in values) or values[0] < values[1]:
                invalid += 1
                break
            delta[field] = values[0] - values[1]
        else:
            windows.append({"ownWaitMsPerNativePresent": delta["waitUs"] / frames / 1000,
                            "nativeFrames": frames, "elapsedMs": elapsed, **delta})
    return {"present": bool(last), "focusedGameplayIntervals": len(windows), "invalidCounterIntervals": invalid,
            "ownWaitMsPerNativePresent": distribution(window["ownWaitMsPerNativePresent"] for window in windows),
            "counterDeltas": {field: sum(window[field] for window in windows) for field in fields},
            "lastByProcess": [{"session": key[0], "pid": key[1], "creationFileTime": key[2], **value} for key, value in last.items()],
            "limits": ["This is deliberate bounded render-ahead pacing after native Present, not exclusive GPU work or a dropped frame.",
                       "The eight-millisecond polling budget can overshoot during OS scheduling or a driver call; maxWaitUs reports actual wall time.",
                       "Its wall time lies in the following pre-camera interval; do not attribute that whole interval to the native renderer."]}


def analyze(paths, crash_paths=()):
    sources, sessions, records, invalid = load_records(paths)
    def order(record):
        game = record.get("game") or {}
        tick = record.get("elapsedMs", game.get("updatedTick", 0) if isinstance(game, dict) else 0)
        return record["_session"], tick if number(tick) else 0
    records.sort(key=order)
    samples, selected, seen, previous, last = [], [], set(), {}, {}
    deltas = {field: 0 for field in COUNTERS}
    identity_rejected, duplicate, region_deltas = 0, 0, {}
    gate_windows, focused_gate_windows, gpu_last_values, valid_gpu_windows = [], [], [], []
    gpu_counter_deltas = {field: 0 for field in ("attempted", "validSamples", "skippedSamples", "disjointSamples", "failures", "resets")}
    for record in records:
        if not isinstance(record.get("game"), dict):
            if record.get("event") == "sample":
                previous = {key: value for key, value in previous.items() if key[0] != record["_session"]}
            continue
        game = record["game"]
        host = record.get("host")
        if isinstance(host, dict) and host.get("gamePid") != game.get("pid"):
            identity_rejected += 1
            record["host"] = None
        process = record.get("process") or {}
        creation = (process.get("game") or {}).get("creationFileTime") if isinstance(process, dict) else None
        stream = (record["_session"], game.get("pid"), creation)
        key = (*stream, game.get("updatedTick", (record["_file"], record["_line"])))
        if key in seen:
            duplicate += 1
            continue
        seen.add(key)
        prior = previous.get(stream)
        record["_rates"] = sample_rates(record, prior)
        samples.append(record)
        last[stream] = record
        elapsed = elapsed_between(record, prior)
        if elapsed is not None:
            gates = gate_window(record, prior, elapsed)
            if gates is not None:
                gate_windows.append(gates)
                if focused_scene(record) and focused_scene(prior):
                    focused_gate_windows.append(gates)
            current_gpu, prior_gpu = game.get("gpuRenderSpan"), prior["game"].get("gpuRenderSpan")
            if isinstance(current_gpu, dict) and isinstance(prior_gpu, dict):
                for field in gpu_counter_deltas:
                    value, before = current_gpu.get(field), prior_gpu.get(field)
                    if number(value) and number(before) and value >= before:
                        gpu_counter_deltas[field] += value - before
                value, before = current_gpu.get("validSamples"), prior_gpu.get("validSamples")
                if (focused_scene(record) and focused_scene(prior) and current_gpu.get("enabled") is True
                        and current_gpu.get("supported") is True and number(value) and number(before) and value > before
                        and number(current_gpu.get("windowSamples", 1)) and current_gpu.get("windowSamples", 1) > 0
                        and number(current_gpu.get("sampleAgeMs", 0)) and 0 <= current_gpu.get("sampleAgeMs", 0) <= 2000):
                    gpu_last_values.append(current_gpu.get("lastMs"))
        if focused_scene(record):
            selected.append(record)
            gpu = game.get("gpuRenderSpan")
            old_gpu = (prior.get("game") or {}).get("gpuRenderSpan") if prior else None
            if isinstance(gpu, dict):
                age = gpu.get("sampleAgeMs")
                # Legacy logs have no age: only a new completed sample proves freshness.
                fresh = (number(age) and 0 <= age <= 2000) if "sampleAgeMs" in gpu else (
                    elapsed is not None and isinstance(old_gpu, dict) and number(gpu.get("validSamples"))
                    and number(old_gpu.get("validSamples")) and gpu["validSamples"] > old_gpu["validSamples"])
                if (fresh and gpu.get("enabled") is True and gpu.get("supported") is True
                        and number(gpu.get("windowSamples", 1)) and gpu.get("windowSamples", 1) > 0
                        and number(gpu.get("validSamples")) and gpu["validSamples"] > 0):
                    valid_gpu_windows.append(gpu)
            if prior is not None and focused_scene(prior):
                for field in COUNTERS:
                    value, before = game.get(field), prior["game"].get(field)
                    if number(value) and number(before) and value >= before:
                        deltas[field] += value - before
                tick, before = game.get("updatedTick"), prior["game"].get("updatedTick")
                elapsed = (int(tick) - int(before)) & 0xffffffff if number(tick) and number(before) else 0
                old_regions = {region.get("name"): region for region in prior["game"].get("profileRegions", []) if isinstance(region, dict)}
                for region in game.get("profileRegions", []):
                    if (not isinstance(region, dict) or region.get("name") not in old_regions or not 0 < elapsed <= 3000
                            or game.get("cpuTimingsEnabled") is False or prior["game"].get("cpuTimingsEnabled") is False):
                        continue
                    old = old_regions[region["name"]]
                    count, old_count = region.get("samples"), old.get("samples")
                    if number(count) and number(old_count) and count <= old_count:
                        continue
                    total, old_total = region.get("estimatedTotalMs"), old.get("estimatedTotalMs")
                    if (not number(total) or not number(old_total) or total < old_total or region.get("kind") != old.get("kind")
                            or region.get("stride") != old.get("stride")):
                        continue
                    delta = total - old_total
                    destination = region_deltas.setdefault(region["name"], {"kind": region.get("kind"), "msPerSecond": [], "msPerNativePresent": [], "msPerPublishedSceneEye": []})
                    destination["msPerSecond"].append(delta * 1000 / elapsed)
                    for counter, field in (("presents", "msPerNativePresent"), ("sceneFrames", "msPerPublishedSceneEye")):
                        value, first = game.get(counter), prior["game"].get(counter)
                        if number(value) and number(first) and value > first:
                            destination[field].append(delta / (value - first))
        previous[stream] = record
    windows = {"game": {field: distribution(record["game"].get(field) for record in selected) for field in GAME_FIELDS},
               "host": {field: distribution(record["host"].get(field) for record in selected) for field in HOST_FIELDS},
               "rates": {field: distribution(record["_rates"].get(field) for record in selected) for field in RATE_FIELDS}}
    for scope in ("game", "host"):
        windows["process" + scope.title()] = {field: distribution(record["process"][scope].get(field)
                for record in selected if isinstance(record.get("process"), dict) and isinstance(record["process"].get(scope), dict))
                for field in ("cpuPercent", "cpuCorePercent", "workingSetBytes", "privateBytes")}
    windows["gpuRenderSpan"] = {field: distribution(window.get(field) for window in valid_gpu_windows)
                                for field in ("medianMs", "p95Ms", "maxMs", "pending")}
    windows["gpuRenderSpan"]["lastMsOnNewResult"] = distribution(gpu_last_values)
    regions = {}
    for record in selected:
        for region in record["game"].get("profileRegions", []):
            if not isinstance(region, dict) or not isinstance(region.get("name"), str):
                continue
            value = region.get("estimatedMsPerSceneSourceFrame", region.get("estimatedMsPerSourceFrame"))
            regions.setdefault(region["name"], []).append(value)
    configurations = sorted({(record["game"].get("renderWidth", record["game"].get("sourceWidth", 0)),
                              record["game"].get("renderHeight", record["game"].get("sourceHeight", 0)),
                              record["game"].get("renderMultisampleType", 0)) for record in samples})
    snapshots = [{"session": stream[0], "pid": stream[1], "updatedTick": record["game"].get("updatedTick"),
                  "gameFocused": record["game"].get("gameFocused"),
                  "counters": {field: record["game"].get(field) for field in COUNTERS},
                  "gpuRenderSpan": record["game"].get("gpuRenderSpan"),
                  "lastSceneGate": record["game"].get("lastSceneGate"), "sceneGateAttempts": record["game"].get("sceneGateAttempts")}
                 for stream, record in last.items()]
    gate_diagnostics = {"allFreshGameIntervals": summarize_gates(gate_windows), "focusedSceneIntervals": summarize_gates(focused_gate_windows),
                        "unit": "SceneDecision attempts; several checks can occur per source frame. These are not dropped-frame rates."}
    crash_artifacts = []
    for path in crash_paths:
        path = pathlib.Path(path)
        content = path.read_bytes()
        try:
            evidence = json.loads(content.decode("utf-8-sig"))
        except ValueError:
            evidence = {"text": content.decode("utf-8-sig", errors="replace")[-16000:]}
        crash_artifacts.append({"file": str(path), "sha256": hashlib.sha256(content).hexdigest(), "evidence": evidence})
    wait_trace, wait_events = native_wait_trace(records)
    flush_trace, flush_events = native_buffer_flush_trace(records)
    native_trace = native_frame_trace(records, wait_events, flush_events)
    configuration_summaries = source_configuration_summaries(records)
    delivery = host_scene_delivery(records)
    queue_pacing = on12_queue_pacing(records)
    summary = []
    for snapshot in flush_trace["lastCumulativeByProcess"]:
        phases = [f"{phase['phase']}: calls={phase['calls']}, total={phase['totalUs'] / 1000:.2f} ms, max={phase['maxUs'] / 1000:.2f} ms"
                  for phase in snapshot["phases"] if all(number(phase[field]) for field in ("calls", "totalUs", "maxUs"))]
        summary.append(f"PID {snapshot['pid']}: native buffer trace cumulative; " + "; ".join(phases) +
                       f"; backend={snapshot['backend']}, batchingSupported={snapshot['batchingSupported']}, batchedCalls={snapshot['batchedCalls']}, savedLocks={snapshot['savedLocks']}, singleMapCalls={snapshot['singleMapCalls']}, "
                       f"lockFailures={snapshot['lockFailures']}, unlockFailures={snapshot['unlockFailures']}, invalidMappings={snapshot['invalidMappings']}, batchingFaulted={snapshot['batchingFaulted']}. "
                       "These totals include work outside retained gameplay frames; saved locks are not a measured FPS improvement.")
    if queue_pacing["ownWaitMsPerNativePresent"]:
        own = queue_pacing["ownWaitMsPerNativePresent"]
        summary.append(f"On12 queue pacing: median {own['median']:.2f} ms per native Present across {own['count']} windows. This measures explicit queue waiting.")
    if not selected:
        summary.append("No matching focused gameplay samples. Gameplay frame rate and the cause of stalls are unavailable.")
    else:
        for field, label in (("sourceHz", "Native Present"), ("hostScenePairHz", "New complete stereo pairs"), ("hostSubmitHz", "Compositor submissions")):
            stat = windows["rates"][field]
            if stat:
                summary.append(f"{label}: median {stat['median']:.2f} Hz across {stat['count']} intervals.")
    for width, height, msaa in configurations:
        if width and height:
            summary.append(f"Native frame {width} x {height}, D3D9 MultiSampleType={msaa}.")
    for snapshot in snapshots:
        counts = snapshot["counters"]
        busy, copies = counts.get("gpuBusy"), counts.get("gpuCopies")
        if number(busy) and number(copies) and busy + copies > 0:
            summary.append(f"PID {snapshot['pid']}: GPU ring busy on {100 * busy / (busy + copies):.1f}% of attempts. Cumulative counts include menu frames.")
        stalls, capture, present = (counts.get(name) for name in ("frameStalls50Ms", "captureStalls50Ms", "presentStalls50Ms"))
        if all(number(value) for value in (stalls, capture, present)) and stalls > max(3, capture + present):
            summary.append(f"PID {snapshot['pid']}: stalls of 50 ms or longer. Between Presents: {stalls}. Inside capture: {capture}. Inside native Present: {present}.")
    gpu_span = windows["gpuRenderSpan"]["lastMsOnNewResult"]
    if gpu_span:
        summary.append(f"GPU queue span from first shader to capture: median {gpu_span['median']:.2f} ms across {gpu_span['count']} samples, including queue idle time.")
    gates = gate_diagnostics["allFreshGameIntervals"]
    if gates["totalAttemptsDelta"]:
        reasons = [reason for reason in gates["reasons"] if reason["name"] != "ready" and reason["attemptsDelta"] > 0][:3]
        detail = ", ".join(f"{reason['name']}={reason['attemptsDelta']}" for reason in reasons)
        summary.append(f"Scene gates rejected {gates['rejectedAttemptsDelta']}/{gates['totalAttemptsDelta']} checks ({gates['rejectionPercent']:.1f}%)" + (f"; {detail}." if detail else ".") + " The count includes menu checks.")
    top_regions = sorted(((name, distribution(region["msPerSecond"])) for name, region in region_deltas.items()
                          if region["kind"] in ("sampled_mod_wall_ms_nested", "sampled_mod_self_wall_ms") and region["msPerSecond"]),
                         key=lambda item: item[1]["median"], reverse=True)[:3]
    if top_regions:
        summary.append("Largest sampled wall-time scopes: " + ", ".join(f"{name}={value['median']:.2f} ms/s" for name, value in top_regions) + ". Nested scopes may overlap.")
    if crash_artifacts:
        summary.append("Crash artifacts are stored separately. The exception and last recorded event indicate where the crash was observed.")
    traced = native_trace["focusedGameplayStatusBracketsAllKinds"]
    if traced["intervalMs"]:
        interval = traced["intervalMs"]
        summary.append(f"Native per-frame trace: {traced['intervalCount']} intervals in focused gameplay; "
                       f"p95={interval['p95']:.2f} ms, p99={interval['p99']:.2f} ms, max={interval['max']:.2f} ms; "
                       f"stalls of at least 50 ms: {traced['stalls']['atLeast50Ms']}.")
    return {"formatVersion": 1, "sources": sources, "sessions": sessions, "recordCount": len(records), "sampleCount": len(samples),
            "invalidLines": invalid, "duplicateSamples": duplicate, "hostIdentityRejected": identity_rejected,
            "uniqueFocusedSceneSamples": len(selected), "windows": windows, "focusedConsecutiveCounterDeltas": deltas,
            "sampledInclusiveRegionsMsPerSourceFrame": {name: distribution(values) for name, values in regions.items()},
            "profileRegionDeltas": {name: {field: value if field == "kind" else distribution(value) for field, value in region.items()} for name, region in region_deltas.items()},
            "gpuRenderSpanDiagnostics": {"counterDeltas": gpu_counter_deltas, "sampledQueueSpan": True, "exclusiveGPUUtilization": False},
            "sceneGateDiagnostics": gate_diagnostics,
            "nativeFrameTrace": native_trace,
            "nativeWaitTrace": wait_trace,
            "nativeBufferFlushTrace": flush_trace,
            "sourceConfigurationSummaries": configuration_summaries,
            "cpuCaptureStages": cpu_capture_stage_summaries(records),
            "scenePublicationHolds": scene_publication_holds(records),
            "hostSceneDelivery": delivery,
            "on12QueuePacing": queue_pacing,
            "nativeConfigurations": [{"width": width, "height": height, "multisampleType": msaa} for width, height, msaa in configurations],
            "lastCounterSnapshots": snapshots, "crashArtifacts": crash_artifacts, "humanSummary": summary,
            "limits": ["Diagnostic windows overlap and mix nearby frames; this is not a matched A/B benchmark.",
                       "Native presents, scene eye frames, estimated sceneFrames/2 and actual complete host pairs are different rates.",
                       "Single snapshots provide cumulative counts, not FPS or a gameplay timing history.",
                       "Sampled mod wall regions can nest and overlap; inclusive native-call scopes are separate. Neither can be added as exclusive CPU frame costs.",
                       "Compositor SubmitFrame wall time includes runtime pacing; it is not CPU work or game GPU rendering time.",
                       "GPU timestamps measure a sampled first-shader to pre-capture queue span, including queue gaps, not exclusive GPU utilization.",
                       "Scene gate counters count admission checks, not unique source frames or dropped frames.",
                       "Missing/reset rates stay null; one-second sampling can miss brief stalls."]}


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--crash", action="append", type=pathlib.Path, default=[])
    args = parser.parse_args()
    result = analyze(args.logs, args.crash)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"samples": result["sampleCount"], "focusedSceneSamples": result["uniqueFocusedSceneSamples"],
                      "humanSummary": result["humanSummary"]}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
