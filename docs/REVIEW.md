# Local review before publication

User decision: use a real public upstream fork, keep S31 modifications and binaries
local, review everything before publishing the port. The public fork currently
contains upstream code only. No S31 branch, tag, binary or release is to be pushed
until publication is explicitly approved.

## Read first

1. `README.md`: feature status, default board and GPIO/physical-pin table.
2. `docs/HARDWARE.md`: onboard ES8311 vs external Audio+ profile and drawings.
3. `docs/STATUS.md`: what was actually tested and what remains experimental.
4. `docs/BUILD.md` and `docs/INSTALL.md`: pinned SDK, build and flash procedures.
5. `UPSTREAM.md` and `THIRD_PARTY_NOTICES.md`: provenance and component licensing,
   especially the non-commercial AirPlay 2 component.
6. `firmware/experimental/manifest.json`: image hashes and source-build provenance.

The changes include the IDF migration inherited from emlynmac as well as the S31
work. The comparison against upstream is therefore substantial; it is not a small
S31-only patch. Individual vendor snapshots are identified in the provenance files.

Local review commands:

```powershell
git diff master-v4.3...codex/s31-port --stat
git diff master-v4.3...codex/s31-port -- README.md main components scripts
```

`push.default=nothing` is set in this local clone to avoid an accidental plain
`git push`. This does not publish or modify any remote branch.
No device flash is part of this packaging/build check.
