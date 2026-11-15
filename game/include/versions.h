#ifndef VERSIONS_H
#define VERSIONS_H

// ROM offsets that the code passes as plain numbers instead of through a segment symbol,
// so they differ per version.
// They are typed u8* like the linker-generated *_ROM_START symbols they stand in for.
#if defined(VERSION_US_REV2)
#define TRAINER_DATA_ROM_START ((u8*)0x895000)
#else
#define TRAINER_DATA_ROM_START ((u8*)0x898000)
#endif

#endif
