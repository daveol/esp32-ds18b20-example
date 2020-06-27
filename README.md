# esp32-ds18b20-mqtt

[![Platform: ESP-IDF](https://img.shields.io/badge/ESP--IDF-v3.0%2B-blue.svg)](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/)

[![license](https://img.shields.io/github/license/mashape/apistatus.svg)]()

## Introduction

This is an application for forwarding the Maxim Integrated DS18B20 Programmable Resolution 1-Wire Digital Thermometer
device to MQTT.

It supports a single or multiple devices on the same 1-Wire bus.

Ensure that submodules are cloned:

    $ git clone --recursive https://github.com/daveol/esp32-ds18b20-mqtt.git

Build the application with:

    $ cd esp32-ds18b20-mqtt
    $ idf.py menuconfig    # set your serial configuration and the 1-Wire GPIO - see below
    $ idf.py build
    $ idf.py -p (PORT) flash monitor

The program should detect your connected devices and periodically obtain temperature readings from them, displaying them
on the console.

## Dependencies

This application makes use of the following components (included as submodules):

 * components/[esp32-owb](https://github.com/DavidAntliff/esp32-owb)
 * components/[esp32-ds18b20](https://github.com/DavidAntliff/esp32-ds18b20)

## Source Code

The source is available from [GitHub](https://www.github.com/daveol/esp32-ds18b20-mqtt).

## License

The code in this project is licensed under the MIT license - see LICENSE for details.

## Links

 * [DS18B20 Datasheet](http://datasheets.maximintegrated.com/en/ds/DS18B20.pdf)
 * [Espressif IoT Development Framework for ESP32](https://github.com/espressif/esp-idf)

## Acknowledgements

 * Based off @DavidAntliff's [esp32-ds18b20-example](https://www.github.com/DavidAntliff/esp32-ds18b20-example)
 * "1-Wire" is a registered trademark of Maxim Integrated.
