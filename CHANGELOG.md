# Changelog

## [1.2.1](https://github.com/jrjohn/arcana-embedded-esp32/compare/v1.2.0...v1.2.1) (2026-06-29)


### Bug Fixes

* **sdfat:** heal "free but referenced" clusters at mount (dual-FAT power-fail) ([#16](https://github.com/jrjohn/arcana-embedded-esp32/issues/16)) ([764f423](https://github.com/jrjohn/arcana-embedded-esp32/commit/764f4237befbcc12a5bc507ef598b7caf3c93bf4))

## [1.2.0](https://github.com/jrjohn/arcana-embedded-esp32/compare/v1.1.0...v1.2.0) (2026-06-24)


### Features

* **storage:** FAT32 → power-fail-safe dual-FAT exFAT (+ AES-256-CCM commands, TLS transport, ECG/upload fixes) ([#11](https://github.com/jrjohn/arcana-embedded-esp32/issues/11)) ([6059b45](https://github.com/jrjohn/arcana-embedded-esp32/commit/6059b45a3b36456d8b57cc5cc9f2b1c2a7b7529e))

## [1.1.0](https://github.com/jrjohn/arcana-embedded-esp32/compare/v1.0.0...v1.1.0) (2026-06-11)


### Features

* **board:** ALIENTEK DNESP32S3 (ESP32-S3) dual-board support ([#10](https://github.com/jrjohn/arcana-embedded-esp32/issues/10)) ([c54fadd](https://github.com/jrjohn/arcana-embedded-esp32/commit/c54fadd3da19453e4d73a93265580ff35c4a0903))


### Bug Fixes

* **ci:** derive jenkins_home host path from container mounts ([#8](https://github.com/jrjohn/arcana-embedded-esp32/issues/8)) ([d1c2c12](https://github.com/jrjohn/arcana-embedded-esp32/commit/d1c2c12429f059c24a2c221d3d9631828540e842))
