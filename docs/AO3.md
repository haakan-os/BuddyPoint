# AO3 on BuddyPoint

Open **Apps → AO3**. The menus, keyboard, button hints, touch controls, fonts and colors use BuddyPoint's selected theme, including Retro Mac, NeXTSTEP and Windows 95.

The main **Library → AO3** shelf shows downloaded fics with AvesO3-style four-part status badges, chapter and word counts, fandoms, tags and summary previews. It refreshes the library index when first opened and supports search across fic titles, authors, fandoms and tags. Select a fic to read it. Other library shelves also show the badge beside managed AO3 downloads. **Apps → AO3** provides bookmark browsing, downloads, updates and full fic details using the same badges.

| Badge quadrant | Meaning |
| --- | --- |
| Top left | AO3 rating: G, T, M, E, or `-` for not rated/unavailable |
| Top right | `-` unread, R reading, F finished, triangle waiting for chapters; a dot on the triangle means new chapters downloaded since last opened |
| Bottom left | `!` archive warnings apply, `?` warnings undisclosed/unavailable, `-` no archive warnings apply; undisclosed warnings have a shaded background |
| Bottom right | Checkmark for a complete work, X for a work in progress, `?` when chapter metadata is unavailable |

Reading status comes from local EPUB progress. Reaching the end of a complete fic shows finished; reaching the end of an unfinished fic shows waiting for chapters. Opening a fic clears its new-chapters dot. These are local reading states, not changes to your AO3 account. The fic details include warning text and a badge legend. Older downloads without warning metadata show `?` until fresh metadata is downloaded, never an assumed “no warnings” state.

This feature is in the firmware source; the previously packaged web-flasher firmware does not include it until a new build is packaged and published.

## Find and read a fic

1. Choose **AO3 work link or ID** and enter an `https://archiveofourown.org/works/…` link or its numeric ID. Chapter links also work. Alternatively, use **Search** with AO3's work-search syntax.
2. Open a result to view its author, chapter count, word count, rating, fandom and summary.
3. Choose **Download**. BuddyPoint saves AO3's EPUB to `/AO3/<work-id>.epub` on the SD card.
4. Open **Downloaded fics**, select the fic and choose **Read fic**. Downloads also appear in the normal file browser and remain readable offline.

Wi-Fi selection opens when needed. The app turns off Wi-Fi on exit if it started the connection. **Back** or the on-screen **Cancel** button stops a transfer between network reads; a connection timeout can take up to 20 seconds.

## Private bookmarks and account-only works

Choose **Sign in to AO3** and enter your username and password on the reader. Then **My AO3 bookmarks** lists your own bookmarks, including private bookmarks, and downloads use the same signed-in session for account-only works. Public works and search do not require sign-in.

**Download bookmarked fics** walks your bookmark pages and downloads individual AO3 works. Existing downloads are checked for updates and unchanged works are skipped. Series bookmarks and external works are skipped; bookmark notes, collections and AO3's Marked for Later list are not imported. A summary reports saved, unchanged and failed works.

If the session expires, bookmark sync stops and asks you to sign in again. It does not quietly continue with only public bookmarks. A rate-limit response also stops sync; try again later. Browser challenges and CAPTCHA cannot be completed on the reader: check the account in a browser and retry later. The app does not bypass AO3 challenges or access restrictions.

The password is used for sign-in and is not written to the SD card. The remembered session is stored in `/.crosspoint/ao3-session.json`, using BuddyPoint's existing device-derived obfuscation. **Obfuscation is not encryption**: treat the SD card and backups as containing account access. **Forget AO3 sign-in** removes the local session, including pending and backup copies; it does not revoke sessions on AO3 or erase downloaded books.

## Download updates

Use **Update downloaded fics** to check every fic downloaded through this app, or **Check and download updates** on a saved fic. Checks compare chapter count, completion, word count, update date and the EPUB download URL. Same-day edits are detected when these values change. Edits that change none of this metadata cannot be detected.

Updates replace the EPUB at the same path, retaining the reader's progress file and separate local reading bookmarks. Derived book, cover and page caches are rebuilt. Authors can reorder or rewrite chapters, so an old reading position may no longer refer to exactly the same passage. AO3 bookmarks themselves are never changed.

