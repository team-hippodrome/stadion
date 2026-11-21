#!/usr/bin/env python3 -I
"""Exercise the actual fainted-attacker scoring routine under AddressSanitizer.

Usage: tools/test_battle_ai_fainted.py --build <native-Ninja-build-directory>
Uses the game's real headers/defines from that build and stubs unrelated scoring helpers.
--expect-failure checks that an unfixed checkout reproduces the out-of-bounds access.
"""
from pathlib import Path
import re,subprocess,tempfile,shlex,sys
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', required=True, type=Path)
parser.add_argument('--compiler', default='clang')
parser.add_argument('--expect-failure', action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
base=root
source=root/'game/src/fragments/battle_engine/battle_engine_com_decision_logic.c'
text=source.read_text()
a=text.index('u8 BattleAI_ScoreAttackVsSwitch(')
b=text.index('\nu16 BattleAI_ApplyStatStageModifier',a)
function=text[a:b]
header='''#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "fragments/battle_engine/battle_engine.h"
AIDifficultyConfig profile;
AIDifficultyConfig* D_843C5564 = &profile;
u8 BattleAI_HasExactlyOneUsableCandidate(BattleAiTeamState* a) { return 0; }
u8 BattleAI_SelectBestSwitchCandidateIndex(BattleAiTeamState* a, BattleAiTeamState* b) { return 0; }
s32 BattleAI_ScoreAttackOptionDoubles(BattleAiMonState* a, BattleAiMonState* b, BattleAiTeamState* c, u8* out, unk_func_8438220C* scores) { *out = 0; return 0; }
s32 BattleAI_ScoreAttackOption(BattleAiMonState* a, BattleAiMonState* b, BattleAiTeamState* c, u8* out, s32* d, s32* e, s32* f, s32* g, s32* h) { *out = 0; return 0; }
u8 BattleAI_ScoreCategorySetLowPP(BattleAiMonState* a, BattleAiMonState* b) { return 0; }
s32 BattleAI_ScaleSignedRatio(s32 a, s32 b) { return 0; }
u8 BattleAI_ClassifyMoveAvailability(BattleAiMonState* a, BattleAiMonState* b, u8 c) { return 0; }
s32 BattleAI_ScoreSwitchUrgencyFromThreatMove(BattleAiMonState* a, BattleAiMonState* b, u8 c) { return 0; }
s32 BattleAI_SelectRandomUsableCandidate(BattleAiTeamState* a) { return 1; }
'''
main='''
int main(int argc, char** argv) {
    BattleAiMonState attacker = {0}, defender = {0};
    BattleAiTeamState team = {0}, opponent = {0};
    unk_func_8438220C* scores = calloc(6, sizeof(*scores));
    u8 choice = 0;
    profile.unk_00 = 0x4000C0;
    defender.unk_12.displayHp = 100;
    attacker.unk_12.maxHp = 100;
    team.unk_06 = 2;
    if (argc > 1) {
        attacker.unk_12.displayHp = 100;
        team.unk_14[0].unk_12.displayHp = 100;
        for (int index = 0; index < 4; index++) {
            scores[0].unk_B4 = index;
            for (int damage = 250; damage <= 251; damage++) {
                scores[0].unk_04[index].unk_0C = damage;
                printf("%d %d %u\\n", index, damage, BattleAI_ScoreAttackVsSwitch(&attacker, &defender, &team, &opponent, &choice, scores));
            }
        }
    } else {
        scores[0].unk_B4 = 167;
        assert(BattleAI_ScoreAttackVsSwitch(&attacker, &defender, &team, &opponent, &choice, scores) == 0xFF);
        for (int index = 0; index < 256; index++) {
            scores[0].unk_B4 = index;
            assert(BattleAI_ScoreAttackVsSwitch(&attacker, &defender, &team, &opponent, &choice, scores) == 0xFF);
        }
        puts("Fainted attacker: selected-move bytes 0..255 safely return forced-switch score");
    }
    free(scores);
    return 0;
}
'''
# Reuse the real game's include paths and compatibility defines, but compile only the actual failing function.
ninja=(args.build/'build.ninja').read_text();key='game/src/fragments/battle_engine/battle_engine_com_decision_logic.c.o:';i=ninja.index(key);block=ninja[i:ninja.index('\n\n',i)]
defs=shlex.split(re.search(r'  DEFINES = (.*)',block).group(1));incs=shlex.split(re.search(r'  INCLUDES = (.*)',block).group(1))
with tempfile.TemporaryDirectory(prefix='pokemon-ai-') as tmp:
 p=Path(tmp);fixture=p/'probe.c';fixture.write_text(header+'\nstatic void require_true(int value) { if (!value) abort(); }\n'+function+main.replace('assert(', 'require_true('))
 command=[args.compiler,'-g','-O1','-fsanitize=address','-funsigned-char','-Wno-incompatible-pointer-types','-Wno-int-conversion','-w',*defs,*incs,'-include',str(base/'src/port/sdk/port_compat.h'),str(fixture),'-o',str(p/'probe')]
 result=subprocess.run(command,capture_output=True,text=True)
 if result.returncode:print(result.stderr);raise SystemExit(result.returncode)
 live=subprocess.run([str(p/'probe'),'live'],capture_output=True,text=True);assert live.returncode==0,live.stderr
 print('Living attacker baseline:',live.stdout.strip().replace('\n','; '))
 dead=subprocess.run([str(p/'probe')],capture_output=True,text=True)
 print('Fainted attacker exit:',dead.returncode)
 print((dead.stdout+dead.stderr)[:3000])
 if args.expect_failure:
  assert dead.returncode!=0 and 'AddressSanitizer' in dead.stderr
 else:
  assert dead.returncode==0,dead.stderr
 expected=''.join(f'{i} {damage} {255 if damage==250 else 94}\n' for i in range(4) for damage in (250,251))
 assert live.stdout==expected,live.stdout
