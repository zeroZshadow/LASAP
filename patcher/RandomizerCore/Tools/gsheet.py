"""Grezzo datasheets (.gsheet) in pure Python: what LASR used oead.gsheet for, with the same names and types.

The binary layout and the order in which the writer lays out objects follow oead's gsheet.cpp (leoetlino,
GPL-2.0-or-later), so a parsed and rewritten sheet is byte for byte what oead writes. This file is under the same
license.

Values are plain Python: a struct is a Struct (dict), an array one of the list types below, a string str, a missing
nullable None. Each array type is its own list subclass, as LASR's oead_tools tells them apart by exact type.
Like oead's, structs and arrays copy what is stored in them, and hand out what they hold by reference: LASR builds
rows by changing one dict between appends.
"""
import enum
import struct as _struct
from typing import Dict, List, Optional, Tuple

MAGIC = b"gsht"
HEADER_SIZE = 0x30
FIELD_SIZE = 0x30
PARENT_MARKER = 0xDEADBEEFDEADBEEF
PLACEHOLDER = 0xFFFFFFFFFFFFFFFF


def _own(value):
    """A deep copy of a value being stored, as oead converts it into its own containers."""
    if isinstance(value, dict):
        return Struct(value)
    if isinstance(value, _Array):
        return type(value)(value)
    if isinstance(value, list):
        return [_own(item) for item in value]
    return value


class Struct(dict):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__()
        for key, value in dict(*args, **kwargs).items():
            super().__setitem__(key, _own(value))

    def __setitem__(self, key, value) -> None:
        super().__setitem__(key, _own(value))

    def update(self, *args, **kwargs) -> None:
        for key, value in dict(*args, **kwargs).items():
            self[key] = value

    @classmethod
    def _adopt(cls, items: dict) -> "Struct":
        """Wraps freshly parsed values without copying them."""
        struct = cls()
        dict.update(struct, items)
        return struct


class _Array(list):
    def __init__(self, items=()) -> None:
        super().__init__(_own(item) for item in items)

    def append(self, item) -> None:
        super().append(_own(item))

    def insert(self, index, item) -> None:
        super().insert(index, _own(item))

    def extend(self, items) -> None:
        super().extend(_own(item) for item in items)

    def __setitem__(self, index, item) -> None:
        if isinstance(index, slice):
            super().__setitem__(index, [_own(i) for i in item])
        else:
            super().__setitem__(index, _own(item))

    @classmethod
    def _adopt(cls, items) -> "_Array":
        array = cls()
        list.extend(array, items)
        return array


class StructArray(_Array):
    pass


class BoolArray(_Array):
    pass


class IntArray(_Array):
    pass


class FloatArray(_Array):
    pass


class StringArray(_Array):
    pass


class Field:
    class Type(enum.IntEnum):
        Struct = 0
        Bool = 1
        Int = 2
        Float = 3
        String = 4

    class Flag(enum.IntFlag):
        IsNullable = 1 << 0
        IsArray = 1 << 1
        IsKey = 1 << 2
        Unknown3 = 1 << 3
        IsEnum = 1 << 4
        Unknown5 = 1 << 5

    def __init__(self) -> None:
        self.name = ""
        self.type_name = ""
        self.type = Field.Type.Struct
        self.x11 = 0
        self.flags = Field.Flag(0)
        self.offset_in_value = 0
        self.inline_size = 0
        self.data_size = 0
        self.fields: List[Field] = []


_ARRAY_TYPES = {Field.Type.Struct: StructArray, Field.Type.Bool: BoolArray, Field.Type.Int: IntArray,
                Field.Type.Float: FloatArray, Field.Type.String: StringArray}


class InvalidDataError(ValueError):
    pass


# Reading

class _Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data

    def u8(self, offset: int) -> int:
        return self.data[offset]

    def u16(self, offset: int) -> int:
        return _struct.unpack_from("<H", self.data, offset)[0]

    def u32(self, offset: int) -> int:
        return _struct.unpack_from("<I", self.data, offset)[0]

    def u64(self, offset: int) -> int:
        return _struct.unpack_from("<Q", self.data, offset)[0]

    def cstr(self, offset: int) -> str:
        end = self.data.index(b"\0", offset)
        return self.data[offset:end].decode("utf-8")

    def field(self, offset: int) -> Field:
        field = Field()
        field.name = self.cstr(self.u64(offset))
        field.type_name = self.cstr(self.u64(offset + 0x8))
        field.type = Field.Type(self.u8(offset + 0x10))
        field.x11 = self.u8(offset + 0x11)
        field.flags = Field.Flag(self.u16(offset + 0x12))
        field.offset_in_value = self.u16(offset + 0x14)
        field.inline_size = self.u16(offset + 0x16)
        field.data_size = self.u16(offset + 0x18)
        count = self.u16(offset + 0x1A)
        fields_offset = self.u64(offset + 0x20)
        if count and not fields_offset:
            raise InvalidDataError("Missing sub-fields")
        field.fields = [self.field(fields_offset + i * FIELD_SIZE) for i in range(count)]
        return field

    def value(self, offset: int, field: Field, ignore_array: bool = False, ignore_nullable: bool = False):
        flags = field.flags
        if flags & Field.Flag.IsArray and not ignore_array:
            data, size = self.u64(offset), self.u32(offset + 0x8)
            return _ARRAY_TYPES[field.type]._adopt(self.value(data + i * field.data_size, field, True,
                                                              ignore_nullable) for i in range(size))
        if field.type == Field.Type.String:
            data = self.u64(offset)
            return self.cstr(data) if data else ""
        if flags & Field.Flag.IsNullable and not ignore_nullable:
            data = self.u64(offset)
            return self.value(data, field, ignore_array, True) if data else None
        if field.type == Field.Type.Struct:
            return Struct._adopt({sub.name: self.value(offset + sub.offset_in_value, sub) for sub in field.fields})
        if field.type == Field.Type.Bool:
            return bool(self.u8(offset))
        if field.type == Field.Type.Int:
            return _struct.unpack_from("<i", self.data, offset)[0]
        return _struct.unpack_from("<f", self.data, offset)[0]


