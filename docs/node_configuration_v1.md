# AxleSense Persistent Configuration Formats V1

> **Frozen V1 binary interface.** The generic persistent storage record V1 and
> AxleSense node payload schema V1 defined here are frozen binary interfaces.
> Any incompatible payload change requires a new payload schema version. Any
> incompatible record-envelope change requires a new storage-format version.

## Serialization conventions

- All multibyte integers are serialized explicitly in little-endian byte order.
- Signed integers use two's-complement representation.
- Firmware and host software must encode and decode individual fields. Neither
  the record nor the payload may be stored, transmitted, or mirrored as a raw C
  structure.
- Reserved fields are written as zero unless this specification explicitly says
  otherwise. Readers ignore reserved values after validating the applicable
  version, allowing compatible future use.
- Flash writes use 32-bit-aligned addresses and 32-bit programming units. The
  record header and payload start on 32-bit boundaries. Any bytes after the
  payload needed to reach a 32-bit boundary remain erased (`0xFF`) and are not
  part of the payload or CRC.

## 1. Generic persistent storage record V1

Each record occupies the beginning of one erased A/B flash slot. Each current
slot is one 128 KiB STM32 flash sector. The generic format permits a maximum
payload of 4096 bytes, even though the containing sector is larger.

### 1.1 Header layout

The header is exactly 48 bytes. The payload begins at byte offset `0x30`.

| Offset | Size | Type | Field | V1 value or meaning |
|---:|---:|---|---|---|
| `0x00` | 4 | `uint32` | `magic` | Bytes `"AXCF"`; little-endian value `0x46435841` |
| `0x04` | 2 | `uint16` | `storage_format_version` | `1` |
| `0x06` | 2 | `uint16` | `header_length` | `48` (`0x0030`) |
| `0x08` | 4 | `uint32` | `record_type` | `1` = AxleSense node configuration |
| `0x0C` | 4 | `uint32` | `payload_schema_version` | Schema used to decode the payload; `1` for the payload in section 2 |
| `0x10` | 4 | `uint32` | `payload_length` | Exact payload byte count; `256` for schema V1 |
| `0x14` | 4 | `uint32` | `flags` | `0` in V1; all bits reserved |
| `0x18` | 8 | `uint64` | `generation` | Monotonically increasing record generation |
| `0x20` | 4 | `uint32` | `reserved0` | `0` |
| `0x24` | 4 | `uint32` | `reserved1` | `0` |
| `0x28` | 4 | `uint32` | `crc32` | CRC-32/ISO-HDLC defined below |
| `0x2C` | 4 | `uint32` | `commit_marker` | Bytes `"CMIT"`; little-endian value `0x54494D43` |
| `0x30` | variable | bytes | `payload` | Explicitly serialized payload |

An erased word reads as `0xFFFFFFFF`. Neither magic value nor the commit marker
is equal to the erased state.

### 1.2 CRC definition

The record uses **CRC-32/ISO-HDLC** with these parameters:

| Parameter | Value |
|---|---|
| Width | 32 bits |
| Polynomial | `0x04C11DB7` |
| Reflected polynomial | `0xEDB88320` |
| Initial value | `0xFFFFFFFF` |
| Input reflected | Yes |
| Output reflected | Yes |
| Final XOR | `0xFFFFFFFF` |
| Check value for `"123456789"` | `0xCBF43926` |

The CRC input is the exact concatenation of:

1. Header bytes `0x00–0x27`, including both reserved words.
2. Exactly `payload_length` bytes beginning at `0x30`.

The CRC does not cover the `crc32` field at `0x28–0x2B`, the commit marker at
`0x2C–0x2F`, or alignment bytes after the payload.

### 1.3 Slot validity

A slot is valid only when all of the following checks pass:

1. The commit marker is exactly `"CMIT"`.
2. The magic is exactly `"AXCF"`.
3. `storage_format_version` is supported and `header_length` is exactly 48 for
   version 1.
4. `record_type` is supported.
5. `payload_schema_version` is supported for that record type.
6. `payload_length` is nonzero, no greater than 4096, valid for the selected
   payload schema, and fits in the slot after the header.
7. V1 flags and reserved-word requirements are satisfied.
8. The calculated CRC matches the stored CRC.
9. Payload-level validation, including product and hardware compatibility,
   succeeds.

An erased slot fails the commit and magic checks. A partially written slot fails
the commit check because the commit marker is programmed last. A corrupt slot
fails structural, CRC, or payload validation. A record using an unsupported
storage or payload version is incompatible rather than corrupt.

