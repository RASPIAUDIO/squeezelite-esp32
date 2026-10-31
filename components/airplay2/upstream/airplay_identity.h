#pragma once

// Do not impersonate a HomePod: some controllers infer unsupported MRP remote
// control from the AudioAccessory model prefix and fail before audio starts.
// Keep mDNS and both XML/binary GET /info responses consistent.
#define AIRPLAY_MODEL "MnCastS31,1"
