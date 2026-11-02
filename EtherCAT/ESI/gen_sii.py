#!/usr/bin/env python3
"""Generate the SII EEPROM image (eeprom.bin) for the F4Ethercat LAN9252 slave.

Keep the values in sync with EtherCAT/ecat_options.h, EtherCAT/objectlist.c
and F4Ethercat.xml.

Usage: python gen_sii.py [output.bin]
"""
import struct
import sys
from pathlib import Path

VENDOR_ID = 0x00000000  # placeholder, replace with an ETG assigned vendor ID
PRODUCT_CODE = 0x00000001
REVISION = 0x00000001
SERIAL = 0x00000001

EEPROM_BYTES = 2048

# ESC configuration area (words 0..6, CRC in word 7)
PDI_CONTROL = 0x80        # 0x0140: SPI
ESC_CONFIG = 0x0C         # 0x0141: DC SYNC out + latch in units enabled
PDI_CONFIG = 0x00         # 0x0150
SYNC_LATCH_CONFIG = 0xCC  # 0x0151: SYNC0/1 push-pull active low, output, AL event
SYNC_PULSE_LEN = 1000     # 0x0982: in 10 ns steps -> 10 us
EXT_PDI_CONFIG = 0x0000   # 0x0152
STATION_ALIAS = 0x0000    # 0x0012

MBX_RX = (0x1000, 128)    # SM0, master -> slave
MBX_TX = (0x1080, 128)    # SM1, slave -> master
MBX_PROTO_COE = 0x0004

# (start, length, control, enable, type)
SYNCMANAGERS = [
    (MBX_RX[0], MBX_RX[1], 0x26, 0x01, 1),  # mailbox out
    (MBX_TX[0], MBX_TX[1], 0x22, 0x01, 2),  # mailbox in
    (0x1100, 1, 0x64, 0x01, 3),             # process data outputs
    (0x1180, 1, 0x20, 0x01, 4),             # process data inputs
]

STRINGS = [
    "F4Ethercat",              # 1: group / order / type
    "F4Ethercat LAN9252 LED",  # 2: device name
    "Outputs",                 # 3: RxPDO 0x1600
    "LED",                     # 4: 0x7000:01
    "Inputs",                  # 5: TxPDO 0x1A00
    "LED_State",               # 6: 0x6000:01
]

DTYPE_UNSIGNED8 = 0x05
PDO_FLAGS = 0x0013  # mandatory, default, fixed content

# (pdo index, sm, name idx, [(index, subindex, name idx, datatype, bitlen)])
RXPDOS = [(0x1600, 2, 3, [(0x7000, 1, 4, DTYPE_UNSIGNED8, 8)])]
TXPDOS = [(0x1A00, 3, 5, [(0x6000, 1, 6, DTYPE_UNSIGNED8, 8)])]

CAT_STRINGS = 10
CAT_GENERAL = 30
CAT_FMMU = 40
CAT_SYNCM = 41
CAT_TXPDO = 50
CAT_RXPDO = 51
CAT_END = 0xFFFF


def crc8(data):
    """ESC configuration area checksum: x^8 + x^2 + x + 1, init 0xFF."""
    crc = 0xFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def category(cat_type, data):
    if len(data) % 2:
        data += b"\x00"
    return struct.pack("<HH", cat_type, len(data) // 2) + data


def strings_category():
    data = bytes([len(STRINGS)])
    for s in STRINGS:
        raw = s.encode("ascii")
        data += bytes([len(raw)]) + raw
    return category(CAT_STRINGS, data)


def general_category():
    data = struct.pack(
        "<BBBBBBBBBBBBhBBH",
        1,     # group (string index)
        0,     # image
        1,     # order
        2,     # name
        0,     # reserved
        0x03,  # CoE: SDO + SDO info
        0,     # FoE
        0,     # EoE
        0,     # SoE channels
        0,     # DS402 channels
        0,     # SysmanClass
        0,     # flags
        0,     # current on E-bus [mA]
        1,     # group (compatibility)
        0,     # reserved
        0x0011,  # physical ports: port0 + port1 MII (internal PHYs)
    )
    data += b"\x00" * (32 - len(data))
    return category(CAT_GENERAL, data)


def fmmu_category():
    return category(CAT_FMMU, bytes([0x01, 0x02, 0x03]))  # outputs, inputs, mbx state


def syncm_category():
    data = b""
    for start, length, control, enable, sm_type in SYNCMANAGERS:
        data += struct.pack("<HHBBBB", start, length, control, 0, enable, sm_type)
    return category(CAT_SYNCM, data)


def pdo_category(cat_type, pdos):
    data = b""
    for index, sm, name, entries in pdos:
        data += struct.pack("<HBBBBH", index, len(entries), sm, 0, name, PDO_FLAGS)
        for e_index, e_sub, e_name, e_type, e_bits in entries:
            data += struct.pack("<HBBBBH", e_index, e_sub, e_name, e_type, e_bits, 0)
    return category(cat_type, data)


def build():
    config = struct.pack(
        "<BBBBHHHHH",
        PDI_CONTROL, ESC_CONFIG,
        PDI_CONFIG, SYNC_LATCH_CONFIG,
        SYNC_PULSE_LEN,
        EXT_PDI_CONFIG,
        STATION_ALIAS,
        0, 0,
    )
    header = config + struct.pack("<H", crc8(config))
    header += struct.pack("<IIII", VENDOR_ID, PRODUCT_CODE, REVISION, SERIAL)
    header += b"\x00" * 8                         # delays + reserved
    header += struct.pack("<HHHH", *MBX_RX, *MBX_TX)  # bootstrap mailbox
    header += struct.pack("<HHHH", *MBX_RX, *MBX_TX)  # standard mailbox
    header += struct.pack("<H", MBX_PROTO_COE)
    header += b"\x00" * (0x7C - len(header))
    header += struct.pack("<HH", EEPROM_BYTES * 8 // 1024 - 1, 1)  # size [kbit - 1], version
    assert len(header) == 0x80

    image = header
    image += strings_category()
    image += general_category()
    image += fmmu_category()
    image += syncm_category()
    image += pdo_category(CAT_TXPDO, TXPDOS)
    image += pdo_category(CAT_RXPDO, RXPDOS)
    image += struct.pack("<H", CAT_END)
    assert len(image) <= EEPROM_BYTES
    return image + b"\xFF" * (EEPROM_BYTES - len(image))


if __name__ == "__main__":
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("eeprom.bin")
    out.write_bytes(build())
    print(f"wrote {out}")
