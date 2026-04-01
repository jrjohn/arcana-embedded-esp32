#!/usr/bin/env python3
"""
BLE Command Test — send FrameCodec commands to ESP32 via BLE GATT.

Requires: pip install bleak

Usage:
  python ble_cmd_test.py                    # scan + ping
  python ble_cmd_test.py --addr XX:XX:XX    # connect to specific device
  python ble_cmd_test.py --cmd sensor       # get sensor data
"""

import asyncio
import struct
import argparse
import sys

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    print("Install bleak: pip install bleak")
    sys.exit(1)

# BLE UUIDs (must match BleUuids.hpp — 16-bit standard format)
SERVICE_UUID = "0000181a-0000-1000-8000-00805f9b34fb"   # Environmental Sensing
CMD_CHAR_UUID = "0000ff10-0000-1000-8000-00805f9b34fb"  # Command write
RSP_CHAR_UUID = "0000ff11-0000-1000-8000-00805f9b34fb"  # Response notify
TEMP_CHAR_UUID = "00002a6e-0000-1000-8000-00805f9b34fb" # Temperature
HUMI_CHAR_UUID = "00002a6f-0000-1000-8000-00805f9b34fb" # Humidity

# FrameCodec
MAGIC = b"\xAC\xDA"
VERSION = 0x01
FLAG_FIN = 0x01

def crc16(data, init=0):
    crc = init
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1: crc = (crc >> 1) ^ 0x8408
            else: crc >>= 1
    return crc & 0xFFFF

def frame_encode(payload, flags=FLAG_FIN, stream_id=0):
    header = MAGIC + bytes([VERSION, flags, stream_id])
    header += struct.pack("<H", len(payload))
    body = header + payload
    return body + struct.pack("<H", crc16(body))

def frame_decode(data):
    if len(data) < 9 or data[0:2] != MAGIC:
        return None
    plen = struct.unpack("<H", data[5:7])[0]
    if len(data) < 7 + plen + 2:
        return None
    return data[7:7+plen], data[3], data[4]

# Protobuf manual encode
def pb_varint(v):
    r = b""
    while v > 0x7F: r += bytes([(v & 0x7F) | 0x80]); v >>= 7
    r += bytes([v & 0x7F])
    return r

def pb_field(fn, wt, data):
    tag = pb_varint((fn << 3) | wt)
    if wt == 0: return tag + pb_varint(data)
    elif wt == 2: return tag + pb_varint(len(data)) + data
    return tag

def pb_decode(data):
    fields = {}; off = 0
    while off < len(data):
        b = data[off]; off += 1; fn = b >> 3; wt = b & 7
        if wt == 0:
            v = 0; s = 0
            while off < len(data):
                b = data[off]; off += 1; v |= (b&0x7F)<<s; s += 7
                if not (b&0x80): break
            fields[fn] = v
        elif wt == 2:
            l = 0; s = 0
            while off < len(data):
                b = data[off]; off += 1; l |= (b&0x7F)<<s; s += 7
                if not (b&0x80): break
            fields[fn] = data[off:off+l]; off += l
    return fields

# Command builders
def cmd_ping():
    """System::Ping (cluster=0, cmd=1)"""
    pb = pb_field(1, 0, 0) + pb_field(2, 0, 1)  # cluster=0, command=1
    return frame_encode(pb)

def cmd_get_device_info():
    """System::GetDeviceInfo (cluster=0, cmd=2)"""
    pb = pb_field(1, 0, 0) + pb_field(2, 0, 2)
    return frame_encode(pb)

def cmd_get_sensor_data():
    """Sensor::GetData (cluster=1, cmd=1)"""
    pb = pb_field(1, 0, 1) + pb_field(2, 0, 1)
    return frame_encode(pb)

def cmd_get_ble_status():
    """Ble::GetStatus (cluster=2, cmd=1)"""
    pb = pb_field(1, 0, 2) + pb_field(2, 0, 1)
    return frame_encode(pb)

COMMANDS = {
    "ping": cmd_ping,
    "info": cmd_get_device_info,
    "sensor": cmd_get_sensor_data,
    "ble": cmd_get_ble_status,
}

async def scan_devices():
    print("Scanning for BLE devices...")
    devices = await BleakScanner.discover(timeout=5.0)
    arcana = [d for d in devices if d.name and "ARCANA" in d.name.upper()]
    if arcana:
        print(f"\nFound Arcana devices:")
        for d in arcana:
            print(f"  {d.address}  {d.name}")
        return arcana[0].address
    else:
        print("No Arcana devices found. All devices:")
        for d in devices:
            if d.name:
                print(f"  {d.address}  {d.name}")
        return None

async def send_command(addr, cmd_name):
    response_data = bytearray()

    def notification_handler(sender, data):
        nonlocal response_data
        response_data.extend(data)
        print(f"  <- Notify ({len(data)} bytes): {data.hex()}")

    async with BleakClient(addr) as client:
        print(f"Connected to {addr}")

        # Enable response notifications
        await client.start_notify(RSP_CHAR_UUID, notification_handler)

        # Build and send command
        cmd_builder = COMMANDS.get(cmd_name, cmd_ping)
        frame = cmd_builder()
        print(f"  -> Write ({len(frame)} bytes): {frame.hex()}")
        await client.write_gatt_char(CMD_CHAR_UUID, frame, response=False)

        # Wait for response
        await asyncio.sleep(2.0)

        # Parse response
        if response_data:
            result = frame_decode(bytes(response_data))
            if result:
                payload, flags, sid = result
                fields = pb_decode(payload)
                print(f"\n  Response fields: {fields}")
                # Parse status
                status = fields.get(3, 0xFF)
                print(f"  Status: {'OK' if status == 0 else f'Error({status})'}")
                if 4 in fields:
                    print(f"  Payload ({len(fields[4])} bytes): {fields[4].hex()}")
            else:
                print(f"\n  Raw response: {response_data.hex()}")
        else:
            print("\n  No response received")

        await client.stop_notify(RSP_CHAR_UUID)

async def main():
    parser = argparse.ArgumentParser(description="BLE Command Test for Arcana ESP32")
    parser.add_argument("--addr", help="BLE device address")
    parser.add_argument("--cmd", choices=list(COMMANDS.keys()), default="ping",
                        help="Command to send")
    parser.add_argument("--scan", action="store_true", help="Scan only")
    args = parser.parse_args()

    if args.scan:
        await scan_devices()
        return

    addr = args.addr
    if not addr:
        addr = await scan_devices()
        if not addr:
            print("No device found. Use --addr to specify.")
            return

    print(f"\nSending {args.cmd} command...")
    await send_command(addr, args.cmd)

if __name__ == "__main__":
    asyncio.run(main())
