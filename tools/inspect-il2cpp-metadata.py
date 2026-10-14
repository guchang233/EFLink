"""Read IL2CPP v29 declarations from a metadata FILE without loading game code.

Type/method/field declarations and optional raw PE method RVAs are inspected.
This never initializes IL2CPP, attaches to a process, or certifies an ABI.
Layout reference: Perfare/Il2CppDumper, commit
4741d46ba9cd6159c5d853eb9d6fc48b4bfa2b1a, MetadataClass.cs (MIT).
See tools/third_party/Il2CppDumper-NOTICE.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


class MetadataError(ValueError):
    pass


class PEImage:
    """Raw-file PE32+ reader. Never maps or executes the image."""
    def __init__(self, data: bytes):
        self.data = data
        self.modules = {}
        if len(data) < 64 or data[:2] != b"MZ":
            raise MetadataError("invalid PE DOS header")
        header = struct.unpack_from("<I", data, 60)[0]
        if header > len(data) - 24 or data[header:header + 4] != b"PE\0\0":
            raise MetadataError("invalid PE header")
        machine, count = struct.unpack_from("<HH", data, header + 4)
        optional_size = struct.unpack_from("<H", data, header + 20)[0]
        optional = header + 24
        if machine != 0x8664 or not 0 < count < 96 or optional_size < 112 or optional + optional_size + count * 40 > len(data):
            raise MetadataError("unsupported PE layout")
        if struct.unpack_from("<H", data, optional)[0] != 0x20B:
            raise MetadataError("only PE32+ is supported")
        self.base = struct.unpack_from("<Q", data, optional + 24)[0]
        self.sections = []
        for i in range(count):
            row = optional + optional_size + i * 40
            virtual_size, virtual, size, offset = struct.unpack_from("<IIII", data, row + 8)
            flags = struct.unpack_from("<I", data, row + 36)[0]
            if size and (offset > len(data) or size > len(data) - offset):
                raise MetadataError("PE section exceeds file")
            self.sections.append((self.base + virtual, offset, size, bool(flags & 0x20000000)))

    def offset(self, address: int, size: int = 1, executable=None):
        matches = [(raw + address - va) for va, raw, count, code in self.sections
                   if address >= va and 0 <= size <= count and address - va <= count - size and
                   (executable is None or code == executable)]
        if len(matches) != 1:
            raise MetadataError("unmapped or ambiguous PE address")
        return matches[0]

    def method_table(self, assembly: str, expected_count: int):
        if assembly in self.modules:
            if len(self.modules[assembly]) != expected_count:
                raise MetadataError("cached code generation module count differs")
            return self.modules[assembly]
        if not 0 < expected_count <= 2_000_000:
            raise MetadataError("invalid code generation module method count")
        needle = assembly.encode("utf-8") + b"\0"
        names = []
        for va, raw, size, code in self.sections:
            if code:
                continue
            end = raw + size
            at = self.data.find(needle, raw, end)
            while at >= 0:
                names.append(va + at - raw)
                at = self.data.find(needle, at + 1, end)
        candidates = {}
        for name in names:
            pointer = struct.pack("<Q", name)
            for va, raw, size, code in self.sections:
                if code:
                    continue
                end = raw + size
                at = self.data.find(pointer, raw, end)
                while at >= 0:
                    if (va + at - raw) % 8 == 0 and at + 24 <= end:
                        count, table = struct.unpack_from("<QQ", self.data, at + 8)
                        if count == expected_count:
                            try:
                                start = self.offset(table, count * 8, executable=False)
                                values = struct.unpack_from(f"<{count}Q", self.data, start)
                                for value in values:
                                    if value:
                                        self.offset(value, executable=True)
                                candidates[table] = values
                            except MetadataError:
                                pass
                    at = self.data.find(pointer, at + 1, end)
        if len(candidates) != 1:
            raise MetadataError(f"missing or ambiguous code generation module: {assembly}")
        values = next(iter(candidates.values()))
        self.modules[assembly] = values
        return values

    def method_rva(self, assembly: str, expected_count: int, token: int):
        if token >> 24 != 6 or not 0 < (token & 0xFFFFFF) <= expected_count:
            raise MetadataError("invalid method definition token")
        value = self.method_table(assembly, expected_count)[(token & 0xFFFFFF) - 1]
        return value - self.base if value else None


TABLES = (
    "stringLiteral stringLiteralData string events properties methods "
    "parameterDefaultValues fieldDefaultValues fieldAndParameterDefaultValueData "
    "fieldMarshaledSizes parameters fields genericParameters genericParameterConstraints "
    "genericContainers nestedTypes interfaces vtableMethods interfaceOffsets "
    "typeDefinitions images assemblies fieldRefs referencedAssemblies attributeData "
    "attributeDataRange unresolvedVirtualCallParameterTypes "
    "unresolvedVirtualCallParameterRanges windowsRuntimeTypeNames windowsRuntimeStrings "
    "exportedTypeDefinitions"
).split()
TYPE = struct.Struct("<IIiiiiiIiiiiiiiiHHHHHHHHII")
TYPE_EXTENDED = struct.Struct("<IIiiiiiIiiiiiiiiiHHHHHHHHII")
METHOD = struct.Struct("<IiiiiIHHHH")
PARAMETER = struct.Struct("<IIi")
FIELD = struct.Struct("<IiI")
IMAGE = struct.Struct("<IiiIiIiIiI")


class Metadata:
    def __init__(self, data: bytes, layout: str = "standard29"):
        self.data = data
        if layout not in {"standard29", "endfield29-extended"}:
            raise MetadataError("unknown metadata layout")
        self.layout = layout
        names = TABLES + (["unresolved_extended_table"] if layout == "endfield29-extended" else [])
        minimum = 8 + len(names) * 8
        if len(data) < minimum:
            raise MetadataError("truncated metadata header")
        magic, self.version = struct.unpack_from("<II", data)
        if magic != 0xFAB11BAF or self.version != 29:
            raise MetadataError("only metadata version 29 is supported")
        self.tables = {}
        for i, name in enumerate(names):
            offset, size = struct.unpack_from("<II", data, 8 + i * 8)
            if size and (offset < minimum or offset > len(data) or size > len(data) - offset):
                raise MetadataError(f"out-of-file table: {name}")
            self.tables[name] = (offset, size)
        if layout == "endfield29-extended":
            if self.tables["stringLiteral"][0] != minimum:
                raise MetadataError("extended metadata header differs")
            # This client's declarations contain an additional word at byte 64.
            # Preserve its unknown semantics by excluding it from the normalized
            # declaration layout; it is never used as a pointer, size or ABI.
            self.types = [row[:16] + row[17:] for row in self.records("typeDefinitions", TYPE_EXTENDED)]
        else:
            self.types = self.records("typeDefinitions", TYPE)
        self.methods = self.records("methods", METHOD)
        self.parameters = self.records("parameters", PARAMETER)
        self.fields = self.records("fields", FIELD)
        self.images = self.records("images", IMAGE)
        self.type_names = {}
        self.assembly = {}
        for i, value in enumerate(self.types):
            name, namespace = self.string(value[0]), self.string(value[1])
            if not name:
                raise MetadataError("empty type name")
            if value[25] >> 24 != 2 or not value[25] & 0xFFFFFF:
                raise MetadataError("invalid type definition token")
            full = f"{namespace}.{name}" if namespace else name
            if value[2] < 0 or value[2] in self.type_names:
                raise MetadataError("ambiguous by-value type index")
            self.type_names[value[2]] = full
            self.range(value[8], value[18], len(self.fields), "type fields")
            self.range(value[9], value[16], len(self.methods), "type methods")
        for image in self.images:
            start, count = image[2], image[3]
            self.range(start, count, len(self.types), "image types")
            name = self.string(image[0])
            for i in range(start, start + count):
                if i in self.assembly:
                    raise MetadataError("overlapping image type ranges")
                self.assembly[i] = name
        for i, method in enumerate(self.methods):
            if method[5] >> 24 != 6 or not method[5] & 0xFFFFFF:
                raise MetadataError("invalid method definition token")
            if not 0 <= method[1] < len(self.types):
                raise MetadataError("invalid method declaring type")
            self.range(method[3], method[9], len(self.parameters), "method parameters")
            owner = self.types[method[1]]
            if not owner[9] <= i < owner[9] + owner[16]:
                raise MetadataError("method outside its declaring type")

    @staticmethod
    def range(start: int, count: int, length: int, label: str):
        if count < 0 or (count and (start < 0 or start > length or count > length - start)):
            raise MetadataError(f"invalid {label} range")

    def records(self, name: str, schema: struct.Struct):
        offset, size = self.tables[name]
        if size % schema.size:
            raise MetadataError(f"unsupported {name} layout size")
        return list(schema.iter_unpack(self.data[offset:offset + size]))

    def string(self, index: int) -> str:
        offset, size = self.tables["string"]
        if not 0 <= index < size:
            raise MetadataError("string index outside metadata string table")
        end = self.data.find(b"\0", offset + index, offset + min(size, index + 4096))
        if end < 0:
            raise MetadataError("unterminated or oversized metadata symbol")
        try:
            return self.data[offset + index:end].decode("utf-8")
        except UnicodeDecodeError as error:
            raise MetadataError("invalid metadata symbol encoding") from error

    def type_name(self, index: int) -> str:
        return self.type_names.get(index, f"<unresolved-type:{index}>")

    def method_count(self, assembly: str) -> int:
        tokens = [row[5] & 0xFFFFFF for row in self.methods if self.assembly.get(row[1]) == assembly]
        if not tokens or len(tokens) != max(tokens) or len(set(tokens)) != len(tokens):
            raise MetadataError("non-contiguous assembly method tokens")
        return len(tokens)

    def select(self, classes: str, methods: str = ".*", assembly: str = ".*"):
        class_pattern, method_pattern, assembly_pattern = map(re.compile, (classes, methods, assembly))
        result = []
        for i, value in enumerate(self.types):
            name, namespace = self.string(value[0]), self.string(value[1])
            full = f"{namespace}.{name}" if namespace else name
            image = self.assembly.get(i, "<unresolved-image>")
            if not class_pattern.search(full) or not assembly_pattern.search(image):
                continue
            rows = []
            for index in range(value[9], value[9] + value[16]):
                method = self.methods[index]
                method_name = self.string(method[0])
                if not method_pattern.search(method_name):
                    continue
                params = self.parameters[method[3]:method[3] + method[9]] if method[9] else []
                rows.append({"index": index, "token": method[5], "name": method_name,
                             "return": self.type_name(method[2]), "static": bool(method[6] & 0x10),
                             "parameters": [{"name": self.string(p[0]), "type": self.type_name(p[2]),
                                             "type_index": p[2]} for p in params]})
            fields = [{"name": self.string(f[0]), "type": self.type_name(f[1]), "type_index": f[1]}
                      for f in self.fields[value[8]:value[8] + value[18]]] if value[18] else []
            result.append({"index": i, "assembly": image, "class": full, "methods": rows, "fields": fields})
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--layout", choices=("standard29", "endfield29-extended"), default="standard29")
    parser.add_argument("--class", dest="classes", required=True, help="type-name regular expression")
    parser.add_argument("--method", default=".*", help="method-name regular expression")
    parser.add_argument("--assembly", default=".*", help="assembly-name regular expression")
    parser.add_argument("--binary", type=Path, help="optional raw PE file; parsed as bytes and never loaded")
    parser.add_argument("--output", type=Path, help="local JSON output; no native code bytes are included")
    args = parser.parse_args()
    data = args.metadata.read_bytes()
    metadata = Metadata(data, args.layout)
    report = {"metadata_version": metadata.version, "declaration_layout": metadata.layout,
              "metadata_sha256": hashlib.sha256(data).hexdigest(),
              "counts": {"types": len(metadata.types), "methods": len(metadata.methods), "images": len(metadata.images)},
              "offline_only": True, "native_addresses_resolved": False, "abi_verified": False,
              "types": metadata.select(args.classes, args.method, args.assembly)}
    if args.binary:
        binary = args.binary.read_bytes()
        pe = PEImage(binary)
        counts = {}
        for row in report["types"]:
            assembly = row["assembly"]
            if assembly not in counts:
                counts[assembly] = metadata.method_count(assembly)
            for method in row["methods"]:
                method["rva"] = pe.method_rva(assembly, counts[assembly], method["token"])
        report["binary_sha256"] = hashlib.sha256(binary).hexdigest()
        report["native_addresses_resolved"] = True
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
        print(json.dumps({"output": str(args.output), "counts": report["counts"], "selected_types": len(report["types"])}))
    else:
        print(text)


if __name__ == "__main__":
    main()
