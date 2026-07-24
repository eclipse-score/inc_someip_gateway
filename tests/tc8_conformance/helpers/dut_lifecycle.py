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
"""DUT lifecycle helpers shared by TC8 tests that manage someipd directly.

Used by ``test_sd_client.py``, ``test_sd_reboot.py``, and
``test_sd_phases_timing.py``.  All functions operate in ITF (QEMU) mode:
*target_init* is a ``QemuTarget`` provided by the ITF framework and
commands execute on the QEMU guest via SSH.
"""

import glob
import logging
import os
import socket
import struct
import time
from pathlib import Path
from typing import Optional, Union

_logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Config name → rendered guest path mapping (mirrors tc8_itf_conftest._CONFIG_MAP)
# ---------------------------------------------------------------------------

#: Maps the template filename used in BUILD.bazel ``env`` to the rendered path
#: on the QEMU guest (written by ``tc8_itf_config_setup`` via sed).
_GUEST_CONFIG_MAP: dict[str, str] = {
    "tc8_someipd_sd.json": "tc8_sd.json",
    "tc8_someipd_service.json": "tc8_service.json",
    "tc8_someipd_multi.json": "tc8_multi.json",
}


# ---------------------------------------------------------------------------
# Popen-compatible adapter for ITF AsyncProcess
# ---------------------------------------------------------------------------


class _TargetProcess:
    """Wraps an ITF ``AsyncProcess`` with a ``subprocess.Popen``-compatible API.

    TC8 lifecycle helpers call ``proc.terminate()`` and ``proc.wait()`` on the
    returned process object.
    """

    def __init__(self, proc: object, target_init: object = None) -> None:
        self._proc = proc
        self._target_init = target_init

    def poll(self) -> Optional[int]:
        """Return ``None`` while running, ``0`` after the process has stopped."""
        return None if self._proc.is_running() else 0  # type: ignore[attr-defined]

    def terminate(self) -> None:
        """Stop the remote process.

        Force-kills someipd on the QEMU guest (``pkill -9 someipd``) before
        calling ``proc.stop()``.  ``pkill`` is portable: procps on Linux,
        ``slay`` symlink on QNX8 (pgrep absent from the QNX IFS).

        Failures are logged as warnings and do not re-raise so teardown
        does not fail the test.
        """
        if self._target_init is not None:
            try:
                self._target_init.execute(  # type: ignore[attr-defined]
                    "pkill -9 someipd 2>/dev/null || true"
                )
            except Exception:  # noqa: BLE001
                _logger.warning(
                    "force-kill of someipd on QEMU guest failed; continuing teardown"
                )
        try:
            self._proc.stop()  # type: ignore[attr-defined]
        except RuntimeError as exc:
            _logger.warning(
                "AsyncProcess.stop() raised during teardown (ignored): %s", exc
            )

    def kill(self) -> None:
        """Alias for terminate — no separate SIGKILL equivalent in ITF."""
        self.terminate()

    def wait(self, timeout: Optional[float] = None) -> int:  # noqa: ARG002
        """No-op: ``stop()`` is already synchronous."""
        return 0

    @property
    def stdout(self) -> None:
        return None

    @property
    def stderr(self) -> None:
        return None

    @property
    def returncode(self) -> Optional[int]:
        return 0 if not self._proc.is_running() else None  # type: ignore[attr-defined]


# ---------------------------------------------------------------------------
# Config helpers
# ---------------------------------------------------------------------------


def render_someip_config(config_name: str, host_ip: str, dest_dir: Path) -> Path:
    """Replace ``__TC8_HOST_IP__``, ``__TC8_SD_PORT__``, ``__TC8_SVC_PORT__``,
    ``__TC8_SVC_TCP_PORT__``, and ``__TC8_LOG_DIR__`` in a config template.

    Writes the rendered config to *dest_dir* and returns the path.

    In ITF mode this function is called but the resulting file is not used
    (the QEMU guest already has its configs rendered by ``tc8_itf_config_setup``
    via sed).  The call is kept so that the ``sd_client_config`` fixture
    signature remains unchanged.
    """
    sd_port = os.environ.get("TC8_SD_PORT", "30490")
    svc_port = os.environ.get("TC8_SVC_PORT", "30509")
    svc_tcp_port = os.environ.get("TC8_SVC_TCP_PORT", "30510")
    template_path = Path(__file__).parent.parent / "config" / config_name
    rendered = (
        template_path.read_text(encoding="utf-8")
        .replace("__TC8_HOST_IP__", host_ip)
        .replace("__TC8_SD_PORT__", sd_port)
        .replace("__TC8_SVC_PORT__", svc_port)
        .replace("__TC8_SVC_TCP_PORT__", svc_tcp_port)
        .replace("__TC8_LOG_DIR__", str(dest_dir))
    )
    config_path = dest_dir / config_name
    config_path.write_text(rendered, encoding="utf-8")
    return config_path


