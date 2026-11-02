# Integr8 MQTT reverse-hex wire protocol

## Byte-for-byte C# compatibility

**Outgoing:** Serialize the JSON object compactly as UTF-8 (JsonConvert.SerializeObject with Formatting.None). Convert EACH byte to exactly two uppercase hexadecimal digits. Reverse the ENTIRE hex string at character level. Publish that reversed hex string's ASCII bytes as MQTT payload.

**Incoming:** Receive ASCII hex text. Reverse its characters, trim edge whitespace, convert pairs of hex digits to bytes, interpret as UTF-8, then parse JSON.

Example: "Hi" => UTF-8 48 69 => hex "4869" => wire payload "9684".
Example: "{}" => hex "7B7D" => wire payload "D7B7".

```csharp
// Server outgoing to device: send on topic integr8/<id>/cards/receive
byte[] payload = Encoding.UTF8.GetBytes(
    Reverse(BitConverter.ToString(
        Encoding.UTF8.GetBytes(
            JsonConvert.SerializeObject(obj, Newtonsoft.Json.Formatting.None)))
        .Replace("-", "")));

// Server inbound from device:
string reversedHex = Encoding.UTF8.GetString(payload);
string json = Encoding.UTF8.GetString(HexStringToByteArray(Reverse(reversedHex).Trim()));
```

No encryption, integrity authentication, or TLS is provided by this transform. Enable authenticated and access-controlled network transport before production.

## Development message topics

- `integr8/<DEVICE_ID>/cards/receive`: card upsert/delete commands from server
- `integr8/<DEVICE_ID>/transactions/acknowledge`: application-level ACK from server
- `integr8/<DEVICE_ID>/send`: device responses and transactions

Topics and message field names are development conventions pending cross-check with the deployed Integr8 server; the encoding itself is now matched exactly.

IMPORTANT: The server must persist the transaction and deduplicate `(DEVICE_ID, Sequence)` before sending `{"Sequence":"...","Accepted":true}`. MQTT PUBACK alone does not acknowledge a transaction in the local queue. All three topics now carry reversed-hex encoded JSON, not clear JSON.

Dates in upsert payloads use `yyyy-MM-dd HH:mm:ss` interpreted as **UTC** by the current prototype, then stored as 32-bit Unix epoch seconds. Confirm the server timezone convention before production.
