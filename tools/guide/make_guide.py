#!/usr/bin/env python3
"""Builds the SkyPoint user guide: docs/SkyPoint_Beta_1_User_Guide.pdf plus two booklet versions
for printing two-sided (2 pages per sheet side, fold in the middle).

Usage:   python3 tools/guide/make_guide.py
Needs:   pip install reportlab pypdf, and the DejaVu fonts (set DEJAVU_DIR if they live elsewhere).
Images:  docs/images/skypoint_logo.png, docs/screenshots/*.png (watch, from the simulator),
         docs/screenshots/android/*.png (phone)."""
import os
from reportlab.lib.pagesizes import letter
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Table, TableStyle,
                                Image, PageBreak, KeepTogether, CondPageBreak, NextPageTemplate)
from reportlab.platypus.tableofcontents import TableOfContents
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib import colors
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.fonts import addMapping

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DOCS = os.path.join(REPO, "docs")
SHOTS = os.path.join(DOCS, "screenshots")
ASHOTS = os.path.join(SHOTS, "android")
RELEASE = "Beta 1"
NAME = "SkyPoint_Beta_1_User_Guide"

F = os.environ.get("DEJAVU_DIR", "/usr/share/fonts/truetype/dejavu/")
pdfmetrics.registerFont(TTFont("Sans", F + "DejaVuSans.ttf"))
pdfmetrics.registerFont(TTFont("Sans-Bold", F + "DejaVuSans-Bold.ttf"))
pdfmetrics.registerFont(TTFont("Sans-Oblique", F + "DejaVuSans-Oblique.ttf"))
pdfmetrics.registerFont(TTFont("Mono", F + "DejaVuSansMono.ttf"))
# headings in the same family as the apps
pdfmetrics.registerFont(TTFont("Head", os.path.join(HERE, "fonts", "SairaSemiCondensed-Bold.ttf")))
pdfmetrics.registerFont(TTFont("Head-Semi", os.path.join(HERE, "fonts", "SairaSemiCondensed-SemiBold.ttf")))
addMapping("Sans", 0, 0, "Sans"); addMapping("Sans", 1, 0, "Sans-Bold")
addMapping("Sans", 0, 1, "Sans-Oblique"); addMapping("Sans", 1, 1, "Sans-Bold")

# palette: the logo's cyan and violet, darkened for print
ACC = colors.HexColor("#0B7A8A")     # deep cyan
VIO = colors.HexColor("#6A4FD0")     # violet
INK = colors.HexColor("#141B26")
DIM = colors.HexColor("#5A6675")
RULE = colors.HexColor("#D3DAE2")
TINT = colors.HexColor("#EEF6F8")
TINTV = colors.HexColor("#F2EFFC")
WARNC = colors.HexColor("#B4441F")
TINTW = colors.HexColor("#FBF0EB")
HEADBG = colors.HexColor("#0E1626")

ss = getSampleStyleSheet()
body = ParagraphStyle("body", parent=ss["Normal"], fontName="Sans", fontSize=9.4, leading=13.6, textColor=INK, spaceAfter=5)
small = ParagraphStyle("small", parent=body, fontSize=8.2, leading=11.2, textColor=DIM)
cap = ParagraphStyle("cap", parent=small, alignment=1, fontSize=7.6, leading=9.6, spaceBefore=2)
cell = ParagraphStyle("cell", parent=body, fontSize=8.2, leading=10.9, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Sans-Bold")
cellh = ParagraphStyle("cellh", parent=cell, fontName="Sans-Bold", textColor=colors.white)
h1 = ParagraphStyle("h1", parent=body, fontName="Head", fontSize=21, leading=25, textColor=INK, spaceBefore=0, spaceAfter=10, keepWithNext=1)
h2 = ParagraphStyle("h2", parent=body, fontName="Head-Semi", fontSize=13.5, leading=17, textColor=ACC, spaceBefore=10, spaceAfter=4, keepWithNext=1)
h3 = ParagraphStyle("h3", parent=body, fontName="Sans-Bold", fontSize=9.8, leading=13, textColor=INK, spaceBefore=6, spaceAfter=2, keepWithNext=1)
bul = ParagraphStyle("bul", parent=body, leftIndent=13, bulletIndent=3, spaceAfter=2.5)
num = ParagraphStyle("num", parent=body, leftIndent=15, bulletIndent=1, spaceAfter=3)
code = ParagraphStyle("code", parent=body, fontName="Mono", fontSize=7.9, leading=10.8, backColor=TINT,
                      borderPadding=(5, 6, 5, 6), spaceBefore=4, spaceAfter=9, textColor=INK)
toc1 = ParagraphStyle("toc1", parent=body, fontName="Sans-Bold", fontSize=9.6, leading=17)
title = ParagraphStyle("title", parent=body, fontName="Head", fontSize=26, leading=30, textColor=INK, alignment=1)
sub = ParagraphStyle("sub", parent=body, fontSize=11.5, leading=15, textColor=DIM, alignment=1)


def P(t, s=body): return Paragraph(t, s)
def B(items): return [Paragraph(i, bul, bulletText="•") for i in items]
def N(items): return [Paragraph(i, num, bulletText=f"{k}.") for k, i in enumerate(items, 1)]


class H1(Paragraph):
    """Chapter heading that also lands in the table of contents."""
    pass


def chapter(t):
    p = H1(t, h1)
    return [CondPageBreak(4.2 * inch), Spacer(1, 6), p]


def box(kind, head, text):
    col, bg = {"TIP": (ACC, TINT), "NOTE": (VIO, TINTV), "IMPORTANT": (WARNC, TINTW)}[kind]
    hs = ParagraphStyle("bh", parent=cell, fontName="Sans-Bold", textColor=col, fontSize=8.2)
    t = Table([[P(f"{kind}  <font color='#141B26'>{head}</font>", hs)], [P(text, cell)]], colWidths=[6.7 * inch])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), bg), ("LINEBEFORE", (0, 0), (0, -1), 2.5, col),
                           ("LEFTPADDING", (0, 0), (-1, -1), 9), ("RIGHTPADDING", (0, 0), (-1, -1), 9),
                           ("TOPPADDING", (0, 0), (-1, 0), 7), ("BOTTOMPADDING", (0, -1), (-1, -1), 8),
                           ("TOPPADDING", (0, 1), (-1, 1), 2)]))
    return KeepTogether([Spacer(1, 4), t, Spacer(1, 8)])


