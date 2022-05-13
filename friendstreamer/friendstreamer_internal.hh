#pragma once

bool demuxer_good();
void block_for_demuxer();
void notify_initial_seek();
void wakeup_playloop();
void set_is_client();
void set_is_host();
void set_player_state_pause(bool paused);
void set_player_state_ts(double ts);
