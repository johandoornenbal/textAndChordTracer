# Third-party provenance

- JUCE 8.0.12: `29396c22c93392d6738e021b83196283d6e4d850`,
  https://github.com/juce-framework/JUCE. JUCE is AGPLv3/commercial, independently
  of this wrapper's MIT license. The VST3 SDK notices bundled with JUCE apply.
- whisper.cpp v1.7.6: `a8d002cfd879315632a579e73f0148d06959de36`,
  https://github.com/ggml-org/whisper.cpp. MIT, including retained ggml notices.
  Fetched at configure time and linked statically. Mac builds embed Metal kernels.
- Whisper model download locations and SHA-1 checksums are pinned from that
  revision's `models/README.md`: tiny `bd577a113a864445d4c299885e0cb97d4ba92b5f`,
  base `465707469ff3a37a2b9b8d8f89f2f99de7299dac`,
  small `55356645c2b361a969dfd0ef2c5a50d530afd8d5`. Weights are separately
  downloaded from ggerganov/whisper.cpp on Hugging Face; OpenAI Whisper is MIT.
  The smoke test uses upstream `samples/jfk.wav`, not a user's recording.
- UI, scaling and installation conventions follow
  https://github.com/johandoornenbal/AirEq and the other Doornenbal Audio wrappers.
  Existing contributor notices are retained in LICENSE. New analysis, lead-sheet,
  document export and transcription-integration code are this project's MIT work.

No Airwindows DSP is used or changed. The tracer is an independent analysis tool,
not an official JUCE, Whisper, Microsoft Word or Google Docs product.
