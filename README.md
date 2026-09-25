# Wireless Power Components

Shared ESP-IDF components for Wireless Power Meter Lite, Wireless Power Meter Pro V2, and Wireless Power Switch Button.

## Status

This repository is being prepared for component extraction. No firmware project currently depends on it. Components will be moved here individually after their interfaces and cross-device behavior are checked.

## Layout

Each component will remain an independent ESP-IDF component with its own `CMakeLists.txt` and, where needed, `idf_component.yml`:

```text
components/
  common/
  bsp/
  middleware/
```

Products will depend on the required component directories through ESP-IDF Component Manager Git dependencies, pinned to a repository tag or commit. Releases will be tagged for the repository as a whole.

The first extraction candidates are the components already identical between the Lite and Pro projects. Components shared with the ESP32-C3 remote will be unified only after their hardware differences, ESP-NOW protocol, pairing, and stored data compatibility are tested.

## License

MIT. See [LICENSE](LICENSE).
