#!/usr/bin/env python3
"""
BLE Crypto Test — ESP32-S3 command channel over BLE GATT (AES-256-CCM + protobuf).

The ESP32 firmware speaks AES-256-CCM + protobuf on the command channel (the
node `ble-cmd-test` tool is ChaCha20+binary, for STM32). This is the matching
client for the ESP32-S3: it connects over BLE (CoreBluetooth/BlueZ via bleak),
performs the ECDH P-256 key exchange, and sends an AES-256-CCM-encrypted command
under the derived session key — proving Perfect Forward Secrecy end-to-end.

Wire format (identical to tools/mqtt_crypto_test.py, only the transport differs):
  Frame:  [magic:2=AC DA][ver:1][flags:1][sid:1][len:2 LE][payload:N][crc:2 LE]
  Crypto: [counter:4 LE][ciphertext:N][tag:8]   (AES-256-CCM, 13-byte nonce)
  Nonce:  SHA256(key || "ARCANA")[0:9] || counter:4 LE
  Payload: protobuf CmdRequest / CmdResponse

GATT (Environmental Sensing 0x181A):
  Command  0xFF10  (Write)   <- client writes framed/encrypted CmdRequest
  Response 0xFF11  (Notify)  <- device notifies framed/encrypted CmdResponse

ECDH (Security cluster 0x04 / cmd 0x01 KeyExchange), all encrypted under the PSK:
  request payload  = client P-256 pubkey (64B raw x||y)
  response payload = server pubkey (64B) || HMAC-SHA256(PSK, serverPub||clientPub) (32B)
  session key      = HKDF-SHA256(ikm=ECDH_shared, salt=PSK, info="ARCANA-SESSION")

Usage:
  export ARCANA_PSK=<64-hex>            # must match firmware CONFIG_CMD_ENCRYPTION_PSK
  python3 tools/ble_crypto_test.py                 # ECDH key exchange + encrypted ping
  python3 tools/ble_crypto_test.py --cmd fw_version
  python3 tools/ble_crypto_test.py --no-key-exchange   # use PSK directly (no session)
"""

import argparse, asyncio, hashlib, hmac, os, struct, sys

try:
    from bleak import BleakScanner, BleakClient
    from cryptography.hazmat.primitives.ciphers.aead import AESCCM
    from cryptography.hazmat.primitives.asymmetric import ec
except ImportError as e:
    sys.exit(f"pip install bleak cryptography  ({e})")

# ── GATT UUIDs (ESP32-S3 firmware) ──────────────────────────────────────────
CMD_CHAR  = "0000ff10-0000-1000-8000-00805f9b34fb"
RSP_CHAR  = "0000ff11-0000-1000-8000-00805f9b34fb"
DEV_NAME  = "ARCANA-ESP32S3"

# ── PSK ─────────────────────────────────────────────────────────────────────
PSK_HEX = os.environ.get("ARCANA_PSK", "")
if not PSK_HEX:
    env = os.path.join(os.path.dirname(__file__), ".env")
    if os.path.exists(env):
        for line in open(env):
            if line.startswith("ARCANA_PSK="):
                PSK_HEX = line.strip().split("=", 1)[1].strip()
if not PSK_HEX:
    sys.exit("ERROR: set ARCANA_PSK env (64 hex) to match firmware CONFIG_CMD_ENCRYPTION_PSK")

# ── protobuf (manual, no .proto dependency) ─────────────────────────────────
def _varint(v):
    out = b""
    while v > 0x7F:
        out += bytes([(v & 0x7F) | 0x80]); v >>= 7
    return out + bytes([v & 0x7F])

def _field(num, wt, data): return _varint((num << 3) | wt) + data
def _u32(num, v): return _field(num, 0, _varint(v))
def _bytes(num, d): return _field(num, 2, _varint(len(d)) + d)

def encode_cmd_request(cluster, command, payload=b""):
    msg = b""
    if cluster: msg += _u32(1, cluster)
    if command: msg += _u32(2, command)
    if payload: msg += _bytes(3, payload)
    return msg

def _dec_varint(d, o):
    r = s = 0
    while o < len(d):
        b = d[o]; o += 1; r |= (b & 0x7F) << s
        if not (b & 0x80): return r, o
        s += 7
    raise ValueError("truncated varint")

