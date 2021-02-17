# PI Azure IoT Plug N Play example

Example project with Azure-IoT-SDK-C and a data model (DigitalTwin Definition) for an Azure-IoT-Central application.  
Works with the following configuration:

- [Raspberry Pi Zero](https://www.raspberrypi.org/products/raspberry-pi-zero/)
- [Sense HAT (B)](https://www.waveshare.com/wiki/Sense_HAT_\(B\))

## Status

Currently only the on target build works.  
The docker cross-build environment is not ready yet.

## Prerequisites

### Prerequisites on Host

- [git](https://git-scm.com/downloads)
- [Docker Desktop](https://www.docker.com/get-started)
- [Visual Studio Code](https://code.visualstudio.com/)
  - Add Extension `ms-vscode-remote.remote-containers`

### Prerequisites on Target

- Build code

  ```sh
  sudo apt-get update
  sudo apt-get install -y curl libcurl4-openssl-dev libssl-dev uuid-dev
  git clone https://github.com/Azure/azure-iot-sdk-c.git
  cd azure-iot-sdk-c
  git submodule update --init
  cd azure-iot-sdk-c
  mkdir cmake
  cd cmake
  cmake -Duse_prov_client=ON -Dhsm_type_symm_key=ON -Drun_e2e_tests=OFF ..
  cmake --build .
  sudo make install
  cd ../../
  rm -r azure-iot-sdk-c
  ```
  
- Provisioning of the device: https://docs.microsoft.com/en-us/azure/iot-central/core/tutorial-connect-device?pivots=programming-language-ansi-c (under Run the code)

### Prerequisites Azure IoT Central App

- Import app/RaspberryPiSenseHatB_simple.json as new device template

## Build

### Build on Host

```sh
git clone https://github.com/Tamaluga/pi-azure-iot-pnp-example.git
./pi-azure-iot-pnp-example/tools/build_app_raspberry.sh
```

### Build on Target

```sh
git clone https://github.com/Tamaluga/pi-azure-iot-pnp-example.git
cd pi-azure-iot-pnp-example
mkdir build
cd build
cmake -S .. -B .
camke --build .
```
