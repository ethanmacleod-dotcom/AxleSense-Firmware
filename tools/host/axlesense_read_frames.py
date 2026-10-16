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

REQUEST_ID_STATUS = 0x2001
REQUEST_ID_FRAMES = 0x2002

FRAME_SIZE = 48
MAX_FRAMES = 5


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


def decode_wire_frame(data):
    if len(data) != FRAME_SIZE:
        raise ValueError("Frame must be exactly 48 bytes")

    sequence = struct.unpack_from("<I", data, 0)[0]
    timestamp_us = struct.unpack_from("<Q", data, 4)[0]
    enabled_mask = data[12]

    raw = [
        struct.unpack_from("<I", data, 16 + 4 * i)[0]
        for i in range(4)
    ]

    offsets = [
        struct.unpack_from("<I", data, 32 + 4 * i)[0]
        for i in range(4)
    ]

    return {
        "sequence": sequence,
        "timestamp_us": timestamp_us,
        "enabled_mask": enabled_mask,
        "raw": raw,
        "offsets": offsets,
    }


with serial.Serial(PORT, BAUD, timeout=2.0) as ser:
    ser.reset_input_buffer()

    # First get STATUS so we know a sequence that is definitely recent.
    status_request = build_packet(
        GET_STATUS,
        REQUEST_ID_STATUS,
    )

    ser.write(status_request)
    ser.flush()

    status_packet = read_packet(ser)

    if status_packet["message_type"] != STATUS_RESPONSE:
        raise RuntimeError("Expected STATUS response")

    status_payload = status_packet["payload"]

    latest_sequence = struct.unpack_from("<I", status_payload, 16)[0]
    oldest_sequence = struct.unpack_from("<I", status_payload, 20)[0]
    buffered_frames = struct.unpack_from("<H", status_payload, 24)[0]

    print("STATUS")
    print(f"Oldest available: {oldest_sequence}")
    print(f"Latest available: {latest_sequence}")
    print(f"Buffered frames:  {buffered_frames}")
    print()

    if latest_sequence == 0xFFFFFFFF:
        raise SystemExit("No frames available")

    # Pick five recent frames safely inside the retained window.
    if latest_sequence >= (MAX_FRAMES - 1):
        first_sequence = latest_sequence - (MAX_FRAMES - 1)
    else:
        first_sequence = oldest_sequence

    payload = struct.pack(
        "<IH",
        first_sequence,
        MAX_FRAMES,
    )

    read_request = build_packet(
        READ_FRAMES,
        REQUEST_ID_FRAMES,
        payload,
    )

    # First READ_FRAMES request.
    ser.write(read_request)
    ser.flush()

    packet = read_packet(ser)

    # Retry the exact same READ_FRAMES packet.
    ser.write(read_request)
    ser.flush()

    retry_packet = read_packet(ser)


if packet["message_type"] != FRAME_DATA_RESPONSE:
    raise SystemExit(
        f"Unexpected response type 0x{packet['message_type']:02X}"
    )

payload = packet["payload"]

status = payload[0]
requested_first = struct.unpack_from("<I", payload, 1)[0]
frame_count = struct.unpack_from("<H", payload, 5)[0]
oldest_available = struct.unpack_from("<I", payload, 7)[0]
latest_available = struct.unpack_from("<I", payload, 11)[0]

print("FRAME_DATA")
print(f"Status:                  0x{status:02X}")
print(f"Requested first:         {requested_first}")
print(f"Returned frames:         {frame_count}")
print(f"Oldest available now:    {oldest_available}")
print(f"Latest available now:    {latest_available}")
print(f"Response payload bytes:  {len(payload)}")
print(f"CRC:                     0x{packet['crc']:04X} VALID")
print()

expected_payload_size = 15 + FRAME_SIZE * frame_count

if len(payload) != expected_payload_size:
    raise SystemExit(
        f"Bad payload size: got {len(payload)}, "
        f"expected {expected_payload_size}"
    )

for i in range(frame_count):
    start = 15 + i * FRAME_SIZE
    end = start + FRAME_SIZE

    frame = decode_wire_frame(payload[start:end])

    print(
        f"Frame {i}: "
        f"seq={frame['sequence']} "
        f"time={frame['timestamp_us']} us "
        f"mask=0x{frame['enabled_mask']:02X}"
    )

    print(
        f"  raw:     "
        f"X={frame['raw'][0]} "
        f"Y={frame['raw'][1]} "
        f"Z={frame['raw'][2]} "
        f"strain={frame['raw'][3]}"
    )

    print(
        f"  offsets: "
        f"X={frame['offsets'][0]} "
        f"Y={frame['offsets'][1]} "
        f"Z={frame['offsets'][2]} "
        f"strain={frame['offsets'][3]} us"
    )


if retry_packet["message_type"] != FRAME_DATA_RESPONSE:
    raise SystemExit(
        f"Unexpected retry response type 0x{retry_packet['message_type']:02X}"
    )

retry_payload = retry_packet["payload"]

retry_status = retry_payload[0]
retry_requested_first = struct.unpack_from("<I", retry_payload, 1)[0]
retry_frame_count = struct.unpack_from("<H", retry_payload, 5)[0]

original_frame_bytes = packet["payload"][15:]
retry_frame_bytes = retry_payload[15:]

print()
print("RETRY CHECK")
print(f"Retry status:            0x{retry_status:02X}")
print(f"Retry requested first:   {retry_requested_first}")
print(f"Retry returned frames:   {retry_frame_count}")
print(
    "Frame data identical:   "
    + ("YES" if retry_frame_bytes == original_frame_bytes else "NO")
)
print(f"Retry CRC:               0x{retry_packet['crc']:04X} VALID")

if retry_status != status:
    raise SystemExit("Retry status differs from original response")

if retry_requested_first != requested_first:
    raise SystemExit("Retry requested-first field differs")

if retry_frame_count != frame_count:
    raise SystemExit("Retry frame count differs")

if retry_frame_bytes != original_frame_bytes:
    raise SystemExit("Retry frame data differs from original response")
