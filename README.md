# YouTube Downloader

This is a desktop application for downloading YouTube videos (including age-restricted content) using [PySide6](https://pypi.org/project/PySide6/), [QtWebEngine](https://pypi.org/project/PySide6-QtWebEngine/), and [yt-dlp](https://github.com/yt-dlp/yt-dlp). It allows you to sign in to YouTube **inside** the app, capture cookies automatically, and download 4K or audio-only content.

![Image](https://github.com/Jacksony100/Youtube-Downloader/blob/main/screenshot.jpg)

## Features

- **In-app login** to YouTube (QtWebEngine) for age-restricted videos.
- **Multiple resolutions** (up to 4K).
- **Audio-only** (MP3) extraction.
- **Progress bar** and cancel button for each download.
- **Open** the downloaded file in Windows Explorer.
- **Dark UI** with PySide6.

## How to Use (Run from source)

1. Install Python 3.9+ (64-bit recommended).
2. Clone this repository:
   ```bash
   git clone https://github.com/Jacksony100/youtube-downloader.git
   cd youtube-downloader
3. pip install pyinstaller
4. Download ffmpeg and drop to folder "ffmpeg.exe"
   ```bash
   https://www.gyan.dev/ffmpeg/builds/
5. Make the application as a whole:
   ```bash
   pyinstaller --onefile --noconsole ^
    --add-binary "ffmpeg.exe;." ^
    --add-data "icon.ico;." ^
    main.py
## Disclaimer

This software is provided "as is", without any express or implied warranties. The author is not responsible for any misuse or damages resulting from the use of this software. It is the user's sole responsibility to ensure that their use of this tool complies with all applicable laws, regulations, and YouTube's Terms of Service. By using this software, you agree that the author shall not be held liable for any legal issues, copyright infringements, or any other consequences arising from its use.
