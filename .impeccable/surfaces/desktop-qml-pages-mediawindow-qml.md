---
version: 1
slug: "desktop-qml-pages-mediawindow-qml"
primary_target: "desktop/qml/Pages/MediaWindow.qml"
related_targets: ["apple/ImagoPayloadControl/UI/MediaView.swift"]
---

# Media (payload photos and videos)

Scope: one new screen inside the established Imago world: desktop (Qt/QML, own window) first,
iPad second, iPhone last; Mac through Catalyst. Mode: Operate.

Audience and job: the payload operator after (sometimes during) a flight, pulling the Orus L's
photos and videos off its storage over the datalink: see what was captured, find a flight,
download it or a selection, delete what is not needed. Content range: 0 to ~2000 files, EO
JPEGs (MB each), IR JPEGs, radiometric TIFFs, MP4 videos (hundreds of MB); a few Mbit/s shared
with the live video. Thumbnails for photos only (EXIF thumbnail, a few KB each); videos are
tiles without a picture.

States: no web server answer, payload still starting (404), login required (unsupported),
empty storage, loading list, thumbnails arriving, downloading (progress, speed, cancel),
download failed, deleting, deleted, already on this device, live video running (warning, pause
choice), no capture times (single roll).

## Direction contract

THESIS: the payload's storage read as flight rolls: captures fall into sorties by their time
gaps, and a sortie is the unit you open, judge and pull. Refuses the flat file-manager list and
the phone-gallery camera roll.

OWN-WORLD: Imago near-black ground (#05060D) with raised #14151C panes, magenta-to-purple only
for selection, the active roll and progress; Space Grotesk tabular figures for every time, size
and count; Manrope for names. Green check (#34D399) only for "on this device"; red only for
delete and errors.

STORY: the operator opens Media, sees the latest flight already selected with its sheet of
thumbnails, recognises the shots, selects or takes the whole roll, chooses whether the live
video pauses, watches the transfer finish, deletes from the payload with one confirmation.

FIRST VIEWPORT: header: "Media", storage gauge (used / total), Photos | Videos | All filter,
Refresh. Left rail (about a quarter): flights newest first, each row = date + start-end time,
counts, size, a capture timeline strip and "Download flight". Right: the selected roll's
thumbnail sheet, 16:9 cells with capture time in figures, IR / TIFF / video badges, on-device
check. Bottom of the sheet: the selection bar that becomes the transfer row (file n of N,
percent, MB/s, Cancel). Signature move: each flight row's capture timeline strip, photos as
ticks and videos as spans across the sortie's duration, in magenta when selected; scrubbing
the strip scrolls the sheet to that moment.

FORM: dealt candidate 7 of 7 ("flight rolls"), seed key 925e389e. Phone: rail becomes a list
that pushes the sheet. iPad: rail as split view sidebar.

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance
