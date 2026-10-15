# orus-sim test video

`bars-720p30.h264`: 10 s of moving colour bars with a grey box, 1280×720 at 30 fps, H.264
Constrained Baseline, one slice per frame, a keyframe every 30 frames, Annex B. Decodable, so the
apps show a real picture against the simulator:

    orus-sim --video tools/orus-sim/testdata/bars-720p30.h264

The desktop CI job streams it to the Windows and Linux apps and saves a screenshot.
