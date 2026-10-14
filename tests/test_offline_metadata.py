"""Synthetic-file validation for the offline metadata declaration reader."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("offline_metadata", Path(__file__).parents[1] / "tools" / "inspect-il2cpp-metadata.py")
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def fixture(extended=False):
    names = reader.TABLES + (["unresolved_extended_table"] if extended else [])
    data = bytearray(8 + len(names) * 8)
    struct.pack_into("<II", data, 0, 0xFAB11BAF, 29)
    strings = bytearray(b"\0")
    ids = {}
    for name in ("Module.dll", "Core", "Actor", "Run", "value", "hp"):
        ids[name] = len(strings)
        strings.extend(name.encode() + b"\0")
    actor = (ids["Actor"], ids["Core"], 123, -1, -1, 123, -1, 1,
             0, 0, -1, -1, -1, -1, -1, -1,
             1, 0, 1, 0, 0, 0, 0, 0, 0, 0x02000001)
    type_data = reader.TYPE.pack(*actor)
    if extended:
        type_data = reader.TYPE_EXTENDED.pack(*(actor[:16] + (0x12345678,) + actor[16:]))
    tables = {
        "string": bytes(strings), "typeDefinitions": type_data,
        "methods": reader.METHOD.pack(ids["Run"], 0, 123, 0, -1, 0x06000001, 0x10, 0, 65535, 1),
        "parameters": reader.PARAMETER.pack(ids["value"], 0x08000001, 123),
        "fields": reader.FIELD.pack(ids["hp"], 123, 0x04000001),
        "images": reader.IMAGE.pack(ids["Module.dll"], 0, 0, 1, -1, 0, -1, 1, 0, 0),
    }
    for i, name in enumerate(names):
        body = tables.get(name, b"")
        struct.pack_into("<II", data, 8 + i * 8, len(data), len(body))
        data.extend(body)
    return data


def pe_fixture():
    data = bytearray(0x800)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 2)
    struct.pack_into("<H", data, 0x94, 0xF0)
    struct.pack_into("<H", data, 0x98, 0x20B)
    base = 0x180000000
    struct.pack_into("<Q", data, 0x98 + 24, base)
    for i, (va, raw, size, flags) in enumerate(((0x1000, 0x400, 0x100, 0x60000020), (0x2000, 0x500, 0x300, 0x40000040))):
        row = 0x188 + i * 40
        struct.pack_into("<IIII", data, row + 8, size, va, size, raw)
        struct.pack_into("<I", data, row + 36, flags)
    data[0x520:0x52B] = b"Module.dll\0"
    struct.pack_into("<QQQ", data, 0x550, base + 0x2020, 1, base + 0x2090)
    struct.pack_into("<Q", data, 0x590, base + 0x1010)
    return data


class OfflineMetadataTest(unittest.TestCase):
    def test_standard_declarations(self):
        metadata = reader.Metadata(fixture())
        selected = metadata.select(r"^Core.Actor$", "^Run$", "Module")
        self.assertEqual(len(selected), 1)
        self.assertEqual(selected[0]["fields"][0]["name"], "hp")
        self.assertEqual(selected[0]["methods"][0]["parameters"], [{"name": "value", "type": "Core.Actor", "type_index": 123}])
        self.assertTrue(selected[0]["methods"][0]["static"])
        self.assertEqual(metadata.select("Other"), [])

    def test_explicit_extended_layout(self):
        data = fixture(True)
        metadata = reader.Metadata(data, "endfield29-extended")
        self.assertEqual(metadata.select("Actor")[0]["methods"][0]["name"], "Run")
        with self.assertRaises(reader.MetadataError):
            reader.Metadata(data)

    def test_truncated_and_version(self):
        with self.assertRaises(reader.MetadataError):
            reader.Metadata(b"\0" * 8)
        data = fixture()
        struct.pack_into("<I", data, 4, 30)
        with self.assertRaises(reader.MetadataError):
            reader.Metadata(data)

    def test_out_of_file_table(self):
        data = fixture()
        struct.pack_into("<II", data, 8 + reader.TABLES.index("typeDefinitions") * 8, len(data) + 1, reader.TYPE.size)
        with self.assertRaises(reader.MetadataError):
            reader.Metadata(data)

    def test_method_owner_and_parameter_bounds(self):
        for relative, schema, value in ((4, "<i", 7), (30, "<H", 2)):
            data = fixture()
            offset = struct.unpack_from("<I", data, 8 + reader.TABLES.index("methods") * 8)[0]
            struct.pack_into(schema, data, offset + relative, value)
            with self.assertRaises(reader.MetadataError):
                reader.Metadata(data)

    def test_invalid_symbol_index(self):
        data = fixture()
        offset = struct.unpack_from("<I", data, 8 + reader.TABLES.index("typeDefinitions") * 8)[0]
        struct.pack_into("<I", data, offset, 0xFFFFFFFF)
        with self.assertRaises(reader.MetadataError):
            reader.Metadata(data)

    def test_unresolved_types_are_explicit(self):
        metadata = reader.Metadata(fixture())
        self.assertEqual(metadata.type_name(999), "<unresolved-type:999>")

    def test_raw_pe_method_resolution(self):
        metadata = reader.Metadata(fixture())
        image = reader.PEImage(pe_fixture())
        self.assertEqual(metadata.method_count("Module.dll"), 1)
        self.assertEqual(image.method_rva("Module.dll", 1, 0x06000001), 0x1010)
        with self.assertRaises(reader.MetadataError):
            image.method_rva("Module.dll", 2, 0x06000001)
        with self.assertRaises(reader.MetadataError):
            image.method_rva("Module.dll", 1, 0x06000002)

    def test_pe_rejects_data_as_code_and_wrong_count(self):
        data = pe_fixture()
        struct.pack_into("<Q", data, 0x590, 0x180002010)
        with self.assertRaises(reader.MetadataError):
            reader.PEImage(data).method_rva("Module.dll", 1, 0x06000001)
        with self.assertRaises(reader.MetadataError):
            reader.PEImage(pe_fixture()).method_rva("Module.dll", 2, 0x06000001)

    def test_ambiguous_pe_module_fails_closed(self):
        data = pe_fixture()
        struct.pack_into("<Q", data, 0x5B0, 0x180001020)
        struct.pack_into("<QQQ", data, 0x5D0, 0x180002020, 1, 0x1800020B0)
        with self.assertRaises(reader.MetadataError):
            reader.PEImage(data).method_rva("Module.dll", 1, 0x06000001)

    def test_invalid_pe_range(self):
        data = pe_fixture()
        struct.pack_into("<I", data, 0x188 + 16, len(data) * 2)
        with self.assertRaises(reader.MetadataError):
            reader.PEImage(data)


if __name__ == "__main__":
    unittest.main()