### 1.4 Selecting between A/B slots

- If only one slot is valid, use it.
- If neither slot is valid, use compiled safe defaults, report that persistent
  configuration is unavailable, and do not claim that calibration is valid.
- If both slots are valid, choose the newer `uint64` generation using modular
  comparison. Generation `a` is newer than `b` when:

  ```text
  a != b && (uint64_t)(a - b) < 2^63
  ```

- A difference of exactly `2^63` is ambiguous and must be treated as a storage
  error rather than choosing silently.
- Identical generations with different record contents are inconsistent and
  must be reported as a storage error. Identical byte-for-byte records may be
  accepted deterministically.

This comparison handles normal counter wraparound as long as valid generations
are never separated by `2^63` or more updates.

### 1.5 Save sequence

Configuration writes occur only during a controlled pause in acquisition.

1. Validate and explicitly serialize the complete new payload in RAM.
2. Determine the active valid slot and select the other slot as the destination.
3. If a valid active record exists, choose its generation plus one modulo
   `2^64`. If no valid record exists, use generation one for the first save.
4. Build the header in RAM with the final metadata and CRC, but leave the flash
   commit word erased.
5. Erase the destination sector. Do not erase or modify the active slot.
6. Verify that the destination area needed by the record is erased.
7. Program header bytes `0x00–0x2B`, excluding the commit word.
8. Program the payload and leave any alignment padding erased.
9. Read the written header and payload back, recalculate the CRC, and verify all
   bytes and payload validation rules.
10. Program `"CMIT"` at `0x2C` as the final flash operation.
11. Read back and verify the commit marker, then treat the new slot as active.

The commit marker is written last so power loss during erase, programming, or
verification cannot make an incomplete destination record appear valid. The old
slot remains available until the new slot has been completely written and
committed.

### 1.6 Boot validation sequence

1. Read the fixed header from both slots.
2. Check each commit marker before trusting variable header fields.
3. Validate magic, storage version, header length, record type, flags, lengths,
   and bounds.
4. Recalculate and compare the CRC over the precisely defined byte ranges.
5. Decode and validate the payload according to its schema version.
6. Apply product, hardware, mapping, and analog/reference compatibility checks.
7. Select the newest valid slot using the A/B generation rules.
8. If neither slot is valid, load compiled safe defaults and expose a persistent
   storage diagnostic.

## 2. AxleSense node payload schema V1

Payload schema V1 is exactly **256 bytes**, covering offsets `0x000–0x0FF`.

### 2.1 Region summary

| Range | Size | Category |
|---|---:|---|
| `0x000–0x01F` | 32 | Node and hardware identity |
| `0x020–0x02F` | 16 | Communications configuration |
| `0x030–0x05F` | 48 | ADC and acquisition configuration |
| `0x060–0x087` | 40 | Strain calibration |
| `0x088–0x0CF` | 72 | Accelerometer calibration |
| `0x0D0–0x0FF` | 48 | Manufacturing and QA provenance |

### 2.2 Node and hardware identity

| Offset | Size | Type | Field | Default | Validation | Permission |
|---:|---:|---|---|---:|---|---|
| `0x000` | 8 | `uint64` | `node_serial_number` | `0` | Zero means unprovisioned | Manufacturing |
| `0x008` | 8 | `uint64` | `pcb_serial_number` | `0` | Zero means unprovisioned | Manufacturing |
| `0x010` | 4 | `uint32` | `product_id` | `1` | Must be recognized by firmware | Manufacturing |
| `0x014` | 2 | `uint16` | `hardware_revision_major` | `0` | Must be supported | Manufacturing |
| `0x016` | 2 | `uint16` | `hardware_revision_minor` | `0` | Must be supported | Manufacturing |
| `0x018` | 2 | `uint16` | `assembly_variant` | `0` | Product-specific enumeration | Manufacturing |
| `0x01A` | 2 | `uint16` | `identity_flags` | `0` | Defined bits only | Manufacturing |
| `0x01C` | 4 | bytes | Reserved | `0` | Write as zero | Fixed |

`identity_flags` defines bit 0 as identity provisioned and bit 1 as
manufacturing completed. Bits 2–15 are reserved.

### 2.3 Communications configuration

UART framing is a firmware/protocol property in V1 and is not serialized.

