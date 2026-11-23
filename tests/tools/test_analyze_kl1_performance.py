import json
import pathlib
import tempfile
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))
from analyze_kl1_performance import analyze, distribution, sample_rates


def sample(tick, presents, scene, pairs, **changes):
    value = {"event": "sample", "elapsedMs": tick, "game": {"pid": 12, "updatedTick": tick, "gameFocused": True,
             "presents": presents, "sceneFrames": scene, "gpuCopies": presents, "gpuBusy": 0},
             "host": {"gamePid": 12, "hostPid": 34, "renderMode": "scene", "phase": "submitting", "framesSubmitted": presents, "newScenePairs": pairs}}
    value.update(changes)
    return value


TRACE_COLUMNS = ["seq", "tick", "thread", "present", "startUs", "intervalUs", "cpuUs", "cpuKnown", "captureUs",
                 "presentUs", "previousCaptureUs", "previousPresentUs", "captured", "drops", "failures", "sourceFrameId",
                 "eye", "kind", "gate", "captureResult", "presentResult"]


def traced_event(sequence, tick, **changes):
    event = dict(seq=sequence, tick=tick, thread=123, present=sequence, startUs=tick * 1000, intervalUs=16000,
                 cpuUs=8000, cpuKnown=True, captureUs=1000, presentUs=100, previousCaptureUs=1000,
                 previousPresentUs=100, captured=sequence, drops=0, failures=0, sourceFrameId=sequence + 1,
                 eye=(sequence + 1) & 1, kind=1, gate=0, captureResult=0, presentResult=0)
    event.update(changes)
    return [event[column] for column in TRACE_COLUMNS]


def with_trace(record, events, **changes):
    record["game"]["nativeFrameTrace"] = {"columns": TRACE_COLUMNS, "events": events, "overwritten": 0, **changes}
    return record


SHADER_COLUMNS = ["firstShaderUs", "shaderThread", "shaderSpanUs", "shaderCpuUs", "shaderCpuKnown", "preShaderUs", "preShaderKnown", "shaderKnown"]
WAIT_COLUMNS = ["seq", "tick", "thread", "callerRva", "api", "site", "startUs", "endUs", "wallUs", "timeout", "objectCount", "waitAll", "result", "lastError"]
CAMERA_COLUMNS = ["firstCameraUs", "cameraThread", "cameraKnown"]
BUFFER_FLUSH_COLUMNS = ["seq", "tick", "thread", "phase", "resource", "startUs", "endUs", "wallUs", "pending", "valid",
                        "shadowReady", "capacity", "ranges", "submittedRanges", "bytes", "spanBytes", "result"]


def flush_event(sequence, start, end, **changes):
    event = dict(seq=sequence, tick=end // 1000, thread=123, phase=0, resource=1, startUs=start, endUs=end,
                 wallUs=end - start, pending=True, valid=True, shadowReady=True, capacity=4096,
                 ranges=4, submittedRanges=1, bytes=1024, spanBytes=1024, result=0)
    event.update(changes)
    return [event[column] for column in BUFFER_FLUSH_COLUMNS]


def with_flushes(record, events, **changes):
    record["game"]["nativeBufferFlushTrace"] = dict(columns=BUFFER_FLUSH_COLUMNS, events=events, overwritten=0,
        installed=True, enabled=True, verified=True, batching=True, on12=True, batchedCalls=4, savedLocks=12,
        droppedEvents=0, phases=[dict(calls=8, active=4, totalUs=100000, maxUs=90000, bytes=4096, ranges=16),
                                 dict(calls=4, active=4, totalUs=1000, maxUs=400, bytes=0, ranges=0)], inProgress=None)
    record["game"]["nativeBufferFlushTrace"].update(changes)
    return record


def wait_event(sequence, start, end, **changes):
    event = dict(seq=sequence, tick=end // 1000, thread=123, callerRva=0xB7320, api=1, site=0,
                 startUs=start, endUs=end, wallUs=end - start, timeout=0xffffffff, objectCount=1, waitAll=0, result=0, lastError=7)
    event.update(changes)
    return [event[column] for column in WAIT_COLUMNS]


def with_waits(record, events, **changes):
    record["game"]["nativeWaitTrace"] = {"columns": WAIT_COLUMNS, "events": events, "overwritten": 0,
        "installed": True, "enabled": True, "verified": True, "droppedEvents": 0, "sites": {}, **changes}
    return record


