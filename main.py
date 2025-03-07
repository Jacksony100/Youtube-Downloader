import sys, os, threading, requests, tempfile, webbrowser, subprocess
from pathlib import Path
from PySide6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit, QPushButton, QComboBox, QFileDialog, QMessageBox, QScrollArea, QProgressBar, QSpacerItem, QSizePolicy, QDialog, QDialogButtonBox
from PySide6.QtCore import Qt, Signal, QObject, QSize, QUrl
from PySide6.QtGui import QPixmap, QIcon
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWebEngineCore import QWebEngineProfile, QWebEngineCookieStore
import yt_dlp

DARK_QSS = """
QMainWindow {
    background-color: #1e1e1e;
    color: #ffffff;
}
QLabel {
    color: #ffffff;
    font-size: 14px;
}
QLineEdit, QComboBox {
    background-color: #2a2a2a;
    color: #ffffff;
    border: 1px solid #444444;
    border-radius: 4px;
    padding: 4px;
}
QScrollArea {
    background-color: #1e1e1e;
    border: none;
}
QPushButton {
    border-radius: 4px;
    padding: 6px 10px;
    font-weight: bold;
    border: none;
}
QPushButton#greenButton {
    background-color: #28a745;
    color: #ffffff;
}
QPushButton#greenButton:hover {
    background-color: #218838;
}
QPushButton#greenButton:pressed {
    background-color: #1e7e34;
}
QPushButton#redButton {
    background-color: #d9534f;
    color: #ffffff;
}
QPushButton#redButton:hover {
    background-color: #c9302c;
}
QPushButton#redButton:pressed {
    background-color: #ac2925;
}
QProgressBar {
    background-color: #2a2a2a;
    border: 1px solid #444444;
    border-radius: 4px;
    text-align: center;
    color: #ffffff;
}
QProgressBar::chunk {
    background-color: #28a745;
}
"""

class LoginDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("YouTube Login")
        self.setMinimumSize(800,600)
        layout = QVBoxLayout(self)
        self.webview = QWebEngineView()
        layout.addWidget(self.webview)
        self.profile = QWebEngineProfile.defaultProfile()
        self.cookie_store = self.profile.cookieStore()
        self.webview.setUrl(QUrl("https://youtube.com"))
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
        self.setLayout(layout)
    def get_cookies(self):
        result = []
        done = []
        def handle_cookie(c):
            d = {
                'domain': c.domain(),
                'path': c.path(),
                'secure': c.isSecure(),
                'httpOnly': c.isHttpOnly(),
                'name': bytes(c.name()).decode('utf-8', 'ignore'),
                'value': bytes(c.value()).decode('utf-8', 'ignore')
            }
            result.append(d)
        def finished():
            done.append(True)
        self.cookie_store.loadAllCookies()
        self.cookie_store.cookieAdded.connect(handle_cookie)
        self.cookie_store.cookiesLoaded.connect(finished)
        self.cookie_store.loadAllCookies()
        while not done:
            QApplication.processEvents()
        return result

class DownloadSignals(QObject):
    finished = Signal(bool, str, str)
    progress = Signal(float)
    status = Signal(str)

class DownloadThread(threading.Thread):
    def __init__(self, url, fmt, ffmpeg_path, output_path, cookie_file):
        super().__init__()
        self.url = url
        self.fmt = fmt
        self.ffmpeg_path = ffmpeg_path
        self.output_path = output_path
        self.cookie_file = cookie_file
        self.signals = DownloadSignals()
        self.cancel_requested = False
    def run(self):
        def hook(d):
            if self.cancel_requested:
                raise Exception("Canceled")
            if d['status'] == 'downloading':
                db = d.get('downloaded_bytes', 0)
                tb = d.get('total_bytes', 0) or d.get('total_bytes_estimate', 0)
                if tb>0:
                    p = db/tb*100
                    self.signals.progress.emit(p)
            elif d['status'] == 'finished':
                self.signals.progress.emit(100)
        post = []
        if "Audio" in self.fmt:
            post = [{'key':'FFmpegExtractAudio','preferredcodec':'mp3','preferredquality':'192'}]
            real_fmt = "bestaudio/best"
        else:
            real_fmt = self.fmt
        opts = {
            'format': real_fmt,
            'outtmpl': os.path.join(self.output_path, '%(title)s.%(ext)s'),
            'merge_output_format': 'mp4',
            'postprocessors': post,
            'progress_hooks': [hook]
        }
        if self.ffmpeg_path:
            opts['ffmpeg_location'] = self.ffmpeg_path
        if self.cookie_file:
            opts['cookiefile'] = self.cookie_file
        try:
            self.signals.status.emit("Downloading...")
            with yt_dlp.YoutubeDL(opts) as ydl:
                info = ydl.extract_info(self.url, download=True)
            final_path = ""
            if 'requested_downloads' in info and info['requested_downloads']:
                final_path = info['requested_downloads'][0].get('filepath',"")
            if not final_path:
                final_path = info.get("_filename","")
            if not os.path.isfile(final_path):
                base, _ = os.path.splitext(final_path)
                exts = [".mp4",".mkv",".webm",".m4a",".mp3"]
                for e in exts:
                    alt = base+e
                    if os.path.isfile(alt):
                        final_path = alt
                        break
            self.signals.finished.emit(True, "Done!", final_path)
        except Exception as e:
            self.signals.finished.emit(False, str(e), "")
    def cancel(self):
        self.cancel_requested = True