def table(rows, widths, header=True):
    data = [[P(c, cellh if (header and r == 0) else (cellb if ci == 0 else cell)) if isinstance(c, str) else c
             for ci, c in enumerate(row)] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    st = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
          ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
          ("LEFTPADDING", (0, 0), (-1, -1), 6), ("RIGHTPADDING", (0, 0), (-1, -1), 6)]
    if header:
        st += [("BACKGROUND", (0, 0), (-1, 0), HEADBG)]
    t.setStyle(TableStyle(st))
    return t


def watch(name, w=1.55 * inch, label=None):
    im = Image(os.path.join(SHOTS, name + ".png"), width=w, height=w * 502 / 410)
    return [im, P(label, cap)] if label else [im]


def phone(name, w=1.25 * inch, label=None):
    im = Image(os.path.join(ASHOTS, name + ".png"), width=w, height=w * 880 / 412)
    return [im, P(label, cap)] if label else [im]


def gallery(items, colw):
    t = Table([items], colWidths=[colw] * len(items))
    t.setStyle(TableStyle([("ALIGN", (0, 0), (-1, -1), "CENTER"), ("VALIGN", (0, 0), (-1, -1), "TOP"),
                           ("LEFTPADDING", (0, 0), (-1, -1), 2), ("RIGHTPADDING", (0, 0), (-1, -1), 2)]))
    return t


def side(pic, paras, picw=1.85 * inch):
    """picture on the left, text on the right"""
    t = Table([[pic, paras]], colWidths=[picw, 6.75 * inch - picw])
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                           ("RIGHTPADDING", (0, 0), (0, -1), 12), ("RIGHTPADDING", (1, 0), (1, -1), 0)]))
    return t


class Doc(BaseDocTemplate):
    def __init__(self, fn):
        super().__init__(fn, pagesize=letter, leftMargin=0.85 * inch, rightMargin=0.85 * inch,
                         topMargin=0.85 * inch, bottomMargin=0.8 * inch,
                         title="SkyPoint User Guide", author="SKIrf Labs", subject="User guide and start-up guide",
                         creator="SKIrf Labs")
        fr = Frame(self.leftMargin, self.bottomMargin, self.width, self.height, id="f")
        self.addPageTemplates([PageTemplate("cover", [fr], onPage=self.cover_page),
                               PageTemplate("body", [fr], onPage=self.body_page)])

    def afterFlowable(self, f):
        if isinstance(f, H1):
            self.notify("TOCEntry", (0, f.getPlainText(), self.page))

    def cover_page(self, c, doc):
        c.saveState()
        c.setFillColor(HEADBG); c.rect(0, letter[1] - 4.35 * inch, letter[0], 4.35 * inch, stroke=0, fill=1)
        # a few stars on the cover band
        import random
        rnd = random.Random(7)
        for _ in range(140):
            x, y = rnd.uniform(0, letter[0]), rnd.uniform(letter[1] - 4.35 * inch, letter[1])
            c.setFillColor(rnd.choice([colors.HexColor("#5cf2ff"), colors.white, colors.HexColor("#9d7bff"), colors.HexColor("#c9d6e6")]))
            c.circle(x, y, rnd.uniform(0.3, 1.2), stroke=0, fill=1)
        logo = os.path.join(DOCS, "images", "skypoint_logo.png")
        lw = 3.6 * inch; lh = lw * 1288 / 1678
        c.drawImage(logo, (letter[0] - lw) / 2, letter[1] - 0.25 * inch - lh, lw, lh, mask="auto")
        c.setFont("Sans", 7.2); c.setFillColor(DIM)
        c.drawCentredString(letter[0] / 2, 0.5 * inch, "Printable guide. Screen pictures are renders of the real interface, not photographs.")
        c.restoreState()

    def body_page(self, c, doc):
        c.saveState()
        c.setFillColor(ACC); c.rect(0.85 * inch, letter[1] - 0.52 * inch, 0.32 * inch, 2.2, stroke=0, fill=1)
        c.setFont("Sans-Bold", 7.2); c.setFillColor(INK)
        c.drawString(1.24 * inch, letter[1] - 0.55 * inch, "SKYPOINT")
        c.setFont("Sans", 7.2); c.setFillColor(DIM)
        c.drawString(1.24 * inch + 40, letter[1] - 0.55 * inch, f"·  User Guide  ·  {RELEASE}")
        c.setStrokeColor(RULE); c.setLineWidth(0.5)
        c.line(0.85 * inch, 0.62 * inch, letter[0] - 0.85 * inch, 0.62 * inch)
        c.drawRightString(letter[0] - 0.85 * inch, 0.45 * inch, f"Page {doc.page}")
        c.drawString(0.85 * inch, 0.45 * inch, "SKIrf Labs")
        c.restoreState()


s = []
# ================================================================== cover
s += [Spacer(1, 3.75 * inch), P("User Guide and Start-up Guide", title), Spacer(1, 4),
      P("LilyGO T-Watch Ultra  ·  Waveshare ESP32-C6-Touch-AMOLED-2.06  ·  Android", sub),
      P(RELEASE, sub), Spacer(1, 16)]
s.append(gallery([watch("01_sky_lock_planet", 1.45 * inch, "Watch: Sky") + [],
                  watch("06_tonight", 1.45 * inch, "Watch: Tonight"),
                  phone("android_01_sky_orion", 0.99 * inch, "Phone: Sky"),
                  phone("android_04_info_iss", 0.99 * inch, "Phone: Info sheet")], 1.68 * inch))
