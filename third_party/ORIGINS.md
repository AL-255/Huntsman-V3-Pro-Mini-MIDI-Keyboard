# Vendored NXP sources

The firmware build is self-contained. The files under `third_party/nxp` are
unmodified source snapshots from the official MCUXpresso SDK repositories.
They were selected from the MCUXpresso Installer 26.06.123 catalog and pinned
to the `release/26.06.00-lts` manifest revisions below.

| Directory | Official repository | Revision |
| --- | --- | --- |
| `core/drivers` | `nxp-mcuxpresso/mcuxsdk-core` | `a910e7645d2d809a3431e1d5f42fca1cdeee69c9` |
| `devices` | `nxp-mcuxpresso/mcux-devices-lpc` | `a38f1d6b6daa9014b3486d8e106af9c35e623e09` |
| `usb` | `nxp-mcuxpresso/mcuxsdk-middleware-usb` | `2289f6c8ce0d07e57421ed9b50e2a82d0c38568b` |
| `cmsis` | `nxp-mcuxpresso/mcu-sdk-cmsis` | `e07cca54712c65a938f41a3e72fbfcb20e2f864a` |
| `component` | `nxp-mcuxpresso/mcux-component` | `c4fba0f97e0c889b9235b53c686d2d2dcc5defa4` |

The corresponding upstream license texts are in `third_party/licenses`.
The keyboard candidate adds `core/drivers/lpc_dma/fsl_dma.{c,h}` from the
same pinned core revision (Git blobs `1368c32a5ee68ed70e8a0739d4cb82ae4c219dd8`
and `379afb1f1b867b023ef5b3867a784a3ee3809390`). This is the LPC descriptor DMA
driver; the older unused `core/drivers/dma` directory is not selected.
Application code is covered by the repository's GPL-2.0 license; vendored
files retain their original SPDX notices and licenses.
