import serial
import struct

PORT = "COM9"
BAUD = 115200
REQUEST_ID = 0x1234
NODE_ADDRESS = 1


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


# Protocol header after sync:
# version, message type, node address, request ID, payload length
header = struct.pack(
    "<BBBHH",
    0x01,           # protocol version
    0x01,           # PING
    NODE_ADDRESS,
    REQUEST_ID,
    0               # payload length
)

crc = crc16_ccitt_false(header)

packet = b"\xAA\x55" + header + struct.pack("<H", crc)

print("TX:", packet.hex(" ").upper())
print(f"TX CRC: 0x{crc:04X}")

with serial.Serial(PORT, BAUD, timeout=1.0) as ser:
    ser.reset_input_buffer()

    ser.write(packet)
    ser.flush()

    response = ser.read(29)

print("RX:", response.hex(" ").upper())
print("RX bytes:", len(response))

if len(response) != 29:
    raise SystemExit("Did not receive complete 29-byte PONG")

if response[0:2] != b"\xAA\x55":
    raise SystemExit("Bad sync")

version = response[2]
message_type = response[3]
node_address = response[4]
request_id, payload_length = struct.unpack("<HH", response[5:9])

received_crc = struct.unpack("<H", response[-2:])[0]
calculated_crc = crc16_ccitt_false(response[2:-2])

print(f"Protocol:       {version}")
print(f"Message type:   0x{message_type:02X}")
print(f"Node address:   {node_address}")
print(f"Request ID:     0x{request_id:04X}")
print(f"Payload length: {payload_length}")
print(f"RX CRC:         0x{received_crc:04X}")
print(f"Calculated CRC: 0x{calculated_crc:04X}")

if received_crc != calculated_crc:
    raise SystemExit("CRC FAILED")

payload = response[9:-2]

status = payload[0]
fw_major = payload[1]
fw_minor = payload[2]
fw_patch = payload[3]
hw_major = payload[4]
hw_minor = payload[5]
device_uid = payload[6:18]

print()
print("PONG VALID")
print(f"Status:         {status}")
print(f"Firmware:       {fw_major}.{fw_minor}.{fw_patch}")
print(f"Hardware:       {hw_major}.{hw_minor}")
print(f"Device UID:     {device_uid.hex().upper()}")