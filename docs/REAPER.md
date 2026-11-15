# REAPER: separate instrument and vocal analysis

The tracer need not live on the 2-bus. A dedicated analysis track lets it hear a
clear chord instrument and an isolated main vocal without changing the mix.

## Recommended routing

1. Create an ordinary track named **Song analysis**. Set its channel count to
   **4** in its routing window.
2. Insert **Text and Chord Tracer** on this track. Enable its optional stereo
   **Clean vocal** input and map the inputs in the plugin's pin connector:

   | Analysis track channels | Plugin input | Purpose |
   | --- | --- | --- |
   | 1/2 | Music left/right | Chord and tempo analysis |
   | 3/4 | Clean vocal left/right | Lyric transcription |

   The host must actually enable the optional VST3 bus, not just increase the
   track's channel count. Check the plugin's exposed bus/pin configuration.
3. Add a send from a rhythm guitar **or** piano track to Song analysis, with
   source audio **1/2** and destination audio **1/2**, initially at 0 dB.
4. Add a send from the main vocal track to Song analysis, with source audio
   **1/2** and destination audio **3/4**, initially at 0 dB.
5. The plugin clears its output after capturing the inputs, so it adds no audible
   copy even if Master/parent send is accidentally left on. For a second guard,
   **disable Master/parent send on Song analysis. Leave the track unmuted.**
   This also protects against REAPER routing around a hard-bypassed or removed plugin, while letting its FX run.
   Keep the original instrument/vocal tracks routed to the mix as normal.
   There is no send from Song analysis back to those source tracks.
6. Select **Detect** or **Hint + follow**. These modes do not rely on the project
   tempo. Select a speech model, arm Trace playback and play the song.

For recorded audio, a **Pre-FX** send can give the tracer the clean guitar or
vocal before mix reverb/distortion. For a virtual piano, use **Pre-Fader (Post-FX)**
so the instrument has generated audio before the send. Post-fader sends also
work, but mix fader rides then affect the analysis signal. Choose the mode that
provides clear audio and enough level without clipping.

Do not route a mix containing the vocal to both destinations and expect vocal
isolation: the plugin does not perform source separation. In this version, the
optional vocal bus is used whenever the host enables it. An enabled but silent
vocal bus is silence for lyrics; disable that bus if you want to attempt words
from the Music input instead.

## One chord source versus several

Start with the instrument that most clearly states the harmony. Sending guitar
and piano together creates one combined Music input in version 1; they are not
independent chord votes. A clean accompaniment can outperform the full mix,
particularly when bass lines, lead instruments and drums obscure chord tones.
If they play different voicings or substitutions, audition one at a time.

## Tap tempo before the take

Set the meter. Tap **Bar start** on each first count, **Other beat** on the remaining counts, and **Stop** after a series of bars. Capture pauses while tapping and waits for replay. Tap during playback to save the changing bar/beat positions for the lead sheet. Automatic guesses from the first marked bar are rebuilt; manual edits remain. English uses the installed base.en model; select Dutch to use multilingual base.

## Playback and export

Keep the analysis track active while playing. A background transcription pass can
finish after transport stops. Wait until it finishes, then export the `.docx`. Choose Lyrics only for the words, Basic for editable chord/lyric text lines (spaces, no cells), or Detailed
for bar numbers and timing information.
Open it in Word, or upload it to Drive and open with Google Docs. Double-click
bars to fix guessed chords or sung words before export. Follow OFF lets you read
earlier bars without automatic scrolling. In the editor, `|` separates beat
positions in both chords and lyrics; blank fields leave empty beats. New take clears the previous capture and its timing map. Restarting playback at
a previously traced project position returns to its saved bar and beat; Follow
moves the view there. Unvisited positions remain a new provisional section.

This guide describes the intended REAPER pin/send configuration. Native REAPER
routing and sung-lyric accuracy require validation on the user's Mac; synthetic
routing and speech smoke tests do not substitute for that host check.

## Suggested next version

Explicit chord-source and lyric-source selectors, separate input meters and bus
status, and optional independent guitar/piano inputs would make routing easier to
verify. Independent chord candidates could then carry confidence and agreement
information instead of simply summing both instruments. A dedicated analysis
track would remain useful, with its Master/parent send disabled.
