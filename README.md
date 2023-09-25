## obs-express
This is a custom build of OBS, ffmpeg, and a light-weight executable wrapper. It is only 50mb (22mb compressed) and is capable of recording and encoding a specific region of the screen at an extremely high speed thanks to OBS.

------

### Usage 

The command line help is as follows:

```txt
obs-express v1.0.0, a command line screen recording utility
  bundled with obs-studio v29.1.3
  created for Clowd (https://github.com/clowd/Clowd)

Global:
  --help                  Show this help text

Required:
  --output {filePath}     The file for the generated recording

One of:
  --region {x,y,w,h}      A capture region to spanning multiple monitors
  --monitor {szDevice}    Only capture the specified monitor

Optional:
  --adapter {int}         The index of the graphics device to use
  --speaker {dev_id}      Output device ID to record (can be multiple)
  --microphone {dev_id}   Input device ID to record (can be multiple)
  --fps {int}             The target video framerate (default: 30)
  --crf {int}             Quality from 0-51, lower is better. (default: 24)
  --maxWidth {int}        Downscale output to a maximum width
  --maxHeight {int}       Downscale output to a maximum height
  --tracker               If the mouse click tracker should be rendered
  --trackerColor {r,g,b}  The color of the tracker (default: 255,0,0)
  --lowCpuMode            Maximize performance if using CPU encoding
  --hwAccel               Use hardware encoding if available
  --noCursor              Do not render mouse cursor in recording
  --pause                 Pause before recording until start command
  --preview {hwnd}        Render a recording preview to window handle
```

The parameter `--output` is required, and you must specify either `--region` or `--monitor`. You can retrieve `szDevice` for a monitor using win32 `GetMonitorInfo`.

Both the `--speaker` and `--microphone` parameters can be specified more than once, to record multiple devices. 
They support `default` being passed in as the value to use the default device, or the `{ID}` of the device as returned from `MMDeviceEnumerator`.
Maximum 5 simultaneous audio devices.

### Realtime Commands

While the recorder is running, you can provide the following commands via stdin:

- `q` or `Ctrl+C`: Stop recording and quit.
- `start`: Used in conjunction with the --pause parameter.
- `mute`: Mutes an audio device. Must provide the device type and index (order in which it was provided in command line arguments). 
  Examples:
  - Mute the first speaker device: `mute s 0`
  - Mute the second microphone device: `mute m 1`
- `unmute`: Unmutes an audio device. Same syntax as `mute`.
- `pause`: Pauses the capture/rendering pipeline. Can be resumed with `start`.


### Compiling

Requirements:
 - Visual Studio 17.2.6 or later
 - Desktop development with C++ (Workload)
 - Windows SDK 10.0.20348

The following will perform a full build and output `obs-express.zip` to the project directory.
```cmd
git clone --recursive https://github.com/clowd/obs-express.git
configure-obs.cmd
pack-release.cmd
```

Now open `ObsExpressCpp.sln` in Visual Studio and you should be able to F5 and run/debug the program.