| Offset | Size | Type | Field | Default | Validation | Permission |
|---:|---:|---|---|---:|---|---|
| `0x020` | 2 | `uint16` | `node_address` | `0` | Zero unassigned; operational range `1–254`; `255` invalid | Logger |
| `0x022` | 2 | bytes | Reserved | `0` | Write as zero | Fixed |
| `0x024` | 4 | `uint32` | `uart_baud_rate` | **`115200`** | Must be supported by firmware | Logger |
| `0x028` | 8 | bytes | Reserved | `0` | Write as zero | Fixed |

The initial allowed baud rates are `9600`, `19200`, `38400`, `57600`,
`115200`, `230400`, `460800`, and `921600`. Firmware may support a subset but
must reject an unsupported value instead of silently substituting one.

### 2.4 Fixed logical channel mapping

The mapping is fixed by the PCB and firmware and is not payload-configurable.

| Logical channel | Sensor | Physical ADC input |
|---:|---|---|
| 0 | Accelerometer X | AIN0 to AVSS |
| 1 | Accelerometer Y | AIN1 to AVSS |
| 2 | Accelerometer Z | AIN2 to AVSS |
| 3 | Strain | AIN4/AIN5 differential |

### 2.5 ADC and acquisition configuration

| Offset | Size | Type | Field | Default | Validation | Permission |
|---:|---:|---|---|---|---|---|
| `0x030` | 1 | `uint8` | `enabled_channel_mask` | `0x08` | Only bits 0–3 allowed | Logger |
| `0x031` | 1 | `uint8` | `adc_power_mode` | `2` | Enumeration `0–2` | Logger |
| `0x032` | 1 | `uint8` | `acquisition_mode` | `0` | Supported enumeration only | Logger |
| `0x033` | 1 | `uint8` | `adc_mapping_version` | `1` | Must be supported | Firmware/manufacturing |
| `0x034` | 4 | `uint8[4]` | `channel_gain` | `{1,1,1,16}` | Each is `1,2,4,8,16,32,64,128` | Logger, calibration-protected |
| `0x038` | 4 | `uint8[4]` | `filter_mode` | `{0,0,0,0}` | Supported semantic modes only | Logger |
| `0x03C` | 4 | `uint8[4]` | `filter_options` | `{0,0,0,0}` | Defined bits only | Logger |
| `0x040` | 16 | `uint32[4]` | `output_data_rate_millihz` | `{0,0,0,0}` | Firmware-supported semantic rates | Logger |
| `0x050` | 2 | `uint16` | `startup_settling_conversions` | `256` | `0–4096` | Logger |
| `0x052` | 1 | `uint8` | `analog_reference_config_version` | `1` | Must match known board topology | Firmware/manufacturing |
| `0x053` | 1 | bytes | Reserved | `0` | Write as zero | Fixed |
| `0x054` | 12 | bytes | Reserved through `0x05F` | `0` | Write as zero | Fixed |

`adc_power_mode` values are 0 low power, 1 mid power, and 2 full power.

`acquisition_mode` value 0 is a firmware-controlled continuous scan of enabled
channels. Other values are reserved.

`filter_mode` is a semantic setting:

| Value | Meaning |
|---:|---|
| 0 | Firmware-validated default profile |
| 1 | SINC4 |
| 2 | SINC3 |
| 3 | Fast-settling filter |
| 4 | 50/60 Hz rejection profile |

`filter_options` bit 0 requests single-cycle settling and bit 1 requests
enhanced 60 Hz rejection. Bits 2–7 are reserved.

`output_data_rate_millihz` contains the requested per-channel output rate. For
example, `1000` means 1 sample/s, `19200` means 19.2 samples/s, and `1000000`
means 1000 samples/s. Zero selects the validated default profile. Firmware
converts these semantic fields into AD7124 register settings and rejects
unsupported combinations instead of silently clamping them.

`adc_mapping_version` identifies the interpretation of the fixed logical and
physical channel mapping. `analog_reference_config_version` identifies the
scale-affecting ADC reference and board-level analog topology. It changes only
when that topology changes in a way that can affect conversion scale.

### 2.6 Strain calibration

The V1 strain model is:

```text
delta_code = raw_code - strain_zero_code
strain_pstrain = strain_offset_pstrain
                  + delta_code * strain_scale_pstrain_per_code
```

One microstrain equals `1,000,000` picostrain.

