Simple Denoise
==============

Reduces background noise, such as air conditioners, fans and room noise, in voice recordings and streaming.
VST® 3 and CLAP formats for Windows x64 and Linux x86_64.

Source code and verification:
  https://github.com/AllegroMoltoV/simple-denoise-audio

Parameter
---------

Amount (0 to 100 %, default 100 %): mix of the denoised and the original sound. 0 % leaves the sound unchanged.

The plug-in reports its latency to the host: 1439 samples (about 30 ms) at 48 kHz.

Install
-------

1. Close your DAW.
2. Windows: copy the folder "VST3\Simple Denoise.vst3" to %LOCALAPPDATA%\Programs\Common\VST3\ (only you)
   or C:\Program Files\Common Files\VST3\ (all users, needs administrator).
3. Windows, for CLAP hosts: copy "CLAP\Simple Denoise.clap" to %LOCALAPPDATA%\Programs\Common\CLAP\
   or C:\Program Files\Common Files\CLAP\.
4. Linux: copy the folder "VST3/Simple Denoise.vst3" to ~/.vst3/ and "CLAP/Simple Denoise.clap" to ~/.clap/.
5. Start your DAW and rescan plug-ins if needed.

No installer and no network access. The plug-in does not send any audio or data anywhere.

Licenses
--------

Simple Denoise is released under the MIT License (LICENSE.txt).
Third-party licenses are in THIRD_PARTY_LICENSES: RNNoise, CLAP, clap-wrapper, {fmt} and the VST 3 SDK.

VST is a registered trademark of Steinberg Media Technologies GmbH.
