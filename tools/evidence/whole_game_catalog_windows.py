"""Cartridge-backed whole-game recipes, reconstructed from the checked-in route documentation.

All generated saves live under the caller's fixture directory. The original cartridge is read only.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
ROUTES = ROOT / 'evidence/whole_game/routes'


def prepare(directory, cartridge):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    rom = directory / 'yellow_de.gb'
    if Path(cartridge).resolve() != rom.resolve():
        shutil.copyfile(cartridge, rom)
    source_hash = hashlib.sha256(rom.read_bytes()).hexdigest()
    stamp = directory / 'prepared.json'
    if stamp.exists() and json.loads(stamp.read_text()).get('rom_sha256') == source_hash and json.loads(stamp.read_text()).get('version') == 3:
        return
    env = dict(os.environ, PYTHONPATH=str(ROOT / 'tools/save_editor'))
    def run(script, *args):
        subprocess.run([sys.executable, '-X', 'utf8', '-I', str(ROOT / script), *map(str, args)],
                       cwd=ROOT, env=env, check=True, stdout=subprocess.DEVNULL)
    def edit(*args):
        subprocess.run([sys.executable, '-X', 'utf8', '-m', 'stadium_save', *map(str, args)],
                       cwd=ROOT, env=env, check=True, stdout=subprocess.DEVNULL)
    run('tools/gb_save.py', directory / 'base.sav', '--got-pokedex', '--rom', rom)
    run('tools/evidence/gb_variants.py', directory / 'base.sav', directory / 'gbv')
    parties = {
        'op6': ';'.join(s + ':55,moves=94/85/58/53,dv=0xFFFF' for s in ['MEWTWO', 'MEW', 'DRAGONITE', 'ALAKAZAM', 'GENGAR', 'ZAPDOS']),
        'opR': ';'.join(s + ':55,moves=94/85/58/53,dv=0xFFFF' for s in ['ZAPDOS', 'GENGAR', 'MEW', 'MEWTWO', 'DRAGONITE', 'ALAKAZAM']),
        'op100b': ';'.join(s + ':100,moves=63/89/85/58,dv=0xFFFF' for s in ['MEWTWO', 'MEW', 'DRAGONITE', 'ALAKAZAM', 'GENGAR', 'ZAPDOS']),
        'L55': ';'.join(s + ':55' for s in ['NIDOKING', 'LAPRAS', 'SNORLAX', 'TAUROS', 'STARMIE', 'ALAKAZAM']),
        'evo1': 'MACHOKE:30;PIKACHU:25;GRAVELER:30',
        'evo2': 'KADABRA:30,ot=GARY,otid=54321;HAUNTER:30,nick=GEIST,ot=GARY,otid=54321;GRAVELER:30;MACHOKE:30,nick=MUSKEL',
        'moltres': 'MOLTRES:60;ZAPDOS:60;ARTICUNO:60;BLASTOISE:70;MEW:100;MAGIKARP:5',
        'six': 'PIKACHU:25;VENUSAUR:50;CHARIZARD:60;BLASTOISE:70;MEW:100;MAGIKARP:5',
        'pika3': 'PIKACHU:25,moves=84/45/86;BULBASAUR:20;CHARMANDER:18',
        'pika4': 'PIKACHU:25,moves=84/45/86/98;BULBASAUR:20;CHARMANDER:18',
        'spike': ';'.join(s + ':50,moves=131/131/131/131' for s in ['CLOYSTER', 'OMASTAR'] * 3),
        'event6': ';'.join(s + ':50' for s in ['PIKACHU', 'VENUSAUR', 'CHARIZARD', 'BLASTOISE', 'SNORLAX', 'LAPRAS']),
    }
    for name, party in parties.items():
        run('tools/evidence/gb_party.py', directory / 'base.sav', directory / (name + '.sav'), party)
    default = ROOT / 'replays/default.flash'
    team = ['PIKACHU:25', 'VENUSAUR:50', 'CHARIZARD:60', 'BLASTOISE:70', 'MEW:100', 'MAGIKARP:5']
    shutil.copyfile(default, directory / 'teams.flash')
    edit('team', directory / 'teams.flash', 'set', 'gym_leader_castle', 0, *team, '--name', 'MICHI', '--ot', 'MICHI')
    edit('unlock', default, '--mewtwo', '--round2', '-o', directory / 'unlocked.flash')
    shutil.copyfile(ROOT / 'tools/save_editor/verify/saves/continue.flash', directory / 'continue.flash')
    shutil.copyfile(default, directory / 'labfull.flash')
    for box in range(1, 4):
        edit('box', directory / 'labfull.flash', 'clear', box)
        edit('box', directory / 'labfull.flash', 'add', box, *team, '--ot', 'MICHI')
    edit('items', directory / 'labfull.flash', 'clear', 1)
    edit('items', directory / 'labfull.flash', 'add', 1, 'TM01:2', 'TM24', 'TM13', '198', 'PP UP:5', 'HP UP:3',
         'PROTEIN:3', 'IRON:3', 'CARBOS:3', 'CALCIUM:3', '--name', 'MICHI', '--trainer-id', 12345)
    shutil.copyfile(directory / 'labfull.flash', directory / 'full4.flash')
    for box in range(2, 5):
        edit('items', directory / 'full4.flash', 'add', box, 'POTION:3', 'ANTIDOTE:2', '--name', 'ASH' + str(box),
             '--trainer-id', 10000 + box)
    edit('items', directory / 'full4.flash', 'clear', 1)
    edit('items', directory / 'full4.flash', 'add', 1, 'MASTER BALL:1', '--name', 'GARY', '--trainer-id', 22222)
    edit('items', directory / 'labfull.flash', 'clear', 1, '-o', directory / 'dup.flash')
    edit('items', directory / 'dup.flash', 'add', 1, 'HP UP:3', 'HP UP:4', 'TM01:2', '--name', 'MICHI', '--trainer-id', 12345)
    edit('set', directory / 'labfull.flash', 'options.bonus_pokemon=1', '-o', directory / 'prize.flash')
    for name, count, category, base in [('CUP10', 10, 'poke_cup', ROOT / 'replays/cup_win_badge.flash'),
                                         ('CUP10X6', 10, 'poke_cup', ROOT / 'replays/cup_win_badge.flash'),
                                         ('CASTLE9', 9, 'gym_leader_castle', directory / 'teams.flash'),
                                         ('CASTLE10', 10, 'gym_leader_castle', directory / 'teams.flash')]:
        dest = directory / (name + '.flash')
        shutil.copyfile(base, dest)
        mons = ['SNORLAX:50', 'PIKACHU:50', 'LAPRAS:50'] + (['TAUROS:50', 'STARMIE:50', 'ALAKAZAM:50'] if name == 'CUP10X6' else [])
        for slot in range(count):
            edit('team', dest, 'set', category, slot, *mons)
    stamp.write_text(json.dumps({'version': 3, 'rom_sha256': source_hash, 'files': {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in directory.rglob('*') if p.is_file() and p != stamp}}, indent=2))


def recording_frames(path):
    text = Path(path).read_text(encoding='utf-8')
    ends = re.findall(r'^# end (\d+)', text, re.M)
    if ends:
        return int(ends[-1])
    frames = [int(line.split()[0]) for line in text.splitlines() if line and not line.startswith('#')]
    return max(frames) + 600


def recipes(fixtures, output=None, backend='opengl'):
    fixtures = Path(fixtures)
    output = Path(output) if output else fixtures.parent / 'whole-game'
    cases = []
    def add(group, stem, *, frames=None, save=None, flash=None, state=None, expect=(), extra=(), natural=True, env=None):
        rec = ROUTES / group / (stem + '.rec')
        options = ['--natural'] if natural else []
        if save:
            source = fixtures / (save if '/' in save or save.endswith('.sav') else save + '.sav')
            options += ['--gb', str(fixtures / 'yellow_de.gb') + '|' + str(source)]
        if flash:
            options += ['--flash', str(fixtures / (flash + '.flash'))]
        if state is not None:
            options += ['--set', f'State_TitleScreen#1@exit:gCurrentGameState:4={state}']
        options += list(extra)
        cases.append(dict(name=(group or 'whole') + '_' + stem, rec=str(rec), frames=frames or recording_frames(rec),
                          source='evidence/whole_game/routes/' + (group or ''),
                          extra=options, expect=list(expect), expect_edges=[], min_calls={}))
        if env:
            cases[-1]['env'] = env
        if group == 'lab_list':
            cases[-1]['expect_prefixes'] = ['LabPCList_']
        elif group in ('lab_pc', 'lab_pc2'):
            cases[-1]['expect_prefixes'] = ['LabItem_' if stem.startswith('items') else 'LabPC_']
        elif group == 'registration':
            cases[-1]['expect_prefixes'] = ['TeamSelection_']
    # These recordings carry their own end frame and start in the Lab via real input.
    for group in ('lab_pc', 'lab_list'):
        for rec in sorted((ROUTES / group).glob('*.rec')):
            save = 'gbv/items.sav' if group == 'lab_pc' and rec.stem.startswith('items') else 'gbv/boxes_full.sav'
            add(group, rec.stem, save=save, flash='labfull')
    for rec in sorted((ROUTES / 'registration').glob('*.rec')):
        add('registration', rec.stem, save='gbv/party6.sav', flash='teams', state=39)
    for stem, save, flash in [('items_gp_new_box', 'gbv/items.sav', 'full4'), ('items_gp_merge', 'gbv/items.sav', 'dup'),
                              ('boxes_ck_moves_save', 'gbv/boxes_full.sav', 'labfull'), ('boxes_sides', 'gbv/boxes_full.sav', 'labfull'),
                              ('poke_reorder_release', 'gbv/boxes_full.sav', 'labfull'), ('prize_pokemon', 'gbv/items.sav', 'prize')]:
        add('lab_pc2', stem, save=save, flash=flash)
        cases[-1]['expect'] = list({
            'items_gp_new_box': ('LabItem_RunBoxScreenModal', 'LabItem_WriteDeckBack'),
            'items_gp_merge': ('LabItem_CombineStackQuantities', 'LabItem_WriteDeckBack'),
            'boxes_ck_moves_save': ('LabPC_SaveModifiedBoxes', 'LabPC_WriteBoxEntries', 'LabPC_MarkBoxSeen'),
            'boxes_sides': ('LabPC_BoxPairSelector_SwapSides', 'LabPC_BoxMenu_ClearButtonHighlights'),
            'poke_reorder_release': ('LabPC_SwapMonsInBox', 'LabPC_RemoveMonFromBox'),
            'prize_pokemon': ('LabPC_PrizePokemonThread', 'LabPC_FindDirtyDeck'),
        }[stem])
    for stem, frames, save, save2, expect in [
        ('trade_evolution', 16600, 'evo1.sav', 'evo2.sav', ('Trade_ApplyEvolution',)),
        ('trade_box_pages', 14600, 'gbv/boxes_mixed.sav', 'evo2.sav', ('Trade_UpdatePickScreenPageSwipe',)),
        ('trade_cancel', 9600, 'evo1.sav', 'evo2.sav', ('Trade_UpdateBoxMachineFlowConfirmDone',)),
        ('trade_cart_error', 9000, 'evo1.sav', 'gbv/not_at_pc.sav', ('Trade_UpdateBoxMachineFlowFrameReturn',))]:
        add('menus_trade', stem, frames=frames, save=save, expect=expect,
            extra=('--gb2', str(fixtures / 'yellow_de.gb') + '|' + str(fixtures / save2)))
    add('menus_trade', 'pokedex_area', frames=9800, save='gbv/dex_full.sav', expect=('Pokedex_ResetCameraEase',))
    add('menus_trade', 'labpc_check_move_page', frames=9000, save='gbv/boxes_full.sav', flash='labfull', expect=('LabPC_ShowMoveDetailPage',))
    for stem, frames, flash, save in [
        ('castle_rules', 6500, 'teams', 'op6'), ('mewtwo_rules', 5500, 'unlocked', 'op6'),
        ('castle_gym_clear', 64000, 'teams', 'op6'), ('castle_round_save_quit', 20000, 'teams', 'op6'),
        ('castle_round_overwrite_save', 20000, 'continue', 'op6'), ('castle_round_continue_no_save', 26000, 'continue', 'op6'),
        ('castle_loss_reenter', 90000, 'teams', 'gbv/party6.sav'), ('castle_loss_restart_gym', 90000, 'teams', 'gbv/party6.sav'),
        ('castle_loss_quit', 90000, 'teams', 'gbv/party6.sav'), ('castle_e4_rival_clear', 150000, 'unlocked', 'opR'),
        ('mewtwo_win', 30000, 'unlocked', 'op100b'), ('mewtwo_loss_battle_again', 24000, 'unlocked', 'op6'),
        ('mewtwo_loss_quit', 24000, 'unlocked', 'op6'), ('battle_now', 22000, 'teams', 'gbv/party6.sav')]:
        add('castle_mewtwo', stem, frames=frames, save=save, flash=flash,
            state=40 if stem == 'battle_now' else 34 if stem.startswith('mewtwo') else 39)
        cases[-1]['expect'] = list({
            'castle_gym_clear': ('BattlePrep_ShowCastleClearOutro',),
            'castle_e4_rival_clear': ('GymLeaderCastleClear_InitState',),
            'mewtwo_win': ('State_RunCredits', 'Credits_OutroLoop'),
            'mewtwo_loss_battle_again': ('BattlePrep_DrawQuitPrompt',),
            'mewtwo_loss_quit': ('BattlePrep_DrawQuitPrompt',),
            'battle_now': ('BattlePrep_ConfirmSimpleRules',),
        }.get(stem, ()))
    # Exercise the Champion's banner before the long Elite Four/rival route reaches it.
    add('castle_mewtwo', 'castle_round_save_quit', frames=9000, save='op6', flash='teams',
        expect=('BattlePrep_LoadBannerAssets', 'BattlePrep_BindCarouselSegment1'),
        extra=('--set', 'State_TitleScreen#1@exit:gCurrentGameState:4=39;BattlePrep_LoadBannerAssets:gSessionContext+0x2:1=9'))
    cases[-1]['name'] = 'champion_banner_probe_round'
    for stem, frames, flash, save, state, expect in [
        ('cup_level_cap', 7600, None, 'L55', None, ('Rental_HandleLevelCapWarning',)),
        ('cup_register_full', 7400, 'CUP10', None, None, ('Rental_HandleTeamCancel',)),
        ('cup_back_to_registered', 5600, 'CUP10X6', None, None, ('Rental_TeamTrayHide',)),
        ('castle_register_grid', 9200, 'teams', 'gbv/party6.sav', 39, ()),
        ('castle_delete_team', 6200, 'CASTLE9', 'gbv/party6.sav', 39, ('RegisteredTeam_DeleteAndCompact',))]:
        extra = ('--flash', str(ROOT / 'replays/cup_win_badge.flash')) if stem == 'cup_level_cap' else ()
        add('rental_grid', stem, frames=frames, save=save, flash=flash, state=state, expect=expect, extra=extra)
    for stem, frames in [('hm_squirtle', 9000), ('lab_level_group', 9000), ('tm24_save', 11000)]:
        add('engine_left', stem, frames=frames, save='gbv/boxes_full.sav', flash='labfull')
        cases[-1]['expect'] = list({'hm_squirtle': ('LabPCList_ClearMovesFrom',),
                                    'lab_level_group': ('LabPCList_FilterByLevelRange',)}.get(stem, ()))
    add('engine_left', 'order_detail_cap', frames=11300, save='six', extra=('--set', 'BattleMenu_InitOrderSelectPanels@exit:gSessionContext+0x1:1=3'),
        expect=('BattleMenu_DrawOrderSelectPanel_MonDetail',))
    add('error_paths', 'continue_delete', frames=3800, save='gbv/party6.sav', flash='continue')
    for stem in ['lab_boxes_mash', 'lab_dex_mash', 'lab_items_mash', 'lab_pokemon_mash', 'lab_seelist_mash', 'lab_trade_mash']:
        extra = ('--gb2', str(fixtures / 'yellow_de.gb') + '|' + str(fixtures / 'gbv/party6.sav')) if 'trade' in stem else ()
        add('', stem, save='gbv/boxes_full.sav', flash='labfull', extra=extra, env={'PORT_CONTROLLERS': '1,2'})
    for stem in ['event_battle', 'event_battle_settings']:
        # The settings recording uses the disabled Start option to enter its
        # settings branch; the battle-entry recording needs an eligible team.
        save = 'event6.sav' if stem == 'event_battle' else 'gbv/party6.sav'
        add('', stem, save=save, state=18, env={'PORT_CONTROLLERS': '1,2'},
            frames=9000 if stem == 'event_battle' else None,
            expect=('EventBattle_ExitToBattle', 'State_BattleFromEvent') if stem == 'event_battle' else ('EventBattle_RunSettingsPanel',),
            extra=('--gb2', str(fixtures / 'yellow_de.gb') + '|' + str(fixtures / save)))
    for count in (3, 4):
        add('', f'pikachu_reward_{count}moves', save='pika' + str(count), natural=False,
            extra=('--call', 'BattlePrep_RunRulesScreen#2@exit:Fragment_LoadAndCall:i:i34,i3891504,i3900720,i0,i0'))
    for stem in ['cup_loss_from_start', 'cup_win_tail']:
        add('', stem, extra=('--flash', str(ROOT / 'replays/cup_win_badge.flash')))
    add('', 'mash')
    for stem in ['free_battle_com_hard', 'free_battle_party_menu', 'free_battle_six_check']:
        add('battle_gates', stem, save='six', natural=stem != 'free_battle_com_hard',
            frames={'free_battle_com_hard': 30000, 'free_battle_party_menu': 13300, 'free_battle_six_check': 14500}[stem])
    add('particles', 'free_battle_team_1p2p', save='spike', natural=False, frames=45000, env={'PORT_CONTROLLERS': '1,2'})
    add('audio_seq', 'tour_mg_dynamo_audio', env={'PORT_GAME_AUDIO': '1'})
    title = 'State_TitleScreen#1@exit'
    calls = ';'.join(title + ':Audio_DispatchSoundBankStopCommand:v:i' + str(bank << 16) + ',i0,i0' for bank in range(2, 12))
    add('debug_crash', 'title_audio', frames=2100, env={'PORT_GAME_AUDIO': '1'},
        expect=('Audio_DispatchSoundBankStopCommand', 'Audio_SeedRandom'),
        extra=('--call', calls + ';' + title + ':Audio_SeedRandom:v:i12345,i6789'))
    add('debug_crash', 'rank2', frames=2600, env={'PORT_CONTROLLERS': '1,2'},
        extra=('--call', title + ':Minigame_RankingDebugTest:i:i0,i0;' + ';'.join('miniUnkDrawHUB:' + f + ':v:' for f in
               ['showDebuggJoystickInfo', 'minigameDebuggVectorEditor', 'miniApplyCameraDebugOffset', 'GalleryCamera_DebugAdjustZoomOffset'])))
    for stem in ['gal_camera', 'gal_album', 'gal_album_boot']:
        add('minigames_gallery', stem, save='base', state=None if stem.endswith('_boot') else 17,
            env={'PORT_CONTROLLERS': '1,2'},
            extra=('--gb2', str(fixtures / 'yellow_de.gb') + '|' + str(fixtures / 'gbv/boxes_full.sav')))
        # Category 12 uses a shared FX bank. Its normal album caller owns that
        # bank; injecting it at title exit lets the next menu replace a live stream.
        cases[-1]['expect'] = (['Gallery_SwapPhotoRecords', 'Gallery_SnapUpdatePasteConfirm', 'Gallery_AlbumConfirmAndExit', 'Audio_PlayCategory12SoundCommand']
                              if stem == 'gal_album' else ['Gallery_LoadSurfingPikachuIcon', 'Gallery_AlbumMain']
                              if stem == 'gal_album_boot' else ['Gallery_LoadSurfingPikachuIcon', 'Gallery_SwitchBackgroundSet'])
        if stem == 'gal_album':
            cases[-1]['expect_edges'] = [('Audio_PlayCategory12SoundCommand', 'Audio_PlaySoundEffect')]
    add('minigames_gallery', 'gal_busy', frames=3400, save='base', state=17, env={'PORT_CONTROLLERS': '1,2'},
        extra=('--gb2', str(fixtures / 'yellow_de.gb') + '|' + str(fixtures / 'gbv/bad_checksum.sav')))
    add('minigames_gallery', 'gal_busy_boot', frames=3800, save='gbv/bad_checksum.sav')
    cases[-1]['expect'] = ['Gallery_RentalViewerUpdateBusyConfirm']
    album = output / backend / 'minigames_gallery_gal_album/port.flash'
    for stem in ['gal_print', 'gal_ss_print', 'gal_ss_bonus', 'gal_bonus_boot', 'seals']:
        photos = output / backend / (stem + '_unlocked.flash')
        unlocked = stem in ('gal_print', 'gal_ss_bonus', 'gal_bonus_boot')
        extra = ['--flash', str(photos if unlocked else album)]
        if stem.startswith('gal_ss'):
            extra += ['--set', 'State_TitleScreen#1@exit:gCurrentGameState:4=17;Gallery_SnapUpdateMenuIconStates:D_8330343C:4=1']
        add('minigames_gallery', stem, frames=5000 if stem == 'seals' else 6000 if not stem.endswith('_boot') else None,
            state=64 if stem == 'seals' else None if stem.endswith('_boot') or stem.startswith('gal_ss') else 17, extra=extra)
        cases[-1]['depends_on'] = ['minigames_gallery_gal_album']
        if stem == 'seals':
            cases[-1]['expect'] = ['Snap_PrintClubPrintSeals']
        elif stem.startswith('gal_ss'):
            cases[-1]['expect'] = ['Gallery_SnapUpdateSaveConfirm']
        if unlocked:
            cases[-1]['derived_flash'] = dict(source=str(album), target=str(photos), unlock=True)
    return cases


def ordered(cases):
    """Short cases first, while respecting album/save dependencies."""
    remaining = {c['name']: c for c in cases}
    done = set()
    result = []
    while remaining:
        ready = [c for c in remaining.values() if set(c.get('depends_on', [])) <= done]
        if not ready:
            raise ValueError('missing or cyclic whole-game dependencies: ' + ', '.join(remaining))
        case = min(ready, key=lambda c: (c['name'].startswith('audio_'), c['frames'], c['name']))
        result.append(case)
        done.add(case['name'])
        del remaining[case['name']]
    return result


def materialize(case):
    derived = case.get('derived_flash')
    if not derived:
        return
    source = Path(derived['source'])
    target = Path(derived['target'])
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    stamp = target.with_suffix('.source-sha256')
    if target.exists() and stamp.exists() and stamp.read_text() == digest:
        return
    env = dict(os.environ, PYTHONPATH=str(ROOT / 'tools/save_editor'))
    subprocess.run([sys.executable, '-X', 'utf8', '-m', 'stadium_save', 'unlock', str(source), '--mewtwo', '--round2',
                    '-o', str(target)], cwd=ROOT, env=env, stdout=subprocess.DEVNULL, check=True)
    stamp.write_text(digest)
