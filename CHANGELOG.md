# Changelog

All notable changes to this project are documented here.

## [Unreleased]
- Improve stream buffer safety; prevent VLA usage.
- Implement ZFILE subpacket parsing in `ZModemEngine`.
- Fix hex header CRC skipping and ZDATA offset flags.
- Add `_handleZmodemState` mapping in `AkitaMeshZmodem`.
- Add local `parseNodeId` helper in `ZmodemModule` to avoid missing dependency.

## [1.1.0]
- Initial public release.
