import socket
import struct
from datetime import datetime, timezone

# Header: Unix Time (I), Sample Tick (I), Sequence (I)
header_format = "<III" 
header_size = struct.calcsize(header_format)

# Capture Settings
PACKETS_TO_SAVE = 1000  # Adjust as needed
output_filename = "capture_data.bin"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", 12345))

print(f"Capturing precise stream to {output_filename}...")
print(f"{'Seq':>10} | {'ISO8601 Timestamp':>24} | {'Tick':>8} | {'Phase':>5}")
print("-" * 75)

captured_count = 0
with open(output_filename, "wb") as f:
    try:
        while captured_count < PACKETS_TO_SAVE:
            data, addr = sock.recvfrom(2048)
            
            if len(data) >= header_size:
                unix_t, tick, seq = struct.unpack(header_format, data[:header_size])
                payload = data[header_size:]
                
                # Write only the raw RF data to file
                f.write(payload)
                
                # Calculate time and phase
                # 8184 samples = 1ms
                ms = tick // 8184
                phase = tick % 8184
                
                dt = datetime.fromtimestamp(unix_t, tz=timezone.utc)
                # ISO format: YYYY-MM-DDTHH:MM:SS.mmmZ
                iso_str = dt.strftime('%Y-%m-%dT%H:%M:%S') + f".{ms:03d}Z"
                
                print(f"{seq:10d} | {iso_str:24} | {tick:8d} | {phase:5d}")
                
                captured_count += 1
                
    except KeyboardInterrupt:
        print("\nCapture stopped by user.")

print(f"\nDone! Saved {captured_count} packets to {output_filename}.")