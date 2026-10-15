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


def native_fixture():
    data = pe_fixture()
    data.extend(b"\0" * (0x2000 - len(data)))
    # Enlarge the raw non-executable section; all pointers remain file VAs.
    struct.pack_into("<I", data, 0x188 + 40 + 8, 0x1B00)
    struct.pack_into("<I", data, 0x188 + 40 + 16, 0x1B00)
    address = lambda raw: 0x180002000 + raw - 0x500
    types = [address(0xB10)] * 124
    types[123] = address(0xB00)
    struct.pack_into("<124Q", data, 0x700, *types)
    struct.pack_into("<QI", data, 0xB00, 0, 0x00120000)  # Core.Actor
    struct.pack_into("<QI", data, 0xB10, 0, 0x00080000)  # System.Int32
    struct.pack_into("<Q", data, 0xB40, address(0xB60))
    struct.pack_into("<Q", data, 0xB50, address(0xB70))
    struct.pack_into("<i", data, 0xB60, 24)
    struct.pack_into("<4I", data, 0xB70, 32, 32, 0, 0)
    struct.pack_into("<16Q", data, 0x600, 0, 0, 0, 0, 0, 0, 124, address(0x700),
                     0, 0, 1, address(0xB40), 1, address(0xB50), 0, 0)
    return data


