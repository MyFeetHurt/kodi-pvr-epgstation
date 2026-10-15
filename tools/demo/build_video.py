#!/usr/bin/env python3
"""Build the demo video and GIF from the scenes tools/demo/capture.py recorded.

  tools/demo/build_video.py            (ffmpeg, rsvg-convert, Noto Sans CJK JP)

media/raw/<scene>.mp4 (Kodi's 1920x1080: x11grab of Kodi 22 under Xvfb; 1920x1216 Android emulator
takes are cropped to it) and media/raw/<still>.png -> 30 fps, a caption per scene, title, feature,
"tested" and end cards ->
media/pvr-epgstation-demo.mp4 (silent) and media/demo.gif (960x540, highlights).
"""
import os
import subprocess
import tempfile
from xml.sax.saxutils import escape

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
RAW = os.path.join(ROOT, "media", "raw")
OUT = os.path.join(ROOT, "media")
FONT = "Noto Sans CJK JP"
NAVY, ACCENT, RED = "#0f1925", "#1fb4e6", "#e8293a"

# scene, trim start, trim end (seconds of the raw clip), caption (Japanese "where -> what"), English
SCENES = [
    ("sysinfo", 9.5, 16.0, "Kodi 22 → EPGStation の状態・空き容量", 'Kodi 22 "Piers": backend status and free disk space'),
    ("channels", 1.5, 11.5, "チャンネル → 地デジ・BS・CS", "Channels from EPGStation, with logos and groups"),
    ("live", 1.5, 31.5, "ライブTV → 視聴・チャンネル切り替え", "Live TV: watch and switch channels"),
    ("guide", 2.0, 14.5, "番組表 → ジャンル色・番組詳細", "Guide with genre colours and full descriptions"),
    ("series", 2.0, 25.5, "番組表 → シリーズ録画（数秒で予約に反映）", "Record a series: its episodes show up within seconds"),
    ("chase", 1.5, 17.0, "録画中の番組 → 追っかけ再生・シーク", "Watch a recording while it records, and seek in it"),
]
# Screenshots shown as scenes: (name, seconds, caption ja, en); media/raw/<name>.png, any size.
STILLS = [
    ("windows", 5.0, "Windows でも Kodi 22", "Kodi 22 on Windows 11 (and Linux, Android)"),
]
# Order of the video.
ORDER = ["title", "sysinfo", "channels", "features", "live", "guide", "series", "chase", "windows", "tested", "end"]
# GIF highlights: (scene, start, end) in the trimmed clip
GIF = [("live", 3.0, 8.0), ("series", 4.0, 14.0), ("chase", 9.5, 15.5)]
CARD_SECONDS = {"title": 4.0, "features": 7.0, "tested": 6.0, "end": 4.0}