s += [NextPageTemplate("body"), PageBreak()]

# ================================================================== contents
toc = TableOfContents()
toc.levelStyles = [toc1]
toc.dotsMinLevel = 0
s += [P("Contents", h1), toc, PageBreak()]

# ================================================================== 1 about
s += chapter("1. About SkyPoint")
s.append(P("SkyPoint is a pocket planetarium you point at the sky. Hold the watch (or your phone) up, look "
           "\"through\" it, and the screen shows what is behind it, live as you move: stars, constellations, "
           "planets, the Moon in its real phase, galaxies and clusters, and satellites such as the "
           "<b>International Space Station</b> on their way over. Centre something and the reticle locks on and "
           "tells you what it is."))
s.append(P("What it shows", h2))
s += B(["<b>1,675 stars</b> down to about magnitude 5, coloured by their real temperature, with their names.",
        "<b>88 constellation figures</b> and names, and on the phone, optional <b>drawings</b> of the best-known figures.",
        "<b>The Sun, the Moon and all seven planets</b>, computed for your place and time. The Moon shows its real phase.",
        "<b>109 Messier objects</b>: the Orion Nebula, the Andromeda Galaxy, the Pleiades and the rest.",
        "<b>Satellites</b> (ISS, Tiangong, Hubble) with their predicted track, and every pass over your location for the next day and a half.",
        "<b>Tonight</b>: sunset, full dark and sunrise, the Moon, which planets are up, and the bright stars and deep-sky objects worth a look."])
s.append(P("Three versions", h2))
s.append(table([
    ["Version", "Runs on", "Finds north with", "Gets time and place from"],
    ["T-Watch Ultra", "LilyGO T-Watch Ultra (ESP32-S3). Standalone, or as an app in bmorcelli's Launcher.", "You: align once per session on something you can see", "Its GPS"],
    ["Waveshare C6", "Waveshare ESP32-C6-Touch-AMOLED-2.06", "You: align once per session", "Wi-Fi (internet time and approximate location) or a city you pick"],
    ["Android", "Phones with Android 8.0 or newer", "The phone's compass, corrected to true north", "The phone's GPS or network location"],
], [1.1 * inch, 2.2 * inch, 1.8 * inch, 1.6 * inch]))
s.append(P("How it works in one minute", h2))
s.append(P("Everything is calculated on the device. The watch knows the time and where you are, so it can work "
           "out where every star, planet and satellite sits in your sky right now. Its motion sensor knows exactly "
           "which way is <b>down</b>, so the height of everything on screen is always right. What the watches "
           "cannot sense is <b>north</b>: neither has a compass chip. You tell them once, by pointing at the Moon, a "
           "planet or a bright star and confirming it (section 5). Phones have a compass, so the Android app knows "
           "north straight away."))
s.append(P("Satellites move fast and their orbits drift, so SkyPoint uses the latest orbital data (\"TLEs\") from "
           "CelesTrak. It ships with data for the ISS and refreshes it over the internet when you ask."))
s.append(box("NOTE", "About Beta 1",
             "This is the first public release. It has been tested on the boards it supports and checked against "
             "professional astronomy software, but expect rough edges. The guide describes Beta 1; if your screen "
             "looks different, you may have a newer build."))

# ================================================================== 2 what you need
s += chapter("2. What you need")
s.append(table([
    ["Item", "Why", "Notes"],
    ["One of the supported devices", "Runs SkyPoint", "LilyGO T-Watch Ultra, Waveshare ESP32-C6-Touch-AMOLED-2.06, or an Android 8.0+ phone"],
    ["USB-C data cable", "Installing on a watch", "Some cables only charge. If the computer doesn't see the watch, try another cable."],
    ["A computer with Chrome or Edge", "The web flasher", "Windows, macOS, Linux or ChromeOS. Firefox and Safari can't flash."],
    ["Wi-Fi network", "Fresh satellite orbits; time and place on the C6", "2.4 GHz network. Optional on the T-Watch."],
    ["A clear view of the sky", "GPS fix (T-Watch), and the fun part", "The T-Watch's first GPS fix outdoors takes 1 to 5 minutes."],
    ["microSD card", "Optional on the T-Watch", "For a Wi-Fi file or your own orbit file (section 10)."],
], [1.7 * inch, 1.9 * inch, 3.1 * inch]))

# ================================================================== 3 quick start
s += chapter("3. Quick start")
s.append(P("3.1 Install on the T-Watch Ultra", h2))
s.append(P("<b>With Launcher</b> (keeps your other apps): download <b>SkyPoint_TWatchUltra_Beta_1_Launcher.bin</b> "
           "from the web flasher or the Releases page, copy it to the watch's microSD card, then install and start "
           "it from Launcher. SETUP › <b>Exit to Launcher</b> takes you back."))
s.append(P("<b>Standalone</b> (replaces whatever is on the watch): open the SkyPoint web flasher at <b>skirf-labs.tech/SKYPOINT</b> in Chrome or "
           "Edge, plug in the watch, pick <b>LilyGO T-Watch Ultra › Install standalone</b> and follow the prompts. "
           "Or use esptool:"))
s.append(P("esptool --chip esp32s3 write-flash 0x0 SkyPoint_TWatchUltra_Beta_1.bin", code))
s.append(box("IMPORTANT", "Launcher users",
             "The standalone image replaces Launcher. If you use Launcher, install the Launcher .bin from the SD card instead."))
s.append(P("3.2 Install on the Waveshare C6 AMOLED 2.06", h2))
s.append(P("In the web flasher (<b>skirf-labs.tech/SKYPOINT</b>) choose <b>Waveshare C6 AMOLED 2.06 › Install</b> and tick <b>Erase device</b> "
           "the first time. Or with esptool:"))
