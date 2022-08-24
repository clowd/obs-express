## obs-express
This is a custom build of OBS, ffmpeg, and a light-weight executable wrapper. It is only 50mb (22mb compressed) and is capable of recording and encoding a specific region of the screen at an extremely high speed thanks to OBS.

------

### Usage 

The command line help is as follows:

```txt
  --help             Show this help
  --adapter          The numerical index of the graphics device to use
  --captureRegion    The region of the desktop to record (eg. 'x,y,w,h') 
  --speakers         Output device ID to record (can be multiple)
  --microphones      Input device ID to record (can be multiple)
  --fps              The target video framerate
  --crf              The contant rate factor (0-51, lower is better) 
  --maxOutputWidth   Downscale to a maximum output width
  --maxOutputHeight  Downscale to a maximum output height
  --trackerEnabled   If the mouse click tracker should be rendered
  --trackerColor     The color of the tracker (eg. 'r,g,b')
  --lowCpuMode       Maximize performance if using CPU encoding
  --hwAccel          Use hardware encoding if available
  --noCursor         Will not render cursor in recording
  --pause            Wait for key-press before recording
  --output           The file name of the generated recording
```

The only required parameters are `captureRegion` and `output`. 

Both the `speakers` and `microphone` parameters can be specified more than once, to record multiple devices. They support `default` being passed in as the value to use the default device, or the `{ID}` of the device as returned from `MMDeviceEnumerator`.

Type 'q' and Enter, or 'Ctrl+C' to stop an in-progress recording.

### Compiling

Requirements:
 - Visual Studio 17.2.6 or later
 - Desktop development with C++ (Workload)
 - Windows SDK 10.0.20348

To start, you need to checkout the repository recursively (to download all submodules). Then you can run the `configure-obs.cmd` script to build OBS. 
```cmd
git clone --recursive https://github.com/clowd/obs-express-cpp.git
configure-obs.cmd
```

Now open `ObsExpressCpp.sln` in Visual Studio and you should be able to F5 and run/debug the program.
