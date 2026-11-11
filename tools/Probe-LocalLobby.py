"""Read a loopback-only SDK lobby advertisement and QoS response.

This does not join a game. Protocol constants/layout come from the reviewed
local xam_netplay.cpp; a successful response proves discovery only.
"""
import argparse
import ipaddress
import json
from pathlib import Path
import socket
import struct
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--source', default='127.0.0.2')
    parser.add_argument('--wait-seconds', type=float, default=2)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    for address in (args.host, args.source):
        ip = ipaddress.ip_address(address)
        if ip.version != 4 or not ip.is_loopback:
            parser.error('This diagnostic is restricted to IPv4 loopback addresses.')
    if not 0 < args.wait_seconds <= 120:
        parser.error('Wait must be greater than zero and at most 120 seconds.')
    target = (args.host, 3076)
    result = {'host': args.host, 'source': args.source, 'discovery': False,
              'qos': False, 'scope': 'Discovery/QoS only; no game session joined.'}
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((args.source, 0))
        sock.settimeout(0.5)
        deadline = time.monotonic() + args.wait_seconds
        while time.monotonic() < deadline:
            sock.sendto(struct.pack('>II', 0x50444D51, 1), target)
            try:
                data, peer = sock.recvfrom(2048)
            except (socket.timeout, ConnectionResetError):
                time.sleep(0.25)
                continue
            if peer != target or len(data) < 92 or struct.unpack_from('>II', data) != (0x50444D41, 1):
                continue
            result.update(discovery=True, reply_bytes=len(data),
                          host_name=data[76:92].split(b'\0', 1)[0].decode('utf-8', errors='replace'),
                          advertised_address=socket.inet_ntoa(data[16:20]))
            if len(data) >= 136:
                result.update(players=data[92], slots=data[93], in_progress=bool(data[94] & 1))
            break
        if result['discovery']:
            sock.sendto(struct.pack('>II', 0x50445151, 1), target)
            try:
                data, peer = sock.recvfrom(2048)
                if peer == target and len(data) >= 12 and struct.unpack_from('>II', data) == (0x50445141, 1):
                    length = struct.unpack_from('>I', data, 8)[0]
                    if length <= len(data)-12:
                        result.update(qos=True, qos_bytes=length, qos_data_hex=data[12:12+length].hex())
            except (socket.timeout, ConnectionResetError):
                pass
    output = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(output, encoding='utf-8')
    print(output)
    return 0 if result['discovery'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
