# CD2 dock: lighting over HID

FlyGlow's primary upload path talks to the dock directly over its vendor HID interface. The facts below were
first published by [OpenFlydigi](https://github.com/mkaliaha/openflydigi) (`PROTOCOL.md`, CC0) and were
re-checked against a CD2 with firmware 0.0.3.9.

## 1. Device

| Property | Value |
|---|---|
| USB VID:PID | `37D7:6001` ("Flydigi CD2") |
| Report ID | none (Windows reports it as `0`) |
| Output report | 64 bytes + report-id byte |
| LEDs | 162, rows of 14, 15, 16, 15, 14, 13 ... 3, row-major from the top |

The interface can be opened shared (`FILE_SHARE_READ | FILE_SHARE_WRITE`) while Space Station's service holds it.
Input reports are delivered to every open handle, so each side sees the other's replies and has to match them
by command id.

## 2. Packets

Request (byte offsets within the report, after the report-id byte at `[0]`):

| Offset | Content |
|---|---|
| `[1] [2]` | magic `5A A5` |
| `[3]` | command id |
| `[4]` | length `n` = 2 + payload size (counts `[3]` and `[4]`) |
| `[5 ...]` | payload |
| `[3 + n]` | checksum: 8-bit sum of bytes `[3, 3 + n)` |

Reply: `5A A5 <command id> <length> <payload...>`. Nothing in Flydigi's stack checks reply checksums.

## 3. Uploading a lighting config

1. Command `97` (`0x61`), payload `0A 00 00 <packs hi> <packs lo>`, waits for a `97` reply.
2. Command `98` (`0x62`) per pack, payload `<packs hi> <packs lo> <index hi> <index lo> <size> <data...>`
   with up to 50 data bytes, each pack waiting for its own `98` reply.

The data blob is:

| Bytes | Content |
|---|---|
| 1 | frame count, 1..255 (Space Station sends up to 200; 250 also played fine) |
| 1 | period: frame interval in 20 ms units |
| 1 | brightness, 1..100 |
| 1 | mode, `3` = Custom |
| 1 | direction, `0` |
| 1 | palette size, `0` for Custom |
| 486 x frames | RGB per LED, row-major |

The frame count must match the frames sent: the dock does not clear its frame memory, so a short write plays
leftovers of the previous animation. A pack round trip takes about 9 ms, so a 200-frame animation
(1945 packs) uploads in about 17 s.

The LEDs do not give out light in proportion to the value: on firmware 0.0.3.9 values up to 8 leave them dark,
and the response is close to `(value / 255)^2.2`. FlyGlow therefore sends gamma-2.2-encoded values by default;
`examples/dock-curve-test.flyglow` shows the low values and a 50% checker next to values 128 and 186 for
checking another unit.

## 4. Useful read-only commands

| Command | Reply |
|---|---|
| `1` heartbeat | device type, firmware (`39 00` = 0.0.3.9), switch states |
| `4` uid | the uid Space Station uses (`1960f0f1...`) |
| `20` LED config | mode, brightness, period, direction, palette (no frames) |
