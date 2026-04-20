import socket
import struct
from datetime import datetime, timezone

# Header: Unix Time (I), Sample Tick (I), Sequence (I)
header_format = "<III" 
header_size = struct.calcsize(header_format)

# Capture Settings
# To get exactly 5ms aligned: 5ms * 8 packets/ms = 40 packets
PACKETS_TO_SAVE = 40  
output_filename = "cap.raw"

RELAY_IP = "127.0.0.1"
RELAY_PORT = 12345

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', 0))  # Bind to any available port

sock.sendto(struct.pack("<I", 0x4A4F494E), (RELAY_IP, RELAY_PORT))  # "JOIN" 
print(f"Sent JOIN command to RFE2UDP server at {RELAY_IP}:{RELAY_PORT}")

print(f"Waiting for millisecond alignment...")
print(f"{'Seq':>10} | {'ISO8601 Timestamp':>24} | {'Tick':>8} | {'Phase':>5}")
print("-" * 75)

captured_count = 0
aligned = False  # New alignment flag

with open(output_filename, "wb") as f:
    try:
        while captured_count < PACKETS_TO_SAVE:
            data, addr = sock.recvfrom(2048)
            
            if len(data) >= header_size:
                unix_t, tick, seq = struct.unpack(header_format, data[:header_size])
                
                # --- ALIGNMENT GATE ---
                if not aligned:
                    # check if tick is a multiple of 8,184 (start of 1ms)
                    if tick % 8184 == 0:
                        aligned = True
                        print(f"[*] ALIGNED at tick {tick}. Starting capture.")
                    else:
                        continue # Keep waiting for a boundary
                # -----------------------

                payload = data[header_size:]
                
                # Write only the raw RF data to file
                f.write(payload)
                
                # Calculation based on 8184 bytes per MS
                ms = (tick % 8184000) // 8184
                phase = tick % 8184
                
                dt = datetime.fromtimestamp(unix_t, tz=timezone.utc)
                iso_str = dt.strftime('%Y-%m-%dT%H:%M:%S') + f".{ms:03d}Z"
                
                print(f"{seq:10d} | {iso_str:24} | {tick:8d} | {phase:5d}")
                
                captured_count += 1
                
    except KeyboardInterrupt:
        print("\nCapture stopped by user.")

print(f"\nDone! Saved {captured_count} aligned packets to {output_filename}.")