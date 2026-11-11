"""Keep IP addresses out of the runtime's multiplayer log lines (rexruntime.dll).

The runtime's "[netplay]" lines name this PC's LAN / virtual-LAN addresses and
other players' addresses. Players attach their logs to public bug reports, so
the release rewrites those format strings in place, same length: the event
stays ("joining X's Private Match"), only trailing placeholders are removed
or the whole argument list is masked ("address hidden"), never one in the
middle, so the remaining placeholders keep their arguments (fmt ignores unused
trailing arguments); the freed bytes become spaces at the end of the line.

    python -I tools/Redact-NetplayLog.py <rexruntime.dll>   -> exit 1 if a string is missing
"""
import sys

EDITS = [
    ("[netplay] answering Private Match searches on {}:{}", "[netplay] answering Private Match searches"),
    ("[netplay] joining {}'s Private Match at {}", "[netplay] joining {}'s Private Match"),
    ("[netplay] found {}'s Private Match at {}", "[netplay] found {}'s Private Match"),
    ("[netplay] no Private Match answered on {}", "[netplay] no Private Match answered"),
    ("[netplay] answered a Private Match search from {}", "[netplay] answered a Private Match search"),
    ("[netplay] Invite Friends while hosting at {}", "[netplay] Invite Friends while hosting"),
    ("[netplay] server browser: {} match(es) on {}", "[netplay] server browser: {} match(es)"),
    ("[netplay] QoS probe of {}: {}", "[netplay] QoS probe (hidden)"),
    ("[netplay] QoS probe of {} through the online server: {}", "[netplay] QoS probe via the online server (hidden)"),
    ('[netplay] online server {}: this PC is {} in room "{}" (seen from {}:{})', '[netplay] online server connected (addresses hidden)'),
    ("[netplay] Private Match addresses: {} (searches also broadcast to: {})",
     "[netplay] Private Match addresses hidden"),
    ("[netplay] joining the Private Match hosted at {} (flags {:08X})", "[netplay] joining the Private Match (address hidden)"),
    ("[netplay] searching {} for a Private Match", "[netplay] searching for a Private Match"),
    ("[net] bind {}:{} (Private Match, type {})", "[net] bind (Private Match)"),
    ("[net] bind {}.{}.{}.{}:{} kept on 127.0.0.1 (net_loopback_only)", "[net] bind kept on loopback (net_loopback_only)"),
    ("[net] recvfrom on port {} <- {}:{} ({} bytes)", "[net] recvfrom on port {}"),
    ("[net] relay recvfrom on port {} <- {}:{} ({} bytes)", "[net] relay recvfrom on port {}"),
    ("[net] relay sendto from port {} -> {}:{} ({} bytes){}", "[net] relay sendto from port {}"),
    ("[net] sendto from port {} -> {}:{} ({} bytes){}", "[net] sendto from port {}"),
    ('[netplay] Private Match addresses: {} (online server {}, room "{}")',
     '[netplay] Private Match addresses hidden (online server)'),
]


def main(path):
    data = bytearray(open(path, 'rb').read())
    missing = 0
    for old, new in EDITS:
        o = old.encode() + b'\x00'
        n = new.encode().ljust(len(old), b' ') + b'\x00'
        assert len(n) == len(o), old
        count = data.count(o)
        if count == 0 and data.count(n) > 0:
            continue  # (already redacted)
        if count == 0:
            print(f'missing: {old}')
            missing += 1
            continue
        data = bytearray(bytes(data).replace(o, n))
        print(f'{count}x {old!r} -> {new!r}')
    open(path, 'wb').write(data)
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
