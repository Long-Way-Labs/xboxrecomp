"""disasm_lazily yields what capstone's eager disasm yields, batch by batch."""
import random

import pytest
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

from tools.recomp.disasm import disasm_lazily


@pytest.mark.parametrize('seed', range(40))
@pytest.mark.parametrize('batch', [1, 2, 32])
def test_same_instructions_and_same_stop(seed, batch):
    rng = random.Random(seed)
    # Valid runs (nop, mov eax imm32, a 15-byte add, ud2, ret) and, for every
    # third seed, the invalid 0f 04: some streams stop early, some reach the end.
    parts = [b'\x90', b'\xb8\x01\x02\x03\x04',
             bytes.fromhex('2e3e26646566818480000000000100'),
             b'\x0f\x0b', b'\xc3']
    code = b''.join(rng.choice(parts) for _ in range(rng.randrange(0, 200)))
    if seed % 3 == 0:
        cut = rng.randrange(0, len(code) + 1)
        code = code[:cut] + b'\x0f\x04' + code[cut:]
    cs = Cs(CS_ARCH_X86, CS_MODE_32)
    eager = [(i.address, i.size) for i in cs.disasm(code, 0x1000)]
    lazy = [(i.address, i.size) for i in disasm_lazily(cs, code, 0x1000, batch)]
    assert lazy == eager
