import serial
import struct

PORT = "COM9"
BAUD = 115200
NODE_ADDRESS = 1

PROTOCOL_VERSION = 0x01
GET_STATUS = 0x02
READ_FRAMES = 0x03

STATUS_RESPONSE = 0x82
FRAME_DATA_RESPONSE = 0x83

REQUEST_ID_STATUS = 0x3001
REQUEST_ID_FRAMES = 0x3002

DATA_NOT_AVAILABLE = 0x05

SEQUENCE_FIRST = 1
SEQUENCE_MAX = 0xFFFFFFFE
SEQUENCE_NONE = 0xFFFFFFFF



def previous_sequence(sequence: int) -> int:
    if sequence <= SEQUENCE_FIRST:
        return SEQUENCE_MAX
    return sequence - 1


def next_sequence(sequence: int) -> int:
    if sequence >= SEQUENCE_MAX or sequence == 0:
        return SEQUENCE_FIRST
    return sequence + 1

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


def build_packet(message_type, request_id, payload=b""):
    header = struct.pack(
        "<BBBHH",
        PROTOCOL_VERSION,
        message_type,
        NODE_ADDRESS,
        request_id,
        len(payload),
    )

    crc = crc16_ccitt_false(header + payload)

    return (
        b"\xAA\x55"
        + header
        + payload
        + struct.pack("<H", crc)
    )


def read_exact(ser, count):
    data = b""

    while len(data) < count:
        chunk = ser.read(count - len(data))

        if not chunk:
            break

        data += chunk

    return data


def read_packet(ser):
    sync = read_exact(ser, 2)

    if sync != b"\xAA\x55":
        raise RuntimeError(f"Bad or missing sync: {sync.hex()}")

    header = read_exact(ser, 7)

    if len(header) != 7:
        raise RuntimeError("Incomplete header")

    version = header[0]
    message_type = header[1]
    node_address = header[2]
    request_id, payload_length = struct.unpack_from("<HH", header, 3)

    payload = read_exact(ser, payload_length)
    crc_bytes = read_exact(ser, 2)

    if len(payload) != payload_length or len(crc_bytes) != 2:
        raise RuntimeError("Incomplete packet")

    received_crc = struct.unpack("<H", crc_bytes)[0]
    calculated_crc = crc16_ccitt_false(header + payload)

    if received_crc != calculated_crc:
        raise RuntimeError(
            f"CRC failure: received 0x{received_crc:04X}, "
            f"calculated 0x{calculated_crc:04X}"
        )

    return {
        "version": version,
        "message_type": message_type,
        "node_address": node_address,
        "request_id": request_id,
        "payload": payload,
        "crc": received_crc,
    }


with serial.Serial(PORT, BAUD, timeout=2.0) as ser:
    ser.reset_input_buffer()

    # Get current buffer limits.
    status_request = build_packet(GET_STATUS, REQUEST_ID_STATUS)
    ser.write(status_request)
    ser.flush()

    status_packet = read_packet(ser)

    if status_packet["message_type"] != STATUS_RESPONSE:
        raise RuntimeError("Expected STATUS response")

    status_payload = status_packet["payload"]

    oldest_sequence = struct.unpack_from("<I", status_payload, 20)[0]
    latest_sequence = struct.unpack_from("<I", status_payload, 16)[0]
    buffered_frames = struct.unpack_from("<H", status_payload, 24)[0]

    if oldest_sequence == 0xFFFFFFFF:
        raise SystemExit("Buffer is empty; cannot test an expired sequence")

    # Ask for exactly one sequence immediately before the oldest retained frame.
    unavailable_sequence = previous_sequence(oldest_sequence)

    request_payload = struct.pack(
        "<IH",
        unavailable_sequence,
        1,
    )

    read_request = build_packet(
        READ_FRAMES,
        REQUEST_ID_FRAMES,
        request_payload,
    )

    ser.write(read_request)
    ser.flush()

    packet = read_packet(ser)


if packet["message_type"] != FRAME_DATA_RESPONSE:
    raise SystemExit(
        f"Unexpected response type 0x{packet['message_type']:02X}"
    )

payload = packet["payload"]

if len(payload) != 15:
    raise SystemExit(
        f"Expected 15-byte metadata-only payload, got {len(payload)}"
    )

status = payload[0]
requested_first = struct.unpack_from("<I", payload, 1)[0]
frame_count = struct.unpack_from("<H", payload, 5)[0]
oldest_available = struct.unpack_from("<I", payload, 7)[0]
latest_available = struct.unpack_from("<I", payload, 11)[0]

print("UNAVAILABLE-SEQUENCE TEST")
print(f"Oldest from STATUS:      {oldest_sequence}")
print(f"Latest from STATUS:      {latest_sequence}")
print(f"Buffered frames:         {buffered_frames}")
print(f"Requested unavailable:   {unavailable_sequence}")
print()
print(f"Response status:         0x{status:02X}")
print(f"Requested first echoed:  {requested_first}")
print(f"Returned frames:         {frame_count}")
print(f"Oldest available now:    {oldest_available}")
print(f"Latest available now:    {latest_available}")
print(f"Response payload bytes:  {len(payload)}")
print(f"CRC:                     0x{packet['crc']:04X} VALID")

if status != DATA_NOT_AVAILABLE:
    raise SystemExit(
        f"FAIL: expected DATA_NOT_AVAILABLE (0x05), got 0x{status:02X}"
    )

if frame_count != 0:
    raise SystemExit(
        f"FAIL: expected zero frames, got {frame_count}"
    )

if requested_first != unavailable_sequence:
    raise SystemExit(
        "FAIL: requested sequence was not echoed correctly"
    )

print()
print("PASS: expired sequence correctly returned DATA_NOT_AVAILABLE.")
