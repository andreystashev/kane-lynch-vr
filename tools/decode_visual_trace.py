#!/usr/bin/env python3
"""Decode a KL1 visual session without third-party modules.

Usage: python decode_visual_trace.py SESSION [--native-schema BUILD_SCHEMA]
       [--output DIRECTORY] [--extract-shaders] [--extract-batches]

The input is never modified. Output is streaming JSONL/CSV plus summary.json.
Nonzero exit means evidence is incomplete/malformed, not a gameplay failure.
"""
import argparse
import binascii
import csv
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

HEADER = struct.Struct("<IIIIQ")
ALIASES = {10: "D", 11: "C", 12: "V", 13: "B"}
LABELS = {10: "owner", 11: "camera", 12: "effect"}
NESTED = {
    "D": {"hands": "H", "ancestors": "A", "world": "M", "view": "M", "inlineWorld": "M",
          "sourceBind": "W", "nativeSourceBind": "W", "weaponSource": "M", "weaponTracked": "M"},
    "W": {"world": "M"}, "M": {"basis": "R", "position": "X"},
    "R": {"forward": "X", "up": "X", "right": "X"},
    "V": {"producer": "S"}, "B": {"packets": "P"},
}
FLOATS = {
    "D": {"projection"}, "X": {"x", "y", "z"},
    "C": {"actualWorld", "actualView", "actualProjection", "expectedView", "vrProjection", "nativeProjection", "packetView", "packetProjection"},
    "V": {"world", "nativeView", "eye", "reference", "projection", "rootOriginal", "rootBound"},
    "S": {"transform", "position"},
}
SIGNED = {"D": {"weaponSide"}, "DrawCall": {"baseVertex"}, "DrawResult": {"result", "preview", "detail"},
          "ShaderHeader": {"result"}, "Texture": {"result"}, "Stream": {"result"}}
BOOLS = {
    "D": {"sourceBindMatchesState", "nativeSourceMatchesEntry", "skinLease", "skinLeaseMatchesEntry", "support", "coupled", "shadow", "weaponBinding", "weaponBindingMatchesEntry", "weaponSupport"},
    "W": {"valid"}, "C": {"installed", "frameValid", "anchorValid", "nativeCinematic", "actorCinematic", "stateRead", "expectedViewValid", "viewMatches", "projectionMatches", "pairValid", "secondaryActive", "packetHeaderRead", "packetSourceRead", "packetCameraFlagsRead"},
    "B": {"cameraFlagsRead", "sourceVerified", "complete"},
    "V": {"valid", "viewKnown", "rawBeforeKnown", "rawAfterKnown", "recordsKnown", "rootKnown"},
    "S": {"valid", "transformKnown", "positionKnown"},
}
ROLE_NAMES = {1: "localBody", 2: "leftHandItem", 4: "rightHandItem", 8: "activeLeftHand", 16: "activeRightHand", 32: "firearm", 64: "attachedToLocalPlayer", 128: "stowedFirearm", 256: "ownerRootMatches", 512: "attachmentAtWrist"}
CAMERA_FAILURES = {1: "notInstalled", 2: "missingState", 4: "foreignState", 8: "noFrame", 16: "oldGeneration", 32: "foreignThread", 64: "cinematic", 128: "unreadableState", 256: "invalidExpectedView", 512: "viewMismatch", 1024: "projectionMismatch"}
FORMATS = {0: "UNKNOWN", 21: "A8R8G8B8", 22: "X8R8G8B8", 23: "R5G6B5", 75: "D24S8", 113: "A16B16G16R16F", 114: "R32F", 116: "A32B32G32R32F"}


class DecodeError(ValueError):
    pass


def load_json(path):
    with Path(path).open("r", encoding="utf-8-sig") as stream:
        return json.load(stream)


def write_json(path, value):
    with Path(path).open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")


def json_line(stream, value):
    stream.write(json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False) + "\n")


def field_shape(field):
    if isinstance(field, dict):
        return int(field["offset"]), int(field["bytes"]), int(field.get("elementBytes", 4)), int(field.get("count", 1))
    offset, size = map(int, field)
    return offset, size, size if size in (1, 2, 4, 8) else 4, 1 if size in (1, 2, 4, 8) else size // 4


