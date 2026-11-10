import asyncio
from contextlib import asynccontextmanager
import copy
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "client"))
import smartfan as sf


class SettingsTests(unittest.TestCase):
    def test_strict_import_and_export(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            sf.write_json(path, sf.DEFAULTS)
            data = sf.load_json(path)
            self.assertEqual(sf.config_decode(sf.config_encode(data)), sf.DEFAULTS)
            with self.assertRaises(FileExistsError):
                sf.write_json(path, sf.DEFAULTS)
            path.write_text('{"wersja":2,"wersja":2}', encoding="utf-8")
            with self.assertRaises(ValueError):
                sf.load_json(path)

        for key, value in (
            ("wersja", True), ("wersja", 3), ("reczna", -1), ("reczna", 101),
            ("filtr_ms", 10001), ("wzrost_pkt_s", 0), ("spadek_pkt_s", 10.0),
            ("nastawy", [45, 30, 60, 75, 95]), ("progi_bpm", [100, 102, 145, 165, 200]),
            ("dodatkowe_pole", 1),
        ):
            settings = dict(sf.DEFAULTS, **{key: value})
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                sf.config_encode(settings)
        with self.assertRaises(ValueError):
            sf.config_decode(b"\x00" * 29)

    def test_cpp_wire_vector(self):
        exe = os.environ.get("SMARTFAN_TEST_EXE")
        self.assertTrue(exe, "Uruchom przez scripts/Test.ps1")
        reference = subprocess.check_output([exe, "--vector"], text=True).strip()
        packet = sf.encode(sf.Frame(sf.VERSION, sf.SET, 42, sf.config_encode(sf.DEFAULTS)))
        self.assertEqual(packet.hex(), reference)

    def test_stream_resync(self):
        expected = sf.Frame(sf.VERSION, sf.GET, 7, b"")
        packet = sf.encode(expected)
        broken = packet[:-1] + bytes([packet[-1] ^ 1])
        stream = sf.Stream()
        self.assertEqual(stream.feed(packet[:5], 5), [])
        self.assertEqual(stream.feed(packet[5:], 7), [])
        self.assertEqual(stream.feed(b"xS" + broken + packet[:3], 8), [])
        self.assertEqual(stream.feed(packet[3:] + packet, 8.5), [expected, expected])


class SimulatedServer:
    def __init__(self):
        self.active = sf.config_encode(sf.DEFAULTS)
        self.sessions = 0
        self.saves = 0
        self.request_ids = []
        self.drop_first_save_reply = False
        self.failure = None

    @asynccontextmanager
    async def connect(self, _device, disconnected=None):
        self.sessions += 1
        client = SimulatedClient(self, disconnected)
        yield client
        client.is_connected = False


class SimulatedClient:
    def __init__(self, server, disconnected):
        self.server = server
        self.is_connected = True
        self.stream = sf.Stream()
        self.disconnect = disconnected
        self.last = None
        self.response = None
        self.candidate = None
        self.callback = None

    async def start_notify(self, uuid, callback):
        assert uuid == sf.TX
        self.callback = callback

    async def stop_notify(self, uuid):
        assert uuid == sf.TX
        self.callback = None

    async def write_gatt_char(self, uuid, fragment, response):
        assert uuid == sf.RX and response is True and len(fragment) <= 20
        now = asyncio.get_running_loop().time()
        for frame in self.stream.feed(fragment, now):
            self.server.request_ids.append((self.server.sessions, frame.op, frame.txid))
            if self.server.failure == "link":
                self.is_connected = False
                raise OSError("rozłączenie")
            if frame != self.last:
                self.last = frame
                payload = b"\0"
                if frame.op == sf.SET:
                    sf.config_decode(frame.payload)
                    self.candidate = frame.payload
                if frame.op == sf.SAVE:
                    assert self.candidate is not None
                    if self.server.failure == "storage":
                        payload = b"\x04"
                    else:
                        self.server.saves += 1
                        self.server.active = self.candidate
                        payload += self.candidate
                if frame.op == sf.GET:
                    payload += self.server.active
                    if self.server.failure == "short":
                        payload = payload[:-1]
                    if self.server.failure == "mismatch" and self.server.sessions > 1:
                        payload = b"\0" + sf.config_encode(sf.DEFAULTS)
                self.response = sf.encode(sf.Frame(sf.VERSION, frame.op | 0x80, frame.txid, payload))
                if frame.op == sf.SAVE and self.server.drop_first_save_reply:
                    continue
            for offset in range(0, len(self.response), 7):
                self.callback(None, self.response[offset:offset + 7])
            if frame.op == sf.CLOSE:
                self.is_connected = False
                if self.disconnect is not None:
                    self.disconnect.set()


class SessionTests(unittest.IsolatedAsyncioTestCase):
    async def test_set_save_retry_close_get(self):
        server = SimulatedServer()
        server.drop_first_save_reply = True
        old_timeout = sf.ACK_SECONDS
        sf.ACK_SECONDS = 0.02
        try:
            settings = copy.deepcopy(sf.DEFAULTS)
            settings["reczna"] = 80
            payload = sf.config_encode(settings)
            await sf.persist_and_verify(server.connect, object(), payload)
        finally:
            sf.ACK_SECONDS = old_timeout
        self.assertEqual(server.active, payload)
        self.assertEqual(server.sessions, 2)
        self.assertEqual(server.saves, 1)
        self.assertEqual(server.request_ids, [(1, sf.SET, 1), (1, sf.SAVE, 2),
                                             (1, sf.SAVE, 2), (1, sf.CLOSE, 3), (2, sf.GET, 1)])

    async def test_unsuccessful_transactions(self):
        for reason, error, count in (
            ("link", OSError, 1), ("storage", sf.ConfigError, 1),
            ("short", sf.ConfigError, 2), ("mismatch", sf.ConfigError, 2),
        ):
            server = SimulatedServer()
            server.failure = reason
            data = sf.config_encode(dict(sf.DEFAULTS, reczna=60))
            with self.subTest(reason=reason), self.assertRaises(error):
                await sf.persist_and_verify(server.connect, object(), data)
            self.assertEqual(server.sessions, count)


if __name__ == "__main__":
    unittest.main()
