# BuddyPoint OTA updates

The firmware checks the latest stable release of `haakan-os/BuddyPoint`:

```text
https://api.github.com/repos/haakan-os/BuddyPoint/releases/latest
```

It never falls back to the upstream CrossPoint release feed. Drafts and prereleases are not offered by this stable update channel.

## One-time migration

Install the current build from the [BuddyPoint web flasher](https://haakan-os.github.io/BuddyPoint/) once. Older firmware still contains the upstream URL and cannot discover this change through its own updater.

After installation, connect the reader to Wi-Fi and use its firmware-update option in Settings. On BuddyPoint 1.6.5, the initial 1.6.5 release should report no newer update; a subsequent version will be offered once published. A full physical-device OTA transfer still needs testing.

## Release format

Stable tags may be `1.6.6` or `v1.6.6`. The application asset must be named:

```text
buddypoint-1.6.6-x3-x4.bin
```

The optional `v` is omitted from the filename. Upload the application binary, never a merged factory image. The release workflow builds the `gh_release` environment, whose embedded version matches `[crosspoint] version` in `crosspoint-upstream/platformio.ini`.

A stable release is offered only when its numeric version is higher, or when replacing a development/RC build of the same version. Malformed version tags are rejected. Existing chip, board, and image validation remains enabled when installing.

## Publish a future update

1. Make and test the firmware changes. Increase `[crosspoint] version` in `crosspoint-upstream/platformio.ini`, for example to `1.6.6`, and push to `main`.
2. In GitHub Actions, select **Publish OTA release**, choose **Run workflow** on `main`, and enter `v1.6.6`.
3. The workflow checks the tag against the configured version, runs release tests, builds the X3/X4 application, checks the firmware package, and publishes the release with its application binary and SHA-256 checksum. It refuses an existing release or tag.

The workflow publishes only after the asset is ready. Simply pushing a commit does not publish an OTA update.

To refresh the bundled web-flasher firmware locally as well:

```sh
cd crosspoint-upstream
pio run -e gh_release
cd ..
python3 scripts/package_web_firmware.py --environment gh_release
npm --prefix web-flasher test
```

Commit the updated `web-flasher/firmware/` files with the matching firmware source. GitHub Pages redeploys automatically after that commit reaches `main`.
