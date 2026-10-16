import serial
import struct

PORT = "COM9"
BAUD = 115200

NODE_ADDRESS = 1
REQUEST_ID = 0x1235


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF

    for byte in data:
        crc ^= byte << 8

        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF

    return crc


# GET_STATUS has no payload.
header = struct.pack(
    "<BBBHH",
    0x01,           # protocol version
    0x02,           # GET_STATUS
    NODE_ADDRESS,
    REQUEST_ID,
    0               # payload length
)

crc = crc16_ccitt_false(header)
packet = b"\xAA\x55" + header + struct.pack("<H", crc)

print("TX:", packet.hex(" ").upper())

with serial.Serial(PORT, BAUD, timeout=1.0) as ser:
    ser.reset_input_buffer()
    ser.write(packet)
    ser.flush()

    response = ser.read(63)

print("RX bytes:", len(response))
print("RX:", response.hex(" ").upper())

if len(response) != 63:
    raise SystemExit("Did not receive complete 63-byte STATUS packet")

if response[0:2] != b"\xAA\x55":
    raise SystemExit("Bad sync")

version = response[2]
message_type = response[3]
node_address = response[4]
request_id, payload_length = struct.unpack_from("<HH", response, 5)

received_crc = struct.unpack_from("<H", response, len(response) - 2)[0]
calculated_crc = crc16_ccitt_false(response[2:-2])

if calculated_crc != received_crc:
    raise SystemExit(
        f"CRC FAILED: received 0x{received_crc:04X}, "
        f"calculated 0x{calculated_crc:04X}"
    )

if message_type != 0x82:
    raise SystemExit(f"Unexpected response type: 0x{message_type:02X}")

if payload_length != 52:
    raise SystemExit(f"Unexpected payload length: {payload_length}")

payload = response[9:-2]

status = payload[0]
node_state = payload[1]
enabled_mask = payload[2]
reserved = payload[3]

node_time_us = struct.unpack_from("<Q", payload, 4)[0]
run_id = struct.unpack_from("<I", payload, 12)[0]
latest_sequence = struct.unpack_from("<I", payload, 16)[0]
oldest_sequence = struct.unpack_from("<I", payload, 20)[0]
buffered_frames = struct.unpack_from("<H", payload, 24)[0]
buffer_capacity = struct.unpack_from("<H", payload, 26)[0]
buffer_overflow_count = struct.unpack_from("<I", payload, 28)[0]
adc_read_errors = struct.unpack_from("<I", payload, 32)[0]
rx_crc_errors = struct.unpack_from("<I", payload, 36)[0]
protocol_errors = struct.unpack_from("<I", payload, 40)[0]
fault_flags = struct.unpack_from("<I", payload, 44)[0]
config_generation = struct.unpack_from("<I", payload, 48)[0]

state_names = {
    0: "IDLE",
    1: "ARMED",
    2: "RUNNING",
    3: "FAULT",
}

print()
print("STATUS VALID")
print(f"Protocol:              {version}")
print(f"Node address:          {node_address}")
print(f"Request ID:            0x{request_id:04X}")
print(f"Status:                {status}")
print(f"Node state:            {node_state} ({state_names.get(node_state, 'UNKNOWN')})")
print(f"Enabled channel mask:  0x{enabled_mask:02X}")
print(f"Node time:             {node_time_us} us")
print(f"Node uptime:           {node_time_us / 1_000_000:.6f} s")
print(f"Run ID:                {run_id}")
print(f"Latest sequence:       {latest_sequence}")
print(f"Oldest sequence:       {oldest_sequence}")
print(f"Buffered frames:       {buffered_frames}")
print(f"Buffer capacity:       {buffer_capacity}")
print(f"Buffer overflows:      {buffer_overflow_count}")
print(f"ADC read errors:       {adc_read_errors}")
print(f"RX CRC errors:         {rx_crc_errors}")
print(f"Protocol errors:       {protocol_errors}")
print(f"Fault flags:           0x{fault_flags:08X}")
print(f"Config generation:     {config_generation}")
print(f"CRC:                   0x{received_crc:04X} VALID")