class AnalyzerTests(unittest.TestCase):
    def run_analysis(self, records, suffix=".jsonl"):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / ("performance" + suffix)
            path.write_text("\n".join(json.dumps(record) for record in records) + "\n{torn", encoding="utf-8")
            return analyze([path])

    def test_rates_and_pair_distinction(self):
        result = self.run_analysis([sample(1000, 10, 10, 4), sample(2000, 40, 30, 13)])
        self.assertEqual(result["windows"]["rates"]["sourceHz"]["median"], 30)
        self.assertEqual(result["windows"]["rates"]["scenePairHz"]["median"], 10)
        self.assertEqual(result["windows"]["rates"]["hostScenePairHz"]["median"], 9)
        self.assertEqual(result["invalidLines"], 1)

    def test_host_rates_use_legacy_report_age_and_preserve_native_rates(self):
        first = sample(1000, 100, 80, 40, freshness={"hostAgeMs": 1000})
        second = sample(2000, 220, 160, 76, freshness={"hostAgeMs": 0},
                        rates={"sourceHz": 80, "hostSubmitHz": 120, "hostScenePairHz": 36})
        rates = sample_rates(second, first)
        self.assertEqual((rates["sourceHz"], rates["hostSubmitHz"], rates["hostScenePairHz"]), (80, 60, 18))

        second["rates"]["hostWindowMs"] = 2000
        second["rates"]["hostSubmitHz"] = 59.95
        self.assertEqual(sample_rates(second, first)["hostSubmitHz"], 59.95)

    def test_reread_of_legacy_host_report_has_no_rate(self):
        first = sample(2000, 220, 160, 76, freshness={"hostAgeMs": 0})
        second = sample(3000, 220, 160, 76, freshness={"hostAgeMs": 990},
                        rates={"sourceHz": 80, "hostSubmitHz": 0, "hostScenePairHz": 0})
        rates = sample_rates(second, first)
        self.assertEqual((rates["hostSubmitHz"], rates["hostScenePairHz"]), (None, None))

    def test_on12_own_pacing_is_separate_from_native_stalls(self):
        first, second = sample(1000, 10, 10, 4), sample(2000, 20, 20, 9)
        counters = dict(issued=10, completed=9, waits=9, polls=9, waitUs=1000, budgetExhausted=0, skipped=0, failures=0,
                        enabled=True, supported=True, resets=1, pending=1, maxWaitUs=1000)
        first["game"]["on12QueuePacing"] = counters
        second["game"]["on12QueuePacing"] = dict(counters, issued=20, completed=19, waits=19, polls=25, waitUs=9000)
        result = self.run_analysis([first, second])["on12QueuePacing"]
        self.assertEqual(result["focusedGameplayIntervals"], 1)
        self.assertEqual(result["ownWaitMsPerNativePresent"]["median"], .8)
        self.assertEqual(result["counterDeltas"]["waitUs"], 8000)

    def test_on12_pacing_reset_and_ui_are_not_gameplay_costs(self):
        first, second = sample(1000, 10, 10, 4), sample(2000, 20, 20, 9)
        counters = dict(issued=10, completed=9, waits=9, polls=9, waitUs=1000, budgetExhausted=0, skipped=0, failures=0,
                        enabled=True, supported=True, resets=1)
        first["game"]["on12QueuePacing"] = counters
        second["game"]["on12QueuePacing"] = dict(counters, resets=2)
        self.assertEqual(self.run_analysis([first, second])["on12QueuePacing"]["focusedGameplayIntervals"], 0)
        second["game"]["on12QueuePacing"] = dict(counters, waitUs=9000)
        second["game"]["nativeMenuActive"] = True
        self.assertEqual(self.run_analysis([first, second])["on12QueuePacing"]["focusedGameplayIntervals"], 0)

    def test_missing_and_reset_rates_remain_null(self):
        result = self.run_analysis([sample(1000, 100, 80, 40), sample(2000, 1, 0, 0)])
        self.assertIsNone(result["windows"]["rates"]["sourceHz"])
        self.assertEqual(distribution([None, False, float("nan"), float("inf")]), None)

    def test_identity_stale_focus_and_duplicates(self):
        original = sample(1000, 10, 10, 4)
        wrong = sample(2000, 20, 20, 8); wrong["host"]["gamePid"] = 99
        stale = sample(3000, 30, 30, 12, freshness={"gameValid": False, "hostValid": True})
        unfocused = sample(4000, 40, 40, 16); unfocused["game"]["gameFocused"] = False
        result = self.run_analysis([original, original, wrong, stale, unfocused])
        self.assertEqual(result["uniqueFocusedSceneSamples"], 1)
        self.assertEqual(result["hostIdentityRejected"], 1)
        self.assertEqual(result["duplicateSamples"], 1)

    def test_supplied_nullable_rates_and_single_snapshot(self):
        result = self.run_analysis([sample(1000, 10, 10, 4, rates={"sourceHz": None}),
                                    sample(2000, 40, 30, 13, rates={"sourceHz": 29})])
        self.assertEqual(result["windows"]["rates"]["sourceHz"]["median"], 29)
        result = self.run_analysis([sample(1000, 10, 10, 4)["game"]])
        self.assertEqual(result["uniqueFocusedSceneSamples"], 0)
        self.assertIsNone(result["windows"]["rates"]["sourceHz"])
        self.assertTrue(result["humanSummary"])

    def test_cumulative_region_delta(self):
        first, second = sample(1000, 10, 10, 4), sample(2000, 40, 30, 13)
        first["game"]["profileRegions"] = [{"name": "ownership", "estimatedTotalMs": 100, "kind": "sampled_mod_self_wall_ms"}]
        second["game"]["profileRegions"] = [{"name": "ownership", "estimatedTotalMs": 160, "kind": "sampled_mod_self_wall_ms"}]
        result = self.run_analysis([first, second])
        region = result["profileRegionDeltas"]["ownership"]
        self.assertEqual(region["msPerSecond"]["median"], 60)
        self.assertEqual(region["msPerNativePresent"]["median"], 2)
        self.assertEqual(region["msPerPublishedSceneEye"]["median"], 3)

    def test_gpu_queue_span_new_results_and_gate_rejection_deltas(self):
        records = [sample(1000, 100, 80, 40), sample(2000, 130, 100, 49), sample(3000, 160, 120, 58)]
        for index, record in enumerate(records):
            record["game"]["gpuRenderSpan"] = {"enabled": True, "supported": True, "validSamples": min(index + 1, 2),
                "lastMs": 40 if index == 0 else 30, "medianMs": 40 if index == 0 else 35, "p95Ms": 40, "windowSamples": min(index + 1, 2),
                "maxMs": 40, "pending": 1, "attempted": 4 + index, "skippedSamples": 0, "disjointSamples": 0, "failures": 0, "resets": 0}
            record["game"]["sceneGateAttempts"] = {"ready": 100 if index == 0 else 180,
                "tracking_invalid": 20 if index == 0 else 25, "native_panel_or_menu": 30 if index == 0 else 35}
        result = self.run_analysis(records)
        self.assertEqual(result["windows"]["gpuRenderSpan"]["lastMsOnNewResult"]["count"], 1)
        self.assertEqual(result["windows"]["gpuRenderSpan"]["lastMsOnNewResult"]["median"], 30)
        self.assertEqual(result["gpuRenderSpanDiagnostics"]["counterDeltas"]["validSamples"], 1)
        self.assertFalse(result["gpuRenderSpanDiagnostics"]["exclusiveGPUUtilization"])
        gates = result["sceneGateDiagnostics"]["allFreshGameIntervals"]
        self.assertEqual(gates["totalAttemptsDelta"], 90)
        self.assertEqual(gates["rejectedAttemptsDelta"], 10)
        self.assertAlmostEqual(gates["rejectionPercent"], 100 / 9)

    def test_pending_gpu_and_reset_gates_are_not_zero_measurements(self):
        first, second = sample(1000, 100, 80, 40), sample(2000, 130, 100, 49)
        for record in (first, second):
            record["game"]["gpuRenderSpan"] = {"enabled": True, "supported": False, "validSamples": 0,
                "lastMs": 0, "medianMs": 0, "p95Ms": 0, "maxMs": 0, "pending": 4}
        first["game"]["sceneGateAttempts"] = {"ready": 100, "tracking_invalid": 20}
        second["game"]["sceneGateAttempts"] = {"ready": 1, "tracking_invalid": 0}
        result = self.run_analysis([first, second])
        self.assertIsNone(result["windows"]["gpuRenderSpan"]["medianMs"])
        self.assertIsNone(result["sceneGateDiagnostics"]["allFreshGameIntervals"]["rejectionPercent"])
        for record in (first, second):
            record["game"]["gpuRenderSpan"].update(supported=True, validSamples=50, windowSamples=0)
        result = self.run_analysis([first, second])
        self.assertIsNone(result["windows"]["gpuRenderSpan"]["medianMs"])
        self.assertIsNone(result["windows"]["gpuRenderSpan"]["lastMsOnNewResult"])

    def test_gpu_measurements_stop_being_fresh_when_queries_stop(self):
        first, second = sample(1000, 100, 80, 40), sample(2000, 130, 100, 49)
        for record in (first, second):
            record["game"]["gpuRenderSpan"] = {"enabled": True, "supported": True, "validSamples": 127,
                "windowSamples": 127, "medianMs": 0.5, "lastMs": 1, "sampleAgeMs": 2500}
        self.assertIsNone(self.run_analysis([first, second])["windows"]["gpuRenderSpan"]["medianMs"])
        second["game"]["gpuRenderSpan"]["sampleAgeMs"] = 1000
        self.assertEqual(self.run_analysis([first, second])["windows"]["gpuRenderSpan"]["medianMs"]["count"], 1)
        for record in (first, second):
            del record["game"]["gpuRenderSpan"]["sampleAgeMs"]
        self.assertIsNone(self.run_analysis([first, second])["windows"]["gpuRenderSpan"]["medianMs"])

    def test_missing_sample_and_process_lifetime_break_delta_chain(self):
        records = [sample(1000, 10, 10, 4), {"event": "sample", "elapsedMs": 1500, "game": None, "host": None}, sample(2000, 40, 30, 13)]
        result = self.run_analysis(records)
        self.assertIsNone(result["windows"]["rates"]["sourceHz"])
        first, second = sample(1000, 10, 10, 4), sample(2000, 40, 30, 13)
        first["process"] = {"game": {"creationFileTime": 100, "cpuPercent": 10, "cpuCorePercent": 80}}
        second["process"] = {"game": {"creationFileTime": 200, "cpuPercent": 12, "cpuCorePercent": 96}}
        result = self.run_analysis([first, second])
        self.assertIsNone(result["windows"]["rates"]["sourceHz"])
        self.assertEqual(result["windows"]["processGame"]["cpuCorePercent"]["median"], 88)

    def test_current_profile_names_and_inclusive_kind_remain_separate(self):
        first, second = sample(1000, 10, 10, 4), sample(2000, 40, 30, 13)
        for record, multiplier in ((first, 1), (second, 2)):
            record["game"]["profileRegions"] = [
                {"name": "weaponOwnershipTraversal", "estimatedTotalMs": 50 * multiplier, "samples": 5 * multiplier, "stride": 61, "kind": "sampled_mod_wall_ms_nested"},
                {"name": "hudShadowUnlock", "estimatedTotalMs": 20 * multiplier, "samples": 5 * multiplier, "stride": 61, "kind": "sampled_mod_wall_ms_nested"},
                {"name": "hudRestore", "estimatedTotalMs": 10 * multiplier, "samples": 5 * multiplier, "stride": 61, "kind": "sampled_mod_wall_ms_nested"},
                {"name": "postprocessDrawInclusive", "estimatedTotalMs": 1000 * multiplier, "samples": 5 * multiplier, "stride": 61, "kind": "inclusive_wall_ms"}]
        result = self.run_analysis([first, second])
        self.assertEqual(result["profileRegionDeltas"]["postprocessDrawInclusive"]["kind"], "inclusive_wall_ms")
        top = next(line for line in result["humanSummary"] if "Largest sampled wall-time scopes" in line)
        self.assertIn("weaponOwnershipTraversal", top)
        self.assertIn("hudRestore", top)
        self.assertNotIn("postprocessDrawInclusive", top)

    def test_unsampled_or_disabled_cpu_regions_are_not_zero_cost(self):
        first, second = sample(1000, 10, 10, 4), sample(2000, 40, 30, 13)
        for record in (first, second):
            record["game"]["profileRegions"] = [{"name": "hudRestore", "estimatedTotalMs": 0, "samples": 0,
                "stride": 61, "kind": "sampled_mod_wall_ms_nested"}]
        self.assertFalse(self.run_analysis([first, second])["profileRegionDeltas"])
        first["game"]["cpuTimingsEnabled"] = False
        second["game"]["profileRegions"][0].update(samples=1, estimatedTotalMs=5)
        self.assertFalse(self.run_analysis([first, second])["profileRegionDeltas"])

    def test_native_trace_previous_frame_attribution_and_true_percentiles(self):
        first = sample(1000, 10, 10, 4)
        second = with_trace(sample(2000, 40, 30, 13), [
            traced_event(1, 1200, intervalUs=100000, cpuUs=10000, captureUs=80000),
            traced_event(2, 1500, intervalUs=90000, previousCaptureUs=80000, previousPresentUs=2000),
            traced_event(3, 1800, intervalUs=80000, cpuKnown=False, previousPresentUs=70000)])
        trace = self.run_analysis([first, second])["nativeFrameTrace"]
        measured = trace["focusedGameplayStatusBrackets"]
        self.assertEqual(measured["intervalMs"]["p95"], 100)
        self.assertEqual(measured["intervalMs"]["p99"], 100)
        self.assertEqual(measured["intervalCount"], 3)
        self.assertEqual(measured["cpuKnownIntervals"], 2)
        self.assertEqual(measured["stalls"]["dominantWallRegionCounts"],
                         {"outside_capture_present": 1, "previous_capture": 1, "previous_native_present": 1, "invalid_budget": 0})
        worst = measured["worstStalls"][0]
        self.assertAlmostEqual(worst["outsideCapturePresentMs"], 98.9)
        self.assertAlmostEqual(worst["threadCpuFraction"], .1)
        self.assertEqual(worst["previousCaptureMs"], 1)
        self.assertEqual(worst["dominantWallRegion"], "outside_capture_present")

    def test_native_trace_stale_scene_capture_keeps_its_real_native_stall(self):
        first = sample(1000, 10, 10, 4)
        second = with_trace(sample(2000, 40, 30, 13), [
            traced_event(1, 1300, intervalUs=390000, cpuUs=218750, kind=2, gate=12, sourceFrameId=0, captureUs=15000),
            traced_event(2, 1800, intervalUs=250000, cpuUs=156250, kind=1, previousCaptureUs=15000)])
        result = self.run_analysis([first, second])["nativeFrameTrace"]
        self.assertEqual(result["focusedGameplayStatusBrackets"]["eventCount"], 1)
        all_native = result["focusedGameplayStatusBracketsAllKinds"]
        self.assertEqual(all_native["eventCount"], 2)
        self.assertEqual(all_native["intervalMs"]["max"], 390)
        self.assertEqual(all_native["stalls"]["atLeast250Ms"], 2)
        self.assertEqual(all_native["worstStalls"][0]["kind"], 2)

    def test_native_trace_unpublished_kind_zero_keeps_source_pause_evidence(self):
        first = sample(1000, 10, 10, 4)
        second = with_trace(sample(2000, 40, 30, 13), [traced_event(1, 1800, intervalUs=400000,
            kind=0, sourceFrameId=0, eye=0, gate=0, drops=1)])
        second["host"]["renderMode"] = "scene_hold"
        trace = self.run_analysis([first, second])["nativeFrameTrace"]
        self.assertEqual(trace["focusedGameplayStatusBrackets"]["eventCount"], 0)
        all_native = trace["focusedGameplayStatusBracketsAllKinds"]
        self.assertEqual(all_native["eventCount"], 1)
        self.assertEqual(all_native["intervalMs"]["max"], 400)
        self.assertEqual(all_native["worstStalls"][0]["kind"], 0)

    def test_native_trace_focus_bracketing_and_gameplay_are_conservative(self):
        first = sample(1000, 10, 10, 4); first["game"]["gameFocused"] = False
        second = with_trace(sample(2000, 40, 30, 13), [traced_event(1, 1500)])
        third = with_trace(sample(3000, 60, 50, 22), [traced_event(2, 1900), traced_event(3, 2500)])
        third["game"]["cinematicActive"] = True
        result = self.run_analysis([first, second, third])["nativeFrameTrace"]
        self.assertEqual(result["uniqueEvents"], 3)
        self.assertEqual(result["focusedSceneStatusBrackets"]["eventCount"], 1)
        self.assertEqual(result["focusedGameplayStatusBrackets"]["eventCount"], 0)
        missing = {"event": "sample", "elapsedMs": 2500, "game": None, "host": None}
        result = self.run_analysis([first, second, missing, third])["nativeFrameTrace"]
        self.assertEqual(result["focusedSceneStatusBrackets"]["eventCount"], 0)

    def test_native_trace_duplicates_gaps_retries_and_malformed_rows(self):
        first = sample(1000, 10, 10, 4)
        rows = [traced_event(1, 1100, sourceFrameId=2, eye=0), traced_event(2, 1300, sourceFrameId=2, eye=0),
                traced_event(4, 1500, sourceFrameId=20, eye=0), traced_event(5, 1700, sourceFrameId=23, eye=1)]
        second = with_trace(sample(2000, 40, 30, 13), rows + [[1], traced_event(6, 1800, intervalUs=-1),
                            traced_event(7, 1900, sourceFrameId="invalid")], overwritten=2)
        result = self.run_analysis([first, second, second])["nativeFrameTrace"]
        self.assertEqual(result["uniqueEvents"], 4)
        self.assertEqual(result["duplicateEvents"], 4)
        self.assertEqual(result["reportedRingOverwrites"], 2)
        self.assertEqual(result["malformedEventsOrBatches"], 6)
        measured = result["focusedGameplayStatusBrackets"]
        self.assertEqual(measured["sequenceGapsWithinSelection"], 1)
        self.assertEqual(measured["consecutiveSceneIdentity"],
                         {"repeatedFrameIds": 1, "repeatedEyes": 1, "skippedSourceFrameIds": 2})

    def test_native_trace_invalid_budgets_stale_status_and_pid_reuse(self):
        first = with_trace(sample(1000, 10, 10, 4), [traced_event(1, 900)])
        first["process"] = {"game": {"creationFileTime": 100}}
        second = with_trace(sample(2000, 40, 30, 13), [traced_event(1, 1800, intervalUs=50000, previousCaptureUs=60000)])
        second["process"] = {"game": {"creationFileTime": 200}}
        third = with_trace(sample(3000, 70, 60, 23), [traced_event(2, 2500)], overwritten=5)
        third["freshness"] = {"gameValid": False, "hostValid": True}
        result = self.run_analysis([first, second, third])["nativeFrameTrace"]
        self.assertEqual(result["uniqueEvents"], 2)
        self.assertEqual(result["duplicateEvents"], 0)
        self.assertEqual(result["staleStatusEventsRejected"], 1)
        self.assertEqual(result["reportedRingOverwrites"], 0)
        self.assertEqual(result["allRetainedNativeFrames"]["invalidAttributionBudgets"], 1)
        self.assertIsNone(result["allRetainedNativeFrames"]["worstStalls"][0]["outsideCapturePresentMs"])
        self.assertEqual(result["focusedSceneStatusBrackets"]["eventCount"], 0)

    def test_native_trace_rotation_deduplicates_process_sequences(self):
        header = {"event": "session", "utc": "2026-11-06T18:00:00Z", "gamePid": 12, "processCreationFileTime": 100}
        record = with_trace(sample(1000, 10, 10, 4), [traced_event(20, 900)])
        with tempfile.TemporaryDirectory() as folder:
            paths = [pathlib.Path(folder) / name for name in ("performance.jsonl", "performance.jsonl.1")]
            for path in paths:
                path.write_text("\n".join(json.dumps(item) for item in (header, record)) + "\n", encoding="utf-8")
            result = analyze(paths)["nativeFrameTrace"]
        self.assertEqual(result["uniqueEvents"], 1)
        self.assertEqual(result["duplicateEvents"], 1)
        self.assertFalse(self.run_analysis([sample(1000, 1, 1, 0)])["nativeFrameTrace"]["present"])

    def test_shader_phase_and_wait_overlap_never_add_asynchronous_dependencies(self):
        first, second = sample(1000, 10, 10, 4), sample(2000, 40, 30, 13)
        event = dict(zip(TRACE_COLUMNS, traced_event(1, 1800, intervalUs=500000, previousCaptureUs=10000, previousPresentUs=1000)))
        event.update(firstShaderUs=1400000, shaderThread=123, shaderSpanUs=400000, shaderCpuUs=10000,
                     shaderCpuKnown=1, preShaderUs=89000, preShaderKnown=1, shaderKnown=1)
        second["game"]["nativeFrameTrace"] = {"columns": TRACE_COLUMNS + SHADER_COLUMNS,
            "events": [[event[column] for column in TRACE_COLUMNS + SHADER_COLUMNS]]}
        with_waits(second, [wait_event(1, 1320000, 1420000), wait_event(2, 1410000, 1450000, site=2, callerRva=0xB47B4),
                            wait_event(3, 1000000, 1900000, thread=456, api=2, site=1, objectCount=2, callerRva=0xB52D9)])
        result = self.run_analysis([first, second])
        frames = result["nativeFrameTrace"]["focusedGameplayStatusBracketsAllKinds"]
        self.assertEqual(frames["firstShaderPhase"]["firstShaderToPresentMs"]["median"], 400)
        self.assertEqual(frames["firstShaderPhase"]["previousPresentEndToFirstShaderMs"]["median"], 89)
        worst = frames["worstStalls"][0]
        self.assertEqual(worst["firstShaderPhase"]["shaderThreadCpuMs"], 10)
        overlap = worst["nativeWaitOverlap"]
        self.assertEqual(overlap["sameThreadUnionMs"], 130)
        self.assertEqual(overlap["sameThreadUnionBeforeFirstShaderMs"], 80)
        self.assertEqual(overlap["sameThreadUnionAfterFirstShaderMs"], 50)
        self.assertEqual(overlap["asynchronousCalls"], 1)
        self.assertEqual(overlap["asynchronousWaits"][0]["overlapMs"], 489)
        self.assertEqual(result["nativeWaitTrace"]["completedWaitsBySite"]["worker_dispatch_wait"]["wallMs"]["median"], 900)

    def test_native_wait_dedup_malformed_stale_and_process_lifetime(self):
        first = with_waits(sample(1000, 10, 10, 4), [wait_event(1, 800000, 900000)] * 2 +
            [wait_event(2, 800000, 900000, wallUs=5), wait_event(3, 800000, 900000, api=99)], overwritten=3)
        first["process"] = {"game": {"creationFileTime": 100}}
        second = with_trace(sample(2000, 40, 30, 13), [traced_event(1, 1800, intervalUs=1000000)])
        second["process"] = {"game": {"creationFileTime": 200}}
        third = with_waits(sample(3000, 50, 40, 17), [wait_event(4, 2100000, 2200000)], overwritten=10)
        third["freshness"] = {"gameValid": False, "hostValid": True}
        result = self.run_analysis([first, first, second, third])
        wait = result["nativeWaitTrace"]
        self.assertEqual(wait["uniqueEvents"], 1)
        self.assertEqual(wait["duplicateEvents"], 3)
        self.assertEqual(wait["malformedEventsOrBatches"], 4)
        self.assertEqual(wait["reportedRingOverwrites"], 3)
        self.assertEqual(wait["staleStatusEventsRejected"], 1)
        self.assertIsNone(result["nativeFrameTrace"]["allRetainedNativeFrames"]["worstStalls"][0]["nativeWaitOverlap"])

    def test_native_wait_priority_retention_keeps_sparse_sequence_and_filter_accounting(self):
        record = with_waits(sample(1000, 10, 10, 4), [wait_event(3, 800000, 900000), wait_event(8, 910000, 940000)],
                            overwritten=6, retentionVersion=2, filterThresholdUs=2000,
                            filteredFastEvents=20000, activeUntracked=3, activeWaits=[])
        wait = self.run_analysis([record])["nativeWaitTrace"]
        self.assertEqual(wait["uniqueEvents"], 2)
        self.assertEqual(wait["reportedRingOverwrites"], 6)
        cumulative = wait["lastCumulativeByProcess"][0]
        self.assertEqual((cumulative["retentionVersion"], cumulative["filterThresholdUs"], cumulative["filteredFastEvents"], cumulative["activeUntracked"]),
                         (2, 2000, 20000, 3))
        self.assertEqual(wait["malformedEventsOrBatches"], 0)
        self.assertEqual(wait["activeWaitObservations"]["uniqueObservedWaits"], 0)

    def test_active_wait_snapshots_never_inflate_completed_frame_overlap(self):
        active = dict(thread=123, callerRva=0xB7320, api=1, site=0, startUs=900000, ageUs=100000,
                      timeout=0xffffffff, objectCount=1, waitAll=0)
        first = with_waits(sample(1000, 10, 10, 4), [], retentionVersion=2, activeWaits=[active])
        second = with_trace(sample(2000, 40, 30, 13), [traced_event(1, 1800, intervalUs=1000000)])
        with_waits(second, [wait_event(1, 900000, 1200000)], retentionVersion=2, activeWaits=[dict(active, ageUs=900000)])
        third = with_waits(sample(3000, 60, 50, 22), [], retentionVersion=2, activeWaits=[])
        result = self.run_analysis([first, second, third])
        overlap = result["nativeFrameTrace"]["focusedGameplayStatusBracketsAllKinds"]["worstStalls"][0]["nativeWaitOverlap"]
        self.assertEqual(overlap["sameThreadUnionMs"], 300)
        wait = result["nativeWaitTrace"]
        self.assertEqual(wait["uniqueEvents"], 1)
        observed = wait["activeWaitObservations"]
        self.assertEqual(observed["uniqueObservedWaits"], 1)
        self.assertEqual(observed["maximumObservedAgeMsBySite"]["producer_camera_wait"]["max"], 900)
        self.assertEqual(observed["longestObserved"][0]["thread"], 123)
        self.assertEqual(wait["lastCumulativeByProcess"][0]["activeWaits"], [])

    def test_active_wait_validation_staleness_and_process_creation(self):
        active = dict(thread=123, callerRva=0xB7320, api=1, site=0, startUs=800000, ageUs=100000,
                      timeout=0xffffffff, objectCount=1, waitAll=0)
        first = with_waits(sample(1000, 10, 10, 4), [], activeWaits=[active, dict(active, ageUs=-1),
                            dict(active, thread=0), dict(active, api=3), dict(active, startUs=True), {}])
        first["process"] = {"game": {"creationFileTime": 100}}
        second = with_waits(sample(2000, 40, 30, 13), [], activeWaits=[dict(active, ageUs=200000)])
        second["process"] = {"game": {"creationFileTime": 200}}
        stale = with_waits(sample(3000, 50, 40, 17), [], activeWaits=[dict(active, ageUs=9000000)])
        stale["freshness"] = {"gameValid": False, "hostValid": True}
        result = self.run_analysis([first, second, stale])["nativeWaitTrace"]
        observed = result["activeWaitObservations"]
        self.assertEqual(observed["uniqueObservedWaits"], 2)
        self.assertEqual(observed["malformedRecordsOrBatches"], 5)
        self.assertEqual(observed["staleObservationsRejected"], 1)
        self.assertEqual(observed["maximumObservedAgeMsBySite"]["producer_camera_wait"]["max"], 200)
        self.assertEqual(len(result["lastCumulativeByProcess"]), 2)
        self.assertEqual(result["uniqueEvents"], 0)

    def test_active_only_wait_does_not_manufacture_completed_stall_overlap(self):
        first = sample(1000, 10, 10, 4)
        second = with_trace(sample(2000, 40, 30, 13), [traced_event(1, 1800, intervalUs=1000000)])
        with_waits(second, [], activeWaits=[dict(thread=123, callerRva=0xB7320, api=1, site=0, startUs=900000, ageUs=900000,
                                              timeout=0xffffffff, objectCount=1, waitAll=0)])
        result = self.run_analysis([first, second])
        self.assertEqual(result["nativeWaitTrace"]["activeWaitObservations"]["uniqueObservedWaits"], 1)
        self.assertIsNone(result["nativeFrameTrace"]["focusedGameplayStatusBracketsAllKinds"]["worstStalls"][0]["nativeWaitOverlap"])

    def test_bad_shader_phase_never_removes_native_frame_or_becomes_zero_cost(self):
        first, second = sample(1000, 10, 10, 4), sample(2000, 40, 30, 13)
        event = dict(zip(TRACE_COLUMNS, traced_event(1, 1800, intervalUs=100000)))
        event.update(firstShaderUs=1900000, shaderThread=456, shaderSpanUs=5, shaderCpuUs=0,
                     shaderCpuKnown=0, preShaderUs=0, preShaderKnown=1, shaderKnown=1)
        second["game"]["nativeFrameTrace"] = {"columns": TRACE_COLUMNS + SHADER_COLUMNS,
            "events": [[event[column] for column in TRACE_COLUMNS + SHADER_COLUMNS]]}
        frames = self.run_analysis([first, second])["nativeFrameTrace"]["focusedGameplayStatusBracketsAllKinds"]
        self.assertEqual(frames["eventCount"], 1)
        self.assertEqual(frames["firstShaderPhase"]["invalidReportedPhases"], 1)
        self.assertIsNone(frames["firstShaderPhase"]["firstShaderToPresentMs"])
        self.assertIsNone(frames["worstStalls"][0]["firstShaderPhase"]["preShaderMs"])

    def test_source_configuration_split_excludes_setting_transition_window(self):
        records = [sample(1000, 10, 10, 4), sample(2000, 40, 30, 13), sample(3000, 50, 40, 17), sample(4000, 70, 60, 26)]
        for index, record in enumerate(records):
            record["game"].update(renderWidth=3840, renderHeight=2160, renderMultisampleType=0 if index < 2 else 8,
                renderMultisampleQuality=0, sourceWidth=3840, sourceHeight=2160)
            record["host"].update(sourceWidth=3840, sourceHeight=2160, recommendedEyeSize=[[2112, 2304], [2112, 2304]])
        with_trace(records[1], [traced_event(1, 1800)])
        with_trace(records[2], [traced_event(2, 2800)])
        with_trace(records[3], [traced_event(3, 3800)])
        result = self.run_analysis(records)
        groups = result["sourceConfigurationSummaries"]["groups"]
        self.assertEqual(len(groups), 2)
        self.assertEqual([group["focusedGameplayAdjacentIntervals"] for group in groups], [1, 1])
        self.assertEqual([group["rates"]["sourceHz"]["median"] for group in groups], [30, 20])
        self.assertEqual(groups[0]["hostSourceDimensions"], [(3840, 2160)])
        self.assertEqual(groups[0]["recommendedPerEyeDimensions"], [((2112, 2304), (2112, 2304))])
        frames = result["nativeFrameTrace"]["focusedGameplayBySourceConfiguration"]
        self.assertEqual([group["frames"]["eventCount"] for group in frames], [1, 1])

    def test_host_delivery_separates_image_arrivals_repeats_and_hold_episodes(self):
        records = [sample(1000, 10, 10, 4), sample(2000, 40, 30, 13)]
        for index, record in enumerate(records):
            record["host"]["sceneDelivery"] = {"freshPairs": 10 + 10 * index, "sceneSubmits": 100 + 120 * index,
                "repeatedSceneSubmits": 90 + 110 * index, "sourcePairIdGaps": 3 + 2 * index,
                "holdEpisodes50Ms": 4 + index, "holdEpisodes100Ms": 2 + index, "holdEpisodes250Ms": index,
                "maxRepeatAgeMs": 200 + 500 * index, "lastNewPairAgeMs": 20 + 100 * index,
                "pairIntervalMedianMs": 25, "pairIntervalP95Ms": 80, "pairIntervalP99Ms": 300, "pairIntervalMaxMs": 700}
        delivery = self.run_analysis(records)["hostSceneDelivery"]
        self.assertEqual(delivery["focusedAdjacentCounterDeltas"]["freshPairs"], 10)
        self.assertEqual(delivery["focusedAdjacentCounterDeltas"]["sceneSubmits"], 120)
        self.assertEqual(delivery["focusedAdjacentCounterDeltas"]["repeatedSceneSubmits"], 110)
        self.assertEqual(delivery["focusedAdjacentCounterDeltas"]["holdEpisodes250Ms"], 1)
        self.assertAlmostEqual(delivery["focusedRepeatedSubmitFraction"], 110 / 120)
        self.assertEqual(delivery["reportedRollingPairArrivalWindows"]["pairIntervalP99Ms"]["median"], 300)
        self.assertEqual(delivery["lastCumulativeByHost"][0]["metrics"]["maxRepeatAgeMs"], 700)
        self.assertTrue(any("do not prove transport drops" in line for line in delivery["limits"]))

    def test_host_delivery_reset_stale_and_mismatched_host_do_not_form_rates(self):
        records = [sample(1000, 10, 10, 4), sample(2000, 40, 30, 13), sample(3000, 70, 60, 23)]
        for record, count in zip(records, [100, 1, 200]):
            record["host"]["sceneDelivery"] = {field: count for field in ("freshPairs", "sceneSubmits", "repeatedSceneSubmits",
                "sourcePairIdGaps", "holdEpisodes50Ms", "holdEpisodes100Ms", "holdEpisodes250Ms")}
        records[2]["freshness"] = {"gameValid": True, "hostValid": False}
        delivery = self.run_analysis(records)["hostSceneDelivery"]
        self.assertEqual(delivery["focusedAdjacentIntervals"], 0)
        self.assertIsNone(delivery["focusedRepeatedSubmitFraction"])
        records[2].pop("freshness");records[2]["host"]["gamePid"] = 99
        self.assertEqual(self.run_analysis(records)["hostSceneDelivery"]["focusedAdjacentIntervals"], 0)
        self.assertFalse(self.run_analysis([sample(1000, 1, 1, 0)])["hostSceneDelivery"]["present"])

    def test_host_delivery_restart_identity_and_invalid_metrics_remain_unknown(self):
        first, second = sample(1000, 10, 10, 4), sample(2000, 40, 30, 13)
        for record, creation in [(first, 100), (second, 200)]:
            record["process"] = {"host": {"creationFileTime": creation}}
            record["host"]["sceneDelivery"] = {field: creation for field in ("freshPairs", "sceneSubmits", "repeatedSceneSubmits",
                "sourcePairIdGaps", "holdEpisodes50Ms", "holdEpisodes100Ms", "holdEpisodes250Ms")}
            record["host"]["sceneDelivery"].update(pairIntervalP99Ms=float("nan"), lastNewPairAgeMs=-1)
        delivery = self.run_analysis([first, second])["hostSceneDelivery"]
        self.assertEqual(delivery["focusedAdjacentIntervals"], 0)
        self.assertEqual(len(delivery["lastCumulativeByHost"]), 2)
        self.assertIsNone(delivery["reportedRollingPairArrivalWindows"]["pairIntervalP99Ms"])
        self.assertIsNone(delivery["reportedRollingPairArrivalWindows"]["lastNewPairAgeMs"])
        json.dumps(delivery, allow_nan=False)


