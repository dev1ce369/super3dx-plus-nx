"""Read which brake lamp model each car loads, and when its high-mounted and wing stop lamps
light, out of `main` by running the arcade's own lookups under the unicorn emulator, and write
tools/brakelamps.json for tools/stage/pack_cars.py.

usage: brakelampdb.py <main>   (WMMT_MAIN for tools/recomp/x86emu.py)

- The car (emCarID) of a pack: the car select's cars by nsCarParts::LuaNickNameGetFromCarID
  (0x893d6fc: the table at A(0x9417720), its lua name, ids 0..55); the story rivals' own cars
  (data/characar) by the model table at A(0x9419888) (24 bytes: lua name, emCarModelID, ...) and
  nsCarParts::CarIDGetFromModelID(model, true) (A(0x893dd48)), as clEffectConstructParam's
  constructor (A(0x85f983c)) finds the car its lamps are made for.
- clCarLampSource's constructor (A(0x8625eb0)) loads nsBrakeParts::BrakeLampFileNameGetFromCarId
  (car, body) (A(0x893f430), the table at A(0x941fb20)): by car, not by the pack's folder, so the
  Evos' GSR and RS share brakelamp_mit_CE9A.igb, the 997's aero B and C its aero A lamps.
- clCarLampManager::update (A(0x8602d98)) shows STOP_HIGH on the brake when the source's +9 is
  set, STOP_WING2 when +0xa is: nsBrakeParts::BrakelampHasHighMount and BrakelampHasWingBrake
  (car, body, wing) (A(0x893ed30), A(0x893f0b0)), the wing being the construct param's +0x224
  (clCarDispManager::setDispWorkDirect hands it clEffectConstructParam's +0xc).

Output: {"cars": {lua name: car}, "lamps": {"car body": [model, high wings, wing2 wings]}}
with body 0 (stock) .. 3 (aero C), the model data/car/brakelamp/brakelamp_<model>.igb and a
bit per wing 0..4 (4: no wing).
"""
from wmmtaddr import A  # the arcade addresses of the build being read
import json
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "recomp"))
os.environ["WMMT_MAIN"] = sys.argv[1]
from x86emu import Emu  # noqa: E402

LUA_NICK = A(0x893d6fc)
CAR_FROM_MODEL = A(0x893dd48)
LAMP_FILE = A(0x893f430)
HAS_HIGH = A(0x893ed30)
HAS_WING2 = A(0x893f0b0)
MODELS = A(0x9419888)
CARS = 120  # clCarLampSource makes no lamps past 0x77
LAMP_DIR = "data/car/brakelamp/brakelamp_"

e = Emu(trace_unknown=False)
elf = open(sys.argv[1], "rb").read()


def cstr(va):
    o = va - A(0x8048000)
    return elf[o:elf.index(b"\0", o)].decode("latin1")


cars = {}
for car in range(CARS):
    name = e.call(LUA_NICK, car)
    if name:
        cars[cstr(name)] = car
for i in range(256):
    name, model, _, _, z0, z1 = struct.unpack_from("<IiiiII", elf, MODELS - A(0x8048000) + 24 * i)
    if z0 or z1 or not A(0x9000000) < name < A(0x9500000):
        break
    cars[cstr(name)] = e.call(CAR_FROM_MODEL, model, 1)

lamps = {}
for car in sorted(set(cars.values())):
    for body in range(4):
        path = e.call(LAMP_FILE, car, body)
        if not path:
            continue
        high = sum((e.call(HAS_HIGH, car, body, w) & 0xFF) << w for w in range(5))
        wing2 = sum((e.call(HAS_WING2, car, body, w) & 0xFF) << w for w in range(5))
        model = cstr(path)
        assert model.startswith(LAMP_DIR) and model.endswith(".igb"), model
        lamps["%d %d" % (car, body)] = [model[len(LAMP_DIR):-4], high, wing2]

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "brakelamps.json")
rows = sorted(cars.items())
text = '{"cars": {\n' + ",\n".join(" %s: %d" % (json.dumps(k), v) for k, v in rows)
rows = sorted(lamps.items(), key=lambda kv: tuple(map(int, kv[0].split())))
text += '\n}, "lamps": {\n' + ",\n".join(" %s: %s" % (json.dumps(k), json.dumps(v)) for k, v in rows)
with open(out, "w") as f:
    f.write(text + "\n}}\n")
print("%d cars, %d lamp models -> %s" % (len(cars), len(lamps), out))
