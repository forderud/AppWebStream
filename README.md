# AppWebStream
[Windows Media Foundation](https://msdn.microsoft.com/en-us/library/ms694197.aspx) and [FFMPEG](http://ffmpeg.org/) sample code for streaming an application window to a web browser. The video is encoded as a H.264 stream inside a fragmented MPEG4 container that is [Media Source Extensions (MSE)](https://www.w3.org/TR/media-source/) compatible, so that it can be received by modern web browsers with minimal client-side buffering.

### Getting started
![screenshot](screenshot.png)
* Open project in Visual Studio
* Build project
* Start `WebAppStream.exe port [window handle]`. You can use Spy++ (included with Visual Studio) to determine window handles.
* Open `http://localhost:port` in web browser, or on another computer. The stream can also be opened as `http://localhost:port/movie.mp4` in VLC media player.

To build with FFMPG, you first need to download & unzip FFMPEG binaries to a folder pointed to by the `FFMPEG_ROOT` environment variable. Then, set the `ENABLE_FFMPEG` preprocessor define before building.

Outstanding tasks:
* Authentication is currently missing.
* The handcrafted HTTP communication should be replaced by a "proper" HTTP library.

### Media Foundation details
* 0-1 frame latency (1st frame in, no output, 2nd frame in, 1st frame out, 3rd frame in, 2nd frame out, 4th frame in, 3rd frame out, 4th frame out, ...)
* The MPEG4 container is manually modified as suggested in https://stackoverflow.com/questions/49429954/mfcreatefmpeg4mediasink-does-not-generate-mse-compatible-mp4 to make it Media Source Extensions (MSE) compatible.

The FFMPEG-based encoder is not affected by this issue.

### Frame grabbing method
The project is currently using `BitBlt` to copy the content of the specified window handle to an offscreen `HBITMAP` object. It would probably be preferable to instead use the newer [Desktop Duplication API](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api) for frame grabbing.

### Browser support
* Confirmed to work with Google Chrome, Microsoft Edge and Firefox.
* Safary doesn't work properly yet. The movie is streamed, but not show on screen. Don't understand why.