Transfers use a temporary file and check the ZIP container before replacing a saved EPUB. Network interruption, cancellation, invalid downloads and failed replacement leave the previous EPUB recoverable. Completed downloads are retained if a later work fails. A metadata-save failure can leave a valid new EPUB available in `/AO3` but missing from the app's tracked list; retry the download after resolving SD errors.

Tracking metadata lives in `/.crosspoint/ao3/<work-id>.json`. Files copied onto the SD card independently, including FanFicFare EPUBs, are readable normally but are not automatically enrolled in this update list. Removing an AO3 bookmark does not delete its downloaded fic. Sync runs only when selected, not in the background.

Keep tracked fics at `/AO3/<work-id>.epub`. Moving them elsewhere, including with the reader's optional “move finished books to Read” setting, removes them from this managed shelf and update list.

## Connection and size limits

- Only `https://archiveofourown.org` is contacted for AO3 requests. TLS certificates are verified, and redirects cannot send credentials to another host. No third-party AO3 mirror is used.
- The reader needs a valid clock for certificate checks. It attempts network time synchronization if its clock is unset.
- Requests are spaced by at least two seconds; there is no automatic retry loop for AO3 rate limits.
- Lists hold at most 20 works per page. HTML responses are capped at 2 MiB, downloads at 64 MiB, and one bookmark-sync run at 500 pages. Oversized or unexpected pages produce an error instead of silently dropping works.
- Cloud validation requires `archiveofourown.org` in the environment's allowed domains. Saving network settings may also require publishing/applying them. This cloud policy is separate from the reader's Wi-Fi access.

## Validation

Host tests cover streamed AO3 HTML, warnings/tags, distinct badge states, reading-status transitions, CSRF and cookie rotation, private-bookmark session expiry, authenticated work and EPUB requests, trusted redirects, rate limits, cancellation, incomplete downloads, real ZIP validation, atomic replacement, cache preservation, SD failures and library pagination. HTTP, hardware credentials and SD storage are simulated in the client tests.

```sh
cmake -S crosspoint-upstream/test -B build/host-tests
cmake --build build/host-tests --target Ao3Test Ao3ClientTest -j4
ctest --test-dir build/host-tests -R '^Ao3' --output-on-failure
pio run -d crosspoint-upstream -e default
```

Before publishing firmware, test a real AO3 account and physical reader: sign in, retrieve a private bookmark and account-only fic, download across multiple bookmark pages, interrupt a download, update a partially read fic, reopen it, and check the selected theme in all four orientations. Observe free heap during TLS and repeated syncs. Cloud compilation and mocked tests do not verify AO3's current production HTML, anti-bot behavior, physical flashing or on-device TLS memory usage.

## Development and attribution

The integration was informed by AvesO3's AO3 metadata and native EPUB update approach, reviewed at [`de5dc31eae377fec6ea8ec48072d9aca95cbe0a1`](https://github.com/SiliconAves/AvesO3/tree/de5dc31eae377fec6ea8ec48072d9aca95cbe0a1). AvesO3 is Copyright © 2025 Dave Allie, licensed under MIT; its [license is retained here](licenses/AvesO3-LICENSE.txt).

BuddyPoint implements the streamed parser, verified HTTPS/session handling, private-bookmark downloads and themed app separately. This is not a wholesale import of AvesO3's library tabs, Marked for Later support, read-status tagging, proxy fallback or EPUB metadata scanner.

Implementation lives in `crosspoint-upstream/lib/Ao3`, `src/network/Ao3Client.*`, `src/activities/apps/Ao3Activity.*`, `src/activities/library/LibraryListActivity.*` and `src/components/Ao3Presentation.h`. The parser and request buffer are allocated once per request on the heap to avoid the ESP32's small task stack. Work lists are bounded and reserved before population, and no page-sized HTML DOM or EPUB download buffer is held in RAM. Each badge is a 288-byte monochrome mask drawn in the list's themed foreground, including selected rows. The app holds one page of masks; the main library holds only a reusable visible window. UI labels remain owned and stable during rendering. New translation keys have English fallback in other locales.
