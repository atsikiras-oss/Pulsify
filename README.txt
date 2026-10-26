Pulsify - Home (YouTube) + offline library + playlists

Build:   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build   (produces Pulsify / Pulsify.exe)
Needs:   Qt 6.4+ (Widgets, Multimedia, Network), CMake 3.21+

Home (first tab, Spotify-style)
  - Search pill at the top (search-as-you-type, or press Enter); Home button returns to the feed.
  - Quick tiles, "Your playlists", "Recently played", "More like <artist>" and discover shelves.
  - Shelves come from YouTube searches via yt-dlp and are cached for 8 hours. Edit kDiscover[] at the
    top of main.cpp to change them. "Refresh" reloads them.
  - Search shows a Top result card, a Songs list and a "More results" shelf.
  - First time: click "Install automatically" in the banner (downloads yt-dlp), or "I have it...".

Your Library: your local files (unchanged).

Playlists (sidebar)
  - "+ Create" (or Ctrl+N), rename / delete from the right-click menu or the "..." button on the page.
  - Add songs: right-click any song -> Add to playlist, the "+" next to the title in the player bar,
    or drag a song row onto a playlist in the sidebar. Works for local files and YouTube songs.
  - Playlists + recently played are stored in pulse-data.json in the app-data folder.

Importing playlists from YouTube / YouTube Music   (sidebar "Import" button, or Ctrl+I)
  From link (no account needed)
    - Paste any public or unlisted playlist link, youtube.com or music.youtube.com. Needs yt-dlp.
    - Importing the same link again only adds songs that are new; your own edits are kept.
  Google account (private playlists + your Liked videos)
    Google only allows this through an OAuth client you create yourself (free, one time):
      1. console.cloud.google.com -> create a project.
      2. Enable "YouTube Data API v3" for it.
      3. OAuth consent screen: User type External, add your own Google account as a Test user.
      4. Credentials -> Create OAuth client ID -> application type "Desktop app".
      5. In Pulsify: Import -> Google setup -> paste the client ID and secret (or load the JSON file).
    Then "Sign in with Google" (opens your browser), tick playlists, "Import".
    - Only the read-only scope youtube.readonly is requested. Pulsify never sees your password.
    - Client ID/secret and the refresh token are stored in Pulsify's settings (plain text, per user).
      "Sign out" revokes the token and deletes them.
    - While the Google project is in "Testing" mode, Google ends the sign-in after 7 days.
      Switch the consent screen to "In production" to avoid that (shows an "unverified app" warning,
      which is normal for your own project).
    - YouTube Music: playlists you made in YouTube Music show up here like any other playlist.
      The API does not expose YouTube Music's own "Liked Music", uploads or podcasts; "Liked videos" is
      the closest. For anything else, use "From link".
    - Reading uses ~1 quota unit per 50 songs (free quota is 10,000 units/day), so it is effectively free.

Playlists now behave like playlists
  - Playing a song from a playlist, search, Home shelf or the library queues the whole list; it carries on
    by itself, honours Repeat (off / all / one) and stops at the end when repeat is off.
  - Shuffle plays every song once before repeating (no random repeats), keeps the current song, and
    can be switched on or off mid-song without losing your place. Next/Previous follow what was played.
  - Right-click a song: "Play next" and "Add to queue" (plays before the list continues).
  - Edit a playlist while it plays (add, remove, move up/down) and the running queue follows.
  - Deleted / private / blocked videos and missing files are skipped instead of stopping playback.
  - The next YouTube song's stream is looked up while the current one plays, so the gap is short.

Search also finds your playlists
  - A "Playlists" row appears in search results for any of your playlists whose name, or a song inside it, matches
    what you typed. Works even before yt-dlp is installed.

Account icon
  - Top right of Home: your YouTube / Google profile picture when signed in (a person icon when signed out).
    Click it for Import playlists / Sign out.

Themes (Themes button top right, or Ctrl+T)
  - 14 built-in themes: dark, AMOLED, light, and full gradient themes (Aurora, Sunset, Ocean, Rose, Neon Nights,
    Forest, Ember, Peach Fizz, Mint Cream). Text switches between light and dark automatically.
  - Custom: your own accent, background, 3-stop gradient with angle, panel glass opacity and album-art tint.
  - Custom background image (copied into the app's data folder) with blur, dim and panel opacity.
  - Everything applies live; Cancel restores the previous look.

Lyrics + Now Playing sidebar (player bar, right side)
  - Lyrics icon (Ctrl+L): opens the full-screen player with the cover on the left and the lyrics on the right;
    press it again to go back to the big cover. Click a synced line to jump to it, scroll to look around.
  - Info icon (Ctrl+Shift+I): the Now Playing sidebar slides in from the right (and out again with the X or the icon).
  - "No lyrics found" is now shown when nothing turns up, instead of "Loading lyrics..." forever.

Discord Rich Presence (Settings -> Discord)
  - Shows "Listening to Pulsify" on your Discord profile with the song title, artist, a live progress bar
    and the album cover. Seeking, pausing, resuming and skipping update it right away; a 5 s check
    corrects any drift (for example after a stream buffers). Paused shows "Paused" with no progress bar.
  - One-time setup: discord.com/developers/applications -> New Application, name it "Pulsify" (that name
    is what Discord shows after "Listening to"), copy the Application ID, paste it in Settings -> Discord.
    Or bake it in:  cmake -DPULSE_DISCORD_CLIENT_ID=<your id> ...
  - The Discord desktop app must be running, and Discord Settings -> Activity Privacy -> "Share my
    activity" must be on. Pulsify reconnects by itself if Discord is started later or restarted.
  - Cover: found online by artist + title (Deezer, then iTunes), so it also works for local files.
    YouTube songs fall back to their video thumbnail. If nothing is found, an optional asset named
    "pulsify" (upload it under Rich Presence -> Art Assets in the Dev Portal) is used.
