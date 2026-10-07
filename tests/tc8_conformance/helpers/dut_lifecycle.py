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
"""DUT lifecycle helpers shared by TC8 tests that manage the DUT stack directly.

Used by ``test_sd_client.py``, ``test_sd_reboot.py``, and
``test_sd_phases_timing.py``.  All functions operate in ITF (QEMU) mode:
*target_init* is a ``QemuTarget`` provided by the ITF framework and
commands execute on the QEMU guest via SSH.
"""

import logging
import os
import socket
import time
from pathlib import Path
from typing import Iterable, Optional, Union

from helpers.sd_helpers import open_multicast_socket, parse_sd_offers

_logger = logging.getLogger(__name__)

#: Binaries launched on the QEMU guest by launch_dut, used both to build the
#: launch commands and as the fallback pkill target list in _TargetProcess.
_SOMEIPD_BIN = "someipd"
_GATEWAYD_BIN = "gatewayd"
_ETS_STUB_BIN = "tc8_ets_stub"

# ---------------------------------------------------------------------------
# Config name to rendered guest path mapping
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

    def __init__(
        self,
        proc: object,
        target_init: object = None,
        secondary_proc: object = None,
        stub_proc: object = None,
        process_names: Iterable[str] = (),
    ) -> None:
        self._proc = proc
        self._target_init = target_init
        self._secondary_proc = secondary_proc
        self._stub_proc = stub_proc
        # Names of the binaries this wrapper was told it launched (supplied
        # by launch_dut). Used only by the pkill fallback in terminate() so
        # this class does not need to hardcode binary names itself.
        self._process_names = tuple(process_names)

    def poll(self) -> Optional[int]:
        """Return ``None`` while running, ``0`` once any tracked process has stopped."""
        for proc in (self._proc, self._secondary_proc, self._stub_proc):
            if proc is not None and not proc.is_running():  # type: ignore[attr-defined]
                return 0
        return None

    def terminate(self) -> None:
        """Stop the remote processes.

        pkill -9 runs first so stop()'s 15s wait() timeout is only hit as a
        fallback, not on every teardown. Do not reorder.
        """
        names = self._process_names
        entries = (
            (self._proc, "primary", names[0] if len(names) > 0 else None),
            (self._secondary_proc, "secondary", names[1] if len(names) > 1 else None),
            (self._stub_proc, "stub", names[2] if len(names) > 2 else None),
        )
        for proc, label, name in entries:
            if proc is None:
                continue
            if name is not None and self._target_init is not None:
                try:
                    self._target_init.execute(f"pkill -9 {name} 2>/dev/null || true")  # type: ignore[attr-defined]
                except Exception:  # noqa: BLE001
                    _logger.warning(
                        "force-kill fallback of %s on QEMU guest failed; continuing teardown",
                        name,
                    )
            if not proc.is_running():  # type: ignore[attr-defined]
                continue
            try:
                proc.stop()  # type: ignore[attr-defined]
            except RuntimeError as exc:
                _logger.warning(
                    "AsyncProcess.stop() for %s proc raised during teardown (ignored): %s",
                    label,
                    exc,
                )

    def kill(self) -> None:
        """Alias for terminate: no separate SIGKILL equivalent in ITF."""
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