s.append(P("esptool --chip esp32c6 erase-flash<br/>esptool --chip esp32c6 write-flash 0x0 SkyPoint_WaveshareC6_AMOLED206_Beta_1.bin", code))
s.append(box("TIP", "If the computer can't connect",
             "Hold the board's BOOT button while you plug in the cable, then let go and try again. The C6 image is "
             "built for the board's 4 MB flash; an image built for 16 MB boots to a black screen."))
s.append(P("3.3 Install on Android", h2))
s += N(["Open <b>skirf-labs.tech/SKYPOINT</b> (or the Releases page) <b>on your phone</b> and tap <b>Download APK</b>.",
        "Open the downloaded <b>SkyPoint_1.0_Beta_1.apk</b>. If asked, allow your browser to install unknown apps.",
        "If Play Protect says it doesn't recognise the app, tap <b>More details › Install anyway</b>.",
        "Open SkyPoint and allow <b>location</b>. Allow the <b>camera</b> later only if you want the camera view."])
s.append(P("3.4 Your first night out", h2))
s += N(["<b>Get time and place.</b> T-Watch: go outside and open the GPS page; wait for <b>LOCKED</b> (the watch buzzes). "
        "C6: SETUP › Wi-Fi, choose your network and type the password, then <b>Sync over Wi-Fi</b>. Phone: allow location.",
        "<b>Align north</b> (watches only, once per session): long-press the sky, pick the Moon, a planet or a bright "
        "star you can see, centre it in the reticle and long-press again (section 5.4).",
        "<b>Look through it.</b> Hold the watch up like a window, or point the phone's back camera at the sky. Move "
        "slowly; the reticle locks on when something is centred.",
        "<b>Hunt.</b> Open TONIGHT or ORBIT and tap anything. An arrow guides you to it, with rising tones."])
s.append(box("IMPORTANT", "Never point at the Sun",
             "SkyPoint shows the Sun's position in daytime. Don't look at the Sun directly, through binoculars or a "
             "camera, even to line it up."))

# ================================================================== 4 getting around (watch)
s += chapter("4. Getting around the watch")
s.append(P("The watch app has four pages in a row (five on the T-Watch Ultra, which adds GPS). Swipe left and right to "
           "move between them. The dots at the bottom show where you are."))
s.append(table([
    ["Page", "What it's for"],
    ["SKY", "The live view of the sky through the watch. Lock on, zoom, find."],
    ["TONIGHT", "Sun and Moon times, planets, bright stars and deep-sky objects for tonight."],
    ["ORBIT", "The next satellite pass with a countdown and sky plot, and the passes after it."],
    ["GPS", "T-Watch only. Fix status, satellites in view, accuracy and position."],
    ["SETUP", "Alignment, location, Wi-Fi, orbits, display, sound and time settings."],
], [1.1 * inch, 5.6 * inch]))
s.append(P("Touch and buttons", h2))
s.append(table([
    ["Do this", "To get this"],
    ["Swipe left / right", "Next or previous page"],
    ["Tap the sky", "Zoom: 90°, 60°, 35°, 20° field of view, then back to 90°"],
    ["Long-press the sky", "Align north (or re-align on the locked object, section 5.4)"],
    ["Tap a row on TONIGHT or ORBIT", "Find mode: an arrow and tones guide you to it. Tap the banner to stop."],
    ["Side button (T-Watch) or BOOT (C6): click", "Next page"],
    ["Side button or BOOT: double-click", "Night vision on / off"],
    ["PWR, short press (C6)", "Screen off / on"],
], [2.4 * inch, 4.3 * inch]))
s.append(P("Sounds and vibration", h2))
s.append(P("SkyPoint ticks when the reticle passes over something, chirps when it locks on, chimes when a find "
           "succeeds, when north is aligned and when the GPS locks, and plays a short alert before a visible ISS pass. "
           "Set the volume in SETUP (0 is silent). The T-Watch also vibrates; switch that off in SETUP › Vibration."))

# ================================================================== 5 sky page
s += chapter("5. The Sky page")
s.append(side(watch("01_sky_lock_planet", 1.75 * inch, "Locked on Saturn"),
              [P("The Sky page is a window. Everything is drawn where it really is, so moving the watch moves the sky.", body),
               P("Reading the screen", h3)] + B([
                   "<b>Reticle</b> in the middle. When an object sits in it, brackets snap around it and it turns amber: <b>locked</b>.",
                   "<b>Info card</b> at the bottom when locked: name, type, constellation, brightness (magnitude), distance, size, and its height (ALT) and direction (AZ).",
                   "<b>Compass tape</b> along the bottom edge: the direction you face, in degrees and as N, NE, E and so on.",
                   "<b>Altitude ladder</b> on the right: how high you are pointing, from 0° (horizon) to 90° (straight up).",
                   "<b>Horizon</b>: a glowing line with N, E, S and W. Below it the ground is shaded; objects below the horizon are dimmed (SETUP › See through Earth).",
                   "<b>Top bar</b>: zoom (field of view), the time, GPS status and battery."])))
s.append(P("5.1 What the symbols mean", h2))
s.append(table([
    ["On screen", "Means"],
    ["Coloured dots", "Stars. Size shows brightness; colour shows temperature (blue-white hot, orange-red cool)."],
    ["Thin lines and names", "Constellation figures and names (SETUP › Constellations)."],
    ["Larger coloured discs", "Planets. Saturn has its rings."],
    ["The Moon", "Drawn in its real phase, lit from the side the Sun is on."],
    ["Small purple rings", "Messier objects: galaxies, nebulae and star clusters."],
    ["Satellite icon with a dotted line", "A satellite and where it will be over the next 10 minutes."],
], [2.0 * inch, 4.7 * inch]))
s.append(P("5.2 Zoom", h2))
s.append(P("Tap the sky to step through 90°, 60°, 35° and 20°. At 20° you see fainter stars and smaller details; the "
           "current field of view is shown at the top left."))