class NativeCameraPhaseTests(unittest.TestCase):
    run_analysis = AnalyzerTests.run_analysis

    def phased(self, **changes):
        first, second = sample(1000, 1, 1, 0), sample(2000, 2, 2, 1)
        event = dict(zip(TRACE_COLUMNS, traced_event(2, 2000, startUs=2000000, intervalUs=1000000)))
        event.update(firstShaderUs=1600000, shaderThread=123, shaderSpanUs=400000, shaderCpuUs=10000,
            shaderCpuKnown=True, preShaderUs=598900, preShaderKnown=True, shaderKnown=True,
            firstCameraUs=1200000, cameraThread=123, cameraKnown=True)
        event.update(changes)
        second["game"]["nativeFrameTrace"] = {"columns": TRACE_COLUMNS + SHADER_COLUMNS + CAMERA_COLUMNS,
            "events": [[event[column] for column in TRACE_COLUMNS + SHADER_COLUMNS + CAMERA_COLUMNS]]}
        return [first, second]

    def test_native_camera_splits_only_the_matching_pre_shader_interval(self):
        trace = self.run_analysis(self.phased())["nativeFrameTrace"]["focusedGameplayStatusBracketsAllKinds"]
        camera = trace["nativeCameraPhase"]
        self.assertEqual((camera["knownFrames"], camera["splitKnownFrames"]), (1, 1))
        self.assertEqual(camera["previousPresentEndToFirstCameraMs"]["median"], 198.9)
        self.assertEqual(camera["firstCameraToFirstShaderMs"]["median"], 400)
        self.assertEqual(camera["firstCameraToPresentMs"]["median"], 800)
        self.assertEqual(trace["worstStalls"][0]["nativeCameraPhase"]["cameraToFirstShaderMs"], 400)

    def test_cross_thread_or_out_of_order_camera_cannot_fabricate_a_phase(self):
        trace = self.run_analysis(self.phased(cameraThread=456))["nativeFrameTrace"]["focusedGameplayStatusBracketsAllKinds"]
        self.assertEqual(trace["nativeCameraPhase"]["knownFrames"], 0)
        self.assertEqual(trace["nativeCameraPhase"]["invalidReportedPhases"], 1)
        trace = self.run_analysis(self.phased(firstCameraUs=1800000))["nativeFrameTrace"]["focusedGameplayStatusBracketsAllKinds"]
        self.assertEqual(trace["nativeCameraPhase"]["knownFrames"], 1)
        self.assertEqual(trace["nativeCameraPhase"]["splitKnownFrames"], 0)
        self.assertIsNone(trace["nativeCameraPhase"]["firstCameraToFirstShaderMs"])
        trace = self.run_analysis(self.phased(firstCameraUs=900000))["nativeFrameTrace"]["focusedGameplayStatusBracketsAllKinds"]
        self.assertEqual(trace["nativeCameraPhase"]["knownFrames"], 0)

    def test_old_trace_camera_fields_remain_unknown(self):
        first, second = sample(1000, 1, 1, 0), with_trace(sample(2000, 2, 2, 1), [traced_event(2, 2000)])
        camera = self.run_analysis([first, second])["nativeFrameTrace"]["focusedGameplayStatusBracketsAllKinds"]["nativeCameraPhase"]
        self.assertEqual(camera["knownFrames"], 0)
        self.assertIsNone(camera["firstCameraToPresentMs"])


