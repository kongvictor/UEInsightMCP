"""
Persistent connection to UE Insight MCP Bridge.

Port 55559 (separate from UEEditorMCP's 55558).
Maintains a persistent socket with heartbeat and auto-reconnect.
"""

import json
import socket
import threading
import time
import logging
from typing import Any, Optional
from dataclasses import dataclass, field
from enum import Enum

logger = logging.getLogger(__name__)


class ConnectionState(Enum):
    """Connection lifecycle states."""
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    RECONNECTING = "reconnecting"
    ERROR = "error"


@dataclass
class ConnectionConfig:
    """Configuration for the Insight connection."""
    host: str = "127.0.0.1"
    port: int = 55559  # UEInsightMCP port (UEEditorMCP uses 55558)
    timeout: float = 120.0
    heartbeat_interval: float = 5.0
    max_reconnect_attempts: int = 5
    reconnect_base_delay: float = 1.0
    reconnect_max_delay: float = 30.0


@dataclass
class CommandResult:
    """Result of a command execution."""
    success: bool
    data: dict = field(default_factory=dict)
    error: Optional[str] = None
    recoverable: bool = True

    def to_dict(self) -> dict:
        result = {"success": self.success}
        if self.success:
            result.update(self.data)
        else:
            result["error"] = self.error
            result["recoverable"] = self.recoverable
            if self.data:
                result.update(self.data)
        return result