s.append(P("5.3 Find mode", h2))
s.append(side(watch("04_sky_find_moon", 1.55 * inch, "Find mode: the Moon"),
              [P("Tap any row on TONIGHT or ORBIT and the Sky page opens with a banner and an arrow at the edge of the "
                 "screen. Turn the way the arrow points. The watch ticks every 10° you get closer and double-buzzes when "
                 "the object is in the reticle. Tap the banner to stop.", body),
               P("A visible ISS pass starts Find mode by itself when the station rises, if Pass alerts are on.", body)],
              picw=1.65 * inch))
s.append(P("5.4 Align north (watches)", h2))
s.append(P("The watches know which way is down but not where north is, so you show them once per session. It takes "
           "about 20 seconds:"))
s += N(["Long-press the sky (or SETUP › <b>Align heading</b>).",
        "Pick something you can actually see: the Moon, a bright planet, or one of the bright stars listed. "
        "<b>Face north</b> is there too if you know where north is.",
        "A dashed amber line marks the object's height. Raise the watch to that line.",
        "Turn on the spot until the object is in the reticle.",
        "Long-press. The watch chimes and says <b>Aligned on …</b>."])
s.append(box("TIP", "Keep it tight",
             "The gyro drifts slowly. When the reticle is locked on something real and you're centred on it, long-press "
             "to re-align in one step. Every half hour or so keeps it accurate."))
s.append(P("5.5 Pointing style", h2))
s.append(P("SETUP › <b>Pointing</b>: <b>Viewfinder</b> (default) means you look through the watch like a window. "
           "<b>Pointer</b> means you aim the watch's 12 o'clock edge at the sky, like a laser pointer, and read the "
           "screen from above."))

# ================================================================== 6 tonight
s += chapter("6. Tonight")
s.append(side(watch("06_tonight", 1.75 * inch, "Tonight"),
              [P("Everything worth knowing before you go out, for your place and today's date.", body)] + B([
                  "<b>Sunset, dark, dawn and sunrise.</b> \"Dark\" is astronomical dark, when the Sun is 18° below the horizon and the sky is fully dark.",
                  "<b>The Moon</b>: its phase drawn to scale, how much is lit, its age in days, and when it rises and sets.",
                  "<b>Planets</b>: which are up now and where, or when each one rises, with its brightness.",
                  "<b>Bright stars and deep-sky objects</b> that are up now."]) +
              [P("Tap any row to find it in the sky.", body)]))

# ================================================================== 7 orbit
s += chapter("7. Orbit")
s.append(side(watch("07_orbit", 1.75 * inch, "Orbit"),
              [P("The next pass of the tracked satellites over your location.", body)] + B([
                  "<b>Countdown</b> to the next pass, large in the middle.",
                  "<b>Sky plot</b>: the pass drawn on a circle of your sky (N at the top, the centre is overhead), from where it rises (green) to where it sets (red).",
                  "<b>Rise, peak and set</b> times and directions, and the highest point.",
                  "<b>VISIBLE</b> or <b>NOT VISIBLE</b>: a satellite can only be seen when it is lit by the Sun while your sky is dark, around dusk and dawn.",
                  "Later passes are listed below. Tap one to find it.",
                  "The age of the orbit data is shown at the top. Update it when it's over a week old (section 10)."])))
s.append(box("TIP", "Seeing the ISS",
             "A visible ISS pass is one of the best things to show people. It looks like a bright, steady star moving "
             "across the sky in a few minutes, with no blinking lights. With SETUP › Pass alerts on, the watch alerts "
             "you two minutes before it rises."))

# ================================================================== 8 gps page
s += chapter("8. The GPS page (T-Watch Ultra)")
s.append(side(watch("10_gps_locked", 1.65 * inch, "GPS locked"),
              [P("The status pill at the top says <b>SEARCHING</b> until the GPS has a fix, then <b>LOCKED</b> (the watch "
                 "buzzes and shows a message wherever you are in the app).", body)] + B([
                  "<b>Radar plot</b>: each satellite the GPS can hear, placed by direction and height. Filled dots are used in the fix; colour shows signal strength.",
                  "<b>Accuracy</b>, <b>altitude</b> and <b>time to fix</b>.",
                  "<b>Position</b> at the bottom. \"(LAST)\" means it's the last saved position while a new fix is found."]), picw=1.8 * inch))
s.append(P("Once locked, the GPS sets the watch's clock (kept in the real-time clock chip) and saves your position, "
           "so the sky is right even indoors afterwards. Indoors with no saved position, pick a city in SETUP › Location."))

# ================================================================== 9 setup
s += chapter("9. Setup reference (watches)")
s.append(gallery([watch("08_setup", 1.5 * inch, "SETUP")[0], watch("12_keyboard", 1.5 * inch, "Wi-Fi password keyboard")[0]], 2.0 * inch))
s.append(table([
    ["Setting", "What it does"],
    ["Align heading", "Tell the watch where north is (section 5.4). Shows when you last aligned."],
    ["Location", "T-Watch: GPS, or pick a city indoors. C6: pick a city or use Wi-Fi."],
    ["Wi-Fi", "Choose a network and type the password on the big-key keyboard."],
    ["Update orbits / Sync over Wi-Fi", "T-Watch: download fresh satellite data. C6: also sets the time and your approximate location."],
    ["Pointing", "Viewfinder (look through) or Pointer (aim the top edge)."],
    ["Night vision", "Turns everything red so your eyes stay dark-adapted. Also a double-click on the button."],
    ["Constellations", "Figures and names on or off."],
    ["Alt/az grid", "Lines of altitude and direction over the sky."],
    ["See through Earth", "Show what's below the horizon, dimmed."],
    ["Labels", "How many names to show: Few, Normal or Lots."],
    ["Pass alerts", "Alert two minutes before a visible ISS pass and start Find mode at rise."],
    ["Time zone, US daylight saving", "Your offset from UTC, and whether US daylight saving applies."],
    ["Sensor axis", "If the sky moves the wrong way on your unit, step through 0 / 90 / 180 / 270°."],
    ["Volume, Vibration", "Sounds 0 to 100 (0 is off); vibration on the T-Watch."],
    ["Brightness", "Screen brightness."],
    ["Exit to Launcher", "T-Watch with Launcher only: restart into the Launcher menu."],
    ["Restart", "Restart the watch."],
], [1.8 * inch, 4.9 * inch]))

