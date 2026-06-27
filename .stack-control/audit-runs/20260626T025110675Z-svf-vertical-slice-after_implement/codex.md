### MIDI callback mutates `std::function` through a cross-thread reference

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   high  
Surface:    adapters/workbench/parameter-view.cpp:24-28, adapters/workbench/parameter-view.cpp:43-46, adapters/workbench/workbench-app.cpp:153-164

`ParameterView` installs GUI callbacks that capture `OnChange& cb = onChange_` by reference, then the workbench also updates controls from the MIDI callback via `MessageManager::callAsync`. `setNormalized()` uses `dontSendNotification`, so the MIDI reflection path is currently quiet, but the captured reference still creates a fragile lifetime/channel boundary: any future control update that does notify, or any queued GUI callback during teardown, can dereference `onChange_` through a lambda that outlives the intended interaction boundary.

The blast radius is high because this is exactly the GUI/MIDI/thread ingress surface for the feature. A consumer extending the workbench controls could introduce a use-after-destroy or call into `node_` after teardown without an obvious compile-time failure. A safer shape is to capture the callback by value in each control lambda, or route all parameter changes through a lifetime-guarded owner callback that is explicitly invalidated before children are destroyed.

### Processed mode leaves output channels above `kMaxChannels` dry

Finding-ID: AUDIT-BARRAGE-codex-02  
Status:     open  
Severity:   medium  
Surface:    adapters/workbench/workbench-app.cpp:100-128

`prepareToPlay()` clamps `preparedChannels_` to `kMaxChannels`, and `getNextAudioBlock()` processes only `jmin(buffer.getNumChannels(), preparedChannels_)`. But `source_.fillBlock(region)` is built with `buffer.getNumChannels()`, so if the device exposes more than 8 output channels, channels 9+ receive source audio and then bypass processing even when the A/B toggle is in processed mode.

The blast radius is medium: most users will run stereo, but on multichannel devices the UI state “Process (A/B)” becomes partially false, and downstream tests or demos could hear inconsistent dry/processed output by channel. Either clear channels beyond the prepared count in processed mode, limit the source region to the prepared channel count, or surface an explicit unsupported-channel error instead of silently producing mixed semantics.
