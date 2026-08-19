# Changelog

All notable changes to this project are documented here.

## [Unreleased]

## [1.1.1]
- Allow a new SEND/RECV after COMPLETE or ERROR instead of leaving the node stuck.
- Keep receivers in RECEIVING instead of reporting them as SENDING.
- Accept later mesh sequence numbers after a lost packet so LoRa drops do not stall the stream.
- Ignore duplicate ZDATA and honor ZDATA offsets so retransmits cannot append twice.
- Validate ZFILE subpacket CRCs and escape data-subpacket CRC bytes.
- Restrict receiver ZRINIT keepalives to session start so they cannot skip ZEOF confirmation.
- Add `reset()`, `isBusy()`, and ABORT/STATUS command handling.
- Reject unsafe filesystem paths and broadcast destinations.
- Add native engine and mesh-loopback regression coverage, including restart and packet-loss recovery.

## [1.1.0]
- Initial public release.
