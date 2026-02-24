# *******************************************************************************
# Copyright (c) 2025 Contributors to the Eclipse Foundation
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
"""
AI generated proof of concept, has been tested and verified to work correctly for the limited configuration in this project.
Scapy can simplify the parsing but requires additional dependencies and setup.
Targets: someipd (192.168.87.2) and sample_client (192.168.87.3)

This script parses a pcap file for SOME/IP-SD messages and generates
a summary of Offers, Finds, and Subscriptions

Usage:
    python3 analyze_someip_sd.py [pcap_file]
"""

import struct
import subprocess
import re
import sys
import os
from collections import defaultdict

# --- Configuration ---
TARGET_HOSTS = {"192.168.87.2": "someipd", "192.168.87.3": "sample_client"}

# Constants for SOME/IP-SD parsing
SOMEIP_SD_SERVICE_ID = 0xFFFF
SOMEIP_SD_METHOD_ID = 0x8100

# SOME/IP-SD Entry Types
ENTRY_TYPE_FIND = 0x00
ENTRY_TYPE_OFFER = 0x01
ENTRY_TYPE_SUBSCRIBE = 0x06
ENTRY_TYPE_STOP_SUBSCRIBE_ACK = 0x07

TYPE_NAMES = {
    ENTRY_TYPE_FIND: "FindService",
    ENTRY_TYPE_OFFER: "OfferService",
    ENTRY_TYPE_SUBSCRIBE: "Subscribe",
    ENTRY_TYPE_STOP_SUBSCRIBE_ACK: "StopSubscribe/Ack",
}

# --- Parsing Functions ---


def decode_someip_sd_entry(data, offset=0):
    """Decode SOME/IP-SD entry at given offset."""
    if len(data) < offset + 16:
        return None

    entry_type = data[offset]
    service_id = struct.unpack("!H", data[offset + 4 : offset + 6])[0]
    instance_id = struct.unpack("!H", data[offset + 6 : offset + 8])[0]

    entry_name = TYPE_NAMES.get(entry_type, f"Type-{entry_type:02x}")

    # For eventgroup entries (Subscribe), eventgroup is at bytes 14-15
    eventgroup = None
    if entry_type in (ENTRY_TYPE_SUBSCRIBE, ENTRY_TYPE_STOP_SUBSCRIBE_ACK):
        eventgroup = struct.unpack("!H", data[offset + 14 : offset + 16])[0]
        if eventgroup == 0xFFFF or eventgroup == 0:
            eventgroup = None

    return {
        "type": entry_name,
        "type_id": entry_type,
        "service": service_id,
        "instance": instance_id,
        "eventgroup": eventgroup,
    }


def parse_tcpdump_hex_output(output):
    """Parse tcpdump -XX output and extract packet information."""
    packets = []
    current_packet = None
    hex_lines = []

    for line in output.split("\n"):
        if re.match(r"^\d{2}:\d{2}:\d{2}", line):
            if current_packet and hex_lines:
                current_packet["raw_bytes"] = parse_hex_lines(hex_lines)
                packets.append(current_packet)
                hex_lines = []
            current_packet = parse_packet_header(line)
        elif line.strip().startswith("0x"):
            hex_lines.append(line)

    if current_packet and hex_lines:
        current_packet["raw_bytes"] = parse_hex_lines(hex_lines)
        packets.append(current_packet)

    return packets


def parse_packet_header(line):
    """Parse tcpdump packet header line."""
    packet = {"src_ip": None, "dst_ip": None, "timestamp": None}
    ts_match = re.match(r"^(\d{2}:\d{2}:\d{2}\.\d+)", line)
    if ts_match:
        packet["timestamp"] = ts_match.group(1)

    ip_match = re.search(
        r"IP\s+(\d+\.\d+\.\d+\.\d+)\.(\d+)\s+>\s+(\d+\.\d+\.\d+\.\d+)\.(\d+)", line
    )
    if ip_match:
        packet["src_ip"] = ip_match.group(1)
        packet["src_port"] = int(ip_match.group(2))
        packet["dst_ip"] = ip_match.group(3)
        packet["dst_port"] = int(ip_match.group(4))
    return packet


def parse_hex_lines(hex_lines):
    """Parses tcpdump hex dump lines into raw bytes."""
    hex_data = []

    for line in hex_lines:
        # Ensure line has an offset (e.g., "0x0000:")
        if ":" not in line:
            continue

        # Get content after the offset colon
        content = line.split(":", 1)[1]

        # Collect hex chunks (2 or 4 chars), stop when we hit the ASCII visualization
        for part in content.split():
            if len(part) in (2, 4) and all(c in "0123456789abcdefABCDEF" for c in part):
                hex_data.append(part)
            else:
                break  # Stop reading this line as we reached the ASCII column

    return bytes.fromhex("".join(hex_data))


