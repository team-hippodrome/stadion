// Matched by what each line says (speech recognition of both banks): the Japanese bank's extra lines are 16-17 (two
// remarks on a low-level Pokémon), 54-55 (the Nintendo Cups '97 and '98's openings), 60-61 (their rules) and 206-207
// (two more knockout remarks).
static const ClipRun kJpClipRuns[] = {
    { 1, 1, 15 },    // status remarks
    { 16, 18, 36 },  // first moves ... the Poké Cup's openings (Japanese: Nintendo Cup '99's)
    { 52, 56, 4 },   // Free Battle, and the Prime, Petit and Pika Cups' rules (Ultra, Fancy, Yellow)
    { 56, 62, 1 },   // the Poké Cup's rules (Nintendo Cup '99's)
    { 57, 63, 39 },  // Gym Leader Castle ... battle starts
    { 96, 102, 104 }, // hits, misses, status, knockouts
    { 200, 208, 458 }, // battle ends, the 151 species, effects, the 165 moves, commentary
};
