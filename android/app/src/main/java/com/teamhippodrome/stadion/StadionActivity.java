package com.teamhippodrome.stadion;

import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import org.libsdl.app.SDLActivity;

// The game is libmain.so (the repository's CMakeLists.txt); SDL itself is libSDL3.so.
public class StadionActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    // main.cpp defines a plain main() (it does not include SDL_main.h, which would rename it to SDL_main)
    @Override
    protected String getMainFunction() {
        return "main";
    }

    // The file name of a content:// link from the file picker, or null. Called from launcher.cpp (ImportPicked): links
    // such as the Downloads folder's end in a number (".../document/msf:1000025301"), not the file's name.
    public String displayName(String uri) {
        try (Cursor c = getContentResolver().query(Uri.parse(uri), new String[] { OpenableColumns.DISPLAY_NAME },
                                                   null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) {
                return c.getString(0);
            }
        } catch (Exception e) {
            // fall back to the link's last part
        }
        return null;
    }
}