# ---------------------------------------------------------------------------
# DUT launch / teardown
# ---------------------------------------------------------------------------


def launch_someipd(
    config_path: Union[Path, str],
    target_init: object = None,
) -> object:
    """Start ``someipd --tc8-standalone`` on the QEMU guest and return a handle.

    *target_init* is a ``QemuTarget`` provided by the ITF framework.  The
    *config_path* filename selects the pre-rendered guest config (written to
    ``/tmp`` by ``tc8_itf_config_setup`` via sed, session-scoped).

    Returns a ``_TargetProcess`` adapter whose ``.terminate()`` / ``.wait()``
    interface is compatible with :func:`terminate_someipd`.
    """
    if target_init is not None:
        # ITF path: someipd runs on the QEMU guest.  Configs are pre-rendered.
        name = (
            Path(config_path).name
            if isinstance(config_path, Path)
            else str(config_path)
        )
        guest_config = _GUEST_CONFIG_MAP.get(name, "tc8_sd.json")
        proc = target_init.execute_async(  # type: ignore[attr-defined]
            f"LD_LIBRARY_PATH=/ "
            f"VSOMEIP_CONFIGURATION=/tmp/{guest_config} "
            f"/someipd "
            f"--tc8-standalone "
            f"--service_instance_manifest /someipd_mw_com_config.json"
        )
        return _TargetProcess(proc, target_init=target_init)

    raise RuntimeError("launch_someipd: target_init must be provided (ITF mode only)")


def terminate_someipd(proc: object) -> None:
    """Terminate ``someipd`` and close its pipes."""
    proc.terminate()  # type: ignore[attr-defined]
    proc.wait(timeout=5)  # type: ignore[attr-defined]
    if proc.stdout:  # type: ignore[attr-defined]
        proc.stdout.close()  # type: ignore[attr-defined]
    if proc.stderr:  # type: ignore[attr-defined]
        proc.stderr.close()  # type: ignore[attr-defined]


# ---------------------------------------------------------------------------
# vsomeip socket cleanup
# ---------------------------------------------------------------------------


def cleanup_vsomeip_sockets(
    base_path: str = "/tmp",
    target_init: object = None,
) -> None:
    """Remove stale vsomeip routing-manager sockets.

    In **legacy mode** (*target_init* is ``None``) removes
    ``<base_path>/vsomeip-*`` socket files on the host.

    In **ITF mode** (*target_init* is a ``QemuTarget``) runs
    ``rm -f /tmp/vsomeip-*`` on the QEMU guest via SSH.
    """
    if target_init is not None:
        # Remove stale sockets from both paths:
        #   Linux QEMU : /tmp/vsomeip-*
        #   QNX8 QEMU  : /var/run/vsomeip-*  (vsomeip-qnx8.patch sets VSOMEIP_BASE_PATH="/var/run")
        target_init.execute("rm -f /tmp/vsomeip-* /var/run/vsomeip-*")  # type: ignore[attr-defined]
        return
    for stale in glob.glob(f"{base_path}/vsomeip-*"):
        try:
            os.unlink(stale)
        except OSError:
            pass


# ---------------------------------------------------------------------------
# SD readiness gate (host-side — works in both legacy and ITF modes)
# ---------------------------------------------------------------------------


def wait_for_sd_readiness(
    host_ip: str,
    timeout_secs: float = 10.0,
) -> bool:
    """Wait until the DUT sends at least one multicast OfferService.

    Opens a short-lived multicast socket on *host_ip* (host TAP interface),
    joins the SD multicast group, and returns ``True`` as soon as a SOME/IP-SD
    OfferService entry is received.  Returns ``False`` on timeout.

    Port and multicast address are read from ``helpers.constants`` so they
    stay in sync with the vsomeip config templates.
    """
    from helpers.constants import SD_MULTICAST_ADDR, SD_PORT  # noqa: PLC0415

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except AttributeError:
        pass
    sock.bind(("", SD_PORT))
    group_bytes = socket.inet_aton(SD_MULTICAST_ADDR)
    iface_bytes = socket.inet_aton(host_ip)
    mreq = struct.pack("4s4s", group_bytes, iface_bytes)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

    deadline = time.monotonic() + timeout_secs
    try:
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(min(remaining, 1.0))
            try:
                data, _ = sock.recvfrom(65535)
            except socket.timeout:
                continue
            if len(data) < 20:
                continue
            service_id = int.from_bytes(data[0:2], "big")
            if service_id != 0xFFFF:
                continue
            sd_offset = 16
            if len(data) < sd_offset + 12:
                continue
            entries_len = int.from_bytes(data[sd_offset + 4 : sd_offset + 8], "big")
            entry_start = sd_offset + 8
            pos = entry_start
            while pos + 16 <= entry_start + entries_len and pos + 16 <= len(data):
                entry_type = data[pos]
                if entry_type == 0x01:  # OfferService
                    return True
                pos += 16
        return False
    finally:
        sock.close()
