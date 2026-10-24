# ArduinoJson (vendored)

Single-header amalgamation of **ArduinoJson 7.4.3**, the version the
firmware is built against (Arduino library manager). MIT licensed; see
`LICENSE.txt`.

Generated from the installed library rather than downloaded:

```bash
python3 -I path/to/amalgamate.py ~/Documents/Arduino/libraries/ArduinoJson/src ArduinoJson.h
```

The amalgamation follows ArduinoJson's own
`extras/scripts/build-single-header.sh`, which needs bash 4 and so does not
run on macOS's stock bash. If the firmware moves to a newer ArduinoJson,
regenerate this file so the host build parses frames the same way.
