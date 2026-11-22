"""Dump the arcade main's memory image (rodata .. end of bss) for the recompiled physics.

The game-logic static constructors are run first (under Unicorn, everything else stubbed),
because the physics reads tables they fill in. They must not use the heap: the recompiled
runtime starts with an empty one.
"""
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..'))
from wmmtaddr import A  # the arcade addresses of the build being read
import sys
from x86emu import Emu, HEAP_BASE
from seq import stub_nonlogic, run_logic_ctors

BASE, END = A(0x092f1b20), A(0x0993a468)
e = Emu(trace_unknown=False)
stub_nonlogic(e)
run_logic_ctors(e)
if e.heap != HEAP_BASE:
    sys.exit("constructors allocated %d heap bytes" % (e.heap - HEAP_BASE))
open(sys.argv[1], "wb").write(e.rd(BASE, END - BASE))
