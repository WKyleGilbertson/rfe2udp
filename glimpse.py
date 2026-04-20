import socket
import struct
from datetime import datetime, timezone

# Configuration
HEADER_FORMAT = "<BIII16s"
HEADER_SIZE = 29
EXPECTED_SIZE = 1052 
CAPTURE_LIMIT = 40
FILENAME = "cap.raw"

RELAY_IP = "127.0.0.1"
RELAY_PORT = 12345

payload_size = EXPECTED_SIZE - HEADER_SIZE
total_target_bytes = CAPTURE_LIMIT * payload_size

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(1.0)
sock.bind(('', 0))
sock.sendto(struct.pack("<I", 0x4A4F494E), (RELAY_IP, RELAY_PORT))

print(f"Targeting {CAPTURE_LIMIT} packets ({total_target_bytes} bytes) -> {FILENAME}")
print(f"Hunting for Millisecond Alignment...")
print("")

# Tightened header alignment
print(f"{'Sequence':>10} | {'Timestamp':>12} | {'Tick':>12} | {'Phase':>6}")
print("-" * 10 + "-+-" + "-" * 12 + "-+-" + "-" * 12 + "-+-" + "-" * 6)

captured_count = 0
aligned = False

try:
    with open(FILENAME, "wb") as f:
        while captured_count < CAPTURE_LIMIT:
            try:
                data, addr = sock.recvfrom(2048)
            except socket.timeout:
                continue
            
            if len(data) != EXPECTED_SIZE:
                continue
            
            pkt_type, unix_t, tick, seq, dev_tag = struct.unpack_from(HEADER_FORMAT, data)
            current_phase = tick % 8184
            
            if not aligned:
                print(f"  [Waiting... Type: {pkt_type} Phase: {current_phase}]", end='\r')
                
                # Lock to Phase 0 specifically since you're hitting it again
                if (pkt_type == 1 or pkt_type == 49) and current_phase == 0:
                    aligned = True
                    dev = dev_tag.decode('ascii', errors='ignore').strip('\x00')
                    print(" " * 60, end='\r') 
                    print(f"[*] LOCKED: Phase 0 | Device: {dev}")
                else:
                    continue

            # Write raw payload
            f.write(data[HEADER_SIZE:])
            captured_count += 1
            
            # Format row
            ms = (tick % 8184000) // 8184
            ts = datetime.fromtimestamp(unix_t, tz=timezone.utc).strftime('%H:%M:%S')
            time_str = f"{ts}.{ms:03d}"
            print(f"{seq:10d} | {time_str:>12} | {tick:12d} | {current_phase:6d}")

except KeyboardInterrupt:
    print("\n\nCapture stopped.")

actual_bytes = captured_count * payload_size
print("-" * 49)
if captured_count > 0:
    print(f"Success: {captured_count} packets captured.")
    print(f"Written: {actual_bytes} bytes to {FILENAME}.")
else:
    print("Alignment not found.")