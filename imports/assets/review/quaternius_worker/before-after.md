# Quaternius Worker processing report

## Before

- Four modular Worker mesh parts
- 2,676 source mesh vertices and 5,240 triangles
- 13 source materials
- Separate Quaternius animation library
- Feet parented outside the lower-leg chain
- Six unused end or helper bones
- No LG Duel weapon socket, collision proxy, or hitbox proxies

## After

- 10,244 encoded GPU vertices and 5,240 triangles
- 13 materials, within the 16-material player review limit
- 73 joints, within the 96-joint limit
- Four skin influences per vertex at most
- 33 retained clips, within the 40-clip limit
- New `Idle_Gun_TwoHanded` clip built from `Idle_Gun_Pointing`
- Fixed foot parents and removed six unweighted helper bones
- Added `weapon_socket`, one collision proxy, and four hitbox proxies
- Added fixed front, side-angle, and two-handed idle previews

## Check result

The headless engine check loaded the GLB, sampled all clips, checked weights and bounds, and sampled 16 instances. It passed. GPU outline quality, weapon fit in the game, client frame time, and final production use remain separate review gates.