class NativeBufferFlushTests(unittest.TestCase):
    run_analysis = AnalyzerTests.run_analysis
    phased = NativeCameraPhaseTests.phased

    def pending(self, **changes):
        item = dict(thread=123, phase=0, resource=1, startUs=1050000, ageUs=50000, pending=True, valid=True,
                    capacity=4096, ranges=4, submittedRanges=1, bytes=1024)
        item.update(changes)
        return item

    def test_missing_trace_keeps_old_logs_unknown(self):
        result = self.run_analysis(self.phased())
        self.assertFalse(result["nativeBufferFlushTrace"]["present"])
        self.assertEqual(result["nativeBufferFlushTrace"]["completedByPhase"], {})
        self.assertIsNone(result["nativeFrameTrace"]["allRetainedNativeFrames"]["worstStalls"][0]["nativeBufferFlushPreCameraOverlap"])

    def test_completed_and_cumulative_are_separate_and_hresult_is_signed(self):
        first = with_flushes(sample(1000, 1, 1, 0), [flush_event(7, 700000, 800000, result=-2147467259, pending=1, valid=1, shadowReady=1)], overwritten=5)
        second = with_flushes(sample(2000, 2, 2, 1), [flush_event(7, 700000, 800000, result=-2147467259, pending=1, valid=1, shadowReady=1)], overwritten=5)
        result = self.run_analysis([first, second])
        report = result["nativeBufferFlushTrace"]
        self.assertEqual((report["uniqueEvents"], report["duplicateEvents"], report["reportedRingOverwrites"]), (1, 1, 5))
        phase = report["completedByPhase"]["staticFlush"]
        self.assertEqual((phase["completedCalls"], phase["failed"], phase["totalWallMs"], phase["wallMs"]["max"]), (1, 1, 100, 100))
        snapshot = report["lastCumulativeByProcess"][0]
        self.assertTrue(snapshot["batching"])
        self.assertEqual((snapshot["savedLocks"], snapshot["batchedCalls"], snapshot["phases"][0]["maxUs"]), (12, 4, 90000))
        self.assertTrue(any("savedLocks=12" in line and "max=90.00 ms" in line for line in result["humanSummary"]))

    def test_precamera_overlap_clips_unions_and_excludes_other_threads_and_pending(self):
        records = self.phased()
        with_flushes(records[1], [flush_event(1, 990000, 1100000), flush_event(2, 1050000, 1250000, phase=1, resource=0),
                                  flush_event(3, 1001100, 1200000, thread=456), flush_event(4, 1300000, 1400000)],
                     inProgress=self.pending())
        result = self.run_analysis(records)
        overlap = result["nativeFrameTrace"]["focusedGameplayStatusBracketsAllKinds"]["worstStalls"][0]["nativeBufferFlushPreCameraOverlap"]
        self.assertEqual((overlap["preCameraMs"], overlap["sameThreadUnionMs"], overlap["sameThreadCalls"]), (198.9, 198.9, 2))
        self.assertEqual(overlap["byPhase"]["staticFlush"]["unionMs"], 98.9)
        self.assertEqual(overlap["byPhase"]["BeginScene"]["unionMs"], 150)
        self.assertEqual(result["nativeBufferFlushTrace"]["inProgressObservations"]["uniqueObservedCalls"], 1)
        self.assertEqual(result["nativeBufferFlushTrace"]["uniqueEvents"], 4)

    def test_sparse_upload_counters_preserve_unknown_for_old_logs(self):
        old = self.run_analysis([with_flushes(sample(1000, 1, 1, 0), [])])
        self.assertIsNone(old["nativeBufferFlushTrace"]["lastCumulativeByProcess"][0]["singleMapCalls"])
        record = with_flushes(sample(2000, 2, 2, 1), [], singleMapCalls=80, lockFailures=2, unlockFailures=1, invalidMappings=0, batchingFaulted=True)
        result = self.run_analysis([record])
        snapshot = result["nativeBufferFlushTrace"]["lastCumulativeByProcess"][0]
        self.assertEqual([snapshot[key] for key in ("singleMapCalls", "lockFailures", "unlockFailures", "invalidMappings")], [80, 2, 1, 0])
        self.assertTrue(snapshot["batchingFaulted"])
        self.assertTrue(any("singleMapCalls=80" in line and "unlockFailures=1" in line for line in result["humanSummary"]))

    def test_progress_then_completion_is_counted_once_and_snapshot_clears(self):
        records = self.phased()
        with_flushes(records[0], [], inProgress=self.pending())
        with_flushes(records[1], [flush_event(1, 1050000, 1150000)], inProgress=None)
        report = self.run_analysis(records)
        flush = report["nativeBufferFlushTrace"]
        self.assertEqual(flush["completedByPhase"]["staticFlush"]["totalWallMs"], 100)
        self.assertEqual(flush["inProgressObservations"]["longestObserved"][0]["ageUs"], 50000)
        self.assertIsNone(flush["lastCumulativeByProcess"][0]["inProgress"])
        overlap = report["nativeFrameTrace"]["allRetainedNativeFrames"]["worstStalls"][0]["nativeBufferFlushPreCameraOverlap"]
        self.assertEqual(overlap["sameThreadUnionMs"], 100)

    def test_backend_support_does_not_follow_on12_capability_alone(self):
        for backend, supported in (("Original", True), ("DXVK", False), ("D3D9On12", True)):
            record = with_flushes(sample(1000, 1, 1, 0), [], backend=backend, batchingSupported=supported, on12=backend == "D3D9On12")
            snapshot = self.run_analysis([record])["nativeBufferFlushTrace"]["lastCumulativeByProcess"][0]
            self.assertEqual((snapshot["backend"], snapshot["batchingSupported"]), (backend, supported))
        old = self.run_analysis([with_flushes(sample(1000, 1, 1, 0), [])])["nativeBufferFlushTrace"]["lastCumulativeByProcess"][0]
        self.assertIsNone(old["backend"])
        self.assertIsNone(old["batchingSupported"])

    def test_missing_camera_or_process_lifetime_never_creates_correlation(self):
        records = self.phased(cameraThread=456)
        with_flushes(records[1], [flush_event(1, 1050000, 1150000)])
        self.assertIsNone(self.run_analysis(records)["nativeFrameTrace"]["allRetainedNativeFrames"]["worstStalls"][0]["nativeBufferFlushPreCameraOverlap"])
        records = self.phased()
        records[0]["process"] = {"game": {"creationFileTime": 1}}
        records[1]["process"] = {"game": {"creationFileTime": 2}}
        with_flushes(records[0], [flush_event(1, 1050000, 1150000)])
        with_flushes(records[1], [flush_event(1, 1500000, 1600000)])
        result = self.run_analysis(records)
        self.assertEqual(result["nativeBufferFlushTrace"]["uniqueEvents"], 2)
        overlap = result["nativeFrameTrace"]["allRetainedNativeFrames"]["worstStalls"][0]["nativeBufferFlushPreCameraOverlap"]
        self.assertEqual(overlap["sameThreadUnionMs"], 0)

    def test_malformed_and_stale_evidence_is_rejected(self):
        first = with_flushes(sample(1000, 1, 1, 0), [flush_event(1, 700000, 800000, wallUs=1),
            flush_event(2, 700000, 800000, result=0xffffffff), flush_event(3, 700000, 800000, phase=2),
            flush_event(4, 700000, 800000, pending="yes")], inProgress={"bad": 1}, savedLocks=float("nan"))
        second = with_flushes(sample(2000, 2, 2, 1), [flush_event(5, 1050000, 1150000)], inProgress=self.pending())
        second["freshness"] = {"gameValid": False, "hostValid": True}
        result = self.run_analysis([first, second])["nativeBufferFlushTrace"]
        self.assertEqual((result["uniqueEvents"], result["malformedEventsOrBatches"], result["staleStatusEventsRejected"]), (0, 4, 1))
        self.assertEqual((result["inProgressObservations"]["malformedRecords"], result["inProgressObservations"]["staleObservationsRejected"]), (1, 1))
        self.assertIsNone(result["lastCumulativeByProcess"][0]["savedLocks"])
        json.dumps(result, allow_nan=False)