| Offset | Size | Type | Field | Default | Validation | Permission |
|---:|---:|---|---|---:|---|---|
| `0x060` | 2 | `uint16` | `strain_calibration_model` | `1` | `1` = V1 affine model | Calibration |
| `0x062` | 2 | `uint16` | `strain_calibration_flags` | `0` | Defined bits only | Calibration |
| `0x064` | 4 | `uint32` | `strain_zero_code` | `0` | `0–0xFFFFFF` | Calibration |
| `0x068` | 4 | `int32` | `strain_scale_pstrain_per_code` | `0` | Nonzero when valid | Calibration |
| `0x06C` | 8 | `int64` | `strain_offset_pstrain` | `0` | Recommended range ±`10^12` | Calibration |
| `0x074` | 4 | `int32` | `strain_calibration_temperature_mC` | `INT32_MIN` | Millidegrees Celsius; `INT32_MIN` unknown | Calibration |
| `0x078` | 4 | `uint32` | `strain_bound_product_id` | `0` | Must match active product | Calibration |
| `0x07C` | 2 | `uint16` | `strain_bound_hardware_revision_major` | `0` | Must match hardware policy | Calibration |
| `0x07E` | 2 | `uint16` | `strain_bound_hardware_revision_minor` | `0` | Must match hardware policy | Calibration |
| `0x080` | 1 | `uint8` | `strain_bound_mapping_version` | `0` | Must match active mapping version | Calibration |
| `0x081` | 1 | `uint8` | `strain_bound_analog_reference_version` | `0` | Must match active analog/reference version | Calibration |
| `0x082` | 1 | `uint8` | `strain_bound_gain` | `0` | Must match active channel 3 gain | Calibration |
| `0x083` | 1 | `uint8` | `strain_calibration_filter_mode` | `0` | Informational only | Calibration |
| `0x084` | 4 | `uint32` | `strain_calibration_rate_millihz` | `0` | Informational only | Calibration |

`strain_calibration_flags` bit 0 means calibration valid and bit 1 means a
calibration temperature was recorded. Bits 2–15 are reserved. Calibration filter
mode and rate retain test provenance but do not control calibration validity.

### 2.7 Accelerometer calibration

The V1 model is independently affine for each X, Y, and Z axis:

```text
acceleration_ng[axis] =
    (raw_code[axis] - accel_zero_code[axis])
    * accel_scale_ng_per_code[axis]
```

One `g` equals `1,000,000,000 ng`. Signed scale values support ADC polarity
without making physical wiring configurable.

| Offset | Size | Type | Field | Default | Validation | Permission |
|---:|---:|---|---|---:|---|---|
| `0x088` | 2 | `uint16` | `accel_calibration_model` | `1` | `1` = independent per-axis affine model | Calibration |
| `0x08A` | 1 | `uint8` | `accel_valid_axis_mask` | `0` | Bits 0=X, 1=Y, 2=Z | Calibration |
| `0x08B` | 1 | `uint8` | `accel_calibration_flags` | `0` | Defined bits only | Calibration |
| `0x08C` | 12 | `uint32[3]` | `accel_zero_code` | `{0,0,0}` | Each `0–0xFFFFFF` | Calibration |
| `0x098` | 12 | `int32[3]` | `accel_scale_ng_per_code` | `{0,0,0}` | Nonzero for every valid axis | Calibration |
| `0x0A4` | 4 | `int32` | `accel_calibration_temperature_mC` | `INT32_MIN` | Millidegrees Celsius; `INT32_MIN` unknown | Calibration |
| `0x0A8` | 4 | `uint32` | `accel_bound_product_id` | `0` | Must match active product | Calibration |
| `0x0AC` | 2 | `uint16` | `accel_bound_hardware_revision_major` | `0` | Must match hardware policy | Calibration |
| `0x0AE` | 2 | `uint16` | `accel_bound_hardware_revision_minor` | `0` | Must match hardware policy | Calibration |
| `0x0B0` | 1 | `uint8` | `accel_bound_mapping_version` | `0` | Must match active mapping version | Calibration |
| `0x0B1` | 1 | `uint8` | `accel_bound_analog_reference_version` | `0` | Must match active analog/reference version | Calibration |
| `0x0B2` | 3 | `uint8[3]` | `accel_bound_gain` | `{0,0,0}` | Per-axis match to channels 0–2 | Calibration |
| `0x0B5` | 3 | `uint8[3]` | `accel_calibration_filter_mode` | `{0,0,0}` | Informational only | Calibration |
| `0x0B8` | 12 | `uint32[3]` | `accel_calibration_rate_millihz` | `{0,0,0}` | Informational only | Calibration |
| `0x0C4` | 3 | `uint8[3]` | `accel_calibration_filter_options` | `{0,0,0}` | Informational only | Calibration |
| `0x0C7` | 9 | bytes | Reserved through `0x0CF` | `0` | Write as zero | Fixed |