class PersistentInsightConnection:
    """
    Maintains a persistent connection to the UE Insight MCP Bridge.

    Features:
    - Keeps socket alive between commands
    - Heartbeat ping for stale connection detection
    - Auto-reconnect with exponential backoff
    - Thread-safe command execution
    """

    def __init__(self, config: Optional[ConnectionConfig] = None):
        self.config = config or ConnectionConfig()
        self._socket: Optional[socket.socket] = None
        self._state = ConnectionState.DISCONNECTED
        self._lock = threading.RLock()
        self._heartbeat_thread: Optional[threading.Thread] = None
        self._stop_heartbeat = threading.Event()
        self._last_activity = time.time()
        self._reconnect_attempts = 0

    @property
    def state(self) -> ConnectionState:
        return self._state

    @property
    def is_connected(self) -> bool:
        return self._state == ConnectionState.CONNECTED and self._socket is not None

    def connect(self) -> bool:
        with self._lock:
            if self._state == ConnectionState.CONNECTED:
                return True

            self._state = ConnectionState.CONNECTING

            try:
                self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self._socket.settimeout(self.config.timeout)
                self._socket.connect((self.config.host, self.config.port))

                self._state = ConnectionState.CONNECTED
                self._reconnect_attempts = 0
                self._last_activity = time.time()

                self._start_heartbeat()

                logger.info(f"Connected to UE Insight at {self.config.host}:{self.config.port}")
                return True

            except (socket.error, socket.timeout, ConnectionRefusedError) as e:
                self._state = ConnectionState.ERROR
                logger.error(f"Failed to connect to UE Insight: {e}")
                self._cleanup_socket()
                return False

    def disconnect(self):
        with self._lock:
            self._stop_heartbeat.set()
            if self._heartbeat_thread:
                self._heartbeat_thread.join(timeout=2.0)

            if self._socket and self._state == ConnectionState.CONNECTED:
                try:
                    self._send_raw({"type": "close"})
                except Exception:
                    pass

            self._cleanup_socket()
            self._state = ConnectionState.DISCONNECTED
            logger.info("Disconnected from UE Insight")

    def send_command(self, command_type: str, params: Optional[dict] = None) -> CommandResult:
        with self._lock:
            if not self.is_connected:
                if not self._try_reconnect():
                    return CommandResult(
                        success=False,
                        error="Not connected to UE Insight and reconnect failed",
                        recoverable=True
                    )

            command = {"type": command_type}
            if params:
                command["params"] = params

            try:
                params_preview = json.dumps(params)[:200] if params else "none"
                logger.debug(f">>> Sending command '{command_type}' with params: {params_preview}")

                self._send_raw(command)
                response = self._receive_raw()
                self._last_activity = time.time()

                if response:
                    response_preview = json.dumps(response)[:500]
                    logger.warning(f"<<< [{command_type}] Response: {response_preview}")

                if response is None:
                    self._state = ConnectionState.ERROR
                    if self._try_reconnect():
                        return self.send_command(command_type, params)
                    return CommandResult(
                        success=False,
                        error="Connection lost and reconnect failed",
                        recoverable=True
                    )

                # Parse response - handle both formats
                if "success" in response:
                    if response.get("success") is True:
                        data = {k: v for k, v in response.items() if k != "success"}
                        return CommandResult(success=True, data=data)
                    else:
                        error_msg = response.get("error", "Unknown error")
                        error_type = response.get("error_type", "unknown")
                        data = {k: v for k, v in response.items()
                                if k not in ("success", "error", "error_type")}
                        return CommandResult(
                            success=False, data=data,
                            error=f"[{error_type}] {error_msg}",
                            recoverable=response.get("recoverable", True)
                        )
                elif "status" in response:
                    if response.get("status") == "success":
                        return CommandResult(success=True, data=response.get("result", {}))
                    else:
                        error_msg = response.get("error", "Unknown error")
                        return CommandResult(success=False, error=error_msg, recoverable=True)
                else:
                    return CommandResult(
                        success=False,
                        error=f"Unknown response format: {json.dumps(response)[:500]}",
                        recoverable=True
                    )

            except socket.timeout:
                return CommandResult(
                    success=False,
                    error=f"Command '{command_type}' timed out after {self.config.timeout}s",
                    recoverable=True
                )
            except (socket.error, BrokenPipeError, ConnectionResetError) as e:
                self._state = ConnectionState.ERROR
                self._cleanup_socket()
                return CommandResult(success=False, error=str(e), recoverable=True)

    def ping(self) -> bool:
        result = self.send_command("ping")
        return result.success and result.data.get("pong", False)

    def get_context(self) -> CommandResult:
        return self.send_command("get_context")

    def _send_raw(self, data: dict):
        if not self._socket:
            raise ConnectionError("Socket not connected")
        json_str = json.dumps(data)
        message = json_str.encode('utf-8')
        length = len(message)
        self._socket.sendall(length.to_bytes(4, byteorder='big'))
        self._socket.sendall(message)

    def _receive_raw(self) -> Optional[dict]:
        if not self._socket:
            return None
        try:
            length_bytes = self._recv_exact(4)
            if not length_bytes:
                return None
            length = int.from_bytes(length_bytes, byteorder='big')
            if length <= 0 or length > 100 * 1024 * 1024:
                return None
            message_bytes = self._recv_exact(length)
            if not message_bytes:
                return None
            return json.loads(message_bytes.decode('utf-8'))
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            logger.error(f"Failed to parse response: {e}")
            return None

    def _recv_exact(self, num_bytes: int) -> Optional[bytes]:
        if not self._socket:
            return None
        data = bytearray()
        while len(data) < num_bytes:
            try:
                chunk = self._socket.recv(num_bytes - len(data))
                if not chunk:
                    return None
                data.extend(chunk)
            except (socket.timeout, socket.error):
                return None
        return bytes(data)

    def _cleanup_socket(self):
        if self._socket:
            try:
                self._socket.close()
            except Exception:
                pass
            self._socket = None

    def _try_reconnect(self) -> bool:
        self._state = ConnectionState.RECONNECTING
        self._cleanup_socket()

        while self._reconnect_attempts < self.config.max_reconnect_attempts:
            self._reconnect_attempts += 1
            delay = min(
                self.config.reconnect_base_delay * (2 ** (self._reconnect_attempts - 1)),
                self.config.reconnect_max_delay
            )
            logger.info(f"Reconnect attempt {self._reconnect_attempts}/{self.config.max_reconnect_attempts} in {delay:.1f}s")
            time.sleep(delay)
            if self.connect():
                return True

        self._state = ConnectionState.ERROR
        return False

    def _start_heartbeat(self):
        self._stop_heartbeat.clear()
        self._heartbeat_thread = threading.Thread(target=self._heartbeat_loop, daemon=True)
        self._heartbeat_thread.start()

    def _heartbeat_loop(self):
        while not self._stop_heartbeat.is_set():
            self._stop_heartbeat.wait(self.config.heartbeat_interval)
            if self._stop_heartbeat.is_set():
                break
            elapsed = time.time() - self._last_activity
            if elapsed >= self.config.heartbeat_interval:
                try:
                    if not self.ping():
                        with self._lock:
                            if self._state == ConnectionState.CONNECTED:
                                self._state = ConnectionState.ERROR
                                self._cleanup_socket()
                except Exception:
                    with self._lock:
                        if self._state == ConnectionState.CONNECTED:
                            self._state = ConnectionState.ERROR
                            self._cleanup_socket()

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.disconnect()
        return False


# Global connection instance
_global_connection: Optional[PersistentInsightConnection] = None


def get_connection() -> PersistentInsightConnection:
    global _global_connection
    if _global_connection is None:
        _global_connection = PersistentInsightConnection()
    return _global_connection


def reset_connection():
    global _global_connection
    if _global_connection:
        _global_connection.disconnect()
    _global_connection = None
