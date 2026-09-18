#pragma once

// Load the config and the game version. Returns false when presence is
// disabled (no numeric client_id configured in discordrpc.conf).
bool presenceInit();

// Drive the Discord connection forever (meant to run on a detached thread).
// Never returns normally.
void runPresence();