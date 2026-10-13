#!/usr/bin/env python3
"""Build the demo video and GIF from the scenes tools/demo/capture.py recorded.

  tools/demo/build_video.py            (ffmpeg, rsvg-convert, Noto Sans CJK JP)

media/raw/<scene>.mp4 (1920x1216 emulator screen, variable frame rate) -> cropped to Kodi's
1920x1080, 30 fps, a caption per scene, title/feature/end cards ->
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
    ("channels", 1.0, 14.5, "チャンネル → 地デジ・BS・CS", "Channels from EPGStation, with logos and groups"),
    ("live", 1.0, 26.0, "ライブTV → 視聴・チャンネル切り替え", "Live TV: watch and switch channels"),
    ("guide", 1.0, 17.5, "番組表 → ジャンル色・番組詳細", "Guide with genre colours and full descriptions"),
    ("record", 0.8, 29.5, "番組表 → 録画予約・シリーズ録画", "Record a programme, or the whole series (a rule)"),
    ("rules", 0.8, 12.8, "タイマールール・タイマー → EPGStation の予約", "Rules and reservations, straight from EPGStation"),
    ("recordings", 1.0, 27.5, "録画 → 再生・シーク", "Play recordings, seek anywhere"),
    ("chase", 1.0, 19.2, "録画中の番組 → 追っかけ再生", "Watch a recording while it is still recording"),
]
# GIF highlights: (scene, start, end) in the trimmed clip
GIF = [("live", 0.5, 8.0), ("guide", 1.0, 8.0), ("record", 0.5, 16.0), ("chase", 3.0, 12.0)]
CARD_SECONDS = {"title": 4.0, "features": 7.0, "end": 4.0}


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
                    "#9fb3c8"))
    elif kind == "features":
        items = [("ライブTV（無変換・エンコード済みモード）", "Live TV, unconverted or any EPGStation mode"),
                 ("番組表：ARIB ジャンル・番組詳細・番組変更の反映", "Guide: ARIB genres, full texts, EPG changes pushed"),
                 ("録画予約・シリーズ録画・EPGStation のルール", "Reservations, series rules, EPGStation's own rules"),
                 ("録画の再生・削除・録画中の追っかけ再生", "Recordings: play, delete, watch while recording"),
                 ("Kodi 21：Linux・Windows・Android", "Kodi 21 on Linux, Windows and Android")]
        body = t(960, 250, 64, "できること", ACCENT, 700)
        for i, (ja, en) in enumerate(items):
            y = 380 + i * 120
            body += (f'<circle cx="410" cy="{y - 16}" r="10" fill="{RED}"/>'
                     + t(450, y, 44, ja, anchor="start") + t(450, y + 44, 26, en, "#9fb3c8", anchor="start"))
    else:
        body = (t(960, 480, 96, "pvr.epgstation", ACCENT, 700)
                + t(960, 580, 44, "Kodi 21 · Linux / Windows / Android · GPL-2.0")
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
               f"[0:v]crop=1920:1080:0:136,fps=30,setpts=PTS-STARTPTS[v];"
               f"[1:v]format=rgba,fade=in:st=0.3:d=0.4:alpha=1[c];"
               f"[v][c]overlay=0:0:shortest=1,fade=in:st=0:d=0.3,fade=out:st={d - 0.3:.2f}:d=0.3",
               "-an", *ENC, parts[name])
    order = ["title"] + [s[0] for s in SCENES[:2]] + ["features"] + [s[0] for s in SCENES[2:]] + ["end"]
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
           "fps=8,scale=960:540:flags=lanczos,split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];"
           "[b][p]paletteuse=dither=bayer:bayer_scale=3:diff_mode=rectangle", os.path.join(OUT, "demo.gif"))
    for f in (mp4, os.path.join(OUT, "demo.gif")):
        out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration,size", "-of", "csv=p=0", f],
                             capture_output=True, text=True).stdout.strip()
        print(os.path.relpath(f, ROOT), out)
    print("work files:", tmp)


if __name__ == "__main__":
    main()
