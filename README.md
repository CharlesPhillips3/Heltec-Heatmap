# Heltec Heatmap

Walk or drive around with a Heltec WiFi LoRa 32 **V3 or V4** in your pocket and your iPhone in your hand, then see a color-coded map of every Wi‑Fi network you passed.

- **The Heltec** scans the 2.4 GHz Wi‑Fi band about every 2 seconds and saves every network it hears (name, MAC, channel, security, signal) to its own flash.
- **The iPhone** records your GPS path in the Heltec Heatmap web app.
- **Afterwards** you download the board's log and import it into the app. The app lines the scans up with your GPS path by time and paints the map.

## What the map shows

| View | Colors mean |
|---|---|
| **Best signal** | Strongest network heard in each square (green ≥ −50 dBm … purple below −85) |
| **One network** | Just the network you tap in the list, plus a "router likely near here" pin |
| **How many** | Number of different networks heard per square (channel crowding) |
| **Open** | Networks with no password |

Square size (5–60 m) is adjustable. Tap any square to see the top networks heard there. Street, dark and satellite base maps.

Exports: GPS tracks (JSON), merged CSV, and **WiGLE CSV** (uploads straight to wigle.net).

## Folder layout

```
firmware/            PlatformIO project (V3 and V4 environments)
firmware/arduino/    Same code as an Arduino IDE sketch
docs/index.html      The web app (GitHub Pages serves this folder)
docs/flash.html      Browser flasher (Chrome/Edge on a computer)
docs/firmware/       Ready-to-flash images for V3 and V4
web/                 App source, build script, single-file copy
```

## 1. Flash the board

**Easiest: browser flasher.** Once the repo is on GitHub Pages, open `https://<your-user>.github.io/Heltec-Heatmap/flash.html` in Chrome or Edge on a computer, plug in the board, and click **Flash Heltec V3** or **Flash Heltec V4**. This replaces Meshtastic; put it back any time from flasher.meshtastic.org.

**Command line (no Pages needed):**
```
pip install esptool
esptool.py --chip esp32s3 write_flash 0x0 docs/firmware/heltec-heatmap-v3.bin   # or -v4.bin
```
If it will not connect: hold PRG, tap RST, let go of PRG, then try again.

**Build it yourself with PlatformIO** (uses the pioarduino ESP32 platform, Arduino core 3.1):
```
cd firmware
pio run -e heltec_v3 -t upload      # or: -e heltec_v4
```

**Arduino IDE**
1. Board manager: install **esp32 by Espressif** (3.x).
2. Library manager: install **U8g2**.
3. Open `firmware/arduino/HeltecHeatmap/HeltecHeatmap.ino`.
4. Board: **Heltec WiFi LoRa 32(V3)**. The sketch folder has its own `partitions.csv` (big log space for 8 MB boards), which the IDE uses automatically. On a V4 that works too; PlatformIO or the flasher gives a V4 its full 16 MB.
5. Upload.

The OLED shows the hotspot name, how many networks it hears, the strongest one, and free log space.

Settings at the top of `main.cpp`: hotspot password (`heatmap123`), weakest signal to log (−95 dBm), pause between scans.

## 2. Put the app on GitHub Pages

iPhone Safari only allows GPS on **https** pages, so the app has to be served from a real web address.

1. Make a repo (for example `Heltec-Heatmap`) and push this folder.
2. Repo **Settings › Pages**: Source = *Deploy from a branch*, Branch = `main`, folder = `/docs`.
3. Open `https://<your-user>.github.io/Heltec-Heatmap/` on the iPhone and use **Share › Add to Home Screen**.
4. First time you tap Record GPS, allow location. If it was denied: Settings › Privacy & Security › Location Services › Safari Websites › While Using.

## 3. Do a survey

1. Power the Heltec (USB battery pack is fine).
2. On the iPhone join Wi‑Fi **Heltec-Heatmap-XXXX** (password `heatmap123`), open **http://192.168.4.1**, type a name if you like and tap **Start logging & sync clock**. This copies the phone's time to the board, which is how scans get matched to GPS later.
3. Open the Heltec Heatmap app and tap **Record GPS**. You can rejoin your normal Wi‑Fi or cell data now; the board keeps logging on its own.
4. Walk or drive. **Keep the screen on** — iPhone pauses web GPS when the screen locks. The app asks the phone to stay awake while recording.
5. Tap **Stop recording** in the app.
6. Rejoin the board's Wi‑Fi, open 192.168.4.1, tap **Stop logging**, then **Download log**. Safari saves `heltec-heatmap-log.txt` to Files.
7. In the app tap **Import log or track** and pick that file.

No phone handy? A short press of the **PRG** button starts or stops logging without a clock. Import a GPX from any GPS logger app recorded at the same time, then use the **Time nudge** slider until the colors sit on the path. Hold PRG for 3 seconds to turn the screen off and save battery.

Logs from several boards or several walks can be imported together; everything is matched by time.

## Good to know

- **2.4 GHz only.** The ESP32-S3 radio cannot hear 5 GHz or 6 GHz networks.
- **Log space:** about 5.8 MB on a V3, roughly 8 hours of driving in a busy area and much longer on walks. The board stops logging when space runs low; download and erase.
- **Speed:** a full scan takes about 1.5–2 s. Walking gives very detailed maps; at 30 mph a square gets one scan every ~25 m, so use a larger square size for drives.
- **Signal guide:** −67 dBm is the usual minimum for smooth voice and video calls, −75 is okay for browsing, below −85 is unreliable.
- A V4 (16 MB flash) has about 13.8 MB for logs.
- To change the app, edit `web/app.src.html` and run `python3 web/build.py`.
