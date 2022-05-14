#pragma once

#include <utility>
#include <cstdint>

typedef struct player_state playerstate;

bool demuxer_good();
void block_for_demuxer();
void notify_initial_seek();
void wakeup_playloop();
void set_is_client();
void set_is_host();
void set_player_state_pause(bool paused);
void set_player_state_ts(double ts);
bool pop_host_state(player_state* ps_out);
std::pair<double, uint64_t> get_last_fill_point();