def extract_someip_sd_entries(raw_bytes):
    """Extract SOME/IP-SD entries from raw packet bytes."""
    if len(raw_bytes) < 58:
        return []

    ip_header_start = 14
    ip_version_ihl = raw_bytes[ip_header_start]
    ip_header_len = (ip_version_ihl & 0x0F) * 4

    someip_start = ip_header_start + ip_header_len + 8  # +8 for UDP
    if len(raw_bytes) < someip_start + 16:
        return []

    service_id = struct.unpack("!H", raw_bytes[someip_start : someip_start + 2])[0]
    method_id = struct.unpack("!H", raw_bytes[someip_start + 2 : someip_start + 4])[0]

    if service_id != SOMEIP_SD_SERVICE_ID or method_id != SOMEIP_SD_METHOD_ID:
        return []

    sd_start = someip_start + 16
    if len(raw_bytes) < sd_start + 8:
        return []

    entries_length = struct.unpack("!I", raw_bytes[sd_start + 4 : sd_start + 8])[0]
    entries_start = sd_start + 8
    entries_end = min(entries_start + entries_length, len(raw_bytes))

    entries = []
    offset = entries_start
    while offset + 16 <= entries_end:
        entry_data = raw_bytes[offset : offset + 16]
        entry = decode_someip_sd_entry(entry_data, 0)
        if entry:
            entries.append(entry)
        offset += 16

    return entries


def run_tcpdump(pcap_file):
    """Run tcpdump to capture all SOME/IP SD traffic."""
    # We capture all UDP 30490 traffic, sorting out IPs in Python
    cmd = ["tcpdump", "-r", pcap_file, "-n", "udp port 30490", "-XX"]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        return result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        print(f"Error: tcpdump timed out reading {pcap_file}")
        return ""
    except FileNotFoundError:
        print("Error: tcpdump not found. Please install tcpdump.")
        sys.exit(1)


# --- Summary Logic ---


def print_summary(ip, name, data):
    """Prints the formatted summary for a specific IP."""
    print("=" * 80)
    print(f"SUMMARY: {name} ({ip})")
    print("=" * 80)

    # OFFERS
    if data["offers"]:
        print("OFFERS:")
        sorted_offers = sorted(list(data["offers"]))
        for svc, inst, eg in sorted_offers:
            eg_str = f" with eventgroup 0x{eg:04x}" if eg else ""
            print(f"            Service 0x{svc:04x}.0x{inst:04x}{eg_str}")
    else:
        print("OFFERS:")
        print("            None detected")
    print()

    # FINDS
    if data["finds"]:
        print("FINDS:")
        for svc, inst in sorted(list(data["finds"])):
            print(f"            Service 0x{svc:04x}.0x{inst:04x}")
    else:
        print("FINDS:")
        print("            None detected")
    print()

    # SUBSCRIBES
    if data["subscribes"]:
        print("SUBSCRIBED SUCCESSFULLY TO:")
        for svc, inst, eg in sorted(list(data["subscribes"])):
            eg_str = f", eventgroup 0x{eg:04x}" if eg else ""
            print(f"            Service 0x{svc:04x}.0x{inst:04x}{eg_str}")
    else:
        print("SUBSCRIBED SUCCESSFULLY TO:")
        print("            None detected")
    print("\n")


def main():
    if len(sys.argv) > 1:
        pcap_file = sys.argv[1]
    else:
        print("Usage: python3 analyze_qemu1_qemu2_someip.py [pcap_file]")
        sys.exit(1)

    if not os.path.exists(pcap_file):
        print(f"Error: pcap file not found: {pcap_file}")
        sys.exit(1)

    print(f"Analyzing {pcap_file} for SOME/IP-SD traffic...")

    # Data structure to hold unique entries per host
    # Structure: host_data[ip] = { 'offers': set(), 'finds': set(), 'subscribes': set() }
    host_data = defaultdict(
        lambda: {"offers": set(), "finds": set(), "subscribes": set()}
    )

    # Run processing
    output = run_tcpdump(pcap_file)
    packets = parse_tcpdump_hex_output(output)

    if not packets:
        print("No SOME/IP-SD packets found.")
        return

    # Process packets
    for packet in packets:
        src_ip = packet["src_ip"]

        # Only process if src_ip is one of our targets
        if src_ip not in TARGET_HOSTS:
            continue

        entries = extract_someip_sd_entries(packet["raw_bytes"])

        for entry in entries:
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

    # Output Results
    print("\n")

    # 1. Print someipd Summary
    ip_someipd = "192.168.87.2"
    if ip_someipd in host_data:
        print_summary(ip_someipd, TARGET_HOSTS[ip_someipd], host_data[ip_someipd])
    else:
        print(f"No data found for {TARGET_HOSTS[ip_someipd]} ({ip_someipd})")

    # 2. Print sample_client Summary
    ip_client = "192.168.87.3"
    if ip_client in host_data:
        print_summary(ip_client, TARGET_HOSTS[ip_client], host_data[ip_client])
    else:
        print(f"No data found for {TARGET_HOSTS[ip_client]} ({ip_client})")


if __name__ == "__main__":
    main()
