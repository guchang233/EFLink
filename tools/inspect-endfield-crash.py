"""Inspect a local Windows minidump without exporting heap or account contents.

Requires the installed minidump and capstone packages. Stack addresses are
candidate frames, not a symbolically unwound or proven call stack.
"""
import argparse
import json
from pathlib import Path, PureWindowsPath
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from minidump.minidumpfile import MinidumpFile


def inspect_dump(path):
    dump = MinidumpFile.parse(str(path))
    reader = dump.get_reader()
    event = dump.exception.exception_records[0]
    record = event.ExceptionRecord
    dump.file_handle.seek(event.ThreadContext.Rva)
    context = dump.file_handle.read(event.ThreadContext.DataSize)
    fields = [('rax', 0x78), ('rcx', 0x80), ('rdx', 0x88), ('rbx', 0x90),
              ('rsp', 0x98), ('rbp', 0xa0), ('rsi', 0xa8), ('rdi', 0xb0),
              ('r8', 0xb8), ('r9', 0xc0), ('r10', 0xc8), ('r11', 0xd0),
              ('r12', 0xd8), ('r13', 0xe0), ('r14', 0xe8), ('r15', 0xf0), ('rip', 0xf8)]
    registers = {name: struct.unpack_from('<Q', context, offset)[0] for name, offset in fields}

    def module_address(address):
        for module in dump.modules.modules:
            if module.baseaddress <= address < module.endaddress:
                return f'{PureWindowsPath(module.name).name}+{address - module.baseaddress:#x}'
        return None

    def available_bytes(address, limit):
        segment = next(s for s in reader.memory_segments if s.inrange(address))
        return reader.read(address, min(limit, segment.end_virtual_address - address))

    instructions = []
    try:
        for instruction in Cs(CS_ARCH_X86, CS_MODE_64).disasm(
                available_bytes(registers['rip'], 96), registers['rip']):
            instructions.append({'address': module_address(instruction.address),
                                 'instruction': instruction.mnemonic + ' ' + instruction.op_str})
    except Exception as error:
        instructions.append({'unavailable': str(error)})
    candidates = []
    stack = available_bytes(registers['rsp'], 4096)
    for offset in range(0, len(stack) - 7, 8):
        label = module_address(struct.unpack_from('<Q', stack, offset)[0])
        if label:
            candidates.append({'stack_offset': hex(offset), 'module_address': label})
    return {
        'dump': str(path.resolve()), 'exception_thread': event.ThreadId,
        'exception_code': hex(record.ExceptionCode_raw),
        'exception_address': module_address(record.ExceptionAddress),
        'exception_parameters': [hex(v) for v in record.ExceptionInformation],
        'registers': {name: hex(value) for name, value in registers.items()},
        'instructions': instructions, 'stack_candidate_addresses': candidates,
        'stack_candidates_are_not_unwound_frames': True,
        'relevant_loaded_modules': [PureWindowsPath(m.name).name for m in dump.modules.modules
                                    if any(term in m.name.lower() for term in
                                           ('eflink.', 'efml.', 'gameassembly', 'unityplayer'))],
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = inspect_dump(args.dump)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(result['exception_code'], result['exception_address'])
    print('Saved local code/register diagnostics:', args.output.resolve())