# ================================================================== 10 time, place, orbits
s += chapter("10. Time, place and satellite data")
s.append(P("T-Watch Ultra", h2))
s += B(["Go outside once: the GPS sets the clock and your position, and both are saved.",
        "For fresh orbits, set up Wi-Fi (SETUP › Wi-Fi) and tap <b>Update orbits</b>. It downloads the ISS, Tiangong and Hubble from CelesTrak.",
        "Or put a file called <b>/skypoint/wifi.txt</b> on the microSD card with the network name on line 1 and the password on line 2.",
        "Offline, you can copy your own orbit file (standard three-line TLE format) to <b>/skypoint/tle.txt</b>."])
s.append(P("Waveshare C6", h2))
s.append(P("SETUP › Wi-Fi, then <b>Sync over Wi-Fi</b>. One tap sets the clock from internet time, finds your "
           "approximate position from your internet address (city level, using ip-api.com), and downloads orbits. "
           "If you'd rather not use the address lookup, pick a city under SETUP › Location; a chosen city is never "
           "overwritten."))
s.append(P("Android", h2))
s.append(P("The phone uses its own clock and location, and downloads orbits by itself every couple of days. "
           "Settings › Update orbits does it now."))
s.append(box("NOTE", "Orbit data goes stale",
             "Satellite orbits change a little every day, and the ISS raises its orbit from time to time. Predictions "
             "stay good to a minute or so for about a week. The ORBIT page shows the data's age."))

# ================================================================== 11 android
s += chapter("11. SkyPoint for Android")
s.append(P("The phone app has the same sky, the same catalogue and the same look, laid out for a phone. Hold the "
           "phone up with the back camera towards the sky; the screen shows what the camera would see."))
s.append(gallery([phone("android_01_sky_orion", 1.2 * inch, "Sky"), phone("android_06_tonight", 1.2 * inch, "Tonight"),
                  phone("android_07_orbit", 1.2 * inch, "Orbit"), phone("android_08_location", 1.2 * inch, "Location"),
                  phone("android_09_settings", 1.2 * inch, "Settings")], 1.35 * inch))
s.append(P("Getting around", h2))
s.append(table([
    ["Do this", "To get this"],
    ["Tap the bar at the bottom", "Sky, Tonight, Orbit, Location and Settings pages"],
    ["Move the phone", "The sky follows. North is automatic (compass, corrected to true north)."],
    ["Pinch", "Zoom between 12° and 110°"],
    ["Tap something in the sky", "Find it: an arrow guides you there"],
    ["Tap the card at the bottom (or its i)", "The info sheet for the locked object"],
    ["Tap the layers button (top left)", "Choose what's drawn on the sky"],
    ["Android Back", "Close a sheet, stop finding, then go back to the sky"],
], [2.4 * inch, 4.3 * inch]))
s.append(P("11.1 The info sheet", h2))
s.append(side(phone("android_04_info_iss", 1.35 * inch, "Info: ISS"),
              [P("Tap the card at the bottom of the sky, or any row on Tonight or Orbit, for everything SkyPoint knows "
                 "about the object:", body)] + B([
                  "where to look now, and when it rises, is highest and sets;",
                  "stars: distance, colour and temperature, brightness compared with the Sun, other names, and the year the light you see left it;",
                  "planets: distance, light travel time, angle from the Sun, size, day and year, moons;",
                  "the Moon: phase, age, next full and new Moon; satellites: height, speed, orbit, next passes;",
                  "a summary from <b>Wikipedia</b> with a picture and a <b>Read on Wikipedia</b> button (needs internet)."]) +
              [P("<b>Find in sky</b> at the top points you to it.", body)], picw=1.5 * inch))
s.append(P("11.2 Layers", h2))
s.append(side(phone("android_10_layers", 1.35 * inch, "Layers"),
              [P("The round button at the top left of the sky opens Layers. Each tile turns a group on or off: stars, "
                 "constellations, constellation art, planets, Sun and Moon, satellites, deep sky, labels and the grid.", body),
               P("Quick picks along the top show <b>Default</b> (everything except the drawings), <b>Everything</b>, "
                 "<b>Naked eye</b>, or one group only. <b>Hold a tile</b> to show only that layer. The button turns "
                 "amber while something is hidden.", body),
               P("<b>Constellation art</b> draws the figures (the Great Bear, the zodiac, Orion and others) over their "
                 "stars. The one you're pointing at lights up.", body)], picw=1.5 * inch))
s.append(P("11.3 Settings", h2))
s.append(table([
    ["Setting", "What it does"],
    ["Camera view", "Draws the sky over the live camera picture (asks for camera permission)."],
    ["Night mode", "Everything red, for dark-adapted eyes."],
    ["Layers", "Opens the Layers sheet."],
    ["Below the horizon", "Show what's under your feet, dimmed."],
    ["Drag to look", "Ignore the sensors and pan by touch, for indoors or planning."],
    ["Screen rotation", "Portrait, Auto (follows the phone and its rotation lock) or Landscape."],
    ["Field of view", "Zoom in steps; pinching does the same."],
    ["Align compass", "Fine-tune north on a star or planet if the compass is off (section 11.4)."],
    ["Volume, Vibration, Pass alerts", "Sounds, buzzes, and the two-minute warning before a visible pass."],
    ["Location", "GPS and network, or choose a city (search the list, tap one)."],
    ["Update orbits", "Download fresh satellite data now."],
], [1.8 * inch, 4.9 * inch]))
s.append(P("11.4 When the compass is off", h2))
s.append(P("Phone compasses are pulled by magnetic cases, cars, railings and steel beams. If a yellow "
           "<b>Compass unsure</b> message appears, wave the phone in a figure-8 a few times. If positions still look "
           "shifted sideways, use Settings › <b>Align compass</b>: pick something you can see, centre it, and tap "
           "<b>Lock it</b>. <b>Clear correction</b> removes it."))