def render_someip_config(
    config_name: str,
    host_ip: str,
    dest_dir: Path,
    service_id: str = "",
    instance_id: str = "",
) -> Path:
    """Render a config template's ``__TC8_*__`` placeholders and write it to *dest_dir*.

    In ITF mode the resulting file is not actually used by the DUT (the QEMU
    guest's configs are rendered separately by ``tc8_itf_config_setup`` via
    sed); this call is kept only so the ``sd_client_config`` fixture
    signature stays unchanged.
    """
    sd_port = os.environ.get("TC8_SD_PORT", "30490")
    svc_port = os.environ.get("TC8_SVC_PORT", "30509")
    svc_tcp_port = os.environ.get("TC8_SVC_TCP_PORT", "30510")
    if not service_id:
        service_id = os.environ.get("TC8_SERVICE_ID", "0x1234")
    if not instance_id:
        instance_id = os.environ.get("TC8_INSTANCE_ID", "0x5678")
    template_path = Path(__file__).parent.parent / "config" / config_name
    rendered = (
        template_path.read_text(encoding="utf-8")
        .replace("__TC8_HOST_IP__", host_ip)
        .replace("__TC8_SERVICE_ID__", service_id)
        .replace("__TC8_INSTANCE_ID__", instance_id)
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


def launch_dut(
    config_path: Union[Path, str],
    target_init: object,
) -> object:
    """Start the full DUT stack (someipd, tc8_ets_stub, gatewayd) on the QEMU guest.

    *config_path*'s filename selects the pre-rendered guest vsomeip config
    (written to ``/tmp`` by ``tc8_itf_config_setup``). someipd is started
    first so it becomes the vsomeip routing manager; gatewayd retries the
    IPC handshake internally until someipd is ready.
    """
    if target_init is None:
        raise RuntimeError("launch_dut: target_init must be provided (ITF mode only)")

    # Production stack runs on the QEMU guest. Configs are pre-rendered.
    name = Path(config_path).name if isinstance(config_path, Path) else str(config_path)
    guest_config = _GUEST_CONFIG_MAP.get(name, "tc8_sd.json")

    # 1. Start someipd: it becomes the vsomeip routing manager.
    someipd_proc = target_init.execute_async(  # type: ignore[attr-defined]
        f"LD_LIBRARY_PATH=/opt:/opt/usr/lib "
        f"VSOMEIP_CONFIGURATION=/tmp/{guest_config} "
        f"MW_LOG_CONFIG_FILE=/opt/tc8_logging.json "
        f"/opt/someipd -c /opt/tc8_someipd_config.bin"
    )

    # 2. Start the ETS stub: provides the mw::com skeleton so gatewayd's
    #    StartFindService callback fires and gatewayd calls offer_event() in vsomeip.
    stub_proc = target_init.execute_async(  # type: ignore[attr-defined]
        f"LD_LIBRARY_PATH=/opt:/opt/usr/lib "
        f"MW_LOG_CONFIG_FILE=/opt/tc8_logging.json /opt/tc8_ets_stub -s /opt/tc8_ets_stub_mw_com_config.json"
    )

    # 3. Start gatewayd: connects to someipd IPC server, discovers the stub,
    #    and calls offer_event() so vsomeip advertises the service.
    gatewayd_proc = target_init.execute_async(  # type: ignore[attr-defined]
        f"LD_LIBRARY_PATH=/opt:/opt/usr/lib "
        f"MW_LOG_CONFIG_FILE=/opt/tc8_logging.json "
        f"/opt/gatewayd -c /opt/tc8_someipd_config.bin -s /opt/tc8_gatewayd_mw_com_config.json"
    )

    return _TargetProcess(
        someipd_proc,
        target_init=target_init,
        secondary_proc=gatewayd_proc,
        stub_proc=stub_proc,
        process_names=(_SOMEIPD_BIN, _GATEWAYD_BIN, _ETS_STUB_BIN),
    )


def terminate_dut(proc: object) -> None:
    """Terminate the DUT stack and close its pipes."""
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
    target_init: object = None,
) -> None:
    """Remove stale vsomeip routing-manager sockets and LoLa SHM/discovery
    leftovers on the QEMU guest before each DUT (re)start.

    Covers both Linux (``/tmp/vsomeip-*``, ``/dev/shm/lola-*``,
    ``/tmp/mw_com_lola/partial_restart/*``) and QNX8 (``/var/run/vsomeip-*``,
    ``/dev/shmem/lola-*``, ``/tmp_discovery/mw_com_lola/partial_restart/*``)
    layouts. Cleanup is broad (not scoped to one service/instance) since the
    target only ever runs one test session at a time.
    """
    if target_init is None:
        _logger.warning("cleanup_vsomeip_sockets: target_init not provided; skipping cleanup (ITF mode only)")
        return
    stale_globs = (
        "/tmp/vsomeip-*",
        "/var/run/vsomeip-*",
        "/dev/shm/lola-*",
        "/dev/shmem/lola-*",
        "/tmp/mw_com_lola/partial_restart/*",
        "/tmp_discovery/mw_com_lola/partial_restart/*",
    )
    exit_code, output = target_init.execute("rm -rf " + " ".join(stale_globs))  # type: ignore[attr-defined]
    if exit_code != 0:
        _logger.warning(
            "vsomeip/LoLa cleanup on target returned %d: %s",
            exit_code,
            output.decode(errors="replace"),
        )


# ---------------------------------------------------------------------------
# SD readiness gate (host-side)
# ---------------------------------------------------------------------------


def wait_for_sd_readiness(
    host_ip: str,
    timeout_secs: float = 10.0,
) -> bool:
    """Wait until the DUT sends at least one multicast OfferService, or return False on timeout."""
    sock = open_multicast_socket(host_ip)

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
            if parse_sd_offers(data):
                return True
        return False
    finally:
        sock.close()
