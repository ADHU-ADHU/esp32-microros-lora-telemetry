#!/usr/bin/env python3
"""Compile and check the firmware's actual packet, CRC, and quantizer on a host.

Requires Python 3 and g++. Only the pure definitions are extracted from the
transmitter/receiver sketches. This does not compile Arduino firmware or test
radio communication, micro-ROS, timing, or the complete receiver callback.
Generated C++ and its executable live in an automatically cleaned temp folder.
"""

import argparse
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile


REPO_ROOT = Path(__file__).resolve().parents[1]
TRANSMITTER = Path('firmware/sx1278_imu_transmitter/sx1278_imu_transmitter.ino')
RECEIVER = Path('firmware/heltec_imu_receiver/heltec_imu_receiver.ino')
PACKET_PATTERN = r'^\s*struct\s+__attribute__\s*\(\(\s*packed\s*\)\)\s+ImuPkt\s*\{'
CRC_PATTERN = r'^\s*uint8_t\s+crc8_07\s*\([^)]*\)\s*\{'
QUANTIZER_PATTERN = r'^\s*static\s+inline\s+int16_t\s+f_to_i16\s*\([^)]*\)\s*\{'
BLOCK_TOKENS = re.compile(
    r'''//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}]''',
    re.DOTALL,
)


def extract_definition(source, pattern, *, struct_definition=False):
    """Extract one complete definition, ignoring braces in comments/strings."""
    matches = list(re.finditer(pattern, source, re.MULTILINE))
    if len(matches) != 1:
        raise ValueError(f'Expected one definition matching {pattern!r}; found {len(matches)}')
    match = matches[0]
    depth = 0
    for token in BLOCK_TOKENS.finditer(source, match.end() - 1):
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if depth == 0:
                end = token.end()
                if struct_definition:
                    terminator = re.match(r'\s*;', source[end:])
                    if terminator is None:
                        raise ValueError('ImuPkt definition is missing its semicolon')
                    end += terminator.end()
                return source[match.start():end]
    raise ValueError('Unbalanced braces in firmware definition')


def reference_crc(data):
    """Independent CRC-8/SMBUS reference using polynomial long division."""
    remainder = int.from_bytes(data, 'big') << 8
    while remainder.bit_length() > 8:
        remainder ^= 0x107 << (remainder.bit_length() - 9)
    return remainder