s.append(P("11.5 Turning sideways", h2))
s.append(P("With Screen rotation on Auto or Landscape, the sky stays lined up whichever way you hold the phone. On "
           "Auto, a phone pointed almost straight up can't tell which way is up and may flip; choose Landscape if "
           "that happens."))
s.append(gallery([phone("android_11_landscape", 3.4 * inch * 412 / 880, None)[0]], 3.6 * inch))
s.append(P("Landscape view", cap))

# ================================================================== 12 troubleshooting
s += chapter("12. Troubleshooting")
s.append(table([
    ["Problem", "Try this"],
    ["Black screen after installing (C6)", "Install again with Erase device ticked. Use the Beta 1 C6 image, which is built for the board's 4 MB flash."],
    ["T-Watch stuck at boot, serial shows FFAT INIT FAILED", "Fixed in Beta 1. Install the Beta 1 image."],
    ["The computer can't see the watch", "Use a data cable, try another USB port. On the C6 hold BOOT while plugging in."],
    ["The sky turns the wrong way (watch)", "SETUP › Sensor axis: step through 0 / 90 / 180 / 270° until moving the watch moves the sky correctly, then align again."],
    ["Everything is shifted sideways (watch)", "Align north again (section 5.4). Re-align every half hour on a long session."],
    ["Everything is shifted sideways (phone)", "Wave in a figure-8, move away from metal, or Settings › Align compass."],
    ["No GPS fix (T-Watch)", "Go outside with a clear view of the sky and wait up to 5 minutes. Buildings and trees slow it down."],
    ["Wrong time (C6)", "SETUP › Sync over Wi-Fi. Check SETUP › Time zone and US daylight saving."],
    ["ISS times look wrong", "The orbit data is old: SETUP › Update orbits (needs Wi-Fi). The ORBIT page shows its age."],
    ["Update orbits fails", "Check the Wi-Fi name and password. The watches need a 2.4 GHz network."],
    ["No Wikipedia summary (phone)", "The phone needs internet. The rest of the info sheet works offline."],
    ["Phone: \"App not installed\"", "An older SkyPoint signed with a different key is in the way: uninstall it, then install again."],
    ["Phone: download opens as text", "The web host serves .apk files with the wrong type; download from the GitHub Releases page instead."],
], [2.2 * inch, 4.5 * inch]))

# ================================================================== 13 care
s += chapter("13. Safety and responsible use")
s += B(["<b>Never look at the Sun</b>, directly or through binoculars, a telescope or a camera.",
        "Watch where you're walking. Looking up at a screen in the dark is a good way to trip; stand still while you look.",
        "Stay off roads, rail lines and private land, and keep your sound low near others.",
        "Night vision (red) keeps your eyes dark-adapted and is kinder to people around you.",
        "Batteries: the watches drain faster with the screen bright. Lower Brightness for a long night out.",
        "Location: the C6's Sync over Wi-Fi sends your internet address to ip-api.com to estimate your city. Pick a "
        "city instead if you'd rather not. The phone app sends nothing anywhere except requests for orbit data "
        "(CelesTrak) and, when you open an info sheet, the object's name to Wikipedia."])

# ================================================================== quick reference
s += chapter("Quick-reference card")
qr = Table([[
    [P("Watch", h3)] + B(["Swipe: next page", "Tap sky: zoom 90 / 60 / 35 / 20°", "Long-press sky: align north",
                         "Tap a row: Find mode, tap banner to stop", "Button click: next page", "Button double-click: night vision"]),
    [P("Phone", h3)] + B(["Move the phone: the sky follows", "Pinch: zoom", "Tap an object: find it", "Tap the card: info sheet",
                         "Top-left button: Layers", "Back: close, stop, back to the sky"]),
]], colWidths=[3.35 * inch, 3.35 * inch])
qr.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("BOX", (0, 0), (-1, -1), 0.8, ACC),
                        ("LINEAFTER", (0, 0), (0, 0), 0.5, RULE), ("LEFTPADDING", (0, 0), (-1, -1), 10),
                        ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 8)]))
s.append(qr)
s.append(Spacer(1, 10))
s.append(table([
    ["First night out", ""],
    ["1. Time and place", "T-Watch: GPS LOCKED. C6: Sync over Wi-Fi. Phone: allow location."],
    ["2. North", "Watches: long-press the sky and align on something you can see."],
    ["3. Look", "Hold it up and move slowly. The reticle locks on."],
    ["4. Hunt", "Tonight or Orbit: tap a row, follow the arrow."],
], [1.6 * inch, 5.1 * inch]))

