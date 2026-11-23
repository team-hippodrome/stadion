#!/bin/sh
# The audio sweep: every song and every FX bank entry the game's play functions select, with the sound engine on,
# on the Options screen (options.rec: A on the title screen, then state 0x13 through the game's loop; it plays
# nothing on its own). usage: runs.sh <out dir>   (from the repository root)
O=$1; D=evidence/whole_game/routes/audio_seq; B=build-evidence-all2/stadion; R=$D/options.rec
mkdir -p "$O"
one() { name=$1 frames=$2; shift 2
  env PORT_GAME_AUDIO=1 "$@" nice -n 15 python3 -I tools/evidence/run.py "$O/$name" --bin $B --frames "$frames" --rec $R \
    --natural --set "State_TitleScreen#1@exit:gCurrentGameState:4=19" --call-file $D/$name.calls > "$O/$name.log" 2>&1
  rm -f "$O/$name/seq.txt" "$O/$name/calls.txt" "$O/$name/port.flash"; }
one music1 81500 &
one music2 81500 &
one music3 81500 &
one music4 81500 &
one system 25200 PORT_TRACE_ACTIVITY=1 &
one moves1 62400 &
one moves2 63500 &
one species 26500 &
wait
