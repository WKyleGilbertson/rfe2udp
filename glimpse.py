import socket
import struct
from datetime import datetime, timezone

# 17-byte header: [Type:1][Unix:4][Tick:4][Seq:4][ID:4]
header_format = "=BIII4s"
HEADER_SIZE = 17
EXPECTED_PACKET_SIZE = 1040

RELAY_IP = "127.0.0.1"
RELAY_PORT = 12345

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(2.0)
sock.bind(('', 0))

# Send JOIN magic
sock.sendto(struct.pack("<I", 0x4A4F494E), (RELAY_IP, RELAY_PORT))

print(f"Sent JOIN command to RFE2UDP server at {RELAY_IP}:{RELAY_PORT}")
print("Waiting for millisecond alignment...")
print(f" T | Source | {'Seq':>10} | {'ISO8601 Timestamp':>24} | {'Tick':>12} | Phase")
print("-" * 95)

captured_count = 0
aligned = False

with open("cap.raw", "wb") as f:
    try:
        while captured_count < 40:
            try:
                data, addr = sock.recvfrom(2048)
            except socket.timeout:
                continue
            
            if len(data) != EXPECTED_PACKET_SIZE:
                continue
            
            # Unpack 17-byte packed header
            pkt_type, unix_t, tick, seq, dev_id_raw = struct.unpack(header_format, data[:HEADER_SIZE])
            dev_id = dev_id_raw.decode('ascii', errors='ignore').strip('\x00')

            # Alignment Gate (8184 samples = 1ms)
            if not aligned:
                # Checking for ASCII '1' (49) or integer 1
                if (pkt_type == 49 or pkt_type == 1) and (tick % 8184 == 0):
                    aligned = True
                    print(f"[*] ALIGNED at tick {tick} for {dev_id}. Starting capture.")
                else:
                    continue

            # Write RF samples
            f.write(data[HEADER_SIZE:])
            
            # Phase: Remainder within the 1ms (8184 sample) window
            phase = tick % 8184
            # MS: Calculated from ticks (8184 ticks/ms)
            ms = (tick % 8184000) // 8184
            
            dt = datetime.fromtimestamp(unix_t, tz=timezone.utc)
            time_str = dt.strftime('%Y-%m-%dT%H:%M:%S') + f".{ms:03d}Z"
            
            # Formatting the ID/Type for the new layout
            type_char = chr(pkt_type) if 32 <= pkt_type <= 126 else str(pkt_type)
            
            print(f" {type_char:1} | {dev_id:>6} | {seq:10d} | {time_str:24} | {tick:12d} | {phase:6d}")
            captured_count += 1
                
    except KeyboardInterrupt:
        pass

print(f"\nDone! Saved {captured_count} aligned packets to cap.raw.")