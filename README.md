# MagicQuestServer: ESP32 MagiQuest Game Engine

A self-contained, open-source game engine running entirely on an ESP32 that decodes authentic MagiQuest wands and triggers sequential video quests. To do: Branching video quests via a master MagicQuestServer and slave servers that would be located in different parts of the room is a coming feature.

This project liberates comatose, proprietary theme-park tech and allows you to build your own interactive wand-based escape rooms, puzzles, or living room adventures.

## 🌟 Features
* **Standalone Local Engine:** No cloud subscriptions or external servers required. Everything runs locally on the ESP32.
* **Authentic Wand Decoding:** Reads the 38kHz IR bursts from real MagiQuest wands and extracts the unique Hex ID.
* **Persistent Player Save States:** Uses the ESP32's non-volatile memory (Preferences library) to securely track which wand belongs to which player and exactly what page of the story they are currently on.
* **Local Asset Storage:** Uses LittleFS solely to store and serve the web UI assets and the CSV story files.
* **Dynamic Web Dashboard:** A built-in web server provides an Author Panel to easily map YouTube Video IDs to specific wand actions and story pages.
* **Seamless Media Playback:** A captive UI dynamically recycles an embedded YouTube iframe to display video sequences seamlessly on a phone or tablet.
* **Extensible API:** Built-in REST endpoints allow you to expand the game with remote, wireless "satellite" stones (ESP8266) in the future.

## 🛠️ Hardware Used
* **Microcontroller:** Seeed Studio XIAO ESP32 (S3/C3) - Chosen for its incredibly tiny footprint.
* **Sensor:** Standard 38kHz IR Receiver (e.g., TSOP38238).
* **Power:** 5V USB-A Pigtail (Hardwired/Wire-wrapped to the ESP32 5V and GND pins).
* **Enclosure:** Custom 3D-printed *Fifth Element* stone featuring a friction-fit mounting sled, a curved internal cable channel for strain relief, and a recessed "sniper tube" for the IR receiver to block ambient light.

## ⚡ Wiring Guide (MVP)
Because the XIAO ESP32 does not break out its USB data lines to the edge pins, this project uses a power-only hardwire approach, keeping the built-in USB-C port free for future code updates.
* **5V (USB Pigtail)** -> `5V` pin on XIAO
* **GND (USB Pigtail)** -> `GND` pin on XIAO
* **IR Receiver VSS** -> `3V3` pin on XIAO
* **IR Receiver GND** -> `GND` pin on XIAO
* **IR Receiver OUT** -> `D0` pin on XIAO *(Or whichever GPIO you assigned)*

*Note: Wire-wrapping is highly recommended for the XIAO board to make it easy to mount the IR receptor, and avoid lifting pads or damaging components.*

## 🚀 How to Play
1. **Power On:** Plug the stone into any standard USB wall charger.
2. **Wi-Fi Setup:** On first boot, the ESP32 creates an Access Point at 192.168.4.1 with the SSID of `MagiQuest-Admin`. The password with   `magic123`. This is used only to connect it to home 2.4GHz wifi (the ESP32-S3 supports ONLY the 2.4GHz wifi band). Navigate to that address and append `/admin` to open the Admin panel to add your local SSID and password. 
3. **Author a Story:** Navigate to the stone's IP address on your local network and append `/author` to open the Author Panel. Link your YouTube Video IDs to build your story.
4. **Cast a Spell:** Open the main IP address on a phone or tablet, tap "Activate Crystal," and flick your MagiQuest wand at the stone!
5. **Import a Story:** Stories are stored in the LittleFS and are comma deliniated. These files can then be imported via the Arduino IDE directly onto the LittleFS.

## Acknowledgement
The objects provided via https://www.thingiverse.com/thing:1012118 (Phillip Avery) are the basis for these cases.

## 📜 License
MIT License - Free to use, modify, and build upon.