class CPUCaptureStageTests(unittest.TestCase):
    run_analysis = AnalyzerTests.run_analysis
    def staged(self, tick, presents, calls, map_total, publish_total=0, **changes):
        value = sample(tick, presents, presents, presents // 2)
        value["game"].update(renderWidth=3840, renderHeight=2160, renderMultisampleType=8,
            renderMultisampleQuality=0, sourceWidth=3840, sourceHeight=2160,
            gpuTransportActive=False, nativeActionPreserveRig=False, **changes)
        value["game"]["cpuCaptureStages"] = {
            "map": {"calls": calls, "totalUs": map_total, "maxUs": 12000},
            "publish": {"calls": calls, "totalUs": publish_total, "maxUs": 5000},
            "convert": {"calls": 0, "totalUs": 0, "maxUs": 0}}
        return value

    def test_stage_deltas_exclude_prior_cumulative_work(self):
        result = self.run_analysis([self.staged(1000, 100, 100, 1000000, 300000),
                                    self.staged(2000, 150, 150, 1400000, 450000)])
        stages = result["cpuCaptureStages"]
        self.assertTrue(stages["present"])
        group = stages["groups"][0]
        self.assertEqual(group["focusedGameplayAdjacentIntervals"], 1)
        self.assertEqual(group["configuration"]["renderMultisampleType"], 8)
        self.assertEqual(group["stages"]["map"]["callsDelta"], 50)
        self.assertEqual(group["stages"]["map"]["weightedMeanMsPerCall"], 8)
        self.assertEqual(group["stages"]["map"]["windowMsPerSourceFrame"]["median"], 8)
        self.assertEqual(group["stages"]["publish"]["weightedMeanMsPerCall"], 3)
        self.assertIsNone(group["stages"]["convert"]["weightedMeanMsPerCall"])
        self.assertIsNone(group["stages"]["readback"]["windowMeanMsPerCall"])

    def test_old_missing_stage_logs_are_unknown(self):
        stages = self.run_analysis([sample(1000, 10, 10, 4), sample(2000, 40, 30, 13)])["cpuCaptureStages"]
        self.assertFalse(stages["present"])
        self.assertEqual(stages["groups"], [])

    def test_stage_counter_reset_and_malformed_payload_are_not_work(self):
        first, second = self.staged(1000, 100, 100, 1000000), self.staged(2000, 150, 1, 100)
        result = self.run_analysis([first, second])["cpuCaptureStages"]
        self.assertGreaterEqual(result["invalidOrResetStageIntervals"], 1)
        self.assertEqual(result["groups"][0]["stages"]["map"]["callsDelta"], 0)
        first["game"]["cpuCaptureStages"] = ["malformed"]
        result = self.run_analysis([first, second])["cpuCaptureStages"]
        self.assertGreater(result["missingStageIntervals"], 0)
        self.assertIsNone(result["groups"][0]["stages"]["map"]["weightedMeanMsPerCall"])

    def test_stage_focus_configuration_and_rig_changes_break_windows(self):
        records = [self.staged(1000, 100, 100, 1000000), self.staged(2000, 150, 150, 1400000),
                   self.staged(3000, 200, 200, 1800000), self.staged(4000, 250, 250, 2200000),
                   self.staged(5000, 300, 300, 2600000)]
        records[1]["game"]["gameFocused"] = False
        records[3]["game"]["renderMultisampleType"] = 0
        records[4]["game"]["nativeActionPreserveRig"] = True
        result = self.run_analysis(records)["cpuCaptureStages"]
        self.assertEqual(result["groups"], [])

    def test_stage_duplicates_and_host_identity_do_not_extend_deltas(self):
        first, second = self.staged(1000, 100, 100, 1000000), self.staged(2000, 150, 150, 1400000)
        result = self.run_analysis([first, first, second, second])["cpuCaptureStages"]
        self.assertEqual(result["uniquePayloadSnapshots"], 2)
        self.assertEqual(result["groups"][0]["stages"]["map"]["callsDelta"], 50)
        second["host"]["gamePid"] = 99
        self.assertEqual(self.run_analysis([first, second])["cpuCaptureStages"]["groups"], [])


class ScenePublicationHoldTests(unittest.TestCase):
    run_analysis = AnalyzerTests.run_analysis

    def held(self, tick, presents, scene=100, camera=500, pairs=50, mode="stale"):
        record = sample(tick, presents, scene, pairs)
        record["utc"] = str(tick)
        record["game"].update(panelFrames=20, gpuCameraCalls=camera, nativeMenuActive=False,
            cinematicPanel=False, nativeMovieActive=False, lastSceneGate="frame_not_tracked")
        record["host"].update(renderMode=mode, leftFrameAgeMs=tick, hmdMounted=True)
        return record

    def test_loading_hold_with_stale_host_is_not_filtered_out(self):
        records = [self.held(tick, 100 + index * 25) for index, tick in enumerate((1000, 2000, 3000, 4000))]
        result = self.run_analysis(records)
        self.assertEqual(result["uniqueFocusedSceneSamples"], 0)
        holds = result["scenePublicationHolds"]
        self.assertEqual(holds["significantEpisodeCount"], 1)
        episode = holds["longestEpisodes"][0]
        self.assertEqual((episode["kind"], episode["durationMs"], episode["nativePresentsDelta"]),
                         ("no_native_camera_packets", 3000, 75))
        self.assertEqual(episode["nativeCameraCallsDelta"], 0)
        self.assertEqual(episode["maxReportedLeftAgeMs"], 4000)

    def test_source_stall_and_host_delivery_have_distinct_kinds(self):
        stopped = [self.held(1000, 100), self.held(2000, 100)]
        self.assertEqual(self.run_analysis(stopped)["scenePublicationHolds"]["longestEpisodes"][0]["kind"], "no_native_presents")
        delivered = [self.held(1000, 100), self.held(2000, 125, scene=125, camera=525)]
        holds = self.run_analysis(delivered)["scenePublicationHolds"]
        self.assertEqual(holds["longestEpisodes"][0]["kind"], "host_pair_delivery_not_progressing")
        self.assertEqual(holds["significantEpisodeCount"], 0)

    def test_healthy_scope_is_not_a_stalled_stereo_pair_delivery(self):
        records = [self.held(tick, 100 + index * 25, scene=100 + index * 25, camera=500 + index * 25,
                             mode="scope" if index == 0 else "scope_hold")
                   for index, tick in enumerate((1000, 2000, 3000, 4000))]
        for record in records:
            record["game"]["nativeScopeActive"] = True
            record["host"]["leftFrameAgeMs"] = 100
        holds = self.run_analysis(records)["scenePublicationHolds"]
        self.assertEqual(holds["episodeCount"], 0)
        self.assertEqual(holds["healthyScopeIntervalsExcluded"], 3)
        records[1]["host"]["leftFrameAgeMs"] = 1001
        records[2]["host"].update(renderMode="stale", leftFrameAgeMs=2000)
        records[3]["host"].update(renderMode="stale", leftFrameAgeMs=3000)
        holds = self.run_analysis(records)["scenePublicationHolds"]
        self.assertEqual(holds["healthyScopeIntervalsExcluded"], 0)
        self.assertEqual(holds["significantEpisodeCount"], 1)
        self.assertEqual(holds["longestEpisodes"][0]["kind"], "host_pair_delivery_not_progressing")

    def test_menu_focus_live_panel_and_identity_break_hold(self):
        first, second = self.held(1000, 100), self.held(2000, 125)
        second["game"]["nativeMenuActive"] = True
        self.assertEqual(self.run_analysis([first, second])["scenePublicationHolds"]["episodeCount"], 0)
        second["game"]["nativeMenuActive"] = False
        second["game"]["panelFrames"] = 45
        self.assertEqual(self.run_analysis([first, second])["scenePublicationHolds"]["episodeCount"], 0)
        second["game"]["panelFrames"] = 20
        second["game"]["nativeLoadingActive"] = True
        self.assertEqual(self.run_analysis([first, second])["scenePublicationHolds"]["episodeCount"], 0)
        second["game"]["nativeLoadingActive"] = False
        second["host"]["gamePid"] = 99
        self.assertEqual(self.run_analysis([first, second])["scenePublicationHolds"]["episodeCount"], 0)


if __name__ == "__main__":
    unittest.main()