def svg_card(kind):
    bg = (f'<rect width="1920" height="1080" fill="{NAVY}"/>'
          f'<g fill="{ACCENT}" opacity="0.07">'
          + "".join(f'<rect x="{x}" y="{y}" width="{w}" height="70" rx="10"/>'
                    for x, y, w in [(140, 120, 380), (540, 120, 640), (1200, 120, 580), (140, 210, 700),
                                    (860, 210, 920), (140, 870, 520), (680, 870, 560), (1260, 870, 520)])
          + "</g>")
    t = lambda x, y, size, text, fill="#ffffff", weight=400, anchor="middle": (
        f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" fill="{fill}" '
        f'text-anchor="{anchor}">{escape(text)}</text>')
    if kind == "title":
        body = (t(960, 470, 120, "pvr.epgstation", ACCENT, 700)
                + t(960, 580, 54, "EPGStation を Kodi のライブTV・番組表・録画で")
                + t(960, 650, 32, "A Kodi PVR client for EPGStation 2.x — Live TV, Guide, Recordings and Timers",
                    "#9fb3c8")
                + t(960, 720, 32, 'Kodi 21 "Omega" · Kodi 22 "Piers"', ACCENT))
    elif kind == "features":
        items = [("Kodi 21・Kodi 22：Linux・Windows・Android", 'Kodi 21 "Omega" and Kodi 22 "Piers" on Linux, Windows and Android'),
                 ("ライブTV（無変換・エンコード済みモード）", "Live TV, unconverted or any EPGStation mode"),
                 ("番組表：ARIB ジャンル・番組詳細・番組変更の反映", "Guide: ARIB genres, full texts, EPG changes pushed"),
                 ("録画予約・シリーズ録画・EPGStation のルール", "Reservations, series rules, EPGStation's own rules"),
                 ("録画の再生・削除・録画中の追っかけ再生とシーク", "Recordings: play, delete, watch and seek while recording"),
                 ("録画先の空き容量を Kodi に表示", "Free disk space of EPGStation's recording folders")]
        body = t(960, 220, 64, "できること", ACCENT, 700)
        for i, (ja, en) in enumerate(items):
            y = 330 + i * 110
            body += (f'<circle cx="410" cy="{y - 16}" r="10" fill="{RED}"/>'
                     + t(450, y, 44, ja, anchor="start") + t(450, y + 44, 26, en, "#9fb3c8", anchor="start"))
    elif kind == "tested":
        items = [("Kodi 21.2・21.3・22.0 RC1 / Linux・Windows 11", "Run on Kodi 21.2, 21.3 and 22.0 RC1, Linux and Windows 11"),
                 ("EPGStation 2.10 に対する自動テスト", "Unit tests, plus live checks against a real EPGStation 2.10"),
                 ("6 プラットフォーム × Kodi 2 バージョンをビルド", "Built and checked for 6 platforms x 2 Kodi versions"),
                 ("回線断・録画終了・チャンネル切替でチューナーを解放", "Outage, end of recording, channel change: tuners freed")]
        body = t(960, 260, 64, "テスト済み", ACCENT, 700)
        for i, (ja, en) in enumerate(items):
            y = 400 + i * 130
            body += (f'<circle cx="370" cy="{y - 16}" r="10" fill="{RED}"/>'
                     + t(410, y, 44, ja, anchor="start") + t(410, y + 44, 26, en, "#9fb3c8", anchor="start"))
    else:
        body = (t(960, 480, 96, "pvr.epgstation", ACCENT, 700)
                + t(960, 580, 44, "Kodi 21 · 22 · Linux / Windows / Android · GPL-2.0")
                + t(960, 650, 30, "EPGStation 2.x のための Kodi PVR クライアント", "#9fb3c8"))
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="1080">{bg}{body}</svg>'


def svg_caption(ja, en):
    # Dark box bottom left, like the other demo videos: Japanese "where -> what", small English below.
    width = max(len(ja) * 46, len(en) * 15) + 80
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="1080">'
            f'<rect x="40" y="880" width="{width}" height="150" rx="14" fill="#000000" fill-opacity="0.78"/>'
            f'<rect x="40" y="880" width="8" height="150" fill="{ACCENT}"/>'
            f'<text x="80" y="948" font-family="{FONT}" font-size="46" font-weight="700" fill="#ffffff">'
            f'{escape(ja)}</text>'
            f'<text x="80" y="1000" font-family="{FONT}" font-size="28" fill="#c8d6e5">{escape(en)}</text></svg>')


def png(svg, path):
    with open(path + ".svg", "w") as f:
        f.write(svg)
    subprocess.run(["rsvg-convert", "-o", path, path + ".svg"], check=True)


def crop(path):
    """crop filter for a raw take: the Android emulator's 1920x1216 screen -> Kodi's 1920x1080."""
    h = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=height",
                        "-of", "csv=p=0", path], capture_output=True, text=True).stdout.strip()
    return "crop=1920:1080:0:136," if h == "1216" else ""


def ffmpeg(*args):
    subprocess.run(["ffmpeg", "-v", "error", "-y", *args], check=True)