class DownloadItem(QWidget):
    def __init__(self, parent, url, title, thumb, fmt, ffmpeg_path, output_path, cookie_file):
        super().__init__(parent)
        self.url = url
        self.title = title
        self.thumb = thumb
        self.fmt = fmt
        self.ffmpeg_path = ffmpeg_path
        self.output_path = output_path
        self.cookie_file = cookie_file
        self.final_path = None
        self.init_ui()
        self.start_download()
    def init_ui(self):
        l = QHBoxLayout(self)
        l.setContentsMargins(5,5,5,5)
        l.setSpacing(10)
        self.thumb_label = QLabel()
        self.thumb_label.setFixedSize(80,60)
        self.thumb_label.setStyleSheet("background-color: #444444;")
        l.addWidget(self.thumb_label)
        self.load_thumb()
        c = QVBoxLayout()
        self.title_label = QLabel(self.title)
        self.title_label.setStyleSheet("font-weight: bold;")
        c.addWidget(self.title_label)
        self.progress_bar = QProgressBar()
        c.addWidget(self.progress_bar)
        self.status_label = QLabel("Waiting...")
        c.addWidget(self.status_label)
        l.addLayout(c)
        r = QVBoxLayout()
        r.setSpacing(5)
        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.setObjectName("redButton")
        self.cancel_btn.clicked.connect(self.cancel_download)
        r.addWidget(self.cancel_btn)
        self.open_btn = QPushButton("Open")
        self.open_btn.setObjectName("greenButton")
        self.open_btn.setEnabled(False)
        self.open_btn.clicked.connect(self.open_file)
        r.addWidget(self.open_btn)
        l.addLayout(r)
    def load_thumb(self):
        if self.thumb:
            try:
                resp = requests.get(self.thumb, timeout=5)
                if resp.status_code==200:
                    pix = QPixmap()
                    pix.loadFromData(resp.content)
                    pix = pix.scaled(self.thumb_label.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation)
                    self.thumb_label.setPixmap(pix)
            except:
                pass
    def start_download(self):
        self.thread = DownloadThread(self.url,self.fmt,self.ffmpeg_path,self.output_path,self.cookie_file)
        self.thread.signals.progress.connect(self.on_progress)
        self.thread.signals.status.connect(self.on_status)
        self.thread.signals.finished.connect(self.on_finished)
        self.thread.start()
    def on_progress(self, p):
        self.progress_bar.setValue(int(p))
    def on_status(self, s):
        self.status_label.setText(s)
    def on_finished(self, ok, msg, fp):
        self.final_path = fp
        if ok:
            self.status_label.setText("Finished!")
            self.cancel_btn.setEnabled(False)
            self.open_btn.setEnabled(True)
        else:
            self.status_label.setText("Error: "+msg)
            self.cancel_btn.setEnabled(False)
    def cancel_download(self):
        if hasattr(self,'thread') and self.thread.is_alive():
            self.thread.cancel()
            self.status_label.setText("Canceled...")
            self.cancel_btn.setEnabled(False)
    def open_file(self):
        if self.final_path and os.path.isfile(self.final_path):
            try:
                subprocess.run(["explorer","/select,",self.final_path])
            except:
                QMessageBox.warning(self,"Error","Could not open the file.")
        else:
            QMessageBox.information(self,"Information","File not found or path is incorrect.")

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setMinimumSize(1200,700)
        self.setWindowTitle("YouTube Downloader")
        icon_path = self.find_icon("icon.ico")
        if icon_path and os.path.isfile(icon_path):
            self.setWindowIcon(QIcon(icon_path))
        self.ffmpeg_path = self.find_ffmpeg()
        self.output_path = str(Path.cwd())
        self.cookie_file = None
        w = QWidget()
        self.setCentralWidget(w)
        ml = QVBoxLayout(w)
        tl = QHBoxLayout()
        self.url_input = QLineEdit()
        self.url_input.setPlaceholderText("Enter a YouTube link...")
        tl.addWidget(self.url_input)
        self.format_combo = QComboBox()
        self.format_combo.addItem("Video (best, up to 4K+)","bestvideo+bestaudio/best")
        self.format_combo.addItem("Video (4K, 2160p)","bestvideo[height<=2160]+bestaudio/best[height<=2160]")
        self.format_combo.addItem("Video (1080p)","bestvideo[height<=1080]+bestaudio/best[height<=1080]")
        self.format_combo.addItem("Video (720p)","bestvideo[height<=720]+bestaudio/best[height<=720]")
        self.format_combo.addItem("Audio (MP3)","bestaudio/best")
        tl.addWidget(self.format_combo)
        add_btn = QPushButton("Add")
        add_btn.setObjectName("greenButton")
        add_btn.clicked.connect(self.on_add)
        tl.addWidget(add_btn)
        fold_btn = QPushButton("Folder")
        fold_btn.setObjectName("greenButton")
        fold_btn.clicked.connect(self.on_folder)
        tl.addWidget(fold_btn)
        self.login_btn = QPushButton("Login in-app")
        self.login_btn.setObjectName("greenButton")
        self.login_btn.clicked.connect(self.on_in_app_login)
        tl.addWidget(self.login_btn)
        ml.addLayout(tl)
        self.scroll_area = QScrollArea()
        self.scroll_area.setWidgetResizable(True)
        self.downloads_container = QWidget()
        self.dl_layout = QVBoxLayout(self.downloads_container)
        self.dl_layout.setContentsMargins(5,5,5,5)
        self.dl_layout.setSpacing(5)
        self.scroll_area.setWidget(self.downloads_container)
        ml.addWidget(self.scroll_area)
        sp = QSpacerItem(20,20,QSizePolicy.Minimum,QSizePolicy.Expanding)
        self.dl_layout.addSpacerItem(sp)
        bot_label = QLabel('coded by Jacksony | TG: <a href="https://t.me/Smesharik_lair">https://t.me/Smesharik_lair</a>')
        bot_label.setOpenExternalLinks(True)
        bot_label.setAlignment(Qt.AlignCenter)
        ml.addWidget(bot_label)
        self.setStyleSheet(DARK_QSS)
    def on_add(self):
        url = self.url_input.text().strip()
        if not url:
            QMessageBox.warning(self,"Error","Please enter a link.")
            return
        f = self.format_combo.currentData()
        try:
            info = self.get_info(url)
            title = info.get("title",url)
            thumb = info.get("thumbnail","")
        except Exception as e:
            QMessageBox.warning(self,"Error",f"Failed to retrieve info:\n{e}")
            return
        item = DownloadItem(self.downloads_container,url,title,thumb,f,self.ffmpeg_path,self.output_path,self.cookie_file)
        self.dl_layout.insertWidget(self.dl_layout.count()-1,item)
        self.url_input.clear()
    def on_folder(self):
        f = QFileDialog.getExistingDirectory(self,"Select folder",self.output_path)
        if f:
            self.output_path = f
            QMessageBox.information(self,"Info",f"Files will be saved in:\n{f}")
    def on_in_app_login(self):
        dlg = LoginDialog(self)
        if dlg.exec()==QDialog.Accepted:
            c = dlg.get_cookies()
            self.cookie_file = self.create_cookie_file(c)
            QMessageBox.information(self,"Info","You are signed in. Age-restricted videos should now work.")
        else:
            QMessageBox.warning(self,"Info","Login canceled.")
    def create_cookie_file(self, cookies):
        tmp = tempfile.NamedTemporaryFile(delete=False, mode='w', suffix='.txt')
        tmp.write("# Netscape HTTP Cookie File\n# Generated by PySide6 QtWebEngine\n\n")
        for c in cookies:
            d = c['domain']
            flag = "TRUE" if d.startswith('.') else "FALSE"
            p = c['path'] if c['path'] else '/'
            s = "TRUE" if c['secure'] else "FALSE"
            exp = "1893456000"
            n = c['name']
            v = c['value']
            line = f"{d}\t{flag}\t{p}\t{s}\t{exp}\t{n}\t{v}\n"
            tmp.write(line)
        tmp.close()
        return tmp.name
    def get_info(self,url):
        o={"skip_download":True}
        if self.ffmpeg_path:
            o["ffmpeg_location"]=self.ffmpeg_path
        if self.cookie_file:
            o["cookiefile"]=self.cookie_file
        with yt_dlp.YoutubeDL(o) as y:
            return y.extract_info(url,download=False)
    def find_ffmpeg(self):
        if getattr(sys,'frozen',False):
            d = sys._MEIPASS
        else:
            d = os.path.dirname(os.path.abspath(__file__))
        c = os.path.join(d,"ffmpeg.exe")
        return c if os.path.isfile(c) else None
    def find_icon(self,name):
        if getattr(sys,'frozen',False):
            d = sys._MEIPASS
        else:
            d = os.path.dirname(os.path.abspath(__file__))
        c = os.path.join(d,name)
        return c if os.path.isfile(c) else None

def main():
    QApplication.setAttribute(Qt.AA_ShareOpenGLContexts)
    app = QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())

if __name__=="__main__":
    main()