`accel_calibration_flags` bit 0 means a calibration temperature was recorded and
bit 1 means calibration used the ±X/±Y/±Z fixture procedure. Bits 2–7 are
reserved. Calibration validity is per axis through `accel_valid_axis_mask`.

### 2.8 Manufacturing and QA provenance

| Offset | Size | Type | Field | Default | Validation | Permission |
|---:|---:|---|---|---:|---|---|
| `0x0D0` | 8 | `uint64` | `manufacturing_timestamp_utc_s` | `0` | Unix UTC seconds; zero unknown | Manufacturing |
| `0x0D8` | 8 | `uint64` | `calibration_timestamp_utc_s` | `0` | Unix UTC seconds; zero unknown | Calibration |
| `0x0E0` | 4 | `uint32` | `manufacturing_station_id` | `0` | Zero unknown | Manufacturing |
| `0x0E4` | 4 | `uint32` | `calibration_station_id` | `0` | Zero unknown | Calibration |
| `0x0E8` | 4 | `uint32` | `manufacturing_lot_id` | `0` | Zero unknown | Manufacturing |
| `0x0EC` | 4 | `uint32` | `calibration_run_id` | `0` | Zero unknown | Calibration |
| `0x0F0` | 4 | `uint32` | `qa_flags` | `0` | Defined bits only | Manufacturing |
| `0x0F4` | 12 | bytes | Reserved through `0x0FF` | `0` | Write as zero | Fixed |

`qa_flags` assigns:

| Bit | Meaning |
|---:|---|
| 0 | Electrical QA passed |
| 1 | ADC/analog QA passed |
| 2 | RS-485 QA passed |
| 3 | Strain calibration passed |
| 4 | Accelerometer X calibration passed |
| 5 | Accelerometer Y calibration passed |
| 6 | Accelerometer Z calibration passed |
| 7 | Final QA passed |

Bits 8–31 are reserved. Detailed measurements, histories, and reports remain on
the logger or manufacturing system rather than in node flash.

### 2.9 Calibration compatibility

A strain calibration or individual accelerometer-axis calibration is usable
only when:

1. Its validity flag is set.
2. Its coefficients pass numeric validation.
3. Its bound product ID is compatible with the active product.
4. Its bound hardware revision is compatible under an explicit firmware policy.
5. Its bound ADC mapping version matches the active mapping version.
6. Its bound analog/reference configuration version matches the active version.
7. Its bound gain matches the corresponding active channel gain.

The following settings do **not** invalidate the basic raw-code-to-engineering-
unit calibration:

- Digital filter mode
- Digital filter options
- Output data rate
- ADC power mode
- Channel enable state
- Startup settling count
- Acquisition mode

Calibration temperature, filter settings, and output rate recorded during
calibration are metadata. They do not control basic calibration validity.

Changing the gain of a calibrated channel requires either a replacement
calibration bound to the new gain or an explicit request to clear that channel's
calibration-valid flag. Otherwise firmware rejects the entire configuration
update and retains the existing record. The gain update and replacement or
invalidation are one atomic record operation.

### 2.10 Permission groups

#### Ordinary logger-adjustable operating configuration

- `node_address`
- `uart_baud_rate`
- `enabled_channel_mask`
- `adc_power_mode`
- `acquisition_mode`
- `channel_gain`, subject to gain-change protection
- `filter_mode`
- `filter_options`
- `output_data_rate_millihz`
- `startup_settling_conversions`

#### Protected calibration and manufacturing information

- Node, PCB, product, hardware revision, and assembly identity
- Identity flags
- ADC mapping and analog/reference configuration versions
- Calibration coefficients, bindings, validity flags, and metadata
- Manufacturing and calibration provenance
- QA flags

These fields require a deliberate manufacturing, service, or calibration
command rather than an ordinary operating-configuration command.

#### Fixed properties that are never payload-configurable

- Physical ADC wiring and logical channel meanings
- ADC and sensor types
- ADC reference and board topology represented by the version number
- MCU pins and peripheral instances
- RS-485 direction-control wiring
- UART data bits, parity, and stop bits
- Flash slot addresses and sizes
- Generic record magic, version, generation, CRC, and commit marker

### 2.11 Raw-data authority

Raw ADC codes remain authoritative and available regardless of calibration
state. Firmware may label a result as calibrated strain or acceleration only
when the corresponding calibration is valid and compatible under section 2.9.

If calibration is absent, corrupt, numerically invalid, or incompatible,
firmware must continue reporting raw codes, mark engineering units unavailable,
and provide a diagnostic reason. It must never present an uncalibrated result as
valid engineering-unit data.
