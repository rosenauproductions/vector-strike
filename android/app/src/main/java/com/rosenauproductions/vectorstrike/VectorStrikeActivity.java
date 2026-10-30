package com.rosenauproductions.vectorstrike;

import org.libsdl.app.SDLActivity;

// The whole game runs in native code (libmain.so); SDL's activity hosts it.
public class VectorStrikeActivity extends SDLActivity {
    @Override protected String[] getLibraries() { return new String[] { "c++_shared", "SDL2", "main" }; }
}
