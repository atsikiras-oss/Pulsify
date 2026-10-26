cmake_minimum_required(VERSION 3.21)
project(Pulsify VERSION 1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Multimedia Network)

# Optional: bake your own Google OAuth client ("Desktop app" type) into the build so
# "Log in with Google" works without typing anything into Settings:
#   cmake -DPULSE_GOOGLE_CLIENT_ID=xxxx.apps.googleusercontent.com -DPULSE_GOOGLE_CLIENT_SECRET=xxxx ..
set(PULSE_GOOGLE_CLIENT_ID "" CACHE STRING "Default Google OAuth client ID")
set(PULSE_GOOGLE_CLIENT_SECRET "" CACHE STRING "Default Google OAuth client secret")

# Optional: bake your Discord Application ID into the build (otherwise paste it in Settings -> Discord):
#   cmake -DPULSE_DISCORD_CLIENT_ID=123456789012345678 ..
set(PULSE_DISCORD_CLIENT_ID "" CACHE STRING "Default Discord Application ID for Rich Presence")

add_executable(Pulsify WIN32 MACOSX_BUNDLE
    main.cpp
    # Core UI and theme
    theme.h
    themedialog.h
    widgets.h
    ui.h
    # Data model and online backend
    library.h
    online.h
    playlists.h
    # Google / YouTube account
    google.h
    # Import dialogs (classic YouTube import + multi-source CSV/text import)
    importdialog.h
    dialogs.h
    # Home page (Deezer charts, image cache, TopBar, HomePage widget)
    home.h
    # Lyrics (Genius + LRCLIB synced display)
    lyrics.h
    # YouTube stream relay (chunked Range requests, handles URL expiry)
    relay.h
    # Now Playing info sidebar
    infopanel.h
    # Discord Rich Presence (title / artist / progress / cover)
    discord.h
)

target_link_libraries(Pulsify PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network)
target_compile_options(Pulsify PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
# MinGW: main.cpp with all headers inlined exceeds the default PE section limit
target_compile_options(Pulsify PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wa,-mbig-obj>)

if(PULSE_DISCORD_CLIENT_ID)
    target_compile_definitions(Pulsify PRIVATE "PULSE_DISCORD_CLIENT_ID=\"${PULSE_DISCORD_CLIENT_ID}\"")
endif()
if(PULSE_GOOGLE_CLIENT_ID)
    target_compile_definitions(Pulsify PRIVATE "PULSE_GOOGLE_CLIENT_ID=\"${PULSE_GOOGLE_CLIENT_ID}\"")
endif()
if(PULSE_GOOGLE_CLIENT_SECRET)
    target_compile_definitions(Pulsify PRIVATE "PULSE_GOOGLE_CLIENT_SECRET=\"${PULSE_GOOGLE_CLIENT_SECRET}\"")
endif()