def build_harness(transmitter, receiver):
    tx_definitions = '\n\n'.join([
        extract_definition(transmitter, PACKET_PATTERN, struct_definition=True),
        extract_definition(transmitter, CRC_PATTERN),
        extract_definition(transmitter, QUANTIZER_PATTERN),
    ])
    rx_definitions = '\n\n'.join([
        extract_definition(receiver, PACKET_PATTERN, struct_definition=True),
        extract_definition(receiver, CRC_PATTERN),
    ])

    # Independent protocol expectations, rather than TX-to-RX comparison alone.
    fields = [
        ('sync', 0, 'uint8_t'), ('type', 1, 'uint8_t'), ('seq', 2, 'uint8_t'),
        ('lin_x', 3, 'int16_t'), ('lin_y', 5, 'int16_t'), ('lin_z', 7, 'int16_t'),
        ('ang_x', 9, 'int16_t'), ('ang_y', 11, 'int16_t'), ('ang_z', 13, 'int16_t'),
        ('crc', 15, 'uint8_t'),
    ]
    layout_checks = []
    for namespace in ('tx', 'rx'):
        layout_checks.append(
            f'static_assert(sizeof({namespace}::ImuPkt) == 16, "{namespace} packet must be 16 bytes");'
        )
        for field, offset, cpp_type in fields:
            layout_checks.extend([
                f'static_assert(offsetof({namespace}::ImuPkt, {field}) == {offset}, '
                f'"{namespace}.{field} offset changed");',
                f'static_assert(std::is_same_v<decltype({namespace}::ImuPkt{{}}.{field}), {cpp_type}>, '
                f'"{namespace}.{field} type changed");',
            ])

    if reference_crc(b'123456789') != 0xF4:
        raise ValueError('Independent CRC reference failed its standard check vector')
    payload = bytes((i * 73 + 19) & 0xFF for i in range(256))
    prefix_crcs = ', '.join(str(reference_crc(payload[:n])) for n in range(257))
    golden = struct.pack('<BBB6h', 0xAA, 0x01, 42, 1234, -2345, 32767, -32768, 0, 32767)
    golden += bytes([reference_crc(golden)])

    harness = r'''
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using std::size_t;
using std::int16_t;
using std::uint8_t;

namespace tx {
__TX_DEFINITIONS__
}
namespace rx {
__RX_DEFINITIONS__
}

__LAYOUT_CHECKS__

void require(bool condition, const char* explanation) {
    if (!condition) throw std::runtime_error(explanation);
}

int main() {
    try {
        const uint8_t* check = reinterpret_cast<const uint8_t*>("123456789");
        require(tx::crc8_07(check, 9) == 0xF4, "TX standard CRC vector failed");
        require(rx::crc8_07(check, 9) == 0xF4, "RX standard CRC vector failed");

        const uint8_t expected_crcs[] = {__PREFIX_CRCS__};
        uint8_t payload[256];
        for (size_t i = 0; i < sizeof(payload); ++i) {
            payload[i] = static_cast<uint8_t>(i * 73 + 19);
        }
        for (size_t n = 0; n <= sizeof(payload); ++n) {
            require(tx::crc8_07(payload, n) == expected_crcs[n], "TX prefix CRC mismatch");
            require(rx::crc8_07(payload, n) == expected_crcs[n], "RX prefix CRC mismatch");
        }

        struct QuantizationCase { float input; int16_t expected; };
        const QuantizationCase cases[] = {
            {0.0f, 0}, {1.2349f, 1234}, {-1.2349f, -1234},
            {0.0009f, 0}, {-0.0009f, 0},
            {1000.0f, 32767}, {-1000.0f, -32768},
            {180.0f, 32767}, {-180.0f, -32768},
        };
        for (const auto& item : cases) {
            require(tx::f_to_i16(item.input, 1000.0f) == item.expected,
                    "TX quantization/truncation/saturation mismatch");
        }

        tx::ImuPkt packet{};
        packet.sync = 0xAA;
        packet.type = 0x01;
        packet.seq = 42;
        packet.lin_x = tx::f_to_i16(1.2349f, 1000.0f);
        packet.lin_y = tx::f_to_i16(-2.3459f, 1000.0f);
        packet.lin_z = tx::f_to_i16(1000.0f, 1000.0f);
        packet.ang_x = tx::f_to_i16(-1000.0f, 1000.0f);
        packet.ang_y = tx::f_to_i16(-0.0009f, 1000.0f);
        packet.ang_z = tx::f_to_i16(180.0f, 1000.0f);
        packet.crc = tx::crc8_07(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet) - 1);

        const uint8_t golden[] = {__GOLDEN_BYTES__};
        require(std::memcmp(&packet, golden, sizeof(golden)) == 0,
                "TX bytes differ from little-endian ESP32 protocol expectations");
        rx::ImuPkt decoded{};
        std::memcpy(&decoded, golden, sizeof(decoded));
        require(decoded.sync == 0xAA && decoded.type == 0x01 && decoded.seq == 42,
                "RX header/sequence layout mismatch");
        require(decoded.lin_x == 1234 && decoded.lin_y == -2345 && decoded.lin_z == 32767 &&
                decoded.ang_x == -32768 && decoded.ang_y == 0 && decoded.ang_z == 32767,
                "RX signed telemetry field layout mismatch");
        require(rx::crc8_07(reinterpret_cast<const uint8_t*>(&decoded), sizeof(decoded) - 1) == decoded.crc,
                "RX rejected intact golden packet CRC");

        size_t detected_corruptions = 0;
        for (size_t byte = 0; byte < sizeof(decoded); ++byte) {
            for (unsigned bit = 0; bit < 8; ++bit) {
                rx::ImuPkt corrupted = decoded;
                auto* bytes = reinterpret_cast<uint8_t*>(&corrupted);
                bytes[byte] ^= static_cast<uint8_t>(1U << bit);
                require(rx::crc8_07(bytes, sizeof(corrupted) - 1) != corrupted.crc,
                        "RX CRC failed to detect a single-bit packet corruption");
                ++detected_corruptions;
            }
        }

        std::cout << "PASS: actual TX/RX layouts are 16 bytes; all 10 field types/offsets match.\n"
                  << "PASS: CRC-8 check 123456789 = 0xF4; 257 prefix cases match an independent reference.\n"
                  << "PASS: 9 quantizer cases cover truncation, saturation, and clipping of +/-180 yaw.\n"
                  << "PASS: exact little-endian golden packet and RX signed fields match.\n"
                  << "PASS: detected all " << detected_corruptions << " single-bit packet corruptions.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
'''
    replacements = {
        '__TX_DEFINITIONS__': tx_definitions,
        '__RX_DEFINITIONS__': rx_definitions,
        '__LAYOUT_CHECKS__': '\n'.join(layout_checks),
        '__PREFIX_CRCS__': prefix_crcs,
        '__GOLDEN_BYTES__': ', '.join(f'0x{byte:02X}' for byte in golden),
    }
    for marker, value in replacements.items():
        harness = harness.replace(marker, value)
    return harness


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='g++', help='C++ compiler executable (default: g++)')
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if compiler is None:
        parser.error(f'C++ compiler not found: {args.compiler}')

    try:
        transmitter = (REPO_ROOT / TRANSMITTER).read_text(encoding='utf-8')
        receiver = (REPO_ROOT / RECEIVER).read_text(encoding='utf-8')
        harness = build_harness(transmitter, receiver)
        with tempfile.TemporaryDirectory(prefix='imu-packet-check-') as directory:
            cpp_file = Path(directory) / 'packet_checks.cpp'
            executable = Path(directory) / 'packet_checks'
            cpp_file.write_text(harness, encoding='utf-8')
            build = subprocess.run(
                [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic',
                 str(cpp_file), '-o', str(executable)],
                capture_output=True, text=True, timeout=30,
            )
            if build.returncode:
                print(build.stdout + build.stderr, file=sys.stderr, end='')
                return build.returncode
            checks = subprocess.run(
                [str(executable)], capture_output=True, text=True, timeout=5,
            )
            print(checks.stdout, end='')
            print(checks.stderr, file=sys.stderr, end='')
            if checks.returncode:
                return checks.returncode
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1

    print('Host packet checks only; Arduino firmware and hardware were not built or tested.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