def scalars(data, element=4, floating=False, signed=False):
    if element not in (1, 2, 4, 8) or len(data) % element:
        raise DecodeError("invalid scalar field shape")
    code = "f" if floating and element == 4 else "d" if floating and element == 8 else {1: "b" if signed else "B", 2: "h" if signed else "H", 4: "i" if signed else "I", 8: "q" if signed else "Q"}[element]
    values = list(struct.unpack("<" + code * (len(data) // element), data))
    if floating:
        values = [v if math.isfinite(v) else "NaN" if math.isnan(v) else "+Infinity" if v > 0 else "-Infinity" for v in values]
    return values


def decode_struct(data, alias, definitions):
    definition = definitions.get(alias)
    if not definition:
        raise DecodeError("missing ABI schema for " + alias)
    size = int(definition["size"])
    if len(data) < size:
        raise DecodeError(f"{alias} payload {len(data)} < ABI size {size}")
    result = {}
    for name, shape in definition["fields"].items():
        offset, length, element, count = field_shape(shape)
        if offset < 0 or length < 0 or offset + length > size:
            raise DecodeError(f"{alias}.{name} outside ABI structure")
        raw = data[offset:offset + length]
        nested = NESTED.get(alias, {}).get(name)
        if nested:
            stride = int(definitions[nested]["size"])
            if not stride or length % stride:
                raise DecodeError(f"{alias}.{name} has invalid nested stride")
            values = [decode_struct(raw[i:i + stride], nested, definitions) for i in range(0, length, stride)]
        else:
            if not isinstance(shape, dict) and name in ("pointer", "hash"):
                element = 8
            values = scalars(raw, element, name in FLOATS.get(alias, set()), name in SIGNED.get(alias, set()))
            if name in BOOLS.get(alias, set()):
                values = [bool(v) for v in values]
        result[name] = values[0] if len(values) == 1 else values
    if alias == "D":
        result["ancestors"] = result.get("ancestors", [])[:min(int(result.get("ancestorCount", 0)), 8)]
        result["roleNames"] = [name for mask, name in ROLE_NAMES.items() if int(result.get("roles", 0)) & mask]
        for ancestor in result["ancestors"]:
            ancestor["roleNames"] = [name for mask, name in ROLE_NAMES.items() if int(ancestor.get("roles", 0)) & mask]
    elif alias == "C":
        result["failureNames"] = [name for mask, name in CAMERA_FAILURES.items() if int(result.get("failures", 0)) & mask]
    elif alias == "B":
        result["packets"] = sorted(result.get("packets", [])[:min(int(result.get("packetsStored", 0)), 128)], key=lambda packet: packet.get("ordinal", 0))
        for packet in result["packets"]:
            packet["payload"] = packet.get("payload", [])[:min(int(packet.get("payloadWords", 0)), 40)]
    return result


def decode_actual(data, schema):
    if len(data) != int(schema["drawBytes"]):
        raise DecodeError("actual draw payload does not match runtime drawBytes")
    result = {}
    definitions = schema.get("types", {})
    for name, shape in schema["fields"].items():
        offset, length, _, _ = field_shape(shape)
        if offset < 0 or length < 0 or offset + length > len(data):
            raise DecodeError("runtime field outside actual draw: " + name)
        raw = data[offset:offset + length]
        kind = {"call": "DrawCall", "textures": "Texture", "streams": "Stream", "rtDesc": "SurfaceDesc", "depthDesc": "SurfaceDesc"}.get(name)
        if kind:
            stride = int(definitions.get(kind, {}).get("size", 0))
            if not stride or length % stride:
                raise DecodeError("missing/incompatible runtime type: " + kind)
            values = [decode_struct(raw[i:i + stride], kind, definitions) for i in range(0, length, stride)]
            result[name] = values[0] if name in ("call", "rtDesc", "depthDesc") else values
        elif name == "elements":
            if length % 8:
                raise DecodeError("invalid declaration element byte count")
            result[name] = [dict(zip(("stream", "offset", "type", "method", "usage", "usageIndex"), struct.unpack_from("<HHBBBB", raw, i))) for i in range(0, length, 8)]
        else:
            floating = name in ("world", "view", "projection", "vsConstants", "psConstants")
            signed = name in ("renderStateResults", "samplerResults", "transformResults", "results", "scissor")
            element = 8 if name in ("rt", "depth", "vs", "ps", "declaration", "indexBuffer", "vsHash", "psHash") else 4
            values = scalars(raw, element, floating, signed)
            result[name] = values[0] if len(values) == 1 else values
    if isinstance(result.get("viewport"), list) and len(result["viewport"]) == 6:
        offset = field_shape(schema["fields"]["viewport"])[0]
        values = list(struct.unpack_from("<IIII", data, offset)) + scalars(data[offset + 16:offset + 24], floating=True)
        result["viewport"] = dict(zip(("x", "y", "width", "height", "minZ", "maxZ"), values))
    result["elements"] = result.get("elements", [])[:min(int(result.get("elementCount", 0)), 65)]
    for key in ("rtDesc", "depthDesc"):
        if key in result:
            result[key]["formatName"] = FORMATS.get(result[key].get("Format"), "other")
    return result


def hresult(value):
    unsigned = int(value) & 0xffffffff
    return {"signed": unsigned - 0x100000000 if unsigned & 0x80000000 else unsigned, "hex": f"0x{unsigned:08x}", "succeeded": not bool(unsigned & 0x80000000)}


def fnv64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def png_info(path):
    """Validate PNG structure/CRC without decompressing or importing PIL."""
    total = path.stat().st_size
    with path.open("rb") as stream:
        if stream.read(8) != b"\x89PNG\r\n\x1a\n":
            raise DecodeError("invalid PNG signature")
        first, dimensions, ended, has_pixels = True, None, False, False
        while stream.tell() < total:
            header = stream.read(8)
            if len(header) != 8:
                raise DecodeError("truncated PNG chunk header")
            length, kind = struct.unpack(">I4s", header)
            if length > total - stream.tell() - 4:
                raise DecodeError("PNG chunk exceeds file")
            if first and (kind != b"IHDR" or length != 13):
                raise DecodeError("missing PNG IHDR")
            if not first and kind == b"IHDR":
                raise DecodeError("duplicate PNG IHDR")
            if kind == b"IDAT":
                has_pixels = True
            crc, remaining, prefix = binascii.crc32(kind), length, b""
            while remaining:
                block = stream.read(min(remaining, 1024 * 1024))
                if not block:
                    raise DecodeError("truncated PNG chunk")
                if first:
                    prefix += block
                crc = binascii.crc32(block, crc)
                remaining -= len(block)
            if struct.unpack(">I", stream.read(4))[0] != crc & 0xffffffff:
                raise DecodeError("PNG CRC mismatch")
            if first:
                width, height = struct.unpack_from(">II", prefix)
                if not width or not height:
                    raise DecodeError("PNG has zero dimensions")
                dimensions = {"width": width, "height": height}
                first = False
            if kind == b"IEND":
                if length or stream.tell() != total:
                    raise DecodeError("invalid/trailing PNG IEND")
                ended = True
                break
        if not ended or not has_pixels:
            raise DecodeError("missing PNG IEND/IDAT")
        return dimensions


def raw_batch(data, queue=None):
    commands, counts, cursor = [], {}, 0
    while cursor < len(data):
        if len(data) - cursor < 24:
            raise DecodeError(f"raw batch truncated header at {cursor}")
        header = struct.unpack_from("<6I", data, cursor)
        size = 24 + int(header[5]) * 4
        if size > len(data) - cursor:
            raise DecodeError(f"raw batch payload exceeds closed range at {cursor}")
        if queue is not None and header[2] != queue:
            raise DecodeError(f"raw batch foreign queue at {cursor}")
        kind = header[0]
        counts[str(kind)] = counts.get(str(kind), 0) + 1
        commands.append({"ordinal": len(commands), "offset": cursor, "header": list(header), "payloadWords": header[5], "preview": list(struct.unpack_from("<" + "I" * min(header[5], 40), data, cursor + 24))})
        cursor += size
        if len(commands) > 65536:
            raise DecodeError("raw batch exceeds native command bound")
    return {"commands": commands, "kindCounts": counts, "bytes": len(data)}


class Decoder:
    def __init__(self, session, output, native_schema=None, extract_shaders=False, extract_batches=False, max_event_bytes=128 * 1024 * 1024):
        self.session, self.output = Path(session).resolve(), Path(output).resolve()
        self.schema = load_json(self.session / "schema.json")
        if self.schema.get("endianness") != "little" or self.schema.get("eventHeader") != ["tag:u32", "size:u32", "draw:u32", "ordinal:u32", "sequence:u64"]:
            raise DecodeError("unsupported runtime event header; need little-endian <IIIIQ")
        native = load_json(native_schema) if native_schema else None
        if native and (native.get("endian") != "little" or native.get("pointerBytes") != self.schema.get("pointerBytes")):
            raise DecodeError("native and runtime ABI schemas have different endian/pointer size")
        self.native = native["structs"] if native else {}
        self.extract_shaders, self.extract_batches, self.max_event_bytes = extract_shaders, extract_batches, max_event_bytes
        self.summary = {"version": 1, "session": str(self.session), "output": str(self.output), "nativeSchema": str(native_schema) if native_schema else None, "frames": 0, "draws": 0, "events": 0, "shaders": 0, "batches": 0, "rawBatches": 0, "pngs": 0, "issues": []}
        self.summary.update(runtimeSchemaSha256=hashlib.sha256((self.session / "schema.json").read_bytes()).hexdigest(), nativeSchemaSha256=hashlib.sha256(Path(native_schema).read_bytes()).hexdigest() if native_schema else None, inputFiles=[], frameMetadata=[], nativeDrawFailures=0, previewFailures=0, previewSkipped=0, detailFailures=0)
        self.shaders, self.batches, self.raw_sequences, self.raw_meta = {}, {}, {}, {}
        self.output.mkdir(parents=True, exist_ok=True)

    def issue(self, frame, code, **details):
        self.summary["issues"].append(dict(frame=frame, code=code, **details))

    def native_context(self, payload, alias):
        if len(payload) != int(self.native.get(alias, {}).get("size", -1)):
            raise DecodeError("native " + alias + " event size differs from build ABI schema")
        return decode_struct(payload, alias, self.native)

    def images(self, folder, metadata):
        images = {}
        for image in metadata.get("images", []):
            name = image.get("name", "")
            if not name or Path(name).name != name or "\\" in name:
                self.issue(folder.name, "unsafe-image-name", name=name)
                continue
            record = dict(image)
            path = folder / name
            record["exists"] = path.is_file()
            if hresult(image.get("result", -1))["succeeded"]:
                if not path.is_file():
                    self.issue(folder.name, "missing-png", name=name)
                else:
                    try:
                        record["png"] = png_info(path)
                        self.summary["pngs"] += 1
                        if any(record["png"][key] != int(image.get(key, 0)) for key in ("width", "height")):
                            self.issue(folder.name, "png-dimensions-mismatch", name=name)
                    except (OSError, DecodeError, struct.error) as error:
                        self.issue(folder.name, "invalid-png", name=name, error=str(error))
            images[name] = record
        return images

    def emit_draw(self, folder, draw, images, metadata, json_output, csv_output):
        state, owner, camera, result = draw.get("actual", {}), draw.get("owner", {}), draw.get("camera", {}), draw.get("result", {})
        draw["frame"] = folder.name
        draw["scene"] = {key: metadata.get(key) for key in ("frame", "epoch", "eye", "tracked", "applied", "gameplay", "cinematic", "scope", "beforeCapture")}
        if not state:
            self.issue(folder.name, "missing-draw-state", draw=draw["draw"])
        if not result:
            self.issue(folder.name, "missing-draw-result", draw=draw["draw"])
        image = {}
        if result:
            for key in ("result", "preview", "detail"):
                result[key + "HRESULT"] = hresult(result[key])
            if not result["resultHRESULT"]["succeeded"]:
                self.summary["nativeDrawFailures"] += 1
            if not result["previewHRESULT"]["succeeded"]:
                self.summary["previewFailures"] += 1
            if result.get("detailIndex", 0xffffffff) != 0xffffffff and not result["detailHRESULT"]["succeeded"]:
                self.summary["detailFailures"] += 1
            sampled_out = (self.schema.get("previewStride", 1) > 1 and result["preview"] == 1
                           and result["page"] == 0xffffffff and result["tile"] == 0xffffffff)
            result["previewSkipped"] = sampled_out
            if sampled_out:
                self.summary["previewSkipped"] += 1
            if result["previewHRESULT"]["succeeded"] and not sampled_out:
                page, tile = int(result["page"]), int(result["tile"])
                columns, rows = self.schema["grid"]
                if not (0 <= page < self.schema["atlasPages"] and 0 <= tile < columns * rows):
                    self.issue(folder.name, "invalid-atlas-index", draw=draw["draw"], page=page, tile=tile)
                image = {"name": f"atlas-{page:02d}.png", "x": tile % columns * self.schema["tile"][0], "y": tile // columns * self.schema["tile"][1], "width": self.schema["tile"][0], "height": self.schema["tile"][1]}
                info = images.get(image["name"])
                image["available"] = bool(info and info.get("png"))
                if not image["available"]:
                    self.issue(folder.name, "missing-preview-image", draw=draw["draw"], name=image["name"])
                elif image["x"] + image["width"] > info["png"]["width"] or image["y"] + image["height"] > info["png"]["height"]:
                    self.issue(folder.name, "preview-outside-png", draw=draw["draw"])
            if result.get("detailIndex", 0xffffffff) != 0xffffffff and result["detailHRESULT"]["succeeded"]:
                name = f"draw-{draw['draw']:05d}.png"
                if not images.get(name, {}).get("png"):
                    self.issue(folder.name, "missing-detail-image", draw=draw["draw"], name=name)
                draw["detailImage"] = name
        draw["previewImage"] = image
        json_line(json_output, draw)
        desc, call = state.get("rtDesc", {}), state.get("call", {})
        csv_output.writerow({"frame": folder.name, "sceneFrame": metadata.get("frame"), "eye": metadata.get("eye"), "draw": draw["draw"], "api": call.get("kind"), "tick": state.get("tick"), "thread": state.get("thread"), "pass": owner.get("pass"), "entry": owner.get("entry"), "geometry": owner.get("geometry"), "roles": "|".join(owner.get("roleNames", [])), "cameraFailures": "|".join(camera.get("failureNames", [])), "viewMatches": camera.get("viewMatches"), "projectionMatches": camera.get("projectionMatches"), "skinLeaseMatchesEntry": owner.get("skinLeaseMatchesEntry"), "weaponBindingMatchesEntry": owner.get("weaponBindingMatchesEntry"), "support": owner.get("support"), "rt": state.get("rt"), "width": desc.get("Width"), "height": desc.get("Height"), "format": desc.get("formatName"), "vsHash": state.get("vsHash"), "psHash": state.get("psHash"), "nativeHRESULT": result.get("resultHRESULT", {}).get("hex"), "previewHRESULT": result.get("previewHRESULT", {}).get("hex"), "atlas": image.get("name"), "tileX": image.get("x"), "tileY": image.get("y"), "imageAvailable": image.get("available")})
        self.summary["draws"] += 1

    def shader(self, folder, event, payload):
        size = int(self.schema["shaderHeaderBytes"])
        header = decode_struct(payload, "ShaderHeader", self.schema["types"])
        length = int(header["length"])
        if size + length != len(payload):
            raise DecodeError("shader bytecode length does not match event size")
        bytecode = payload[size:]
        digest = hashlib.sha256(bytecode).hexdigest()
        if bytecode and hresult(header["result"])["succeeded"] and fnv64(bytecode) != header["hash"]:
            self.issue(folder.name, "shader-hash-mismatch", draw=event["draw"], pointer=header["pointer"])
        key = (header["kind"], header["hash"], digest)
        if key not in self.shaders:
            header.update(frame=folder.name, draw=event["draw"], sha256=digest)
            if self.extract_shaders and bytecode:
                path = self.output / "shaders" / f"{'vs' if header['kind'] == 0 else 'ps'}-{header['hash']:016x}-{digest[:12]}.bin"
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(bytecode)
                header["file"] = str(path.relative_to(self.output))
            self.shaders[key] = header

    def frame(self, folder, json_output, csv_output, batch_output):
        self.summary["frames"] += 1
        try:
            metadata = load_json(folder / "frame.json")
        except (OSError, ValueError) as error:
            self.issue(folder.name, "missing-or-invalid-frame-json", error=str(error)); metadata = {}
        images = self.images(folder, metadata)
        self.summary["frameMetadata"].append(dict(directory=folder.name, metadata=metadata))
        pending, completed, expected_ordinal = {}, set(), 0
        path = folder / "events.bin"
        try:
            total = path.stat().st_size
        except OSError as error:
            self.issue(folder.name, "missing-event-file", error=str(error)); return
        if metadata.get("eventBytes", total) != total:
            self.issue(folder.name, "event-file-size-mismatch", expected=metadata.get("eventBytes"), actual=total)
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            while stream.tell() < total:
                offset = stream.tell(); raw = stream.read(HEADER.size); digest.update(raw)
                if len(raw) != HEADER.size:
                    self.issue(folder.name, "truncated-event-header", offset=offset); break
                tag, size, number, ordinal, sequence = HEADER.unpack(raw)
                if size > self.max_event_bytes or size > total - stream.tell():
                    self.issue(folder.name, "invalid-event-size", offset=offset, size=size, remaining=total - stream.tell())
                    while True:
                        tail = stream.read(1024 * 1024)
                        if not tail: break
                        digest.update(tail)
                    break
                payload = stream.read(size); digest.update(payload); self.summary["events"] += 1
                if ordinal != expected_ordinal:
                    self.issue(folder.name, "event-ordinal-gap", offset=offset, expected=expected_ordinal, actual=ordinal)
                expected_ordinal = ordinal + 1
                event = dict(tag=tag, draw=number, ordinal=ordinal, sequence=sequence, offset=offset)
                try:
                    if tag == 3:
                        self.shader(folder, event, payload)
                    elif tag == 13:
                        context = self.native_context(payload, "B")
                        seq = int(context["sequence"])
                        if seq in self.batches:
                            self.issue(folder.name, "duplicate-batch-sequence", sequence=seq)
                        self.batches[seq] = {key: context.get(key) for key in ("queue", "commands", "before", "after", "complete", "reason")}
                        if sequence not in (0, seq):
                            self.issue(folder.name, "batch-sequence-mismatch", headerSequence=sequence, contextSequence=seq)
                        if not context.get("complete"):
                            self.issue(folder.name, "native-batch-incomplete", sequence=seq, reason=context.get("reason"))
                        json_line(batch_output, dict(frame=folder.name, event=event, context=context))
                        self.summary["batches"] += 1
                    elif tag == 14:
                        context = self.batches.get(sequence)
                        decoded = raw_batch(payload, context.get("queue") if context else None)
                        decoded.update(sequence=sequence, frame=folder.name, event=event, sha256=hashlib.sha256(payload).hexdigest(), contextAvailable=bool(context))
                        if sequence in self.raw_sequences:
                            self.issue(folder.name, "duplicate-raw-batch-sequence", sequence=sequence, sameBytes=self.raw_sequences[sequence] == decoded["sha256"])
                        if context and len(decoded["commands"]) != int(context["commands"]):
                            self.issue(folder.name, "raw-batch-count-mismatch", sequence=sequence)
                        self.raw_meta[sequence] = {"commands": len(decoded["commands"]), "bytes": len(payload), "queues": sorted({command["header"][2] for command in decoded["commands"]})}
                        if self.extract_batches:
                            raw_path = self.output / "batches" / f"sequence-{sequence:020d}-{decoded['sha256'][:12]}.bin"
                            raw_path.parent.mkdir(exist_ok=True); raw_path.write_bytes(payload); decoded["file"] = str(raw_path.relative_to(self.output))
                        self.raw_sequences[sequence] = decoded["sha256"]; json_line(batch_output, decoded); self.summary["rawBatches"] += 1
                    elif tag in (1, 2, 10, 11, 12):
                        if tag in (10, 11, 12) and number == 0:
                            self.native_context(payload, ALIASES[tag])
                            self.issue(folder.name, "context-without-actual-draw", tag=tag)
                            continue
                        if number in completed:
                            self.issue(folder.name, "late-event-for-completed-draw", draw=number, tag=tag)
                        draw = pending.setdefault(number, {"draw": number, "events": []})
                        draw["events"].append(event)
                        if tag == 1:
                            draw["actual"] = decode_actual(payload, self.schema)
                            if int(draw["actual"].get("index", number)) != number:
                                self.issue(folder.name, "draw-index-mismatch", draw=number)
                        elif tag == 2:
                            if len(payload) != int(self.schema["resultBytes"]):
                                raise DecodeError("draw result size mismatch")
                            draw["result"] = decode_struct(payload, "DrawResult", self.schema["types"])
                            self.emit_draw(folder, draw, images, metadata, json_output, csv_output); del pending[number]; completed.add(number)
                        else:
                            draw[LABELS[tag]] = self.native_context(payload, ALIASES[tag])
                    else:
                        self.issue(folder.name, "unknown-event-tag", tag=tag, offset=offset, size=size)
                except (DecodeError, KeyError, TypeError, struct.error, ValueError) as error:
                    self.issue(folder.name, "invalid-event-payload", tag=tag, draw=number, offset=offset, error=str(error))
        for draw in pending.values():
            self.emit_draw(folder, draw, images, metadata, json_output, csv_output)
        self.summary["inputFiles"].append(dict(path=str(path.relative_to(self.session)), bytes=total, sha256=digest.hexdigest()))
        if metadata.get("draws", len(completed)) != len(completed):
            self.issue(folder.name, "frame-draw-count-mismatch", declared=metadata.get("draws"), completed=len(completed))
        if metadata.get("droppedDraws") or metadata.get("droppedEvents"):
            self.issue(folder.name, "runtime-recording-drops", draws=metadata.get("droppedDraws", 0), events=metadata.get("droppedEvents", 0))

    def run(self):
        columns = "frame sceneFrame eye draw api tick thread pass entry geometry roles cameraFailures viewMatches projectionMatches skinLeaseMatchesEntry weaponBindingMatchesEntry support rt width height format vsHash psHash nativeHRESULT previewHRESULT atlas tileX tileY imageAvailable".split()
        with (self.output / "draws.jsonl").open("w", encoding="utf-8", newline="\n") as j, (self.output / "draws.csv").open("w", encoding="utf-8-sig", newline="") as c, (self.output / "batches.jsonl").open("w", encoding="utf-8", newline="\n") as b:
            writer = csv.DictWriter(c, columns); writer.writeheader()
            folders = sorted(path for path in self.session.glob("frame-*") if path.is_dir())
            if not folders:
                self.issue("session", "no-event-files")
            for folder in folders:
                self.frame(folder, j, writer, b)
        for sequence, context in self.batches.items():
            expected_bytes = int(context.get("after") or 0) - int(context.get("before") or 0)
            if expected_bytes > 0 and sequence not in self.raw_sequences:
                self.issue("session", "missing-raw-batch", sequence=sequence)
            raw = self.raw_meta.get(sequence)
            if raw and (raw["bytes"] != expected_bytes or raw["commands"] != context.get("commands") or any(queue != context.get("queue") for queue in raw["queues"])):
                self.issue("session", "raw-batch-context-mismatch", sequence=sequence)
        for sequence in self.raw_sequences:
            if sequence not in self.batches:
                self.issue("session", "orphan-raw-batch", sequence=sequence)
        self.summary["shaders"] = len(self.shaders)
        write_json(self.output / "shaders.json", list(self.shaders.values()))
        self.summary["complete"] = not self.summary["issues"]
        write_json(self.output / "summary.json", self.summary)
        return self.summary


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("session", type=Path)
    parser.add_argument("--native-schema", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--extract-shaders", action="store_true")
    parser.add_argument("--extract-batches", action="store_true")
    args = parser.parse_args(argv)
    native = args.native_schema
    if native is None:
        for directory in (args.session, *list(args.session.parents)[:4]):
            candidate = directory / "build-native-evidence-schema.json"
            if candidate.is_file():
                native = candidate; break
    try:
        summary = Decoder(args.session, args.output or args.session / "decoded", native, args.extract_shaders, args.extract_batches).run()
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("Decode failed:", error, file=sys.stderr); return 2
    print(json.dumps({key: summary[key] for key in ("output", "frames", "draws", "events", "shaders", "batches", "rawBatches", "pngs", "complete")}, ensure_ascii=False))
    print(f"Issues: {len(summary['issues'])}; details in summary.json")
    return 0 if summary["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
