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

REQUEST_ID_STATUS = 0x4001
REQUEST_ID_FRAMES = 0x4002

FRAME_SIZE = 48
FRAME_COUNT_REQUESTED = 20


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
        raise RuntimeError(
            f"Bad or missing sync: {sync.hex()}"
        )

    header = read_exact(ser, 7)

    if len(header) != 7:
        raise RuntimeError("Incomplete header")

    version = header[0]
    message_type = header[1]
    node_address = header[2]

    request_id, payload_length = struct.unpack_from(
        "<HH",
        header,
        3
    )

    payload = read_exact(
        ser,
        payload_length
    )

    crc_bytes = read_exact(
        ser,
        2
    )

    if len(payload) != payload_length:
        raise RuntimeError("Incomplete payload")

    if len(crc_bytes) != 2:
        raise RuntimeError("Incomplete CRC")

    received_crc = struct.unpack(
        "<H",
        crc_bytes
    )[0]

    calculated_crc = crc16_ccitt_false(
        header + payload
    )

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


with serial.Serial(
    PORT,
    BAUD,
    timeout=3.0
) as ser:

    ser.reset_input_buffer()

    #
    # Get current STATUS.
    #
    status_request = build_packet(
        GET_STATUS,
        REQUEST_ID_STATUS,
    )

    ser.write(status_request)
    ser.flush()

    status_packet = read_packet(ser)

    if status_packet["message_type"] != STATUS_RESPONSE:
        raise RuntimeError(
            "Expected STATUS response"
        )

    status_payload = status_packet["payload"]

    latest_sequence = struct.unpack_from(
        "<I",
        status_payload,
        16
    )[0]

    oldest_sequence = struct.unpack_from(
        "<I",
        status_payload,
        20
    )[0]

    buffered_frames = struct.unpack_from(
        "<H",
        status_payload,
        24
    )[0]

    if latest_sequence == 0xFFFFFFFF:
        raise SystemExit(
            "No frames available"
        )

    if buffered_frames < FRAME_COUNT_REQUESTED:
        raise SystemExit(
            f"Need at least {FRAME_COUNT_REQUESTED} "
            f"buffered frames; only {buffered_frames} available"
        )

    #
    # Request the most recent 20 complete frames.
    #
    first_sequence = (
        latest_sequence
        - (FRAME_COUNT_REQUESTED - 1)
    ) & 0xFFFFFFFF

    request_payload = struct.pack(
        "<IH",
        first_sequence,
        FRAME_COUNT_REQUESTED,
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
        f"Unexpected response type "
        f"0x{packet['message_type']:02X}"
    )

payload = packet["payload"]

status = payload[0]

requested_first = struct.unpack_from(
    "<I",
    payload,
    1
)[0]

frame_count = struct.unpack_from(
    "<H",
    payload,
    5
)[0]

oldest_available = struct.unpack_from(
    "<I",
    payload,
    7
)[0]

latest_available = struct.unpack_from(
    "<I",
    payload,
    11
)[0]

expected_payload_bytes = (
    15
    + FRAME_SIZE * FRAME_COUNT_REQUESTED
)

complete_packet_bytes = (
    2
    + 7
    + len(payload)
    + 2
)

print("MAX-FRAME READ TEST")
print(
    f"Oldest from STATUS:      {oldest_sequence}"
)
print(
    f"Latest from STATUS:      {latest_sequence}"
)
print(
    f"Requested first:         {first_sequence}"
)
print()

print(
    f"Response status:         0x{status:02X}"
)
print(
    f"Returned frames:         {frame_count}"
)
print(
    f"Oldest available now:    {oldest_available}"
)
print(
    f"Latest available now:    {latest_available}"
)
print(
    f"Response payload bytes:  {len(payload)}"
)
print(
    f"Complete packet bytes:   {complete_packet_bytes}"
)
print(
    f"CRC:                     "
    f"0x{packet['crc']:04X} VALID"
)

if status != 0x00:
    raise SystemExit(
        f"FAIL: expected OK (0x00), "
        f"got 0x{status:02X}"
    )

if frame_count != FRAME_COUNT_REQUESTED:
    raise SystemExit(
        f"FAIL: expected "
        f"{FRAME_COUNT_REQUESTED} frames, "
        f"got {frame_count}"
    )

if len(payload) != expected_payload_bytes:
    raise SystemExit(
        f"FAIL: expected "
        f"{expected_payload_bytes}-byte payload, "
        f"got {len(payload)}"
    )

if complete_packet_bytes != 986:
    raise SystemExit(
        f"FAIL: expected 986-byte packet, "
        f"got {complete_packet_bytes}"
    )

#
# Verify all 20 frames are present and sequential.
#
sequences = []

for i in range(frame_count):

    start = 15 + i * FRAME_SIZE
    end = start + FRAME_SIZE

    frame_bytes = payload[start:end]

    if len(frame_bytes) != FRAME_SIZE:
        raise SystemExit(
            f"FAIL: frame {i} has "
            f"{len(frame_bytes)} bytes"
        )

    sequence = struct.unpack_from(
        "<I",
        frame_bytes,
        0
    )[0]

    sequences.append(sequence)


for i in range(1, len(sequences)):

    expected = (
        sequences[i - 1] + 1
    ) & 0xFFFFFFFF

    if sequences[i] != expected:
        raise SystemExit(
            f"FAIL: sequence discontinuity "
            f"at frame {i}: "
            f"got {sequences[i]}, "
            f"expected {expected}"
        )


if sequences[0] != first_sequence:
    raise SystemExit(
        f"FAIL: first returned sequence "
        f"{sequences[0]} does not match "
        f"requested {first_sequence}"
    )


print(
    f"First returned sequence: {sequences[0]}"
)

print(
    f"Last returned sequence:  {sequences[-1]}"
)

print()
print(
    "PASS: full 20-frame / 986-byte "
    "READ_FRAMES response is valid."
)
