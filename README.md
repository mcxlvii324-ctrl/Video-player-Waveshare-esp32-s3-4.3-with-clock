# Video-player-Waveshare-esp32-s3-4.3-with-clock
How to play avi files and choose them,or autoplay at 800x480 touch screen.It also plays at waveshare esp32s3 7' board.
https://www.youtube.com/watch?v=n14cAWKRzxA
High-Performance Bare-Metal AVI Video Player for Waveshare ESP32-S3 (4.3" & 7")

A highly optimized, bare-metal AVI media player featuring an integrated Wi-Fi synced clock for the Waveshare ESP32-S3-Touch-LCD-4.3 and Waveshare ESP32-S3-Touch-LCD-7 development boards.
Unlike standard implementations that rely on heavy UI frameworks like LVGL (which downscale video performance to 13–15 FPS), this project completely bypasses the graphics framework overhead during video playback. By writing decoded pixels directly into the native Espressif RGB DMA frame buffer via highly optimized 32-bit hardware byte-swapping pointers, it achieves stable playback speeds up to 30+ FPS with zero skipped frames and under 20% CPU utilization.

Key Features

• Playlist Autoplay: Automatically cycles through all compatible videos on the SD card sequentially.
• Instant Touch Interrupt: Tap anywhere on the touch screen during video playback to stop the video immediately and return to the main menu.
• Wi-Fi Sync Clock: Syncs real-time clock data (Time and Date) during the boot phase via NTP.
• Hardware Stability Control: Automatically shuts down the Wi-Fi RF stack post-sync to eliminate background network interrupts, preventing LCD timing jitter and visual artifacts (sparkles/flickering).

Technical Specifications & Video Encoding

To guarantee a stable and high frame rate, video files must be converted into an ultra-low overhead format that the ESP32-S3 dual-core CPU can process efficiently.

🎥 Supported Video Specs

• Container Format: .avi (Audio Video Interleave)
• Video Codec: Cinepak (cvid)
• Optimal Resolution: 480 x 272 (Will render perfectly centered on the 800x480 screen with zero visual tearing).
• Target Frame Rate: 24 FPS or 25 FPS
• Compression Quality: 8, 9, or 10 (Quantization Step)

🛠️ Required Conversion Tool

Videos must be converted using the web-based utility:
👉 Video Conversion Studio

💡 Crucial Encoding Tips for Maximum FPS

1. Source Media Prep (Mute): Before importing your file into the converter, strip out the audio channel completely (Mute the MP4 file). Because this hardware configurations bypass I2S audio chips, removing audio headers completely unburdens the SD card bus, unlocking stable 30+ FPS speeds.
2. YouTube Sourcing: When downloading source clips from YouTube, fetch them directly in 360p or 480p resolution without an audio layer (Video Only). The conversion process in Video Conversion Studio will execute instantly, and the resulting file will run flawlessly on the board.

File System Structure

The software utilizes high-speed 4-bit native bus addressing (SD_MMC).
1. Format your MicroSD Card as FAT32.
2. Create a folder named exactly avi in the root directory.
3. Drop your converted .avi videos inside that folder.

📦 MicroSD Root
 ┗ 📂 avi
    ┣ 📜 Video1.avi
    ┣ 📜 Video2.avi
    ┗ 📜 Movie.avi


How It Works (Application Workflow)


1. Boot Phase (Live Logs)

Upon power-on, the device clears the screen to a deep navy blue and prints step-by-step diagnostic initialization logs:
• Configures dual core display configurations.
• Initiates Wi-Fi network handshakes.
• Contacts the NTP server pool to pull local global time variables.
• Shuts down the Wi-Fi stack completely to isolate the hardware buses from radio frequency core interference.
• Mounts the MicroSD card cluster via SD_MMC.

2. Main Selection Menu

The program renders a black canvas displaying:
• Top: The selected .avi filename decoded beautifully with sub-pixel alignment using the FreeSansBold12pt7b font.
• Center: A rolling kίτρινο (Yellow) clock tracking hours, minutes, seconds, and the active date completely offline.
• Navigation UI: Soft-touch left/right white triangle arrows for navigation and a green playback button.

3. Playback Engine

When the user taps the Green Play Button:
• The screen clears to black and triggers avi_open().
• Dual-core pipelines isolate decoding vectors onto Core 0 while letting Core 1 handle direct rendering pipelines.
• Tapping anywhere on the screen executes an instantaneous hardware touch polling check, invoking a graceful abort loop sequence back to the home selector array.

Hardware Pin Mapping Reference (Waveshare 4.3" / 7")

• SD_MMC Native Bus Pins: CLK: 12, MOSI: 11, MISO: 13
• GT911 Touch Controller I2C Interfacing: SDA: 8, SCL: 9, INT: 4, RST: Controlled via CH422G Expander IO

Authors & Credits

• Bare-metal system scaling engine, Touch Playlist structures, and Anti-Flicker components rewritten by mcxlvii324-ctrl.
• Baseline MJPEG/Cinepak decoding concepts adapted from The Last Outpost Workshop.
