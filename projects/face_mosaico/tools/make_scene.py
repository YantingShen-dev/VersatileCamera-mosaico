"""Rebuild the authored 480x480 scene and its minimal open-source font subset."""

import json
import os
from pathlib import Path

from fontTools import subset
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "components/app_face/ui"
TEXT = "人脸识别本地离线正在加载模型请正对镜头尚未录入人员新面孔管理返回删除取消确认无法恢复记录已保存识别到未录入的人脸未检测到人脸请靠近一些多人入镜请单人录入请保持清晰重新尝试存储失败摄像头不可用模型加载失败正在处理录入成功已经录入最多五人已满已删除暂无记录已录入耗时相似度此操作只删除所选人员数据仅在本机保存模拟摄像头等待已识别0123456789 /:+-.()%msFACE ID"


def main():
    UI.mkdir(parents=True, exist_ok=True)
    (UI / "fonts").mkdir(exist_ok=True)
    (UI / "assets").mkdir(exist_ok=True)
    font_source = Path(os.environ["FACE_FONT_SOURCE"])
    font = subset.load_font(str(font_source), subset.Options())
    sub = subset.Subsetter()
    extra_text = "".join(p.read_text(encoding="utf-8") for p in (ROOT / "components/app_face").glob("*.c"))
    charset = "".join(sorted(set(TEXT + extra_text)))
    sub.populate(text=charset)
    sub.subset(font)
    subset.save_font(font, str(UI / "fonts/FaceSans.ttf"), subset.Options())
    (UI / "fonts/OFL.txt").write_bytes(font_source.with_name("OFL.txt").read_bytes())
    Image.new("RGB", (440, 264), "#263438").save(UI / "assets/preview.png")
    objects = []

    def obj(kind, x, y, w, h, parent=-1, **kw):
        objects.append(dict(type=kind, parent=parent, x=x, y=y, w=w, h=h, **kw))
        return len(objects) - 1

    def label(text, x, y, w, h, size=18, color="#E5EBEB", parent=-1, bind=None):
        kw = dict(text=text, font_size=size, fg_color=color)
        if bind:
            kw.update(name=bind, bind=bind, font_charset=charset)
        return obj("label", x, y, w, h, parent, **kw)

    def button(name, text, x, y, w, h, parent=-1, primary=False):
        b = obj(
            "button",
            x,
            y,
            w,
            h,
            parent,
            name=name,
            callback=name,
            radius=8,
            bg_color="#82E4B2" if primary else "#2C3B40",
        )
        obj(
            "label",
            0,
            0,
            w,
            h,
            b,
            text=text,
            font_size=18,
            text_align="center",
            fg_color="#11291E" if primary else "#E5EBEB",
        )
        return b

    def overlay(name):
        return obj(
            "container", 0, 0, 480, 480, bg_color="#10191C", name=name, bind=name, bind_target="visible", hidden=True
        )

    label("人脸识别", 20, 12, 300, 36, 26)
    label("FACE ID", 21, 48, 200, 18, 12, "#91A4AA")
    obj("container", 368, 22, 92, 28, bg_color="#213C35", radius=6)
    label("本地 / 离线", 377, 25, 85, 23, 13, "#8AE2B8")
    obj("image", 20, 78, 440, 264, name="preview", bind="preview", image="assets/preview.png")
    label("正在加载模型", 20, 351, 440, 27, 19, bind="status")
    label("已录入 0 / 5 人", 20, 382, 220, 22, 14, "#91A4AA", bind="count")
    label("耗时 -- ms", 305, 382, 155, 22, 14, "#91A4AA", bind="timing")
    button("enroll", "+ 录入新面孔", 20, 414, 280, 48, primary=True)
    button("manage", "管理人脸", 312, 414, 148, 48)
    manager = overlay("manager")
    label("管理人脸", 20, 20, 440, 40, 26, parent=manager)
    label("数据仅在本机保存", 20, 66, 440, 28, 16, "#91A4AA", manager)
    for i in range(5):
        y = 108 + i * 52
        label(f"人员 0{i + 1} / 暂无记录", 28, y + 8, 310, 28, 18, parent=manager, bind=f"person{i + 1}")
        button(f"delete{i + 1}", "删除", 366, y, 92, 44, manager)
    label("", 20, 374, 440, 28, 17, "#8AE2B8", manager, "manage_status")
    button("back", "返回", 20, 414, 440, 48, manager)
    confirm = overlay("confirm")
    label("删除人员记录", 28, 100, 424, 50, 26, parent=confirm)
    label("", 28, 166, 424, 40, 22, parent=confirm, bind="confirm_text")
    label("此操作只删除所选人员", 28, 224, 424, 30, 17, "#91A4AA", confirm)
    button("cancel", "取消", 20, 350, 212, 60, confirm)
    button("confirm_delete", "确认删除", 248, 350, 212, 60, confirm, True)
    scene = dict(
        screen="face",
        w=480,
        h=480,
        screen_bg="#10191C",
        font="fonts/FaceSans.ttf",
        default_font_size=18,
        swipe=False,
        objects=objects,
    )
    (UI / "main.json").write_text(json.dumps(scene, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