def decode_cmd_response(d):
    res = {"cluster": 0, "command": 0, "status": 0, "payload": b""}
    o = 0
    while o < len(d):
        tag, o = _dec_varint(d, o); fn = tag >> 3; wt = tag & 7
        if wt == 0:
            v, o = _dec_varint(d, o)
            if fn == 1: res["cluster"] = v
            elif fn == 2: res["command"] = v
            elif fn == 3: res["status"] = v
        elif wt == 2:
            ln, o = _dec_varint(d, o); res["payload"] = d[o:o+ln]; o += ln
        else:
            raise ValueError(f"wire type {wt}")
    return res

# ── CRC-16 (poly 0x8408 reflected) + FrameCodec ─────────────────────────────
def crc16(data, init=0):
    crc = init
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc & 0xFFFF

MAGIC = b"\xAC\xDA"; VERSION = 0x01; FLAG_FIN = 0x01

def frame_encode(payload, flags=FLAG_FIN, sid=0):
    body = MAGIC + bytes([VERSION, flags, sid]) + struct.pack("<H", len(payload)) + payload
    return body + struct.pack("<H", crc16(body))

def frame_decode(data):
    if len(data) < 9 or data[0:2] != MAGIC or data[2] != VERSION: return None
    plen = struct.unpack("<H", data[5:7])[0]
    if len(data) != 7 + plen + 2: return None
    if crc16(data[:7+plen]) != struct.unpack("<H", data[7+plen:9+plen])[0]: return None
    return data[7:7+plen], data[3], data[4]

# ── AES-256-CCM CryptoEngine (matches firmware CryptoEngine) ────────────────
class CryptoEngine:
    TAG = 8; CTR = 4; NPREFIX = 9

    def __init__(self, key):
        assert len(key) == 32
        self.prefix = hashlib.sha256(key + b"ARCANA").digest()[:self.NPREFIX]
        self.ccm = AESCCM(key, tag_length=self.TAG)
        self.tx = 0; self.rx = -1

    def _nonce(self, ctr): return self.prefix + struct.pack("<I", ctr)

    def encrypt(self, pt):
        ctr = self.tx; self.tx += 1
        ct = self.ccm.encrypt(self._nonce(ctr), pt, None)  # ct = ciphertext||tag
        return struct.pack("<I", ctr) + ct

    def decrypt(self, data):
        if len(data) < self.CTR + self.TAG: raise ValueError("too short")
        ctr = struct.unpack("<I", data[:4])[0]
        if ctr <= self.rx: raise ValueError(f"replay {ctr} <= {self.rx}")
        pt = self.ccm.decrypt(self._nonce(ctr), data[4:], None)
        self.rx = ctr
        return pt

# ── HKDF-SHA256 (single block) ──────────────────────────────────────────────
def hkdf(ikm, salt, info, length=32):
    prk = hmac.new(salt, ikm, "sha256").digest()
    return hmac.new(prk, info + b"\x01", "sha256").digest()[:length]

CLUSTER_SECURITY = 0x04; KEYEXCHANGE = 0x01
COMMANDS = {"ping": (0x00, 0x01), "fw_version": (0x00, 0x02),
            "compile_time": (0x00, 0x03), "temperature": (0x01, 0x02)}
STATUS = {0: "OK", 1: "NotFound", 2: "InvalidParam", 3: "Busy", 4: "Error"}


