# Sky cube assets

These files are 512 x 512 RGBA PNG cube faces for the SDL GPU client.
They use sRGB color. The renderer turns sampled values into linear color
before the HDR scene pass.

The source panoramas are original LG Duel project art. They stay unchanged
under `art/sky/source-art` and do not ship with the game.

| Sky | Source SHA-256 |
| --- | --- |
| `aurora` | `77a64a9f474e4aafb5ef21cf73573d99fc6cc9b16724c4e3ac058dae6210150e` |
| `crimson-sunset` | `bb45b3c3ab2a2e54e3760bbad68ce011e40944bf6bf4c9d5cd2035ade6a35772` |

## Face rules

The tool writes faces in this order: `posx`, `negx`, `posy`, `negy`,
`posz`, `negz`. For face coordinates `u` and `v` in `[-1, 1]`, it uses:

| Face | Direction before normalizing |
| --- | --- |
| `posx` | `(1, -v, -u)` |
| `negx` | `(-1, -v, u)` |
| `posy` | `(u, 1, v)` |
| `negy` | `(u, -1, -v)` |
| `posz` | `(u, -v, 1)` |
| `negz` | `(-u, -v, -1)` |

The end pixels sit on exact cube edges. Sampling wraps across the panorama's
left and right seam and clamps at its top and bottom. The tool reports all 12
edge pairs and all 8 corners in its continuity check.

## Rebuild

Run these commands from the project root. Pillow supplies PNG input and
output.

```powershell
python tools/convert_sky_panorama.py `
  art/sky/source-art/aurora-panorama.png `
  assets/sky/aurora `
  --size 512 `
  --expect-sha256 77a64a9f474e4aafb5ef21cf73573d99fc6cc9b16724c4e3ac058dae6210150e

python tools/convert_sky_panorama.py `
  art/sky/source-art/crimson-sunset-panorama.png `
  assets/sky/crimson-sunset `
  --size 512 `
  --expect-sha256 bb45b3c3ab2a2e54e3760bbad68ce011e40944bf6bf4c9d5cd2035ade6a35772
```

The current output hashes are:

| Sky | Face | SHA-256 |
| --- | --- | --- |
| `aurora` | `posx.png` | `a6482aaafab30d6a3c040a86124522f3ebb7863e67603d062d0b2e9a6a2d30d1` |
| `aurora` | `negx.png` | `d7e8e182e720c6f23d6bcd7c5aeee7943d6b817571bcabb0867d41db419ad8fe` |
| `aurora` | `posy.png` | `d4c1960f8bb3af20c80eed81964174f26b9f6b6762c486c8120c65096fcd0308` |
| `aurora` | `negy.png` | `5aba8afe1069e714cde7f578f74bad3c946b852ed284a1a90faa59bcc86c05f8` |
| `aurora` | `posz.png` | `fc0b751952360ecdc6358712fb65862097c9d443e27baa067126258e98478d08` |
| `aurora` | `negz.png` | `98221ef844a12c0cda426aec2b65c5d53fc5e715695c317ea33ce3cd4de024fc` |
| `crimson-sunset` | `posx.png` | `57e6ea88b08dc8aa230384fa3ad03b7e147334ead7868fe9db8bd87158305ae8` |
| `crimson-sunset` | `negx.png` | `587d1bbc06159d4e05e35faed50566944c79cd660a6d0fc7c60424f9cf3915f9` |
| `crimson-sunset` | `posy.png` | `993aade0f694bc95342e4e70c0f9ebb278fd9c1089a74f901e35fea11c82baeb` |
| `crimson-sunset` | `negy.png` | `7b7ac932a4aa6bed6f131ac660edf4236c2141215aad8d1d36680a57a7e6a4f2` |
| `crimson-sunset` | `posz.png` | `808d81825cca9d182ba44443054d8f9a32da1cbafd4bd94736706ac1132f6c28` |
| `crimson-sunset` | `negz.png` | `cfa3627821209e06e3b799b85a60e74920e585ff60e69c774704e08b27fbdf99` |
