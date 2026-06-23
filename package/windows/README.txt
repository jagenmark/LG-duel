LG DUEL WINDOWS PLAYTEST
========================

1. Keep every file in this folder together.
2. Double-click "Play LG Duel.bat" to join a server.
   To host, double-click "Host LG Duel Server.bat" or pass a UDP port as the
   first command-line argument.
3. If Windows Defender Firewall asks, allow access on private and public
   networks so the game can exchange UDP traffic with the host.
4. Wait for the other players.
5. Connected players press F3 to ready up.

The launcher selects the SDL_GPU renderer, which prefers Vulkan and falls
back automatically when Vulkan is unavailable.

The server address is stored in server-address.txt. The player launcher uses
the host and port from that file; the host launcher uses the port. Packages
built by GitHub Actions receive this file from the workflow's server settings,
which default to the VM_HOST repository secret and UDP port 27960. Only edit
that file when the host gives you a new address.

See PLAYTEST_GUIDE.html for controls and troubleshooting.
