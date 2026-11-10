"""Konfigurator BLE HRM SmartFan v2 dla Windows."""

import argparse
import asyncio
from contextlib import asynccontextmanager
from dataclasses import dataclass
import json
from pathlib import Path
import struct
import sys
from zlib import crc32

SERVICE = "578c0001-a943-42e2-90d7-e9eb82a2a89b"
RX = "578c0002-a943-42e2-90d7-e9eb82a2a89b"
TX = "578c0003-a943-42e2-90d7-e9eb82a2a89b"
VERSION = 2
GET, SET, SAVE, CLOSE = 1, 2, 3, 4
HEADER = struct.Struct("<2sBBIH")
CONFIG = struct.Struct("<15H")
CHECKSUM = struct.Struct("<I")
CHUNK = 20
MAX_PAYLOAD = CONFIG.size + 1
FRAGMENT_SECONDS = 2
ACK_SECONDS = 3
RESULTS = {
    1: "Niepoprawny format lub wersja protokołu.",
    2: "Parametry konfiguracji nie przeszły walidacji.",
    3: "Brak zatwierdzonego kandydata SET w tej sesji.",
    4: "Urządzenie nie potwierdziło zapisu i odczytu NVS.",
    5: "Identyfikator transakcji jest powtórzony lub nieaktualny.",
    6: "Brak poprawnej konfiguracji w NVS. Przygotuj plik poleceniem defaults.",
}


@dataclass(frozen=True)
class Frame:
    version: int
    op: int
    txid: int
    payload: bytes


def encode(frame):
    if len(frame.payload) > MAX_PAYLOAD:
        raise ValueError("Za długi komunikat.")
    packet = HEADER.pack(b"SF", frame.version, frame.op, frame.txid, len(frame.payload)) + frame.payload
    return packet + CHECKSUM.pack(crc32(packet))


class Stream:
    def __init__(self):
        self.buffer = bytearray()
        self.last = 0

    def feed(self, chunk, now):
        if now - self.last >= FRAGMENT_SECONDS:
            self.buffer.clear()
        self.last = now
        frames = []
        for value in chunk:
            if ((len(self.buffer) == 0 and value != ord("S"))
                    or (len(self.buffer) == 1 and value != ord("F"))):
                self.buffer.clear()
                if value != ord("S"):
                    continue
            self.buffer.append(value)
            if len(self.buffer) < HEADER.size:
                continue
            _, version, op, txid, size = HEADER.unpack_from(self.buffer)
            if size > MAX_PAYLOAD:
                self.buffer.clear()
                continue
            end = HEADER.size + size
            if len(self.buffer) < end + CHECKSUM.size:
                continue
            if CHECKSUM.unpack_from(self.buffer, end)[0] == crc32(self.buffer[:end]):
                frames.append(Frame(version, op, txid, bytes(self.buffer[HEADER.size:end])))
            self.buffer.clear()
        return frames


DEFAULTS = {
    "wersja": 2,
    "progi_bpm": [105, 125, 145, 165, 200],
    "nastawy": [30, 45, 60, 75, 95],
    "histereza_bpm": 3,
    "filtr_ms": 2000,
    "wzrost_pkt_s": 15,
    "spadek_pkt_s": 8,
    "reczna": 45,
}


