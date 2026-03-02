# *******************************************************************************
# Copyright (c) 2026 Contributors to the Eclipse Foundation
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License Version 2.0 which is available at
# https://www.apache.org/licenses/LICENSE-2.0
#
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************

#!/usr/bin/env python3
"""Analyze pcap files for SOME/IP-SD messages using scapy.

Parses a pcap file for SOME/IP-SD messages and generates a summary of
Offers, Finds, and Subscriptions per target host.

Targets: someipd (192.168.87.2) and sample_client (192.168.87.3)

Usage:
    python3 analyze_pcap_someip.py [pcap_file]
"""

import struct
import sys
import os
from collections import defaultdict

from scapy.all import rdpcap, IP, UDP, conf

# Suppress scapy verbosity
conf.verb = 0

# --- Configuration ---
TARGET_HOSTS = {"192.168.87.2": "someipd", "192.168.87.3": "sample_client"}

# Constants for SOME/IP-SD parsing
SOMEIP_SD_SERVICE_ID = 0xFFFF
SOMEIP_SD_METHOD_ID = 0x8100
SD_PORT = 30490

# SOME/IP-SD Entry Types
ENTRY_TYPE_FIND = 0x00
ENTRY_TYPE_OFFER = 0x01
ENTRY_TYPE_SUBSCRIBE = 0x06
ENTRY_TYPE_STOP_SUBSCRIBE_ACK = 0x07


def decode_sd_entry(data, offset=0):
    """Decode a single 16-byte SOME/IP-SD entry."""
    if len(data) < offset + 16:
        return None

    entry_type = data[offset]
    service_id = struct.unpack("!H", data[offset + 4 : offset + 6])[0]
    instance_id = struct.unpack("!H", data[offset + 6 : offset + 8])[0]

    eventgroup = None
    if entry_type in (ENTRY_TYPE_SUBSCRIBE, ENTRY_TYPE_STOP_SUBSCRIBE_ACK):
        eventgroup = struct.unpack("!H", data[offset + 14 : offset + 16])[0]
        if eventgroup in (0xFFFF, 0):
            eventgroup = None

    return {
        "type_id": entry_type,
        "service": service_id,
        "instance": instance_id,
        "eventgroup": eventgroup,
    }


def extract_sd_entries(payload):
    """Extract SOME/IP-SD entries from raw UDP payload bytes."""
    if len(payload) < 24:  # 16 SOME/IP header + 8 SD header minimum
        return []

    service_id = struct.unpack("!H", payload[0:2])[0]
    method_id = struct.unpack("!H", payload[2:4])[0]

    if service_id != SOMEIP_SD_SERVICE_ID or method_id != SOMEIP_SD_METHOD_ID:
        return []

    # SD header starts after 16-byte SOME/IP header
    sd_start = 16
    if len(payload) < sd_start + 8:
        return []

    entries_length = struct.unpack("!I", payload[sd_start + 4 : sd_start + 8])[0]
    entries_start = sd_start + 8
    entries_end = min(entries_start + entries_length, len(payload))

    entries = []
    offset = entries_start
    while offset + 16 <= entries_end:
        entry = decode_sd_entry(payload, offset)
        if entry:
            entries.append(entry)
        offset += 16

    return entries


def print_summary(ip, name, data):
    """Prints the formatted summary for a specific IP."""
    print("=" * 80)
    print(f"SUMMARY: {name} ({ip})")
    print("=" * 80)

    # OFFERS
    if data["offers"]:
        print("OFFERS:")
        for svc, inst, eg in sorted(data["offers"]):
            eg_str = f" with eventgroup 0x{eg:04x}" if eg else ""
            print(f"            Service 0x{svc:04x}.0x{inst:04x}{eg_str}")
    else:
        print("OFFERS:")
        print("            None detected")
    print()

    # FINDS
    if data["finds"]:
        print("FINDS:")
        for svc, inst in sorted(data["finds"]):
            print(f"            Service 0x{svc:04x}.0x{inst:04x}")
    else:
        print("FINDS:")
        print("            None detected")
    print()

    # SUBSCRIBES
    if data["subscribes"]:
        print("SUBSCRIBED SUCCESSFULLY TO:")
        for svc, inst, eg in sorted(data["subscribes"]):
            eg_str = f", eventgroup 0x{eg:04x}" if eg else ""
            print(f"            Service 0x{svc:04x}.0x{inst:04x}{eg_str}")
    else:
        print("SUBSCRIBED SUCCESSFULLY TO:")
        print("            None detected")
    print("\n")


def analyze(pcap_file: str) -> dict[str, dict[str, set]]:
    """Analyze a pcap file and return SOME/IP-SD data per host.

    Returns dict keyed by IP with values {"offers": set, "finds": set, "subscribes": set}.
    Offer/subscribe entries are tuples of (service_id, instance_id, eventgroup).
    Find entries are tuples of (service_id, instance_id).
    """
    host_data = defaultdict(
        lambda: {"offers": set(), "finds": set(), "subscribes": set()}
    )

    packets = rdpcap(pcap_file)

    for pkt in packets:
        if not pkt.haslayer(IP) or not pkt.haslayer(UDP):
            continue

        if pkt[UDP].dport != SD_PORT and pkt[UDP].sport != SD_PORT:
            continue

        src_ip = pkt[IP].src
        if src_ip not in TARGET_HOSTS:
            continue

        payload = bytes(pkt[UDP].payload)
        for entry in extract_sd_entries(payload):
            t_id = entry["type_id"]
            svc = entry["service"]
            inst = entry["instance"]
            eg = entry["eventgroup"]

            if t_id == ENTRY_TYPE_OFFER:
                host_data[src_ip]["offers"].add((svc, inst, eg))
            elif t_id == ENTRY_TYPE_FIND:
                host_data[src_ip]["finds"].add((svc, inst))
            elif t_id == ENTRY_TYPE_SUBSCRIBE:
                host_data[src_ip]["subscribes"].add((svc, inst, eg))

    # Only return IPs that had actual SD traffic
    return dict(host_data)


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 analyze_pcap_someip.py [pcap_file]")
        sys.exit(1)

    pcap_file = sys.argv[1]

    if not os.path.exists(pcap_file):
        print(f"Error: pcap file not found: {pcap_file}")
        sys.exit(1)

    print(f"Analyzing {pcap_file} for SOME/IP-SD traffic...")

    host_data = analyze(pcap_file)

    # Output Results
    print("\n")

    for ip in ("192.168.87.2", "192.168.87.3"):
        if ip in host_data:
            print_summary(ip, TARGET_HOSTS[ip], host_data[ip])
        else:
            print(f"No data found for {TARGET_HOSTS[ip]} ({ip})")


if __name__ == "__main__":
    main()
