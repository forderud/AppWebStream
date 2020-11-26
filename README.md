# AppWebStream
[Windows Media Foundation](https://msdn.microsoft.com/en-us/library/ms694197.aspx) and [FFMPEG](http://ffmpeg.org/) sample code for streaming an application window to a web browser. The video is encoded as a H.264 stream inside a fragmented MPEG4 container that is Media Source Extensions (MSE) compatible, so that it can be received by modern web browsers with minimal client-side buffering.

The streaming is primarily tested on and works best with Google Chrome.

### Getting started
![screenshot](screenshot.png)
* Open project in VS2015
* Build project
* Start `WebAppStream.exe port [window handle]`. You can use Spy++ (included with Visual Studio) to determine window handles.
* Open `localhost:[port]` in web browser, or on another computer.

To build with FFMPG, you first need to download & unzip FFMPEG binaries to a folder pointed to by the `FFMPEG_ROOT` environment variable. Then, set the `ENABLE_FFMPEG` preprocessor define before building.

### Media Foundation issues
* The MPEG4 container needs to be manually modified as described in https://stackoverflow.com/questions/49429954/mfcreatefmpeg4mediasink-does-not-generate-mse-compatible-mp4 to reduce latency and make it Media Source Extensions (MSE) compatible.

The FFMPEG-based encoder is not affected by any of the issues above.

### Browser support
* Confirmed to work with Google Chrome, Microsoft Edge and Firefox.
* Safary doesn't work properly yet. The movie is streamed, but not show on screen.