def config_encode(settings):
    if not isinstance(settings, dict) or settings.keys() != DEFAULTS.keys():
        raise ValueError("Plik musi zawierać pełny zestaw pól konfiguracji v2.")
    if type(settings["wersja"]) is not int or settings["wersja"] != VERSION:
        raise ValueError("Nieznana wersja konfiguracji.")
    thresholds = settings["progi_bpm"]
    demands = settings["nastawy"]
    if not all(isinstance(x, list) and len(x) == 5 for x in (thresholds, demands)):
        raise ValueError("Wymagane jest pięć progów i pięć nastaw.")
    scalar_keys = ("histereza_bpm", "filtr_ms", "wzrost_pkt_s", "spadek_pkt_s", "reczna")
    scalars = [settings[key] for key in scalar_keys]
    values = thresholds + demands + scalars
    ranges = [(30, 240)] * 5 + [(0, 100)] * 5 + [(0, 10), (0, 10000), (1, 100), (1, 100), (0, 100)]
    if not all(type(value) is int and low <= value <= high
               for value, (low, high) in zip(values, ranges, strict=True)):
        raise ValueError("Niedozwolony typ lub zakres parametru.")
    if any(b - a <= scalars[0] for a, b in zip(thresholds, thresholds[1:])):
        raise ValueError("Progi muszą rosnąć z odstępem większym od histerezy.")
    if any(b < a for a, b in zip(demands, demands[1:])):
        raise ValueError("Nastawy muszą być niemalejące.")
    return CONFIG.pack(*values)


def config_decode(data):
    if len(data) != CONFIG.size:
        raise ValueError("Niekompletny odczyt konfiguracji.")
    values = CONFIG.unpack(data)
    settings = dict(zip(DEFAULTS, (VERSION, list(values[:5]), list(values[5:10]), *values[10:]), strict=True))
    config_encode(settings)
    return settings


def load_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"Powtórzone pole w JSON: {key}")
            result[key] = value
        return result

    return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=unique)


def write_json(path, settings):
    with path.open("x", encoding="utf-8") as target:
        json.dump(settings, target, ensure_ascii=False, indent=2)
        target.write("\n")


class ConfigError(Exception):
    pass


class Session:
    def __init__(self, client):
        self.client = client
        self.next_id = 0
        self.stream = Stream()
        self.inbox = asyncio.Queue(maxsize=16)
        self.waiting = None
        self.overflow = False

    async def __aenter__(self):
        await self.client.start_notify(TX, self._receive)
        return self

    async def __aexit__(self, *_):
        if self.client.is_connected:
            await self.client.stop_notify(TX)

    def _receive(self, _characteristic, data):
        for frame in self.stream.feed(data, asyncio.get_running_loop().time()):
            if (frame.op, frame.txid) != self.waiting:
                continue
            try:
                self.inbox.put_nowait(frame)
            except asyncio.QueueFull:
                self.overflow = True

    async def request(self, op, payload=b""):
        self.next_id += 1
        packet = encode(Frame(VERSION, op, self.next_id, payload))
        self.waiting = op | 0x80, self.next_id
        try:
            for _ in range(3):
                self.stream = Stream()
                self.inbox = asyncio.Queue(maxsize=16)
                self.overflow = False
                if not self.client.is_connected:
                    raise ConfigError("Połączenie zostało zerwane.")
                for offset in range(0, len(packet), CHUNK):
                    # Błąd zapisu ATT kończy operację. Nie zgadujemy, ile bajtów doszło.
                    await asyncio.wait_for(
                        self.client.write_gatt_char(RX, packet[offset:offset + CHUNK], response=True), 5
                    )
                try:
                    frame = await asyncio.wait_for(self.inbox.get(), ACK_SECONDS)
                except TimeoutError:
                    continue
                if self.overflow or frame.version != VERSION or not frame.payload:
                    raise ConfigError("Nieprawidłowa odpowiedź sterownika.")
                code = frame.payload[0]
                if code:
                    raise ConfigError(RESULTS.get(code, f"Nieznany status {code}."))
                body = frame.payload[1:]
                if len(body) != (0 if op in (SET, CLOSE) else CONFIG.size):
                    raise ConfigError("Niekompletne potwierdzenie transakcji.")
                return body
            raise ConfigError("Brak ACK. Po SAVE wynik może być nieznany. Odczytaj ustawienia ponownie.")
        finally:
            self.waiting = None


async def discover(scanner):
    found = await scanner.discover(timeout=4, return_adv=True, service_uuids=[SERVICE])
    return [device for device, advertisement in found.values() if SERVICE in advertisement.service_uuids]