# ================================================================== glossary
s += chapter("Glossary")
s.append(table([
    ["Term", "Meaning"],
    ["Altitude (ALT)", "Height above the horizon in degrees: 0° on the horizon, 90° straight overhead."],
    ["Azimuth (AZ)", "Direction around the horizon in degrees: 0° north, 90° east, 180° south, 270° west."],
    ["Magnitude", "Brightness. Smaller is brighter: Venus about −4, Sirius −1.4, the faintest stars in a dark sky about 6."],
    ["Light year (ly)", "The distance light travels in a year, about 9.5 trillion km."],
    ["AU", "Astronomical unit: the average Earth–Sun distance, about 150 million km."],
    ["Constellation", "One of 88 official areas of the sky, and the figure its stars make."],
    ["Messier object", "One of 109 galaxies, nebulae and clusters listed by Charles Messier in the 1700s (M1 to M110)."],
    ["Pass", "A satellite crossing your sky from horizon to horizon, usually 4 to 10 minutes."],
    ["Visible pass", "A pass while the satellite is sunlit and your sky is dark, so you can see it."],
    ["TLE", "Two-line element set: the standard format for a satellite's orbit, from CelesTrak."],
    ["Astronomical dark", "When the Sun is more than 18° below the horizon and the sky is fully dark."],
    ["True north / magnetic north", "The direction to the North Pole, and where a compass points. They differ by a few degrees depending on where you are; the phone app corrects for it."],
    ["Field of view", "How much sky the screen shows, in degrees from top to bottom."],
], [1.7 * inch, 5.0 * inch]))

# ================================================================== appendix A
s += chapter("Appendix A. Technical summary")
s.append(table([
    ["", "T-Watch Ultra", "Waveshare C6", "Android"],
    ["Chip", "ESP32-S3, 8 MB PSRAM", "ESP32-C6, no PSRAM, 4 MB flash", "Any Android 8.0+ phone"],
    ["Display", "2.06\" AMOLED 410×502", "2.06\" AMOLED 410×502 (CO5300)", "Phone screen"],
    ["Orientation", "BHI260AP on-chip fusion", "QMI8658 + Mahony filter in firmware", "Rotation vector + true-north correction"],
    ["Time", "GPS, kept in the PCF85063 RTC", "Internet time (NTP), kept in the RTC", "Phone clock"],
    ["Position", "GPS (MIA-M10Q)", "Internet address lookup, or a city", "GPS / network"],
    ["Sound", "MAX98357A amplifier", "ES8311 codec", "Phone speaker"],
    ["Vibration", "DRV2605", "None", "Phone"],
], [1.1 * inch, 1.85 * inch, 1.95 * inch, 1.8 * inch]))
s.append(P("Accuracy", h2))
s.append(P("The watch astronomy is checked against PyEphem: the Sun, Moon and inner planets within 1 arcminute, "
           "Jupiter and Saturn within 5 arcminutes, rise and set times within about 2 minutes, and ISS positions and "
           "passes to the second. The Android app uses Astronomy Engine and satellite.js (SGP4). On the watches, how "
           "well the sky lines up with the real one depends mostly on your alignment; on phones, on the compass."))
s.append(P("Catalogue", h2))
s.append(P("1,675 stars (HYG v4.1), 88 constellation figures (Stellarium modern sky culture), 109 Messier objects, "
           "the Sun, the Moon, seven planets, and the ISS, Tiangong and Hubble."))

# ================================================================== appendix B
s += chapter("Appendix B. Credits and licences")
s.append(P("SkyPoint is free software under the GNU General Public License v3. It uses:"))
s += B(["Stars: HYG Database v4.1 (astronexus), CC BY-SA 4.0",
        "Constellation figures: Stellarium \"modern\" sky culture, GPL-2.0-or-later",
        "Constellation drawings (Android): Johan Meuris for Stellarium, Free Art License 1.3",
        "Planet elements: E. M. Standish, JPL. Sun and Moon: J. Meeus, <i>Astronomical Algorithms</i>",
        "SGP4: Hopperpop/Sgp4-Library (MIT), satellite.js (MIT); Astronomy Engine by Don Cross (MIT)",
        "Orbital elements: CelesTrak. Approximate location (C6): ip-api.com",
        "Fonts: Orbitron, Rajdhani, Share Tech Mono, Noto Sans, Saira Semi Condensed (SIL Open Font License 1.1)",
        "LVGL (MIT), LilyGoLib (MIT), GFX Library for Arduino (BSD)"])
s.append(P("The full notices are in THIRD_PARTY_NOTICES.md and the licenses folder of the source code. "
           "Wikipedia text shown in the Android app is CC BY-SA 4.0.", small))
s.append(Spacer(1, 14))
s.append(P("SkyPoint  ·  Web flasher: skirf-labs.tech/SKYPOINT  ·  Source: github.com/SKIrf26/SKYPOINT  ·  SKIrf Labs", small))


def build(out):
    doc = Doc(out)
    doc.multiBuild(s)


def booklet(src, dst, rotate_back):
    """Saddle-stitch imposition: 2 pages per landscape side; fold the stack in half."""
    from pypdf import PdfReader, PdfWriter, Transformation, PageObject
    r = PdfReader(src)
    pages = list(r.pages)
    W, H = float(pages[0].mediabox.width), float(pages[0].mediabox.height)
    while len(pages) % 4:
        pages.append(None)
    n = len(pages)
    w = PdfWriter()
    order = []
    for k in range(n // 4):
        order.append((n - 1 - 2 * k, 2 * k, False))        # front: last | first
        order.append((2 * k + 1, n - 2 - 2 * k, True))     # back
    for left, right, back in order:
        sheet = PageObject.create_blank_page(width=2 * W, height=H)
        for idx, x in ((left, 0), (right, W)):
            if pages[idx] is None:
                continue
            sheet.merge_transformed_page(pages[idx], Transformation().translate(x, 0))
        if back and rotate_back:
            sheet.rotate(180)
        w.add_page(sheet)
    w.add_metadata({"/Title": "SkyPoint User Guide (booklet)", "/Author": "SKIrf Labs"})
    with open(dst, "wb") as f:
        w.write(f)


if __name__ == "__main__":
    out = os.path.join(DOCS, NAME + ".pdf")
    build(out)
    # the same booklet prints correctly on either kind of duplex printer:
    booklet(out, os.path.join(DOCS, NAME + "_BOOKLET_flip-short-edge.pdf"), rotate_back=False)
    booklet(out, os.path.join(DOCS, NAME + "_BOOKLET_flip-long-edge.pdf"), rotate_back=True)
    print("wrote", out, "and two booklets")
