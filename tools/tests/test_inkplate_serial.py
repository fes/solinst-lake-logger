import unittest

from tools import inkplate_serial


class InkplateSerialProtocolTests(unittest.TestCase):
    def test_command_frame_round_trip(self):
        encoded = inkplate_serial.encode_frame(42, "COMMAND", "status")
        self.assertEqual(encoded, "@1|42|COMMAND|status*C8BC\n")
        self.assertEqual(
            inkplate_serial.decode_frame(encoded),
            inkplate_serial.Frame(1, 42, "COMMAND", "status"),
        )

    def test_rejects_corruption_and_reserved_delimiters(self):
        with self.assertRaisesRegex(inkplate_serial.ProtocolError, "checksum mismatch"):
            inkplate_serial.decode_frame("@1|42|COMMAND|reboot*AD89\n")
        with self.assertRaisesRegex(inkplate_serial.ProtocolError, "reserved delimiter"):
            inkplate_serial.encode_frame(42, "COMMAND", "bad|command")

    def test_rejects_reserved_or_overflowed_sequence(self):
        for sequence in (0, -1, 0x100000000):
            with self.subTest(sequence=sequence):
                with self.assertRaisesRegex(inkplate_serial.ProtocolError, "sequence"):
                    inkplate_serial.encode_frame(sequence, "COMMAND", "status")


if __name__ == "__main__":
    unittest.main()