def replace_table(data, name, body):
    old = reader.Metadata(data)
    result = bytearray(data[:8 + len(reader.TABLES) * 8])
    for i, table in enumerate(reader.TABLES):
        offset, size = old.tables[table]
        value = body if table == name else data[offset:offset + size]
        struct.pack_into("<II", result, 8 + i * 8, len(result), len(value))
        result.extend(value)
    return result


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

    def test_abstract_virtual_declaration_is_not_a_direct_call_target(self):
        data = fixture()
        offset = reader.Metadata(data).tables["methods"][0]
        struct.pack_into("<H", data, offset + 24, 0x440)
        method = reader.Metadata(data).select("Actor")[0]["methods"][0]
        self.assertTrue(method["virtual"])
        self.assertTrue(method["abstract"])
        self.assertFalse(method["static"])

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

    def test_native_registration_signatures_and_offsets(self):
        metadata = reader.Metadata(fixture())
        native = reader.NativeTypes(metadata, reader.PEImage(native_fixture()))
        self.assertEqual(native.registration_rva, 0x2100)
        self.assertEqual(native.name(0), "System.Int32")
        self.assertEqual(native.name(123), "Core.Actor")
        self.assertEqual(native.field_offsets(0), (24,))
        field = metadata.select("Actor", native=native)[0]["fields"][0]
        self.assertEqual(field["offset"], 24)
        self.assertFalse(field["is_static"])
        with self.assertRaises(reader.MetadataError):
            native.name(124)

    def test_native_definition_identity_must_match(self):
        for raw, schema, value in ((0xB00, "<Q", 9), (0xB08, "<I", 0x00110000),
                                   (0xB08, "<I", 0x20120000), (0xB40, "<Q", 0x180001010),
                                   (0xB50, "<Q", 0)):
            data = native_fixture()
            struct.pack_into(schema, data, raw, value)
            with self.assertRaises(reader.MetadataError):
                reader.NativeTypes(reader.Metadata(fixture()), reader.PEImage(data))

    def test_native_registration_ambiguity_is_rejected(self):
        data = native_fixture()
        data[0xC00:0xC80] = data[0x600:0x680]
        with self.assertRaises(reader.MetadataError):
            reader.NativeTypes(reader.Metadata(fixture()), reader.PEImage(data))

    def test_native_references_arrays_and_static_fields(self):
        data = native_fixture()
        address = lambda raw: 0x180002000 + raw - 0x500
        # Field and parameter use a separate byref/static type node, as in IL2CPP.
        struct.pack_into("<Q", data, 0x708, address(0xC00))
        struct.pack_into("<QI", data, 0xC00, 0, 0x20080010)
        native = reader.NativeTypes(reader.Metadata(fixture()), reader.PEImage(data))
        self.assertEqual(native.name(1), "System.Int32&")
        self.assertEqual(native.node(1)[1] & 0x10, 0x10)
        for kind, suffix in ((15, "*"), (29, "[]"), (16, "&")):
            struct.pack_into("<QI", data, 0xC00, address(0xB10), kind << 16)
            native = reader.NativeTypes(reader.Metadata(fixture()), reader.PEImage(data))
            self.assertEqual(native.name(1), "System.Int32" + suffix)
        struct.pack_into("<QI", data, 0xC00, address(0xC20), 20 << 16)
        struct.pack_into("<QB", data, 0xC20, address(0xB10), 3)
        native = reader.NativeTypes(reader.Metadata(fixture()), reader.PEImage(data))
        self.assertEqual(native.name(1), "System.Int32[,,]")

    def test_native_generic_types_and_cycles(self):
        data = native_fixture()
        address = lambda raw: 0x180002000 + raw - 0x500
        struct.pack_into("<Q", data, 0x708, address(0xC00))
        struct.pack_into("<QI", data, 0xC00, address(0xC20), 21 << 16)
        struct.pack_into("<4Q", data, 0xC20, address(0xB00), address(0xC50), 0, 0)
        struct.pack_into("<2Q", data, 0xC50, 1, address(0xC70))
        struct.pack_into("<Q", data, 0xC70, address(0xB10))
        native = reader.NativeTypes(reader.Metadata(fixture()), reader.PEImage(data))
        self.assertEqual(native.name(1), "Core.Actor<System.Int32>")
        struct.pack_into("<Q", data, 0xC70, address(0xC00))
        native = reader.NativeTypes(reader.Metadata(fixture()), reader.PEImage(data))
        with self.assertRaisesRegex(reader.MetadataError, "cyclic"):
            native.name(1)
        struct.pack_into("<Q", data, 0xC50, 65)
        native = reader.NativeTypes(reader.Metadata(fixture()), reader.PEImage(data))
        with self.assertRaisesRegex(reader.MetadataError, "argument count"):
            native.name(1)

    def test_native_unknown_kinds_and_unmapped_children(self):
        data = native_fixture()
        address = lambda raw: 0x180002000 + raw - 0x500
        struct.pack_into("<Q", data, 0x708, address(0xC00))
        struct.pack_into("<QI", data, 0xC00, 0, 0x00550000)
        native = reader.NativeTypes(reader.Metadata(fixture()), reader.PEImage(data))
        self.assertEqual(native.name(1), "<unresolved-native-kind:85>")
        struct.pack_into("<QI", data, 0xC00, 0x180001000, 29 << 16)
        native = reader.NativeTypes(reader.Metadata(fixture()), reader.PEImage(data))
        with self.assertRaises(reader.MetadataError):
            native.name(1)

    def test_generic_parameters_and_raw_sentinel_offsets(self):
        metadata_data = fixture()
        metadata = reader.Metadata(metadata_data)
        # Existing symbol 'value' is also a valid generic parameter name.
        symbol = metadata.parameters[0][0]
        metadata_data = replace_table(metadata_data, "genericParameters", reader.GENERIC_PARAMETER.pack(0, symbol, -1, 0, 0, 0))
        metadata = reader.Metadata(metadata_data)
        data = native_fixture()
        address = lambda raw: 0x180002000 + raw - 0x500
        struct.pack_into("<Q", data, 0x708, address(0xC00))
        struct.pack_into("<QI", data, 0xC00, 0, 19 << 16)
        struct.pack_into("<i", data, 0xB60, -1)
        native = reader.NativeTypes(metadata, reader.PEImage(data))
        self.assertEqual(native.name(1), "value")
        self.assertEqual(native.field_offsets(0), (-1,))
        struct.pack_into("<QI", data, 0xC00, 1, 30 << 16)
        native = reader.NativeTypes(metadata, reader.PEImage(data))
        with self.assertRaisesRegex(reader.MetadataError, "generic parameter"):
            native.name(1)

    def test_nested_type_names_and_declaring_cycles(self):
        data = fixture()
        metadata = reader.Metadata(data)
        actor = metadata.types[0]
        nested = actor[:2] + (124, 123, 123) + actor[5:8] + (-1, -1) + actor[10:16] + (0, 0, 0) + actor[19:25] + (0x02000002,)
        data = replace_table(data, "typeDefinitions", reader.TYPE.pack(*actor) + reader.TYPE.pack(*nested))
        image = reader.IMAGE.pack(metadata.images[0][0], 0, 0, 2, -1, 0, -1, 1, 0, 0)
        data = replace_table(data, "images", image)
        metadata = reader.Metadata(data)
        self.assertEqual(metadata.type_name(124), "Core.Actor/Actor")
        self.assertEqual(len(metadata.select("^Core.Actor/Actor$")), 1)
        bad = fixture()
        offset = reader.Metadata(bad).tables["typeDefinitions"][0]
        struct.pack_into("<i", bad, offset + 12, 123)
        with self.assertRaisesRegex(reader.MetadataError, "cyclic"):
            reader.Metadata(bad)


if __name__ == "__main__":
    unittest.main()
