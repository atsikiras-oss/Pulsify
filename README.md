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

Song info sidebar
  - Right-hand panel for the current song: cover, title, artist, album, duration, and what plays next.
    YouTube songs also show channel, views, upload date and the actual stream (codec, container, bitrate,
    sample rate) with "Open on YouTube" / "Copy link". Local files show tags (genre, year, track), format,
    bitrate, size and "Show in folder".
  - Toggle with the (i) button next to the volume, Ctrl+Shift+I, or the x in the panel. The choice is remembered.
    It appears while something is playing and the window is at least 1120 px wide.

Stream reliability (fix for the stream dropping / demuxing failing mid-song)   [relay.h]
  - YouTube streams no longer go straight into the media backend. They play through a small relay on
    127.0.0.1 that fetches the audio in 512 KB range requests (like yt-dlp does) with the headers yt-dlp
    asked for, and silently re-requests any chunk that fails, from the exact byte where it stopped.
  - If the stream URL expires mid-song the relay asks yt-dlp for a fresh one and carries on; playback
    doesn't stop. Seeking works the same way (the relay answers Range requests).
  - If the player still errors, ends the song far earlier than its real length, or stalls for 20 s,
    Pulsify reconnects and RESUMES AT THE SAME POSITION (it used to restart at 0:00, and skip the song
    on the second failure). Up to 4 reconnects in a row; playing steadily for 20 s earns the budget back.
  - yt-dlp now prefers plain https m4a/audio formats over HLS / DASH-fragment ones.
  - If the relay can't be used, the stream URL is played directly as before.
