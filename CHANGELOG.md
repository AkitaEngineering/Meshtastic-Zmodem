# Changelog

All notable changes to this project are documented here.

## [Unreleased]
- Improve stream buffer safety; prevent VLA usage.
- Implement ZFILE subpacket parsing in `ZModemEngine`.
- Fix hex header CRC skipping and ZDATA offset flags.
- Add `_handleZmodemState` mapping in `AkitaMeshZmodem`.
- Add local `parseNodeId` helper in `ZmodemModule` to avoid missing dependency.
- Parse binary ZMODEM headers and keep partial ZFILE/ZDATA subpackets across loop ticks.
- Validate control-header CRCs and route data-port traffic during both send and receive flows.
- Coalesce multiple received mesh packets in the stream wrapper to reduce dropped protocol bytes.
- Update README and usage docs to document current verification scope and helper-tool prerequisites.

## [1.1.0]
- Initial public release.