def parse(data: bytes) -> "Sheet":
    data = bytes(data)
    reader = _Reader(data)
    if len(data) < HEADER_SIZE or data[:4] != MAGIC:
        raise InvalidDataError("Invalid magic")
    if reader.u32(0x4) != 1:
        raise InvalidDataError("Invalid version (expected 1)")
    if reader.u8(0xC) != 1 or reader.u8(0xD) != 8:
        raise InvalidDataError("Invalid bool or pointer size")
    sheet = Sheet()
    sheet.hash = reader.u32(0x8)
    sheet.alignment = reader.u8(0xE)
    sheet.name = reader.cstr(reader.u64(0x10))
    num_root_fields = reader.u32(0x18)
    sheet.root_fields = [reader.field(HEADER_SIZE + i * FIELD_SIZE) for i in range(num_root_fields)]
    values, num_values, value_size = reader.u64(0x20), reader.u32(0x28), reader.u32(0x2C)
    sheet.values = StructArray._adopt(
        Struct._adopt({field.name: reader.value(values + i * value_size + field.offset_in_value, field)
                       for field in sheet.root_fields})
        for i in range(num_values))
    return sheet


# Writing

def _fresh(value):
    """A copy whose containers are all distinct objects: the writer finds objects by identity, like oead's."""
    if isinstance(value, dict):
        return {k: _fresh(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_fresh(v) for v in value]
    return value


class _Writer:
    def __init__(self) -> None:
        self.buf = bytearray()
        self.pos = 0
        self.field_strings: List[Tuple[int, str]] = []
        self.pointers: Dict[tuple, int] = {}  # object key -> offset of the pointer to it
        self.objects: Dict[tuple, int] = {}  # object key -> offset of the object

    def tell(self) -> int:
        return self.pos

    def seek(self, offset: int) -> None:
        self.pos = offset

    def write(self, data: bytes) -> None:
        end = self.pos + len(data)
        if end > len(self.buf):
            self.buf.extend(bytes(end - len(self.buf)))
        self.buf[self.pos:end] = data
        self.pos = end

    def pack(self, fmt: str, *values) -> None:
        self.write(_struct.pack(fmt, *values))

    def cstr(self, text: str) -> None:
        self.write(text.encode("utf-8") + b"\0")

    def align(self, alignment: int) -> None:
        self.pos = (self.pos + alignment - 1) // alignment * alignment

    def write_at(self, offset: int, fmt: str, *values) -> None:
        here = self.pos
        self.pos = offset
        self.pack(fmt, *values)
        self.pos = here

    def register(self, key: tuple) -> None:
        if key in self.objects:
            raise ValueError("Attempted to register an object twice")
        self.objects[key] = self.pos

    def pointer(self, key: tuple) -> None:
        if key in self.pointers:
            raise ValueError("Attempted to register a pointer twice")
        self.pointers[key] = self.pos
        self.pack("<Q", PLACEHOLDER)

    # Fields

    def write_field(self, field: Field) -> None:
        offset = self.tell()
        self.pack("<QQBBHHHHHI", 0, 0, int(field.type), field.x11, int(field.flags), field.offset_in_value,
                  field.inline_size, field.data_size, len(field.fields), 0)
        self.pack("<QQ", 0, PARENT_MARKER)
        self.field_strings.append((offset, field.name))
        self.field_strings.append((offset + 0x8, field.type_name))
        if field.fields:
            here = self.pos
            self.pos = offset + 0x20
            self.pointer(("fields", id(field.fields)))
            self.pos = here

    def write_fields(self, fields: List[Field], is_root: bool = False) -> None:
        if not is_root and fields:
            self.register(("fields", id(fields)))
        for field in fields:
            self.write_field(field)
        for field in fields:
            self.write_fields(field.fields)

    # Values

    def write_inline(self, data, field: Field) -> None:
        if field.type == Field.Type.Struct:
            self.write_struct(data, field.fields)
        elif field.type == Field.Type.Bool:
            self.pack("<B", 1 if data else 0)
        elif field.type == Field.Type.Int:
            self.pack("<i", int(data))
        elif field.type == Field.Type.Float:
            self.pack("<f", float(data))
        else:
            self.cstr(data)

    def write_string_ptr(self, text: str, field: Field, key: tuple) -> None:
        if text or not field.flags & Field.Flag.IsNullable:
            self.pointer(key)
        else:
            self.pack("<Q", 0)
        self.pack("<II", len(text.encode("utf-8")), 0)

    def write_struct(self, value: dict, fields: List[Field]) -> None:
        base = self.tell()
        for field in fields:
            self.seek(base + field.offset_in_value)
            data = value[field.name]
            key = (id(value), field.name)
            if field.flags & Field.Flag.IsArray:
                self.pointer(key)
                self.pack("<II", len(data), 0)
            elif field.type == Field.Type.String:
                self.write_string_ptr(data, field, key)
            elif field.flags & Field.Flag.IsNullable:
                if data is None:
                    self.pack("<Q", 0)
                else:
                    self.pointer(key)
            else:
                self.write_inline(data, field)

    def write_struct_data(self, value: dict, fields: List[Field]) -> None:
        for field in fields:
            data = value[field.name]
            key = (id(value), field.name)
            if field.flags & Field.Flag.IsArray:
                if field.type == Field.Type.Struct:
                    for item in data:
                        self.write_struct_data(item, field.fields)
                    self.align(8)
                    self.register(key)
                    for item in data:
                        self.write_struct(item, field.fields)
                        self.align(8)
                elif field.type == Field.Type.String:
                    for i, text in enumerate(data):
                        self.register((id(data), i))
                        self.cstr(text)
                    self.align(8)
                    self.register(key)
                    for i, text in enumerate(data):
                        self.write_string_ptr(text, field, (id(data), i))
                else:
                    if field.type != Field.Type.Bool:
                        self.align(4)
                    self.register(key)
                    for item in data:
                        self.write_inline(item, field)
                        self.align(4)
            elif field.type == Field.Type.String:
                if data or not field.flags & Field.Flag.IsNullable:
                    self.register(key)
                    self.cstr(data)
            elif field.flags & Field.Flag.IsNullable:
                if data is not None:
                    if field.type == Field.Type.Struct:
                        self.write_struct_data(data, field.fields)
                        self.align(8)
                        self.register(key)
                        self.write_struct(data, field.fields)
                    else:
                        self.register(key)
                        self.write_inline(data, field)
            elif field.type == Field.Type.Struct:
                self.write_struct_data(data, field.fields)

    def write_sheet(self, sheet: "Sheet") -> bytes:
        values: List[dict] = [_fresh(value) for value in sheet.values]
        root = sheet.root_fields
        value_size = 0
        if root:
            last = root[-1]
            value_size = (last.offset_in_value + last.inline_size + sheet.alignment - 1) \
                // sheet.alignment * sheet.alignment
        self.pack("<4siIBBBx", MAGIC, 1, sheet.hash, 1, 8, sheet.alignment)
        self.pack("<QII", 0, len(root), _count_fields(root))
        self.pack("<QII", 0, len(values), value_size)
        self.field_strings.append((0x10, sheet.name))
        here = self.pos
        self.pos = 0x20
        self.pointer(("values",))
        self.pos = here

        self.write_fields(root, True)

        self.register(("values",))
        for value in values:
            self.write_struct(value, root)
            self.align(sheet.alignment)

        self.align(0x10)
        for ptr_offset, text in self.field_strings:
            self.write_at(ptr_offset, "<Q", self.pos)
            self.cstr(text)

        for value in values:
            self.write_struct_data(value, root)

        self.align(0x10)
        if self.pos > len(self.buf):
            self.buf.extend(bytes(self.pos - len(self.buf)))

        for key, ptr_offset in self.pointers.items():
            if key not in self.objects:
                raise ValueError(f"Missing object: no object was written for pointer {ptr_offset:#x}")
            self.write_at(ptr_offset, "<Q", self.objects[key])
        for key, obj_offset in self.objects.items():
            if key not in self.pointers:
                raise ValueError(f"Inaccessible object: no pointer was written for object {obj_offset:#x}")
        return bytes(self.buf)


def _count_fields(fields: List[Field]) -> int:
    return sum(1 + _count_fields(field.fields) for field in fields)


class Sheet:
    def __init__(self) -> None:
        self.alignment = 8
        self.hash = 0
        self.name = ""
        self.root_fields: List[Field] = []
        self.values: list = []

    def to_binary(self) -> bytes:
        return _Writer().write_sheet(self)


def parse_file(path) -> Optional[Sheet]:
    with open(path, "rb") as f:
        return parse(f.read())
