# LiteNix Networking (Phase 9.1)

This document describes LiteNix's networking layer and the Phase 9.1 work
that introduced a real DHCP client and a system hostname.

## Components

| Component | Path | Purpose |
| --- | --- | --- |
| `dhcpcd` | `/sbin/dhcpcd` | DHCPv4 client (DISCOVER/OFFER/REQUEST/ACK) |
| `hostname` | `/bin/hostname` | get/set the kernel hostname |
| `ifconfig` | `/sbin/ifconfig` | set IP/netmask/gw (still no parse output) |
| `/proc/net/config` | — | read current network config |
| `/proc/sys/kernel/hostname` | — | read/write kernel hostname (also feeds `uname -n`) |
| `/etc/resolv.conf` | — | nameserver(s) populated by dhcpcd |

## DHCP client

`/sbin/dhcpcd` performs the standard four-way handshake (RFC 2131):

1. **DISCOVER** — broadcast to 255.255.255.255:67 from UDP port 68
2. **OFFER** — first reply, parsed for server-id and offered IP
3. **REQUEST** — accept the offer; sent as broadcast
4. **ACK** — final config; lease applied

If no OFFER is received within 4 seconds the client exits with status 2
so `rcS.sh` can fall back to the static `10.0.2.15` configuration that was
hard-coded before this phase.

On a successful ACK the client:

- invokes `/sbin/ifconfig eth0 <ip> netmask <mask> gw <router>`
- writes `/etc/resolv.conf` with the offered DNS servers
- invokes `/bin/hostname <name>` if the server offered one

The packet builder, option parser, and the lease struct are all in
`user/libc-lite/libc_lite.c` so the same code path is exercised by the
in-kernel test (init Test 34) and the live `dhcpcd` binary.

## `dhcp_parse_reply`

```c
int dhcp_parse_reply(const void *pkt, size_t pkt_len,
                     const void *opts, size_t opts_len,
                     uint32_t expected_xid, struct dhcp_lease *out_lease);
```

Returns 0 on success, -1 on malformed input (wrong op, wrong magic, wrong
XID, or option section too short). Populates `out_lease` with the parsed
fields and `*_has_*` flags so callers can distinguish between
"server omitted the option" and "server sent length 0".

Supports these options:

| Code | Field | Has-flag |
| --- | --- | --- |
| 1  | subnet mask | `has_subnet_mask` |
| 3  | router | `has_router` |
| 6  | DNS server | `has_dns` |
| 12 | hostname | `has_hostname` |
| 15 | domain name | `has_domain` |
| 51 | lease time | `lease_seconds` |
| 53 | msg type | `msg_type` |
| 54 | server id | `has_server_id` |
| 58 | T1 renewal | `t1_seconds` |
| 59 | T2 rebind | `t2_seconds` |

## Hostname

The kernel keeps the hostname in a global `char kernel_hostname[65]`
(default `"litenix"`) and exposes it at `/proc/sys/kernel/hostname`. The
`hostname` binary:

- `hostname` reads the proc file and prints the value
- `hostname <name>` writes the proc file (truncates trailing newline)

`sys_uname` also reads from this global, so `uname -n` and the
`/proc/sys/kernel/hostname` value stay in sync with the binary.

## rcS.sh integration

`/etc/init.d/rcS` now does:

```sh
if [ -f "/etc/hostname" ]; then
    hostname $(cat /etc/hostname)
fi

echo "Attempting DHCP on eth0..."
/sbin/dhcpcd 2>/dev/null
if [ $? -ne 0 ]; then
    echo "DHCP failed, using static configuration..."
    /sbin/ifconfig eth0 10.0.2.15 netmask 255.255.255.0 gw 10.0.2.2
fi
```

The exit-status check on `/sbin/dhcpcd` matches the values documented above
(0 = success, 1 = protocol error, 2 = timeout/fallback).

## Verification

`make verify-boot` now requires the boot log to contain:

```
NET_BOOT: all tests passed
```

This is printed after init's `Test 34` covers:

1. The kernel `/proc/sys/kernel/hostname` file exists
2. The `hostname` binary round-trips set → read
3. The kernel's stored hostname is the value the binary set
4. `/sbin/dhcpcd` exists
5. The DHCP reply parser extracts IP, router, DNS, lease, and server-id
   from a known-good fixture packet
6. The parser rejects a fixture with the wrong transaction ID
7. The parser rejects a fixture with the wrong magic cookie

## Known limitations

- The kernel's IPv4 receive path only accepts DHCP replies whose
  `ip->dest[3] == 255` or `ip->dest == my_ip`. QEMU's default
  `user-mode networking` does not actually run a DHCP server (it just hands
  the client a static address on link-up), so the live handshake only
  works against a real DHCP server (such as the one in `-netdev user,dhcp`
  mode or an external network).
- The dhcpcd timeout is fixed at 4 seconds. A real distribution would
  start a background retry with exponential backoff.
- No support for DHCPv6 or for renewing/rebinding leases.

## Future work

- A real boot-time DHCP server for QEMU so verify-boot can exercise the
  live handshake
- DHCPv6 (RFC 8415) for IPv6 autoconfiguration
- Hostname resolution via `/etc/hosts` (currently only `/etc/resolv.conf`
  is honored by `dns_resolve.c`)
- Drop static-IP fallback once DHCP is proven
- Persist the lease to a small file so the boot is faster on the second
  boot (no waiting for the server)
