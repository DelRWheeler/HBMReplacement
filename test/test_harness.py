#!/usr/bin/env python3
"""
FIT7 Load Cell Simulator — Test Harness

Connects to the FIT7 simulator via PTY serial port and exercises
the full command set used by the DCH overhead sizing system.

Usage:
    python3 test_harness.py <pty_path>
    python3 test_harness.py /dev/pts/5

The PTY path is printed by the simulator at startup.
"""

import sys
import serial
import time
import struct
import argparse


class FIT7TestHarness:
    def __init__(self, port, baudrate=38400, timeout=0.5):
        self.ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=timeout,
        )
        self.ser.reset_input_buffer()
        self.pass_count = 0
        self.fail_count = 0
        print(f"Connected to {port} at {baudrate} baud")

    def close(self):
        self.ser.close()

    def send_command(self, cmd):
        """Send a command (with semicolon terminator) and return the response."""
        # Strip any existing trailing semicolons, then add exactly one
        cmd = cmd.rstrip(";") + ";"
        self.ser.reset_input_buffer()
        self.ser.write(cmd.encode("ascii"))
        self.ser.flush()
        # Wait for the device to process and respond
        time.sleep(0.15)
        # Read with the serial timeout (0.5s) — read until we get a full line
        response = self.ser.readline()
        return response.decode("ascii", errors="replace").strip()

    def send_no_response(self, cmd):
        """Send a command that expects no response (STP, RES)."""
        if not cmd.endswith(";"):
            cmd += ";"
        self.ser.write(cmd.encode("ascii"))
        time.sleep(0.1)
        self.ser.reset_input_buffer()

    def check(self, test_name, result, expected):
        """Check a test result."""
        # Normalize: strip leading + and zeros for numeric comparison
        r = result.strip()
        e = expected.strip()
        passed = r == e
        if not passed:
            # Try numeric comparison
            try:
                passed = int(r) == int(e)
            except ValueError:
                pass
        if passed:
            self.pass_count += 1
            print(f"  PASS: {test_name}")
        else:
            self.fail_count += 1
            print(f"  FAIL: {test_name}")
            print(f"        Expected: {repr(expected)}")
            print(f"        Got:      {repr(result)}")

    def check_ok(self, test_name, result):
        """Check that response is the success code '0'."""
        self.check(test_name, result, "0")

    # ===== Test Sequences =====

    def test_idn(self):
        """Test IDN query — matches the init sequence in HBMLoadCell.cpp."""
        print("\n--- IDN Query ---")
        resp = self.send_command("IDN?;")
        # Should contain comma-separated identification
        if "," in resp and len(resp) > 5:
            self.pass_count += 1
            print(f"  PASS: IDN response: {resp}")
        else:
            self.fail_count += 1
            print(f"  FAIL: IDN response: {resp}")

    def test_init_sequence(self):
        """Test the full initialization sequence from HBMLoadCell::init_adc()."""
        print("\n--- Init Sequence (HBMLoadCell::init_adc) ---")

        # STR 1 — enable termination resistor
        resp = self.send_command("STR 1;")
        self.check_ok("STR 1", resp)

        # BDR? — check baud rate
        resp = self.send_command("BDR?;")
        self.check("BDR? = 38400,0", resp, "38400,0")

        # COF 8 — set output format
        resp = self.send_command("COF 8;")
        self.check_ok("COF 8", resp)

        # Verify COF
        resp = self.send_command("COF?;")
        self.check("COF? = 8", resp, "+0000008")

        # CSM 1 — enable checksum
        resp = self.send_command("CSM 1;")
        self.check_ok("CSM 1", resp)

        # Verify CSM
        resp = self.send_command("CSM?;")
        self.check("CSM? = 1", resp, "+0000001")

    def test_settings_read(self):
        """Test reading all settings — matches HBMCheckSettings first-time read."""
        print("\n--- Settings Read (HBMCheckSettings) ---")

        settings = [
            ("ASF?", "ASF"),
            ("FMD?", "FMD"),
            ("ICR?", "ICR"),
            ("CWT?", "CWT"),
            ("LDW?", "LDW"),
            ("LWT?", "LWT"),
            ("NOV?", "NOV"),
            ("RSN?", "RSN"),
            ("MTD?", "MTD"),
            ("LIC?", "LIC"),
            ("ZTR?", "ZTR"),
            ("ZSE?", "ZSE"),
            ("TRC?", "TRC"),
        ]

        for cmd, name in settings:
            resp = self.send_command(cmd)
            if resp and resp != "?":
                self.pass_count += 1
                print(f"  PASS: {name} = {resp}")
            else:
                self.fail_count += 1
                print(f"  FAIL: {name} returned: {resp}")

    def test_settings_write(self):
        """Test changing ASF setting (the main runtime-changeable parameter)."""
        print("\n--- Settings Write ---")

        # Change ASF from 6 to 4
        resp = self.send_command("ASF 4;")
        self.check_ok("ASF 4", resp)

        resp = self.send_command("ASF?;")
        self.check("ASF? = 4", resp, "+0000004")

        # Change back to 6
        resp = self.send_command("ASF 6;")
        self.check_ok("ASF 6", resp)

        # Test invalid ASF (< 2)
        resp = self.send_command("ASF 1;")
        self.check("ASF 1 rejected", resp, "?")

    def test_password_protected(self):
        """Test password-protected settings (CWT, LDW, LWT, LIC)."""
        print("\n--- Password Protected Settings ---")

        # Try setting CWT without password — should fail
        resp = self.send_command("CWT 500000;")
        self.check("CWT without password rejected", resp, "?")

        # Send password
        resp = self.send_command('SPW"AED";')
        self.check_ok("SPW password accepted", resp)

        # Now set CWT
        resp = self.send_command("CWT 500000;")
        self.check_ok("CWT 500000 with password", resp)

        # Verify
        resp = self.send_command("CWT?;")
        # CWT returns two values
        if "500000" in resp:
            self.pass_count += 1
            print(f"  PASS: CWT? = {resp}")
        else:
            self.fail_count += 1
            print(f"  FAIL: CWT? = {resp}")

        # Restore default
        resp = self.send_command("CWT 1000000;")
        self.check_ok("CWT restored to 1000000", resp)

    def test_tdd_save(self):
        """Test saving parameters to storage."""
        print("\n--- TDD1 Save ---")
        resp = self.send_command("TDD1;")
        self.check_ok("TDD1 save", resp)

    def test_streaming(self):
        """Test starting and stopping continuous measurement output."""
        print("\n--- Streaming (MSV?0 / STP) ---")

        # Start streaming
        self.ser.reset_input_buffer()
        self.ser.write(b"MSV?0;")
        time.sleep(1.0)  # Collect 1 second of data

        # Read whatever came in
        data = self.ser.read(self.ser.in_waiting or 4096)

        # Stop streaming
        self.ser.write(b"STP;")
        time.sleep(0.2)
        self.ser.reset_input_buffer()

        if len(data) > 20:
            # Count CRLF-terminated frames
            frames = data.split(b"\r\n")
            frame_count = len([f for f in frames if len(f) >= 4])
            print(f"  Received {len(data)} bytes, ~{frame_count} frames in 1 second")
            if frame_count > 10:
                self.pass_count += 1
                print(f"  PASS: Streaming active ({frame_count} frames/sec)")
            else:
                self.fail_count += 1
                print(f"  FAIL: Too few frames ({frame_count})")
        else:
            self.fail_count += 1
            print(f"  FAIL: No streaming data received ({len(data)} bytes)")

        # Verify we can send commands after stopping
        time.sleep(0.2)
        resp = self.send_command("IDN?;")
        if "," in resp:
            self.pass_count += 1
            print(f"  PASS: Commands work after STP")
        else:
            self.fail_count += 1
            print(f"  FAIL: Commands broken after STP: {resp}")

    def test_filter_change_during_stream(self):
        """Test changing ASF filter while streaming."""
        print("\n--- Filter Change During Stream ---")

        # Start streaming
        self.ser.reset_input_buffer()
        self.ser.write(b"MSV?0;")
        time.sleep(0.5)

        # Change filter mid-stream
        self.ser.write(b"STP;")
        time.sleep(0.1)
        self.ser.reset_input_buffer()

        resp = self.send_command("ASF 8;")
        self.check_ok("ASF 8 during idle", resp)

        # Resume streaming
        self.ser.reset_input_buffer()
        self.ser.write(b"MSV?0;")
        time.sleep(0.5)

        data = self.ser.read(self.ser.in_waiting or 4096)

        # Stop
        self.ser.write(b"STP;")
        time.sleep(0.2)
        self.ser.reset_input_buffer()

        if len(data) > 20:
            self.pass_count += 1
            print(f"  PASS: Streaming after filter change ({len(data)} bytes)")
        else:
            self.fail_count += 1
            print(f"  FAIL: No data after filter change")

        # Restore ASF
        resp = self.send_command("ASF 6;")
        self.check_ok("ASF restored to 6", resp)

    def run_all(self):
        """Run the full test suite."""
        print("=" * 50)
        print("FIT7 Simulator Test Harness")
        print("=" * 50)

        self.test_idn()
        self.test_init_sequence()
        self.test_settings_read()
        self.test_settings_write()
        self.test_password_protected()
        self.test_tdd_save()
        self.test_streaming()
        self.test_filter_change_during_stream()

        print("\n" + "=" * 50)
        print(f"Results: {self.pass_count} passed, {self.fail_count} failed")
        print("=" * 50)

        return self.fail_count == 0


def main():
    parser = argparse.ArgumentParser(description="FIT7 Simulator Test Harness")
    parser.add_argument("port", help="PTY path (e.g., /dev/pts/5)")
    parser.add_argument("--baud", type=int, default=38400, help="Baud rate")
    args = parser.parse_args()

    harness = FIT7TestHarness(args.port, args.baud)
    try:
        success = harness.run_all()
        sys.exit(0 if success else 1)
    finally:
        harness.close()


if __name__ == "__main__":
    main()
