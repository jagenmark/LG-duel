# Glide movement experiment

This branch tests the movement and camera feel in the linked 26-second clip:

<https://x.com/atomicwastlandd/status/2094509370990104591>

The clip's weapon, health, damage, and healing rules are out of scope. The test
focuses on its first-person motion:

- crouching above 55% of ground speed starts a low slide;
- a slide keeps most of its speed and can lead straight into a jump;
- a slide jump starts at no less than 116% of normal ground speed;
- air input can bend that carried path without changing its speed;
- landing while crouched starts the next slide, so the loop can be chained;
- the camera and weapon follow those movement states with small, smooth shifts.

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
| `cl_fov` | `90` | `90` |
| `cl_camera_position_response` | `0.7` | `0` |
| `cl_camera_roll` | `2.5` | new; `0` disables it |
| `cl_camera_fov_boost` | `3.5` | new; `0` disables it |

Set `cl_camera_position_response`, `cl_camera_roll`, and
`cl_camera_fov_boost` to `0` to test the movement with a fixed camera. Set the
six `g_*` values to the prior values in the table to compare the old movement
with the new camera.

The field-of-view gain turns off while general zoom or Sniper Rifle ADS is
active. The bank stays active because it rotates around the center aim ray.

For a visual check, use `r_show_weapons 1`. Record a full, uncropped game
window at 60 fps, then perform a slide, slide jump, air turn, crouched landing,
and second slide jump without a cut.
