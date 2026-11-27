#include "audio_stored_fade.h"
#include "audio_commands.h"
#include "src/rom_device.h"
#include "src/cry.h"
#include "src/audio_cache.h"
#include "libnumus/player.h"
#include "StadionPort.h"

extern s32 D_80078388;

// .data
u8 D_80078A10[3] = { 0xFF, 0xFF, 0xFF };
u32 D_80078A14 = 0;
s32 D_80078A18 = 0;
u8 D_80078A1C[4] = { 0 };
u8 D_80078A20[16] = { 0 };

// Related to Partner Pikachu, probably makes it go Pika Pika
s32 PlayMoveSoundEffect(UNUSED s32 side_index, u32 move_id, u32 species, u32 sound_kind) {
    if (PORT_AUDIO_OFF()) {
        return 0;
    }
    u32 i;
    u32 temp_v0;
    u32 slot;
    s32 romAddr;
    s32 size;
    MoveSoundEntry* var_a0;
    u8 sound_kind_flags;
    fx_t* seq;
    u8* entry;
    u8 is_partner_pikachu;

    is_partner_pikachu = 0;
    D_80078A20[0] = 0;
    Audio_QueueFadeSoundCommand(D_80078A18, 0xA);

    if (species == 0xC8) {
        is_partner_pikachu = 1;
    } else if ((species == 0) || (species >= 0x98U)) {
        return 0;
    }
    if ((species == 0) || (move_id >= 0xA6U)) {
        return 0;
    }

    sound_kind_flags = ((sound_kind & 0xFF00) >> 8) & 0xFF;
    sound_kind &= 0xFF;
    if (((sound_kind == 0xA) || (sound_kind == 0xB)) && ((sound_kind_flags == 0x10) || (sound_kind_flags == 0x20))) {
        return 0;
    }

    for (i = 0; i < 3; i++) {
        if (species == D_80078A10[i]) {
            slot = i;
            goto found;
        }
    }

    slot = D_80078A14 % 3;
    D_80078A14++;
    D_80078A10[slot] = species;
    if (is_partner_pikachu) {
        species = 0x97;
    }
    species--;
    if (species < (D_800FC6FC->num_files - 1)) {
        romAddr = ((u32*)D_800FC6FC)[species + 3];
        size = ((u32*)D_800FC6FC)[species + 4] - romAddr;
    } else {
        romAddr = ((u32*)D_800FC6FC)[species + 3];
        size = (s32)((uintptr_t)D_800FC6E8->seqArray[3].offset - romAddr);
    }
    Rom_DmaRead(romAddr, D_800FC6DC, size);
    Audio_CopyDataWithCacheSync(D_800FC6DC, D_800FC6B0[slot], 0x3E8);
    Audio_RelocateSoundBankTable(D_800FC6B0[slot]);
    if (is_partner_pikachu) {
        species = 0x97;
    }
    size = D_800FC700->seqArray[species].len;
    if (size & 1) {
        size++;
    }
    Rom_DmaRead((uintptr_t)D_800FC700->seqArray[species].offset, D_800FC6DC, size);
    Audio_CopyDataWithCacheSync(D_800FC6DC, D_800FC6C0[slot], 0x44C);

found:
    switch (sound_kind) {
        case 0:
        case 2:
        case 3:
            size = 0;
            break;
        case 4:
            size = 2;
            break;
        case 1:
            size = 4;
            break;
        case 11:
            size = 0;
            break;
        case 16:
            species = move_id;
            goto block_38;
        default:
            return 0;
    }
    if (sound_kind == 0xB) {
        move_id = 0;
    }
    var_a0 = D_800FC6C0[slot]->entries;
    entry = &(var_a0 + move_id)->unk0[size];
    species = entry[0];
    D_80078A1C[0] = entry[1];

block_38:
    seq = D_800FC6B0[slot];
    temp_v0 = seq->num_entries;
    if (!is_partner_pikachu && (species >= temp_v0)) {
        return 0;
    }
    if (is_partner_pikachu) {
        if ((move_id == 0x2D) || (move_id == 0x2E)) {
            D_80078A20[0] = 1;
        }
        if (species != 0) {
            Cry_Play(species, 0xA);
        }
        return 0;
    }
    // The translated releases play cries quieter while the battle music stream plays, and the European releases play
    // them quieter overall.
    temp_v0 = Audio_PlaySoundEffect(D_800FC6AC, seq, species,
                                    (PORT_VERSION_TRANSLATED && (D_80078388 != 0) && (D_80078E78 == 0) &&
                                     (sLoopPointTable[0] == 0))
                                        ? 0x70
                                        : 0x80,
                                    0x80, -1);
    if (PORT_VERSION_PAL) {
        Audio_QueueVoiceVolume(temp_v0, 0x6A);
    }
    D_80078A18 = temp_v0;
    return temp_v0;
}

void Audio_FadeStoredSound(void) {
    if (PORT_AUDIO_OFF()) {
        return;
    }
    if (D_80078A1C[0] != 0) {
        Audio_QueueFadeSoundCommand(D_80078A18, (s32)D_80078A1C[0]);
    }
}
