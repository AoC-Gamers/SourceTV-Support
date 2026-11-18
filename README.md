# SourceTV-Support

Introduction
------
SourceMod extension that fixes broken broadcasting/demo recording features in Left 4 Dead 2.

More information about SourceTV on wiki: https://developer.valvesoftware.com/wiki/SourceTV

Requirements
------
- [SourceMod](https://www.sourcemod.net/) y Metamod:Source 1.12 para la matriz de build AoC. El README heredado declaraba SourceMod 1.8+; la carga con versiones anteriores a 1.12 queda pendiente de validar.

Supported Games
------
- [Left 4 Dead 2](https://store.steampowered.com/app/550/Left_4_Dead_2/)

Installation and building
------
Use the [installation guide](docs/installation/README.md) and the
[build guide](docs/build/README.md). CI ZIPs are available from the
[AoC Actions runs](https://github.com/AoC-Gamers/SourceTV-Support/actions).

Also Recommended
------
[[L4D/2] Unlink Camera Entities](https://github.com/shqke/sp_public/tree/master/disable_cameras) - fixes multiple potential visual bugs on round restart, such as missing HUD and viewmodel for spectators after "finale vehicle escape" sequence team swap.

[[L4D/2] Automated Demo Recording](https://github.com/shqke/sp_public/tree/master/autorecorder) - automates demo recording process based on player count.

[Manage HLTV Cameras](https://github.com/shqke/sp_public/tree/master/hltv_cameras) - adds missing fixed cameras for HLTV Director.

Command Line Parameters
------
- `+tv_enable 1` - Make SourceTV available on first map start (don't require to `changelevel`/`map`)
- `-hltv` - Enable broadcasting feature (clients can connect to a port specified in `tv_port` to spectate the game)
- `+tv_port <port>` - Set the SourceTV host port (default 27020). Must be set as srcds start parameter
- `-tvmasteronly` - SourceTV can only serve one client and can't be used as relay proxy

Fixed Issues
------
- Property **CHLTVServer::stringTableCRC** was never set
- Insufficient buffer size to store string tables and/or data tables
- Hibernation was causing a consistent memory leak
- Cvar `tv_enable` and some others were not available from command line
- HLTV clients were forced to interact with lobby system
- (L4D2) **ISteamGameServer** interface was failing to initiate due to bound HLTV port being passed as query port
- Server would crash on HLTV client disconnect (auth ticket method)
- (L4D2) Event `player_full_connect` was sent for HLTV clients userids
- (L4D2) **ISteamGameServer** interface would log off whenever **CHLTVServer** was shutting down
- SourceTV bot was never moved into Spectator team
- SourceTV bot doesn't take a human slot and doesn't count as a potential voter
- "CUtlRBTree overflow" on level transition
- Broken demos on early recording (tv_autorecord)
- Disabled `tv_transmitall` would leave bots PVS data empty
- (feature) Allowed addons for HLTV clients and in demo playback

Known Issues
------
- Missing arms models/blinking world model attachments during SourceTV footage playback
  - Needed modifications on game client
- Relay feature is broken
