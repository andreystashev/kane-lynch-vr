"""Synthetic stream/ABI tests; these do not claim native rendering proof."""
import csv
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

MODULE = Path(__file__).resolve().parents[2] / "tools/decode_visual_trace.py"
spec = importlib.util.spec_from_file_location("decode_visual_trace", MODULE)
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


def native_field(offset, size=4, count=1):
    return {"offset": offset, "bytes": size * count, "elementBytes": size, "count": count}


def schemas():
    fields = {"version": [0, 4], "index": [4, 4], "tick": [8, 4], "thread": [12, 4], "call": [16, 44], "rt": [64, 8], "vsHash": [72, 8], "psHash": [80, 8], "rtDesc": [88, 32], "viewport": [120, 24], "world": [144, 64], "results": [208, 16]}
    call = {name: [i * 4, 4] for i, name in enumerate("kind type baseVertex minVertex vertices first count stride indexFormat vertexData indexData".split())}
    types = {
        "DrawCall": {"size": 44, "fields": call},
        "DrawResult": {"size": 24, "fields": {name: [i * 4, 4] for i, name in enumerate("result preview detail page tile detailIndex".split())}},
        "ShaderHeader": {"size": 32, "fields": {"pointer": [0, 8], "hash": [8, 8], "kind": [16, 4], "length": [20, 4], "result": [24, 4]}},
        "SurfaceDesc": {"size": 32, "fields": {name: [i * 4, 4] for i, name in enumerate("Format Type Usage Pool MultiSampleType MultiSampleQuality Width Height".split())}},
    }
    runtime = {"version": 1, "endianness": "little", "pointerBytes": 4, "eventHeader": ["tag:u32", "size:u32", "draw:u32", "ordinal:u32", "sequence:u64"], "drawBytes": 256, "resultBytes": 24, "shaderHeaderBytes": 32, "fields": fields, "types": types, "grid": [1, 1], "tile": [2, 2], "atlasPages": 1}
    native = {"format": 1, "pointerBytes": 4, "endian": "little", "structs": {
        "D": {"size": 24, "fields": {"version": native_field(0), "evidence": native_field(4), "pass": native_field(8), "entry": native_field(12), "roles": native_field(16), "support": native_field(20, 1)}},
        "C": {"size": 16, "fields": {"version": native_field(0), "failures": native_field(4), "viewMatches": native_field(8, 1), "projectionMatches": native_field(9, 1)}},
        "V": {"size": 64, "fields": {"valid": native_field(0, 1), "entry": native_field(4), "world": native_field(8, 4, 12), "sequence": native_field(56, 8)}},
        "B": {"size": 40, "fields": {"sequence": native_field(0, 8), "queue": native_field(8), "before": native_field(12), "after": native_field(16), "commands": native_field(20), "complete": native_field(24, 1), "reason": native_field(28)}},
    }}
    return runtime, native


def png(width=2, height=2):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    pixels = (b"\0" + b"\0\xff\0\xff" * width) * height
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b"")


class VisualDecoderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="kl1-decoder-")
        self.root = Path(self.temp.name)
        self.session = self.root / "session"; self.session.mkdir()
        self.frame = self.session / "frame-0001"; self.frame.mkdir()
        self.output = self.root / "decoded"
        self.runtime, self.native = schemas()
        self.native_path = self.root / "build-native-evidence-schema.json"
        decoder.write_json(self.session / "schema.json", self.runtime)
        decoder.write_json(self.native_path, self.native)
        self.events = []
        self.shader = struct.pack("<2I", 0xfffe0200, 0xffff)
        self.batch = struct.pack("<7I", 0x12, 0x123400, 0x900000, 0, 9, 1, 0xabcdef)
        self.add(3, struct.pack("<QQIIi4x", 0x778800, decoder.fnv64(self.shader), 0, len(self.shader), 0) + self.shader)
        self.add(13, self.batch_context(77, len(self.batch), 1), draw=0)
        self.add(14, self.batch, draw=0, sequence=77)
        state = bytearray(256)
        struct.pack_into("<4I", state, 0, 1, 1, 123, 456)
        struct.pack_into("<IIi8I", state, 16, 1, 5, -13, 19, 23, 7, 11, 0, 0, 0, 0)
        struct.pack_into("<3Q", state, 64, 0x88880000, decoder.fnv64(self.shader), 0)
        struct.pack_into("<8I", state, 88, 21, 1, 1, 0, 0, 0, 2, 2)
        struct.pack_into("<4I2f", state, 120, 0, 0, 2, 2, 0, 1)
        struct.pack_into("<16f", state, 144, *[float(i) / 4 for i in range(16)])
        self.add(1, state)
        self.add(10, struct.pack("<5IB3x", 1, 31, 9, 0x123400, 4 | 16 | 32, 1))
        self.add(11, struct.pack("<IIBB6x", 1, 512, 0, 1))
        self.add(12, struct.pack("<B3xI12fQ", 1, 0x123400, *[float(i) for i in range(12)], 19))
        self.add(2, struct.pack("<iiiIII", 0, 0, -2147483638, 0, 0, 0xffffffff))
        self.add(13, self.batch_context(78, 0, 0), sequence=78)
        self.add(14, b"", sequence=78)
        self.save()

    def tearDown(self):
        self.temp.cleanup()

    def add(self, tag, payload, draw=1, sequence=0):
        self.events.append(decoder.HEADER.pack(tag, len(payload), draw, len(self.events), sequence) + payload)

    def batch_context(self, sequence, length, count):
        return struct.pack("<QIIIIB3xI8x", sequence, 0x900000, 0x10000, 0x10000 + length, count, 1, 0)

    def save(self, raw=None):
        data = b"".join(self.events) if raw is None else raw
        (self.frame / "events.bin").write_bytes(data)
        (self.frame / "atlas-00.png").write_bytes(png())
        decoder.write_json(self.frame / "frame.json", {"draws": 1, "eventBytes": len(data), "images": [{"name": "atlas-00.png", "width": 2, "height": 2, "result": 0}]})

    def run_decoder(self, **options):
        return decoder.Decoder(self.session, self.output, self.native_path, **options).run()

    def codes(self, summary):
        return {issue["code"] for issue in summary["issues"]}

    def test_full_stream_dynamic_schemas_and_extractions(self):
        original = (self.frame / "events.bin").read_bytes()
        summary = self.run_decoder(extract_shaders=True, extract_batches=True)
        self.assertTrue(summary["complete"], summary["issues"])
        self.assertEqual((summary["draws"], summary["events"], summary["shaders"], summary["batches"], summary["rawBatches"]), (1, 10, 1, 2, 2))
        row = json.loads((self.output / "draws.jsonl").read_text())
        self.assertEqual(row["actual"]["call"]["baseVertex"], -13)
        self.assertEqual(row["owner"]["roleNames"], ["rightHandItem", "activeRightHand", "firearm"])
        self.assertEqual(row["camera"]["failureNames"], ["viewMismatch"])
        self.assertEqual(row["effect"]["world"][5], 5.0)
        self.assertTrue(row["previewImage"]["available"])
        self.assertEqual(row["result"]["resultHRESULT"]["hex"], "0x00000000")
        self.assertEqual(next((self.output / "shaders").glob("*.bin")).read_bytes(), self.shader)
        self.assertEqual(sorted(path.stat().st_size for path in (self.output / "batches").glob("*.bin")), [0, len(self.batch)])
        with (self.output / "draws.csv").open(encoding="utf-8-sig", newline="") as stream:
            csv_row = next(csv.DictReader(stream))
        self.assertEqual(csv_row["pass"], "9")
        self.assertEqual((self.frame / "events.bin").read_bytes(), original)

    def test_truncated_header_and_payload_are_explicit(self):
        self.save(b"".join(self.events) + b"\1\2\3")
        summary = self.run_decoder()
        self.assertIn("truncated-event-header", self.codes(summary))
        self.save(decoder.HEADER.pack(1, 256, 1, 0, 0) + b"short")
        summary = self.run_decoder()
        self.assertIn("invalid-event-size", self.codes(summary))

    def test_sampled_preview_is_explicit_without_false_missing_image(self):
        self.runtime["previewStride"] = 32
        decoder.write_json(self.session / "schema.json", self.runtime)
        for index, event in enumerate(self.events):
            tag, size, draw, ordinal, sequence = decoder.HEADER.unpack_from(event)
            if tag == 2:
                self.events[index] = decoder.HEADER.pack(tag, size, draw, ordinal, sequence) + struct.pack(
                    "<iiiIII", 0, 1, -2147483638, 0xffffffff, 0xffffffff, 0xffffffff)
        self.save()
        summary = self.run_decoder()
        self.assertTrue(summary["complete"], summary["issues"])
        self.assertEqual((summary["previewSkipped"], summary["previewFailures"]), (1, 0))
        row = json.loads((self.output / "draws.jsonl").read_text())
        self.assertTrue(row["result"]["previewSkipped"])
        self.assertEqual(row["previewImage"], {})
        # Old schemas do not silently reinterpret a corrupt atlas result.
        self.runtime.pop("previewStride")
        decoder.write_json(self.session / "schema.json", self.runtime)
        summary = self.run_decoder()
        self.assertIn("invalid-atlas-index", self.codes(summary))

    def test_missing_and_corrupted_png(self):
        (self.frame / "atlas-00.png").unlink()
        summary = self.run_decoder()
        self.assertIn("missing-png", self.codes(summary))
        self.assertIn("missing-preview-image", self.codes(summary))
        (self.frame / "atlas-00.png").write_bytes(png()[:-3])
        summary = self.run_decoder()
        self.assertIn("invalid-png", self.codes(summary))

    def test_native_schema_size_and_pointer_mismatch(self):
        self.native["structs"]["D"]["size"] += 4
        decoder.write_json(self.native_path, self.native)
        summary = self.run_decoder()
        self.assertIn("invalid-event-payload", self.codes(summary))
        self.native["pointerBytes"] = 8
        decoder.write_json(self.native_path, self.native)
        with self.assertRaises(decoder.DecodeError):
            self.run_decoder()

    def test_unknown_event_ordinal_gap_and_event_budget(self):
        self.events.append(decoder.HEADER.pack(99, 1, 1, 100, 0) + b"x")
        self.save()
        summary = self.run_decoder()
        self.assertTrue({"unknown-event-tag", "event-ordinal-gap"}.issubset(self.codes(summary)))
        summary = self.run_decoder(max_event_bytes=32)
        self.assertIn("invalid-event-size", self.codes(summary))

    def test_closed_batch_bounds_queue_and_empty(self):
        self.assertEqual(decoder.raw_batch(b"")["commands"], [])
        with self.assertRaises(decoder.DecodeError):
            decoder.raw_batch(self.batch, queue=123)
        with self.assertRaises(decoder.DecodeError):
            decoder.raw_batch(self.batch[:-1])
        with self.assertRaises(decoder.DecodeError):
            decoder.raw_batch(b"x")

    def test_missing_batch_and_result_and_nonfinite(self):
        self.events = [event for event in self.events if decoder.HEADER.unpack_from(event)[0] not in (14, 2)]
        self.save()
        summary = self.run_decoder()
        self.assertIn("missing-raw-batch", self.codes(summary))
        self.assertIn("missing-draw-result", self.codes(summary))
        self.assertEqual(decoder.scalars(struct.pack("<3f", float("nan"), float("inf"), -float("inf")), floating=True), ["NaN", "+Infinity", "-Infinity"])

    def test_schema_bounds_and_png_crc(self):
        runtime, _ = schemas()
        state = bytearray(256); struct.pack_into("<f", state, 136, float("nan"))
        self.assertEqual(decoder.decode_actual(state, runtime)["viewport"]["minZ"], "NaN")
        runtime["fields"]["world"] = [250, 64]
        with self.assertRaises(decoder.DecodeError):
            decoder.decode_actual(bytes(256), runtime)
        path = self.root / "bad.png"; data = bytearray(png()); data[20] ^= 1; path.write_bytes(data)
        with self.assertRaises(decoder.DecodeError):
            decoder.png_info(path)


if __name__ == "__main__":
    unittest.main()
