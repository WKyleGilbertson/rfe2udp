import socket
import struct
from datetime import datetime, timezone

# Header: 3 unsigned 32-bit ints (unix, tick, seq) = 12 bytes
header_format = "<III" 
header_size = struct.calcsize(header_format)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", 12345))

print(f"{'Seq':>10} | {'ISO8601 Timestamp':>24} | {'Tick':>8} | {'Phase':>5}")
print("-" * 75)

while True:
    data, addr = sock.recvfrom(2048)
    if len(data) >= header_size:
        # Unpack the 12-byte header
        unix_t, tick, seq = struct.unpack(header_format, data[:header_size])
        
        # Calculate sub-second info from the master tick (8184 bytes/ms)
        sub_ms = tick // 8184
        phase = tick % 8184
        
        # Format the time
        dt = datetime.fromtimestamp(unix_t, tz=timezone.utc)
        iso_str = dt.strftime('%Y-%m-%dT%H:%M:%S') + f".{sub_ms:03d}Z"
        
        # Payload size (should be 1024)
        payload_len = len(data) - header_size
        
        print(f"{seq:10d} | {iso_str:24} | {tick:8d} | {phase:5d}")