# Installation

## Requirements

- VCV Rack 2 for macOS.
- An Apple Silicon Mac for the ARM64 package or an Intel Mac for the x86_64
  package.
- The matching Rack SDK under `../LIBS`:

  ```text
  ../LIBS/Rack-SDK-ARM64
  ../LIBS/Rack-SDK-X64
  ```

Linux and Windows packages are not currently provided by this POC.

## Install a prebuilt package

Build artifacts are placed under `target/`:

```text
target/arm64/virtual-network-bus-2.0.0-mac-arm64.vcvplugin
target/x86_64/virtual-network-bus-2.0.0-mac-x64.vcvplugin
```

### Apple Silicon

```sh
mkdir -p "$HOME/Library/Application Support/Rack2/plugins-mac-arm64"
cp target/arm64/virtual-network-bus-2.0.0-mac-arm64.vcvplugin \
  "$HOME/Library/Application Support/Rack2/plugins-mac-arm64/"
```

### Intel x86_64

```sh
mkdir -p "$HOME/Library/Application Support/Rack2/plugins-mac-x64"
cp target/x86_64/virtual-network-bus-2.0.0-mac-x64.vcvplugin \
  "$HOME/Library/Application Support/Rack2/plugins-mac-x64/"
```

Start Rack after installation. Rack extracts the `.vcvplugin` package and
loads it during startup:

```sh
cd ../Rack
make runr
```

Search the module browser for:

- `CV Publisher`
- `CV Subscriber`

Right-click a module body to select the topic and view diagnostics.

After building the ARM64 package into `target/arm64/`, install it with:

```sh
make install-arm64
```

This target only copies the existing package to Rack's ARM64 user plugin
directory; it does not compile the plugin.

After building the x86_64 package into `target/x86_64/`, install it with:

```sh
make install-x86_64
```

This target only copies the existing package to Rack's x86_64 user plugin
directory; it does not compile the plugin.

To detect the current machine architecture and install the matching package,
use:

```sh
make install
```

The target supports `arm64` and `x86_64` hosts, copies the existing package
for the detected architecture, and rejects unsupported architectures. It does
not compile the plugin. The corresponding package must already exist under
`target/`.

## Select the network interface

The publisher and subscriber share one network-interface setting within a Rack
process. Right-click either module and use **Network interface** to choose:

- **Automatic**, which selects the active multicast-capable interface;
- an explicit interface such as `Wi-Fi (en0)` or `Ethernet (en1)`.

The menu displays the resolved interface and IPv4 address. Discovery and CV
transport use the same selected interface. If the selected interface is
unavailable, the module context menu reports the error rather than silently
switching to another interface.

## Build both packages

Run these commands from the plugin repository:

```sh
rm -rf target
mkdir -p target/arm64 target/x86_64

make RACK_DIR=../LIBS/Rack-SDK-ARM64 clean
make -j6 RACK_DIR=../LIBS/Rack-SDK-ARM64 dist
cp dist/*.vcvplugin target/arm64/

make RACK_DIR=../LIBS/Rack-SDK-X64 \
  CROSS_COMPILE=x86_64-apple-darwin clean
make -j6 RACK_DIR=../LIBS/Rack-SDK-X64 \
  CROSS_COMPILE=x86_64-apple-darwin dist
cp dist/*.vcvplugin target/x86_64/
```

The `clean` steps are required because Rack's build files use a shared
`build/` directory. Do not build both architectures into the same object
directory concurrently.

## Verify a package

To inspect the architecture inside a package:

```sh
mkdir -p /tmp/vcvb-package-check
zstd -d -c target/arm64/*.vcvplugin \
  | tar -x -C /tmp/vcvb-package-check
file /tmp/vcvb-package-check/virtual-network-bus/plugin.dylib
```

The expected output is `arm64` for the ARM package or `x86_64` for the Intel
package.

Check the host architecture with:

```sh
uname -m
```

Expected values are `arm64` for Apple Silicon and `x86_64` for Intel Macs.
32-bit `i386` Macs are not supported.

## Troubleshooting

### The plugin does not appear

Confirm that the package architecture matches the Rack installation:

```sh
uname -m
```

Restart Rack after copying a new package. Inspect the Rack log if necessary:

```text
~/Library/Application Support/Rack2/log.txt
```

The log should contain:

```text
Loaded plugin virtual-network-bus 2.0.0
```

### Rack rejects the plugin manifest

The plugin version must begin with the Rack major version. This project uses
`2.0.0` for Rack 2 compatibility. The manifest also declares both module
slugs:

- `CVPublisher`
- `CVSubscriber`

### The module panel is blank

Make sure the installed package contains the `res/Panel.svg` resource. Rebuild
and reinstall the package if the resource is missing.