ENC = ["-c:v", "libx264", "-preset", "slow", "-crf", "26", "-pix_fmt", "yuv420p", "-r", "30"]


def main():
    tmp = tempfile.mkdtemp(prefix="pvr-demo-")
    parts = {}
    for kind, secs in CARD_SECONDS.items():
        png(svg_card(kind), f"{tmp}/{kind}.png")
        parts[kind] = f"{tmp}/{kind}.mp4"
        ffmpeg("-loop", "1", "-t", str(secs), "-i", f"{tmp}/{kind}.png",
               "-vf", "fade=in:st=0:d=0.4,fade=out:st=%.1f:d=0.4" % (secs - 0.4), *ENC, parts[kind])
    for name, start, end, ja, en in SCENES:
        png(svg_caption(ja, en), f"{tmp}/cap_{name}.png")
        d = end - start
        parts[name] = f"{tmp}/{name}.mp4"
        ffmpeg("-ss", str(start), "-to", str(end), "-i", f"{RAW}/{name}.mp4", "-loop", "1", "-i", f"{tmp}/cap_{name}.png",
               "-filter_complex",
               f"[0:v]{crop(f'{RAW}/{name}.mp4')}fps=30,setpts=PTS-STARTPTS[v];"
               f"[1:v]format=rgba,fade=in:st=0.3:d=0.4:alpha=1[c];"
               f"[v][c]overlay=0:0:shortest=1,fade=in:st=0:d=0.3,fade=out:st={d - 0.3:.2f}:d=0.3",
               "-an", *ENC, parts[name])
    for name, secs, ja, en in STILLS:
        png(svg_caption(ja, en), f"{tmp}/cap_{name}.png")
        parts[name] = f"{tmp}/{name}.mp4"
        # The screenshot on the card background, as large as fits above the caption.
        ffmpeg("-loop", "1", "-t", str(secs), "-i", f"{tmp}/title.png", "-loop", "1", "-t", str(secs),
               "-i", f"{RAW}/{name}.png", "-loop", "1", "-i", f"{tmp}/cap_{name}.png", "-filter_complex",
               "[1:v]scale=-2:820[s];[0:v]drawbox=c=0x0f1925:t=fill[b];[b][s]overlay=(W-w)/2:40[v];"
               f"[2:v]format=rgba[c];[v][c]overlay=0:0,fade=in:st=0:d=0.3,fade=out:st={secs - 0.3:.2f}:d=0.3",
               "-t", str(secs), *ENC, parts[name])
    order = ORDER
    with open(f"{tmp}/list.txt", "w") as f:
        f.writelines(f"file '{parts[p]}'\n" for p in order)
    mp4 = os.path.join(OUT, "pvr-epgstation-demo.mp4")
    ffmpeg("-f", "concat", "-safe", "0", "-i", f"{tmp}/list.txt", "-c", "copy", "-movflags", "+faststart", mp4)

    gif_parts = [parts["title"]]
    for i, (name, start, end) in enumerate(GIF):
        p = f"{tmp}/gif{i}.mp4"
        ffmpeg("-ss", str(start), "-to", str(end), "-i", parts[name], *ENC, p)
        gif_parts.append(p)
    with open(f"{tmp}/gif.txt", "w") as f:
        f.writelines(f"file '{p}'\n" for p in gif_parts)
    ffmpeg("-f", "concat", "-safe", "0", "-i", f"{tmp}/gif.txt", "-vf",
           "fps=6,scale=854:480:flags=lanczos,split[a][b];[a]palettegen=max_colors=64:stats_mode=diff[p];"
           "[b][p]paletteuse=dither=bayer:bayer_scale=3:diff_mode=rectangle", os.path.join(OUT, "demo.gif"))
    for f in (mp4, os.path.join(OUT, "demo.gif")):
        out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration,size", "-of", "csv=p=0", f],
                             capture_output=True, text=True).stdout.strip()
        print(os.path.relpath(f, ROOT), out)
    print("work files:", tmp)


if __name__ == "__main__":
    main()