class BleCmd:
    def __init__(self, client, psk):
        self.client = client
        self.psk = psk
        self.crypto = CryptoEngine(psk)   # bootstrap engine (PSK)
        self.session = None               # set after key exchange
        self._rx = asyncio.Queue()

    def _notify(self, _char, data: bytearray):
        self._rx.put_nowait(bytes(data))

    async def start(self):
        await self.client.start_notify(RSP_CHAR, self._notify)

    async def _round_trip(self, framed, timeout=12):
        # drain any stale notifications
        while not self._rx.empty():
            self._rx.get_nowait()
        await self.client.write_gatt_char(CMD_CHAR, framed, response=True)
        return await asyncio.wait_for(self._rx.get(), timeout)

    async def key_exchange(self):
        priv = ec.generate_private_key(ec.SECP256R1())
        n = priv.public_key().public_numbers()
        client_pub = n.x.to_bytes(32, "big") + n.y.to_bytes(32, "big")
        print(f"[KE] client pub  {client_pub[:8].hex()}…{client_pub[-8:].hex()}")

        pb = encode_cmd_request(CLUSTER_SECURITY, KEYEXCHANGE, client_pub)
        framed = frame_encode(self.crypto.encrypt(pb))   # encrypted under PSK
        print(f"[KE] → Security/KeyExchange  ({len(framed)}B, AES-256-CCM/PSK)")

        raw = await self._round_trip(framed)
        dec = frame_decode(raw)
        if not dec: raise RuntimeError(f"deframe fail: {raw.hex()}")
        plain = self.crypto.decrypt(dec[0])
        rsp = decode_cmd_response(plain)
        if rsp["status"] != 0: raise RuntimeError(f"server rejected status={rsp['status']}")
        if len(rsp["payload"]) != 96: raise RuntimeError(f"bad KE payload {len(rsp['payload'])}B")

        server_pub = rsp["payload"][:64]; auth_tag = rsp["payload"][64:96]
        print(f"[KE] server pub  {server_pub[:8].hex()}…{server_pub[-8:].hex()}")

        expect = hmac.new(self.psk, server_pub + client_pub, "sha256").digest()
        if not hmac.compare_digest(auth_tag, expect):
            raise RuntimeError("AUTH TAG MISMATCH — wrong PSK or MITM")
        print("[KE] auth tag verified (HMAC-SHA256 over serverPub‖clientPub, keyed by PSK)")

        spn = ec.EllipticCurvePublicNumbers(int.from_bytes(server_pub[:32], "big"),
                                            int.from_bytes(server_pub[32:64], "big"),
                                            ec.SECP256R1())
        shared = priv.exchange(ec.ECDH(), spn.public_key())
        session_key = hkdf(shared, self.psk, b"ARCANA-SESSION", 32)
        print(f"[KE] ECDH shared {shared[:8].hex()}…  →  session key {session_key[:8].hex()}…")

        self.session = CryptoEngine(session_key)
        print("[KE] session established — Perfect Forward Secrecy active ✅")

    async def command(self, name):
        cluster, cmd = COMMANDS[name]
        eng = self.session or self.crypto
        which = "session key" if self.session else "PSK"
        pb = encode_cmd_request(cluster, cmd)
        framed = frame_encode(eng.encrypt(pb))
        print(f"[CMD] → {name}  cluster=0x{cluster:02X} cmd=0x{cmd:02X}  "
              f"({len(framed)}B, AES-256-CCM/{which})")
        raw = await self._round_trip(framed)
        dec = frame_decode(raw)
        if not dec: raise RuntimeError(f"deframe fail: {raw.hex()}")
        rsp = decode_cmd_response(eng.decrypt(dec[0]))
        st = STATUS.get(rsp["status"], hex(rsp["status"]))
        print(f"[CMD] ← cluster=0x{rsp['cluster']:02X} cmd=0x{rsp['command']:02X} status={st}"
              + (f"  payload({len(rsp['payload'])}B)={rsp['payload'].hex()}" if rsp["payload"] else ""))
        if rsp["payload"] and len(rsp["payload"]) >= 8 and name == "ping":
            us = struct.unpack("<Q", rsp["payload"][:8])[0]
            print(f"[CMD]   ping uptime = {us} µs ({us/1e6:.1f} s)")
        return rsp


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cmd", default="ping", choices=COMMANDS.keys())
    ap.add_argument("--no-key-exchange", action="store_true", help="use PSK directly (no ECDH session)")
    ap.add_argument("--addr", help="BLE address/UUID (skip scan)")
    ap.add_argument("--timeout", type=float, default=15)
    args = ap.parse_args()

    psk = bytes.fromhex(PSK_HEX)
    print(f"PSK: {PSK_HEX[:8]}…{PSK_HEX[-8:]} (32B)  | target: {DEV_NAME}")

    addr = args.addr
    if not addr:
        print(f"Scanning for {DEV_NAME} …")
        dev = await BleakScanner.find_device_by_name(DEV_NAME, timeout=args.timeout)
        if not dev: sys.exit(f"not found: {DEV_NAME}")
        addr = dev.address
        print(f"Found {DEV_NAME} @ {addr}")

    async with BleakClient(addr) as client:
        print(f"Connected (mtu≈{getattr(client, 'mtu_size', '?')})")
        h = BleCmd(client, psk)
        await h.start()
        if not args.no_key_exchange:
            await h.key_exchange()
        await h.command(args.cmd)
        print("DONE")


if __name__ == "__main__":
    asyncio.run(main())
