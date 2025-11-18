#!/bin/bash

# *******************************************************************************
# Copyright (c) 2025 Elektrobit Automotive GmbH
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

MANIFEST_LOCATION="/tmp/mw_com_lola_service_manifest.json"
TEMPDIR=""

function cleanup_lola() {
    # Ensure tests are run in a clean state

    # Linux
    rm -rf /dev/shm/lola-*6432*
    rm -rf /tmp/mw_com_lola/*/*6432*
    rm -rf /tmp/lola-*-*6432*_lock

    # QNX
    rm -rf /dev/shmem/lola-*6432*
    rm -rf /tmp_discovery/mw_com_lola/*/*6432*
    rm -rf /tmp_discovery/lola-*-*6432*_lock
}

function create_service_manifest() {
    local file="$1"
    local provider_user_id=$2
    local consumer_user_id=$3

    cat <<EOF > "$file"
{
  "serviceTypes": [
    {
      "serviceTypeName": "/bmw/adp/MapApiLanesStamped",
      "version": {
        "major": 1,
        "minor": 0
      },
      "bindings": [
        {
          "binding": "SHM",
          "serviceId": 6432,
          "events": [
            {
              "eventName": "map_api_lanes_stamped",
              "eventId": 1
            }
          ]
        }
      ]
    }
  ],
  "serviceInstances": [
    {
      "instanceSpecifier": "xpad/cp60/MapApiLanesStamped",
      "serviceTypeName": "/bmw/adp/MapApiLanesStamped",
      "version": {
        "major": 1,
        "minor": 0
      },
      "instances": [
        {
          "instanceId": 1,
          "allowedConsumer": {
            "QM": [
              $consumer_user_id
            ]
          },
          "allowedProvider": {
            "QM": [
              $provider_user_id
            ]
          },
          "asil-level": "QM",
          "binding": "SHM",
          "events": [
            {
              "eventName": "map_api_lanes_stamped",
              "numberOfSampleSlots": 10,
              "maxSubscribers": 3
            }
          ]
        }
      ]
    }
  ]
}
EOF
}

function trap_cleanup() {
    rm -f "$MANIFEST_LOCATION"
    if [ -d "$TEMPDIR" ]; then
      cat $TEMPDIR/*
      rm -rf "$TEMPDIR"
    fi
}

function setup() {
    create_service_manifest "$MANIFEST_LOCATION" $(id -u) $(id -u)
    trap trap_cleanup EXIT
}

function run_receiver_sender() {
    EXAMPLE_CMD_RECV="$1"
    EXAMPLE_CMD_SEND="$2"

    TEMPDIR=$(mktemp -d /tmp/ipc_bridge.XXXXXX)

    # Ensure we start with a clean state
    cleanup_lola

    # Run examples
    $EXAMPLE_CMD_SEND --num-cycles 0 > "$TEMPDIR/send.log" 2>&1 &
    sender_pid=$!
    $EXAMPLE_CMD_RECV --num-cycles 2 > "$TEMPDIR/recv.log" 2>&1

    echo -e "\nReceiver log:"
    cat "$TEMPDIR/recv.log"

    echo -e "\nSender log:"
    cat "$TEMPDIR/send.log"
    echo ""

    # Check if the receiver received the message
    grep -q "Subscrib" "$TEMPDIR/recv.log"
    grep -q "Received sample" "$TEMPDIR/recv.log"

    rm -rf "$TEMPDIR"

    # Kill sender and check its return code
    kill $sender_pid

    set +e
    wait $sender_pid
    sender_return_code=$?
    set -e
    [[ "143" == "$sender_return_code" ]]

    # Cleanup due to SIGINT
    cleanup_lola
}
