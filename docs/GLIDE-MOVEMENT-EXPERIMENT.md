# Glide movement experiment

This branch tests the movement and camera feel in the linked 26-second clip:

<https://x.com/atomicwastlandd/status/2094509370990104591>

The clip's weapon, health, damage, and healing rules are out of scope. The test
focuses on four parts of its first-person motion:

- low ground friction keeps speed after release;
- softer acceleration lets the old path carry into a new input direction;
- stronger air input and air control allow useful course changes after a jump;
- sideways travel banks the camera while speed adds a small field-of-view gain.

The camera bank, field-of-view gain, and position shift affect rendering only.
They do not change the view angles sent to the server, the crosshair ray, weapon
traces, player bounds, or network prediction.

## Trial values

| Setting | Trial | Prior value |
|---|---:|---:|
| `g_accel` | `7.5` | `10` |
| `g_airaccel` | `3` | `1` |
| `g_aircontrol` | `1` | `0` |
| `g_friction` | `1.35` | `6` |
| `g_stopspeed` | `1` | `2.5` |
| `g_maxspeed` | `10.5` | `8` |
| `cl_fov` | `96` | `90` |
| `cl_camera_position_response` | `0.08` | `0` |
| `cl_camera_roll` | `5.5` | new; `0` disables it |
| `cl_camera_fov_boost` | `7.5` | new; `0` disables it |

Set `cl_camera_position_response`, `cl_camera_roll`, and
`cl_camera_fov_boost` to `0` to test the movement with a fixed camera. Set the
six `g_*` values to the prior values in the table to compare the old movement
with the new camera.

The speed gain turns off while general zoom or Sniper Rifle ADS is active. The
bank stays active because it rotates around the center aim ray.
