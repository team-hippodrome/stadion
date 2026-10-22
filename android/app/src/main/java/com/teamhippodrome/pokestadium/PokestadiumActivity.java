package com.teamhippodrome.pokestadium;

import org.libsdl.app.SDLActivity;

// The game is libmain.so (the repository's CMakeLists.txt); SDL itself is libSDL3.so.
public class PokestadiumActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    // main.cpp defines a plain main() (it does not include SDL_main.h, which would rename it to SDL_main)
    @Override
    protected String getMainFunction() {
        return "main";
    }
}