def select_device(devices, address):
    matches = [d for d in devices if address is None or d.address.casefold() == address.casefold()]
    if not matches:
        raise ConfigError("Brak sterownika. Otwórz fizycznie tryb CONFIG i sprawdź parowanie Windows.")
    if len(matches) != 1:
        raise ConfigError("Znaleziono kilka sterowników. Użyj scan i podaj --adres.")
    return matches[0]


async def persist_and_verify(connect, device, payload):
    disconnected = asyncio.Event()
    async with connect(device, disconnected) as client, Session(client) as session:
        await session.request(SET, payload)
        if await session.request(SAVE) != payload:
            raise ConfigError("Potwierdzenie SAVE różni się od wysłanych danych.")
        await session.request(CLOSE)
        await asyncio.wait_for(disconnected.wait(), 5)
    async with connect(device) as client, Session(client) as session:
        if await session.request(GET) != payload:
            raise ConfigError("Odczyt po ponownym połączeniu różni się od zapisanych ustawień.")


async def run_ble(args, payload):
    from bleak import BleakClient, BleakScanner
    from winrt.windows.devices.bluetooth.genericattributeprofile import GattProtectionLevel

    devices = await discover(BleakScanner)
    if args.command == "scan":
        for device in devices:
            print(f"{device.address}  {device.name or 'SmartFan v2'}")
        if not devices:
            raise ConfigError("Brak sterowników w trybie CONFIG.")
        return

    device = select_device(devices, args.adres)

    @asynccontextmanager
    async def connect(peer, disconnected=None):
        callback = None if disconnected is None else lambda _client: disconnected.set()
        async with BleakClient(peer, timeout=15, disconnected_callback=callback,
                               winrt={"use_cached_services": False}) as client:
            rx = client.services.get_characteristic(RX)
            tx = client.services.get_characteristic(TX)
            if rx is None or "write" not in rx.properties or tx is None or "notify" not in tx.properties:
                raise ConfigError("Nie znaleziono wymaganych charakterystyk GATT v2.")
            rx.obj.protection_level = GattProtectionLevel.ENCRYPTION_AND_AUTHENTICATION_REQUIRED
            yield client

    if args.command in {"set", "import"}:
        await persist_and_verify(connect, device, payload)
        print("Zapis potwierdzony odczytem po ponownym połączeniu.")
        return

    async with connect(device) as client, Session(client) as session:
        settings = config_decode(await session.request(GET))
    if args.command == "export":
        write_json(args.plik, settings)
    else:
        print(json.dumps(settings, ensure_ascii=False, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adres", help="Adres z polecenia scan, przy kilku sterownikach")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("scan", help="Wyszukaj sterowniki w trybie CONFIG")
    commands.add_parser("get", help="Odczytaj utrwalone ustawienia")
    for command, help_text in {
        "set": "Wyślij kompletną konfigurację, zapisz i zweryfikuj",
        "import": "Zaimportuj JSON, zapisz i zweryfikuj",
        "export": "Zapisz odczyt ustawień do nowego pliku",
        "defaults": "Zapisz szablon ustawień do nowego pliku",
        "validate": "Sprawdź plik lokalnie",
    }.items():
        commands.add_parser(command, help=help_text).add_argument("plik", type=Path)
    args = parser.parse_args()

    from bleak.exc import BleakError
    try:
        payload = config_encode(load_json(args.plik)) if args.command in {"set", "import", "validate"} else None
        if args.command == "defaults":
            write_json(args.plik, DEFAULTS)
        elif args.command == "validate":
            print("Poprawna konfiguracja v2.")
        else:
            asyncio.run(run_ble(args, payload))
    except (ConfigError, OSError, ValueError, BleakError, TimeoutError) as exc:
        print(f"Błąd: {exc or 'przekroczono czas operacji BLE'}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
